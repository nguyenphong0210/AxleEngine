#version 450 core

layout(location = 0) in vec3 a_QuadPos;
layout(location = 1) in vec2 a_QuadTexCoord;

// Instanced Attributes (divisor = 1)
layout(location = 2) in vec3 a_InstancePosition;
layout(location = 3) in float a_InstanceSize;
layout(location = 4) in vec4 a_InstanceColor;
layout(location = 5) in float a_InstanceRotation;
layout(location = 6) in int a_InstanceOrientation;
layout(location = 7) in vec3 a_InstanceVelocity;
layout(location = 8) in vec4 a_InstanceUVOffsetScale;

uniform mat4 u_View;
uniform mat4 u_Projection;
uniform mat4 u_ViewProjection;
uniform vec3 u_CameraPosition;
uniform vec3 u_CameraRight;
uniform vec3 u_CameraUp;

out vec2 v_TexCoord;
out vec4 v_Color;
out vec3 v_WorldPos;
out vec3 v_ViewPos;

void main() {
    // Flipbook UV mapping: base quad UV [0,1] scaled and offset
    v_TexCoord = a_InstanceUVOffsetScale.xy + (a_QuadTexCoord * a_InstanceUVOffsetScale.zw);
    v_Color = a_InstanceColor;

    // Base 2D rotation of quad
    float c = cos(a_InstanceRotation);
    float s = sin(a_InstanceRotation);
    vec2 rotPos = vec2(
        a_QuadPos.x * c - a_QuadPos.y * s,
        a_QuadPos.x * s + a_QuadPos.y * c
    ) * a_InstanceSize;

    vec3 worldRight = u_CameraRight;
    vec3 worldUp = u_CameraUp;

    if (a_InstanceOrientation == 0) {
        // 0: Spherical Camera-Facing (Standard Smoke, Dust, Cloud, Fire)
        worldRight = u_CameraRight;
        worldUp = u_CameraUp;
    } else if (a_InstanceOrientation == 1) {
        // 1: Cylindrical / Axial Y (Rising columns, vertical beams)
        worldUp = vec3(0.0, 1.0, 0.0);
        vec3 toCam = u_CameraPosition - a_InstancePosition;
        toCam.y = 0.0;
        if (length(toCam) > 0.0001) {
            toCam = normalize(toCam);
            worldRight = normalize(cross(worldUp, toCam));
        } else {
            worldRight = vec3(1.0, 0.0, 0.0);
        }
    } else if (a_InstanceOrientation == 2) {
        // 2: Velocity-Aligned (Stretched Sparks, Rain, Debris)
        vec3 vel = a_InstanceVelocity;
        float speed = length(vel);
        if (speed > 0.001) {
            vec3 dir = vel / speed;
            vec3 toCam = normalize(u_CameraPosition - a_InstancePosition);
            worldRight = normalize(cross(dir, toCam));
            worldUp = dir;
            // Stretch along velocity direction proportional to speed
            rotPos.y *= max(1.0, speed * 0.25);
        } else {
            worldRight = u_CameraRight;
            worldUp = u_CameraUp;
        }
    }

    vec3 worldPos = a_InstancePosition + (worldRight * rotPos.x) + (worldUp * rotPos.y);
    v_WorldPos = worldPos;

    vec4 viewPos = u_View * vec4(worldPos, 1.0);
    v_ViewPos = viewPos.xyz;

    gl_Position = u_Projection * viewPos;
}
