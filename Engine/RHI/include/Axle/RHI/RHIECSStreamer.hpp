#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIStreamer.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <entt/entt.hpp>

#include <cstdint>
#include <vector>
#include <functional>
#include <memory>

#ifdef _WIN32
#undef min
#undef max
#endif

namespace Axle::RHI {

    // =========================================================================
    // GPU Instance Data Layout (16-byte aligned for SSBO & Root Descriptors)
    // =========================================================================
    struct alignas(16) GPUInstanceData {
        glm::mat4 WorldTransform{ 1.0f }; // 64 bytes (Object-to-World matrix)
        glm::vec4 Color{ 1.0f };          // 16 bytes (Base color / Albedo tint)
        uint32_t EntityID = 0;            // 4 bytes  (GPU mouse picking / selection ID)
        float Roughness = 0.5f;           // 4 bytes  (PBR Roughness)
        float Metallic = 0.0f;            // 4 bytes  (PBR Metallic)
        uint32_t CustomFlags = 0;         // 4 bytes  (Custom shader flags / animation indices)
    };
    static_assert(sizeof(GPUInstanceData) % 16 == 0, "GPUInstanceData must be aligned to 16 bytes for HLSL/GLSL SSBO");

    // =========================================================================
    // GPU Indirect Draw Command Signatures (Matches D3D12 / Vulkan / NRI)
    // =========================================================================
    struct alignas(4) GPUIndirectDrawCommand {
        uint32_t VertexCount = 0;
        uint32_t InstanceCount = 0;
        uint32_t FirstVertex = 0;
        uint32_t FirstInstance = 0;
    };
    static_assert(sizeof(GPUIndirectDrawCommand) == sizeof(nri::DrawDesc), "Size must match nri::DrawDesc");

    struct alignas(4) GPUIndirectDrawIndexedCommand {
        uint32_t IndexCount = 0;
        uint32_t InstanceCount = 0;
        uint32_t FirstIndex = 0;
        int32_t  BaseVertex = 0;
        uint32_t FirstInstance = 0;
    };
    static_assert(sizeof(GPUIndirectDrawIndexedCommand) == sizeof(nri::DrawIndexedDesc), "Size must match nri::DrawIndexedDesc");

    // Result of an ECS streaming operation
    struct RHIECSStreamResult {
        RHIStreamedBuffer InstanceBuffer;    // Dynamic Structured Buffer (SSBO) containing packed GPUInstanceData
        RHIStreamedBuffer IndirectBuffer;    // Dynamic Indirect Argument Buffer containing GPUIndirectDraw(Indexed)Command
        uint32_t DrawCount = 0;             // Number of indirect draw commands
        uint32_t TotalInstances = 0;        // Total instances streamed
        bool IsValid() const { return InstanceBuffer.IsValid() && IndirectBuffer.IsValid() && TotalInstances > 0; }
        operator bool() const { return IsValid(); }
    };

    // Mesh batch key / specification for grouping instances
    struct RHIMeshBatchKey {
        uint32_t IndexCount = 0;
        uint32_t FirstIndex = 0;
        int32_t  BaseVertex = 0;
        uint64_t MeshAssetHandle = 0;
    };

    // Multi-batch item for drawing multiple different sub-meshes with a single indirect call
    struct RHIMultiMeshBatch {
        RHIMeshBatchKey Key;
        std::vector<GPUInstanceData> Instances;
    };

    class RHIECSStreamer {
    public:
        // Generic direct vector streaming of packed GPUInstanceData into SSBO + Indirect Command Buffer
        static RHIECSStreamResult StreamInstanceData(
            RHIStreamer& streamer,
            const std::vector<GPUInstanceData>& instances,
            const RHIMeshBatchKey& batchKey
        );

        // Multi-batch streaming: groups arbitrary mesh instances into separate sub-draw commands within a single indirect buffer
        static RHIECSStreamResult StreamMultiBatches(
            RHIStreamer& streamer,
            const std::vector<RHIMultiMeshBatch>& batches
        );

        // Templated EnTT view streaming: iterates over any EnTT component combination,
        // packs using a user-supplied extractor functor, and streams directly into GPU memory
        template <typename... Comps, typename ExtractorFunc>
        static RHIECSStreamResult StreamView(
            RHIStreamer& streamer,
            const entt::registry& registry,
            const RHIMeshBatchKey& batchKey,
            ExtractorFunc&& extractor)
        {
            std::vector<GPUInstanceData> instances;
            auto view = registry.view<Comps...>();
            instances.reserve(view.size_hint());

            for (auto entity : view) {
                GPUInstanceData data = {};
                if (extractor(entity, view.template get<Comps>(entity)..., data)) {
                    instances.push_back(data);
                }
            }

            return StreamInstanceData(streamer, instances, batchKey);
        }

        // Templated multi-batch EnTT view streaming with sub-mesh classifier
        template <typename... Comps, typename ClassifierFunc>
        static RHIECSStreamResult StreamViewBatched(
            RHIStreamer& streamer,
            const entt::registry& registry,
            ClassifierFunc&& classifier)
        {
            std::unordered_map<uint64_t, RHIMultiMeshBatch> batchMap;
            auto view = registry.view<Comps...>();

            for (auto entity : view) {
                GPUInstanceData data = {};
                RHIMeshBatchKey key = {};
                if (classifier(entity, view.template get<Comps>(entity)..., data, key)) {
                    auto& b = batchMap[key.MeshAssetHandle];
                    b.Key = key;
                    b.Instances.push_back(data);
                }
            }

            std::vector<RHIMultiMeshBatch> batches;
            batches.reserve(batchMap.size());
            for (auto& [handle, b] : batchMap) {
                if (!b.Instances.empty()) {
                    batches.push_back(std::move(b));
                }
            }

            return StreamMultiBatches(streamer, batches);
        }
    };

}
