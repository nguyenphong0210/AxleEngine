// Canonical opaque material surface template. Graphs supply AxleEvaluateMaterial.
cbuffer EngineObject : register(b0, space0) {
    column_major float4x4 u_Model;
};
cbuffer EngineScene : register(b1, space0) {
    column_major float4x4 u_ViewProjection;
    float4 u_CameraPosition;
    float4 u_SunDirection;
    float4 u_SunColor;
};

// @AXLE_GRAPH_DECLARATIONS@

struct MaterialSurface {
    float3 BaseColor;
    float Metallic;
    float Roughness;
    float3 Normal; // Tangent-space direction; default (0, 0, 1).
    float3 Emission;
    float AO;
    float Clearcoat;
    float ClearcoatRoughness;
    float Anisotropy;
    float AnisotropyRotation;
    float3 SheenColor;
    float SheenRoughness;
    float3 SubsurfaceColor;
    float SubsurfaceRadius;
    float3 AbsorptionColor;
    float IOR;
    float Dispersion;
    uint FeatureFlags;
};

// @AXLE_GRAPH_FUNCTION@

struct VSInput {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float2 TexCoord : TEXCOORD0;
};
struct PSInput {
    float4 Position : SV_Position;
    float3 WorldPos : POSITION0;
    float3 Normal : NORMAL0;
    float4 Tangent : TANGENT0;
    float2 TexCoord : TEXCOORD0;
};

float3 AxleSafeNormal(float3 value, float3 fallback) {
    return dot(value, value) > 1e-12 ? normalize(value) : fallback;
}

float3 AxleEvaluateWorldNormal(PSInput input, MaterialSurface surface) {
    float3 geometricNormal = AxleSafeNormal(input.Normal, float3(0,1,0));
    float3 fallbackTangent = abs(geometricNormal.z) < 0.99 ? cross(float3(0,0,1), geometricNormal) : cross(float3(0,1,0), geometricNormal);
    float3 tangent = AxleSafeNormal(input.Tangent.xyz - geometricNormal*dot(input.Tangent.xyz, geometricNormal), normalize(fallbackTangent));
    float3 bitangent = cross(geometricNormal, tangent)*(input.Tangent.w < 0.0 ? -1.0 : 1.0);
    return AxleSafeNormal(tangent*surface.Normal.x + bitangent*surface.Normal.y + geometricNormal*surface.Normal.z, geometricNormal);
}

PSInput VSMain(VSInput input) {
    PSInput output;
    float4 world = mul(u_Model, float4(input.Position, 1.0));
    output.Position = mul(u_ViewProjection, world);
    output.WorldPos = world.xyz;
    float3 a = mul((float3x3)u_Model, float3(1,0,0));
    float3 b = mul((float3x3)u_Model, float3(0,1,0));
    float3 c = mul((float3x3)u_Model, float3(0,0,1));
    float sign = dot(a, cross(b,c)) < 0 ? -1.0 : 1.0;
    float3 normal = (cross(b,c)*input.Normal.x + cross(c,a)*input.Normal.y + cross(a,b)*input.Normal.z)*sign;
    output.Normal = AxleSafeNormal(normal, float3(0,1,0));
    output.Tangent = float4(mul((float3x3)u_Model, input.Tangent.xyz), input.Tangent.w*sign);
    output.TexCoord = input.TexCoord;
    return output;
}

float4 PSMain(PSInput input) : SV_Target0 {
    MaterialSurface surface = AxleEvaluateMaterial(input.TexCoord);
    float3 base = max(surface.BaseColor, float3(0,0,0));
    float metallic = saturate(surface.Metallic);
    float roughness = clamp(surface.Roughness, 0.04, 1.0);
    // Keep the color path's geometric TBN construction stable. The normal
    // helper is shared with PSNormalMain, but must not feed back into the
    // tangent basis used by the clear-coat lobe.
    float3 geometricNormal = AxleSafeNormal(input.Normal, float3(0,1,0));
    float3 fallbackTangent = abs(geometricNormal.z) < 0.99 ? cross(float3(0,0,1), geometricNormal) : cross(float3(0,1,0), geometricNormal);
    float3 tangent = AxleSafeNormal(input.Tangent.xyz - geometricNormal*dot(input.Tangent.xyz, geometricNormal), normalize(fallbackTangent));
    float3 bitangent = cross(geometricNormal, tangent)*(input.Tangent.w < 0.0 ? -1.0 : 1.0);
    float3 normal = AxleSafeNormal(tangent*surface.Normal.x + bitangent*surface.Normal.y + geometricNormal*surface.Normal.z, geometricNormal);
    float3 view = AxleSafeNormal(u_CameraPosition.xyz - input.WorldPos, normal);
    float3 light = AxleSafeNormal(u_SunDirection.xyz, normal);
    float3 halfVector = AxleSafeNormal(view + light, normal);
    float nv = max(dot(normal, view), 0.001);
    float nl = saturate(dot(normal, light));
    float3 f0 = lerp(float3(0.04,0.04,0.04), base, metallic);
    float3 fresnel = FresnelSchlick(saturate(dot(halfVector, view)), f0);
    float3 specular = DistributionGGX(normal, halfVector, roughness)*GeometrySmith(normal, view, light, roughness)*fresnel / max(4.0*nv*nl, 0.0001);
    float3 diffuse = (1.0 - fresnel)*(1.0 - metallic)*base * INV_PI;

    // Direct sun lighting using unified physical calibration scale
    static const float DIRECT_LUX_TO_RADIANCE = 0.000035;
    float sunIntensity = max(u_SunDirection.w, 0.0);
    float sunRadianceScale = sunIntensity > 10.0 ? DIRECT_LUX_TO_RADIANCE : 1.0;
    float3 directLight = max(u_SunColor.rgb, float3(0,0,0)) * (sunIntensity * sunRadianceScale) * nl;
    float3 direct = (diffuse + specular) * directLight;

    // Clearcoat layer evaluation
    float coat = saturate(surface.Clearcoat);
    if (coat > 0.0) {
        float coatRoughness = clamp(surface.ClearcoatRoughness, 0.04, 1.0);
        float coatF = 0.04 + 0.96 * pow(1.0 - saturate(dot(view, halfVector)), 5.0);
        float coatBRDF = DistributionGGX(normal, halfVector, coatRoughness) * GeometrySmith(normal, view, light, coatRoughness) * coatF / max(4.0 * nv * max(nl, 0.001), 0.0001);
        direct = direct * (1.0 - coat * coatF) * (1.0 - coat * coatF) + coat * coatBRDF * directLight;
    }

    // Ambient / indirect term scaled by AO; direct and emissive are preserved
    float3 ambientRef = FresnelSchlickRoughness(nv, f0, roughness);
    float3 ambient = (base * 0.03 * (1.0 - metallic) + ambientRef * 0.02) * saturate(surface.AO);
    return float4(direct + ambient + max(surface.Emission, float3(0,0,0)), 1.0);
}

// Material Graphs use this companion entry point for the screen-space normal
// producer. It evaluates the same authored graph function and TBN path as the
// color pass, so GTAO/SSR never silently fall back to the default descriptor.
float4 PSNormalMain(PSInput input) : SV_Target0 {
    MaterialSurface surface = AxleEvaluateMaterial(input.TexCoord);
    return float4(AxleEvaluateWorldNormal(input, surface), 1.0);
}
