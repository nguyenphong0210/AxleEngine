#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/Texture2D.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/PhysicsScene.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

namespace Axle {

    struct IDComponent {
        UUID ID;

        IDComponent() = default;
        IDComponent(const UUID& id)
            : ID(id) {}
    };

    struct TagComponent {
        std::string Tag;

        TagComponent() = default;
        TagComponent(const std::string& tag)
            : Tag(tag) {}
    };

    struct TransformComponent {
        glm::vec3 Translation{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f };

        glm::mat4 LocalMatrix{ 1.0f };
        glm::mat4 WorldMatrix{ 1.0f };

        TransformComponent() = default;
        TransformComponent(const glm::vec3& translation)
            : Translation(translation) {}
        TransformComponent(const glm::vec3& translation, const glm::vec3& scale)
            : Translation(translation), Scale(scale) {}
        TransformComponent(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
            : Translation(translation), Rotation(rotation), Scale(scale) {}

        glm::mat4 GetLocalTransform() const {
            return glm::translate(glm::mat4(1.0f), Translation)
                * glm::mat4_cast(Rotation)
                * glm::scale(glm::mat4(1.0f), Scale);
        }

        glm::mat4 GetTransform() const {
            return GetLocalTransform();
        }
    };

    struct MeshInstance3D {
        std::shared_ptr<VertexArray> VAO;
        std::string AssetPath;

        // Visual Caro (Checkerboard) material properties
        int PatternMode = 1; // 0: World XZ Caro (Floor only), 1: Local 3D Caro (Default for 3D meshes), 2: Spherical Caro
        float TileScale = 4.0f; // Default 4.0 so procedural checker tiles are immediately visible
        glm::vec4 Color1{ 0.88f, 0.89f, 0.92f, 1.0f }; // Titanium Silver
        glm::vec4 Color2{ 0.45f, 0.48f, 0.52f, 1.0f }; // Slate Graphite

        MeshInstance3D() = default;
        MeshInstance3D(const std::shared_ptr<VertexArray>& vao, int patternMode, float tileScale, const glm::vec4& c1, const glm::vec4& c2)
            : VAO(vao), PatternMode(patternMode), TileScale(tileScale), Color1(c1), Color2(c2) {}
        MeshInstance3D(const std::shared_ptr<VertexArray>& vao, const std::string& assetPath, int patternMode, float tileScale, const glm::vec4& c1, const glm::vec4& c2)
            : VAO(vao), AssetPath(assetPath), PatternMode(patternMode), TileScale(tileScale), Color1(c1), Color2(c2) {}
    };
    using MeshComponent = MeshInstance3D; // Backward compatibility alias

    struct MaterialComponent {
        std::string AlbedoPath;
        std::shared_ptr<Texture2D> AlbedoMap;
        bool UseAlbedoMap = false;
        glm::vec4 AlbedoColor{ 1.0f, 1.0f, 1.0f, 1.0f };

        std::string NormalPath;
        std::shared_ptr<Texture2D> NormalMap;
        bool UseNormalMap = false;
        float NormalStrength = 1.0f;

        std::string RoughnessPath;
        std::shared_ptr<Texture2D> RoughnessMap;
        bool UseRoughnessMap = false;
        float Roughness = 0.5f;

        std::string MetallicPath;
        std::shared_ptr<Texture2D> MetallicMap;
        bool UseMetallicMap = false;
        float Metallic = 0.0f;

        glm::vec3 EmissionColor{ 0.0f, 0.0f, 0.0f };
        float EmissionIntensity = 0.0f;

        glm::vec2 UVScale{ 1.0f, 1.0f };

        MaterialComponent() = default;

        void ResetTextures() {
            AlbedoPath.clear();
            AlbedoMap = nullptr;
            UseAlbedoMap = false;

            NormalPath.clear();
            NormalMap = nullptr;
            UseNormalMap = false;

            RoughnessPath.clear();
            RoughnessMap = nullptr;
            UseRoughnessMap = false;

            MetallicPath.clear();
            MetallicMap = nullptr;
            UseMetallicMap = false;
        }

        void SetPresetCarPaintRed() {
            ResetTextures();
            AlbedoColor = glm::vec4(0.85f, 0.06f, 0.08f, 1.0f);
            Roughness = 0.15f;
            Metallic = 0.0f;
            EmissionColor = glm::vec3(0.0f);
            EmissionIntensity = 0.0f;
        }

