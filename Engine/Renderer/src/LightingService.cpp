#include <Axle/LightingService.hpp>
#include <algorithm>
#include <cmath>
namespace Axle {
    namespace {
        // Authored intensities are dimensionless multipliers. The directional
        // reference is 100,000 lux; local reference factors are historical
        // scene-linear calibrations, not candela or lumen conversions.
        constexpr float DirectSunlightIlluminance=100000.0f;
        constexpr float MaxArtistIntensity=100.0f;
        float ArtistIntensity(float intensity) {
            return std::isfinite(intensity) ? std::clamp(intensity, 0.0f, MaxArtistIntensity) : 0.0f;
        }
        float DirectionalIlluminance(float intensity) {
            return ArtistIntensity(intensity) * DirectSunlightIlluminance;
        }

        float PointIntensity(float intensity) {
            return ArtistIntensity(intensity) * 1000.0f;
        }

        float SpotIntensity(float intensity) {
            return ArtistIntensity(intensity) * 500.0f;
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
            auto safeDir = [](const glm::vec3& v, const glm::vec3& fallback) {
                const float lenSq = glm::dot(v, v);
                return (std::isfinite(lenSq) && lenSq > 1e-12f) ? (v * (1.0f / std::sqrt(lenSq))) : fallback;
            };
            const glm::vec3 dir = safeDir(light.Direction, {0.0f, 0.0f, -1.0f});
            const glm::vec3 upFallbackAxis = (std::abs(dir.y) < 0.99f) ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
            const glm::vec3 orthoUp = safeDir(light.Up - dir * glm::dot(light.Up, dir),
                safeDir(glm::cross(upFallbackAxis, dir), {0.0f, 1.0f, 0.0f}));
            packed.Position = light.Position;
            packed.Width = (std::isfinite(light.Width) && light.Width > 0.01f) ? light.Width : 0.01f;
            packed.Direction = dir;
            packed.Height = (std::isfinite(light.Height) && light.Height > 0.01f) ? light.Height : 0.01f;
            packed.Up = orthoUp;
            packed.Range = (std::isfinite(light.Range) && light.Range > 0.0f) ? light.Range : 0.0f;
            packed.Color = glm::vec3(
                (std::isfinite(light.Color.r) && light.Color.r > 0.0f) ? light.Color.r : 0.0f,
                (std::isfinite(light.Color.g) && light.Color.g > 0.0f) ? light.Color.g : 0.0f,
                (std::isfinite(light.Color.b) && light.Color.b > 0.0f) ? light.Color.b : 0.0f);
            const float safeIntensity = ArtistIntensity(std::isfinite(light.Intensity) ? std::abs(light.Intensity) : 0.0f);
            packed.SignedIntensity = light.TwoSided ? -safeIntensity : safeIntensity;
            packed.Shape = light.Shape;
            packed.ShadowPolicy = AreaLightShadowPolicy::UnshadowedAnalyticLTC;
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
