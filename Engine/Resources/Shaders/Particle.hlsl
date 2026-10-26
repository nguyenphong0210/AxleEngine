// =============================================================================
// Axle Engine - Particle.hlsl
// GPU Instanced Volumetric Particle VFX Pass (HLSL 2021)
// Direct3D 12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V) Unified Shader Codebase
// =============================================================================

// -----------------------------------------------------------------------------
// Uniform Constant Buffer (b0, space 0) - Exactly 512 bytes (CBV aligned)
// -----------------------------------------------------------------------------
cbuffer ParticleConstants : register(b0, space0) {
    float4x4 u_ViewProjection;        // 64 bytes (offset 0)
    float4x4 u_View;                  // 64 bytes (offset 64)
    float4x4 u_Projection;            // 64 bytes (offset 128)
    float4   u_CameraPosition;        // 16 bytes (xyz = CamPos, w = CameraNear) (offset 192)
    float4   u_CameraRight;           // 16 bytes (xyz = CamRight, w = CameraFar) (offset 208)
    float4   u_CameraUp;              // 16 bytes (xyz = CamUp, w = SoftnessDistance) (offset 224)
    float4   u_SunDirection;          // 16 bytes (xyz = SunDir, w = WrapLighting) (offset 240)

    float4   u_SunColor;              // 16 bytes (rgb = SunColor, w = ScatteringFactor) (offset 256)
    float4   u_AmbientColor;          // 16 bytes (rgb = AmbientColor, w = NormalMapStrength) (offset 272)
    float4   u_ScreenSize;            // 16 bytes (xy = ScreenSize, zw = 1.0 / ScreenSize) (offset 288)
    float4   u_FadeParams;            // 16 bytes (x = NearFadeStart, y = NearFadeEnd, z = FarFadeStart, w = FarFadeEnd) (offset 304)
    uint4    u_Flags;                 // 16 bytes (x = HasTexture, y = SubFrameBlending, z = HasNormalMap, w = LightingEnabled) (offset 320)
    uint4    u_Flags2;                // 16 bytes (x = NearFadeEnabled, y = FarFadeEnabled, z = SoftParticlesEnabled, w = PointLightCount) (offset 336)
    float4   u_PointLightPositions[4];// 64 bytes (xyz = Position, w = Radius) (offset 352..416)
    float4   u_PointLightColors[4];   // 64 bytes (rgb = Color, w = Intensity) (offset 416..480)
    float4   u_Reserved[2];           // 32 bytes padding -> Total 512 bytes (offset 480..512)
};

// -----------------------------------------------------------------------------
// Texture & Sampler Bindings (space 1)
// -----------------------------------------------------------------------------
Texture2D    u_ParticleTexture   : register(t0, space1);
Texture2D    u_ParticleNormalMap : register(t1, space1);
SamplerState u_LinearSampler     : register(s0, space1);

// -----------------------------------------------------------------------------
// Vertex Shader Input (Instanced Vertex Stream)
// -----------------------------------------------------------------------------
struct VSInput {
    uint   VertexID            : SV_VertexID;
    float3 InstancePosition    : POSITION;
    float  InstanceSize        : PSIZE;
    float4 InstanceColor       : COLOR;
    float  InstanceRotation    : ROTATION;
    int    InstanceOrientation : ORIENTATION;
    float3 InstanceVelocity    : VELOCITY;
    float4 InstanceUVOffsetScale : TEXCOORD0;
    float4 InstanceUVSubFrame  : TEXCOORD1;
};

// -----------------------------------------------------------------------------
// Vertex to Pixel Shader Interpolators
// -----------------------------------------------------------------------------
struct PSInput {
    float4 Position       : SV_Position;
    float2 TexCoord       : TEXCOORD0;
    float2 TexCoordNext   : TEXCOORD1;
    float  SubFrameBlend  : BLENDWEIGHT;
    float2 QuadCoord      : QUADCOORD;
    float4 Color          : COLOR;
    float3 WorldPos       : WORLDPOS;
    float3 ViewPos        : VIEWPOS;
    float3 Normal         : NORMAL;
    float  LightingFlag   : LIGHTINGFLAG;
};

