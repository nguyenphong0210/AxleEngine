#version 450 core
layout(location = 0) in vec3 a_Position;

out vec3 v_TexCoord;

uniform mat4 u_View;
uniform mat4 u_Projection;

void main() {
    // a_Position is in NDC [-1, 1]. Render on far plane (z=1.0, w=1.0)
    gl_Position = vec4(a_Position.x, a_Position.y, 1.0, 1.0);

    // Unproject NDC coordinates to world-space view ray direction
    vec4 viewRay = inverse(u_Projection) * vec4(a_Position.x, a_Position.y, 1.0, 1.0);
    v_TexCoord = transpose(mat3(u_View)) * viewRay.xyz;
}
