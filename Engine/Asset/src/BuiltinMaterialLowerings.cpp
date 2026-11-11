#include <Axle/MaterialGraphHLSL.hpp>
#include <stdexcept>

namespace Axle {
namespace {
    bool UV(const MaterialHLSLNodeContext&, MaterialHLSLNodeEmission& output, MaterialGraphDiagnostic&) {
        output.Outputs["uv"] = "graphUV"; return true;
    }
    bool Parameter(const MaterialHLSLNodeContext& context, MaterialHLSLNodeEmission& output, MaterialGraphDiagnostic&) {
        if (!context.Definition.ParameterAttribute) return false;
        for (const auto& [name, value] : context.Node.Attributes) if (name == *context.Definition.ParameterAttribute) {
            output.Outputs["value"] = context.Parameters.at(std::get<std::string>(value)); return true;
        }
        return false;
    }
    bool Multiply(const MaterialHLSLNodeContext& context, MaterialHLSLNodeEmission& output, MaterialGraphDiagnostic&) {
        output.Outputs["result"] = '(' + context.Inputs.at("a") + " * " + context.Inputs.at("b") + ')'; return true;
    }
    bool Lerp(const MaterialHLSLNodeContext& context, MaterialHLSLNodeEmission& output, MaterialGraphDiagnostic&) {
        output.Outputs["result"] = "lerp(" + context.Inputs.at("a") + ", " + context.Inputs.at("b") + ", " + context.Inputs.at("alpha") + ')'; return true;
    }
    bool Texture(const MaterialHLSLNodeContext& context, MaterialHLSLNodeEmission& output, MaterialGraphDiagnostic&) {
        if (!context.TextureSymbol || !context.SamplerSymbol) return false;
        output.Statements = "    float4 " + context.Temporary + " = " + *context.TextureSymbol + ".Sample(" + *context.SamplerSymbol + ", " + context.Inputs.at("uv") + ");\n";
        output.Outputs = {{"rgba", context.Temporary}, {"rgb", context.Temporary + ".rgb"}, {"a", context.Temporary + ".a"}};
        return true;
    }
    bool Surface(const MaterialHLSLNodeContext& context, MaterialHLSLNodeEmission& output, MaterialGraphDiagnostic&) {
        output.Surface = context.Inputs; return true;
    }
    bool UnpackNormal(const MaterialHLSLNodeContext& context, MaterialHLSLNodeEmission& output, MaterialGraphDiagnostic&) {
        output.Statements = "    float3 " + context.Temporary + " = " + context.Inputs.at("rgb") + " * 2.0 - 1.0;\n";
        output.Outputs["normal"] = "(dot(" + context.Temporary + ", " + context.Temporary + ") > 1e-12 ? normalize(" + context.Temporary + ") : float3(0,0,1))";
        return true;
    }
}

    const MaterialHLSLRegistry& MaterialHLSLRegistry::Builtins() {
        static const MaterialHLSLRegistry registry = [] {
            MaterialHLSLRegistry result;
            auto add = [&](MaterialHLSLNodeLowering lowering) {
                MaterialGraphDiagnostic error;
                if (!result.Register(std::move(lowering), &error)) throw std::logic_error(error.Message);
            };
            add({"InputUV", 1, 1, UV, {}, {}, {}});
            add({"ParameterRef", 1, 1, Parameter, {}, {}, {}});
            add({"Multiply", 1, 1, Multiply, {}, {}, {}});
            add({"Lerp", 1, 1, Lerp, {}, {}, {}});
            add({"SampleTexture2D", 2, 2, Texture, "texturePath", "colorSpace", "sampler", "fallback"});
            add({"UnpackNormal", 1, 1, UnpackNormal, {}, {}, {}});
            add({"PBRMasterOutput", 1, 1, Surface, {}, {}, {}});
            return result;
        }();
        return registry;
    }
}