// Procedural quad unit vertices (2 triangles, CCW, [-0.5, 0.5])
static const float2 c_QuadPositions[6] = {
    float2(-0.5f, -0.5f),
    float2( 0.5f, -0.5f),
    float2( 0.5f,  0.5f),
    float2( 0.5f,  0.5f),
    float2(-0.5f,  0.5f),
    float2(-0.5f, -0.5f)
};

static const float2 c_QuadUVs[6] = {
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f),
    float2(1.0f, 1.0f),
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f)
};

// =============================================================================
// VERTEX SHADER
// =============================================================================
PSInput VSMain(VSInput input) {
    PSInput output;

    uint vIdx = input.VertexID % 6;
    float2 quadPos = c_QuadPositions[vIdx];
    float2 quadUV  = c_QuadUVs[vIdx];

    output.TexCoord = input.InstanceUVOffsetScale.xy + (quadUV * input.InstanceUVOffsetScale.zw);
    output.TexCoordNext = input.InstanceUVSubFrame.xy + (quadUV * input.InstanceUVOffsetScale.zw);
    output.SubFrameBlend = input.InstanceUVSubFrame.z;
    output.LightingFlag = input.InstanceUVSubFrame.w;
    output.QuadCoord = quadUV;
    output.Color = input.InstanceColor;

    // 2D Rotation around billboard normal
    float c = cos(input.InstanceRotation);
    float s = sin(input.InstanceRotation);
    float2 rotPos = float2(
        quadPos.x * c - quadPos.y * s,
        quadPos.x * s + quadPos.y * c
    ) * input.InstanceSize;

    // Billboard orientation calculation
    float3 worldRight = u_CameraRight.xyz;
    float3 worldUp = u_CameraUp.xyz;

    if (input.InstanceOrientation == 0) {
        // Camera-Facing Billboard
        worldRight = u_CameraRight.xyz;
        worldUp = u_CameraUp.xyz;
    } else if (input.InstanceOrientation == 1) {
        // Axial-Y Billboard (e.g. smoke columns, vertical torches)
        worldUp = float3(0.0f, 1.0f, 0.0f);
        float3 toCam = u_CameraPosition.xyz - input.InstancePosition;
        toCam.y = 0.0f;
        float dCam = length(toCam);
        if (dCam > 0.0001f) {
            toCam /= dCam;
            worldRight = normalize(cross(worldUp, toCam));
        } else {
            worldRight = float3(1.0f, 0.0f, 0.0f);
        }
    } else if (input.InstanceOrientation == 2) {
        // Velocity-Aligned Billboard (sparks, tire roost trails, debris)
        float3 vel = input.InstanceVelocity;
        float speed = length(vel);
        if (speed > 0.001f) {
            float3 dir = vel / speed;
            float3 toCam = normalize(u_CameraPosition.xyz - input.InstancePosition);
            worldRight = normalize(cross(dir, toCam));
            worldUp = dir;
            // Stretch along motion velocity
            rotPos.y *= max(1.0f, speed * 0.25f);
        } else {
            worldRight = u_CameraRight.xyz;
            worldUp = u_CameraUp.xyz;
        }
    }

    float3 worldPos = input.InstancePosition + (worldRight * rotPos.x) + (worldUp * rotPos.y);
    output.WorldPos = worldPos;

    float4 viewPos = mul(u_View, float4(worldPos, 1.0f));
    output.ViewPos = viewPos.xyz;

    // Spherical pseudo-normal for volumetric lighting (hemisphere normal on billboard)
    float3 forward = normalize(cross(worldRight, worldUp));
    float2 d = (quadUV - float2(0.5f, 0.5f)) * 2.0f;
    float r2 = dot(d, d);
    float3 sphereNormalLocal = float3(d.x, d.y, sqrt(max(0.0f, 1.0f - r2)));
    output.Normal = normalize(worldRight * sphereNormalLocal.x + worldUp * sphereNormalLocal.y + forward * sphereNormalLocal.z);

    output.Position = mul(u_Projection, viewPos);
    return output;
}

