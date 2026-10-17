#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/ParallelECS.hpp>
#include <Axle/Vehicle.hpp>
#include <algorithm>

namespace Axle {

    Scene::Scene() {
    }

    Entity Scene::CreateEntity(const std::string& name) {
        return CreateEntityWithUUID(UUID(), name);
    }

    Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name) {
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<IDComponent>(uuid);
        entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<HierarchyComponent>();
        return entity;
    }

    Entity Scene::InstantiateAssembly(const std::filesystem::path& filepath,
                                     PhysicsScene* physicsScene,
                                     const glm::vec3& spawnPosition,
                                     const SceneSerializerContext* context) {
        return AssemblySerializer::Deserialize(filepath, this, physicsScene, spawnPosition, context);
    }

    void Scene::Clear() {
        auto scriptView = m_Registry.view<NativeScriptComponent>();
        for (auto entityID : scriptView) {
            auto& nsc = scriptView.get<NativeScriptComponent>(entityID);
            if (nsc.Instance && nsc.DestroyScript) {
                nsc.Instance->OnDestroy();
                nsc.DestroyScript(&nsc);
            }
        }
        m_Registry.clear();
    }

    void Scene::DestroyEntity(Entity entity, PhysicsScene* physicsScene) {
        if (!entity || !m_Registry.valid((entt::entity)entity)) {
            return;
        }

        if (entity.HasComponent<NativeScriptComponent>()) {
            auto& nsc = entity.GetComponent<NativeScriptComponent>();
            if (nsc.Instance && nsc.DestroyScript) {
                nsc.Instance->OnDestroy();
                nsc.DestroyScript(&nsc);
            }
        }

        if (physicsScene && entity.HasComponent<RigidBody3D>()) {
            auto& rb = entity.GetComponent<RigidBody3D>();
            if (!rb.RuntimeBodyID.IsInvalid()) {
                physicsScene->DestroyBody(rb.RuntimeBodyID);
                rb.RuntimeBodyID = {};
            }
        }

        if (entity.HasComponent<HierarchyComponent>()) {
            auto& hc = entity.GetComponent<HierarchyComponent>();
            auto children = hc.Children; // Copy list to avoid iterator invalidation
            for (auto child : children) {
                if (m_Registry.valid(child)) {
                    DestroyEntity({ child, this }, physicsScene);
                }
            }
            if (hc.Parent != entt::null && m_Registry.valid(hc.Parent)) {
                if (m_Registry.all_of<HierarchyComponent>(hc.Parent)) {
                    auto& parentHc = m_Registry.get<HierarchyComponent>(hc.Parent);
                    auto it = std::remove(parentHc.Children.begin(), parentHc.Children.end(), static_cast<entt::entity>(entity));
                    parentHc.Children.erase(it, parentHc.Children.end());
                }
            }
        }
        m_Registry.destroy(entity);
    }

    Entity Scene::DuplicateEntity(Entity entity, PhysicsScene* physicsScene) {
        if (!entity || !m_Registry.valid((entt::entity)entity)) {
            return {};
        }

        std::string name = entity.GetName();
        Entity newEntity = CreateEntity(name + " (Copy)");

        // Copy TransformComponent
        if (entity.HasComponent<TransformComponent>()) {
            newEntity.GetComponent<TransformComponent>() = entity.GetComponent<TransformComponent>();
        }

        // Copy MeshInstance3D
        if (entity.HasComponent<MeshInstance3D>()) {
            newEntity.AddComponent<MeshInstance3D>(entity.GetComponent<MeshInstance3D>());
        }

        // Copy Lights & Environment
        if (entity.HasComponent<DirectionalLightComponent>()) {
            newEntity.AddComponent<DirectionalLightComponent>(entity.GetComponent<DirectionalLightComponent>());
        }
        if (entity.HasComponent<PointLightComponent>()) {
            newEntity.AddComponent<PointLightComponent>(entity.GetComponent<PointLightComponent>());
        }
        if (entity.HasComponent<CameraComponent>()) {
            newEntity.AddComponent<CameraComponent>(entity.GetComponent<CameraComponent>());
        }
        if (entity.HasComponent<SkyboxComponent>()) {
            newEntity.AddComponent<SkyboxComponent>(entity.GetComponent<SkyboxComponent>());
        }

        // Copy NativeScriptComponent
        if (entity.HasComponent<NativeScriptComponent>()) {
            const auto& srcNsc = entity.GetComponent<NativeScriptComponent>();
            auto& dstNsc = newEntity.AddComponent<NativeScriptComponent>(srcNsc.ScriptName);
            dstNsc.FloatFields = srcNsc.FloatFields;
            dstNsc.IntFields = srcNsc.IntFields;
            dstNsc.BoolFields = srcNsc.BoolFields;
            dstNsc.Vec3Fields = srcNsc.Vec3Fields;
        }

        // Copy AssemblyComponent
        if (entity.HasComponent<AssemblyComponent>()) {
            newEntity.AddComponent<AssemblyComponent>(entity.GetComponent<AssemblyComponent>());
        }

        // Copy VehicleComponent
        if (entity.HasComponent<VehicleComponent>()) {
            newEntity.AddComponent<VehicleComponent>(entity.GetComponent<VehicleComponent>());
        }

        // Copy WheelComponent
        if (entity.HasComponent<WheelComponent>()) {
            newEntity.AddComponent<WheelComponent>(entity.GetComponent<WheelComponent>());
        }

        // Copy CollisionShape3D and create new body in PhysicsScene
        if (entity.HasComponent<CollisionShape3D>()) {
            newEntity.AddComponent<CollisionShape3D>(entity.GetComponent<CollisionShape3D>());
        }
        if (physicsScene && entity.HasComponent<RigidBody3D>()) {
            const auto& srcRb = entity.GetComponent<RigidBody3D>();
            if (entity.HasComponent<CollisionShape3D>()) {
                const auto& shape = entity.GetComponent<CollisionShape3D>();
                const auto& tc = newEntity.GetComponent<TransformComponent>();
                BodyID newBodyID{};
                switch (shape.ShapeType) {
                    case CollisionShapeType::Box: {
                        BoxBodyProps bProps;
                        bProps.Position = tc.Translation;
                        bProps.Rotation = tc.Rotation;
                        bProps.HalfExtent = shape.HalfExtent;
                        bProps.IsDynamic = srcRb.IsDynamic;
                        bProps.Friction = shape.Friction;
                        bProps.Restitution = shape.Restitution;
                        newBodyID = physicsScene->CreateBox(bProps);
                        break;
                    }
                    case CollisionShapeType::Sphere: {
                        SphereBodyProps sProps;
                        sProps.Position = tc.Translation;
                        sProps.Radius = shape.Radius;
                        sProps.IsDynamic = srcRb.IsDynamic;
                        sProps.Friction = shape.Friction;
                        sProps.Restitution = shape.Restitution;
                        newBodyID = physicsScene->CreateSphere(sProps);
                        break;
                    }
                    case CollisionShapeType::Cylinder: {
                        CylinderBodyProps cProps;
                        cProps.Position = tc.Translation;
                        cProps.HalfHeight = shape.HalfHeight;
                        cProps.Radius = shape.Radius;
                        cProps.IsDynamic = srcRb.IsDynamic;
                        cProps.Friction = shape.Friction;
                        cProps.Restitution = shape.Restitution;
                        newBodyID = physicsScene->CreateCylinder(cProps);
                        break;
                    }
                    default:
                        break;
                }
                if (!newBodyID.IsInvalid()) {
                    newEntity.AddComponent<RigidBody3D>(newBodyID, srcRb.IsDynamic);
                }
            }
        }

        // Parent relationship: preserve parent if source entity has a parent
        Entity parent = entity.GetParent();
        if (parent) {
            newEntity.SetParent(parent, false);
        }

        // Deep Recursive Duplication of Children
        if (entity.HasComponent<HierarchyComponent>()) {
            auto children = entity.GetComponent<HierarchyComponent>().Children;
            for (auto childID : children) {
                if (m_Registry.valid(childID)) {
                    Entity childEntity{ childID, this };
                    Entity duplicatedChild = DuplicateEntity(childEntity, physicsScene);
                    if (duplicatedChild) {
                        duplicatedChild.SetParent(newEntity, false);
                    }
                }
            }
        }

        return newEntity;
    }

    Entity Scene::FindEntityByBodyID(BodyID bodyID) {
        if (bodyID.IsInvalid()) return {};

        auto view = m_Registry.view<RigidBody3D>();
        for (auto entityID : view) {
            const auto& rb = view.get<RigidBody3D>(entityID);
            if (rb.RuntimeBodyID == bodyID) {
                return { entityID, this };
            }
        }
        return {};
    }

    Entity Scene::GetEntityByUUID(UUID uuid) {
        if ((uint64_t)uuid == 0) return {};

        auto view = m_Registry.view<IDComponent>();
        for (auto entityID : view) {
            if (view.get<IDComponent>(entityID).ID == uuid) {
                return { entityID, this };
            }
        }
        return {};
    }

    glm::mat4 Scene::GetWorldTransform(entt::entity entityID) const {
        if (!m_Registry.valid(entityID) || !m_Registry.all_of<TransformComponent>(entityID)) {
            return glm::mat4(1.0f);
        }

        auto& tc = const_cast<entt::registry&>(m_Registry).get<TransformComponent>(entityID);
        tc.LocalMatrix = tc.GetLocalTransform();
        glm::mat4 transform = tc.LocalMatrix;
        if (m_Registry.all_of<HierarchyComponent>(entityID)) {
            const auto& hc = m_Registry.get<HierarchyComponent>(entityID);
            if (hc.InheritTransform && hc.Parent != entt::null && m_Registry.valid(hc.Parent) && m_Registry.all_of<TransformComponent>(hc.Parent)) {
                glm::mat4 parentWorld = GetWorldTransform(hc.Parent);
                if (!hc.InheritScale) {
                    glm::vec3 pPos = glm::vec3(parentWorld[3]);
                    glm::vec3 col0 = glm::vec3(parentWorld[0]);
                    glm::vec3 col1 = glm::vec3(parentWorld[1]);
                    glm::vec3 col2 = glm::vec3(parentWorld[2]);
                    float l0 = glm::length(col0);
                    float l1 = glm::length(col1);
                    float l2 = glm::length(col2);
                    glm::mat3 rot(
                        l0 > 1e-5f ? col0 / l0 : glm::vec3(1.0f, 0.0f, 0.0f),
                        l1 > 1e-5f ? col1 / l1 : glm::vec3(0.0f, 1.0f, 0.0f),
                        l2 > 1e-5f ? col2 / l2 : glm::vec3(0.0f, 0.0f, 1.0f)
                    );
                    glm::mat4 rigidParent = glm::mat4(rot);
                    rigidParent[3] = glm::vec4(pPos, 1.0f);
                    transform = rigidParent * transform;
                } else {
                    transform = parentWorld * transform;
                }
            }
        }
        tc.WorldMatrix = transform;
        return transform;
    }

    void Scene::UpdateWorldTransforms() {
        UpdateWorldTransformsParallel(64);
    }

    void Scene::UpdateWorldTransformsParallel(u32 minBatchSize) {
        ParallelECS::UpdateWorldTransforms(m_Registry, minBatchSize);
    }

    void Scene::UpdateEntityTransformRecursive(entt::entity entityID, const glm::mat4& parentWorld) {
        if (!m_Registry.valid(entityID) || !m_Registry.all_of<TransformComponent>(entityID)) {
            return;
        }

        auto& tc = m_Registry.get<TransformComponent>(entityID);
        tc.LocalMatrix = tc.GetLocalTransform();

        glm::mat4 effectiveParent = parentWorld;
        if (m_Registry.all_of<HierarchyComponent>(entityID)) {
            const auto& hc = m_Registry.get<HierarchyComponent>(entityID);
            if (!hc.InheritTransform) {
                effectiveParent = glm::mat4(1.0f);
            } else if (!hc.InheritScale) {
                glm::vec3 pPos = glm::vec3(parentWorld[3]);
                glm::vec3 col0 = glm::vec3(parentWorld[0]);
                glm::vec3 col1 = glm::vec3(parentWorld[1]);
                glm::vec3 col2 = glm::vec3(parentWorld[2]);
                float l0 = glm::length(col0);
                float l1 = glm::length(col1);
                float l2 = glm::length(col2);
                glm::mat3 rot(
                    l0 > 1e-5f ? col0 / l0 : glm::vec3(1.0f, 0.0f, 0.0f),
                    l1 > 1e-5f ? col1 / l1 : glm::vec3(0.0f, 1.0f, 0.0f),
                    l2 > 1e-5f ? col2 / l2 : glm::vec3(0.0f, 0.0f, 1.0f)
                );
                glm::mat4 rigidParent = glm::mat4(rot);
                rigidParent[3] = glm::vec4(pPos, 1.0f);
                effectiveParent = rigidParent;
            }
        }
        tc.WorldMatrix = effectiveParent * tc.LocalMatrix;

        if (m_Registry.all_of<HierarchyComponent>(entityID)) {
            const auto& hc = m_Registry.get<HierarchyComponent>(entityID);
            for (auto childHandle : hc.Children) {
                if (m_Registry.valid(childHandle)) {
                    UpdateEntityTransformRecursive(childHandle, tc.WorldMatrix);
                }
            }
        }
    }

    void Scene::SetParent(Entity child, Entity parent, bool keepWorldPosition) {
        if (child) {
            child.SetParent(parent, keepWorldPosition);
        }
    }


    bool Scene::GetPrimaryDirectionalLight(DirectionalLightComponent& outLight) const {
        auto view = m_Registry.view<DirectionalLightComponent>();
        for (auto entity : view) {
            outLight = view.get<DirectionalLightComponent>(entity);
            return true;
        }
        return false;
    }

    bool Scene::GetPrimarySkybox(SkyboxComponent& outSkybox) const {
        auto view = m_Registry.view<SkyboxComponent>();
        for (auto entity : view) {
            outSkybox = view.get<SkyboxComponent>(entity);
            return true;
        }
        return false;
    }

    Entity Scene::GetPrimaryCameraEntity() {
        auto view = m_Registry.view<CameraComponent>();
        for (auto entityID : view) {
            const auto& cam = view.get<CameraComponent>(entityID);
            if (cam.Primary) {
                return Entity{ entityID, this };
            }
        }
        // Fallback: Return the first camera entity found if none explicitly marked Primary
        for (auto entityID : view) {
            return Entity{ entityID, this };
        }
        return Entity{};
    }

    void Scene::OnUpdate(Timestep ts, PhysicsScene* physicsScene, const PerspectiveCamera& camera, Shader& shader,
                         const glm::mat4& lightSpaceMatrix, const glm::vec3& lightDir, int shadowMapSlot) {
        (void)ts;

        // 1. Synchronize physics simulation transforms into TransformComponents
        if (physicsScene) {
            auto physicsView = m_Registry.view<RigidBody3D, TransformComponent>();
            for (auto entity : physicsView) {
                if (m_Registry.all_of<VehicleComponent>(entity)) {
                    continue; // Skip vehicle chassis; transform is managed by Vehicle physics controller with COM compensation
                }
                const auto& rb = m_Registry.get<RigidBody3D>(entity);
                if (rb.MotionType == BodyMotionType::Static) {
                    continue; // Static bodies never move; prevents visual mesh drift when collider has offset
                }

                auto& tc = m_Registry.get<TransformComponent>(entity);
                if (!rb.RuntimeBodyID.IsInvalid()) {
                    glm::vec3 worldPos = physicsScene->GetBodyPosition(rb.RuntimeBodyID);
                    glm::quat worldRot = physicsScene->GetBodyRotation(rb.RuntimeBodyID);

                    // Compensate for collider offset so TransformComponent represents the entity origin
                    if (m_Registry.all_of<CollisionShape3D>(entity)) {
                        const auto& cs = m_Registry.get<CollisionShape3D>(entity);
                        worldPos -= worldRot * cs.Offset;
                    }

                    // If entity is in a hierarchy with an active parent, convert world space to local space
                    if (m_Registry.all_of<HierarchyComponent>(entity)) {
                        const auto& hc = m_Registry.get<HierarchyComponent>(entity);
                        if (hc.InheritTransform && hc.Parent != entt::null && m_Registry.valid(hc.Parent) && m_Registry.all_of<TransformComponent>(hc.Parent)) {
                            glm::mat4 parentWorld = GetWorldTransform(hc.Parent);
                            glm::mat4 invParent = glm::inverse(parentWorld);
                            glm::mat4 childWorldMat = glm::translate(glm::mat4(1.0f), worldPos) * glm::mat4_cast(worldRot);
                            glm::mat4 childLocalMat = invParent * childWorldMat;
                            tc.Translation = glm::vec3(childLocalMat[3]);

                            glm::vec3 scale(
                                glm::length(glm::vec3(childLocalMat[0])),
                                glm::length(glm::vec3(childLocalMat[1])),
                                glm::length(glm::vec3(childLocalMat[2]))
                            );
                            glm::mat3 rotMatrix(1.0f);
                            if (scale.x > 1e-6f) rotMatrix[0] = glm::vec3(childLocalMat[0]) / scale.x;
                            if (scale.y > 1e-6f) rotMatrix[1] = glm::vec3(childLocalMat[1]) / scale.y;
                            if (scale.z > 1e-6f) rotMatrix[2] = glm::vec3(childLocalMat[2]) / scale.z;
                            tc.Rotation = glm::quat_cast(rotMatrix);
                            continue;
                        }
                    }

                    tc.Translation = worldPos;
                    tc.Rotation = worldRot;
                }
            }
        }


        // 2. Propagate scene graph hierarchy transforms (Root -> Children)
        UpdateWorldTransformsParallel(64);

        // 3. Query active directional sun light in scene

        DirectionalLightComponent sunLight;
        bool hasSunLight = GetPrimaryDirectionalLight(sunLight);

        glm::vec3 activeLightDir = hasSunLight ? sunLight.GetDirection() : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 activeLightColor = hasSunLight ? sunLight.Color : glm::vec3(0.0f);
        float activeIntensity = hasSunLight ? sunLight.Intensity : 0.0f;
        float activeAmbient = hasSunLight ? sunLight.AmbientIntensity : 0.0f;
        int activeShadows = (hasSunLight && sunLight.CastShadows) ? 1 : 0;
        float activeExposure = hasSunLight ? sunLight.Exposure : 1.0f;
        int activeToneMapping = hasSunLight ? sunLight.ToneMappingMode : 0;
        float activeGamma = hasSunLight ? sunLight.Gamma : 2.2f;

        // 3. Render all entities with MeshInstance3D and TransformComponent (with hierarchy propagation)
        shader.Bind();
        shader.SetMat4("u_ViewProjection", camera.GetViewProjectionMatrix());
        shader.SetMat4("u_LightSpaceMatrix", lightSpaceMatrix);
        shader.SetFloat3("u_LightDir", activeLightDir);
        shader.SetFloat3("u_LightColor", activeLightColor);
        shader.SetFloat("u_LightIntensity", activeIntensity);
        shader.SetFloat("u_AmbientIntensity", activeAmbient);
        shader.SetInt("u_ShadowsEnabled", activeShadows);
        shader.SetInt("u_ShadowMap", shadowMapSlot);

        // Tone Mapping & Photometric Controls (Phase 11: Sub-step 3.2)
        shader.SetFloat("u_Exposure", activeExposure);
        shader.SetInt("u_ToneMappingMode", activeToneMapping);
        shader.SetFloat("u_Gamma", activeGamma);

        // 3. Query active atmospheric skybox for hemisphere ambient irradiance (Phase 11: Sub-step 4.3)
        SkyboxComponent skybox;
        bool hasSkybox = GetPrimarySkybox(skybox);
        int useHemisphereAmbient = (hasSkybox && skybox.Enabled && skybox.HemisphereAmbient) ? 1 : 0;
        glm::vec3 skyZenith = hasSkybox ? skybox.ZenithColor : glm::vec3(0.18f, 0.42f, 0.78f);
        glm::vec3 skyHorizon = hasSkybox ? skybox.HorizonColor : glm::vec3(0.70f, 0.82f, 0.95f);
        glm::vec3 skyGround = hasSkybox ? skybox.GroundColor : glm::vec3(0.18f, 0.16f, 0.14f);

        shader.SetFloat3("u_CameraPos", camera.GetPosition());
        shader.SetInt("u_UseHemisphereAmbient", useHemisphereAmbient);
        shader.SetFloat3("u_SkyZenithColor", skyZenith);
        shader.SetFloat3("u_SkyHorizonColor", skyHorizon);
        shader.SetFloat3("u_SkyGroundColor", skyGround);

        // Texture sampler slot assignments
        shader.SetInt("u_AlbedoMap", 0);
        shader.SetInt("u_NormalMap", 2);
        shader.SetInt("u_RoughnessMap", 3);
        shader.SetInt("u_MetallicMap", 4);

        // Default texture & material parameters
        shader.SetInt("u_UseAlbedoMap", 0);
        shader.SetInt("u_UseNormalMap", 0);
        shader.SetInt("u_UseRoughnessMap", 0);
        shader.SetInt("u_UseMetallicMap", 0);
        shader.SetFloat2("u_UVScale", glm::vec2(1.0f, 1.0f));
        shader.SetFloat("u_Roughness", 0.5f);
        shader.SetFloat("u_Metallic", 0.0f);

        auto renderView = m_Registry.view<MeshInstance3D, TransformComponent>();
        for (auto entity : renderView) {
            auto& mesh = m_Registry.get<MeshInstance3D>(entity);
            if (mesh.VAO) {
                glm::mat4 worldTransform = GetWorldTransform(entity);
                shader.SetMat4("u_Transform", worldTransform);
                shader.SetInt("u_PatternMode", mesh.PatternMode);
                shader.SetFloat("u_TileScale", mesh.TileScale);

                if (m_Registry.all_of<MaterialComponent>(entity)) {
                    auto& mat = m_Registry.get<MaterialComponent>(entity);

                    if (mat.UseAlbedoMap && mat.AlbedoMap) {
                        mat.AlbedoMap->Bind(0);
                        shader.SetInt("u_UseAlbedoMap", 1);
                    } else {
                        Texture2D::GetWhiteTexture()->Bind(0);
                        shader.SetInt("u_UseAlbedoMap", 0);
                    }

                    if (mat.UseNormalMap && mat.NormalMap) {
                        mat.NormalMap->Bind(2);
                        shader.SetInt("u_UseNormalMap", 1);
                    } else {
                        Texture2D::GetFlatNormalTexture()->Bind(2);
                        shader.SetInt("u_UseNormalMap", 0);
                    }

                    if (mat.UseRoughnessMap && mat.RoughnessMap) {
                        mat.RoughnessMap->Bind(3);
                        shader.SetInt("u_UseRoughnessMap", 1);
                    } else {
                        Texture2D::GetDefaultRoughnessTexture()->Bind(3);
                        shader.SetInt("u_UseRoughnessMap", 0);
                    }

                    if (mat.UseMetallicMap && mat.MetallicMap) {
                        mat.MetallicMap->Bind(4);
                        shader.SetInt("u_UseMetallicMap", 1);
                    } else {
                        Texture2D::GetBlackTexture()->Bind(4);
                        shader.SetInt("u_UseMetallicMap", 0);
                    }

                    shader.SetFloat2("u_UVScale", mat.UVScale);
                    shader.SetFloat("u_Roughness", mat.Roughness);
                    shader.SetFloat("u_Metallic", mat.Metallic);
                    shader.SetFloat("u_NormalStrength", mat.NormalStrength);
                    shader.SetFloat3("u_EmissionColor", mat.EmissionColor);
                    shader.SetFloat("u_EmissionIntensity", mat.EmissionIntensity);
                    shader.SetFloat4("u_Color1", mat.AlbedoColor);
                    shader.SetFloat4("u_Color2", mesh.Color2);
                } else {
                    Texture2D::GetWhiteTexture()->Bind(0);
                    Texture2D::GetFlatNormalTexture()->Bind(2);
                    Texture2D::GetDefaultRoughnessTexture()->Bind(3);
                    Texture2D::GetBlackTexture()->Bind(4);

                    shader.SetInt("u_UseAlbedoMap", 0);
                    shader.SetInt("u_UseNormalMap", 0);
                    shader.SetInt("u_UseRoughnessMap", 0);
                    shader.SetInt("u_UseMetallicMap", 0);
                    shader.SetFloat2("u_UVScale", glm::vec2(1.0f, 1.0f));
                    shader.SetFloat("u_Roughness", 0.5f);
                    shader.SetFloat("u_Metallic", 0.0f);
                    shader.SetFloat("u_NormalStrength", 1.0f);
                    shader.SetFloat3("u_EmissionColor", glm::vec3(0.0f));
                    shader.SetFloat("u_EmissionIntensity", 0.0f);
                    shader.SetFloat4("u_Color1", mesh.Color1);
                    shader.SetFloat4("u_Color2", mesh.Color2);
                }

                RenderCommand::DrawIndexed(mesh.VAO);
            }
        }
    }

    void Scene::RenderShadowPass(Shader& shadowShader, const glm::mat4& lightSpaceMatrix) {
        shadowShader.Bind();
        shadowShader.SetMat4("u_LightSpaceMatrix", lightSpaceMatrix);

        auto renderView = m_Registry.view<MeshInstance3D, TransformComponent>();
        for (auto entity : renderView) {
            auto& mesh = m_Registry.get<MeshInstance3D>(entity);
            if (mesh.VAO) {
                glm::mat4 worldTransform = GetWorldTransform(entity);
                shadowShader.SetMat4("u_Transform", worldTransform);
                RenderCommand::DrawIndexed(mesh.VAO);
            }
        }
    }

    Vehicle* Scene::GetRuntimeVehicle(entt::entity chassisEntityID) const {
        for (const auto& rv : m_RuntimeVehicles) {
            if (rv.ChassisEntityID == chassisEntityID) {
                return rv.VehicleInstance.get();
            }
        }
        return nullptr;
    }

    Vehicle* Scene::GetRuntimeVehicle(Entity chassisEntity) const {
        return GetRuntimeVehicle(static_cast<entt::entity>(chassisEntity));
    }

    Vehicle* Scene::GetPrimaryRuntimeVehicle() const {
        if (!m_RuntimeVehicles.empty()) {
            return m_RuntimeVehicles[0].VehicleInstance.get();
        }
        return nullptr;
    }

    void Scene::OnRuntimeStart(PhysicsScene* physicsScene) {
        m_RuntimePhysicsScene = physicsScene;

        // 1. Initialize C++ Native Scripting components
        auto scriptView = m_Registry.view<NativeScriptComponent>();
        for (auto entity : scriptView) {
            auto& nsc = scriptView.get<NativeScriptComponent>(entity);
            if (!nsc.InstantiateScript && !nsc.ScriptName.empty()) {
                nsc.Bind(nsc.ScriptName);
            }

            if (!nsc.Instance && nsc.InstantiateScript) {
                nsc.Instance = nsc.InstantiateScript();
                nsc.Instance->m_Entity = Entity{ entity, this };
                nsc.SyncFieldsToInstance();
                nsc.Instance->OnCreate();
            }
        }

        // 2. Dynamic Vehicle Assembly & Jolt Physics Instantiation (Unity ECS com.unity.vehicles model)
        if (physicsScene) {
            m_RuntimeVehicles.clear();
            auto vehicleView = m_Registry.view<VehicleComponent>();

            for (auto entityID : vehicleView) {
                Entity chassisEntity{ entityID, this };
                if (!chassisEntity.HasComponent<TransformComponent>()) continue;

                auto& chassisTC = chassisEntity.GetComponent<TransformComponent>();
                auto& vc = chassisEntity.GetComponent<VehicleComponent>();

                VehicleConfig config;
                config.InitialPosition = chassisTC.Translation;
                config.InitialRotation = chassisTC.Rotation;
                config.ChassisMass = vc.ChassisMass;
                config.MaxEngineTorque = vc.MaxEngineTorque;
                config.MaxRPM = vc.MaxRPM;
                config.MinRPM = vc.MinRPM;
                config.MaxBrakeTorque = vc.MaxBrakeTorque;
                config.MaxHandBrakeTorque = vc.MaxHandBrakeTorque;
                config.FourWheelDrive = vc.FourWheelDrive;
                config.CenterOfMassOffset = vc.CenterOfMassOffset;

                if (chassisEntity.HasComponent<CollisionShape3D>()) {
                    const auto& cs = chassisEntity.GetComponent<CollisionShape3D>();
                    if (cs.ShapeType == CollisionShapeType::Box) {
                        config.ChassisHalfExtent = cs.HalfExtent;
                    } else {
                        config.ChassisHalfExtent = glm::max(chassisTC.Scale * 0.5f, glm::vec3(0.1f));
                    }
                } else {
                    config.ChassisHalfExtent = glm::max(chassisTC.Scale * 0.5f, glm::vec3(0.1f));
                }

                if (chassisEntity.HasComponent<RigidBody3D>()) {
                    const auto& rb = chassisEntity.GetComponent<RigidBody3D>();
                    if (rb.Mass > 0.0f) {
                        config.ChassisMass = rb.Mass;
                    }
                }

                // Dynamic Wheel Discovery (VehicleUpdateColliders equivalent):
                // Strategy A: Direct child entities holding WheelComponent
                std::vector<entt::entity> wheelEntityIDs;
                for (auto child : chassisEntity.GetChildren()) {
                    if (child && child.HasComponent<WheelComponent>()) {
                        wheelEntityIDs.push_back(static_cast<entt::entity>(child));
                    }
                }

                // Strategy B: Fallback search across registry if no direct children
                if (wheelEntityIDs.empty()) {
                    auto wheelView = m_Registry.view<WheelComponent>();
                    for (auto wID : wheelView) {
                        wheelEntityIDs.push_back(wID);
                    }
                }

                // Build CustomWheels
                if (!wheelEntityIDs.empty()) {
                    config.CustomWheels.reserve(wheelEntityIDs.size());
                    for (auto wID : wheelEntityIDs) {
                        Entity wEnt{ wID, this };
                        WheelConfig wc;
                        if (wEnt.HasComponent<WheelComponent>()) {
                            const auto& comp = wEnt.GetComponent<WheelComponent>();
                            wc.Radius = comp.Radius;
                            wc.Width = comp.Width;
                            wc.SuspensionMinLength = comp.SuspensionMinLength;
                            wc.SuspensionMaxLength = comp.SuspensionMaxLength;
                            wc.SpringFrequency = comp.SpringFrequency;
                            wc.SpringDamping = comp.SpringDamping;
                            wc.CanSteer = comp.CanSteer;
                            wc.MaxSteerAngleDegrees = comp.MaxSteerAngle;
                            wc.IsDriven = comp.IsDriven;
                            wc.MaxBrakeTorque = comp.BrakeTorque;
                            wc.MaxHandBrakeTorque = comp.HasHandbrake ? comp.HandBrakeTorque : 0.0f;
                        }

                        if (wEnt.HasComponent<TransformComponent>()) {
                            const auto& wtc = wEnt.GetComponent<TransformComponent>();
                            if (wEnt.GetParent() == chassisEntity) {
                                wc.Position = wtc.Translation;
                            } else {
                                glm::vec3 worldPos = glm::vec3(wEnt.GetWorldTransform()[3]);
                                wc.Position = glm::inverse(chassisTC.Rotation) * (worldPos - chassisTC.Translation);
                            }
                        }

                        // Ensure wheel visual mesh uses local coordinate mapping so roll rotation is visible
                        if (wEnt.HasComponent<MeshInstance3D>()) {
                            auto& mc = wEnt.GetComponent<MeshInstance3D>();
                            if (mc.PatternMode == 0) {
                                mc.PatternMode = 1;
                                if (mc.TileScale <= 1.0f) mc.TileScale = 4.0f;
                            }
                        }

                        config.CustomWheels.push_back(wc);
                    }
                }

                // Destroy pre-existing chassis body in PhysicsScene to avoid duplicate overlapping rigid bodies
                if (chassisEntity.HasComponent<RigidBody3D>()) {
                    auto& rb = chassisEntity.GetComponent<RigidBody3D>();
                    if (!rb.RuntimeBodyID.IsInvalid() && physicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                        physicsScene->DestroyBody(rb.RuntimeBodyID);
                        rb.RuntimeBodyID = {};
                    }
                }

                auto vehicleInstance = std::make_unique<Vehicle>(*physicsScene, config);
                vehicleInstance->Reset(chassisTC.Translation, chassisTC.Rotation);

                vc.VehicleInstance = vehicleInstance.get();

                if (chassisEntity.HasComponent<RigidBody3D>()) {
                    chassisEntity.GetComponent<RigidBody3D>().RuntimeBodyID = vehicleInstance->GetChassisBodyID();
                } else {
                    chassisEntity.AddComponent<RigidBody3D>(vehicleInstance->GetChassisBodyID(), true);
                }

                if (!chassisEntity.HasComponent<CollisionShape3D>()) {
                    chassisEntity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, config.ChassisHalfExtent, 0.5f, 0.0f);
                }

                RuntimeVehicleData rvData;
                rvData.ChassisEntityID = entityID;
                rvData.VehicleInstance = std::move(vehicleInstance);
                rvData.WheelEntityIDs = std::move(wheelEntityIDs);
                m_RuntimeVehicles.push_back(std::move(rvData));
            }
        }
    }

    void Scene::OnUpdateRuntime(Timestep ts, PhysicsScene* physicsScene) {
        PhysicsScene* pScene = physicsScene ? physicsScene : m_RuntimePhysicsScene;

        // 1. Update C++ Native Scripting components
        auto scriptView = m_Registry.view<NativeScriptComponent>();
        for (auto entity : scriptView) {
            auto& nsc = scriptView.get<NativeScriptComponent>(entity);
            if (!nsc.InstantiateScript && !nsc.ScriptName.empty()) {
                nsc.Bind(nsc.ScriptName);
            }

            if (!nsc.Instance && nsc.InstantiateScript) {
                nsc.Instance = nsc.InstantiateScript();
                nsc.Instance->m_Entity = Entity{ entity, this };
                nsc.SyncFieldsToInstance();
                nsc.Instance->OnCreate();
            }

            if (nsc.Instance) {
                nsc.Instance->OnUpdate(ts);
            }
        }

        // 2. Real-time Cache-Friendly Vehicle Physics & Wheel Pose Synchronization
        for (auto& rv : m_RuntimeVehicles) {
            if (!rv.VehicleInstance || rv.ChassisEntityID == entt::null || !m_Registry.valid(rv.ChassisEntityID)) {
                continue;
            }

            Entity chassisEntity{ rv.ChassisEntityID, this };
            if (!chassisEntity.HasComponent<VehicleComponent>() || !chassisEntity.HasComponent<TransformComponent>()) {
                continue;
            }

            auto& vc = chassisEntity.GetComponent<VehicleComponent>();
            auto& chassisTC = chassisEntity.GetComponent<TransformComponent>();

            // Dispatch ECS driver inputs to physics vehicle controller
            rv.VehicleInstance->SetDriverInput(vc.Throttle, vc.Steering, vc.Brake, vc.Handbrake ? 1.0f : 0.0f);

            // Sync chassis TransformComponent with vehicle physics body
            chassisTC.Translation = rv.VehicleInstance->GetPosition();
            if (pScene) {
                chassisTC.Rotation = pScene->GetBodyRotation(rv.VehicleInstance->GetChassisBodyID());
            }

            // Sync vehicle telemetry to VehicleComponent buffer
            auto telemetry = rv.VehicleInstance->GetTelemetry();
            vc.CurrentSpeedKmh = telemetry.SpeedKmh;
            vc.CurrentSpeedMps = telemetry.SpeedMps;
            vc.CurrentRPM = telemetry.EngineRPM;
            vc.CurrentGear = telemetry.CurrentGear;

            // Sync child wheel entities (Transforms & Telemetry)
            for (size_t i = 0; i < rv.WheelEntityIDs.size(); ++i) {
                entt::entity wID = rv.WheelEntityIDs[i];
                if (wID == entt::null || !m_Registry.valid(wID)) continue;

                Entity wEnt{ wID, this };
                const glm::mat4 wheelMat = rv.VehicleInstance->GetWheelTransform(static_cast<u32>(i));

                if (wEnt.HasComponent<TransformComponent>()) {
                    auto& wtc = wEnt.GetComponent<TransformComponent>();
                    bool autoSync = true;
                    if (wEnt.HasComponent<WheelComponent>()) {
                        autoSync = wEnt.GetComponent<WheelComponent>().AutoSyncMesh;
                    }

                    if (autoSync) {
                        if (wEnt.GetParent() == chassisEntity) {
                            glm::mat4 chassisWorld = rv.VehicleInstance->GetChassisTransform();
                            glm::mat4 localWheelMat = glm::inverse(chassisWorld) * wheelMat;
                            wtc.Translation = glm::vec3(localWheelMat[3]);
                            wtc.Rotation = glm::quat_cast(localWheelMat);
                        } else {
                            wtc.Translation = glm::vec3(wheelMat[3]);
                            wtc.Rotation = glm::quat_cast(wheelMat);
                        }
                    }
                }

                if (wEnt.HasComponent<WheelComponent>() && i < 4) {
                    auto& wc = wEnt.GetComponent<WheelComponent>();
                    const auto& wData = telemetry.Wheels[i];
                    wc.IsGrounded = wData.HasContact;
                    wc.SuspensionLength = wData.SuspensionLength;
                    wc.AngularVelocity = wData.AngularVelocity;
                    wc.SteerAngle = wData.SteerAngle;
                    wc.SlipAngle = glm::degrees(wData.SteerAngle);
                    wc.LateralSlip = wData.LateralSlip;
                    wc.LongitudinalSlip = wData.LongitudinalSlip;
                }
            }
        }
    }

    void Scene::OnRuntimeStop(PhysicsScene* physicsScene) {
        // Reset and clear runtime vehicles cleanly
        for (auto& rv : m_RuntimeVehicles) {
            if (rv.ChassisEntityID != entt::null && m_Registry.valid(rv.ChassisEntityID)) {
                Entity chassisEntity{ rv.ChassisEntityID, this };
                if (chassisEntity.HasComponent<VehicleComponent>()) {
                    auto& vc = chassisEntity.GetComponent<VehicleComponent>();
                    vc.VehicleInstance = nullptr;
                    vc.Throttle = vc.Steering = vc.Brake = 0.0f;
                    vc.Handbrake = false;
                    vc.CurrentSpeedKmh = vc.CurrentSpeedMps = vc.CurrentRPM = 0.0f;
                }
            }
        }
        m_RuntimeVehicles.clear();
        m_RuntimePhysicsScene = nullptr;

        auto view = m_Registry.view<NativeScriptComponent>();
        for (auto entity : view) {
            auto& nsc = view.get<NativeScriptComponent>(entity);
            if (nsc.Instance && nsc.DestroyScript) {
                nsc.Instance->OnDestroy();
                nsc.DestroyScript(&nsc);
            }
        }
    }

    void Scene::OnPreScriptReload() {
        auto view = m_Registry.view<NativeScriptComponent>();
        for (auto entity : view) {
            auto& nsc = view.get<NativeScriptComponent>(entity);
            if (nsc.Instance) {
                // Pull modified values back to buffers
                nsc.SyncFieldsFromInstance();
                if (nsc.DestroyScript) {
                    nsc.DestroyScript(&nsc);
                } else {
                    delete nsc.Instance;
                    nsc.Instance = nullptr;
                }
            }
            nsc.InstantiateScript = nullptr;
            nsc.DestroyScript = nullptr;
        }
    }

    void Scene::OnPostScriptReload(bool isRuntimeActive) {
        auto view = m_Registry.view<NativeScriptComponent>();
        for (auto entity : view) {
            auto& nsc = view.get<NativeScriptComponent>(entity);
            if (!nsc.ScriptName.empty() && ScriptRegistry::Has(nsc.ScriptName)) {
                nsc.Bind(nsc.ScriptName);
                if (isRuntimeActive && nsc.InstantiateScript) {
                    nsc.Instance = nsc.InstantiateScript();
                    if (nsc.Instance) {
                        nsc.Instance->m_Entity = Entity{ entity, this };
                        nsc.SyncFieldsToInstance();
                        nsc.Instance->OnCreate();
                    }
                }
            } else {
                nsc.InstantiateScript = nullptr;
                nsc.DestroyScript = nullptr;
                nsc.Instance = nullptr;
            }
        }
    }

    std::shared_ptr<Scene> Scene::Copy(std::shared_ptr<Scene> other) {
        if (!other) return nullptr;

        std::shared_ptr<Scene> newScene = std::make_shared<Scene>();

        // Map old UUID to new Entity handle
        std::unordered_map<UUID, entt::entity> entMap;

        // Pass 1: Clone all entities preserving exact UUID and Tag
        auto idView = other->m_Registry.view<IDComponent>();
        for (auto srcEntityID : idView) {
            UUID uuid = other->m_Registry.get<IDComponent>(srcEntityID).ID;
            std::string name = "Entity";
            if (other->m_Registry.all_of<TagComponent>(srcEntityID)) {
                name = other->m_Registry.get<TagComponent>(srcEntityID).Tag;
            }

            Entity dstEntity = newScene->CreateEntityWithUUID(uuid, name);
            entMap[uuid] = (entt::entity)dstEntity;
        }

        // Pass 2: Deep copy all components
        for (auto srcEntityID : idView) {
            UUID uuid = other->m_Registry.get<IDComponent>(srcEntityID).ID;
            Entity srcEntity{ srcEntityID, other.get() };
            Entity dstEntity{ entMap[uuid], newScene.get() };

            // TransformComponent
            if (srcEntity.HasComponent<TransformComponent>()) {
                dstEntity.GetComponent<TransformComponent>() = srcEntity.GetComponent<TransformComponent>();
            }

            // MeshInstance3D
            if (srcEntity.HasComponent<MeshInstance3D>()) {
                dstEntity.AddComponent<MeshInstance3D>(srcEntity.GetComponent<MeshInstance3D>());
            }

            // MaterialComponent
            if (srcEntity.HasComponent<MaterialComponent>()) {
                dstEntity.AddComponent<MaterialComponent>(srcEntity.GetComponent<MaterialComponent>());
            }

            // DirectionalLightComponent
            if (srcEntity.HasComponent<DirectionalLightComponent>()) {
                dstEntity.AddComponent<DirectionalLightComponent>(srcEntity.GetComponent<DirectionalLightComponent>());
            }

            // PointLightComponent
            if (srcEntity.HasComponent<PointLightComponent>()) {
                dstEntity.AddComponent<PointLightComponent>(srcEntity.GetComponent<PointLightComponent>());
            }

            // CameraComponent
            if (srcEntity.HasComponent<CameraComponent>()) {
                dstEntity.AddComponent<CameraComponent>(srcEntity.GetComponent<CameraComponent>());
            }

            // SkyboxComponent
            if (srcEntity.HasComponent<SkyboxComponent>()) {
                dstEntity.AddComponent<SkyboxComponent>(srcEntity.GetComponent<SkyboxComponent>());
            }

            // AssemblyComponent
            if (srcEntity.HasComponent<AssemblyComponent>()) {
                dstEntity.AddComponent<AssemblyComponent>(srcEntity.GetComponent<AssemblyComponent>());
            }

            // CollisionShape3D
            if (srcEntity.HasComponent<CollisionShape3D>()) {
                dstEntity.AddComponent<CollisionShape3D>(srcEntity.GetComponent<CollisionShape3D>());
            }

            // RigidBody3D
            if (srcEntity.HasComponent<RigidBody3D>()) {
                dstEntity.AddComponent<RigidBody3D>(srcEntity.GetComponent<RigidBody3D>());
            }

            // NativeScriptComponent
            if (srcEntity.HasComponent<NativeScriptComponent>()) {
                const auto& srcNsc = srcEntity.GetComponent<NativeScriptComponent>();
                auto& dstNsc = dstEntity.AddComponent<NativeScriptComponent>(srcNsc.ScriptName);
                dstNsc.FloatFields = srcNsc.FloatFields;
                dstNsc.IntFields = srcNsc.IntFields;
                dstNsc.BoolFields = srcNsc.BoolFields;
                dstNsc.Vec3Fields = srcNsc.Vec3Fields;
                dstNsc.StringFields = srcNsc.StringFields;
                dstNsc.EntityFields = srcNsc.EntityFields;
                dstNsc.InstantiateScript = srcNsc.InstantiateScript;
                dstNsc.DestroyScript = srcNsc.DestroyScript;
            }

            // VehicleComponent
            if (srcEntity.HasComponent<VehicleComponent>()) {
                dstEntity.AddComponent<VehicleComponent>(srcEntity.GetComponent<VehicleComponent>());
            }

            // WheelComponent
            if (srcEntity.HasComponent<WheelComponent>()) {
                dstEntity.AddComponent<WheelComponent>(srcEntity.GetComponent<WheelComponent>());
            }
        }

        // Pass 3: Remap Hierarchy Relationships using UUID mapping
        for (auto srcEntityID : idView) {
            UUID uuid = other->m_Registry.get<IDComponent>(srcEntityID).ID;
            Entity srcEntity{ srcEntityID, other.get() };
            Entity dstEntity{ entMap[uuid], newScene.get() };

            if (srcEntity.HasComponent<HierarchyComponent>()) {
                const auto& srcHc = srcEntity.GetComponent<HierarchyComponent>();
                if (srcHc.Parent != entt::null && other->m_Registry.valid(srcHc.Parent)) {
                    UUID parentUUID = other->m_Registry.get<IDComponent>(srcHc.Parent).ID;
                    auto it = entMap.find(parentUUID);
                    if (it != entMap.end()) {
                        dstEntity.SetParent(Entity{ it->second, newScene.get() }, false);
                        if (dstEntity.HasComponent<HierarchyComponent>()) {
                            dstEntity.GetComponent<HierarchyComponent>().InheritTransform = srcHc.InheritTransform;
                            dstEntity.GetComponent<HierarchyComponent>().InheritScale = srcHc.InheritScale;
                        }
                    }
                }
            }
        }

        newScene->UpdateWorldTransforms();
        return newScene;
    }

    void Scene::RestorePhysicsBodies(PhysicsScene* physicsScene) {
        if (!physicsScene) return;

        auto view = m_Registry.view<RigidBody3D, TransformComponent>();
        for (auto entity : view) {
            const auto& rb = view.get<RigidBody3D>(entity);
            const auto& tc = view.get<TransformComponent>(entity);

            if (!rb.RuntimeBodyID.IsInvalid() && physicsScene->IsBodyValid(rb.RuntimeBodyID)) {
                glm::vec3 offset(0.0f);
                if (m_Registry.all_of<CollisionShape3D>(entity)) {
                    offset = m_Registry.get<CollisionShape3D>(entity).Offset;
                }
                glm::vec3 worldPos = tc.Translation + tc.Rotation * offset;
                physicsScene->SetBodyPositionAndRotation(rb.RuntimeBodyID, worldPos, tc.Rotation, true);
                physicsScene->SetLinearVelocity(rb.RuntimeBodyID, glm::vec3(0.0f));
                physicsScene->GetBodyInterface().SetAngularVelocity(rb.RuntimeBodyID, JPH::Vec3::sZero());
            }
        }
    }

}

