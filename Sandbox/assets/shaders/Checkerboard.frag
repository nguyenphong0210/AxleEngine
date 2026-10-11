#version 450 core

layout(location = 0) out vec4 o_Color;

in vec3 v_WorldPos;
in vec3 v_LocalPos;
in vec2 v_TexCoord;
in vec4 v_FragPosLightSpace;

// 1. Procedural Pattern Uniforms
uniform int u_PatternMode;        // 0: World XZ Caro (Floor), 1: 3D Caro (Box/Mesh), 2: Spherical Caro
uniform float u_TileScale;        // Checkerboard tile frequency
uniform vec4 u_Color1;            // Primary color or tint multiplier
uniform vec4 u_Color2;            // Secondary pattern color

// 2. Directional Sun Light & Real-Time Shadow Mapping
uniform vec3 u_LightDir;          // Directional sun light vector
uniform vec3 u_LightColor;        // Sun light RGB color
uniform float u_LightIntensity;   // Sun brightness multiplier
uniform float u_AmbientIntensity; // Ambient fill lighting
uniform int u_ShadowsEnabled;     // 1: Enable soft shadows, 0: Disable shadows
uniform mat4 u_Transform;

// 3. Texture Sampler Slots
uniform sampler2D u_AlbedoMap;     // Slot 0: Base Color / Diffuse Map
uniform sampler2D u_ShadowMap;     // Slot 1: Directional Light Depth Texture
uniform sampler2D u_NormalMap;     // Slot 2: Tangent-space Normal Map
uniform sampler2D u_RoughnessMap;  // Slot 3: Micro-surface Roughness Map
uniform sampler2D u_MetallicMap;   // Slot 4: Metallic Mask Map

// 4. Texture Mapping & Material Controls
uniform int u_UseAlbedoMap;        // 1: Sample u_AlbedoMap, 0: Procedural pattern
uniform int u_UseNormalMap;        // 1: Perturb normal via u_NormalMap, 0: Geometric normal
uniform int u_UseRoughnessMap;     // 1: Sample u_RoughnessMap, 0: Uniform u_Roughness
uniform int u_UseMetallicMap;      // 1: Sample u_MetallicMap, 0: Uniform u_Metallic
uniform vec2 u_UVScale;            // UV tiling multiplier (default 1.0, 1.0)
uniform float u_Roughness;         // Material roughness (0.04 .. 1.0, default 0.5)
uniform float u_Metallic;          // Material metallic (0.0 .. 1.0, default 0.0)

float CalculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    // Adaptive slope-scaled depth bias to eliminate acne and moiré artifacts
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
    return shadow / 9.0;
}

// Cotangent-frame normal perturbation from screen-space derivatives
vec3 PerturbNormal(vec3 N, vec3 V, vec2 texcoord) {
    vec3 mapNormal = texture(u_NormalMap, texcoord).xyz * 2.0 - 1.0;

    vec3 dp1 = dFdx(v_WorldPos);
    vec3 dp2 = dFdy(v_WorldPos);
    vec2 duv1 = dFdx(texcoord);
    vec2 duv2 = dFdy(texcoord);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float lenT = length(T);
    float lenB = length(B);
    if (lenT < 1e-6 || lenB < 1e-6) {
        return N;
    }

    mat3 TBN = mat3(T / lenT, B / lenB, N);
    return normalize(TBN * mapNormal);
}

void main() {
    vec2 uvScale = (u_UVScale.x != 0.0 && u_UVScale.y != 0.0) ? u_UVScale : vec2(1.0);
    vec2 uv = v_TexCoord * uvScale;

    // 1. Geometric surface normal
    vec3 normal = vec3(0.0, 1.0, 0.0);
    if (u_PatternMode == 0) {
        normal = vec3(0.0, 1.0, 0.0);
    } else if (u_PatternMode == 1) {
        vec3 dx = dFdx(v_WorldPos);
        vec3 dy = dFdy(v_WorldPos);
        normal = normalize(cross(dx, dy));
        if (dot(normal, normalize(-v_WorldPos)) < 0.0) normal = -normal;
    } else {
        normal = normalize((u_Transform * vec4(v_LocalPos, 0.0)).xyz);
    }

    // 2. Cotangent normal map perturbation
    if (u_UseNormalMap == 1) {
        vec3 viewDir = normalize(-v_WorldPos);
        normal = PerturbNormal(normal, viewDir, uv);
    }

    // 3. Base color (Albedo texture map or procedural checkerboard)
    vec4 baseColor;
    if (u_UseAlbedoMap == 1) {
        vec4 texColor = texture(u_AlbedoMap, uv);
        baseColor = texColor * u_Color1;
    } else {
        float pattern = 0.0;
        if (u_PatternMode == 0) {
            vec2 coord = floor(v_WorldPos.xz * u_TileScale);
            pattern = mod(coord.x + coord.y, 2.0);
        } else if (u_PatternMode == 1) {
            vec3 coord = floor((v_LocalPos + vec3(0.5)) * u_TileScale + vec3(0.0001));
            pattern = mod(coord.x + coord.y + coord.z, 2.0);
        } else {
            vec3 nLocal = normalize(v_LocalPos);
            float uSph = atan(nLocal.z, nLocal.x) / (2.0 * 3.14159265359) + 0.5;
            float vSph = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265359 + 0.5;
            vec2 coord = floor(vec2(uSph * u_TileScale, vSph * (u_TileScale * 0.5)));
            pattern = mod(coord.x + coord.y, 2.0);
        }
        baseColor = mix(u_Color1, u_Color2, pattern);
    }

    // 4. Material properties (Roughness & Metallic)
    float roughness = (u_UseRoughnessMap == 1) ? texture(u_RoughnessMap, uv).r : u_Roughness;
    roughness = clamp(roughness, 0.04, 1.0);

    float metallic = (u_UseMetallicMap == 1) ? texture(u_MetallicMap, uv).r : u_Metallic;
    metallic = clamp(metallic, 0.0, 1.0);

    // 5. Directional diffuse + ambient lighting with real-time soft shadows
    vec3 lightDir = normalize(u_LightDir);
    float NdotL = max(dot(normal, lightDir), 0.0);

    float shadow = 0.0;
    if (u_ShadowsEnabled == 1) {
        shadow = CalculateShadow(v_FragPosLightSpace, normal, lightDir);
    }

    // 6. View-dependent specular reflection
    vec3 viewDir = normalize(-v_WorldPos);
    vec3 halfDir = normalize(lightDir + viewDir);

    float alpha = max(roughness * roughness, 0.001);
    float shininess = clamp(2.0 / (alpha * alpha) - 2.0, 1.0, 1024.0);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float spec = pow(NdotH, shininess) * (1.0 - shadow);

    vec3 specColor = mix(vec3(0.04), baseColor.rgb, metallic);
    vec3 diffColor = baseColor.rgb * (1.0 - metallic);

    vec3 ambient = diffColor * u_LightColor * u_AmbientIntensity;
    vec3 diffuse = diffColor * u_LightColor * (u_LightIntensity * NdotL * (1.0 - shadow));
    vec3 specular = specColor * u_LightColor * (spec * u_LightIntensity * (0.2 + 0.8 * metallic));

    vec3 finalRgb = ambient + diffuse + specular;
    o_Color = vec4(finalRgb, baseColor.a);
}
