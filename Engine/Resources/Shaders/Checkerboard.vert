#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;
uniform mat4 u_LightSpaceMatrix;

out vec3 v_WorldPos;
out vec3 v_LocalPos;
out vec2 v_TexCoord;
out vec4 v_FragPosLightSpace;

void main() {
    v_LocalPos = a_Position;
    v_TexCoord = a_TexCoord;
    vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_FragPosLightSpace = u_LightSpaceMatrix * worldPos;
    gl_Position = u_ViewProjection * worldPos;
}
