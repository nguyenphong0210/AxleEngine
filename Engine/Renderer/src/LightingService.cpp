#include <Axle/LightingService.hpp>
#include <algorithm>
#include <cmath>
namespace Axle {
    namespace {
        // Preserve the existing artist/physical intensity thresholds during
        // migration. Fidelity/unit redesign belongs to M15.
        constexpr float DirectSunlightIlluminance=100000.0f;
        float DirectionalIlluminance(float intensity) {
            return intensity > 100.0f ? intensity : intensity * DirectSunlightIlluminance;
        }

        float PointIntensity(float intensity) {
            return intensity > 10.0f ? intensity : intensity * 1000.0f;
        }

        float SpotIntensity(float intensity) {
            return intensity > 10.0f ? intensity : intensity * 500.0f;
        }

        glm::vec3 NormalizedFallbackDirection(const glm::vec3& direction) {
            const float length=glm::length(direction);
            if (std::isfinite(length) && length > 1e-4f) return direction / length;
            return {0.0f, 1.0f, 0.0f};
        }
    }

    LightingFrame LightingService::BuildFrame(const RenderWorld& world, const SceneRenderSettings& settings, const ShadowPublication& shadows) const {
        LightingFrame frame;
        frame.Scene=ResolveSceneLighting(world);
        frame.Shadows=shadows;
        frame.DirectionalCount=world.DirectionalLight?1u:0u;
        frame.DirectionalCount+=std::min<uint32_t>(static_cast<uint32_t>(world.AdditionalDirectionalLights.size()), MaxLightingPointLights);

        frame.Sun.Direction=world.DirectionalLight ? world.DirectionalLight->Direction : NormalizedFallbackDirection(frame.Scene.SunDirection);
        frame.Sun.Illuminance=world.DirectionalLight ? DirectionalIlluminance(world.DirectionalLight->Intensity) : 0.0f;
        frame.Sun.Color=world.DirectionalLight ? world.DirectionalLight->Color : frame.Scene.SunColor;
        frame.Sun.AmbientIntensity=frame.Scene.AmbientIntensity;
        frame.Sun.AmbientColor=frame.Scene.AmbientColor;
        if (world.DirectionalLight) {
            frame.Sun.ShadowBias=world.DirectionalLight->ShadowBias;
            frame.Sun.ShadowFilterRadius=world.DirectionalLight->ShadowFilterRadius;
            frame.Sun.SolarAngularRadius=std::clamp(world.DirectionalLight->SolarAngularRadius, 0.0f, 0.25f);
        }

        uint32_t pointIndex=0;
        for (const auto& light : world.AdditionalDirectionalLights) {
            if (pointIndex >= MaxLightingPointLights) break;
            auto& packed=frame.PointLights[pointIndex++];
            packed.Position=light.Direction;
            packed.Color=light.Color;
            packed.Intensity=DirectionalIlluminance(light.Intensity)*0.000035f;
            packed.Directional=true;
        }
        for (const auto& light : world.PointLights) {
            if (pointIndex >= MaxLightingPointLights) break;
            auto& packed=frame.PointLights[pointIndex++];
            packed.Position=light.Position;
            packed.Radius=light.Radius;
            packed.Color=light.Color;
            packed.Intensity=PointIntensity(light.Intensity);
            packed.Falloff=light.Falloff;
            packed.MinRadius=0.05f;
        }
        frame.PointCount=pointIndex;

        uint32_t spotIndex=0;
        for (const auto& light : world.SpotLights) {
            if (spotIndex >= MaxLightingSpotLights) break;
            auto& packed=frame.SpotLights[spotIndex++];
            packed.Position=light.Position;
            packed.Range=light.Range;
            packed.Direction=light.Direction;
            packed.Intensity=SpotIntensity(light.Intensity);
            packed.Color=light.Color;
            packed.CosInnerAngle=std::cos(glm::radians(light.InnerAngle));
            packed.CosOuterAngle=std::cos(glm::radians(light.OuterAngle));
            packed.Falloff=1.0f;
        }
        frame.SpotCount=spotIndex;

        uint32_t rectIndex=0;
        for (const auto& light : world.RectLights) {
            if (rectIndex >= MaxLightingRectLights) break;
            auto& packed=frame.RectLights[rectIndex++];
            packed.Position=light.Position;
            packed.Width=light.Width;
            packed.Direction=light.Direction;
            packed.Height=light.Height;
            packed.Up=light.Up;
            packed.Range=light.Range;
            packed.Color=light.Color;
            packed.SignedIntensity=light.TwoSided ? -std::abs(light.Intensity) : std::abs(light.Intensity);
        }
        frame.RectCount=rectIndex;

        if (!settings.EnableShadows) frame.Shadows={};
        else {
            frame.Shadows.CascadedAvailable = shadows.CascadedAvailable && bool(shadows.CascadeGeneration);
            frame.Shadows.LocalAtlasAvailable = shadows.LocalAtlasAvailable && bool(shadows.LocalGeneration);
            if (!frame.Shadows.CascadedAvailable) frame.Shadows.CascadeGeneration={};
            if (!frame.Shadows.LocalAtlasAvailable) frame.Shadows.LocalGeneration={};
            if (!frame.Shadows.CascadedAvailable && !frame.Shadows.LocalAtlasAvailable) {
                frame.Status=LightingFrameStatus::Fallback;
                frame.Diagnostic="shadow_publication_unavailable: direct lighting remains valid without receiver atlases";
            }
        }
        return frame;
    }
}
