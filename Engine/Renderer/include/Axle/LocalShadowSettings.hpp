#pragma once
#include <cstdint>

namespace Axle {
    enum class AreaLightShape : uint8_t {
        Rectangle = 0,
        Disk = 1
    };

    enum class AreaLightShadowPolicy : uint8_t {
        UnshadowedAnalyticLTC = 0
    };

    struct LocalShadowSettings {
        bool CastShadows = true;
        // Receiver offsets in world units, independent of perspective depth.
        float Bias = .002f, SlopeBias = .005f, NormalBias = .01f;
        float FilterRadius = 1.f, NearPlane = .05f;
    };
}
