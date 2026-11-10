#include <Axle/MaterialGraph.hpp>
#include "MaterialGraphInternal.hpp"
#include <stdexcept>

namespace Axle {
namespace {
    using Type = MaterialValueType;
    MaterialNodePinDefinition Input(std::string name, Type type, float value) {
        return {std::move(name), type, MaterialValue{type, std::vector<float>(MaterialGraphInternal::Width(type), value)}};
    }

    MaterialNodePropertyDefinition StringProperty(std::string name, std::vector<std::string> choices = {}) {
        MaterialNodePropertyDefinition property;
        property.Name = std::move(name); property.AllowedStrings = std::move(choices);
        return property;
    }

    void Add(MaterialNodeRegistry& registry, MaterialNodeDefinition definition) {
        MaterialGraphDiagnostic error;
        if (!registry.Register(std::move(definition), &error)) throw std::logic_error(error.Message);
    }

    void RegisterInputs(MaterialNodeRegistry& registry) {
        MaterialNodeDefinition uv; uv.Type = "InputUV";
        uv.Signatures = {{{}, {{"uv", Type::Float2, {}}}}};
        Add(registry, std::move(uv));

        MaterialNodeDefinition parameter; parameter.Type = "ParameterRef";
        parameter.ParameterAttribute = "parameter";
        parameter.Attributes = {StringProperty("parameter")};
        parameter.Signatures = {{{}, {{"value", std::nullopt, {}}}}};
        Add(registry, std::move(parameter));
    }

    void RegisterArithmetic(MaterialNodeRegistry& registry) {
        const std::vector<Type> types{Type::Float, Type::Float2, Type::Float3, Type::Float4};
        MaterialNodeDefinition multiply; multiply.Type = "Multiply";
        for (auto a : types) for (auto b : types) {
            if (a != b && a != Type::Float && b != Type::Float) continue;
            const auto result = MaterialGraphInternal::Width(a) >= MaterialGraphInternal::Width(b) ? a : b;
            multiply.Signatures.push_back({{Input("a", a, 0), Input("b", b, 1)}, {{"result", result, {}}}});
        }
        Add(registry, std::move(multiply));

        MaterialNodeDefinition lerp; lerp.Type = "Lerp";
        for (auto type : types)
            lerp.Signatures.push_back({{Input("a", type, 0), Input("b", type, 1), Input("alpha", Type::Float, 0)}, {{"result", type, {}}}});
        Add(registry, std::move(lerp));
    }

    void RegisterTexture(MaterialNodeRegistry& registry) {
        MaterialNodeDefinition texture; texture.Type = "SampleTexture2D";
        auto path = StringProperty("texturePath"); path.AssetPath = true; path.DiagnosticCode = "MG_RESOURCE_PATH";
        auto space = StringProperty("colorSpace", {"sRGB", "Linear"}); space.DiagnosticCode = "MG_RESOURCE";
        auto sampler = StringProperty("sampler", {"LinearRepeat", "LinearClamp", "PointRepeat", "PointClamp"}); sampler.DiagnosticCode = "MG_RESOURCE";
        texture.Properties = {std::move(path), std::move(space), std::move(sampler)};
        texture.Signatures = {{{Input("uv", Type::Float2, 0)}, {{"rgba", Type::Float4, {}}, {"rgb", Type::Float3, {}}, {"a", Type::Float, {}}}}};
        Add(registry, std::move(texture));
    }

    void RegisterSurface(MaterialNodeRegistry& registry) {
        MaterialNodeDefinition surface; surface.Type = "PBRMasterOutput"; surface.Role = MaterialNodeRole::SurfaceOutput;
        auto normal = Input("Normal", Type::Float3, 0); normal.Default->Components = {0,0,1};
        surface.Signatures = {{{Input("BaseColor", Type::Float3, 1), Input("Metallic", Type::Float, 0), Input("Roughness", Type::Float, 0.5f),
                               std::move(normal), Input("Emission", Type::Float3, 0), Input("AO", Type::Float, 1)}, {}}};
        Add(registry, std::move(surface));
    }
}

    const MaterialNodeRegistry& MaterialNodeRegistry::Builtins() {
        static const MaterialNodeRegistry registry = [] {
            MaterialNodeRegistry result;
            RegisterInputs(result); RegisterArithmetic(result); RegisterTexture(result); RegisterSurface(result);
            return result;
        }();
        return registry;
    }
}
