#version 450 core

layout(location = 0) out vec4 o_Color;

in vec3 v_WorldPos;
in vec3 v_LocalPos;
in vec4 v_FragPosLightSpace;

uniform int u_PatternMode;      // 0: World-space XZ caro (Floor), 1: Local-space 3D caro (Box), 2: Spherical Caro (Sphere)
uniform float u_TileScale;      // Frequency of the checkerboard tiles
uniform vec4 u_Color1;          // Primary checker tile color
uniform vec4 u_Color2;          // Secondary checker tile color
uniform vec3 u_LightDir;        // Directional sun light vector
uniform mat4 u_Transform;
uniform sampler2D u_ShadowMap;  // Real-time shadow map depth texture

float CalculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    // Perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // Transform from [-1, 1] NDC to [0, 1] texture coordinates
    projCoords = projCoords * 0.5 + 0.5;

    // Fragments outside the light frustum far plane or boundary are not shadowed
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    // Adaptive slope-scaled depth bias to eliminate shadow acne & moiré patterns
    float bias = max(0.0015 * (1.0 - dot(normal, lightDir)), 0.00015);

    // 3x3 Percentage-Closer Filtering (PCF) for smooth, high-quality penumbra
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(u_ShadowMap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(u_ShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    return shadow;
}

void main() {
    float pattern = 0.0;
    vec3 normal = vec3(0.0, 1.0, 0.0);

    if (u_PatternMode == 0) {
        // World-space XZ checkerboard (ideal for floor grids)
        vec2 coord = floor(v_WorldPos.xz * u_TileScale);
        pattern = mod(coord.x + coord.y, 2.0);

        normal = vec3(0.0, 1.0, 0.0);
    } else if (u_PatternMode == 1) {
        // Local 3D checkerboard (4x4 tiles per face on the 1x1x1 box)
        vec3 coord = floor((v_LocalPos + vec3(0.5)) * u_TileScale + vec3(0.0001));
        pattern = mod(coord.x + coord.y + coord.z, 2.0);

        vec3 dx = dFdx(v_WorldPos);
        vec3 dy = dFdy(v_WorldPos);
        normal = normalize(cross(dx, dy));
        if (dot(normal, normalize(-v_WorldPos)) < 0.0) normal = -normal;
    } else {
        // Spherical Caro (Latitude & Longitude checkered pattern)
        vec3 nLocal = normalize(v_LocalPos);
        float u = atan(nLocal.z, nLocal.x) / (2.0 * 3.14159265359) + 0.5;
        float v = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265359 + 0.5;
        vec2 coord = floor(vec2(u * u_TileScale, v * (u_TileScale * 0.5)));
        pattern = mod(coord.x + coord.y, 2.0);

        // Smooth analytic normal for sphere
        normal = normalize((u_Transform * vec4(v_LocalPos, 0.0)).xyz);
    }

    vec4 baseColor = mix(u_Color1, u_Color2, pattern);

    // Directional diffuse + ambient light with real-time soft shadows
    vec3 lightDir = normalize(u_LightDir);
    float NdotL = max(dot(normal, lightDir), 0.0);

    float shadow = CalculateShadow(v_FragPosLightSpace, normal, lightDir);

    float ambient = 0.28;
    float diffuse = 0.72 * NdotL * (1.0 - shadow);
    float lighting = ambient + diffuse;

    // Subtle specular highlight for refined studio reflection (masked by shadow)
    vec3 viewDir = normalize(-v_WorldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.18 * (1.0 - shadow);

    o_Color = vec4(baseColor.rgb * lighting + vec3(spec), baseColor.a);
}