        void SetPresetCarPaintBlue() {
            ResetTextures();
            AlbedoColor = glm::vec4(0.08f, 0.28f, 0.65f, 1.0f);
            Roughness = 0.22f;
            Metallic = 0.75f;
            EmissionColor = glm::vec3(0.0f);
            EmissionIntensity = 0.0f;
        }

        void SetPresetChrome() {
            ResetTextures();
            AlbedoColor = glm::vec4(0.95f, 0.95f, 0.95f, 1.0f);
            Roughness = 0.04f;
            Metallic = 1.0f;
            EmissionColor = glm::vec3(0.0f);
            EmissionIntensity = 0.0f;
        }

        void SetPresetGold() {
            ResetTextures();
            AlbedoColor = glm::vec4(1.00f, 0.77f, 0.34f, 1.0f);
            Roughness = 0.18f;
            Metallic = 1.0f;
            EmissionColor = glm::vec3(0.0f);
            EmissionIntensity = 0.0f;
        }

        void SetPresetCopper() {
            ResetTextures();
            AlbedoColor = glm::vec4(0.96f, 0.64f, 0.54f, 1.0f);
            Roughness = 0.35f;
            Metallic = 0.95f;
            EmissionColor = glm::vec3(0.0f);
            EmissionIntensity = 0.0f;
        }

        void SetPresetRubber() {
            ResetTextures();
            AlbedoColor = glm::vec4(0.07f, 0.07f, 0.07f, 1.0f);
            Roughness = 0.85f;
            Metallic = 0.0f;
            EmissionColor = glm::vec3(0.0f);
            EmissionIntensity = 0.0f;
        }

        void SetPresetPlastic() {
            ResetTextures();
            AlbedoColor = glm::vec4(0.80f, 0.80f, 0.80f, 1.0f);
            Roughness = 0.60f;
            Metallic = 0.0f;
            EmissionColor = glm::vec3(0.0f);
            EmissionIntensity = 0.0f;
        }

        void SetPresetCastIron() {
            ResetTextures();
            AlbedoColor = glm::vec4(0.56f, 0.57f, 0.58f, 1.0f);
            Roughness = 0.45f;
            Metallic = 1.0f;
            EmissionColor = glm::vec3(0.0f);
            EmissionIntensity = 0.0f;
        }

        void SetPresetAluminum() {
            ResetTextures();
            AlbedoColor = glm::vec4(0.97f, 0.96f, 0.92f, 1.0f);
            Roughness = 0.12f;
            Metallic = 1.0f;
            EmissionColor = glm::vec3(0.0f);
            EmissionIntensity = 0.0f;
        }

        void SetPresetObsidian() {
            ResetTextures();
            AlbedoColor = glm::vec4(0.04f, 0.04f, 0.05f, 1.0f);
            Roughness = 0.08f;
            Metallic = 0.0f;
            EmissionColor = glm::vec3(0.0f);
            EmissionIntensity = 0.0f;
        }

        void SetPresetHeadlightLED() {
            ResetTextures();
            AlbedoColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            Roughness = 0.10f;
            Metallic = 0.0f;
            EmissionColor = glm::vec3(0.9f, 0.95f, 1.0f);
            EmissionIntensity = 5.0f;
        }

        void SetPresetTaillightLED() {
            ResetTextures();
            AlbedoColor = glm::vec4(0.9f, 0.05f, 0.05f, 1.0f);
            Roughness = 0.20f;
            Metallic = 0.0f;
            EmissionColor = glm::vec3(1.0f, 0.05f, 0.05f);
            EmissionIntensity = 4.0f;
        }

        void SetAlbedo(const std::string& path) {
            AlbedoPath = path;
            if (!path.empty()) {
                auto resolved = FileSystem::ResolveAssetPath(path);
                AlbedoMap = Texture2D::Create(resolved.string());
                UseAlbedoMap = (AlbedoMap && AlbedoMap->IsLoaded());
            } else {
                AlbedoMap = nullptr;
                UseAlbedoMap = false;
            }
        }

        void SetNormal(const std::string& path) {
            NormalPath = path;
            if (!path.empty()) {
                auto resolved = FileSystem::ResolveAssetPath(path);
                NormalMap = Texture2D::Create(resolved.string());
                UseNormalMap = (NormalMap && NormalMap->IsLoaded());
            } else {
                NormalMap = nullptr;
                UseNormalMap = false;
            }
        }

