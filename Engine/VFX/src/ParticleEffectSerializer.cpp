#include <Axle/ParticleEffectSerializer.hpp>
#include <Axle/AssetDSL.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <cmath>

namespace Axle {

    static std::string ShapeTypeToString(ParticleEmitterShapeType shape) {
        switch (shape) {
            case ParticleEmitterShapeType::Point: return "Point";
            case ParticleEmitterShapeType::Sphere: return "Sphere";
            case ParticleEmitterShapeType::Hemisphere: return "Hemisphere";
            case ParticleEmitterShapeType::Box: return "Box";
            case ParticleEmitterShapeType::Cone: return "Cone";
            case ParticleEmitterShapeType::Line: return "Line";
            case ParticleEmitterShapeType::Circle: return "Circle";
            case ParticleEmitterShapeType::MeshSurface: return "MeshSurface";
            default: return "Point";
        }
    }

    static ParticleEmitterShapeType StringToShapeType(std::string_view str) {
        if (str == "Point") return ParticleEmitterShapeType::Point;
        if (str == "Sphere") return ParticleEmitterShapeType::Sphere;
        if (str == "Hemisphere") return ParticleEmitterShapeType::Hemisphere;
        if (str == "Box") return ParticleEmitterShapeType::Box;
        if (str == "Cone") return ParticleEmitterShapeType::Cone;
        if (str == "Line") return ParticleEmitterShapeType::Line;
        if (str == "Circle") return ParticleEmitterShapeType::Circle;
        if (str == "MeshSurface") return ParticleEmitterShapeType::MeshSurface;
        return ParticleEmitterShapeType::Point;
    }

    static std::string BlendModeToString(ParticleBlendMode mode) {
        switch (mode) {
            case ParticleBlendMode::Additive: return "Additive";
            case ParticleBlendMode::AlphaBlend: return "AlphaBlend";
            case ParticleBlendMode::Multiply: return "Multiply";
            case ParticleBlendMode::Cutout: return "Cutout";
            default: return "AlphaBlend";
        }
    }

    static ParticleBlendMode StringToBlendMode(std::string_view str) {
        if (str == "Additive") return ParticleBlendMode::Additive;
        if (str == "AlphaBlend") return ParticleBlendMode::AlphaBlend;
        if (str == "Multiply") return ParticleBlendMode::Multiply;
        if (str == "Cutout") return ParticleBlendMode::Cutout;
        return ParticleBlendMode::AlphaBlend;
    }

    static std::string SimSpaceToString(ParticleSimulationSpace space) {
        return space == ParticleSimulationSpace::Local ? "Local" : "World";
    }

    static ParticleSimulationSpace StringToSimSpace(std::string_view str) {
        return str == "Local" ? ParticleSimulationSpace::Local : ParticleSimulationSpace::World;
    }

    static std::string OrientationToString(ParticleOrientation orient) {
        switch (orient) {
            case ParticleOrientation::CameraFacing: return "CameraFacing";
            case ParticleOrientation::AxialY: return "AxialY";
            case ParticleOrientation::VelocityAligned: return "VelocityAligned";
            default: return "CameraFacing";
        }
    }

    static ParticleOrientation StringToOrientation(std::string_view str) {
        if (str == "CameraFacing") return ParticleOrientation::CameraFacing;
        if (str == "AxialY") return ParticleOrientation::AxialY;
        if (str == "VelocityAligned") return ParticleOrientation::VelocityAligned;
        return ParticleOrientation::CameraFacing;
    }

    static FloatRange ParseFloatRange(const AssetBlock& block, const std::string& key, FloatRange defaultVal) {
        if (!block.HasProperty(key)) return defaultVal;
        auto prop = block.GetProperty(key);
        if (prop->RawValue.find('[') != std::string::npos) {
            glm::vec2 v = prop->AsVec2(glm::vec2(defaultVal.Min, defaultVal.Max));
            return FloatRange(v.x, v.y);
        }
        float val = prop->AsFloat(defaultVal.Min);
        return FloatRange(val, val);
    }

    static void SerializeFloatRange(AssetBlock& block, const std::string& key, const FloatRange& range) {
        if (std::abs(range.Min - range.Max) < 0.0001f) {
            block.SetFloat(key, range.Min);
        } else {
            block.SetVec2(key, glm::vec2(range.Min, range.Max));
        }
    }

