// =============================================================================
// Axle Engine - AtmosphericSkybox.hlsl
// Procedural Rayleigh & Mie Atmospheric Scattering Skybox (HLSL 2021)
// Direct3D 12 (DXIL) & Vulkan 1.3 (SPIR-V) Unified Shader Codebase
// =============================================================================

struct VSOutput {
    float4 Position : SV_Position;
    float3 ViewRay  : TEXCOORD0;
};

// -----------------------------------------------------------------------------
// Uniform Constant Buffer (b0, space 0) - Exactly 256 bytes (CBV aligned)
// -----------------------------------------------------------------------------
cbuffer SkyboxConstants : register(b0, space0) {
    float4x4 u_InvViewProjection;   // 64 bytes (offset 0)
    float4   u_CameraPosition;      // 16 bytes (xyz = world cam pos, w = 1.0) (offset 64)
    float4   u_SunDirection;        // 16 bytes (xyz = normalized dir, w = solar elevation) (offset 80)
    float4   u_SunColor;            // 16 bytes (rgb = sun color * intensity, w = day factor) (offset 96)
    float4   u_ZenithColor;         // 16 bytes (rgb = zenith color, w = sunset factor) (offset 112)
    float4   u_HorizonColor;        // 16 bytes (rgb = horizon color, w = turbidity) (offset 128)
    float4   u_GroundColor;         // 16 bytes (rgb = ground floor color, w = exposure) (offset 144)
    float4   u_Params;              // 16 bytes (x = SunSize, y = SunGlow, z = ToneMappingMode, w = Gamma) (offset 160)
    float4   u_Reserved[5];         // 80 bytes padding -> Total 256 bytes (CBV aligned) (offset 176..256)
};

// =============================================================================
// VERTEX SHADER ENTRY POINT - Full-Screen Triangle with Inverse Unproject
// =============================================================================
VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Generate full-screen triangle in NDC covering [-1, 1] x [-1, 1]
    // vertexID 0: (-1, 1), vertexID 1: (3, 1), vertexID 2: (-1, -3)
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);

    // Far plane depth (z = 1.0)
    output.Position = float4(ndc, 1.0, 1.0);

    // Unproject clip coordinates to world-space far plane position
    float4 worldPosH = mul(u_InvViewProjection, float4(ndc, 1.0, 1.0));
    float3 worldPos = worldPosH.xyz / worldPosH.w;

    // View ray from camera position to far plane
    output.ViewRay = worldPos - u_CameraPosition.xyz;

    return output;
}

// =============================================================================
// TONE MAPPING & GAMMA FUNCTIONS
// =============================================================================
float3 ToneMapACES(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 ToneMapReinhard(float3 x) {
    return x / (x + 1.0);
}

float3 Uncharted2Tonemap(float3 x) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 ToneMapUncharted2(float3 x) {
    const float W = 11.2;
    float3 curr = Uncharted2Tonemap(x * 2.0);
    float3 whiteScale = 1.0 / Uncharted2Tonemap(W.xxx);
    return saturate(curr * whiteScale);
}

// Pseudo-random 2D hash for procedural night stars
float Hash21(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

// =============================================================================
// PIXEL SHADER ENTRY POINT - Analytical Rayleigh & Mie Scattering
// =============================================================================
float4 PSMain(VSOutput input) : SV_Target {
    float3 dir = normalize(input.ViewRay);
    float3 sunDir = normalize(u_SunDirection.xyz);

    float sunElev = u_SunDirection.w;
    float dayFactor = u_SunColor.w;
    float sunsetFactor = u_ZenithColor.w;
    float turbidity = u_HorizonColor.w;
    float exposure = u_GroundColor.w;

    // Dynamic Sunset Horizon & Night Zenith Colors
    float3 sunsetHorizon = float3(1.0, 0.45, 0.15);
    float3 dynamicHorizon = lerp(u_HorizonColor.rgb, sunsetHorizon, sunsetFactor * 0.85);

    float3 nightZenith = float3(0.015, 0.025, 0.06);
    float3 nightHorizon = float3(0.04, 0.05, 0.08);
    float3 dynamicZenith = lerp(nightZenith, u_ZenithColor.rgb, dayFactor);
    dynamicHorizon = lerp(nightHorizon, dynamicHorizon, dayFactor);

    // 1. Rayleigh Atmospheric Scattering Gradient
    float3 skyColor;
    if (dir.y >= 0.0) {
        // Upper hemisphere (Horizon -> Zenith)
        float t = pow(dir.y, clamp(0.55 * turbidity, 0.2, 2.0));
        skyColor = lerp(dynamicHorizon, dynamicZenith, t);
    } else {
        // Lower hemisphere (Horizon -> Ground floor)
        float t = pow(-dir.y, 0.5);
        skyColor = lerp(dynamicHorizon, u_GroundColor.rgb * (0.3 + 0.7 * dayFactor), t);
    }

    // 2. Solar Scattering & Sun Disk
    float cosTheta = dot(dir, sunDir);
    if (cosTheta > 0.0) {
        // Forward Mie scattering corona halo
        float sunGlow = u_Params.y;
        float glowExp = max(16.0 / max(sunGlow, 0.01), 1.0);
        float glow = pow(cosTheta, glowExp) * dayFactor;
        float3 haloColor = lerp(u_SunColor.rgb, sunsetHorizon, sunsetFactor * 0.5);
        skyColor += haloColor * glow * 1.8;

        // Sharp analytical sun disk with soft anti-aliased limb
        float sunSize = u_Params.x;
        float sunAngle = acos(clamp(cosTheta, -1.0, 1.0));
        float radius = max(sunSize, 0.005);
        float disk = 1.0 - smoothstep(radius * 0.90, radius * 1.08, sunAngle);
        disk *= step(0.0, sunElev + radius); // Hide sun when fully below horizon
        skyColor += u_SunColor.rgb * disk * 12.0 * dayFactor;
    }

    // 3. Subtle Night Sky Stars
    if (dir.y > 0.05 && dayFactor < 0.6) {
        float2 starCoord = float2(atan2(dir.z, dir.x) * 150.0, dir.y * 300.0);
        float2 id = floor(starCoord);
        float starRand = Hash21(id);
        if (starRand > 0.993) {
            float starBrightness = (starRand - 0.993) / 0.007;
            float nightFade = (1.0 - dayFactor) * smoothstep(0.05, 0.2, dir.y);
            skyColor += (starBrightness * 1.5).xxx * nightFade;
        }
    }

    // 4. Photometric Exposure, Tone Mapping & Display Gamma EOTF
    float expVal = (exposure > 0.0) ? exposure : 1.0;
    float3 exposed = skyColor * expVal;

    int toneMapMode = (int)u_Params.z;
    float3 mappedColor;
    if (toneMapMode == 0) {
        mappedColor = ToneMapACES(exposed);
    } else if (toneMapMode == 1) {
        mappedColor = ToneMapReinhard(exposed);
    } else if (toneMapMode == 2) {
        mappedColor = ToneMapUncharted2(exposed);
    } else {
        mappedColor = saturate(exposed);
    }

    float gamma = (u_Params.w > 0.0) ? u_Params.w : 2.2;
    float3 srgbColor = pow(max(mappedColor, 0.0.xxx), 1.0 / gamma);

    return float4(srgbColor, 1.0);
}
