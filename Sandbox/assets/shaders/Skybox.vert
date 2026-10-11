#version 450 core
layout(location = 0) in vec3 a_Position;

out vec3 v_TexCoord;

uniform mat4 u_View;
uniform mat4 u_Projection;

void main() {
    v_TexCoord = a_Position;
    // Strip translation component from camera view matrix so skybox is centered at infinity
    mat4 rotView = mat4(mat3(u_View));
    vec4 clipPos = u_Projection * rotView * vec4(a_Position, 1.0);
    // Setting z = w ensures NDC depth (z/w) is exactly 1.0 on the far clipping plane
    gl_Position = clipPos.xyww;
}
