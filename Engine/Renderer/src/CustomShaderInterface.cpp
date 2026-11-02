#include <Axle/CustomShaderInterface.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace Axle {

    namespace {
        std::string ToLower(std::string str) {
            std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return str;
        }
    }

    std::string CustomShaderInterface::GetCanonicalTemplate(const std::string& shaderName) {
        std::ostringstream oss;
        oss << "// =============================================================================\n"
            << "// " << shaderName << ".hlsl - AxleEngine Canonical Custom Material Shader\n"
            << "// Direct3D 12 (DXIL) & Vulkan 1.3 (SPIR-V) SM 6.0 Standard\n"
            << "// =============================================================================\n\n"
            << "// All three cbuffers are descriptor-bound in register space 0\n"
            << "cbuffer EngineObject : register(b0, space0) {\n"
            << "    column_major float4x4 u_Model;\n"
            << "    float4 u_BaseColor;\n"
            << "    float4 u_BaseUV; // scale.xy, offset.xy\n"
            << "};\n\n"
            << "cbuffer EngineScene : register(b1, space0) {\n"
            << "    column_major float4x4 u_ViewProjection;\n"
            << "    float4 u_CameraPosition;\n"
            << "};\n\n"
            << "cbuffer MaterialParameters : register(b2, space0) {\n"
            << "    float4 Tint;\n"
            << "    float  Gain;\n"
            << "    uint   Enabled;\n"
            << "    float2 Reserved;\n"
            << "};\n\n"
            << "// Textures & Samplers in register space 1 to prevent SPIR-V binding collision\n"
            << "Texture2D    u_BaseMap : register(t0, space1);\n"
            << "SamplerState u_Sampler : register(s0, space1);\n\n"
            << "struct VSInput {\n"
            << "    float3 Position : POSITION;\n"
            << "    float3 Normal   : NORMAL;\n"
            << "    float2 TexCoord : TEXCOORD0;\n"
            << "};\n\n"
            << "struct PSInput {\n"
            << "    float4 Position : SV_Position;\n"
            << "    float3 WorldPos : POSITION0;\n"
            << "    float3 Normal   : NORMAL0;\n"
            << "    float2 TexCoord : TEXCOORD0;\n"
            << "};\n\n"
            << "PSInput VSMain(VSInput input) {\n"
            << "    PSInput output;\n"
            << "    float4 worldPos = mul(u_Model, float4(input.Position, 1.0));\n"
            << "    output.Position = mul(u_ViewProjection, worldPos);\n"
            << "    output.WorldPos = worldPos.xyz;\n"
            << "    output.Normal   = normalize(mul((float3x3)u_Model, input.Normal));\n"
            << "    output.TexCoord = input.TexCoord * u_BaseUV.xy + u_BaseUV.zw;\n"
            << "    return output;\n"
            << "}\n\n"
            << "float4 PSMain(PSInput input) : SV_Target0 {\n"
            << "    float4 texColor = u_BaseMap.Sample(u_Sampler, input.TexCoord);\n"
            << "    float3 baseRgb = u_BaseColor.rgb * texColor.rgb;\n"
            << "    float3 tinted = Enabled ? (baseRgb * Tint.rgb * Gain) : baseRgb;\n"
            << "    return float4(tinted, u_BaseColor.a * texColor.a * (Enabled ? Tint.a : 1.0));\n"
            << "}\n";
        return oss.str();
    }

    std::string CustomShaderInterface::GetLegacyTemplate(const std::string& shaderName) {
        std::ostringstream oss;
        oss << "// =============================================================================\n"
            << "// " << shaderName << ".hlsl - AxleEngine Standard PBR Forward Shader (Legacy)\n"
            << "// =============================================================================\n\n"
            << "cbuffer ObjectConstants : register(b0, space0) {\n"
            << "    float4x4 u_Model;\n"
            << "    float4   u_BaseColor;\n"
            << "    float4   u_Params; // x: Roughness, y: Metallic, z: AO, w: Tiling\n"
            << "};\n\n"
            << "cbuffer SceneConstants : register(b1, space0) {\n"
            << "    float4x4 u_ViewProjection;\n"
            << "    float4   u_CameraPosition;\n"
            << "    float4   u_SunDirection;\n"
            << "    float4   u_SunColor;\n"
            << "};\n\n"
            << "Texture2D    u_BaseMap : register(t0, space0);\n"
            << "SamplerState u_Sampler : register(s0, space0);\n\n"
            << "struct VSInput {\n"
            << "    float3 Position : POSITION;\n"
            << "    float3 Normal   : NORMAL;\n"
            << "    float2 TexCoord : TEXCOORD0;\n"
            << "};\n\n"
            << "struct PSInput {\n"
            << "    float4 Position : SV_Position;\n"
            << "    float3 WorldPos : POSITION0;\n"
            << "    float3 Normal   : NORMAL0;\n"
            << "    float2 TexCoord : TEXCOORD0;\n"
            << "};\n\n"
            << "PSInput VSMain(VSInput input) {\n"
            << "    PSInput output;\n"
            << "    float4 worldPos = mul(u_Model, float4(input.Position, 1.0));\n"
            << "    output.Position = mul(u_ViewProjection, worldPos);\n"
            << "    output.WorldPos = worldPos.xyz;\n"
            << "    output.Normal   = normalize(mul((float3x3)u_Model, input.Normal));\n"
            << "    output.TexCoord = input.TexCoord * u_Params.w;\n"
            << "    return output;\n"
            << "}\n\n"
            << "float4 PSMain(PSInput input) : SV_Target0 {\n"
            << "    float4 texColor = u_BaseMap.Sample(u_Sampler, input.TexCoord);\n"
            << "    float3 albedo   = u_BaseColor.rgb * texColor.rgb;\n"
            << "    return float4(albedo, u_BaseColor.a * texColor.a);\n"
            << "}\n";
        return oss.str();
    }

    CustomInterfaceType CustomShaderInterface::DetectInterface(const RHI::RHIShaderReflection& reflection) {
        bool hasEngineObject = false;
        bool hasObjectConstants = false;

        for (const auto& cb : reflection.ConstantBuffers) {
            std::string nameLower = ToLower(cb.Name);
            if (nameLower == "engineobject" || nameLower == "materialparameters") {
                hasEngineObject = true;
            }
            if (nameLower == "objectconstants") {
                hasObjectConstants = true;
            }
        }

        if (hasEngineObject) return CustomInterfaceType::Canonical;
        if (hasObjectConstants) return CustomInterfaceType::LegacyTemplate;
        return CustomInterfaceType::Unknown;
    }

    bool CustomShaderInterface::HasVulkanBindingCollision(const RHI::RHIShaderReflection& reflection, std::string& outDetails) {
        outDetails.clear();
        for (const auto& cb : reflection.ConstantBuffers) {
            for (const auto& res : reflection.Resources) {
                if (res.Type == RHI::RHIResourceType::Texture || res.Type == RHI::RHIResourceType::Sampler) {
                    if (cb.Space == res.Space && cb.BindPoint == res.BindPoint) {
                        outDetails = "Collision in space " + std::to_string(cb.Space) + " at register " +
                                     std::to_string(cb.BindPoint) + " between cbuffer '" + cb.Name + "' and resource '" + res.Name + "'";
                        return true;
                    }
                }
            }
        }
        return false;
    }

    InterfaceValidationResult CustomShaderInterface::Validate(const RHI::RHIShaderReflection& reflection) {
        InterfaceValidationResult res;
        res.Type = DetectInterface(reflection);

        if (res.Type == CustomInterfaceType::Unknown) {
            res.IsValid = false;
            res.Diagnostic = "Unrecognized shader interface layout: neither canonical EngineObject nor legacy ObjectConstants constant buffer found.";
            return res;
        }

        if (res.Type == CustomInterfaceType::Canonical) {
            // Check for required EngineObject variables
            const auto* objCb = reflection.FindConstantBuffer("EngineObject");
            if (!objCb) {
                res.IsValid = false;
                res.Diagnostic = "Missing required cbuffer 'EngineObject'.";
                return res;
            }
            bool hasModel = false;
            for (const auto& v : objCb->Variables) {
                if (ToLower(v.Name) == "u_model") hasModel = true;
            }
            if (!hasModel) {
                res.IsValid = false;
                res.Diagnostic = "cbuffer 'EngineObject' is missing required uniform 'u_Model'.";
                return res;
            }
        } else if (res.Type == CustomInterfaceType::LegacyTemplate) {
            const auto* objCb = reflection.FindConstantBuffer("ObjectConstants");
            if (!objCb) {
                res.IsValid = false;
                res.Diagnostic = "Missing legacy cbuffer 'ObjectConstants'.";
                return res;
            }
        }

        res.IsValid = true;
        return res;
    }

} // namespace Axle
