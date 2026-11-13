#pragma once
#include <Axle/PhysicsDrawSettings.hpp>
#include <glm/glm.hpp>
#include <cstdint>

namespace Axle {
    struct SceneRenderSettings {
        bool EnableShadows = true;
        uint32_t LocalShadowResolution = 512; // One tile; bounded 4x4 atlas.
        uint8_t MaxShadowedSpotLights = 4, MaxShadowedPointLights = 2;
        float LocalShadowDistance = 60.f;
        uint8_t ShadowCascadeCount = 4; // Modern RHI: 3 or 4 cascades.
        bool EnableSkybox = true;
        bool EnableStudioLightLobes = false; // Compatibility option; previews author explicit studio lights.
        bool EnableParticles = true;
        bool EnableToneMapping = false;
        bool EnableFXAA = false;
        bool EnableGTAO = false;
        bool EnableContactShadows = false;
        uint8_t MSAASamples = 1; // 1 (disabled), 2, 4, 8
        bool Wireframe = false;
        bool ShowPhysicsDebug = false;
        PhysicsDrawSettings PhysicsDrawSettings;
        float PhysicsDepthBias = 0.0001f;
        float PhysicsAlphaMultiplier = 1.0f;
        float Exposure = 1.0f;
        int ToneMappingMode = 0; // 0: ACES Filmic, 1: Reinhard, 2: Uncharted 2, 3: Linear Bypass
        float Gamma = 2.2f;
        float WhitePoint = 4.0f; // Configurable white point for Extended Reinhard & Uncharted 2
        float FXAASubpixQuality = 0.75f;
        float FXAAEdgeThreshold = 0.166f;
        float FXAAEdgeThresholdMin = 0.0833f;
        float GTAORadiusPixels = 8.0f;
        float GTAOStrength = 1.0f;
        float ContactShadowLengthPixels = 32.0f;
        float ContactShadowStrength = 1.0f;
        glm::vec4 ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f }; // Configurable background clear color (Supports transparent clear for editor viewports)
    };

}
