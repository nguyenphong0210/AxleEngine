#include <Axle/ParticleEmitterSerializer.hpp>
#include <Axle/AssetDSL.hpp>
#include <fstream>
#include <sstream>
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
            default: return "AlphaBlend";
        }
    }

    static ParticleBlendMode StringToBlendMode(std::string_view str) {
        if (str == "Additive") return ParticleBlendMode::Additive;
        if (str == "AlphaBlend") return ParticleBlendMode::AlphaBlend;
        if (str == "Multiply") return ParticleBlendMode::Multiply;
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

    std::string ParticleEmitterSerializer::Serialize(const ParticleEmitterConfig& config, const std::string& texturePath) {
        AssetDocument doc;
        doc.Kind = AssetKind::ParticleEmitter;
        doc.Root = std::make_shared<AssetBlock>();
        doc.Root->Type = "ParticleEmitter";

        auto& root = *doc.Root;
        root.SetString("name", config.Name.empty() ? "DefaultEmitter" : config.Name);
        root.SetUInt("maxParticles", config.MaxParticles);
        root.SetFloat("emissionRate", config.EmissionRate);
        root.SetFloat("duration", config.Duration);
        root.SetBool("looping", config.Looping);
        root.SetBool("playOnAwake", config.PlayOnAwake);

        if (!config.MaterialPath.empty()) {
            root.SetString("material", config.MaterialPath);
        }

        if (config.BurstCount > 0) {
            root.SetUInt("burstCount", config.BurstCount);
            root.SetFloat("burstInterval", config.BurstInterval);
        }

        SerializeFloatRange(root, "lifetime", config.Lifetime);
        SerializeFloatRange(root, "speed", config.InitialSpeed);
        if (config.ConstantAcceleration != glm::vec3(0.0f)) {
            root.SetVec3("acceleration", config.ConstantAcceleration);
        }
        if (config.GravityModifier != 0.0f) {
            root.SetFloat("gravityModifier", config.GravityModifier);
        }
        root.SetFloat("drag", config.Drag);

        root.SetString("shape", ShapeTypeToString(config.Shape.Type));
        if (config.Shape.Type == ParticleEmitterShapeType::Cone) {
            root.SetFloat("coneAngle", config.Shape.ConeAngle);
            root.SetFloat("radius", config.Shape.Radius);
        } else if (config.Shape.Type == ParticleEmitterShapeType::Sphere || config.Shape.Type == ParticleEmitterShapeType::Hemisphere || config.Shape.Type == ParticleEmitterShapeType::Circle) {
            root.SetFloat("radius", config.Shape.Radius);
            root.SetBool("surfaceOnly", config.Shape.SurfaceOnly);
        } else if (config.Shape.Type == ParticleEmitterShapeType::Box) {
            root.SetVec3("boxExtents", config.Shape.BoxExtents);
            root.SetBool("surfaceOnly", config.Shape.SurfaceOnly);
        } else if (config.Shape.Type == ParticleEmitterShapeType::Line) {
            root.SetFloat("lineLength", config.Shape.LineLength);
        }
        if (config.Shape.Direction != glm::vec3(0.0f, 1.0f, 0.0f)) {
            root.SetVec3("direction", config.Shape.Direction);
        }

        SerializeFloatRange(root, "startSize", config.StartSize);
        SerializeFloatRange(root, "endSize", config.EndSize);
        SerializeFloatRange(root, "startRotation", config.StartRotation);
        SerializeFloatRange(root, "angularVelocity", config.AngularVelocity);

        // Curves (Size, Speed, Alpha over Lifetime)
        root.SetVec4("sizeCurveBezier", glm::vec4(config.SizeOverLifetime.P0.y, config.SizeOverLifetime.P1.y, config.SizeOverLifetime.P2.y, config.SizeOverLifetime.P3.y));
        root.SetInt("sizeCurveType", static_cast<int32_t>(config.SizeOverLifetime.Type));

        root.SetVec4("speedCurveBezier", glm::vec4(config.SpeedOverLifetime.P0.y, config.SpeedOverLifetime.P1.y, config.SpeedOverLifetime.P2.y, config.SpeedOverLifetime.P3.y));
        root.SetInt("speedCurveType", static_cast<int32_t>(config.SpeedOverLifetime.Type));

        root.SetVec4("alphaCurveBezier", glm::vec4(config.AlphaOverLifetime.P0.y, config.AlphaOverLifetime.P1.y, config.AlphaOverLifetime.P2.y, config.AlphaOverLifetime.P3.y));
        root.SetInt("alphaCurveType", static_cast<int32_t>(config.AlphaOverLifetime.Type));

        root.SetHexColor("startColor", config.ColorOverLifetime.StartColor);
        root.SetHexColor("endColor", config.ColorOverLifetime.EndColor);

        if (!texturePath.empty()) {
            root.SetString("texture", texturePath);
        }

        if (config.TextureRows > 1 || config.TextureCols > 1) {
            root.SetUInt("textureRows", config.TextureRows);
            root.SetUInt("textureCols", config.TextureCols);
            root.SetUInt("totalFrames", config.TotalFrames);
            root.SetFloat("animationCycles", config.AnimationCycles);
            root.SetBool("subFrameBlending", config.SubFrameBlending);
        }

        root.SetString("blendMode", BlendModeToString(config.BlendMode));
        root.SetString("simulationSpace", SimSpaceToString(config.SimulationSpace));
        root.SetString("orientation", OrientationToString(config.Orientation));

        if (config.LightingEnabled) {
            root.SetBool("lightingEnabled", true);
            root.SetFloat("wrapLighting", config.WrapLighting);
            root.SetFloat("scatteringFactor", config.ScatteringFactor);
            root.SetFloat("normalMapStrength", config.NormalMapStrength);
        }

        if (!config.NearFadeEnabled) {
            root.SetBool("nearFadeEnabled", false);
        } else {
            root.SetFloat("nearFadeStart", config.NearFadeStart);
            root.SetFloat("nearFadeEnd", config.NearFadeEnd);
        }

        if (config.FarFadeEnabled) {
            root.SetBool("farFadeEnabled", true);
            root.SetFloat("farFadeStart", config.FarFadeStart);
            root.SetFloat("farFadeEnd", config.FarFadeEnd);
        }
        root.SetFloat("softnessDistance", config.SoftnessDistance);

        if (config.CurlNoise.Enabled) {
            root.SetBool("curlNoiseEnabled", true);
            root.SetFloat("curlNoiseFrequency", config.CurlNoise.Frequency);
            root.SetFloat("curlNoiseStrength", config.CurlNoise.Strength);
            root.SetUInt("curlNoiseOctaves", config.CurlNoise.Octaves);
        }

        if (config.Wind != glm::vec3(0.0f)) {
            root.SetVec3("wind", config.Wind);
            root.SetFloat("windInfluence", config.WindInfluence);
        }
        if (config.Buoyancy != 0.0f) {
            root.SetFloat("buoyancy", config.Buoyancy);
        }

        if (config.Collision.Enabled) {
            root.SetBool("collisionEnabled", true);
            root.SetFloat("collisionRestitution", config.Collision.Restitution);
            root.SetFloat("collisionFriction", config.Collision.Friction);
            root.SetBool("killOnCollision", config.Collision.KillOnCollision);
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
            root.Children.push_back(bBlock);
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
            root.Children.push_back(sBlock);
        }

        // Color & Alpha gradient multi-keys
        for (const auto& ck : config.ColorOverLifetime.ColorKeys) {
            auto ckBlock = std::make_shared<AssetBlock>();
            ckBlock->Type = "ColorKey";
            ckBlock->SetFloat("time", ck.Time);
            ckBlock->SetVec3("color", ck.Color);
            root.Children.push_back(ckBlock);
        }
        for (const auto& ak : config.ColorOverLifetime.AlphaKeys) {
            auto akBlock = std::make_shared<AssetBlock>();
            akBlock->Type = "AlphaKey";
            akBlock->SetFloat("time", ak.Time);
            akBlock->SetFloat("alpha", ak.Alpha);
            root.Children.push_back(akBlock);
        }

        return AssetDSL::SerializeDocument(doc);
    }

    bool ParticleEmitterSerializer::Deserialize(std::string_view dslText, ParticleEmitterConfig& outConfig, std::string& outTexturePath) {
        AssetDocument doc;
        std::string err;
        if (!AssetDSL::ParseSource(dslText, doc, &err) || !doc.IsValid()) {
            return false;
        }

        if (doc.Kind != AssetKind::ParticleEmitter && doc.Root->Type != "ParticleEmitter") {
            return false;
        }

        const auto& root = *doc.Root;
        outConfig.Name = root.GetString("name", outConfig.Name);
        outConfig.MaxParticles = root.GetUInt("maxParticles", outConfig.MaxParticles);
        outConfig.EmissionRate = root.GetFloat("emissionRate", outConfig.EmissionRate);
        outConfig.Duration = root.GetFloat("duration", outConfig.Duration);
        outConfig.Looping = root.GetBool("looping", outConfig.Looping);
        outConfig.PlayOnAwake = root.GetBool("playOnAwake", outConfig.PlayOnAwake);
        outConfig.MaterialPath = root.GetString("material", outConfig.MaterialPath);

        outConfig.BurstCount = root.GetUInt("burstCount", outConfig.BurstCount);
        outConfig.BurstInterval = root.GetFloat("burstInterval", outConfig.BurstInterval);

        // Kinematics
        if (root.HasProperty("lifetime")) {
            outConfig.Lifetime = ParseFloatRange(root, "lifetime", outConfig.Lifetime);
        }
        if (root.HasProperty("speed")) {
            outConfig.InitialSpeed = ParseFloatRange(root, "speed", outConfig.InitialSpeed);
        } else if (root.HasProperty("initialSpeed")) {
            outConfig.InitialSpeed = ParseFloatRange(root, "initialSpeed", outConfig.InitialSpeed);
        }

        outConfig.ConstantAcceleration = root.GetVec3("acceleration", outConfig.ConstantAcceleration);
        outConfig.GravityModifier = root.GetFloat("gravityModifier", outConfig.GravityModifier);
        outConfig.Drag = root.GetFloat("drag", outConfig.Drag);

        // Shape
        if (root.HasProperty("shape")) {
            outConfig.Shape.Type = StringToShapeType(root.GetString("shape"));
        }
        outConfig.Shape.Radius = root.GetFloat("radius", outConfig.Shape.Radius);
        outConfig.Shape.ConeAngle = root.GetFloat("coneAngle", outConfig.Shape.ConeAngle);
        outConfig.Shape.BoxExtents = root.GetVec3("boxExtents", outConfig.Shape.BoxExtents);
        outConfig.Shape.LineLength = root.GetFloat("lineLength", outConfig.Shape.LineLength);
        outConfig.Shape.SurfaceOnly = root.GetBool("surfaceOnly", outConfig.Shape.SurfaceOnly);
        outConfig.Shape.Direction = root.GetVec3("direction", outConfig.Shape.Direction);

        // Curves / Size / Rotation
        if (root.HasProperty("startSize")) {
            outConfig.StartSize = ParseFloatRange(root, "startSize", outConfig.StartSize);
        }
        if (root.HasProperty("endSize")) {
            outConfig.EndSize = ParseFloatRange(root, "endSize", outConfig.EndSize);
        }
        if (root.HasProperty("startRotation")) {
            outConfig.StartRotation = ParseFloatRange(root, "startRotation", outConfig.StartRotation);
        }
        if (root.HasProperty("angularVelocity")) {
            outConfig.AngularVelocity = ParseFloatRange(root, "angularVelocity", outConfig.AngularVelocity);
        }

        // Cubic Bezier Curves
        if (root.HasProperty("sizeCurveBezier")) {
            glm::vec4 b = root.GetVec4("sizeCurveBezier", glm::vec4(1.0f, 1.0f, 0.5f, 0.0f));
            outConfig.SizeOverLifetime.SetCubicBezier(b.x, b.y, b.z, b.w);
            if (root.HasProperty("sizeCurveType")) {
                outConfig.SizeOverLifetime.Type = static_cast<CurveType>(root.GetInt("sizeCurveType", 0));
            }
        }
        if (root.HasProperty("speedCurveBezier")) {
            glm::vec4 b = root.GetVec4("speedCurveBezier", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            outConfig.SpeedOverLifetime.SetCubicBezier(b.x, b.y, b.z, b.w);
            if (root.HasProperty("speedCurveType")) {
                outConfig.SpeedOverLifetime.Type = static_cast<CurveType>(root.GetInt("speedCurveType", 0));
            }
        }
        if (root.HasProperty("alphaCurveBezier")) {
            glm::vec4 b = root.GetVec4("alphaCurveBezier", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            outConfig.AlphaOverLifetime.SetCubicBezier(b.x, b.y, b.z, b.w);
            if (root.HasProperty("alphaCurveType")) {
                outConfig.AlphaOverLifetime.Type = static_cast<CurveType>(root.GetInt("alphaCurveType", 0));
            }
        }

        // Color & Alpha
        if (root.HasProperty("startColor")) {
            outConfig.ColorOverLifetime.StartColor = root.GetVec4("startColor", outConfig.ColorOverLifetime.StartColor);
        }
        if (root.HasProperty("endColor")) {
            outConfig.ColorOverLifetime.EndColor = root.GetVec4("endColor", outConfig.ColorOverLifetime.EndColor);
        }

        // Texture
        if (root.HasProperty("texture")) {
            outTexturePath = root.GetString("texture");
        }
        outConfig.TextureRows = root.GetUInt("textureRows", outConfig.TextureRows);
        outConfig.TextureCols = root.GetUInt("textureCols", outConfig.TextureCols);
        outConfig.TotalFrames = root.GetUInt("totalFrames", outConfig.TotalFrames);
        outConfig.AnimationCycles = root.GetFloat("animationCycles", outConfig.AnimationCycles);
        outConfig.SubFrameBlending = root.GetBool("subFrameBlending", outConfig.SubFrameBlending);

        // BlendMode, Space, Orientation
        if (root.HasProperty("blendMode")) {
            outConfig.BlendMode = StringToBlendMode(root.GetString("blendMode"));
        }
        if (root.HasProperty("simulationSpace")) {
            outConfig.SimulationSpace = StringToSimSpace(root.GetString("simulationSpace"));
        }
        if (root.HasProperty("orientation")) {
            outConfig.Orientation = StringToOrientation(root.GetString("orientation"));
        }

        // Volumetric Lighting & Shading
        outConfig.LightingEnabled = root.GetBool("lightingEnabled", outConfig.LightingEnabled);
        outConfig.WrapLighting = root.GetFloat("wrapLighting", outConfig.WrapLighting);
        outConfig.ScatteringFactor = root.GetFloat("scatteringFactor", outConfig.ScatteringFactor);
        outConfig.NormalMapStrength = root.GetFloat("normalMapStrength", outConfig.NormalMapStrength);

        // Near / Far Fade
        outConfig.NearFadeEnabled = root.GetBool("nearFadeEnabled", outConfig.NearFadeEnabled);
        outConfig.NearFadeStart = root.GetFloat("nearFadeStart", outConfig.NearFadeStart);
        outConfig.NearFadeEnd = root.GetFloat("nearFadeEnd", outConfig.NearFadeEnd);
        outConfig.FarFadeEnabled = root.GetBool("farFadeEnabled", outConfig.FarFadeEnabled);
        outConfig.FarFadeStart = root.GetFloat("farFadeStart", outConfig.FarFadeStart);
        outConfig.FarFadeEnd = root.GetFloat("farFadeEnd", outConfig.FarFadeEnd);
        outConfig.SoftnessDistance = root.GetFloat("softnessDistance", outConfig.SoftnessDistance);

        // Curl Noise
        outConfig.CurlNoise.Enabled = root.GetBool("curlNoiseEnabled", outConfig.CurlNoise.Enabled);
        outConfig.CurlNoise.Frequency = root.GetFloat("curlNoiseFrequency", outConfig.CurlNoise.Frequency);
        outConfig.CurlNoise.Strength = root.GetFloat("curlNoiseStrength", outConfig.CurlNoise.Strength);
        outConfig.CurlNoise.Octaves = root.GetUInt("curlNoiseOctaves", outConfig.CurlNoise.Octaves);

        // Force fields
        outConfig.Wind = root.GetVec3("wind", outConfig.Wind);
        outConfig.WindInfluence = root.GetFloat("windInfluence", outConfig.WindInfluence);
        outConfig.Buoyancy = root.GetFloat("buoyancy", outConfig.Buoyancy);

        // Collision
        outConfig.Collision.Enabled = root.GetBool("collisionEnabled", outConfig.Collision.Enabled);
        outConfig.Collision.Restitution = root.GetFloat("collisionRestitution", outConfig.Collision.Restitution);
        outConfig.Collision.Friction = root.GetFloat("collisionFriction", outConfig.Collision.Friction);
        outConfig.Collision.KillOnCollision = root.GetBool("killOnCollision", outConfig.Collision.KillOnCollision);

        // Child blocks parsing
        for (const auto& child : root.Children) {
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
            }
        }

        return true;
    }

    bool ParticleEmitterSerializer::SerializeToFile(const std::string& filePath, const ParticleEmitterConfig& config, const std::string& texturePath) {
        std::string dsl = Serialize(config, texturePath);
        std::ofstream out(filePath);
        if (!out.is_open()) return false;
        out << dsl;
        return true;
    }

    bool ParticleEmitterSerializer::DeserializeFromFile(const std::string& filePath, ParticleEmitterConfig& outConfig, std::string& outTexturePath) {
        std::ifstream in(filePath);
        if (!in.is_open()) return false;
        std::stringstream buffer;
        buffer << in.rdbuf();
        return Deserialize(buffer.str(), outConfig, outTexturePath);
    }

}