// =============================================================================
// PIXEL SHADER
// =============================================================================
float4 PSMain(PSInput input) : SV_Target0 {
    float4 texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

    if (u_Flags.x == 1) { // HasTexture
        float4 colA = u_ParticleTexture.Sample(u_LinearSampler, input.TexCoord);
        if (u_Flags.y == 1 && input.SubFrameBlend > 0.0f) { // SubFrameBlending
            float4 colB = u_ParticleTexture.Sample(u_LinearSampler, input.TexCoordNext);
            texColor = lerp(colA, colB, input.SubFrameBlend);
        } else {
            texColor = colA;
        }
    } else {
        // Procedural circular soft radial falloff for untextured particles
        float2 centerCoord = (input.QuadCoord - float2(0.5f, 0.5f)) * 2.0f;
        float distSq = dot(centerCoord, centerCoord);
        if (distSq > 1.0f) {
            discard;
        }
        float alphaMask = smoothstep(1.0f, 0.15f, distSq);
        texColor = float4(1.0f, 1.0f, 1.0f, alphaMask);
    }

    float4 finalColor = input.Color * texColor;
    if (finalColor.a <= 0.001f) {
        discard;
    }

    // Volumetric wrap lighting
    if (u_Flags.w == 1 && input.LightingFlag > 0.5f) { // LightingEnabled
        float3 N = normalize(input.Normal);
        if (u_Flags.z == 1) { // HasNormalMap
            float3 normalTex = u_ParticleNormalMap.Sample(u_LinearSampler, input.TexCoord).xyz * 2.0f - 1.0f;
            N = normalize(lerp(N, normalTex, saturate(u_AmbientColor.w))); // u_AmbientColor.w = NormalMapStrength
        }

        float3 sunDir = normalize(u_SunDirection.xyz);
        float NdotL = dot(N, sunDir);
        float wrap = max(0.0f, u_SunDirection.w); // u_SunDirection.w = WrapLighting
        float wrappedDiff = saturate((NdotL + wrap) / (1.0f + wrap));

        float3 viewDir = normalize(u_CameraPosition.xyz - input.WorldPos);
        float VdotL = max(0.0f, dot(-viewDir, sunDir));
        float forwardScatter = pow(VdotL, 4.0f) * u_SunColor.w; // u_SunColor.w = ScatteringFactor

        float3 totalLight = u_AmbientColor.rgb + (u_SunColor.rgb * (wrappedDiff + forwardScatter));

        // Up to 4 dynamic point lights
        uint pointLightCount = min(u_Flags2.w, 4u);
        for (uint i = 0; i < pointLightCount; ++i) {
            float3 lightVec = u_PointLightPositions[i].xyz - input.WorldPos;
            float dist = length(lightVec);
            float radius = u_PointLightPositions[i].w;
            if (dist < radius) {
                float3 L = lightVec / max(0.0001f, dist);
                float atten = saturate(1.0f - (dist / radius));
                atten *= atten;
                float pDiff = saturate((dot(N, L) + wrap) / (1.0f + wrap));
                totalLight += u_PointLightColors[i].rgb * (pDiff * atten * u_PointLightColors[i].w);
            }
        }

        finalColor.rgb *= totalLight;
    }

    // Near & far camera distance fading
    float camDist = -input.ViewPos.z;
    if (u_Flags2.x == 1) { // NearFadeEnabled
        float nearFade = smoothstep(u_FadeParams.x, u_FadeParams.y, camDist);
        finalColor.a *= nearFade;
    }

    if (u_Flags2.y == 1) { // FarFadeEnabled
        float farFade = 1.0f - smoothstep(u_FadeParams.z, u_FadeParams.w, camDist);
        finalColor.a *= farFade;
    }

    return finalColor;
}