    void ParticleEffectSerializer::WriteEmitterConfig(AssetBlock& block, const ParticleEmitterConfig& config) {
        block.SetString("name", config.Name.empty() ? "Emitter" : config.Name);
        block.SetUInt("maxParticles", config.MaxParticles);
        block.SetFloat("emissionRate", config.EmissionRate);
        block.SetFloat("duration", config.Duration);
        block.SetBool("looping", config.Looping);
        block.SetBool("playOnAwake", config.PlayOnAwake);

        if (!config.MaterialPath.empty()) {
            block.SetString("material", config.MaterialPath);
        }

        if (config.BurstCount > 0) {
            block.SetUInt("burstCount", config.BurstCount);
            block.SetFloat("burstInterval", config.BurstInterval);
        }

        SerializeFloatRange(block, "lifetime", config.Lifetime);
        SerializeFloatRange(block, "speed", config.InitialSpeed);
        if (config.ConstantAcceleration != glm::vec3(0.0f)) {
            block.SetVec3("acceleration", config.ConstantAcceleration);
        }
        if (config.GravityModifier != 0.0f) {
            block.SetFloat("gravityModifier", config.GravityModifier);
        }
        block.SetFloat("drag", config.Drag);

        block.SetString("shape", ShapeTypeToString(config.Shape.Type));
        if (config.Shape.Type == ParticleEmitterShapeType::Cone) {
            block.SetFloat("coneAngle", config.Shape.ConeAngle);
            block.SetFloat("radius", config.Shape.Radius);
        } else if (config.Shape.Type == ParticleEmitterShapeType::Sphere || config.Shape.Type == ParticleEmitterShapeType::Hemisphere || config.Shape.Type == ParticleEmitterShapeType::Circle) {
            block.SetFloat("radius", config.Shape.Radius);
            block.SetBool("surfaceOnly", config.Shape.SurfaceOnly);
        } else if (config.Shape.Type == ParticleEmitterShapeType::Box) {
            block.SetVec3("boxExtents", config.Shape.BoxExtents);
            block.SetBool("surfaceOnly", config.Shape.SurfaceOnly);
        } else if (config.Shape.Type == ParticleEmitterShapeType::Line) {
            block.SetFloat("lineLength", config.Shape.LineLength);
        } else if (config.Shape.Type == ParticleEmitterShapeType::MeshSurface) {
            block.SetString("meshPath", config.Shape.MeshPath);
        }
        if (config.Shape.Direction != glm::vec3(0.0f, 1.0f, 0.0f)) {
            block.SetVec3("direction", config.Shape.Direction);
        }

        SerializeFloatRange(block, "startSize", config.StartSize);
        SerializeFloatRange(block, "endSize", config.EndSize);
        SerializeFloatRange(block, "startRotation", config.StartRotation);
        SerializeFloatRange(block, "angularVelocity", config.AngularVelocity);

        // Curves (Size, Speed, Alpha)
        block.SetVec4("sizeCurveBezier", glm::vec4(config.SizeOverLifetime.P0.y, config.SizeOverLifetime.P1.y, config.SizeOverLifetime.P2.y, config.SizeOverLifetime.P3.y));
        block.SetInt("sizeCurveType", static_cast<int32_t>(config.SizeOverLifetime.Type));

        block.SetVec4("speedCurveBezier", glm::vec4(config.SpeedOverLifetime.P0.y, config.SpeedOverLifetime.P1.y, config.SpeedOverLifetime.P2.y, config.SpeedOverLifetime.P3.y));
        block.SetInt("speedCurveType", static_cast<int32_t>(config.SpeedOverLifetime.Type));

        block.SetVec4("alphaCurveBezier", glm::vec4(config.AlphaOverLifetime.P0.y, config.AlphaOverLifetime.P1.y, config.AlphaOverLifetime.P2.y, config.AlphaOverLifetime.P3.y));
        block.SetInt("alphaCurveType", static_cast<int32_t>(config.AlphaOverLifetime.Type));

        block.SetVec4("startColor", config.ColorOverLifetime.StartColor);
        block.SetVec4("endColor", config.ColorOverLifetime.EndColor);

        if (config.TextureRows > 1 || config.TextureCols > 1) {
            block.SetUInt("textureRows", config.TextureRows);
            block.SetUInt("textureCols", config.TextureCols);
            block.SetUInt("totalFrames", config.TotalFrames);
            block.SetFloat("animationCycles", config.AnimationCycles);
            block.SetBool("subFrameBlending", config.SubFrameBlending);
        }

        block.SetString("blendMode", BlendModeToString(config.BlendMode));
        block.SetString("simulationSpace", SimSpaceToString(config.SimulationSpace));
        block.SetString("orientation", OrientationToString(config.Orientation));

        if (config.LightingEnabled) {
            block.SetBool("lightingEnabled", true);
            block.SetFloat("wrapLighting", config.WrapLighting);
            block.SetFloat("scatteringFactor", config.ScatteringFactor);
            block.SetFloat("normalMapStrength", config.NormalMapStrength);
        }

        if (!config.NearFadeEnabled) {
            block.SetBool("nearFadeEnabled", false);
        } else {
            block.SetFloat("nearFadeStart", config.NearFadeStart);
            block.SetFloat("nearFadeEnd", config.NearFadeEnd);
        }

        if (config.FarFadeEnabled) {
            block.SetBool("farFadeEnabled", true);
            block.SetFloat("farFadeStart", config.FarFadeStart);
            block.SetFloat("farFadeEnd", config.FarFadeEnd);
        }
        block.SetFloat("softnessDistance", config.SoftnessDistance);

        if (config.CurlNoise.Enabled) {
            block.SetBool("curlNoiseEnabled", true);
            block.SetFloat("curlNoiseFrequency", config.CurlNoise.Frequency);
            block.SetFloat("curlNoiseStrength", config.CurlNoise.Strength);
            block.SetUInt("curlNoiseOctaves", config.CurlNoise.Octaves);
        }

        if (config.Wind != glm::vec3(0.0f)) {
            block.SetVec3("wind", config.Wind);
            block.SetFloat("windInfluence", config.WindInfluence);
        }
        if (config.Buoyancy != 0.0f) {
            block.SetFloat("buoyancy", config.Buoyancy);
        }

        if (config.Collision.Enabled) {
            block.SetBool("collisionEnabled", true);
            block.SetInt("collisionMode", static_cast<int32_t>(config.Collision.CollisionMode));
            block.SetFloat("collisionRestitution", config.Collision.Restitution);
            block.SetFloat("collisionFriction", config.Collision.Friction);
            block.SetBool("killOnCollision", config.Collision.KillOnCollision);
            block.SetFloat("minBounceSpeed", config.Collision.MinBounceSpeed);
            block.SetFloat("particleRadius", config.Collision.ParticleRadius);
            block.SetFloat("floorHeight", config.Collision.FloorHeight);
            block.SetBool("floorKill", config.Collision.FloorKill);

            for (const auto& plane : config.Collision.Planes) {
                auto pBlock = std::make_shared<AssetBlock>();
                pBlock->Type = "CollisionPlane";
                pBlock->SetVec3("normal", plane.Normal);
                pBlock->SetFloat("distance", plane.Distance);
                pBlock->SetFloat("restitution", plane.Restitution);
                pBlock->SetFloat("friction", plane.Friction);
                pBlock->SetBool("kill", plane.KillOnCollision);
                block.Children.push_back(pBlock);
            }

            for (const auto& box : config.Collision.Boxes) {
                auto bBlock = std::make_shared<AssetBlock>();
                bBlock->Type = "CollisionBox";
                bBlock->SetVec3("min", box.Min);
                bBlock->SetVec3("max", box.Max);
                bBlock->SetBool("inside", box.CollideInside);
                bBlock->SetFloat("restitution", box.Restitution);
                bBlock->SetFloat("friction", box.Friction);
                bBlock->SetBool("kill", box.KillOnCollision);
                block.Children.push_back(bBlock);
            }
        }

        // Bursts child blocks
        for (const auto& burst : config.Bursts) {
            auto bBlock = std::make_shared<AssetBlock>();
            bBlock->Type = "Burst";
            bBlock->SetFloat("time", burst.Time);
            bBlock->SetUInt("minCount", burst.MinCount);
            bBlock->SetUInt("maxCount", burst.MaxCount);
            bBlock->SetUInt("cycles", burst.Cycles);
            bBlock->SetFloat("interval", burst.Interval);
            bBlock->SetFloat("probability", burst.Probability);
            block.Children.push_back(bBlock);
        }

        // Sub-Emitters child blocks
        for (const auto& sub : config.SubEmitters) {
            auto sBlock = std::make_shared<AssetBlock>();
            sBlock->Type = "SubEmitter";
            sBlock->SetString("trigger", sub.Trigger == SubEmitterTrigger::Birth ? "Birth" : (sub.Trigger == SubEmitterTrigger::Death ? "Death" : "Collision"));
            sBlock->SetString("target", sub.TargetEmitterName);
            sBlock->SetUInt("emitCount", sub.EmitCount);
            sBlock->SetFloat("probability", sub.Probability);
            sBlock->SetBool("inheritColor", sub.InheritColor);
            sBlock->SetBool("inheritVelocity", sub.InheritVelocity);
            block.Children.push_back(sBlock);
        }

        // Color & Alpha gradient keys
        for (const auto& ck : config.ColorOverLifetime.ColorKeys) {
            auto ckBlock = std::make_shared<AssetBlock>();
            ckBlock->Type = "ColorKey";
            ckBlock->SetFloat("time", ck.Time);
            ckBlock->SetVec3("color", ck.Color);
            block.Children.push_back(ckBlock);
        }
        for (const auto& ak : config.ColorOverLifetime.AlphaKeys) {
            auto akBlock = std::make_shared<AssetBlock>();
            akBlock->Type = "AlphaKey";
            akBlock->SetFloat("time", ak.Time);
            akBlock->SetFloat("alpha", ak.Alpha);
            block.Children.push_back(akBlock);
        }
    }