        void SetRoughnessMap(const std::string& path) {
            RoughnessPath = path;
            if (!path.empty()) {
                auto resolved = FileSystem::ResolveAssetPath(path);
                RoughnessMap = Texture2D::Create(resolved.string());
                UseRoughnessMap = (RoughnessMap && RoughnessMap->IsLoaded());
            } else {
                RoughnessMap = nullptr;
                UseRoughnessMap = false;
            }
        }

        void SetMetallicMap(const std::string& path) {
            MetallicPath = path;
            if (!path.empty()) {
                auto resolved = FileSystem::ResolveAssetPath(path);
                MetallicMap = Texture2D::Create(resolved.string());
                UseMetallicMap = (MetallicMap && MetallicMap->IsLoaded());
            } else {
                MetallicMap = nullptr;
                UseMetallicMap = false;
            }
        }
    };

    enum class CollisionShapeType {
        Box = 0,
        Sphere = 1,
        Cylinder = 2,
        Cone = 3,
        Plane = 4,
        Mesh = 5
    };
    using BodyShapeType = CollisionShapeType; // Backward compatibility alias

    struct CollisionShape3D {
        CollisionShapeType ShapeType = CollisionShapeType::Box;
        glm::vec3 HalfExtent{ 0.5f };
        float Radius = 0.5f;
        float HalfHeight = 0.5f;
        glm::vec3 Offset{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false;

        CollisionShape3D() = default;
        explicit CollisionShape3D(CollisionShapeType shape)
            : ShapeType(shape) {}

        // Box / Plane: 2 args (shape, halfExtent) and 4 args (shape, halfExtent, friction, restitution)
        CollisionShape3D(CollisionShapeType shape, const glm::vec3& halfExtent)
            : ShapeType(shape), HalfExtent(halfExtent) {
            Radius = glm::max(halfExtent.x, 0.01f);
            HalfHeight = glm::max(halfExtent.y, 0.01f);
        }
        CollisionShape3D(CollisionShapeType shape, const glm::vec3& halfExtent, float friction, float restitution)
            : ShapeType(shape), HalfExtent(halfExtent), Friction(friction), Restitution(restitution) {
            Radius = glm::max(halfExtent.x, 0.01f);
            HalfHeight = glm::max(halfExtent.y, 0.01f);
        }

        // Sphere: 2 args (shape, radius) and 4 args (shape, radius, friction, restitution)
        CollisionShape3D(CollisionShapeType shape, float radius)
            : ShapeType(shape), Radius(radius), HalfExtent(glm::vec3(radius)) {}
        CollisionShape3D(CollisionShapeType shape, float radius, float friction, float restitution)
            : ShapeType(shape), Radius(radius), HalfExtent(glm::vec3(radius)), Friction(friction), Restitution(restitution) {}

        // Cylinder / Cone: 3 args (shape, halfHeight, radius) and 5 args (shape, halfHeight, radius, friction, restitution)
        CollisionShape3D(CollisionShapeType shape, float halfHeight, float radius)
            : ShapeType(shape), HalfHeight(halfHeight), Radius(radius), HalfExtent(glm::vec3(radius, halfHeight, radius)) {}
        CollisionShape3D(CollisionShapeType shape, float halfHeight, float radius, float friction, float restitution)
            : ShapeType(shape), HalfHeight(halfHeight), Radius(radius), HalfExtent(glm::vec3(radius, halfHeight, radius)), Friction(friction), Restitution(restitution) {}
    };

    enum class BodyMotionType {
        Static = 0,
        Kinematic = 1,
        Dynamic = 2
    };

    struct RigidBody3D {
        BodyID RuntimeBodyID;
        BodyMotionType MotionType = BodyMotionType::Dynamic;
        bool IsDynamic = true;
        float Mass = 1.0f;
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;
        float GravityFactor = 1.0f;

        RigidBody3D() = default;
        RigidBody3D(BodyID bodyId, bool isDynamic = true)
            : RuntimeBodyID(bodyId), IsDynamic(isDynamic), MotionType(isDynamic ? BodyMotionType::Dynamic : BodyMotionType::Static) {}
        RigidBody3D(BodyID bodyId, BodyMotionType motionType)
            : RuntimeBodyID(bodyId), MotionType(motionType), IsDynamic(motionType == BodyMotionType::Dynamic) {}
        // Backward compatibility constructor overload (shape moved to CollisionShape3D)
        RigidBody3D(BodyID bodyId, bool isDynamic, CollisionShapeType /*shape*/, const glm::vec3& /*halfExtent*/, float /*friction*/ = 0.5f, float /*restitution*/ = 0.0f)
            : RuntimeBodyID(bodyId), IsDynamic(isDynamic), MotionType(isDynamic ? BodyMotionType::Dynamic : BodyMotionType::Static) {}
    };
    using RigidBodyComponent = RigidBody3D; // Backward compatibility alias

