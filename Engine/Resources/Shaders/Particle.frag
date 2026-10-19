#version 450 core

layout(location = 0) out vec4 o_Color;

in vec2 v_TexCoord;
in vec4 v_Color;
in vec3 v_WorldPos;
in vec3 v_ViewPos;

uniform sampler2D u_ParticleTexture;
uniform int u_HasTexture;

// Soft Particles depth uniforms
uniform sampler2D u_SceneDepthBuffer;
uniform int u_SoftParticlesEnabled;
uniform float u_SoftnessDistance;
uniform vec2 u_ScreenSize;
uniform float u_CameraNear;
uniform float u_CameraFar;

float LinearizeDepth(float depth, float near, float far) {
    // Map non-linear depth buffer [0, 1] to linear view-space depth
    return (2.0 * near * far) / (far + near - (depth * 2.0 - 1.0) * (far - near));
}

void main() {
    vec4 texColor = vec4(1.0);
    if (u_HasTexture == 1) {
        texColor = texture(u_ParticleTexture, v_TexCoord);
    } else {
        // High-quality procedural radial soft circle
        vec2 centerCoord = (v_TexCoord - vec2(0.5)) * 2.0;
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

    // Soft Particle Depth-Fade Calculation
    if (u_SoftParticlesEnabled == 1 && u_ScreenSize.x > 0.0 && u_ScreenSize.y > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / u_ScreenSize;
        float sceneDepthRaw = texture(u_SceneDepthBuffer, screenUV).r;
        float sceneDepthLinear = LinearizeDepth(sceneDepthRaw, u_CameraNear, u_CameraFar);
        float particleDepthLinear = -v_ViewPos.z;

        float depthDiff = sceneDepthLinear - particleDepthLinear;
        if (depthDiff > 0.0) {
            float softFactor = clamp(depthDiff / max(0.001, u_SoftnessDistance), 0.0, 1.0);
            finalColor.a *= softFactor;
        }
    }

    o_Color = finalColor;
}