    bool ParticleEffectSerializer::ReadEmitterConfig(const AssetBlock& block,
                                                     ParticleEmitterConfig& outConfig,
                                                     std::vector<VFXDiagnostic>* outDiagnostics) {
        outConfig.Name = block.GetString("name", outConfig.Name);
        outConfig.MaxParticles = block.GetUInt("maxParticles", outConfig.MaxParticles);
        outConfig.EmissionRate = block.GetFloat("emissionRate", outConfig.EmissionRate);
        outConfig.Duration = block.GetFloat("duration", outConfig.Duration);
        outConfig.Looping = block.GetBool("looping", outConfig.Looping);
        outConfig.PlayOnAwake = block.GetBool("playOnAwake", outConfig.PlayOnAwake);
        outConfig.MaterialPath = block.GetString("material", outConfig.MaterialPath);

        outConfig.BurstCount = block.GetUInt("burstCount", outConfig.BurstCount);
        outConfig.BurstInterval = block.GetFloat("burstInterval", outConfig.BurstInterval);

        if (block.HasProperty("lifetime")) {
            outConfig.Lifetime = ParseFloatRange(block, "lifetime", outConfig.Lifetime);
        }
        if (block.HasProperty("speed")) {
            outConfig.InitialSpeed = ParseFloatRange(block, "speed", outConfig.InitialSpeed);
        } else if (block.HasProperty("initialSpeed")) {
            outConfig.InitialSpeed = ParseFloatRange(block, "initialSpeed", outConfig.InitialSpeed);
        }

        outConfig.ConstantAcceleration = block.GetVec3("acceleration", outConfig.ConstantAcceleration);
        outConfig.GravityModifier = block.GetFloat("gravityModifier", outConfig.GravityModifier);
        outConfig.Drag = block.GetFloat("drag", outConfig.Drag);

        if (block.HasProperty("shape")) {
            outConfig.Shape.Type = StringToShapeType(block.GetString("shape"));
        }
        outConfig.Shape.Radius = block.GetFloat("radius", outConfig.Shape.Radius);
        outConfig.Shape.ConeAngle = block.GetFloat("coneAngle", outConfig.Shape.ConeAngle);
        outConfig.Shape.BoxExtents = block.GetVec3("boxExtents", outConfig.Shape.BoxExtents);
        outConfig.Shape.LineLength = block.GetFloat("lineLength", outConfig.Shape.LineLength);
        outConfig.Shape.SurfaceOnly = block.GetBool("surfaceOnly", outConfig.Shape.SurfaceOnly);
        outConfig.Shape.Direction = block.GetVec3("direction", outConfig.Shape.Direction);
        if (block.HasProperty("meshPath")) {
            outConfig.Shape.MeshPath = block.GetString("meshPath");
        }

        if (block.HasProperty("startSize")) {
            outConfig.StartSize = ParseFloatRange(block, "startSize", outConfig.StartSize);
        }
        if (block.HasProperty("endSize")) {
            outConfig.EndSize = ParseFloatRange(block, "endSize", outConfig.EndSize);
        }
        if (block.HasProperty("startRotation")) {
            outConfig.StartRotation = ParseFloatRange(block, "startRotation", outConfig.StartRotation);
        }
        if (block.HasProperty("angularVelocity")) {
            outConfig.AngularVelocity = ParseFloatRange(block, "angularVelocity", outConfig.AngularVelocity);
        }

        if (block.HasProperty("sizeCurveBezier")) {
            glm::vec4 b = block.GetVec4("sizeCurveBezier", glm::vec4(1.0f, 1.0f, 0.5f, 0.0f));
            outConfig.SizeOverLifetime.SetCubicBezier(b.x, b.y, b.z, b.w);
            if (block.HasProperty("sizeCurveType")) {
                outConfig.SizeOverLifetime.Type = static_cast<CurveType>(block.GetInt("sizeCurveType", 0));
            }
        }
        if (block.HasProperty("speedCurveBezier")) {
            glm::vec4 b = block.GetVec4("speedCurveBezier", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            outConfig.SpeedOverLifetime.SetCubicBezier(b.x, b.y, b.z, b.w);
            if (block.HasProperty("speedCurveType")) {
                outConfig.SpeedOverLifetime.Type = static_cast<CurveType>(block.GetInt("speedCurveType", 0));
            }
        }
        if (block.HasProperty("alphaCurveBezier")) {
            glm::vec4 b = block.GetVec4("alphaCurveBezier", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            outConfig.AlphaOverLifetime.SetCubicBezier(b.x, b.y, b.z, b.w);
            if (block.HasProperty("alphaCurveType")) {
                outConfig.AlphaOverLifetime.Type = static_cast<CurveType>(block.GetInt("alphaCurveType", 0));
            }
        }

        if (block.HasProperty("startColor")) {
            outConfig.ColorOverLifetime.StartColor = block.GetVec4("startColor", outConfig.ColorOverLifetime.StartColor);
        }
        if (block.HasProperty("endColor")) {
            outConfig.ColorOverLifetime.EndColor = block.GetVec4("endColor", outConfig.ColorOverLifetime.EndColor);
        }

        outConfig.TextureRows = block.GetUInt("textureRows", outConfig.TextureRows);
        outConfig.TextureCols = block.GetUInt("textureCols", outConfig.TextureCols);
        outConfig.TotalFrames = block.GetUInt("totalFrames", outConfig.TotalFrames);
        outConfig.AnimationCycles = block.GetFloat("animationCycles", outConfig.AnimationCycles);
        outConfig.SubFrameBlending = block.GetBool("subFrameBlending", outConfig.SubFrameBlending);

        if (block.HasProperty("blendMode")) {
            outConfig.BlendMode = StringToBlendMode(block.GetString("blendMode"));
        }
        if (block.HasProperty("simulationSpace")) {
            outConfig.SimulationSpace = StringToSimSpace(block.GetString("simulationSpace"));
        }
        if (block.HasProperty("orientation")) {
            outConfig.Orientation = StringToOrientation(block.GetString("orientation"));
        }

        outConfig.LightingEnabled = block.GetBool("lightingEnabled", outConfig.LightingEnabled);
        outConfig.WrapLighting = block.GetFloat("wrapLighting", outConfig.WrapLighting);
        outConfig.ScatteringFactor = block.GetFloat("scatteringFactor", outConfig.ScatteringFactor);
        outConfig.NormalMapStrength = block.GetFloat("normalMapStrength", outConfig.NormalMapStrength);

        outConfig.NearFadeEnabled = block.GetBool("nearFadeEnabled", outConfig.NearFadeEnabled);
        outConfig.NearFadeStart = block.GetFloat("nearFadeStart", outConfig.NearFadeStart);
        outConfig.NearFadeEnd = block.GetFloat("nearFadeEnd", outConfig.NearFadeEnd);
        outConfig.FarFadeEnabled = block.GetBool("farFadeEnabled", outConfig.FarFadeEnabled);
        outConfig.FarFadeStart = block.GetFloat("farFadeStart", outConfig.FarFadeStart);
        outConfig.FarFadeEnd = block.GetFloat("farFadeEnd", outConfig.FarFadeEnd);
        outConfig.SoftnessDistance = block.GetFloat("softnessDistance", outConfig.SoftnessDistance);

        outConfig.CurlNoise.Enabled = block.GetBool("curlNoiseEnabled", outConfig.CurlNoise.Enabled);
        outConfig.CurlNoise.Frequency = block.GetFloat("curlNoiseFrequency", outConfig.CurlNoise.Frequency);
        outConfig.CurlNoise.Strength = block.GetFloat("curlNoiseStrength", outConfig.CurlNoise.Strength);
        outConfig.CurlNoise.Octaves = block.GetUInt("curlNoiseOctaves", outConfig.CurlNoise.Octaves);

        outConfig.Wind = block.GetVec3("wind", outConfig.Wind);
        outConfig.WindInfluence = block.GetFloat("windInfluence", outConfig.WindInfluence);
        outConfig.Buoyancy = block.GetFloat("buoyancy", outConfig.Buoyancy);

        outConfig.Collision.Enabled = block.GetBool("collisionEnabled", outConfig.Collision.Enabled);
        outConfig.Collision.Restitution = block.GetFloat("collisionRestitution", outConfig.Collision.Restitution);
        outConfig.Collision.Friction = block.GetFloat("collisionFriction", outConfig.Collision.Friction);
        outConfig.Collision.KillOnCollision = block.GetBool("killOnCollision", outConfig.Collision.KillOnCollision);
        if (block.HasProperty("collisionMode")) {
            outConfig.Collision.CollisionMode = static_cast<ParticleCollisionMode>(block.GetInt("collisionMode"));
        }
        outConfig.Collision.MinBounceSpeed = block.GetFloat("minBounceSpeed", outConfig.Collision.MinBounceSpeed);
        outConfig.Collision.ParticleRadius = block.GetFloat("particleRadius", outConfig.Collision.ParticleRadius);
        outConfig.Collision.FloorHeight = block.GetFloat("floorHeight", outConfig.Collision.FloorHeight);
        outConfig.Collision.FloorKill = block.GetBool("floorKill", outConfig.Collision.FloorKill);
        outConfig.Collision.Planes.clear();
        outConfig.Collision.Boxes.clear();

        // Clear child collections before appending
        outConfig.Bursts.clear();
        outConfig.SubEmitters.clear();
        outConfig.ColorOverLifetime.ColorKeys.clear();
        outConfig.ColorOverLifetime.AlphaKeys.clear();

        for (const auto& child : block.Children) {
            if (child->Type == "Burst") {
                ParticleBurst b;
                b.Time = child->GetFloat("time", 0.0f);
                b.MinCount = child->GetUInt("minCount", 30);
                b.MaxCount = child->GetUInt("maxCount", 30);
                b.Cycles = child->GetUInt("cycles", 1);
                b.Interval = child->GetFloat("interval", 0.0f);
                b.Probability = child->GetFloat("probability", 1.0f);
                b.Reset();
                outConfig.Bursts.push_back(b);
            } else if (child->Type == "SubEmitter") {
                SubEmitterConfig sub;
                std::string trigStr = child->GetString("trigger", "Collision");
                if (trigStr == "Birth") sub.Trigger = SubEmitterTrigger::Birth;
                else if (trigStr == "Death") sub.Trigger = SubEmitterTrigger::Death;
                else sub.Trigger = SubEmitterTrigger::Collision;
                sub.TargetEmitterName = child->GetString("target", "");
                sub.EmitCount = child->GetUInt("emitCount", 10);
                sub.Probability = child->GetFloat("probability", 1.0f);
                sub.InheritColor = child->GetBool("inheritColor", true);
                sub.InheritVelocity = child->GetBool("inheritVelocity", false);
                outConfig.SubEmitters.push_back(sub);
            } else if (child->Type == "ColorKey") {
                GradientColorKey ck;
                ck.Time = child->GetFloat("time", 0.0f);
                ck.Color = child->GetVec3("color", glm::vec3(1.0f));
                outConfig.ColorOverLifetime.ColorKeys.push_back(ck);
            } else if (child->Type == "AlphaKey") {
                GradientAlphaKey ak;
                ak.Time = child->GetFloat("time", 0.0f);
                ak.Alpha = child->GetFloat("alpha", 1.0f);
                outConfig.ColorOverLifetime.AlphaKeys.push_back(ak);
            } else if (child->Type == "CollisionPlane") {
                ParticleCollisionPlane plane;
                plane.Normal = child->GetVec3("normal", plane.Normal);
                plane.Distance = child->GetFloat("distance", plane.Distance);
                plane.Restitution = child->GetFloat("restitution", plane.Restitution);
                plane.Friction = child->GetFloat("friction", plane.Friction);
                plane.KillOnCollision = child->GetBool("kill", plane.KillOnCollision);
                outConfig.Collision.Planes.push_back(plane);
            } else if (child->Type == "CollisionBox") {
                ParticleCollisionBox box;
                box.Min = child->GetVec3("min", box.Min);
                box.Max = child->GetVec3("max", box.Max);
                box.CollideInside = child->GetBool("inside", box.CollideInside);
                box.Restitution = child->GetFloat("restitution", box.Restitution);
                box.Friction = child->GetFloat("friction", box.Friction);
                box.KillOnCollision = child->GetBool("kill", box.KillOnCollision);
                outConfig.Collision.Boxes.push_back(box);
            }
        }

        return true;
    }

    void ParticleEffectSerializer::WriteEmitterBlock(AssetBlock& parentBlock, const EmitterDefinition& emitter) {
        auto eBlock = std::make_shared<AssetBlock>();
        eBlock->Type = "Emitter";
        eBlock->SetString("id", emitter.Id);
        eBlock->SetString("name", emitter.Name);
        eBlock->SetBool("enabled", emitter.Enabled);
        eBlock->SetBool("triggerOnly", emitter.TriggerOnly);

        eBlock->SetVec3("translation", emitter.LocalTransform.Translation);
        eBlock->SetVec4("rotation", glm::vec4(emitter.LocalTransform.Rotation.w,
                                              emitter.LocalTransform.Rotation.x,
                                              emitter.LocalTransform.Rotation.y,
                                              emitter.LocalTransform.Rotation.z));
        eBlock->SetVec3("scale", emitter.LocalTransform.Scale);

        if (!emitter.Resources.TexturePath.empty()) {
            eBlock->SetString("texture", emitter.Resources.TexturePath);
        }
        eBlock->SetBool("useTexture", emitter.Resources.UseTexture);

        if (!emitter.Resources.NormalMapPath.empty()) {
            eBlock->SetString("normalMap", emitter.Resources.NormalMapPath);
        }
        eBlock->SetBool("useNormalMap", emitter.Resources.UseNormalMap);

        if (!emitter.Resources.MaterialPath.empty()) {
            eBlock->SetString("material", emitter.Resources.MaterialPath);
        }

        auto cfgBlock = std::make_shared<AssetBlock>();
        cfgBlock->Type = "Config";
        WriteEmitterConfig(*cfgBlock, emitter.Config);
        eBlock->Children.push_back(cfgBlock);

        parentBlock.Children.push_back(eBlock);
    }

    bool ParticleEffectSerializer::ReadEmitterBlock(const AssetBlock& emitterBlock,
                                                    EmitterDefinition& outEmitter,
                                                    std::vector<VFXDiagnostic>* outDiagnostics) {
        outEmitter.Id = emitterBlock.GetString("id", "");
        if (outEmitter.Id.empty()) {
            if (outDiagnostics) {
                outDiagnostics->push_back({ VFXDiagnosticSeverity::Error, "MISSING_ID", "Emitter.id", "Emitter missing required non-empty ID" });
            }
            return false;
        }

        outEmitter.Name = emitterBlock.GetString("name", "Emitter");
        outEmitter.Enabled = emitterBlock.GetBool("enabled", true);
        outEmitter.TriggerOnly = emitterBlock.GetBool("triggerOnly", false);

        outEmitter.LocalTransform.Translation = emitterBlock.GetVec3("translation", glm::vec3(0.0f));
        glm::vec4 rot = emitterBlock.GetVec4("rotation", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        outEmitter.LocalTransform.Rotation = glm::quat(rot.x, rot.y, rot.z, rot.w);
        outEmitter.LocalTransform.Scale = emitterBlock.GetVec3("scale", glm::vec3(1.0f));

        outEmitter.Resources.TexturePath = emitterBlock.GetString("texture", "");
        outEmitter.Resources.UseTexture = emitterBlock.GetBool("useTexture", !outEmitter.Resources.TexturePath.empty());
        outEmitter.Resources.NormalMapPath = emitterBlock.GetString("normalMap", "");
        outEmitter.Resources.UseNormalMap = emitterBlock.GetBool("useNormalMap", !outEmitter.Resources.NormalMapPath.empty());
        outEmitter.Resources.MaterialPath = emitterBlock.GetString("material", "");

        for (const auto& child : emitterBlock.Children) {
            if (child->Type == "Config") {
                return ReadEmitterConfig(*child, outEmitter.Config, outDiagnostics);
            }
        }

        // If no Config child block, fallback to reading properties directly from emitterBlock
        return ReadEmitterConfig(emitterBlock, outEmitter.Config, outDiagnostics);
    }

    std::string ParticleEffectSerializer::Serialize(const ParticleEffectDefinition& effect) {
        AssetDocument doc;
        doc.Kind = AssetKind::ParticleEmitter;
        doc.Root = std::make_shared<AssetBlock>();
        doc.Root->Type = "ParticleEmitter";

        doc.Root->SetUInt("schemaVersion", effect.SchemaVersion);
        doc.Root->SetString("name", effect.Name.empty() ? "ParticleEffect" : effect.Name);
        doc.Root->SetUInt("seed", effect.Seed);

        for (const auto& emitter : effect.Emitters) {
            WriteEmitterBlock(*doc.Root, emitter);
        }

        return AssetDSL::SerializeDocument(doc);
    }

    bool ParticleEffectSerializer::Deserialize(std::string_view dslText,
                                               ParticleEffectDefinition& outEffect,
                                               std::vector<VFXDiagnostic>* outDiagnostics) {
        AssetDocument doc;
        std::string err;
        if (!AssetDSL::ParseSource(dslText, doc, &err) || !doc.IsValid()) {
            if (outDiagnostics) {
                outDiagnostics->push_back({ VFXDiagnosticSeverity::Error, "PARSE_FAILED", "", err.empty() ? "Invalid AssetDSL document" : err });
            }
            return false;
        }

        if (doc.Kind != AssetKind::ParticleEmitter && doc.Root->Type != "ParticleEmitter") {
            if (outDiagnostics) {
                outDiagnostics->push_back({ VFXDiagnosticSeverity::Error, "INVALID_ROOT", "", "Root block must be ParticleEmitter" });
            }
            return false;
        }

        ParticleEffectDefinition candidate;
        const auto& root = *doc.Root;

        if (root.HasProperty("schemaVersion")) {
            uint32_t version = root.GetUInt("schemaVersion", 1);
            if (version > 2) {
                if (outDiagnostics) {
                    outDiagnostics->push_back({ VFXDiagnosticSeverity::Error, "UNSUPPORTED_VERSION", "schemaVersion", "Schema version " + std::to_string(version) + " is not supported" });
                }
                return false;
            }
            candidate.SchemaVersion = version;
        } else {
            candidate.SchemaVersion = 1;
        }

        candidate.Name = root.GetString("name", "UnnamedEffect");
        candidate.Seed = root.GetUInt("seed", 1);

        if (candidate.SchemaVersion >= 2) {
            for (const auto& child : root.Children) {
                if (child->Type == "Emitter") {
                    EmitterDefinition emitter;
                    if (!ReadEmitterBlock(*child, emitter, outDiagnostics)) {
                        return false;
                    }
                    candidate.Emitters.push_back(std::move(emitter));
                }
            }
        } else {
            // Legacy v1 single-emitter migration
            candidate.SchemaVersion = 2; // Migrate to schema 2 in memory
            EmitterDefinition legacyEmitter;
            legacyEmitter.Id = "legacy-0001";
            legacyEmitter.Name = candidate.Name;
            legacyEmitter.Enabled = true;
            legacyEmitter.TriggerOnly = false;

            if (!ReadEmitterConfig(root, legacyEmitter.Config, outDiagnostics)) {
                return false;
            }

            if (root.HasProperty("texture")) {
                legacyEmitter.Resources.TexturePath = root.GetString("texture");
                legacyEmitter.Resources.UseTexture = !legacyEmitter.Resources.TexturePath.empty();
            }
            if (root.HasProperty("material")) {
                legacyEmitter.Resources.MaterialPath = root.GetString("material");
            }
            candidate.Emitters.push_back(std::move(legacyEmitter));
        }

        // Validate complete candidate before replacing outEffect
        std::vector<std::string> valErrors;
        if (!ValidateParticleEffectDefinition(candidate, valErrors)) {
            if (outDiagnostics) {
                for (const auto& vErr : valErrors) {
                    outDiagnostics->push_back({ VFXDiagnosticSeverity::Error, "VALIDATION_ERROR", "", vErr });
                }
            }
            return false;
        }

        outEffect = std::move(candidate);
        return true;
    }

    bool ParticleEffectSerializer::SerializeToFile(const std::string& filePath,
                                                   const ParticleEffectDefinition& effect,
                                                   std::vector<VFXDiagnostic>* outDiagnostics) {
        std::vector<std::string> valErrors;
        if (!ValidateParticleEffectDefinition(effect, valErrors)) {
            if (outDiagnostics) {
                for (const auto& vErr : valErrors) {
                    outDiagnostics->push_back({ VFXDiagnosticSeverity::Error, "VALIDATION_FAILED", "", vErr });
                }
            }
            return false;
        }

        std::string dsl = Serialize(effect);
        std::filesystem::path target(filePath);
        if (target.has_parent_path()) {
            std::filesystem::create_directories(target.parent_path());
        }

        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path tempPath = target.string() + ".tmp." + std::to_string(now);

        {
            std::ofstream out(tempPath, std::ios::binary);
            if (!out.is_open()) {
                if (outDiagnostics) {
                    outDiagnostics->push_back({ VFXDiagnosticSeverity::Error, "IO_OPEN_FAILED", "", "Failed to open sibling temp file for write: " + tempPath.string() });
                }
                return false;
            }
            out << dsl;
            out.flush();
            if (out.fail()) {
                out.close();
                std::filesystem::remove(tempPath);
                if (outDiagnostics) {
                    outDiagnostics->push_back({ VFXDiagnosticSeverity::Error, "IO_WRITE_FAILED", "", "Failed to write temp file: " + tempPath.string() });
                }
                return false;
            }
            out.close();
        }

        std::error_code ec;
        std::filesystem::copy_file(tempPath, target, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tempPath);

        if (ec) {
            if (outDiagnostics) {
                outDiagnostics->push_back({ VFXDiagnosticSeverity::Error, "IO_REPLACE_FAILED", "", "Failed to atomic-replace target file: " + ec.message() });
            }
            return false;
        }

        return true;
    }

    bool ParticleEffectSerializer::DeserializeFromFile(const std::string& filePath,
                                                       ParticleEffectDefinition& outEffect,
                                                       std::vector<VFXDiagnostic>* outDiagnostics) {
        std::ifstream in(filePath, std::ios::binary);
        if (!in.is_open()) {
            if (outDiagnostics) {
                outDiagnostics->push_back({ VFXDiagnosticSeverity::Error, "FILE_NOT_FOUND", "", "Cannot open file: " + filePath });
            }
            return false;
        }

        std::stringstream ss;
        ss << in.rdbuf();
        return Deserialize(ss.str(), outEffect, outDiagnostics);
    }

} // namespace Axle
