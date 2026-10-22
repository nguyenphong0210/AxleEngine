#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <Axle/RHI/RHIStreamer.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

namespace Axle::RHI {

    struct RHISubmesh {
        std::string Name = "Submesh";
        uint32_t IndexOffset = 0;
        uint32_t IndexCount = 0;
        uint32_t VertexOffset = 0;
        uint32_t VertexCount = 0;
        glm::vec3 BoundingMin{ 0.0f };
        glm::vec3 BoundingMax{ 0.0f };
    };

    class RHIMesh {
    public:
        RHIMesh() = default;
        ~RHIMesh();

        RHIMesh(const RHIMesh&) = delete;
        RHIMesh& operator=(const RHIMesh&) = delete;

        RHIMesh(RHIMesh&& other) noexcept;
        RHIMesh& operator=(RHIMesh&& other) noexcept;

        // Synchronous VRAM allocation & upload (via HostUpload / DeviceUpload)
        bool Init(const std::vector<RHIVertex3D>& vertices, const std::vector<uint32_t>& indices, const std::string& debugName = "");

        // Asynchronous Staging Upload to Device-local VRAM via RHIStreamer ring buffer
        bool StreamUpload(RHIStreamer& streamer, RHICommandBuffer& cmdBuffer,
                          const std::vector<RHIVertex3D>& vertices, const std::vector<uint32_t>& indices,
                          const std::string& debugName = "");

        void Shutdown();

        // GPU Command Recording helpers
        void Bind(RHICommandBuffer& cmdBuffer) const;
        void Draw(RHICommandBuffer& cmdBuffer, uint32_t instanceCount = 1) const;
        void DrawSubmesh(RHICommandBuffer& cmdBuffer, uint32_t submeshIndex, uint32_t instanceCount = 1) const;

        bool IsValid() const { return m_VertexBuffer.IsValid() && m_IndexBuffer.IsValid() && m_IndexCount > 0; }

        RHIBuffer& GetVertexBuffer() { return m_VertexBuffer; }
        const RHIBuffer& GetVertexBuffer() const { return m_VertexBuffer; }

        RHIBuffer& GetIndexBuffer() { return m_IndexBuffer; }
        const RHIBuffer& GetIndexBuffer() const { return m_IndexBuffer; }

        uint32_t GetVertexCount() const { return m_VertexCount; }
        uint32_t GetIndexCount() const { return m_IndexCount; }

        const std::vector<RHISubmesh>& GetSubmeshes() const { return m_Submeshes; }
        void AddSubmesh(const RHISubmesh& submesh) { m_Submeshes.push_back(submesh); }

        const glm::vec3& GetBoundingMin() const { return m_BoundingMin; }
        const glm::vec3& GetBoundingMax() const { return m_BoundingMax; }
        glm::vec3 GetBoundingSize() const { return m_BoundingMax - m_BoundingMin; }
        glm::vec3 GetBoundingCenter() const { return (m_BoundingMin + m_BoundingMax) * 0.5f; }

        const std::string& GetDebugName() const { return m_DebugName; }
        void SetDebugName(const std::string& name) { m_DebugName = name; }

        // --- Procedural Geometry Generators (Vertices & Indices) ---
        static void GenerateCube(float size, std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices);
        static void GenerateSphere(float radius, uint32_t stacks, uint32_t sectors, std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices);
        static void GenerateCylinder(float radius, float height, uint32_t segments, std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices);
        static void GenerateCone(float radius, float height, uint32_t segments, std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices);
        static void GeneratePlane(float sizeX, float sizeZ, uint32_t resX, uint32_t resZ, std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices);
        static void GenerateTerrainHeightfield(uint32_t resX, uint32_t resZ, const std::vector<float>& heights, const glm::vec3& scale, const glm::vec3& origin,
                                              const std::vector<glm::vec4>& splatWeights, std::vector<RHIVertex3D>& outVertices, std::vector<uint32_t>& outIndices);

        // --- Convenient Factory Constructors (Synchronous Init) ---
        static std::shared_ptr<RHIMesh> CreateCube(float size = 1.0f);
        static std::shared_ptr<RHIMesh> CreateSphere(float radius = 1.0f, uint32_t stacks = 24, uint32_t sectors = 32);
        static std::shared_ptr<RHIMesh> CreateCylinder(float radius = 1.0f, float height = 2.0f, uint32_t segments = 32);
        static std::shared_ptr<RHIMesh> CreateCone(float radius = 1.0f, float height = 2.0f, uint32_t segments = 32);
        static std::shared_ptr<RHIMesh> CreatePlane(float sizeX = 1.0f, float sizeZ = 1.0f, uint32_t resX = 1, uint32_t resZ = 1);
        static std::shared_ptr<RHIMesh> CreateFromRaw(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
                                                      const std::vector<glm::vec3>& normals = {}, const std::vector<glm::vec2>& uvs = {},
                                                      const glm::vec4& color = glm::vec4(1.0f), const std::string& name = "");
        static std::shared_ptr<RHIMesh> CreateFromAxleMesh(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
                                                           const glm::vec4& color = glm::vec4(0.85f, 0.88f, 0.92f, 1.0f), const std::string& name = "");
        static std::shared_ptr<RHIMesh> CreateTerrainHeightfield(uint32_t resX, uint32_t resZ, const std::vector<float>& heights,
                                                                 const glm::vec3& scale = glm::vec3(1.0f), const glm::vec3& origin = glm::vec3(0.0f),
                                                                 const std::vector<glm::vec4>& splatWeights = {});

        // --- Streamed Factory Constructors (Asynchronous Staging via RHIStreamer) ---
        static std::shared_ptr<RHIMesh> StreamCube(RHIStreamer& streamer, RHICommandBuffer& cmd, float size = 1.0f);
        static std::shared_ptr<RHIMesh> StreamSphere(RHIStreamer& streamer, RHICommandBuffer& cmd, float radius = 1.0f, uint32_t stacks = 24, uint32_t sectors = 32);
        static std::shared_ptr<RHIMesh> StreamPlane(RHIStreamer& streamer, RHICommandBuffer& cmd, float sizeX = 1.0f, float sizeZ = 1.0f, uint32_t resX = 1, uint32_t resZ = 1);
        static std::shared_ptr<RHIMesh> StreamFromAxleMesh(RHIStreamer& streamer, RHICommandBuffer& cmd,
                                                           const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
                                                           const glm::vec4& color = glm::vec4(0.85f, 0.88f, 0.92f, 1.0f), const std::string& name = "AxleMesh");
        static std::shared_ptr<RHIMesh> StreamTerrainHeightfield(RHIStreamer& streamer, RHICommandBuffer& cmd,
                                                                 uint32_t resX, uint32_t resZ, const std::vector<float>& heights,
                                                                 const glm::vec3& scale = glm::vec3(1.0f), const glm::vec3& origin = glm::vec3(0.0f),
                                                                 const std::vector<glm::vec4>& splatWeights = {});

    private:
        void UpdateBoundingBox(const std::vector<RHIVertex3D>& vertices);

        RHIBuffer m_VertexBuffer;
        RHIBuffer m_IndexBuffer;
        uint32_t m_VertexCount = 0;
        uint32_t m_IndexCount = 0;
        std::vector<RHISubmesh> m_Submeshes;

        glm::vec3 m_BoundingMin{ 0.0f };
        glm::vec3 m_BoundingMax{ 0.0f };
        std::string m_DebugName;
    };

}