    struct HierarchyComponent {
        entt::entity Parent{ entt::null };
        std::vector<entt::entity> Children;
        bool InheritTransform = true;
        bool InheritScale = true;

        HierarchyComponent() = default;
        HierarchyComponent(entt::entity parent, bool inheritTransform = true, bool inheritScale = true)
            : Parent(parent), InheritTransform(inheritTransform), InheritScale(inheritScale) {}
    };
    using RelationshipComponent = HierarchyComponent; // Relationship / Scene Graph alias

    class ScriptableEntity;

    struct NativeScriptComponent {
        ScriptableEntity* Instance = nullptr;
        std::string ScriptName = "NativeScript";

        ScriptableEntity* (*InstantiateScript)() = nullptr;
        void (*DestroyScript)(NativeScriptComponent*) = nullptr;

        // Edit Mode & Serialization Field Buffers
        std::unordered_map<std::string, float> FloatFields;
        std::unordered_map<std::string, int> IntFields;
        std::unordered_map<std::string, bool> BoolFields;
        std::unordered_map<std::string, glm::vec3> Vec3Fields;
        std::unordered_map<std::string, std::string> StringFields;
        std::unordered_map<std::string, UUID> EntityFields;

        NativeScriptComponent() = default;
        NativeScriptComponent(const std::string& scriptName) {
            Bind(scriptName);
        }

        void Bind(const std::string& scriptName);
        void SyncFieldsToInstance();
        void SyncFieldsFromInstance();

        template<typename T>
        void Bind(const std::string& name = "") {
            std::string actualName = name.empty() ? typeid(T).name() : name;
            ScriptName = actualName;
            InstantiateScript = []() -> ScriptableEntity* { return static_cast<ScriptableEntity*>(new T()); };
            DestroyScript = [](NativeScriptComponent* nsc) {
                delete nsc->Instance;
                nsc->Instance = nullptr;
            };
        }
    };

    class Vehicle;

    // Modern Unity ECS-style Vehicle Root Component (VehicleControlAuthoring equivalent)
    struct VehicleComponent {
        Vehicle* VehicleInstance = nullptr;

        // Driver Input Buffer (Pure ECS Data)
        float Throttle = 0.0f;              // -1.0 (Reverse) to +1.0 (Forward)
        float Steering = 0.0f;              // -1.0 (Full Left) to +1.0 (Full Right)
        float Brake = 0.0f;                 // 0.0 (None) to 1.0 (Full Brake)
        bool Handbrake = false;             // Handbrake state

        // Chassis & Powertrain Tuning (Phase 20.2 & 20.3)
        float ChassisMass = 1500.0f;        // Chassis mass in kg
        float MaxEngineTorque = 800.0f;     // Engine peak torque in N.m
        float MaxRPM = 7000.0f;             // Redline RPM
        float MinRPM = 1000.0f;             // Idle RPM
        float MaxBrakeTorque = 3000.0f;     // Service brake torque in N.m
        float MaxHandBrakeTorque = 6000.0f; // Handbrake torque in N.m
        float AntiRollBarStiffness = 1000.0f; // Anti-roll bar stiffness (N/m)

        // Default suspension settings for child wheel spawning
        float DefaultSuspensionFrequency = 2.0f; // Spring frequency (Hz)
        float DefaultSuspensionDamping = 0.5f;   // Suspension damping ratio (0..1)
        bool FourWheelDrive = true;              // AWD vs RWD
        glm::vec3 CenterOfMassOffset{ 0.0f, 0.0f, 0.0f }; // Center of mass offset relative to geometric pivot

        // Live Telemetry (Updated per-frame by Scene runtime physics sync)
        float CurrentSpeedKmh = 0.0f;       // Vehicle linear speed in km/h
        float CurrentSpeedMps = 0.0f;       // Vehicle linear speed in m/s
        float CurrentRPM = 0.0f;            // Engine rotational speed in RPM
        int CurrentGear = 1;                // Active transmission gear

