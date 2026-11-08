#include <Axle/RHIParticleRenderer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Path.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHIMaterial.hpp>

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace Axle {

    RHIParticleRenderer::RHIParticleRenderer() = default;

    RHIParticleRenderer::~RHIParticleRenderer() {
        Shutdown();
    }

    bool RHIParticleRenderer::Init(RHI::RHIDevice& device) {
        if (m_Initialized) return true;
        m_Device = &device;

        if (!CompileShaders(device)) {
            AXLE_CORE_ERROR("RHIParticleRenderer: Failed to compile particle shaders!");
            return false;
        }

        if (!CreatePipelineLayout(device)) {
            AXLE_CORE_ERROR("RHIParticleRenderer: Failed to create particle pipeline layout!");
            return false;
        }

        if (!CreateBuffers(device)) {
            AXLE_CORE_ERROR("RHIParticleRenderer: Failed to create GPU buffers!");
            return false;
        }

        if (!CreatePipelines(device)) {
            AXLE_CORE_ERROR("RHIParticleRenderer: Failed to create default graphics pipelines!");
            return false;
        }

        m_InstanceStagingBuffer.reserve(MAX_PARTICLES_PER_BATCH);
        m_Initialized = true;

        AXLE_CORE_INFO("RHIParticleRenderer: Successfully initialized GPU Instancing Particle Pipeline (Max: {0} particles/batch, Stride: {1}B).",
                       MAX_PARTICLES_PER_BATCH, sizeof(ParticleInstanceVertex));
        return true;
    }

    void RHIParticleRenderer::Shutdown() {
        if (!m_Initialized) return;

        for (auto& res : m_DrawResources) {
            if (res) {
                res->Constants.Shutdown();
                res->Instances.Shutdown();
            }
        }
        m_DrawResources.clear();
        m_DrawResourceCursor = 0;

        m_ConstantBuffer.Shutdown();
        m_InstanceBuffer.Shutdown();
        m_LinearSampler.Shutdown();
        m_DescriptorPool.Shutdown();
        m_PipelineLayout.Shutdown();

        m_PipelineAlphaBlendSDR = nullptr;
        m_PipelineAlphaBlendHDR = nullptr;
        m_PipelineAdditiveSDR = nullptr;
        m_PipelineAdditiveHDR = nullptr;
        m_DescriptorSet = nullptr;

        m_InstanceStagingBuffer.clear();
        m_InBatch = false;
        m_Initialized = false;
    }

    ParticleDrawResources *RHIParticleRenderer::AcquireDrawResources(uint32_t instanceCount) {
        if (instanceCount == 0 || instanceCount > MAX_PARTICLES_PER_BATCH) return nullptr;

        // Emitters grow continuously. Reserve geometrically instead of recreating a
        // committed allocation for every new particle during the first seconds of playback.
        uint32_t capacity = 256;
        while (capacity < instanceCount) capacity = std::min(capacity * 2, MAX_PARTICLES_PER_BATCH);
        const uint64_t bytes = static_cast<uint64_t>(capacity) * sizeof(ParticleInstanceVertex);
        if (m_DrawResourceCursor < m_DrawResources.size()) {
            auto *resource = m_DrawResources[m_DrawResourceCursor++].get();
            if (!resource->Instances.IsValid() ||
                resource->Instances.GetSize() < static_cast<uint64_t>(instanceCount) * sizeof(ParticleInstanceVertex)) {
                RHI::RHIBuffer replacement;
                if (!replacement.Init(RHI::RHIBufferDesc::Vertex(bytes,
                        RHI::RHIMemoryLocation::HostUpload, "ParticleInstances")))
                    return nullptr;
                // Keep the previous valid buffer on failure, so the next frame can retry.
                resource->Instances = std::move(replacement);
            }
            return resource;
        }

        auto res = std::make_unique<ParticleDrawResources>();
        std::string suffix = std::to_string(m_DrawResources.size());
        if (!res->Constants.Init(RHI::RHIBufferDesc::Constant(sizeof(RHIParticleConstants), RHI::RHIMemoryLocation::HostUpload, "Particle_CBV_" + suffix))) {
            return nullptr;
        }
        if (!res->Instances.Init(RHI::RHIBufferDesc::Vertex(bytes,
                                                            RHI::RHIMemoryLocation::HostUpload,
                                                            "Particle_VBO_" + suffix))) {
            return nullptr;
        }
        if (!res->Pool.Init(RHI::RHIDescriptorPoolDesc::Tier2Bindless(3, 1))) return nullptr;
        res->Set = res->Pool.AllocateDescriptorSet(m_PipelineLayout, 0);
        if (!res->Set) return nullptr;
        res->Pool.UpdateSampler(res->Set, 3, 0, m_LinearSampler.GetNative());
        m_DrawResources.push_back(std::move(res));
        m_DrawResourceCursor = m_DrawResources.size();
        return m_DrawResources.back().get();
    }

    bool RHIParticleRenderer::CompileShaders(RHI::RHIDevice& device) {
        auto& compiler = RHI::RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            compiler.Init();
        }

        std::string shaderSource;
        auto shaderPath = FileSystem::ResolveAssetPath("shaders/Particle.hlsl");
        if (!FileSystem::Exists(shaderPath)) {
            shaderPath = std::filesystem::current_path() / "Engine" / "Resources" / "Shaders" / "Particle.hlsl";
        }
        if (!FileSystem::Exists(shaderPath)) {
            shaderPath = "Engine/Resources/Shaders/Particle.hlsl";
        }

        if (FileSystem::Exists(shaderPath)) shaderSource = FileSystem::ReadTextFile(shaderPath);

        if (shaderSource.empty()) {
            AXLE_CORE_ERROR("RHIParticleRenderer: Failed to locate Particle.hlsl!");
            return false;
        }

        RHI::RHIShaderCompileDesc vsDesc;
        vsDesc.Source = shaderSource;
        vsDesc.SourcePath = PathToUtf8(shaderPath);
        vsDesc.EntryPoint = "VSMain";
        vsDesc.Stage = RHI::RHIShaderStage::Vertex;
        vsDesc.HLSLVersion = "2021";
        vsDesc.ShaderModel = "6_0";
        vsDesc.Format = (device.GetCapabilities().ActiveBackend == RHI::RHIType::Vulkan) ? RHI::RHIShaderFormat::SPIRV : RHI::RHIShaderFormat::DXIL;
        vsDesc.Optimization = true;

        auto vsRes = compiler.Compile(vsDesc);
        if (!vsRes.Success) {
            AXLE_CORE_ERROR("RHIParticleRenderer: Failed to compile Particle VS: {0}", vsRes.ErrorMessage);
            return false;
        }
        m_VertexShader = vsRes.Bytecode;

        RHI::RHIShaderCompileDesc psDesc = vsDesc;
        psDesc.EntryPoint = "PSMain";
        psDesc.Stage = RHI::RHIShaderStage::Fragment;

        auto psRes = compiler.Compile(psDesc);
        if (!psRes.Success) {
            AXLE_CORE_ERROR("RHIParticleRenderer: Failed to compile Particle PS: {0}", psRes.ErrorMessage);
            return false;
        }
        m_PixelShader = psRes.Bytecode;

        return true;
    }

    bool RHIParticleRenderer::CreatePipelineLayout(RHI::RHIDevice& device) {
        // Root Descriptor [0] b0, space 0: aligned RHIParticleConstants
        nri::RootDescriptorDesc rootDesc = {};
        rootDesc.registerIndex = 0;
        rootDesc.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        rootDesc.shaderStages = nri::StageBits::ALL;

        // Descriptor Set (space 1):
        // Range 0: t0 (Particle Texture, 1 descriptor)
        // Range 1: t1 (Normal Map, 1 descriptor)
        // Range 2: t2 (Scene Depth Map, 1 descriptor)
        // Range 3: s0 (Linear Sampler, 1 descriptor)
        nri::DescriptorRangeDesc ranges[4] = {};
        ranges[0].baseRegisterIndex = 0;
        ranges[0].descriptorNum = 1;
        ranges[0].descriptorType = nri::DescriptorType::TEXTURE;
        ranges[0].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[0].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        ranges[1].baseRegisterIndex = 1;
        ranges[1].descriptorNum = 1;
        ranges[1].descriptorType = nri::DescriptorType::TEXTURE;
        ranges[1].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[1].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        ranges[2].baseRegisterIndex = 2;
        ranges[2].descriptorNum = 1;
        ranges[2].descriptorType = nri::DescriptorType::TEXTURE;
        ranges[2].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[2].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        ranges[3].baseRegisterIndex = 0;
        ranges[3].descriptorNum = 1;
        ranges[3].descriptorType = nri::DescriptorType::SAMPLER;
        ranges[3].shaderStages = nri::StageBits::FRAGMENT_SHADER;
        ranges[3].flags = nri::DescriptorRangeBits::PARTIALLY_BOUND;

        nri::DescriptorSetDesc descSet = {};
        descSet.registerSpace = 1;
        descSet.ranges = ranges;
        descSet.rangeNum = 4;

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootRegisterSpace = 0;
        layoutDesc.rootDescriptors = &rootDesc;
        layoutDesc.rootDescriptorNum = 1;
        layoutDesc.descriptorSets = &descSet;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        return m_PipelineLayout.Init(layoutDesc);
    }

    bool RHIParticleRenderer::CreateBuffers(RHI::RHIDevice& device) {
        // 1. Hardware Constant Buffer (aligned to 256B boundary)
        if (!m_ConstantBuffer.Init(RHI::RHIBufferDesc::Constant(sizeof(RHIParticleConstants), RHI::RHIMemoryLocation::HostUpload, "Particle_ConstantBuffer"))) {
            return false;
        }

        // 2. Hardware Dynamic Instance Buffer (20,000 particles * 84 bytes = 1.68 MB)
        if (!m_InstanceBuffer.Init(RHI::RHIBufferDesc::Vertex(MAX_PARTICLES_PER_BATCH * sizeof(ParticleInstanceVertex), RHI::RHIMemoryLocation::HostUpload, "Particle_InstanceVBO"))) {
            return false;
        }

        // 3. Bilinear Sampler
        if (!m_LinearSampler.Init(RHI::RHISamplerDesc::Bilinear(RHI::RHIAddressMode::ClampToEdge))) {
            return false;
        }

        // 4. Descriptor Pool & Default Descriptor Set
        if (!m_DescriptorPool.Init(RHI::RHIDescriptorPoolDesc::Tier2Bindless(64, 16))) {
            return false;
        }

        m_DescriptorSet = m_DescriptorPool.AllocateDescriptorSet(m_PipelineLayout, 0);
        if (!m_DescriptorSet) {
            return false;
        }

        if (m_LinearSampler.IsValid()) {
            m_DescriptorPool.UpdateSampler(m_DescriptorSet, 3, 0, m_LinearSampler.GetNative());
        }

        return true;
    }

    nri::Pipeline* RHIParticleRenderer::GetOrCreatePipeline(ParticleBlendMode blendMode,
                                                           nri::Format colorFormat,
                                                           nri::Format depthFormat,
                                                           uint8_t sampleCount)
    {
        RHI::RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = &m_PipelineLayout;
        gfxDesc.Shaders = { m_VertexShader, m_PixelShader };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        gfxDesc.Rasterizer.FrontCounterClockwise = false;

        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = false;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS_EQUAL;
        gfxDesc.DepthStencil.DepthStencilFormat = depthFormat;

        RHI::RHIBlendAttachmentDesc blendDesc;
        if (blendMode == ParticleBlendMode::Additive) {
            blendDesc = RHI::RHIBlendAttachmentDesc::Additive();
        } else if (blendMode == ParticleBlendMode::Multiply) {
            blendDesc.BlendEnabled = true;
            blendDesc.SrcColor = nri::BlendFactor::DST_COLOR;
            blendDesc.DstColor = nri::BlendFactor::ZERO;
            blendDesc.ColorOp = nri::BlendOp::ADD;
            blendDesc.SrcAlpha = nri::BlendFactor::DST_ALPHA;
            blendDesc.DstAlpha = nri::BlendFactor::ZERO;
            blendDesc.AlphaOp = nri::BlendOp::ADD;
        } else if (blendMode == ParticleBlendMode::Cutout) {
            blendDesc.BlendEnabled = false;
            gfxDesc.DepthStencil.DepthWriteEnable = true;
        } else {
            blendDesc = RHI::RHIBlendAttachmentDesc::AlphaBlend();
        }

        gfxDesc.ColorAttachments = { blendDesc };
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.SampleCount = sampleCount;

        // Instanced Vertex Layout: 1 Stream (StepRate = PER_INSTANCE), 8 Attributes
        gfxDesc.VertexInput.Streams = {
            { 0, sizeof(ParticleInstanceVertex), nri::VertexStreamStepRate::PER_INSTANCE }
        };

        gfxDesc.VertexInput.Attributes = {
            { "POSITION",    0, 0, 0,  nri::Format::RGB32_SFLOAT,  0 },
            { "PSIZE",       0, 1, 12, nri::Format::R32_SFLOAT,    0 },
            { "COLOR",       0, 2, 16, nri::Format::RGBA32_SFLOAT, 0 },
            { "ROTATION",    0, 3, 32, nri::Format::R32_SFLOAT,    0 },
            { "ORIENTATION", 0, 4, 36, nri::Format::R32_SINT,      0 },
            { "VELOCITY",    0, 5, 40, nri::Format::RGB32_SFLOAT,  0 },
            { "TEXCOORD",    0, 6, 52, nri::Format::RGBA32_SFLOAT, 0 },
            { "TEXCOORD",    1, 7, 68, nri::Format::RGBA32_SFLOAT, 0 }
        };

        auto& cache = RHI::RHIPipelineCache::Get();
        return cache.GetOrCreateGraphicsPipeline(gfxDesc);
    }

    bool RHIParticleRenderer::CreatePipelines(RHI::RHIDevice& device) {
        m_PipelineAlphaBlendSDR = GetOrCreatePipeline(ParticleBlendMode::AlphaBlend, nri::Format::RGBA8_UNORM);
        m_PipelineAlphaBlendHDR = GetOrCreatePipeline(ParticleBlendMode::AlphaBlend, nri::Format::RGBA16_SFLOAT);
        m_PipelineAdditiveSDR = GetOrCreatePipeline(ParticleBlendMode::Additive, nri::Format::RGBA8_UNORM);
        m_PipelineAdditiveHDR = GetOrCreatePipeline(ParticleBlendMode::Additive, nri::Format::RGBA16_SFLOAT);
        m_PipelineCutoutSDR = GetOrCreatePipeline(ParticleBlendMode::Cutout, nri::Format::RGBA8_UNORM);
        m_PipelineCutoutHDR = GetOrCreatePipeline(ParticleBlendMode::Cutout, nri::Format::RGBA16_SFLOAT);

        return m_PipelineAlphaBlendSDR != nullptr && m_PipelineAdditiveSDR != nullptr;
    }

    void ParticleRenderPlanner::BuildDrawPackets(
        const ParticlePool& pool,
        const ParticleEmitterConfig& config,
        const PerspectiveCamera& camera,
        const glm::vec3& emitterWorldPos,
        const glm::quat& emitterWorldRot,
        std::vector<ParticleDrawPacket>& outPackets,
        uint32_t maxBatchSize, const glm::vec3& emitterWorldScale)
    {
        uint32_t activeCount = pool.GetActiveCount();
        if (activeCount == 0 || maxBatchSize == 0) return;

        const auto& positions = pool.GetPositions();
        const auto& sizes = pool.GetSizes();
        const auto& colors = pool.GetColors();
        const auto& rotations = pool.GetRotations();
        const auto& velocities = pool.GetVelocities();
        const auto& uvs = pool.GetUVOffsetScales();
        const auto& subFrames = pool.GetUVSubFrames();
        int orientation = static_cast<int>(config.Orientation);
        glm::vec3 camPos = camera.GetPosition();

        struct ParticleIndexKey {
            uint32_t Index;
            float DistSq;
        };
        std::vector<ParticleIndexKey> sortedKeys(activeCount);
        bool isLocal = (config.SimulationSpace == ParticleSimulationSpace::Local);

        for (uint32_t i = 0; i < activeCount; ++i) {
            sortedKeys[i].Index = i;
            glm::vec3 worldP = isLocal ? (emitterWorldPos + (emitterWorldRot * (positions[i] * emitterWorldScale))) : positions[i];
            sortedKeys[i].DistSq = -(camera.GetViewMatrix() * glm::vec4(worldP, 1)).z;
        }

        // Alpha sorting (back-to-front, i.e. descending distance)
        if (config.BlendMode == ParticleBlendMode::AlphaBlend) {
            std::stable_sort(sortedKeys.begin(), sortedKeys.end(), [](const ParticleIndexKey& a, const ParticleIndexKey& b) {
                return a.DistSq > b.DistSq;
            });
        }

        uint32_t totalPackets = (activeCount + maxBatchSize - 1) / maxBatchSize;
        outPackets.reserve(outPackets.size() + totalPackets);

        for (uint32_t offset = 0; offset < activeCount; offset += maxBatchSize) {
            uint32_t batchCount = std::min(activeCount - offset, maxBatchSize);
            ParticleDrawPacket packet;
            packet.EmitterName = config.Name;
            packet.BlendMode = config.BlendMode;
            packet.InstanceOffset = offset;
            packet.InstanceCount = batchCount;
            packet.Instances.resize(batchCount);

            for (uint32_t j = 0; j < batchCount; ++j) {
                uint32_t srcIdx = sortedKeys[offset + j].Index;
                auto& v = packet.Instances[j];
                if (isLocal) {
                    v.Position = emitterWorldPos + (emitterWorldRot * (positions[srcIdx] * emitterWorldScale));
                    v.Velocity = emitterWorldRot * (velocities[srcIdx] * emitterWorldScale);
                } else {
                    v.Position = positions[srcIdx];
                    v.Velocity = velocities[srcIdx];
                }
                v.Size = sizes[srcIdx] * (isLocal ? glm::max(glm::max(glm::abs(emitterWorldScale.x), glm::abs(emitterWorldScale.y)), glm::abs(emitterWorldScale.z)) : 1.0f);
                v.Color = colors[srcIdx];
                v.Rotation = rotations[srcIdx];
                v.Orientation = orientation;
                v.UVOffsetScale = uvs[srcIdx];
                v.UVSubFrame = subFrames[srcIdx];
            }

            outPackets.push_back(std::move(packet));
        }
    }

    void RHIParticleRenderer::Begin(RHI::RHICommandBuffer& cmd,
                                   const PerspectiveCamera& camera,
                                   nri::Format colorFormat,
                                   nri::Format depthFormat,
                                   uint8_t sampleCount)
    {
        m_DrawResourceCursor = 0;
        m_ActiveColorFormat = colorFormat;
        m_ActiveDepthFormat = depthFormat;
        m_ActiveSampleCount = sampleCount;

        // 1. Populate Scene & Camera Matrices & Vectors
        m_ActiveConstants.ViewProjection = camera.GetViewProjectionMatrixZO();
        m_ActiveConstants.View = camera.GetViewMatrix();
        m_ActiveConstants.Projection = camera.GetProjectionMatrixZO();

        glm::vec3 camPos = camera.GetPosition();
        glm::mat4 invView = glm::inverse(camera.GetViewMatrix());
        glm::vec3 camRight = glm::normalize(glm::vec3(invView[0]));
        glm::vec3 camUp = glm::normalize(glm::vec3(invView[1]));

        m_ActiveConstants.CameraPosition = glm::vec4(camPos, camera.GetNearClip());
        m_ActiveConstants.CameraRight = glm::vec4(camRight, camera.GetFarClip());
        m_ActiveConstants.CameraUp = glm::vec4(camUp, 0.5f);

        // 2. Populate Light Environment
        m_ActiveConstants.SunDirection = glm::vec4(m_LightEnvironment.SunDirection, 0.2f);
        m_ActiveConstants.SunColor = glm::vec4(m_LightEnvironment.SunColor, 0.5f);
        m_ActiveConstants.AmbientColor = glm::vec4(m_LightEnvironment.AmbientColor, 1.0f);

        uint32_t pointCount = static_cast<uint32_t>(std::min<size_t>(m_LightEnvironment.PointLights.size(), 4));
        m_ActiveConstants.Flags2.w = pointCount;
        for (uint32_t i = 0; i < pointCount; ++i) {
            const auto& pl = m_LightEnvironment.PointLights[i];
            m_ActiveConstants.PointLightPositions[i] = glm::vec4(pl.Position, pl.Radius);
            m_ActiveConstants.PointLightColors[i] = glm::vec4(pl.Color, pl.Intensity);
        }

        m_InBatch = true;
    }

    void RHIParticleRenderer::End() {
        m_InBatch = false;
        m_SceneDepth = nullptr;
    }

    void RHIParticleRenderer::Render(RHI::RHICommandBuffer& cmd,
                                    const ParticleSystem& system,
                                    RHI::RHITexture* texture,
                                    RHI::RHITexture* normalMap,
                                    float softnessDistance,
                                    const glm::vec3& emitterWorldPos,
                                    const glm::quat& emitterWorldRot, const glm::vec3& emitterWorldScale, const EffectiveParticleRenderSettings* material)
    {
        Render(cmd, system.GetPool(), system.GetConfig(), texture, normalMap, softnessDistance, emitterWorldPos, emitterWorldRot, emitterWorldScale, material);
    }

    void RHIParticleRenderer::Render(RHI::RHICommandBuffer& cmd,
                                    const ParticlePool& pool,
                                    const ParticleEmitterConfig& config,
                                    RHI::RHITexture* texture,
                                    RHI::RHITexture* normalMap,
                                    float softnessDistance,
                                    const glm::vec3& emitterWorldPos,
                                    const glm::quat& emitterWorldRot, const glm::vec3& emitterWorldScale, const EffectiveParticleRenderSettings* material)
    {
        nri::Descriptor* texSRV = (texture && texture->IsValid()) ? texture->GetSRV() : nullptr;
        nri::Descriptor* normSRV = (normalMap && normalMap->IsValid()) ? normalMap->GetSRV() : nullptr;
        Render(cmd, pool, config, texSRV, normSRV, softnessDistance, emitterWorldPos, emitterWorldRot, emitterWorldScale, material);
    }

    void RHIParticleRenderer::Render(RHI::RHICommandBuffer& cmd,
                                    const ParticlePool& pool,
                                    const ParticleEmitterConfig& config,
                                    nri::Descriptor* textureSRV,
                                    nri::Descriptor* normalMapSRV,
                                    float softnessDistance,
                                    const glm::vec3& emitterWorldPos,
                                    const glm::quat& emitterWorldRot, const glm::vec3& emitterWorldScale, const EffectiveParticleRenderSettings* material)
    {
        uint32_t activeCount = pool.GetActiveCount();
        if (activeCount == 0 || !m_Initialized || !m_InBatch) return;

        PerspectiveCamera cam(55.0f, 1.0f, 0.1f, 1000.0f);
        cam.SetViewMatrix(m_ActiveConstants.View, glm::vec3(m_ActiveConstants.CameraPosition));

        std::vector<ParticleDrawPacket> packets;
        ParticleRenderPlanner::BuildDrawPackets(pool, config, cam, emitterWorldPos, emitterWorldRot, packets, MAX_PARTICLES_PER_BATCH, emitterWorldScale);

        for (const auto& packet : packets) {
            ParticleDrawResources *drawRes = AcquireDrawResources(packet.InstanceCount);
            if (!drawRes) continue;

            // 1. Upload to GPU Instance Buffer
            if (!drawRes->Instances.UpdateData(packet.Instances.data(),
                    static_cast<uint64_t>(packet.InstanceCount) * sizeof(ParticleInstanceVertex)))
                continue;

            // 2. Update Constant Buffer Parameters
            RHIParticleConstants drawConstants = m_ActiveConstants;
            drawConstants.Flags.x = (textureSRV != nullptr) ? 1u : 0u;
            drawConstants.Flags.y = config.SubFrameBlending ? 1u : 0u;
            drawConstants.Flags.z = (normalMapSRV != nullptr) ? 1u : 0u;
            drawConstants.Flags.w = config.LightingEnabled ? 1u : 0u;

            drawConstants.SunDirection.w = config.WrapLighting;
            drawConstants.SunColor.w = config.ScatteringFactor;
            drawConstants.AmbientColor.w = config.NormalMapStrength;

            drawConstants.FadeParams = glm::vec4(config.NearFadeStart, config.NearFadeEnd, config.FarFadeStart, config.FarFadeEnd);
            drawConstants.Flags2.x = config.NearFadeEnabled ? 1u : 0u;
            drawConstants.Flags2.y = config.FarFadeEnabled ? 1u : 0u;
            drawConstants.Flags2.z = (m_SceneDepth && softnessDistance > 0.0f) ? 1u : 0u;
            drawConstants.CameraUp.w = (softnessDistance > 0.0f) ? softnessDistance : config.SoftnessDistance;

            bool isCutout = (config.BlendMode == ParticleBlendMode::Cutout);
            drawConstants.CutoutAndMaterial = glm::vec4(material ? material->AlphaCutoff : 0.5f, config.SoftnessDistance, 0.0f, isCutout ? 1.0f : 0.0f);
            drawConstants.MaterialTint = material ? material->BaseColor : glm::vec4(1);
            drawConstants.MaterialEmission = material ? glm::vec4(material->EmissiveColor, material->EmissiveIntensity) : glm::vec4(0);
            drawConstants.UVTransform = material ? glm::vec4(material->UVScale, material->UVOffset) : glm::vec4(1,1,0,0);

            if (!drawRes->Constants.UpdateData(&drawConstants, sizeof(RHIParticleConstants)))
                continue;

            // 3. Update Texture Descriptors in Descriptor Set (Space 1)
            if (drawRes->Pool.IsValid() && drawRes->Set) {
                auto defaultWhite = RHI::RHIMaterial::GetDefaultWhiteTexture();
                auto defaultFlatNormal = RHI::RHIMaterial::GetDefaultFlatNormalTexture();

                nri::Descriptor* finalTexSRV = textureSRV ? textureSRV : (defaultWhite ? defaultWhite->GetSRV() : nullptr);
                nri::Descriptor* finalNormSRV = normalMapSRV ? normalMapSRV : (defaultFlatNormal ? defaultFlatNormal->GetSRV() : nullptr);
                nri::Descriptor* finalDepthSRV = m_SceneDepth ? m_SceneDepth : (defaultWhite ? defaultWhite->GetSRV() : nullptr);

                if (finalTexSRV) {
                    drawRes->Pool.UpdateTexture(drawRes->Set, 0, 0, finalTexSRV);
                }
                if (finalNormSRV) {
                    drawRes->Pool.UpdateTexture(drawRes->Set, 1, 0, finalNormSRV);
                }
                if (finalDepthSRV) {
                    drawRes->Pool.UpdateTexture(drawRes->Set, 2, 0, finalDepthSRV);
                }
                if (m_LinearSampler.IsValid()) {
                    drawRes->Pool.UpdateSampler(drawRes->Set, 3, 0, m_LinearSampler.GetNative());
                }
            }

            // 4. Select Graphics Pipeline State Object
            nri::Pipeline* pipeline = GetOrCreatePipeline(config.BlendMode, m_ActiveColorFormat, m_ActiveDepthFormat, m_ActiveSampleCount);
            if (!pipeline) continue; // Never substitute an SDR/single-sample PSO into an incompatible pass.

            cmd.SetPipeline(pipeline);
            cmd.SetPipelineLayout(nri::BindPoint::GRAPHICS, m_PipelineLayout);

            // Bind Root Descriptor [0]: ParticleConstants (b0)
            if (drawRes->Constants.IsValid() && drawRes->Constants.GetCBV()) {
                cmd.SetRootDescriptor(0, drawRes->Constants.GetCBV());
            }

            // Bind Descriptor Set [0]: Particle Textures & Sampler (Space 1)
            if (drawRes->Pool.IsValid() && drawRes->Set) {
                drawRes->Pool.Bind(cmd.GetNative());
                RHI::RHIDescriptorPool::BindDescriptorSet(cmd.GetNative(), 0, drawRes->Set);
            }

            // Bind Instanced Vertex Buffer (Stream 0, StepRate = PER_INSTANCE)
            cmd.SetVertexBuffer(0, drawRes->Instances.GetNative(), 0, sizeof(ParticleInstanceVertex));

            // Draw 6-vertex quad per particle instance
            cmd.Draw(6, packet.InstanceCount, 0, 0);

            m_DrawCallCount++;
            m_RenderedParticleCount += packet.InstanceCount;
        }
    }

}
