#pragma once
#include <Axle/RenderWorld.hpp>
#include <algorithm>
#include <cmath>
namespace Axle {
    // Ordinary views have no implicit lighting. Private previews author their
    // own lights in their private world rather than changing this policy.
    struct SceneLighting {
        glm::vec3 SunDirection{0.45f,1.0f,0.65f}, SunColor{1.0f};
        float SunIntensity=0, AmbientIntensity=0;
        glm::vec3 AmbientColor{0.22f,0.25f,0.32f};
        bool Environment=false;
    };
    inline SceneLighting ResolveSceneLighting(const RenderWorld& world) {
        SceneLighting result;
        if(world.DirectionalLight) {
            result.SunDirection=world.DirectionalLight->Direction;
            result.SunColor=world.DirectionalLight->Color;
            result.SunIntensity=std::max(0.0f,world.DirectionalLight->Intensity);
            result.AmbientIntensity=std::max(0.0f,world.DirectionalLight->AmbientIntensity);
        }
        result.Environment=world.Skybox.Authored&&world.Skybox.Enabled;
        if(result.Environment) {
            result.AmbientIntensity=std::isfinite(world.Skybox.Intensity)?std::max(0.0f,world.Skybox.Intensity):0;
            if(world.Skybox.Type==1) {
                const double exposure=std::isfinite(world.Skybox.Exposure)?std::max(0.0f,world.Skybox.Exposure):0;
                const double intensity=result.AmbientIntensity*exposure;
                result.AmbientIntensity=intensity<=1e12?static_cast<float>(intensity):0;
            }
            result.AmbientColor=(glm::vec3(world.Skybox.ZenithColor)+glm::vec3(world.Skybox.HorizonColor)+glm::vec3(world.Skybox.GroundColor))/3.0f;
        }
        // Match the native lux-to-radiance convention for artist and physical inputs.
        result.SunIntensity=(result.SunIntensity>100.0f?result.SunIntensity:result.SunIntensity*100000.0f)*0.000035f;
        return result;
    }
}
