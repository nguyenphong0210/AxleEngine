#pragma once
#include <Axle/RenderWorld.hpp>
#include <Axle/SceneLighting.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/RHI/RHIResource.hpp>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
namespace Axle {
    constexpr uint32_t MaxLightingPointLights = 16;
    constexpr uint32_t MaxLightingSpotLights = 16;
    constexpr uint32_t MaxLightingRectLights = 2;

    enum class LightingFrameStatus : uint8_t { Supported, Fallback, Failed };
    struct ShadowPublication {
        bool CascadedAvailable=false, LocalAtlasAvailable=false;
        RHI::RHIResourceHandle CascadeGeneration, LocalGeneration;
    };
    struct LightingDirectionalFrame {
        glm::vec3 Direction{0.45f, 1.0f, 0.65f};
        float Illuminance=0.0f;
        glm::vec3 Color{1.0f, 0.98f, 0.92f};
        float AmbientIntensity=0.0f;
        glm::vec3 AmbientColor{0.22f, 0.25f, 0.32f};
        float ShadowBias=0.0012f;
        float ShadowFilterRadius=2.0f;
        float SolarAngularRadius=0.00462512f;
    };
    struct LightingPointFrame {
        glm::vec3 Position{0.0f};
        float Radius=0.0f;
        glm::vec3 Color{0.0f};
        float Intensity=0.0f;
        float Falloff=0.0f;
        float MinRadius=0.0f;
        bool Directional=false;
    };
    struct LightingSpotFrame {
        glm::vec3 Position{0.0f};
        float Range=0.0f;
        glm::vec3 Direction{0.0f, -1.0f, 0.0f};
        float Intensity=0.0f;
        glm::vec3 Color{0.0f};
        float CosInnerAngle=1.0f;
        float CosOuterAngle=1.0f;
        float Falloff=1.0f;
    };
    struct LightingRectFrame {
        glm::vec3 Position{0.0f};
        float Width=0.0f;
        glm::vec3 Direction{0.0f, 0.0f, -1.0f};
        float Height=0.0f;
        glm::vec3 Up{0.0f, 1.0f, 0.0f};
        float Range=0.0f;
        glm::vec3 Color{0.0f};
        float SignedIntensity=0.0f;
    };
    struct LightingFrame {
        LightingFrameStatus Status=LightingFrameStatus::Supported;
        SceneLighting Scene;
        LightingDirectionalFrame Sun;
        std::array<LightingPointFrame,MaxLightingPointLights> PointLights{};
        std::array<LightingSpotFrame,MaxLightingSpotLights> SpotLights{};
        std::array<LightingRectFrame,MaxLightingRectLights> RectLights{};
        // PointCount is the occupied native point-array prefix, including
        // additional directionals encoded with Directional=true.
        uint32_t DirectionalCount=0, PointCount=0, SpotCount=0, RectCount=0;
        ShadowPublication Shadows;
        std::string Diagnostic;
        explicit operator bool() const { return Status != LightingFrameStatus::Failed; }
    };
    class LightingService { public: LightingFrame BuildFrame(const RenderWorld&, const SceneRenderSettings&, const ShadowPublication& = {}) const; };
}
