#include <Axle/SceneExtraction.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/RenderParticleData.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/MeshResourceResolver.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/ImageSource.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Axle {

    static void ResolveParticleMaterial(RenderParticleData& batch) {
        batch.Config.Collision.WorldRaycast=nullptr;
        batch.Material=ParticleMaterialAdapter::Resolve(batch.Config,batch.Resources,[](const auto& path) {
            // Preserve unsaved inspector edits without asking the loading cache
            // to allocate GPU resources. Only effective values escape this callback.
            if(auto authored=MaterialCache::FindLoadedMaterial(path))return std::make_shared<Material>(*authored);
            auto material=std::make_shared<Material>();
            return material->Load(FileSystem::ResolveAssetPath(path),false)?material:nullptr;
        });
        if(batch.Resources.MaterialPath.empty()) {
            if(batch.Texture&&batch.Resources.UseTexture)batch.Material.UseTexture=true;
            if(batch.Normal&&batch.Resources.UseNormalMap)batch.Material.UseNormalMap=true;
        }
        batch.MaterialResolved=true;
    }

    bool ResolveRenderWorldResources(RenderWorld& world) {
        if (!world.ErrorMessage.empty()) return false;
        for (auto& instance : world.Meshes) {
            if(RHI::RHIDevice::Get().IsNativeReady()&&instance.NativeView) {
                if(!instance.NativeView->IsValid()){world.ErrorMessage="Stale captured mesh generation";return false;}
                continue;
            }
            MeshInstance3D candidate;
            candidate.AssetPath = instance.MeshURI; candidate.VAO = instance.GLMesh; candidate.RHIMeshAsset = instance.NativeMesh;
            candidate.Source = instance.Source;
            if (!ResolveMeshResources(candidate)) { world.ErrorMessage = "Cannot resolve mesh: " + instance.MeshURI; return false; }
            instance.GLMesh = candidate.VAO; instance.NativeMesh = candidate.RHIMeshAsset;
            if(instance.NativeMesh)instance.NativeView=instance.NativeMesh->CaptureView();
        }
        for(auto& immutable:world.Particles) {
            auto batch=std::make_shared<RenderParticleData>(*immutable);
            if(!batch->MaterialResolved)ResolveParticleMaterial(*batch);
            const auto& material=batch->Material;
            batch->Config.BlendMode=material.BlendMode;batch->Config.LightingEnabled=material.LightingEnabled;
            batch->Config.WrapLighting=material.WrapLighting;batch->Config.ScatteringFactor=material.ScatteringFactor;
            batch->Config.NormalMapStrength=material.NormalMapStrength;
            batch->Config.Collision.WorldRaycast=nullptr;
            if(!RHI::RHIDevice::Get().IsNativeReady()) {
                auto load=[](const std::string& path,bool srgb) {
                    TextureSpecification specification;specification.FlipVertically=false;specification.sRGB=srgb;
                    ImageDecodeOptions options;options.SRGB=srgb;
                    return ImageTextureCache::GetGL(LoadImageSource(path,options),specification);
                };
                if(!material.UseTexture)batch->Texture.reset();
                else if(!material.TexturePath.empty())batch->Texture=load(material.TexturePath,true);
                if(!material.UseNormalMap)batch->Normal.reset();
                else if(!material.NormalMapPath.empty())batch->Normal=load(material.NormalMapPath,false);
            }
            immutable=std::move(batch);
        }
        return true;
    }

    static RenderWorld ExtractInternal(const Scene& scene, const SceneExtractionOptions& options) {
        RenderWorld world;
        if (!scene.CanMutate()) { world.ErrorMessage = "Extraction requires the scene owner boundary"; return world; }
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
            rdl.ShadowQuality = dl.ShadowQuality; rdl.ShadowBias = dl.ShadowBias;
            rdl.ShadowSlopeBias = dl.ShadowSlopeBias; rdl.ShadowNormalBias = dl.ShadowNormalBias;
            rdl.ShadowFilterRadius = dl.ShadowFilterRadius; rdl.MaxShadowDistance = dl.MaxShadowDistance;
            rdl.CascadeSplitLambda = dl.CascadeSplitLambda; rdl.Exposure = dl.Exposure;
            rdl.Gamma = dl.Gamma; rdl.ToneMappingMode = dl.ToneMappingMode;

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
            rsl.Direction = glm::normalize(glm::mat3(worldMat) * glm::vec3(0.0f, 0.0f, -1.0f));
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
            world.Skybox.Authored = true;
            world.Skybox.SyncWithSun = sc.SyncWithSun;
            world.Skybox.CustomAzimuth = sc.CustomAzimuth;
            world.Skybox.CustomElevation = sc.CustomElevation;
            world.Skybox.Enabled = sc.Enabled;
            world.Skybox.HemisphereAmbient = sc.HemisphereAmbient;
            world.Skybox.Type = static_cast<int>(sc.Type);
            world.Skybox.HDRPath = sc.HDRPath;
            world.Skybox.Intensity = sc.Intensity;
            world.Skybox.Exposure = sc.Exposure;
            world.Skybox.SunSize = sc.SunSize; world.Skybox.SunGlow = sc.SunGlow; world.Skybox.Turbidity = sc.Turbidity;
            world.Skybox.ZenithColor = glm::vec4(sc.ZenithColor, 1.0f);
            world.Skybox.HorizonColor = glm::vec4(sc.HorizonColor, 1.0f);
            world.Skybox.GroundColor = glm::vec4(sc.GroundColor, 1.0f);
            if (world.DirectionalLight.has_value()) {
                world.Skybox.SunDirection = glm::vec4(world.DirectionalLight->Direction, 0.0f);
                world.Skybox.SunColor=glm::vec4(world.DirectionalLight->Color*world.DirectionalLight->Intensity,1);
            } else world.Skybox.SunColor=glm::vec4(0);
            if(!sc.SyncWithSun) {
                const float azimuth=glm::radians(sc.CustomAzimuth), elevation=glm::radians(sc.CustomElevation);
                world.Skybox.SunDirection=glm::vec4(std::cos(elevation)*std::sin(azimuth),std::sin(elevation),std::cos(elevation)*std::cos(azimuth),0);
            }
            break;
        }

        // 6. Mesh instances extraction
        auto meshView = registry.view<TransformComponent, MeshInstance3D>();
        for (auto entity : meshView) {
            const auto& mc = meshView.get<MeshInstance3D>(entity);
            // An empty component has no geometry assignment. It must not abort
            // otherwise valid meshes; nonempty missing references still fail.
            if(mc.AssetPath.empty()&&!mc.VAO&&!mc.RHIMeshAsset&&!mc.Source)continue;
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
            if(rmi.NativeMesh)rmi.NativeView=rmi.NativeMesh->CaptureView();
            rmi.Source = mc.Source;
            rmi.WorldTransform = scene.GetWorldTransform(entity);
            rmi.Selected = options.SelectedEntities && (options.SelectedEntities->find(rmi.EntityID) != options.SelectedEntities->end());

            // Material properties
            if (registry.all_of<MaterialComponent>(entity)) {
                // Copy authoring values and caches. Synchronization operates only on
                // this local candidate; RHIMaterial construction itself is CPU-only.
                auto matComp = registry.get<MaterialComponent>(entity);
                matComp.RHIMaterialAsset.reset();
                if (matComp.HasAsset() && matComp.MaterialAsset) matComp.SyncFromAsset();
                else matComp.SyncToRHI();
                matComp.MaterialAsset = std::make_shared<Material>("Render snapshot", matComp.MaterialHandle);
                matComp.SyncToAsset();
                rmi.Material.MaterialUUID = matComp.MaterialHandle;
                rmi.Material.MaterialPath = matComp.AssetPath;
                rmi.Material.MaterialAsset = matComp.MaterialAsset;
                rmi.Material.RHIMaterialAsset = matComp.RHIMaterialAsset;
                rmi.Material.CustomProperties = matComp.CustomProperties;
                rmi.Material.UVScale = matComp.UVScale; rmi.Material.UVOffset = matComp.UVOffset;
                rmi.Material.AO = matComp.AOStrength; rmi.Material.AlbedoPath = matComp.AlbedoPath;
                rmi.Material.UseAlbedoMap = matComp.UseAlbedoMap;
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
                if (mc.RHIMaterialAsset) rmi.Material.RHIMaterialAsset = std::make_shared<RHI::RHIMaterial>(*mc.RHIMaterialAsset);
                rmi.Material.AlbedoColor = mc.Color1;
                rmi.Material.SecondaryColor = mc.Color2;
                rmi.Material.TileScale = glm::vec2(mc.TileScale);
                rmi.Material.PatternMode = mc.PatternMode;
            }

            world.Meshes.push_back(std::move(rmi));
        }

        // Preserve supported GL skinning as immutable pose/geometry references.
        for (auto entity : registry.view<TransformComponent, SkeletalMeshComponent>()) {
            const auto& mesh = registry.get<SkeletalMeshComponent>(entity);
            if (!mesh.Visible || !mesh.Mesh) continue;
            RenderSkinInstance skin;
            skin.GLMesh = mesh.Mesh->GetVAO(); skin.WorldTransform = scene.GetWorldTransform(entity);
            skin.Color = mesh.Color1; skin.CastsShadows = mesh.CastShadows;
            if (auto* animator = registry.try_get<AnimatorComponent>(entity); animator && animator->AnimatorInstance)
                skin.BoneMatrices = animator->AnimatorInstance->GetSkinningMatrices();
            world.Skins.push_back(std::move(skin));
        }
        auto appendSystem = [&](const std::shared_ptr<ParticleSystem>& system) {
            if (!system || !system->GetActiveParticleCount()) return;
            auto batch = std::make_shared<RenderParticleData>();
            batch->Pool = system->GetPool(); batch->Config = system->GetConfig();batch->Config.Collision.WorldRaycast=nullptr;
            world.Particles.push_back(std::move(batch));
        };
        for (auto entity : registry.view<TransformComponent, ParticleEmitterComponent>()) {
            const auto& emitter = registry.get<ParticleEmitterComponent>(entity);
            if (!emitter.Visible) continue;
            const auto owner = scene.GetWorldTransform(entity);
            glm::vec3 scale(glm::length(glm::vec3(owner[0])), glm::length(glm::vec3(owner[1])), glm::length(glm::vec3(owner[2])));
            auto axes = glm::mat3(owner);
            for (int a = 0; a < 3; ++a) axes[a] /= std::max(scale[a], 0.0001f);
            for (const auto& stack : emitter.EmitterStacks) {
                if (!stack.Visible || !stack.Enabled || !stack.System || !stack.System->GetActiveParticleCount()) continue;
                auto batch = std::make_shared<RenderParticleData>();
                batch->Pool = stack.System->GetPool(); batch->Config = stack.Config;batch->Config.Collision.WorldRaycast=nullptr;
                batch->Resources = stack.GetDefinition().Resources;
                if (batch->Resources.MaterialPath.empty()) batch->Resources.MaterialPath = stack.Config.MaterialPath;
                batch->Texture = stack.Texture ? stack.Texture : emitter.Texture;
                batch->Normal = stack.NormalMap ? stack.NormalMap : emitter.NormalMap;
                const auto transform = stack.GetWorldTransform(glm::vec3(owner[3]), glm::quat_cast(axes), scale);
                batch->Position = transform.Translation; batch->Rotation = transform.Rotation; batch->Scale = transform.Scale;
                world.Particles.push_back(std::move(batch));
            }
        }
        for (auto entity : registry.view<CharacterVFXComponent>()) {
            const auto& vfx = registry.get<CharacterVFXComponent>(entity);
            if (!vfx.Enabled || !vfx.RuntimeVFX) continue;
            appendSystem(vfx.RuntimeVFX->GetFootstepSystem());
            appendSystem(vfx.RuntimeVFX->GetLandingShockwaveSystem());
        }

        // CPU asset values are frozen after all registry walks; no texture/GPU
        // creation occurs here. GPU publication remains in the shared resolver.
        for(auto& immutable:world.Particles) {
            auto batch=std::make_shared<RenderParticleData>(*immutable);ResolveParticleMaterial(*batch);immutable=std::move(batch);
        }

        world.SceneRevision = scene.GetTransformRevision();
        for (const auto& line : scene.ExtractExtensionDebug()) {
            world.DebugLines.push_back({line.CodeLease, {line.From[0], line.From[1], line.From[2]},
                {line.To[0], line.To[1], line.To[2]}, {line.Color[0], line.Color[1], line.Color[2], line.Color[3]}});
        }
        world.TotalEntities = registry.storage<entt::entity>() ? static_cast<uint32_t>(registry.storage<entt::entity>()->size()) : 0;

        return world;
    }

    RenderWorld ExtractRenderWorld(Scene& scene, const SceneExtractionOptions& options) {
        if (!scene.CanMutate()) { RenderWorld rejected; rejected.ErrorMessage = "Extraction requires the scene owner boundary"; return rejected; }
        scene.UpdateWorldTransforms();
        return ExtractInternal(scene, options);
    }

    RenderWorld ExtractRenderWorld(const Scene& scene, const SceneExtractionOptions& options) {
        return ExtractInternal(scene, options);
    }

}