        VehicleComponent() = default;
        VehicleComponent(Vehicle* vehicle)
            : VehicleInstance(vehicle) {}

        // Clean C++ API for Scripts and Controllers
        void SetDriverInput(float throttle, float steering, float brake, bool handbrake = false) {
            Throttle = std::clamp(throttle, -1.0f, 1.0f);
            Steering = std::clamp(steering, -1.0f, 1.0f);
            Brake = std::clamp(brake, 0.0f, 1.0f);
            Handbrake = handbrake;
        }

        float GetSpeedKmh() const { return CurrentSpeedKmh; }
        float GetSpeedMps() const { return CurrentSpeedMps; }
        float GetRPM() const { return CurrentRPM; }
        int GetCurrentGear() const { return CurrentGear; }
        bool HasActivePhysics() const { return VehicleInstance != nullptr; }
    };

    // Modern Unity ECS-style Wheel Component (ControllableWheelAuthoring / VehicleWheel3D equivalent)
    struct WheelComponent {
        // Physical Geometry
        float Radius = 0.35f;               // Wheel tire radius (m)
        float Width = 0.25f;                // Wheel tire width (m)

        // Suspension Dynamics
        float SuspensionMinLength = 0.15f;  // Maximum suspension compression limit (m)
        float SuspensionMaxLength = 0.45f;  // Maximum suspension extension limit (m)
        float SpringFrequency = 2.0f;       // Spring oscillation frequency (Hz)
        float SpringDamping = 0.5f;         // Suspension damping ratio (0..1)

        // Steering & Powertrain Control Flags
        bool CanSteer = false;              // Steerable wheel (responds to steering input)
        float MaxSteerAngle = 32.0f;        // Peak steering angle (degrees)
        bool IsDriven = true;               // Driven wheel (receives powertrain engine torque)
        bool HasHandbrake = false;          // Locked by handbrake input
        float BrakeTorque = 3000.0f;        // Service brake torque (N.m)
        float HandBrakeTorque = 0.0f;       // Handbrake torque (N.m)

        // Mesh & Transform Automation
        bool AutoSyncMesh = true;           // Automatically sync Entity TransformComponent to physical wheel pose

        // Live Dynamic Telemetry (Updated per-frame by Scene runtime physics sync)
        bool IsGrounded = false;            // Ground contact flag
        float SuspensionLength = 0.0f;      // Current suspension travel length (m)
        float AngularVelocity = 0.0f;       // Wheel spin angular velocity (rad/s)
        float SteerAngle = 0.0f;            // Current dynamic steering angle (rad)
        float SlipAngle = 0.0f;             // Tire slip angle (degrees)
        float LateralSlip = 0.0f;           // Sideways tire slip velocity
        float LongitudinalSlip = 0.0f;      // Forward/rearward tire slip ratio

        WheelComponent() = default;
    };
    using VehicleWheel3D = WheelComponent; // Godot-style decoupled wheel alias


    struct DirectionalLightComponent {
        glm::vec3 Color{ 1.0f, 0.98f, 0.92f }; // Sunlight warm tint
        float Intensity = 1.0f;
        float AmbientIntensity = 0.28f;
        float Azimuth = 45.0f;    // Compass angle (0..360 deg)
        float Elevation = 50.0f;  // Sun altitude angle (5..90 deg)
        bool CastShadows = true;

        // Post-Processing & Photometric Tone Mapping (Phase 11: Sub-step 3.2)
        float Exposure = 1.0f;
        int ToneMappingMode = 0;  // 0: ACES Filmic, 1: Reinhard, 2: Uncharted 2, 3: Linear Bypass
        float Gamma = 2.2f;

        DirectionalLightComponent() = default;
        DirectionalLightComponent(const glm::vec3& color, float intensity = 1.0f, float ambient = 0.28f, float azimuth = 45.0f, float elevation = 50.0f, bool castShadows = true,
                                  float exposure = 1.0f, int toneMapping = 0, float gamma = 2.2f)
            : Color(color), Intensity(intensity), AmbientIntensity(ambient), Azimuth(azimuth), Elevation(elevation), CastShadows(castShadows),
              Exposure(exposure), ToneMappingMode(toneMapping), Gamma(gamma) {}

