#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdint>

namespace Axle::RHI {

    // =========================================================================
    // RHIObjectPushConstants (Root Constants)
    // =========================================================================
    // High-performance per-entity transform and shading parameters.
    // Size: EXACTLY 128 bytes (32 DWORDs).
    // Complies with universal 128-byte Push Constant hardware limit across all
    // Direct3D 12 and Vulkan 1.3 GPU architectures (NVIDIA, AMD, Intel, Apple).
    // Streamed dynamically with 0 CPU-GPU pipeline stalls and 0 descriptor overhead.
    struct alignas(16) RHIObjectPushConstants {
        glm::mat4 ModelViewProjection{ 1.0f };       // 64 bytes (Offset 0)
        glm::vec4 ColorPrimary{ 0.85f, 0.88f, 0.92f, 1.0f }; // 16 bytes (Offset 64)
        glm::vec4 ColorSecondary{ 0.45f, 0.48f, 0.52f, 1.0f }; // 16 bytes (Offset 80)
        glm::vec4 Params{ 4.0f, 1.0f, 0.5f, 0.0f };  // 16 bytes (Offset 96) - x: TileScale, y: PatternMode, z: Roughness, w: Metallic
        glm::vec4 SunDirAndIntensity{ 0.45f, 0.85f, 0.25f, 1.2f }; // 16 bytes (Offset 112) - xyz: Light dir, w: Sun intensity

        static constexpr uint32_t GetByteSize() { return sizeof(RHIObjectPushConstants); }
    };
    static_assert(sizeof(RHIObjectPushConstants) == 128, "RHIObjectPushConstants must be exactly 128 bytes for hardware push constants!");

    // =========================================================================
    // RHISceneConstants (Uniform Buffer Ring Streamer)
    // =========================================================================
    // Comprehensive global camera, view, and lighting parameters.
    // Size: 256 bytes (Hardware Constant Buffer 256-byte aligned).
    // Streamed per-frame via RHIStreamer dynamic ring buffer.
    struct alignas(256) RHISceneConstants {
        glm::mat4 ViewProjection{ 1.0f };            // 64 bytes
        glm::mat4 View{ 1.0f };                      // 64 bytes
        glm::mat4 Projection{ 1.0f };                // 64 bytes
        glm::vec4 CameraPosition{ 0.0f, 0.0f, 0.0f, 1.0f }; // 16 bytes - xyz: World Pos, w: Elapsed Time
        glm::vec4 AmbientLight{ 0.25f, 0.28f, 0.35f, 1.0f }; // 16 bytes - xyz: Color, w: Intensity
        glm::vec4 SunDirection{ 0.45f, 0.85f, 0.25f, 1.2f }; // 16 bytes - xyz: Normalized Dir, w: Intensity
        glm::vec4 SunColor{ 1.0f, 0.98f, 0.92f, 1.0f };      // 16 bytes - rgba

        static constexpr uint32_t GetByteSize() { return sizeof(RHISceneConstants); }
    };
    static_assert(sizeof(RHISceneConstants) == 256, "RHISceneConstants must be 256 bytes aligned!");

    // Helper to compute combined ModelViewProjection matrix
    inline glm::mat4 ComputeMVP(const glm::mat4& viewProjection, const glm::mat4& model) {
        return viewProjection * model;
    }

}
