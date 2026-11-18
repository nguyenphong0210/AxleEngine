// =============================================================================
// PlanarReflection.hlsl - Unified D3D12 (DXIL) & Vulkan 1.3 (SPIR-V) Pass
// Unified convex indirect specular resolve & hierarchical fallback:
//   PlanarReflection -> SSR -> LocalReflectionProbe -> SkyIBL
// =============================================================================

cbuffer PlanarReflectionConstants : register(b0, space0) {
    float4x4 u_View;
    float4x4 u_InvView;
    float4x4 u_InvProjection;
    float4x4 u_ReflectedViewProj;
    float4   u_PlaneWorld;         // xyz: unit plane normal, w: plane offset d (dot(N, P) + d)
    float4   u_PlaneOriginExtent;  // xyz: reflector origin WS, w: halfExtentU
    float4   u_PlaneTangentExtent; // xyz: reflector tangent WS, w: halfExtentV
    float4   u_PlaneBitangentFade; // xyz: reflector bitangent WS, w: edgeFadeDistance
    float4   u_PlanarParams;       // x: strength, y: maxRoughness, z: thicknessMeters, w: clipToExtents
    float4   u_PlanarScreenSize;   // x: mainW, y: mainH, z: planarW, w: planarH
    float4   u_Padding[10];        // 160 bytes -> 512 bytes total
};

Texture2D<float>  u_SceneDepth          : register(t0, space1);
Texture2D<float4> u_SceneNormal         : register(t1, space1);
Texture2D<float4> u_OpaqueColor         : register(t2, space1);
Texture2D<float4> u_EnvironmentSpecular : register(t3, space1);
Texture2D<float4> u_ReflectionResponse  : register(t4, space1);
Texture2D<float4> u_PlanarColor         : register(t5, space1);
SamplerState      u_LinearSampler       : register(s0, space1);

