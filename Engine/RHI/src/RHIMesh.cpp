#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/Log.hpp>
#include <cmath>
#include <algorithm>

namespace Axle::RHI {

    namespace {
        constexpr float PI = 3.14159265358979323846f;
    }

    RHIMesh::~RHIMesh() {
        Shutdown();
    }

    RHIMesh::RHIMesh(RHIMesh&& other) noexcept
        : m_VertexBuffer(std::move(other.m_VertexBuffer))
        , m_IndexBuffer(std::move(other.m_IndexBuffer))
        , m_VertexCount(other.m_VertexCount)
        , m_IndexCount(other.m_IndexCount)
        , m_Submeshes(std::move(other.m_Submeshes))
        , m_BoundingMin(other.m_BoundingMin)
        , m_BoundingMax(other.m_BoundingMax)
        , m_DebugName(std::move(other.m_DebugName))
    {
        other.m_VertexCount = 0;
        other.m_IndexCount = 0;
    }

    RHIMesh& RHIMesh::operator=(RHIMesh&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_VertexBuffer = std::move(other.m_VertexBuffer);
            m_IndexBuffer = std::move(other.m_IndexBuffer);
            m_VertexCount = other.m_VertexCount;
            m_IndexCount = other.m_IndexCount;
            m_Submeshes = std::move(other.m_Submeshes);
            m_BoundingMin = other.m_BoundingMin;
            m_BoundingMax = other.m_BoundingMax;
            m_DebugName = std::move(other.m_DebugName);

            other.m_VertexCount = 0;
            other.m_IndexCount = 0;
        }
        return *this;
    }

    void RHIMesh::Shutdown() {
        m_VertexBuffer.Shutdown();
        m_IndexBuffer.Shutdown();
        m_VertexCount = 0;
        m_IndexCount = 0;
        m_Submeshes.clear();
        m_BoundingMin = glm::vec3(0.0f);
        m_BoundingMax = glm::vec3(0.0f);
    }

    void RHIMesh::UpdateBoundingBox(const std::vector<RHIVertex3D>& vertices) {
        if (vertices.empty()) {
            m_BoundingMin = glm::vec3(0.0f);
            m_BoundingMax = glm::vec3(0.0f);
            return;
        }

        m_BoundingMin = glm::vec3(1e9f);
        m_BoundingMax = glm::vec3(-1e9f);
        for (const auto& v : vertices) {
            m_BoundingMin = glm::min(m_BoundingMin, v.Position);
            m_BoundingMax = glm::max(m_BoundingMax, v.Position);
        }
    }

    bool RHIMesh::Init(const std::vector<RHIVertex3D>& vertices, const std::vector<uint32_t>& indices, const std::string& debugName) {
        if (vertices.empty() || indices.empty()) {
            AXLE_CORE_ERROR("RHIMesh::Init failed: Vertex or Index array is empty! (Mesh: '{0}')", debugName);
            return false;
        }

        Shutdown();
        m_DebugName = debugName;
        m_VertexCount = static_cast<uint32_t>(vertices.size());
        m_IndexCount = static_cast<uint32_t>(indices.size());
        UpdateBoundingBox(vertices);

        uint64_t vbSize = static_cast<uint64_t>(vertices.size() * sizeof(RHIVertex3D));
        uint64_t ibSize = static_cast<uint64_t>(indices.size() * sizeof(uint32_t));

        // Direct upload to DeviceUpload VRAM
        RHIBufferDesc vbDesc = RHIBufferDesc::Vertex(vbSize, RHIMemoryLocation::DeviceUpload, debugName + "_VB");
        if (!m_VertexBuffer.Init(vbDesc, vertices.data())) {
            AXLE_CORE_ERROR("RHIMesh::Init failed: Could not allocate Vertex Buffer for '{0}'!", debugName);
            Shutdown();
            return false;
        }

        RHIBufferDesc ibDesc = RHIBufferDesc::Index(ibSize, RHIMemoryLocation::DeviceUpload, debugName + "_IB");
        if (!m_IndexBuffer.Init(ibDesc, indices.data())) {
            AXLE_CORE_ERROR("RHIMesh::Init failed: Could not allocate Index Buffer for '{0}'!", debugName);
            Shutdown();
            return false;
        }

        if (m_Submeshes.empty()) {
            RHISubmesh defaultSubmesh;
            defaultSubmesh.Name = debugName.empty() ? "MainSubmesh" : debugName;
            defaultSubmesh.IndexOffset = 0;
            defaultSubmesh.IndexCount = m_IndexCount;
            defaultSubmesh.VertexOffset = 0;
            defaultSubmesh.VertexCount = m_VertexCount;
            defaultSubmesh.BoundingMin = m_BoundingMin;
            defaultSubmesh.BoundingMax = m_BoundingMax;
            m_Submeshes.push_back(defaultSubmesh);
        }

        return true;
    }

    bool RHIMesh::StreamUpload(RHIStreamer& streamer, RHICommandBuffer& cmdBuffer,
                              const std::vector<RHIVertex3D>& vertices, const std::vector<uint32_t>& indices,
                              const std::string& debugName) {
        if (!streamer.IsValid() || !cmdBuffer.IsValid() || !cmdBuffer.IsRecording()) {
            AXLE_CORE_ERROR("RHIMesh::StreamUpload failed: Invalid streamer or command buffer is not recording!");
            return false;
        }

        if (vertices.empty() || indices.empty()) {
            AXLE_CORE_ERROR("RHIMesh::StreamUpload failed: Vertex or Index array is empty! (Mesh: '{0}')", debugName);
            return false;
        }

        Shutdown();
        m_DebugName = debugName;
        m_VertexCount = static_cast<uint32_t>(vertices.size());
        m_IndexCount = static_cast<uint32_t>(indices.size());
        UpdateBoundingBox(vertices);

        uint64_t vbSize = static_cast<uint64_t>(vertices.size() * sizeof(RHIVertex3D));
        uint64_t ibSize = static_cast<uint64_t>(indices.size() * sizeof(uint32_t));

        // Allocate Device-local high performance VRAM buffers
        RHIBufferDesc vbDesc = RHIBufferDesc::Vertex(vbSize, RHIMemoryLocation::Device, debugName + "_VB");
        if (!m_VertexBuffer.Init(vbDesc)) {
            AXLE_CORE_ERROR("RHIMesh::StreamUpload: Device-local VB allocation failed for '{0}'!", debugName);
            Shutdown();
            return false;
        }

        RHIBufferDesc ibDesc = RHIBufferDesc::Index(ibSize, RHIMemoryLocation::Device, debugName + "_IB");
        if (!m_IndexBuffer.Init(ibDesc)) {
            AXLE_CORE_ERROR("RHIMesh::StreamUpload: Device-local IB allocation failed for '{0}'!", debugName);
            Shutdown();
            return false;
        }

        // Staged upload via RHIStreamer
        RHICopyBatch copyBatch = streamer.BeginCopyBatch();
        streamer.StreamBufferUpload(copyBatch, m_VertexBuffer, 0, vertices.data(), vbSize);
        streamer.StreamBufferUpload(copyBatch, m_IndexBuffer, 0, indices.data(), ibSize);

        // Transition destination buffers to CopyDest state before copy
        cmdBuffer.TransitionBuffer(m_VertexBuffer.GetNative(), RHIAccess::None, RHIAccess::CopyDest,
                                   RHIPipelineStage::AllCommands, RHIPipelineStage::Copy);
        cmdBuffer.TransitionBuffer(m_IndexBuffer.GetNative(), RHIAccess::None, RHIAccess::CopyDest,
                                   RHIPipelineStage::AllCommands, RHIPipelineStage::Copy);

        // Record GPU copy command
        streamer.CmdCopyStreamedData(cmdBuffer, copyBatch);

        // Transition destination buffers to Vertex/Index input state
        cmdBuffer.TransitionBuffer(m_VertexBuffer.GetNative(), RHIAccess::CopyDest, RHIAccess::VertexBuffer,
                                   RHIPipelineStage::Copy, RHIPipelineStage::VertexShader);
        cmdBuffer.TransitionBuffer(m_IndexBuffer.GetNative(), RHIAccess::CopyDest, RHIAccess::IndexBuffer,
                                   RHIPipelineStage::Copy, RHIPipelineStage::IndexInput);

        if (m_Submeshes.empty()) {
            RHISubmesh defaultSubmesh;
            defaultSubmesh.Name = debugName.empty() ? "MainSubmesh" : debugName;
            defaultSubmesh.IndexOffset = 0;
            defaultSubmesh.IndexCount = m_IndexCount;
            defaultSubmesh.VertexOffset = 0;
            defaultSubmesh.VertexCount = m_VertexCount;
            defaultSubmesh.BoundingMin = m_BoundingMin;
            defaultSubmesh.BoundingMax = m_BoundingMax;
            m_Submeshes.push_back(defaultSubmesh);
        }

        return true;
    }

    void RHIMesh::Bind(RHICommandBuffer& cmdBuffer) const {
        if (!IsValid()) return;
        cmdBuffer.SetVertexBuffer(0, m_VertexBuffer, 0, sizeof(RHIVertex3D));
        cmdBuffer.SetIndexBuffer(m_IndexBuffer, 0, nri::IndexType::UINT32);
    }

    void RHIMesh::Draw(RHICommandBuffer& cmdBuffer, uint32_t instanceCount) const {
        if (!IsValid() || m_IndexCount == 0) return;
        cmdBuffer.DrawIndexed(m_IndexCount, instanceCount, 0, 0, 0);
    }

    void RHIMesh::DrawSubmesh(RHICommandBuffer& cmdBuffer, uint32_t submeshIndex, uint32_t instanceCount) const {
        if (!IsValid() || submeshIndex >= m_Submeshes.size()) return;
        const auto& sub = m_Submeshes[submeshIndex];
        cmdBuffer.DrawIndexed(sub.IndexCount, instanceCount, sub.IndexOffset, static_cast<int32_t>(sub.VertexOffset), 0);
    }

    // --- Procedural Geometry Generators ---

    void RHIMesh::GenerateCube(float size, std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices) {
        float h = size * 0.5f;
        outVertices.clear();
        outVertices.reserve(24);

        // 6 faces * 4 vertices = 24 unique vertices with clean normals and UVs
        struct FaceDef {
            glm::vec3 normal;
            glm::vec4 tangent;
            glm::vec3 corners[4];
        };

        FaceDef faces[6] = {
            // Front (+Z)
            { { 0, 0, 1 }, { 1, 0, 0, 1 }, { { -h, -h,  h }, {  h, -h,  h }, {  h,  h,  h }, { -h,  h,  h } } },
            // Back (-Z)
            { { 0, 0,-1 }, {-1, 0, 0, 1 }, { {  h, -h, -h }, { -h, -h, -h }, { -h,  h, -h }, {  h,  h, -h } } },
            // Top (+Y)
            { { 0, 1, 0 }, { 1, 0, 0, 1 }, { { -h,  h,  h }, {  h,  h,  h }, {  h,  h, -h }, { -h,  h, -h } } },
            // Bottom (-Y)
            { { 0,-1, 0 }, { 1, 0, 0, 1 }, { { -h, -h, -h }, {  h, -h, -h }, {  h, -h,  h }, { -h, -h,  h } } },
            // Right (+X)
            { { 1, 0, 0 }, { 0, 0,-1, 1 }, { {  h, -h,  h }, {  h, -h, -h }, {  h,  h, -h }, {  h,  h,  h } } },
            // Left (-X)
            { {-1, 0, 0 }, { 0, 0, 1, 1 }, { { -h, -h, -h }, { -h, -h,  h }, { -h,  h,  h }, { -h,  h, -h } } }
        };

        glm::vec2 uvs[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

        outIndices.clear();
        outIndices.reserve(36);

        for (int f = 0; f < 6; ++f) {
            uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());
            for (int v = 0; v < 4; ++v) {
                RHIVertex3D vert;
                vert.Position = faces[f].corners[v];
                vert.Normal = faces[f].normal;
                vert.Tangent = faces[f].tangent;
                vert.TexCoord = uvs[v];
                vert.Color = glm::vec4(1.0f);
                outVertices.push_back(vert);
            }

            outIndices.push_back(baseIdx + 0);
            outIndices.push_back(baseIdx + 1);
            outIndices.push_back(baseIdx + 2);
            outIndices.push_back(baseIdx + 2);
            outIndices.push_back(baseIdx + 3);
            outIndices.push_back(baseIdx + 0);
        }
    }

    void RHIMesh::GenerateSphere(float radius, uint32_t stacks, uint32_t sectors,
                                 std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices) {
        outVertices.clear();
        outIndices.clear();

        for (uint32_t i = 0; i <= stacks; ++i) {
            float v = static_cast<float>(i) / static_cast<float>(stacks);
            float phi = v * PI;

            for (uint32_t j = 0; j <= sectors; ++j) {
                float u = static_cast<float>(j) / static_cast<float>(sectors);
                float theta = u * 2.0f * PI;

                float x = std::cos(theta) * std::sin(phi);
                float y = std::cos(phi);
                float z = std::sin(theta) * std::sin(phi);

                glm::vec3 norm(x, y, z);
                glm::vec3 pos = norm * radius;

                RHIVertex3D vert;
                vert.Position = pos;
                vert.Normal = norm;
                vert.TexCoord = glm::vec2(u, v);
                vert.Color = glm::vec4(1.0f);

                // Tangent along longitude
                glm::vec3 tangent(-std::sin(theta), 0.0f, std::cos(theta));
                if (glm::dot(tangent, tangent) > 1e-6f) {
                    vert.Tangent = glm::vec4(glm::normalize(tangent), 1.0f);
                } else {
                    vert.Tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                }

                outVertices.push_back(vert);
            }
        }

        for (uint32_t i = 0; i < stacks; ++i) {
            uint32_t k1 = i * (sectors + 1);
            uint32_t k2 = k1 + sectors + 1;

            for (uint32_t j = 0; j < sectors; ++j, ++k1, ++k2) {
                if (i != 0) {
                    outIndices.push_back(k1);
                    outIndices.push_back(k2);
                    outIndices.push_back(k1 + 1);
                }
                if (i != (stacks - 1)) {
                    outIndices.push_back(k1 + 1);
                    outIndices.push_back(k2);
                    outIndices.push_back(k2 + 1);
                }
            }
        }
    }

    void RHIMesh::GenerateCylinder(float radius, float height, uint32_t segments,
                                   std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices) {
        outVertices.clear();
        outIndices.clear();
        float halfH = height * 0.5f;

        // Sides
        uint32_t baseSideIdx = static_cast<uint32_t>(outVertices.size());
        for (uint32_t i = 0; i <= segments; ++i) {
            float u = static_cast<float>(i) / static_cast<float>(segments);
            float theta = u * 2.0f * PI;
            float cosT = std::cos(theta);
            float sinT = std::sin(theta);

            glm::vec3 normal(cosT, 0.0f, sinT);
            glm::vec4 tangent(-sinT, 0.0f, cosT, 1.0f);

            // Top vertex
            RHIVertex3D vTop;
            vTop.Position = glm::vec3(cosT * radius, halfH, sinT * radius);
            vTop.Normal = normal;
            vTop.Tangent = tangent;
            vTop.TexCoord = glm::vec2(u, 1.0f);
            vTop.Color = glm::vec4(1.0f);
            outVertices.push_back(vTop);

            // Bottom vertex
            RHIVertex3D vBot;
            vBot.Position = glm::vec3(cosT * radius, -halfH, sinT * radius);
            vBot.Normal = normal;
            vBot.Tangent = tangent;
            vBot.TexCoord = glm::vec2(u, 0.0f);
            vBot.Color = glm::vec4(1.0f);
            outVertices.push_back(vBot);
        }

        for (uint32_t i = 0; i < segments; ++i) {
            uint32_t idx = baseSideIdx + i * 2;
            outIndices.push_back(idx);
            outIndices.push_back(idx + 1);
            outIndices.push_back(idx + 2);

            outIndices.push_back(idx + 2);
            outIndices.push_back(idx + 1);
            outIndices.push_back(idx + 3);
        }
    }

    void RHIMesh::GenerateCone(float radius, float height, uint32_t segments,
                               std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices) {
        outVertices.clear();
        outIndices.clear();

        float slant = std::sqrt(radius * radius + height * height);
        float cosAlpha = radius / slant;
        float sinAlpha = height / slant;

        for (uint32_t i = 0; i <= segments; ++i) {
            float u = static_cast<float>(i) / static_cast<float>(segments);
            float theta = u * 2.0f * PI;
            float cosT = std::cos(theta);
            float sinT = std::sin(theta);

            glm::vec3 normal(cosT * sinAlpha, cosAlpha, sinT * sinAlpha);
            glm::vec4 tangent(-sinT, 0.0f, cosT, 1.0f);

            // Apex
            RHIVertex3D vApex;
            vApex.Position = glm::vec3(0.0f, height * 0.5f, 0.0f);
            vApex.Normal = normal;
            vApex.Tangent = tangent;
            vApex.TexCoord = glm::vec2(u, 1.0f);
            vApex.Color = glm::vec4(1.0f);
            outVertices.push_back(vApex);

            // Base
            RHIVertex3D vBase;
            vBase.Position = glm::vec3(cosT * radius, -height * 0.5f, sinT * radius);
            vBase.Normal = normal;
            vBase.Tangent = tangent;
            vBase.TexCoord = glm::vec2(u, 0.0f);
            vBase.Color = glm::vec4(1.0f);
            outVertices.push_back(vBase);
        }

        for (uint32_t i = 0; i < segments; ++i) {
            uint32_t idx = i * 2;
            outIndices.push_back(idx);
            outIndices.push_back(idx + 1);
            outIndices.push_back(idx + 3);
        }
    }

    void RHIMesh::GeneratePlane(float sizeX, float sizeZ, uint32_t resX, uint32_t resZ,
                                std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices) {
        outVertices.clear();
        outIndices.clear();

        float halfX = sizeX * 0.5f;
        float halfZ = sizeZ * 0.5f;

        for (uint32_t z = 0; z <= resZ; ++z) {
            float fz = static_cast<float>(z) / static_cast<float>(resZ);
            float posZ = -halfZ + fz * sizeZ;

            for (uint32_t x = 0; x <= resX; ++x) {
                float fx = static_cast<float>(x) / static_cast<float>(resX);
                float posX = -halfX + fx * sizeX;

                RHIVertex3D vert;
                vert.Position = glm::vec3(posX, 0.0f, posZ);
                vert.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
                vert.Tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                vert.TexCoord = glm::vec2(fx, fz);
                vert.Color = glm::vec4(1.0f);
                outVertices.push_back(vert);
            }
        }

        for (uint32_t z = 0; z < resZ; ++z) {
            for (uint32_t x = 0; x < resX; ++x) {
                uint32_t i0 = z * (resX + 1) + x;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = (z + 1) * (resX + 1) + x;
                uint32_t i3 = i2 + 1;

                outIndices.push_back(i0);
                outIndices.push_back(i2);
                outIndices.push_back(i1);

                outIndices.push_back(i1);
                outIndices.push_back(i2);
                outIndices.push_back(i3);
            }
        }
    }

    void RHIMesh::GenerateTerrainHeightfield(uint32_t resX, uint32_t resZ, const std::vector<float>& heights,
                                            const glm::vec3& scale, const glm::vec3& origin,
                                            const std::vector<glm::vec4>& splatWeights,
                                            std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices) {
        outVertices.clear();
        outIndices.clear();

        if (resX < 2 || resZ < 2 || heights.size() < resX * resZ) {
            return;
        }

        outVertices.reserve(resX * resZ);
        outIndices.reserve((resX - 1) * (resZ - 1) * 6);

        auto GetHeight = [&](int x, int z) -> float {
            x = std::clamp(x, 0, static_cast<int>(resX) - 1);
            z = std::clamp(z, 0, static_cast<int>(resZ) - 1);
            return heights[z * resX + x];
        };

        for (uint32_t z = 0; z < resZ; ++z) {
            for (uint32_t x = 0; x < resX; ++x) {
                float h = heights[z * resX + x];
                glm::vec3 pos = origin + glm::vec3(static_cast<float>(x) * scale.x, h * scale.y, static_cast<float>(z) * scale.z);

                // Central difference normal
                float hL = GetHeight(static_cast<int>(x) - 1, static_cast<int>(z));
                float hR = GetHeight(static_cast<int>(x) + 1, static_cast<int>(z));
                float hD = GetHeight(static_cast<int>(x), static_cast<int>(z) - 1);
                float hU = GetHeight(static_cast<int>(x), static_cast<int>(z) + 1);

                glm::vec3 normal(
                    (hL - hR) * scale.y,
                    2.0f * scale.x,
                    (hD - hU) * scale.y
                );
                normal = glm::normalize(normal);

                glm::vec4 tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                glm::vec2 uv(static_cast<float>(x) / static_cast<float>(resX - 1),
                             static_cast<float>(z) / static_cast<float>(resZ - 1));

                glm::vec4 splat = (!splatWeights.empty() && (z * resX + x) < splatWeights.size())
                    ? splatWeights[z * resX + x]
                    : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f); // Default to layer 0 (Grass)

                RHIVertex3D vert;
                vert.Position = pos;
                vert.Normal = normal;
                vert.Tangent = tangent;
                vert.TexCoord = uv;
                vert.Color = splat;
                outVertices.push_back(vert);
            }
        }

        for (uint32_t z = 0; z < resZ - 1; ++z) {
            for (uint32_t x = 0; x < resX - 1; ++x) {
                uint32_t i0 = z * resX + x;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = (z + 1) * resX + x;
                uint32_t i3 = i2 + 1;

                outIndices.push_back(i0);
                outIndices.push_back(i2);
                outIndices.push_back(i1);

                outIndices.push_back(i1);
                outIndices.push_back(i2);
                outIndices.push_back(i3);
            }
        }
    }

    // --- Synchronous Factory Constructors ---

    std::shared_ptr<RHIMesh> RHIMesh::CreateCube(float size) {
        std::vector<RHIVertex3D> vertices;
        std::vector<uint32_t> indices;
        GenerateCube(size, vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        mesh->Init(vertices, indices, "Cube");
        return mesh;
    }

    std::shared_ptr<RHIMesh> RHIMesh::CreateSphere(float radius, uint32_t stacks, uint32_t sectors) {
        std::vector<RHIVertex3D> vertices;
        std::vector<uint32_t> indices;
        GenerateSphere(radius, stacks, sectors, vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        mesh->Init(vertices, indices, "Sphere");
        return mesh;
    }

    std::shared_ptr<RHIMesh> RHIMesh::CreateCylinder(float radius, float height, uint32_t segments) {
        std::vector<RHIVertex3D> vertices;
        std::vector<uint32_t> indices;
        GenerateCylinder(radius, height, segments, vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        mesh->Init(vertices, indices, "Cylinder");
        return mesh;
    }

    std::shared_ptr<RHIMesh> RHIMesh::CreateCone(float radius, float height, uint32_t segments) {
        std::vector<RHIVertex3D> vertices;
        std::vector<uint32_t> indices;
        GenerateCone(radius, height, segments, vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        mesh->Init(vertices, indices, "Cone");
        return mesh;
    }

    std::shared_ptr<RHIMesh> RHIMesh::CreatePlane(float sizeX, float sizeZ, uint32_t resX, uint32_t resZ) {
        std::vector<RHIVertex3D> vertices;
        std::vector<uint32_t> indices;
        GeneratePlane(sizeX, sizeZ, resX, resZ, vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        mesh->Init(vertices, indices, "Plane");
        return mesh;
    }

    std::shared_ptr<RHIMesh> RHIMesh::CreateFromRaw(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
                                                    const std::vector<glm::vec3>& normals, const std::vector<glm::vec2>& uvs,
                                                    const glm::vec4& color, const std::string& name) {
        if (positions.empty() || indices.empty()) return nullptr;

        std::vector<RHIVertex3D> vertices(positions.size());
        for (size_t i = 0; i < positions.size(); ++i) {
            vertices[i].Position = positions[i];
            vertices[i].Normal = (i < normals.size()) ? normals[i] : glm::vec3(0.0f, 1.0f, 0.0f);
            vertices[i].TexCoord = (i < uvs.size()) ? uvs[i] : glm::vec2(0.0f);
            vertices[i].Color = color;
            vertices[i].Tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        }

        ComputeMeshTangents(vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        mesh->Init(vertices, indices, name.empty() ? "RawMesh" : name);
        return mesh;
    }

    std::shared_ptr<RHIMesh> RHIMesh::CreateFromAxleMesh(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
                                                         const glm::vec4& color, const std::string& name) {
        if (positions.empty() || indices.empty()) return nullptr;

        glm::vec3 minB(1e9f);
        glm::vec3 maxB(-1e9f);
        for (const auto& p : positions) {
            minB = glm::min(minB, p);
            maxB = glm::max(maxB, p);
        }
        glm::vec3 size = glm::max(maxB - minB, glm::vec3(0.01f));

        std::vector<RHIVertex3D> vertices(positions.size());
        for (size_t i = 0; i < positions.size(); ++i) {
            vertices[i].Position = positions[i];
            vertices[i].Normal = glm::vec3(0.0f, 1.0f, 0.0f); // Default normal; will be accumulated per triangle below
            vertices[i].TexCoord = glm::vec2((positions[i].x - minB.x) / size.x, (positions[i].z - minB.z) / size.z);
            vertices[i].Color = color;
            vertices[i].Tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        }

        // Accumulate triangle face normals
        std::vector<glm::vec3> faceNormals(positions.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];
            if (i0 < positions.size() && i1 < positions.size() && i2 < positions.size()) {
                glm::vec3 e1 = positions[i1] - positions[i0];
                glm::vec3 e2 = positions[i2] - positions[i0];
                glm::vec3 fn = glm::cross(e1, e2);
                faceNormals[i0] += fn;
                faceNormals[i1] += fn;
                faceNormals[i2] += fn;
            }
        }
        for (size_t i = 0; i < positions.size(); ++i) {
            if (glm::dot(faceNormals[i], faceNormals[i]) > 1e-6f) {
                vertices[i].Normal = glm::normalize(faceNormals[i]);
            }
        }

        ComputeMeshTangents(vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        mesh->Init(vertices, indices, name.empty() ? "AxleMesh" : name);
        return mesh;
    }

    std::shared_ptr<RHIMesh> RHIMesh::CreateTerrainHeightfield(uint32_t resX, uint32_t resZ, const std::vector<float>& heights,
                                                               const glm::vec3& scale, const glm::vec3& origin,
                                                               const std::vector<glm::vec4>& splatWeights) {
        std::vector<RHIVertex3D> vertices;
        std::vector<uint32_t> indices;
        GenerateTerrainHeightfield(resX, resZ, heights, scale, origin, splatWeights, vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        mesh->Init(vertices, indices, "TerrainHeightfield");
        return mesh;
    }

    // --- Streamed Factory Constructors ---

    std::shared_ptr<RHIMesh> RHIMesh::StreamCube(RHIStreamer& streamer, RHICommandBuffer& cmd, float size) {
        std::vector<RHIVertex3D> vertices;
        std::vector<uint32_t> indices;
        GenerateCube(size, vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        if (!mesh->StreamUpload(streamer, cmd, vertices, indices, "StreamedCube")) return nullptr;
        return mesh;
    }

    std::shared_ptr<RHIMesh> RHIMesh::StreamSphere(RHIStreamer& streamer, RHICommandBuffer& cmd, float radius, uint32_t stacks, uint32_t sectors) {
        std::vector<RHIVertex3D> vertices;
        std::vector<uint32_t> indices;
        GenerateSphere(radius, stacks, sectors, vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        if (!mesh->StreamUpload(streamer, cmd, vertices, indices, "StreamedSphere")) return nullptr;
        return mesh;
    }

    std::shared_ptr<RHIMesh> RHIMesh::StreamPlane(RHIStreamer& streamer, RHICommandBuffer& cmd, float sizeX, float sizeZ, uint32_t resX, uint32_t resZ) {
        std::vector<RHIVertex3D> vertices;
        std::vector<uint32_t> indices;
        GeneratePlane(sizeX, sizeZ, resX, resZ, vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        if (!mesh->StreamUpload(streamer, cmd, vertices, indices, "StreamedPlane")) return nullptr;
        return mesh;
    }

    std::shared_ptr<RHIMesh> RHIMesh::StreamFromAxleMesh(RHIStreamer& streamer, RHICommandBuffer& cmd,
                                                         const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
                                                         const glm::vec4& color, const std::string& name) {
        if (positions.empty() || indices.empty()) return nullptr;

        glm::vec3 minB(1e9f);
        glm::vec3 maxB(-1e9f);
        for (const auto& p : positions) {
            minB = glm::min(minB, p);
            maxB = glm::max(maxB, p);
        }
        glm::vec3 size = glm::max(maxB - minB, glm::vec3(0.01f));

        std::vector<RHIVertex3D> vertices(positions.size());
        for (size_t i = 0; i < positions.size(); ++i) {
            vertices[i].Position = positions[i];
            vertices[i].Normal = glm::vec3(0.0f, 1.0f, 0.0f);
            vertices[i].TexCoord = glm::vec2((positions[i].x - minB.x) / size.x, (positions[i].z - minB.z) / size.z);
            vertices[i].Color = color;
            vertices[i].Tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        }

        // Accumulate face normals
        std::vector<glm::vec3> faceNormals(positions.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];
            if (i0 < positions.size() && i1 < positions.size() && i2 < positions.size()) {
                glm::vec3 e1 = positions[i1] - positions[i0];
                glm::vec3 e2 = positions[i2] - positions[i0];
                glm::vec3 fn = glm::cross(e1, e2);
                faceNormals[i0] += fn;
                faceNormals[i1] += fn;
                faceNormals[i2] += fn;
            }
        }
        for (size_t i = 0; i < positions.size(); ++i) {
            if (glm::dot(faceNormals[i], faceNormals[i]) > 1e-6f) {
                vertices[i].Normal = glm::normalize(faceNormals[i]);
            }
        }

        ComputeMeshTangents(vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        if (!mesh->StreamUpload(streamer, cmd, vertices, indices, name)) return nullptr;
        return mesh;
    }

    std::shared_ptr<RHIMesh> RHIMesh::StreamTerrainHeightfield(RHIStreamer& streamer, RHICommandBuffer& cmd,
                                                               uint32_t resX, uint32_t resZ, const std::vector<float>& heights,
                                                               const glm::vec3& scale, const glm::vec3& origin,
                                                               const std::vector<glm::vec4>& splatWeights) {
        std::vector<RHIVertex3D> vertices;
        std::vector<uint32_t> indices;
        GenerateTerrainHeightfield(resX, resZ, heights, scale, origin, splatWeights, vertices, indices);

        auto mesh = std::make_shared<RHIMesh>();
        if (!mesh->StreamUpload(streamer, cmd, vertices, indices, "StreamedTerrain")) return nullptr;
        return mesh;
    }

}
