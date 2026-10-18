#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in ivec4 a_BoneIDs;
layout(location = 4) in vec4 a_BoneWeights;

const int MAX_BONES = 128;
uniform mat4 u_BoneMatrices[MAX_BONES];
uniform int u_SkinningEnabled;

uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;
uniform mat4 u_LightSpaceMatrix;

out vec3 v_WorldPos;
out vec3 v_LocalPos;
out vec2 v_TexCoord;
out vec4 v_FragPosLightSpace;

void main() {
    v_TexCoord = a_TexCoord;

    vec4 skinnedPos = vec4(0.0);
    vec3 skinnedNormal = vec3(0.0);

    if (u_SkinningEnabled == 1) {
        float totalWeight = 0.0;
        for (int i = 0; i < 4; ++i) {
            int boneID = a_BoneIDs[i];
            float weight = a_BoneWeights[i];
            if (boneID >= 0 && boneID < MAX_BONES && weight > 0.0) {
                skinnedPos += (u_BoneMatrices[boneID] * vec4(a_Position, 1.0)) * weight;
                skinnedNormal += (mat3(u_BoneMatrices[boneID]) * a_Normal) * weight;
                totalWeight += weight;
            }
        }
        if (totalWeight <= 0.0001) {
            skinnedPos = vec4(a_Position, 1.0);
            skinnedNormal = a_Normal;
        }
    } else {
        skinnedPos = vec4(a_Position, 1.0);
        skinnedNormal = a_Normal;
    }

    v_LocalPos = skinnedPos.xyz;
    vec4 worldPos = u_Transform * vec4(skinnedPos.xyz, 1.0);
    v_WorldPos = worldPos.xyz;
    v_FragPosLightSpace = u_LightSpaceMatrix * worldPos;
    gl_Position = u_ViewProjection * worldPos;
}
