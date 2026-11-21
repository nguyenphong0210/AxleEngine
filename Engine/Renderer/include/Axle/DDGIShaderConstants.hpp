#pragma once

#include <glm/glm.hpp>

namespace Axle::DDGIContract {

    // Shared 256-byte (CBV aligned) DDGI uniform contract shared between C++ and HLSL.
    struct alignas(256) DDGIShaderConstants {
        glm::vec4 ScreenSize{};      // View width, height, 1/width, 1/height
        glm::vec4 AtlasSize{};       // IrradW, IrradH, DistW, DistH
        glm::vec4 GridDimensions{};  // x, y, z, total probeCount
        glm::vec4 GridOrigin{};      // xyz origin, w = atlasCols
        glm::vec4 GridSpacing{};     // xyz spacing, w = maxRayDistance
        glm::vec4 Params{};          // x = energyMultiplier, y = hysteresis, z = viewBias, w = normalBias
        glm::vec4 UpdateParams{};    // x = startProbeIndex, y = updateCount, z = captureCols, w = captureFaceRes
        glm::vec4 Flags{};           // x = samplingEnabled, y = hasHistoryForMultiBounce, z = seedResetFrame, w = atlasRows
        glm::vec4 ClipParams{ 0.05f, 100.0f, 0.0f, 0.0f }; // x = captureNear, y = captureFar
        glm::vec4 CaptureShadowDepth{}; // World-space plane for sampling the main-view CSM atlas
        glm::vec4 Reserved[6]{};     // Padding to 256 bytes
    };
    static_assert(sizeof(DDGIShaderConstants) == 256, "DDGIShaderConstants must be 256 bytes aligned!");

} // namespace Axle::DDGIContract