        // Calculate unit vector pointing toward the sun light source
        glm::vec3 GetDirection() const {
            float radAz = glm::radians(Azimuth);
            float radEl = glm::radians(std::clamp(Elevation, 1.0f, 89.9f));
            return glm::normalize(glm::vec3(
                std::cos(radEl) * std::sin(radAz),
                std::sin(radEl),
                std::cos(radEl) * std::cos(radAz)
            ));
        }
    };

    struct PointLightComponent {
        glm::vec3 Color{ 1.0f, 1.0f, 1.0f };
        float Intensity = 1.0f;
        float Radius = 10.0f;
        float Falloff = 1.0f;

        PointLightComponent() = default;
        PointLightComponent(const glm::vec3& color, float intensity = 1.0f, float radius = 10.0f, float falloff = 1.0f)
            : Color(color), Intensity(intensity), Radius(radius), Falloff(falloff) {}
    };

    struct CameraComponent {
        float FOV = 45.0f;
        float NearClip = 0.1f;
        float FarClip = 1000.0f;
        bool Primary = false;

        CameraComponent() = default;
        CameraComponent(float fov, float nearClip = 0.1f, float farClip = 1000.0f, bool primary = false)
            : FOV(fov), NearClip(nearClip), FarClip(farClip), Primary(primary) {}
    };

    struct SkyboxComponent {

        bool Enabled = true;
        glm::vec3 ZenithColor{ 0.18f, 0.42f, 0.78f };    // Deep sky blue at zenith
        glm::vec3 HorizonColor{ 0.70f, 0.82f, 0.95f };   // Soft atmospheric haze at horizon
        glm::vec3 GroundColor{ 0.18f, 0.16f, 0.14f };    // Warm terrain ground floor
        float SunSize = 0.045f;                           // Angular disk radius
        float SunGlow = 0.16f;                            // Forward Mie scattering halo
        float Turbidity = 1.0f;                           // Atmospheric thickness / haze
        bool SyncWithSun = true;                          // Dynamically bind to primary directional sun
        float CustomAzimuth = 45.0f;                      // Custom sun heading if not synced
        float CustomElevation = 50.0f;                    // Custom sun altitude if not synced
        bool HemisphereAmbient = true;                    // Enable atmospheric hemisphere ambient irradiance

        SkyboxComponent() = default;

        void SetPresetNoon() {
            ZenithColor = glm::vec3(0.18f, 0.42f, 0.78f);
            HorizonColor = glm::vec3(0.70f, 0.82f, 0.95f);
            GroundColor = glm::vec3(0.18f, 0.16f, 0.14f);
            SunSize = 0.045f;
            SunGlow = 0.16f;
            Turbidity = 1.0f;
        }

        void SetPresetSunset() {
            ZenithColor = glm::vec3(0.12f, 0.18f, 0.42f);
            HorizonColor = glm::vec3(0.95f, 0.45f, 0.18f);
            GroundColor = glm::vec3(0.12f, 0.08f, 0.06f);
            SunSize = 0.060f;
            SunGlow = 0.35f;
            Turbidity = 1.5f;
        }

        void SetPresetNight() {
            ZenithColor = glm::vec3(0.012f, 0.020f, 0.055f);
            HorizonColor = glm::vec3(0.035f, 0.045f, 0.080f);
            GroundColor = glm::vec3(0.010f, 0.010f, 0.015f);
            SunSize = 0.030f;
            SunGlow = 0.08f;
            Turbidity = 0.8f;
        }

        void SetPresetOvercast() {
            ZenithColor = glm::vec3(0.60f, 0.65f, 0.70f);
            HorizonColor = glm::vec3(0.75f, 0.78f, 0.82f);
            GroundColor = glm::vec3(0.25f, 0.25f, 0.25f);
            SunSize = 0.050f;
            SunGlow = 0.50f;
            Turbidity = 2.2f;
        }

        void SetPresetAlienMars() {
            ZenithColor = glm::vec3(0.65f, 0.22f, 0.12f);
            HorizonColor = glm::vec3(0.85f, 0.45f, 0.25f);
            GroundColor = glm::vec3(0.35f, 0.10f, 0.05f);
            SunSize = 0.035f;
            SunGlow = 0.25f;
            Turbidity = 1.8f;
        }
    };

    struct AssemblyComponent {
        std::string AssetPath;
        u64 AssemblyUUID = 0;
        bool IsUnpacked = false;

        AssemblyComponent() = default;
        AssemblyComponent(const std::string& assetPath, u64 assemblyUUID = 0)
            : AssetPath(assetPath), AssemblyUUID(assemblyUUID) {}
    };

}

