// =========================================================================
// ForwardScene.hlsl - Unified D3D12 & Vulkan Viewport Forward Scene Shader
// AxleEngine Multi-Backend 3D Viewport Scene Renderer
// =========================================================================

// Root Constants / Push Constants (register b0, space0)
// Size: EXACTLY 128 bytes (32 DWORDs)
cbuffer ObjectConstants : register(b0, space0) {
    float4x4 u_ModelViewProjection; // 64 bytes (Offset 0)
    float4   u_ColorPrimary;        // 16 bytes (Offset 64)
    float4   u_ColorSecondary;      // 16 bytes (Offset 80)
    float4   u_Params;              // 16 bytes (Offset 96) - x: TileScale, y: PatternMode, z: Roughness, w: Metallic
    float4   u_SunDirAndIntensity;  // 16 bytes (Offset 112) - xyz: Normalized sun direction, w: Sun intensity
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
    float3 NormalWS   : NORMAL;
    float2 TexCoord   : TEXCOORD0;
    float4 VertexColor: COLOR0;
    float3 LocalPos   : TEXCOORD1;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.PositionCS = mul(u_ModelViewProjection, float4(input.Position, 1.0));
    output.NormalWS   = input.Normal;
    output.TexCoord   = input.TexCoord;
    output.VertexColor= input.Color;
    output.LocalPos   = input.Position;
    return output;
}

float4 PSMain(PSInput input) : SV_Target0 {
    float3 N = normalize(input.NormalWS);
    float3 L = normalize(u_SunDirAndIntensity.xyz);
    
    // Pattern & Color Calculation
    int patternMode = (int)u_Params.y;
    float tileScale = max(0.01, u_Params.x);
    float4 baseColor = u_ColorPrimary;
    
    if (patternMode == 0 || patternMode == 1) {
        // Procedural 3D Caro / Checkerboard pattern
        float3 checkCoords = floor(input.LocalPos / tileScale);
        float check = fmod(abs(checkCoords.x + checkCoords.y + checkCoords.z), 2.0);
        baseColor = (check > 0.5) ? u_ColorPrimary : u_ColorSecondary;
    } else if (patternMode == 2) {
        // Vertex Color splat weights (Terrain heightfield)
        baseColor = u_ColorPrimary * input.VertexColor;
    } else {
        // Solid color or custom tint
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
