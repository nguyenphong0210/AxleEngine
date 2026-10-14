#version 450 core
layout(location = 0) out vec4 o_Color;

in vec3 v_TexCoord;

// 1. Atmosphere & Sun Direction Uniforms
uniform vec3 u_SunDirection;
uniform vec3 u_SunColor;
uniform vec3 u_ZenithColor;
uniform vec3 u_HorizonColor;
uniform vec3 u_GroundColor;
uniform float u_SunSize;
uniform float u_SunGlow;
uniform float u_Turbidity;

// 2. Camera Exposure & Post-Processing (Phase 11: Sub-step 3.2)
uniform float u_Exposure;
uniform int u_ToneMappingMode; // 0: ACES, 1: Reinhard, 2: Uncharted 2, 3: Linear Bypass
uniform float u_Gamma;

// ACES Filmic Tone Mapping Curve (Narkowicz fit)
vec3 ToneMapACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Reinhard Tone Mapping Curve
vec3 ToneMapReinhard(vec3 x) {
    return x / (x + vec3(1.0));
}

// Uncharted 2 Filmic Curve (John Hable)
vec3 Uncharted2Tonemap(vec3 x) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 ToneMapUncharted2(vec3 x) {
    const float W = 11.2;
    vec3 curr = Uncharted2Tonemap(x * 2.0);
    vec3 whiteScale = 1.0 / Uncharted2Tonemap(vec3(W));
    return clamp(curr * whiteScale, 0.0, 1.0);
}

// Pseudo-random 2D hash for procedural night stars
float Hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec3 dir = normalize(v_TexCoord);
    vec3 sunDir = normalize(u_SunDirection);

    // 1. Solar elevation and day/night transitions
    float sunElev = sunDir.y;
    float sunsetFactor = clamp(1.0 - abs(sunElev) * 2.5, 0.0, 1.0);
    float dayFactor = clamp(sunElev * 3.0 + 0.5, 0.0, 1.0);

    // Sunset horizon tinting (golden amber / crimson glow)
    vec3 sunsetHorizon = vec3(1.0, 0.45, 0.15);
    vec3 dynamicHorizon = mix(u_HorizonColor, sunsetHorizon, sunsetFactor * 0.85);

    // Night sky darkening
    vec3 nightZenith = vec3(0.015, 0.025, 0.06);
    vec3 nightHorizon = vec3(0.04, 0.05, 0.08);
    vec3 dynamicZenith = mix(nightZenith, u_ZenithColor, dayFactor);
    dynamicHorizon = mix(nightHorizon, dynamicHorizon, dayFactor);

    // 2. Rayleigh Atmospheric Gradient
    vec3 skyColor;
    if (dir.y >= 0.0) {
        // Upper atmosphere hemisphere (Horizon to Zenith)
        float t = pow(dir.y, clamp(0.55 * u_Turbidity, 0.2, 2.0));
        skyColor = mix(dynamicHorizon, dynamicZenith, t);
    } else {
        // Lower ground hemisphere (Horizon to Earth floor)
        float t = pow(-dir.y, 0.5);
        skyColor = mix(dynamicHorizon, u_GroundColor * (0.3 + 0.7 * dayFactor), t);
    }

    // 3. Solar Scattering & Sun Disk
    float cosTheta = dot(dir, sunDir);
    if (cosTheta > 0.0) {
        // Forward Mie scattering halo (Corona glow)
        float glowExp = max(16.0 / max(u_SunGlow, 0.01), 1.0);
        float glow = pow(cosTheta, glowExp) * dayFactor;
        vec3 haloColor = mix(u_SunColor, sunsetHorizon, sunsetFactor * 0.5);
        skyColor += haloColor * glow * 1.8;

        // Sharp analytical sun disk with soft anti-aliased limb
        float sunAngle = acos(clamp(cosTheta, -1.0, 1.0));
        float radius = max(u_SunSize, 0.005);
        float disk = 1.0 - smoothstep(radius * 0.90, radius * 1.08, sunAngle);
        disk *= step(0.0, sunElev + radius); // Hide sun when fully below horizon
        skyColor += u_SunColor * disk * 12.0 * dayFactor;
    }

    // 4. Subtle Night Sky Stars
    if (dir.y > 0.05 && dayFactor < 0.6) {
        vec2 starCoord = vec2(atan(dir.z, dir.x) * 150.0, dir.y * 300.0);
        vec2 id = floor(starCoord);
        float starRand = Hash21(id);
        if (starRand > 0.993) {
            float starBrightness = (starRand - 0.993) / 0.007;
            float nightFade = (1.0 - dayFactor) * smoothstep(0.05, 0.2, dir.y);
            skyColor += vec3(starBrightness * 1.5) * nightFade;
        }
    }

    // 5. Camera Exposure, Filmic Tone Mapping & Gamma Correction
    float exposure = (u_Exposure > 0.0) ? u_Exposure : 1.0;
    vec3 exposed = skyColor * exposure;

    vec3 mappedColor;
    if (u_ToneMappingMode == 0) {
        mappedColor = ToneMapACES(exposed);
    } else if (u_ToneMappingMode == 1) {
        mappedColor = ToneMapReinhard(exposed);
    } else if (u_ToneMappingMode == 2) {
        mappedColor = ToneMapUncharted2(exposed);
    } else {
        mappedColor = clamp(exposed, 0.0, 1.0);
    }

    float gamma = (u_Gamma > 0.0) ? u_Gamma : 2.2;
    vec3 srgbColor = pow(mappedColor, vec3(1.0 / gamma));

    o_Color = vec4(srgbColor, 1.0);
}
