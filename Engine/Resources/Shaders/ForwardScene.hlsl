// =========================================================================
// ForwardScene.hlsl - Unified D3D12 & Vulkan Viewport Forward Scene Shader
// AxleEngine Multi-Backend 3D Viewport Scene Renderer
// =========================================================================

// Root Constants / Push Constants (register b0, space0)
// Size: EXACTLY 128 bytes (32 DWORDs)
// In Vulkan (SPIR-V), DXC compiles [[vk::push_constant]] to physical push constant range
#if defined(__spirv__)
struct ObjectConstants_t {
    float4x4 u_Model;               // 64 bytes (Offset 0)
    float4   u_ColorPrimary;        // 16 bytes (Offset 64)
    float4   u_ColorSecondary;      // 16 bytes (Offset 80)
    float4   u_Params;              // 16 bytes (Offset 96) - x: TileScale, y: PatternMode, z: Roughness, w: Metallic
    float4   u_SunDirAndIntensity;  // 16 bytes (Offset 112) - xyz: Normalized sun direction, w: Sun intensity
};
[[vk::push_constant]] ObjectConstants_t ObjectConstants;
#define u_Model (ObjectConstants.u_Model)
#define u_ColorPrimary (ObjectConstants.u_ColorPrimary)
#define u_ColorSecondary (ObjectConstants.u_ColorSecondary)
#define u_Params (ObjectConstants.u_Params)
#define u_SunDirAndIntensity (ObjectConstants.u_SunDirAndIntensity)
#else
cbuffer ObjectConstants : register(b0, space0) {
    float4x4 u_Model;               // 64 bytes (Offset 0)
    float4   u_ColorPrimary;        // 16 bytes (Offset 64)
    float4   u_ColorSecondary;      // 16 bytes (Offset 80)
    float4   u_Params;              // 16 bytes (Offset 96) - x: TileScale, y: PatternMode, z: Roughness, w: Metallic
    float4   u_SunDirAndIntensity;  // 16 bytes (Offset 112) - xyz: Normalized sun direction, w: Sun intensity
};
#endif

// Scene Constants (register b1, space0)
cbuffer SceneConstants : register(b1, space0) {
    float4x4 u_ViewProjection;
};

// Vertex Input Layout (Matches 64-byte RHIVertex3D)
struct VSInput {
    float3 Position   : POSITION;
    float3 Normal     : NORMAL;
    float4 Tangent    : TANGENT;
    float2 TexCoord   : TEXCOORD0;
    float4 Color      : COLOR0;
};

// Vertex Output / Pixel Input
struct PSInput {
    float4 PositionCS : SV_Position;
    float3 WorldPos   : POSITION;
    float3 NormalWS   : NORMAL;
    float2 TexCoord   : TEXCOORD0;
    float4 VertexColor: COLOR0;
    float3 LocalPos   : TEXCOORD1;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    float4 worldPos    = mul(u_Model, float4(input.Position, 1.0));
    output.PositionCS  = mul(u_ViewProjection, worldPos);
    output.WorldPos    = worldPos.xyz;
    output.NormalWS    = normalize(mul((float3x3)u_Model, input.Normal));
    output.TexCoord    = input.TexCoord;
    output.VertexColor = input.Color;
    output.LocalPos    = input.Position;
    return output;
}

float glsl_mod(float x, float y) {
    return x - y * floor(x / y);
}

float4 PSMain(PSInput input) : SV_Target0 {
    float3 N = normalize(input.NormalWS);
    float3 L = normalize(u_SunDirAndIntensity.xyz);
    
    // Pattern & Color Calculation
    int patternMode = (int)u_Params.y;
    float tileScale = max(0.01, u_Params.x);
    float4 baseColor = u_ColorPrimary;
    
    if (patternMode == 0) {
        // Mode 0: World XZ Caro (Floor)
        float2 coord = floor(input.WorldPos.xz * tileScale);
        float pattern = glsl_mod(coord.x + coord.y, 2.0);
        baseColor = lerp(u_ColorPrimary, u_ColorSecondary, pattern);
    } else if (patternMode == 1) {
        // Mode 1: Local 3D Caro (Box/Mesh)
        float3 coord = floor((input.LocalPos + float3(0.5, 0.5, 0.5)) * tileScale + float3(0.0001, 0.0001, 0.0001));
        float pattern = glsl_mod(coord.x + coord.y + coord.z, 2.0);
        baseColor = lerp(u_ColorPrimary, u_ColorSecondary, pattern);
    } else if (patternMode == 2) {
        // Mode 2: Spherical Caro (Sphere)
        float3 nLocal = normalize(input.LocalPos);
        float uSph = atan2(nLocal.z, nLocal.x) / (2.0 * 3.14159265) + 0.5;
        float vSph = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265 + 0.5;
        float2 coord = floor(float2(uSph * tileScale, vSph * (tileScale * 0.5)));
        float pattern = glsl_mod(coord.x + coord.y, 2.0);
        baseColor = lerp(u_ColorPrimary, u_ColorSecondary, pattern);
    } else if (patternMode == 3) {
        // Mode 3: Solid Color
        baseColor = u_ColorPrimary;
    } else if (patternMode == 4) {
        // Mode 4: Splatmap Terrain Heightfield
        baseColor = u_ColorPrimary * input.VertexColor;
    } else {
        baseColor = u_ColorPrimary;
    }
    
    // Diffuse directional lighting (Half-Lambert / Lambertian)
    float NdotL = max(0.0, dot(N, L));
    float sunIntensity = max(0.2, u_SunDirAndIntensity.w);
    float3 diffuse = baseColor.rgb * (NdotL * sunIntensity);
    
    // Ambient light fill (cool atmospheric sky tint)
    float3 ambient = baseColor.rgb * float3(0.22, 0.25, 0.32);
    
    // Specular highlight (Blinn-Phong)
    float3 V = float3(0.0, 0.707, 0.707); // Approximation of view direction
    float3 H = normalize(L + V);
    float NdotH = max(0.0, dot(N, H));
    float specPower = lerp(16.0, 128.0, 1.0 - u_Params.z);
    float3 specular = float3(0.9, 0.9, 0.9) * pow(NdotH, specPower) * (1.0 - u_Params.z) * 0.35;
    
    float3 finalRGB = ambient + diffuse + specular;
    return float4(finalRGB, baseColor.a);
}
