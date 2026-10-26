#include <Axle/RHIPhysicsDebugRenderer.hpp>
#include <Axle/Log.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/RHI/RHIPipeline.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace Axle {

    namespace Internal {
        JoltInitHelper::JoltInitHelper() {
            if (!PhysicsEngine::IsInitialized()) {
                PhysicsEngine::Init();
            }
        }
    }

    RHIPhysicsDebugRenderer::RHIPhysicsDebugRenderer()
        : Internal::JoltInitHelper(), JPH::DebugRendererSimple()
    {
        m_LineVertices.reserve(MAX_VERTICES_PER_BATCH);
    }

    RHIPhysicsDebugRenderer::~RHIPhysicsDebugRenderer() {
        Shutdown();
    }

    bool RHIPhysicsDebugRenderer::Init(RHI::RHIDevice& device) {
        if (m_Initialized) return true;
        m_Device = &device;

        if (!CompileShaders(device)) {
            AXLE_CORE_ERROR("RHIPhysicsDebugRenderer: Failed to compile PhysicsWireframe.hlsl");
            return false;
        }

        if (!CreatePipelineLayout(device)) {
            AXLE_CORE_ERROR("RHIPhysicsDebugRenderer: Failed to create pipeline layout");
            return false;
        }

        if (!CreateBuffers(device)) {
            AXLE_CORE_ERROR("RHIPhysicsDebugRenderer: Failed to allocate GPU line buffers");
            return false;
        }

        // Pre-cache default SDR and HDR line pipelines (with and without depth test)
        m_PipelineSDR_Depth   = GetOrCreatePipeline(nri::Format::RGBA8_UNORM, nri::Format::D32_SFLOAT, 1, true);
        m_PipelineSDR_NoDepth = GetOrCreatePipeline(nri::Format::RGBA8_UNORM, nri::Format::D32_SFLOAT, 1, false);
        m_PipelineHDR_Depth   = GetOrCreatePipeline(nri::Format::RGBA16_SFLOAT, nri::Format::D32_SFLOAT, 1, true);
        m_PipelineHDR_NoDepth = GetOrCreatePipeline(nri::Format::RGBA16_SFLOAT, nri::Format::D32_SFLOAT, 1, false);

        if (!m_PipelineSDR_Depth || !m_PipelineSDR_NoDepth) {
            AXLE_CORE_ERROR("RHIPhysicsDebugRenderer: Failed to create default graphics pipelines");
            return false;
        }

        m_Initialized = true;
        AXLE_CORE_INFO("RHIPhysicsDebugRenderer: Successfully initialized line wireframe pipeline (Max: {0} vertices).", MAX_VERTICES_PER_BATCH);
        return true;
    }

    void RHIPhysicsDebugRenderer::Shutdown() {
        if (!m_Initialized) return;

        m_VertexBuffer.Shutdown();
        m_ConstantBuffer.Shutdown();
        m_PipelineLayout.Shutdown();

        m_VertexShader = {};
        m_PixelShader = {};

        m_PipelineSDR_Depth = nullptr;
        m_PipelineSDR_NoDepth = nullptr;
        m_PipelineHDR_Depth = nullptr;
        m_PipelineHDR_NoDepth = nullptr;

        m_LineVertices.clear();
        m_Device = nullptr;
        m_Initialized = false;
        m_InBatch = false;
    }

    bool RHIPhysicsDebugRenderer::CompileShaders(RHI::RHIDevice& device) {
        std::filesystem::path shaderPath = "Engine/Resources/Shaders/PhysicsWireframe.hlsl";
        if (!std::filesystem::exists(shaderPath)) {
            shaderPath = "../Engine/Resources/Shaders/PhysicsWireframe.hlsl";
        }
        if (!std::filesystem::exists(shaderPath)) {
            shaderPath = "../../Engine/Resources/Shaders/PhysicsWireframe.hlsl";
        }
        if (!std::filesystem::exists(shaderPath)) {
            AXLE_CORE_ERROR("RHIPhysicsDebugRenderer: Shader not found at '{0}'", shaderPath.string());
            return false;
        }

        std::ifstream file(shaderPath);
        if (!file.is_open()) return false;
        std::stringstream ss;
        ss << file.rdbuf();
        std::string shaderSource = ss.str();

        auto& compiler = RHI::RHIShaderCompiler::Get();

        // Vertex Shader
        RHI::RHIShaderCompileDesc vsDesc;
        vsDesc.Source = shaderSource;
        vsDesc.SourcePath = shaderPath.string();
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHI::RHIShaderStage::Vertex;
        vsDesc.HLSLVersion = "2021";
        vsDesc.ShaderModel = "6_0";
        vsDesc.Format = (device.GetCapabilities().ActiveBackend == RHI::RHIType::Vulkan)
            ? RHI::RHIShaderFormat::SPIRV : RHI::RHIShaderFormat::DXIL;
        vsDesc.Optimization = true;

        auto vsResult = compiler.Compile(vsDesc);
        if (!vsResult.Success) {
            AXLE_CORE_ERROR("RHIPhysicsDebugRenderer VS Error: {0}", vsResult.ErrorMessage);
            return false;
        }
        m_VertexShader = vsResult.Bytecode;

        // Pixel Shader
        RHI::RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHI::RHIShaderStage::Fragment;

        auto psResult = compiler.Compile(psDesc);
        if (!psResult.Success) {
            AXLE_CORE_ERROR("RHIPhysicsDebugRenderer PS Error: {0}", psResult.ErrorMessage);
            return false;
        }
        m_PixelShader = psResult.Bytecode;

        return true;
    }

    bool RHIPhysicsDebugRenderer::CreatePipelineLayout(RHI::RHIDevice& device) {
        (void)device;
        // Root Constant Buffer at b0, space 0
        nri::RootDescriptorDesc rootDesc = {};
        rootDesc.registerIndex = 0;
        rootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDesc.shaderStages = nri::StageBits::ALL;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootDescriptors = &rootDesc;
        layoutDesc.rootDescriptorNum = 1;
        layoutDesc.shaderStages = nri::StageBits::ALL;

        return m_PipelineLayout.Init(layoutDesc);
    }

    bool RHIPhysicsDebugRenderer::CreateBuffers(RHI::RHIDevice& device) {
        (void)device;
        // 256-byte CBV for ViewProjection and DepthBiasParams
        if (!m_ConstantBuffer.Init(RHI::RHIBufferDesc::Constant(sizeof(RHIPhysicsDebugConstants), RHI::RHIMemoryLocation::HostUpload, "PhysicsDebug_CBV"))) {
            return false;
        }

        // Dynamic Line Vertex Buffer (28 bytes * 100,000 vertices = 2,800,000 bytes)
        uint64_t vbSize = sizeof(RHIDebugLineVertex) * MAX_VERTICES_PER_BATCH;
        if (!m_VertexBuffer.Init(RHI::RHIBufferDesc::Vertex(vbSize, RHI::RHIMemoryLocation::HostUpload, "PhysicsDebug_LineVB"))) {
            return false;
        }

        return true;
    }

    nri::Pipeline* RHIPhysicsDebugRenderer::GetOrCreatePipeline(nri::Format colorFormat,
                                                               nri::Format depthFormat,
                                                               uint8_t sampleCount,
                                                               bool depthTest)
    {
        RHI::RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PipelineLayout;
        gfxDesc.Shaders = { m_VertexShader, m_PixelShader };
        gfxDesc.InputAssembly.Topology = nri::Topology::LINE_LIST;

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = depthTest;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS_EQUAL;
        gfxDesc.DepthStencil.DepthStencilFormat = depthFormat;

        // Line wireframes support alpha blending (for fade-out or transparent debug bounds)
        RHI::RHIBlendAttachmentDesc blendDesc = RHI::RHIBlendAttachmentDesc::AlphaBlend();
        gfxDesc.ColorAttachments = { blendDesc };
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.SampleCount = sampleCount;

        // Line Vertex Input: POSITION (float3), COLOR (float4)
        gfxDesc.VertexInput.Streams = {
            { 0, sizeof(RHIDebugLineVertex), nri::VertexStreamStepRate::PER_VERTEX }
        };

        gfxDesc.VertexInput.Attributes = {
            { "POSITION", 0, 0, 0,  nri::Format::RGB32_SFLOAT,  0 },
            { "COLOR",    0, 1, 12, nri::Format::RGBA32_SFLOAT, 0 }
        };

        auto& cache = RHI::RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    void RHIPhysicsDebugRenderer::Begin(const glm::vec3& cameraPos,
                                        const glm::mat4& viewProj,
                                        nri::Format colorFormat,
                                        nri::Format depthFormat,
                                        uint8_t sampleCount,
                                        bool depthTest,
                                        float depthBias,
                                        float alphaMultiplier)
    {
        m_InBatch = true;
        m_ActiveConstants.ViewProjection = viewProj;
        m_ActiveConstants.DepthBiasParams = glm::vec4(depthBias, alphaMultiplier, 0.0f, 0.0f);

        m_ActiveColorFormat = colorFormat;
        m_ActiveDepthFormat = depthFormat;
        m_ActiveSampleCount = sampleCount;
        m_ActiveDepthTest = depthTest;

        SetCameraPos(JPH::RVec3(cameraPos.x, cameraPos.y, cameraPos.z));
        m_LineVertices.clear();
        m_DrawCallCount = 0;
        m_FlushedVertexCount = 0;

        NextFrame();
    }

    void RHIPhysicsDebugRenderer::Flush(RHI::RHICommandBuffer& cmd) {
        if (!m_Initialized || m_LineVertices.empty()) return;

        uint32_t totalVertices = static_cast<uint32_t>(m_LineVertices.size());
        uint32_t verticesToRender = std::min(totalVertices, MAX_VERTICES_PER_BATCH);
        // Ensure even number of vertices for LINE_LIST
        verticesToRender = (verticesToRender / 2) * 2;
        if (verticesToRender == 0) return;

        // 1. Upload Constant Buffer
        void* cbData = m_ConstantBuffer.Map(0, sizeof(RHIPhysicsDebugConstants));
        if (cbData) {
            std::memcpy(cbData, &m_ActiveConstants, sizeof(RHIPhysicsDebugConstants));
            m_ConstantBuffer.Unmap();
        }

        // 2. Upload Dynamic Vertex Buffer
        uint64_t uploadBytes = verticesToRender * sizeof(RHIDebugLineVertex);
        void* vbData = m_VertexBuffer.Map(0, uploadBytes);
        if (vbData) {
            std::memcpy(vbData, m_LineVertices.data(), uploadBytes);
            m_VertexBuffer.Unmap();
        }

        // 3. Pipeline State Binding
        nri::Pipeline* pso = GetOrCreatePipeline(m_ActiveColorFormat, m_ActiveDepthFormat, m_ActiveSampleCount, m_ActiveDepthTest);
        if (!pso) return;

        cmd.SetPipeline(pso);
        cmd.SetPipelineLayout(nri::BindPoint::GRAPHICS, m_PipelineLayout);

        // 4. Bind Root CBV at Slot 0
        nri::Descriptor* cbv = m_ConstantBuffer.GetCBV();
        cmd.SetRootDescriptor(0, cbv, 0, nri::BindPoint::GRAPHICS);

        // 5. Bind Vertex Buffer
        cmd.SetVertexBuffer(0, m_VertexBuffer.GetNative(), 0, sizeof(RHIDebugLineVertex));

        // 6. Draw Line List
        cmd.Draw(verticesToRender, 1, 0, 0);

        m_DrawCallCount++;
        m_FlushedVertexCount += verticesToRender;
        m_LineVertices.clear();
    }

    void RHIPhysicsDebugRenderer::End(RHI::RHICommandBuffer& cmd) {
        if (!m_InBatch) return;
        Flush(cmd);
        m_InBatch = false;
    }

    void RHIPhysicsDebugRenderer::Clear() {
        m_LineVertices.clear();
    }

    void RHIPhysicsDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) {
        const float fx = static_cast<float>(inFrom.GetX());
        const float fy = static_cast<float>(inFrom.GetY());
        const float fz = static_cast<float>(inFrom.GetZ());
        const float tx = static_cast<float>(inTo.GetX());
        const float ty = static_cast<float>(inTo.GetY());
        const float tz = static_cast<float>(inTo.GetZ());

        if (std::isnan(fx) || std::isnan(fy) || std::isnan(fz) ||
            std::isnan(tx) || std::isnan(ty) || std::isnan(tz) ||
            std::isinf(fx) || std::isinf(fy) || std::isinf(fz) ||
            std::isinf(tx) || std::isinf(ty) || std::isinf(tz)) {
            return;
        }

        if (m_LineVertices.size() + 2 > MAX_VERTICES_PER_BATCH) {
            return; // Cap line buffer to prevent overflow before flush
        }

        const glm::vec4 col(
            static_cast<float>(inColor.r) / 255.0f,
            static_cast<float>(inColor.g) / 255.0f,
            static_cast<float>(inColor.b) / 255.0f,
            static_cast<float>(inColor.a) / 255.0f
        );

        m_LineVertices.push_back({ glm::vec3(fx, fy, fz), col });
        m_LineVertices.push_back({ glm::vec3(tx, ty, tz), col });
    }

    void RHIPhysicsDebugRenderer::DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight) {
        (void)inPosition;
        (void)inString;
        (void)inColor;
        (void)inHeight;
    }

    void RHIPhysicsDebugRenderer::DrawLineSegment(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color) {
        if (m_LineVertices.size() + 2 > MAX_VERTICES_PER_BATCH) return;
        m_LineVertices.push_back({ from, color });
        m_LineVertices.push_back({ to, color });
    }

    void RHIPhysicsDebugRenderer::DrawBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color) {
        // 8 Corners of AABB
        glm::vec3 c0(min.x, min.y, min.z);
        glm::vec3 c1(max.x, min.y, min.z);
        glm::vec3 c2(max.x, max.y, min.z);
        glm::vec3 c3(min.x, max.y, min.z);

        glm::vec3 c4(min.x, min.y, max.z);
        glm::vec3 c5(max.x, min.y, max.z);
        glm::vec3 c6(max.x, max.y, max.z);
        glm::vec3 c7(min.x, max.y, max.z);

        // Bottom quad
        DrawLineSegment(c0, c1, color);
        DrawLineSegment(c1, c2, color);
        DrawLineSegment(c2, c3, color);
        DrawLineSegment(c3, c0, color);

        // Top quad
        DrawLineSegment(c4, c5, color);
        DrawLineSegment(c5, c6, color);
        DrawLineSegment(c6, c7, color);
        DrawLineSegment(c7, c4, color);

        // Vertical pillars
        DrawLineSegment(c0, c4, color);
        DrawLineSegment(c1, c5, color);
        DrawLineSegment(c2, c6, color);
        DrawLineSegment(c3, c7, color);
    }

    void RHIPhysicsDebugRenderer::DrawOrientedBox(const glm::mat4& transform, const glm::vec3& halfExtent, const glm::vec4& color) {
        glm::vec3 corners[8] = {
            glm::vec3(-halfExtent.x, -halfExtent.y, -halfExtent.z),
            glm::vec3( halfExtent.x, -halfExtent.y, -halfExtent.z),
            glm::vec3( halfExtent.x,  halfExtent.y, -halfExtent.z),
            glm::vec3(-halfExtent.x,  halfExtent.y, -halfExtent.z),
            glm::vec3(-halfExtent.x, -halfExtent.y,  halfExtent.z),
            glm::vec3( halfExtent.x, -halfExtent.y,  halfExtent.z),
            glm::vec3( halfExtent.x,  halfExtent.y,  halfExtent.z),
            glm::vec3(-halfExtent.x,  halfExtent.y,  halfExtent.z)
        };

        for (int i = 0; i < 8; ++i) {
            corners[i] = glm::vec3(transform * glm::vec4(corners[i], 1.0f));
        }

        // Bottom
        DrawLineSegment(corners[0], corners[1], color);
        DrawLineSegment(corners[1], corners[2], color);
        DrawLineSegment(corners[2], corners[3], color);
        DrawLineSegment(corners[3], corners[0], color);

        // Top
        DrawLineSegment(corners[4], corners[5], color);
        DrawLineSegment(corners[5], corners[6], color);
        DrawLineSegment(corners[6], corners[7], color);
        DrawLineSegment(corners[7], corners[4], color);

        // Vertical
        DrawLineSegment(corners[0], corners[4], color);
        DrawLineSegment(corners[1], corners[5], color);
        DrawLineSegment(corners[2], corners[6], color);
        DrawLineSegment(corners[3], corners[7], color);
    }

    void RHIPhysicsDebugRenderer::DrawTransform(const glm::mat4& transform, float scale) {
        glm::vec3 origin = glm::vec3(transform[3]);
        glm::vec3 right  = glm::normalize(glm::vec3(transform[0])) * scale;
        glm::vec3 up     = glm::normalize(glm::vec3(transform[1])) * scale;
        glm::vec3 fwd    = glm::normalize(glm::vec3(transform[2])) * scale;

        DrawLineSegment(origin, origin + right, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)); // Red X
        DrawLineSegment(origin, origin + up,    glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)); // Green Y
        DrawLineSegment(origin, origin + fwd,   glm::vec4(0.0f, 0.5f, 1.0f, 1.0f)); // Blue Z
    }

    void RHIPhysicsDebugRenderer::DrawCircle(const glm::vec3& center, float radius, const glm::vec3& normal, const glm::vec4& color, int segments) {
        if (segments < 3) segments = 3;

        glm::vec3 n = glm::normalize(normal);
        glm::vec3 tangent = (std::abs(n.y) < 0.999f) ? glm::cross(n, glm::vec3(0.0f, 1.0f, 0.0f)) : glm::cross(n, glm::vec3(1.0f, 0.0f, 0.0f));
        tangent = glm::normalize(tangent);
        glm::vec3 bitangent = glm::normalize(glm::cross(n, tangent));

        float angleStep = 6.2831853f / static_cast<float>(segments);
        glm::vec3 prevPoint = center + (tangent * std::cos(0.0f) + bitangent * std::sin(0.0f)) * radius;

        for (int i = 1; i <= segments; ++i) {
            float angle = static_cast<float>(i) * angleStep;
            glm::vec3 currentPoint = center + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
            DrawLineSegment(prevPoint, currentPoint, color);
            prevPoint = currentPoint;
        }
    }

    void RHIPhysicsDebugRenderer::DrawSphere(const glm::vec3& center, float radius, const glm::vec4& color, int segments) {
        DrawCircle(center, radius, glm::vec3(1.0f, 0.0f, 0.0f), color, segments);
        DrawCircle(center, radius, glm::vec3(0.0f, 1.0f, 0.0f), color, segments);
        DrawCircle(center, radius, glm::vec3(0.0f, 0.0f, 1.0f), color, segments);
    }

    void RHIPhysicsDebugRenderer::DrawRay(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color) {
        glm::vec3 end = origin + glm::normalize(direction) * length;
        DrawLineSegment(origin, end, color);
    }

}
