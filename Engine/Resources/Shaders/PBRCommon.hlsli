#ifndef AXLE_PBR_COMMON_INCLUDED
#define AXLE_PBR_COMMON_INCLUDED

static const float PI = 3.14159265358979323846;

// 1. Trowbridge-Reitz GGX Normal Distribution Function (NDF)
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = saturate(dot(N, H));
    float NdotH2 = NdotH * NdotH;
    float denom = (1.0 - NdotH2) + a2 * NdotH2;
    denom = PI * denom * denom;
    return a2 / max(denom, 1e-12);
}

// 2. Schlick-GGX Geometry Function
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// 3. Smith's Dual Shadowing-Masking Geometry Attenuation
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// 4. Fresnel-Schlick Approximation
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// 5. Fresnel-Schlick with Roughness for Ambient Specular Reflection
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float3 maxRef = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    return F0 + (maxRef - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

#endif