struct VSOutput {
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

float3 SanitizeHDRColor(float3 c) {
    bool valid = isfinite(c.r) && isfinite(c.g) && isfinite(c.b);
    return valid ? clamp(c, 0.0, 65504.0) : float3(0.0, 0.0, 0.0);
}

VSOutput VSMain(uint id : SV_VertexID) {
    VSOutput output;
    float2 uv = float2((id << 1) & 2, id & 2);
    output.TexCoord = uv;
    output.Position = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target {
    int2 pixelCoord = int2(input.Position.xy);

    float depth = u_SceneDepth.Load(int3(pixelCoord, 0));
    if (!isfinite(depth) || depth >= 0.99999) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    float4 envTerm = u_EnvironmentSpecular.Load(int3(pixelCoord, 0));
    float signedRoughness = envTerm.a;
    if (!isfinite(signedRoughness) || signedRoughness < -0.5) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    float3 response = SanitizeHDRColor(u_ReflectionResponse.Load(int3(pixelCoord, 0)).rgb);
    if (max(response.r, max(response.g, response.b)) <= 1e-5) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    float4 normalEncoded = u_SceneNormal.Load(int3(pixelCoord, 0));
    if (normalEncoded.w < 0.5 || !isfinite(normalEncoded.x) || !isfinite(normalEncoded.y) || !isfinite(normalEncoded.z)) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    float normalLenSq = dot(normalEncoded.xyz, normalEncoded.xyz);
    if (normalLenSq <= 1e-6) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    float3 normalWS = normalEncoded.xyz * rsqrt(normalLenSq);

    // Single-sided mirror normal alignment gate matching CPU PlanarReflectionContract.
    float3 planeNormal = normalize(u_PlaneWorld.xyz);
    float normalDot = dot(normalWS, planeNormal);
    float normalWeight = smoothstep(0.95, 0.995, normalDot);
    if (normalWeight <= 1e-4) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    float2 ndc = float2(input.TexCoord.x * 2.0 - 1.0, (1.0 - input.TexCoord.y) * 2.0 - 1.0);
    float4 viewPosH = mul(u_InvProjection, float4(ndc, depth, 1.0));
    if (!isfinite(viewPosH.w) || abs(viewPosH.w) <= 1e-5) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    float3 viewPos = viewPosH.xyz / viewPosH.w;
    float3 worldPos = mul(u_InvView, float4(viewPos, 1.0)).xyz;
    if (!isfinite(worldPos.x) || !isfinite(worldPos.y) || !isfinite(worldPos.z)) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    // Distance-to-plane thickness gate: reject foreground/background geometry outside mirror surface.
    float planeDist = abs(dot(planeNormal, worldPos) + u_PlaneWorld.w);
    float thickness = max(u_PlanarParams.z, 0.002 * length(viewPos));
    float planeWeight = 1.0 - smoothstep(thickness * 0.5, thickness, planeDist);
    if (planeWeight <= 1e-4) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    // Reflector quad extent gate and soft edge fade toward fallback hierarchy.
    float extentWeight = 1.0;
    if (u_PlanarParams.w > 0.5) {
        float3 delta = worldPos - u_PlaneOriginExtent.xyz;
        float uDist = abs(dot(delta, u_PlaneTangentExtent.xyz));
        float vDist = abs(dot(delta, u_PlaneBitangentFade.xyz));
        float uMax = max(u_PlaneOriginExtent.w, 1e-4);
        float vMax = max(u_PlaneTangentExtent.w, 1e-4);
        if (uDist > uMax || vDist > vMax) {
            return float4(0.0, 0.0, 0.0, 0.0);
        }
        float fadeDist = max(u_PlaneBitangentFade.w, 0.0);
        if (fadeDist > 1e-5) {
            float margin = min(uMax - uDist, vMax - vDist);
            extentWeight = smoothstep(0.0, fadeDist, margin);
        }
    }
    if (extentWeight <= 1e-4) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    // Roughness rolloff toward SSR -> LocalProbe -> SkyIBL.
    float roughness = clamp(abs(signedRoughness), 0.0, 1.0);
    float maxRough = clamp(u_PlanarParams.y, 0.02, 1.0);
    float fadeStart = maxRough * 0.65;
    float roughnessWeight = 1.0 - smoothstep(fadeStart, maxRough, roughness);
    if (roughnessWeight <= 1e-4) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    // Project mirror surface point into mirrored capture UV coordinates.
    float4 refClip = mul(u_ReflectedViewProj, float4(worldPos, 1.0));
    if (!isfinite(refClip.w) || refClip.w <= 1e-5) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    float2 refNdc = refClip.xy / refClip.w;
    float2 refUv = float2(refNdc.x * 0.5 + 0.5, 0.5 - refNdc.y * 0.5);
    if (any(refUv < 0.0) || any(refUv > 1.0)) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    float edgeDist = min(min(refUv.x, 1.0 - refUv.x), min(refUv.y, 1.0 - refUv.y));
    float screenBorderWeight = smoothstep(0.0, 0.03, edgeDist);

    float wPlanar = saturate(u_PlanarParams.x * normalWeight * planeWeight * extentWeight * roughnessWeight * screenBorderWeight);
    if (wPlanar <= 1e-4) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    // Sample planar target with roughness-aware narrow blur kernel for glossy reflectors.
    float2 planarTexSize = max(u_PlanarScreenSize.zw, float2(1.0, 1.0));
    float2 planarTexel = 1.0 / planarTexSize;
    float2 safeUv = clamp(refUv, planarTexel * 0.5, 1.0 - planarTexel * 0.5);
    float3 centerRadiance = SanitizeHDRColor(u_PlanarColor.SampleLevel(u_LinearSampler, safeUv, 0.0).rgb);

    float roughBlur = saturate((roughness - 0.03) / max(maxRough - 0.03, 0.01));
    float3 planarRadiance = centerRadiance;
    if (roughBlur > 1e-3) {
        float2 o = planarTexel * (1.35 * roughBlur);
        float3 s0 = SanitizeHDRColor(u_PlanarColor.SampleLevel(u_LinearSampler, clamp(safeUv + float2( o.x,  0.0), planarTexel * 0.5, 1.0 - planarTexel * 0.5), 0.0).rgb);
        float3 s1 = SanitizeHDRColor(u_PlanarColor.SampleLevel(u_LinearSampler, clamp(safeUv + float2(-o.x,  0.0), planarTexel * 0.5, 1.0 - planarTexel * 0.5), 0.0).rgb);
        float3 s2 = SanitizeHDRColor(u_PlanarColor.SampleLevel(u_LinearSampler, clamp(safeUv + float2( 0.0,  o.y), planarTexel * 0.5, 1.0 - planarTexel * 0.5), 0.0).rgb);
        float3 s3 = SanitizeHDRColor(u_PlanarColor.SampleLevel(u_LinearSampler, clamp(safeUv + float2( 0.0, -o.y), planarTexel * 0.5, 1.0 - planarTexel * 0.5), 0.0).rgb);
        float3 blurred = 0.25 * (centerRadiance) + 0.1875 * (s0 + s1 + s2 + s3);
        planarRadiance = lerp(centerRadiance, blurred, roughBlur);
    }

    // Preserve direct + diffuse + emission (`baseLit = OpaqueColor - Environment`) and compose
    // convexly over current HDR buffer (`baseLit + prevSpec`) via hardware AlphaBlend:
    //   dst_new = lerp(dst_prev, baseLit + planarRadiance * response, wPlanar)
    float3 envSpec = SanitizeHDRColor(envTerm.rgb);
    float3 opaqueColor = SanitizeHDRColor(u_OpaqueColor.Load(int3(pixelCoord, 0)).rgb);
    float3 baseLit = max(opaqueColor - envSpec, 0.0);
    float3 planarSpec = planarRadiance * response;
    float3 composed = SanitizeHDRColor(baseLit + planarSpec);
    return float4(composed, wPlanar);
}


// Resample a lower-resolution capture into a fresh unpublished destination.
float4 PSCopyMain(VSOutput input) : SV_Target {
    return float4(SanitizeHDRColor(u_PlanarColor.SampleLevel(u_LinearSampler, input.TexCoord, 0.0).rgb), 1.0);
}
