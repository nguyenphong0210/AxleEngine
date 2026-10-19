#version 450 core

layout(location = 0) out vec4 o_Color;

in vec2 v_TexCoord;
in vec2 v_TexCoordNext;
in float v_SubFrameBlend;
in vec2 v_QuadCoord;
in vec4 v_Color;
in vec3 v_WorldPos;
in vec3 v_ViewPos;
in vec3 v_Normal;
in float v_LightingFlag;

// Texture Atlas & Flipbook
uniform sampler2D u_ParticleTexture;
uniform int u_HasTexture;
uniform int u_SubFrameBlendingEnabled;

// Normal Mapping & Volumetric Lighting
uniform sampler2D u_ParticleNormalMap;
uniform int u_HasNormalMap;
uniform float u_NormalMapStrength;
uniform int u_LightingEnabled;

uniform vec3 u_CameraPosition;
uniform vec3 u_SunDirection;
uniform vec3 u_SunColor;
uniform vec3 u_AmbientColor;
uniform float u_WrapLighting;
uniform float u_ScatteringFactor;

// Point Lights (up to 4)
uniform int u_PointLightCount;
uniform vec3 u_PointLightPositions[4];
uniform vec3 u_PointLightColors[4];
uniform float u_PointLightRadii[4];
uniform float u_PointLightIntensities[4];

// Camera Near & Far Fading
uniform int u_NearFadeEnabled;
uniform float u_NearFadeStart;
uniform float u_NearFadeEnd;

uniform int u_FarFadeEnabled;
uniform float u_FarFadeStart;
uniform float u_FarFadeEnd;

// Soft Particles depth uniforms
uniform sampler2D u_SceneDepthBuffer;
uniform int u_SoftParticlesEnabled;
uniform float u_SoftnessDistance;
uniform vec2 u_ScreenSize;
uniform float u_CameraNear;
uniform float u_CameraFar;

float LinearizeDepth(float depth, float near, float far) {
    return (2.0 * near * far) / (far + near - (depth * 2.0 - 1.0) * (far - near));
}

void main() {
    // 1. Texture Sampling & Flipbook Sub-Frame Interpolation
    vec4 texColor = vec4(1.0);
    if (u_HasTexture == 1) {
        vec4 colA = texture(u_ParticleTexture, v_TexCoord);
        if (u_SubFrameBlendingEnabled == 1 && v_SubFrameBlend > 0.0) {
            vec4 colB = texture(u_ParticleTexture, v_TexCoordNext);
            texColor = mix(colA, colB, v_SubFrameBlend);
        } else {
            texColor = colA;
        }
    } else {
        // High-quality procedural radial soft circle
        vec2 centerCoord = (v_QuadCoord - vec2(0.5)) * 2.0;
        float distSq = dot(centerCoord, centerCoord);
        if (distSq > 1.0) {
            discard;
        }
        float alphaMask = smoothstep(1.0, 0.15, distSq);
        texColor = vec4(1.0, 1.0, 1.0, alphaMask);
    }

    vec4 finalColor = v_Color * texColor;
    if (finalColor.a <= 0.001) {
        discard;
    }

    // 2. Normal-Mapped Volumetric Lighting
    if (u_LightingEnabled == 1 && v_LightingFlag > 0.5) {
        vec3 N = normalize(v_Normal);
        if (u_HasNormalMap == 1) {
            vec3 normalTex = texture(u_ParticleNormalMap, v_TexCoord).xyz * 2.0 - 1.0;
            N = normalize(mix(N, normalTex, clamp(u_NormalMapStrength, 0.0, 1.0)));
        }

        // Half-Lambert wrapped diffuse lighting for soft light penetration in volumes
        float NdotL = dot(N, normalize(u_SunDirection));
        float wrap = max(0.0, u_WrapLighting);
        float wrappedDiff = clamp((NdotL + wrap) / (1.0 + wrap), 0.0, 1.0);

        // Forward Mie scattering: translucent glow when looking toward light source
        vec3 viewDir = normalize(u_CameraPosition - v_WorldPos);
        float VdotL = max(0.0, dot(-viewDir, normalize(u_SunDirection)));
        float forwardScatter = pow(VdotL, 4.0) * u_ScatteringFactor;

        vec3 totalLight = u_AmbientColor + (u_SunColor * (wrappedDiff + forwardScatter));

        // Point light contributions
        for (int i = 0; i < u_PointLightCount && i < 4; ++i) {
            vec3 lightVec = u_PointLightPositions[i] - v_WorldPos;
            float dist = length(lightVec);
            float radius = u_PointLightRadii[i];
            if (dist < radius) {
                vec3 L = lightVec / max(0.0001, dist);
                float atten = clamp(1.0 - (dist / radius), 0.0, 1.0);
                atten *= atten;
                float pDiff = clamp((dot(N, L) + wrap) / (1.0 + wrap), 0.0, 1.0);
                totalLight += u_PointLightColors[i] * (pDiff * atten * u_PointLightIntensities[i]);
            }
        }

        finalColor.rgb *= totalLight;
    }

    // 3. Camera Near-Plane Fade
    float camDist = -v_ViewPos.z;
    if (u_NearFadeEnabled == 1) {
        float nearFade = smoothstep(u_NearFadeStart, u_NearFadeEnd, camDist);
        finalColor.a *= nearFade;
    }

    // 4. Distance / Far View Fading
    if (u_FarFadeEnabled == 1) {
        float farFade = 1.0 - smoothstep(u_FarFadeStart, u_FarFadeEnd, camDist);
        finalColor.a *= farFade;
    }

    // 5. Soft Particle Depth-Fade with Smooth S-Curve
    if (u_SoftParticlesEnabled == 1 && u_ScreenSize.x > 0.0 && u_ScreenSize.y > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / u_ScreenSize;
        float sceneDepthRaw = texture(u_SceneDepthBuffer, screenUV).r;
        float sceneDepthLinear = LinearizeDepth(sceneDepthRaw, u_CameraNear, u_CameraFar);

        float depthDiff = sceneDepthLinear - camDist;
        if (depthDiff > 0.0) {
            float rawFactor = clamp(depthDiff / max(0.001, u_SoftnessDistance), 0.0, 1.0);
            float softFactor = smoothstep(0.0, 1.0, rawFactor);
            finalColor.a *= softFactor;
        }
    }

    o_Color = finalColor;
}
