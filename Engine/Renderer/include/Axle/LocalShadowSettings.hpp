#pragma once
namespace Axle {
    struct LocalShadowSettings {
        bool CastShadows = true;
        // Receiver offsets in world units, independent of perspective depth.
        float Bias = .002f, SlopeBias = .005f, NormalBias = .01f;
        float FilterRadius = 1.f, NearPlane = .05f;
    };
}
