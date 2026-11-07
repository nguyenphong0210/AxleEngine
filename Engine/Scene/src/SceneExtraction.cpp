#include <Axle/SceneExtraction.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/MeshResourceResolver.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Axle {

    static RenderWorld ExtractInternal(const Scene& scene, const SceneExtractionOptions& options) {
        RenderWorld world;
        const auto& registry = scene.GetRegistry();

        // 1. Camera extraction
        if (options.CameraOverride) {
            world.Camera.Position = options.CameraOverride->GetPosition();
            world.Camera.ViewMatrix = options.CameraOverride->GetViewMatrix();
            world.Camera.ProjectionMatrix = options.CameraOverride->GetProjectionMatrix();
            world.Camera.ViewProjectionMatrix = options.CameraOverride->GetViewProjectionMatrix();
            world.Camera.NearClip = options.CameraOverride->GetNearClip();
            world.Camera.FarClip = options.CameraOverride->GetFarClip();
            world.Camera.FOV = options.CameraOverride->GetFOV();
            world.Camera.AspectRatio = options.CameraOverride->GetAspectRatio();
        } else {
            // Find primary camera entity in scene
            auto cameraView = registry.view<TransformComponent, CameraComponent>();
            for (auto entity : cameraView) {
                const auto& cam = cameraView.get<CameraComponent>(entity);
                const auto& tc = cameraView.get<TransformComponent>(entity);
                glm::mat4 worldMat = scene.GetWorldTransform(entity);
                glm::vec3 pos = glm::vec3(worldMat[3]);
                glm::vec3 forward = glm::normalize(glm::mat3(worldMat) * glm::vec3(0.0f, 0.0f, -1.0f));
                glm::vec3 up = glm::normalize(glm::mat3(worldMat) * glm::vec3(0.0f, 1.0f, 0.0f));

                world.Camera.Position = pos;
                world.Camera.ViewMatrix = glm::lookAt(pos, pos + forward, up);
                world.Camera.NearClip = cam.NearClip;
                world.Camera.FarClip = cam.FarClip;
                world.Camera.FOV = cam.FOV;
                world.Camera.AspectRatio = 16.0f / 9.0f;
                world.Camera.ProjectionMatrix = glm::perspective(glm::radians(cam.FOV), world.Camera.AspectRatio, cam.NearClip, cam.FarClip);
                world.Camera.ViewProjectionMatrix = world.Camera.ProjectionMatrix * world.Camera.ViewMatrix;
                if (cam.Primary) break; // Prefer primary camera
            }
        }

        // 2. Directional Light extraction
        auto dirLightView = registry.view<DirectionalLightComponent>();
        bool primaryDirFound = false;
        for (auto entity : dirLightView) {
            const auto& dl = dirLightView.get<DirectionalLightComponent>(entity);
            RenderDirectionalLight rdl;
            rdl.Color = dl.Color;
            rdl.Intensity = dl.Intensity;
            rdl.AmbientIntensity = dl.AmbientIntensity;
            rdl.CastsShadows = dl.CastShadows;

            if (registry.all_of<TransformComponent>(entity)) {
                glm::mat4 worldMat = scene.GetWorldTransform(entity);
                rdl.Direction = glm::normalize(glm::mat3(worldMat) * dl.GetDirection());
            } else {
                rdl.Direction = dl.GetDirection();
            }

            if (!primaryDirFound) {
                world.DirectionalLight = rdl;
                primaryDirFound = true;
            } else {
                world.AdditionalDirectionalLights.push_back(rdl);
            }
        }

        // 3. Point Lights extraction
        auto pointLightView = registry.view<TransformComponent, PointLightComponent>();
        for (auto entity : pointLightView) {
            const auto& pl = pointLightView.get<PointLightComponent>(entity);
            glm::mat4 worldMat = scene.GetWorldTransform(entity);
            RenderPointLight rpl;
            rpl.EntityID = static_cast<uint32_t>(entity);
            rpl.Position = glm::vec3(worldMat[3]);
            rpl.Color = pl.Color;
            rpl.Intensity = pl.Intensity;
            rpl.Radius = pl.Radius;
            rpl.Falloff = pl.Falloff;
            world.PointLights.push_back(rpl);
        }

        // 4. Spot Lights extraction
        auto spotLightView = registry.view<TransformComponent, SpotLightComponent>();
        for (auto entity : spotLightView) {
            const auto& sl = spotLightView.get<SpotLightComponent>(entity);
            if (!sl.Enabled) continue;
            glm::mat4 worldMat = scene.GetWorldTransform(entity);
            RenderSpotLight rsl;
            rsl.EntityID = static_cast<uint32_t>(entity);
            rsl.Position = glm::vec3(worldMat[3]);
            rsl.Direction = glm::normalize(glm::mat3(worldMat) * glm::vec3(0.0f, -1.0f, 0.0f));
            rsl.Color = sl.Color;
            rsl.Intensity = sl.Intensity;
            rsl.Range = sl.Range;
            rsl.InnerAngle = sl.InnerConeAngle;
            rsl.OuterAngle = sl.OuterConeAngle;
            world.SpotLights.push_back(rsl);
        }

        // 5. Skybox extraction
        auto skyboxView = registry.view<SkyboxComponent>();
        for (auto entity : skyboxView) {
            const auto& sc = skyboxView.get<SkyboxComponent>(entity);
            world.Skybox.Enabled = sc.Enabled;
            world.Skybox.Type = static_cast<int>(sc.Type);
            world.Skybox.HDRPath = sc.HDRPath;
            world.Skybox.Intensity = sc.Intensity;
            world.Skybox.Exposure = sc.Exposure;
            world.Skybox.ZenithColor = glm::vec4(sc.ZenithColor, 1.0f);
            world.Skybox.HorizonColor = glm::vec4(sc.HorizonColor, 1.0f);
            world.Skybox.GroundColor = glm::vec4(sc.GroundColor, 1.0f);
            if (world.DirectionalLight.has_value()) {
                world.Skybox.SunDirection = glm::vec4(world.DirectionalLight->Direction, 0.0f);
            }
            break;
        }

        // 6. Mesh instances extraction
        auto meshView = registry.view<TransformComponent, MeshInstance3D>();
        for (auto entity : meshView) {
            const auto& mc = meshView.get<MeshInstance3D>(entity);
            if (options.ResolveResources && (!mc.VAO || !mc.RHIMeshAsset)) {
                auto& mutableMc = const_cast<MeshInstance3D&>(mc);
                ResolveMeshResources(mutableMc);
            }

            RenderMeshInstance rmi;
            rmi.EntityID = static_cast<uint32_t>(entity);
            if (registry.all_of<IDComponent>(entity)) {
                rmi.StableUUID = registry.get<IDComponent>(entity).ID;
            }
            world.EntityToStableUUID[rmi.EntityID] = rmi.StableUUID;

            if (registry.all_of<TagComponent>(entity)) {
                rmi.Tag = registry.get<TagComponent>(entity).Tag;
            }

            rmi.MeshURI = mc.AssetPath;
            rmi.BuiltinId = ParseBuiltinMesh(mc.AssetPath);
            rmi.GLMesh = mc.VAO;
            rmi.NativeMesh = mc.RHIMeshAsset;
            rmi.WorldTransform = scene.GetWorldTransform(entity);
            rmi.Selected = options.SelectedEntities && (options.SelectedEntities->find(rmi.EntityID) != options.SelectedEntities->end());

            // Material properties
            if (registry.all_of<MaterialComponent>(entity)) {
                const auto& matComp = registry.get<MaterialComponent>(entity);
                if (options.ResolveResources && !matComp.RHIMaterialAsset) {
                    auto& mutableMat = const_cast<MaterialComponent&>(matComp);
                    if (mutableMat.HasAsset() && mutableMat.MaterialAsset) mutableMat.SyncFromAsset();
                    else mutableMat.SyncToRHI();
                }
                rmi.Material.MaterialUUID = matComp.MaterialHandle;
                rmi.Material.MaterialPath = matComp.AssetPath;
                rmi.Material.MaterialAsset = matComp.MaterialAsset;
                rmi.Material.RHIMaterialAsset = matComp.RHIMaterialAsset;
                rmi.Material.UseCustomShader = matComp.UseCustomShader;
                rmi.Material.CustomShaderPath = matComp.CustomShaderPath;
                rmi.Material.AlbedoColor = matComp.AlbedoColor;
                rmi.Material.SecondaryColor = matComp.AlbedoColor * 0.85f;
                rmi.Material.Roughness = matComp.Roughness;
                rmi.Material.Metallic = matComp.Metallic;
                rmi.Material.Emission = matComp.EmissionIntensity;
                rmi.Material.TileScale = glm::vec2(mc.TileScale);
                rmi.Material.PatternMode = mc.PatternMode;
            } else {
                rmi.Material.RHIMaterialAsset = mc.RHIMaterialAsset;
                rmi.Material.AlbedoColor = mc.Color1;
                rmi.Material.SecondaryColor = mc.Color2;
                rmi.Material.TileScale = glm::vec2(mc.TileScale);
                rmi.Material.PatternMode = mc.PatternMode;
            }

            world.Meshes.push_back(std::move(rmi));
        }

        world.SceneRevision = scene.GetTransformRevision();
        world.TotalEntities = registry.storage<entt::entity>() ? static_cast<uint32_t>(registry.storage<entt::entity>()->size()) : 0;

        return world;
    }

    RenderWorld ExtractRenderWorld(Scene& scene, const SceneExtractionOptions& options) {
        scene.UpdateWorldTransforms();
        return ExtractInternal(scene, options);
    }

    RenderWorld ExtractRenderWorld(const Scene& scene, const SceneExtractionOptions& options) {
        return ExtractInternal(scene, options);
    }

}
