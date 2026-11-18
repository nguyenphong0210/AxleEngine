#ifndef AXLE_PBR_COMMON_INCLUDED
#define AXLE_PBR_COMMON_INCLUDED

static const float PI = 3.14159265358979323846;
static const float INV_PI = 0.31830988618379067154;

// 1. Trowbridge-Reitz GGX Normal Distribution Function (NDF)
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = saturate(dot(N, H));
    float NdotH2 = NdotH * NdotH;
    float denom = (1.0 - NdotH2) + a2 * NdotH2;
    denom = PI * denom * denom;
    return a2 / max(denom, 1e-12);
}

// 2. Schlick-GGX Geometry Function
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// 3. Smith's Dual Shadowing-Masking Geometry Attenuation
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// 4. Fresnel-Schlick Approximation
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// 5. Fresnel-Schlick with Roughness for Ambient Specular Reflection
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float3 maxRef = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    return F0 + (maxRef - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// =============================================================================
// LTC Rectangular & Disk Area Light Contract (M15-U5-40 .. M15-U5-45)
// Matches Axle::AreaLightLTCContract in Engine/Renderer/src/RenderContracts.cpp.
// =============================================================================
float4 EvaluateLTC1GridPoint(uint roughIdx, uint viewIdx) {
    uint ix = min(roughIdx, 7u);
    uint iy = min(viewIdx, 7u);
    float r = 0.04 + (float(ix) / 7.0) * 0.96;
    float v = float(iy) / 7.0;
    float ndotv = clamp(1.0 - v * v, 0.001, 1.0);
    float thetaV = acos(ndotv);
    float alpha = clamp(r * r, 0.01, 1.0);

    float thetaP = thetaV * (1.0 - alpha);
    float cp = cos(thetaP);
    float sp = sin(thetaP);

    float alphaX = clamp((2.0 * alpha) / (1.0 + alpha), 0.02, 1.0);
    float alphaY = clamp(lerp(alphaX * max(ndotv, 0.25), alphaX, alpha), 0.02, 1.0);
    float ratio = alphaY / max(alphaX, 1e-4);
    return float4(ratio * cp, ratio * sp, -alphaY * sp, alphaY * cp);
}

float4 EvaluateLTC2GridPoint(uint roughIdx, uint viewIdx) {
    uint ix = min(roughIdx, 7u);
    uint iy = min(viewIdx, 7u);
    float r = 0.04 + (float(ix) / 7.0) * 0.96;
    float v = float(iy) / 7.0;
    float ndotv = clamp(1.0 - v * v, 0.001, 1.0);

    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 rv = r * c0 + c1;
    float a004 = min(rv.x * rv.x, exp2(-9.28 * ndotv)) * rv.x + rv.y;
    float2 ab = float2(-1.04, 1.04) * a004 + rv.zw;

    float fresnelTerm = clamp(ab.y, 0.0, 0.99);
    float ggxNorm = clamp(ab.x + ab.y, 0.05, 1.0);
    float sphereFormFactor = 1.0 - 0.1 * v * (1.0 - r);
    return float4(ggxNorm, fresnelTerm, sphereFormFactor, 0.0);
}

float4 SampleLTC1(float roughness, float NdotV) {
    float safeR = clamp(roughness, 0.04, 1.0);
    float safeNV = clamp(NdotV, 0.0, 1.0);
    float u = saturate((safeR - 0.04) / 0.96);
    float v = saturate(sqrt(max(0.0, 1.0 - safeNV)));
    float gx = u * 7.0;
    float gy = v * 7.0;
    uint ix0 = min((uint)floor(gx), 7u);
    uint iy0 = min((uint)floor(gy), 7u);
    uint ix1 = min(ix0 + 1u, 7u);
    uint iy1 = min(iy0 + 1u, 7u);
    float fx = gx - float(ix0);
    float fy = gy - float(iy0);

    float4 c00 = EvaluateLTC1GridPoint(ix0, iy0);
    float4 c10 = EvaluateLTC1GridPoint(ix1, iy0);
    float4 c01 = EvaluateLTC1GridPoint(ix0, iy1);
    float4 c11 = EvaluateLTC1GridPoint(ix1, iy1);
    return lerp(lerp(c00, c10, fx), lerp(c01, c11, fx), fy);
}

float4 SampleLTC2(float roughness, float NdotV) {
    float safeR = clamp(roughness, 0.04, 1.0);
    float safeNV = clamp(NdotV, 0.0, 1.0);
    float u = saturate((safeR - 0.04) / 0.96);
    float v = saturate(sqrt(max(0.0, 1.0 - safeNV)));
    float gx = u * 7.0;
    float gy = v * 7.0;
    uint ix0 = min((uint)floor(gx), 7u);
    uint iy0 = min((uint)floor(gy), 7u);
    uint ix1 = min(ix0 + 1u, 7u);
    uint iy1 = min(iy0 + 1u, 7u);
    float fx = gx - float(ix0);
    float fy = gy - float(iy0);

    float4 c00 = EvaluateLTC2GridPoint(ix0, iy0);
    float4 c10 = EvaluateLTC2GridPoint(ix1, iy0);
    float4 c01 = EvaluateLTC2GridPoint(ix0, iy1);
    float4 c11 = EvaluateLTC2GridPoint(ix1, iy1);
    return lerp(lerp(c00, c10, fx), lerp(c01, c11, fx), fy);
}

float3 LTC_IntegrateEdgeVec(float3 v1, float3 v2) {
    float x = clamp(dot(v1, v2), -0.999999, 0.999999);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float thetaSinTheta = (x > 0.0) ? v : (0.5 * rsqrt(max(1.0 - x * x, 1e-7)) - v);
    return cross(v1, v2) * thetaSinTheta;
}

void LTC_ClipPolygonZ0(float3 inPoly[12], uint inCount, out float3 outPoly[12], out uint outCount) {
    outCount = 0u;
    [unroll] for (uint k = 0u; k < 12u; ++k) {
        outPoly[k] = float3(0.0, 0.0, 0.0);
    }
    if (inCount < 3u) return;

    [loop] for (uint i = 0u; i < inCount; ++i) {
        uint nextIdx = (i + 1u < inCount) ? (i + 1u) : 0u;
        float3 curr = inPoly[i];
        float3 next = inPoly[nextIdx];
        bool currAbove = (curr.z >= 0.0);
        bool nextAbove = (next.z >= 0.0);
        if (currAbove) {
            if (outCount < 12u) outPoly[outCount++] = curr;
            if (!nextAbove && outCount < 12u) {
                float denom = curr.z - next.z;
                float t = (abs(denom) > 1e-7) ? saturate(curr.z / denom) : 0.0;
                float3 clipped = lerp(curr, next, t);
                clipped.z = 0.0;
                outPoly[outCount++] = clipped;
            }
        } else if (nextAbove && outCount < 12u) {
            float denom = next.z - curr.z;
            float t = (abs(denom) > 1e-7) ? saturate(-curr.z / denom) : 0.0;
            float3 clipped = lerp(curr, next, t);
            clipped.z = 0.0;
            outPoly[outCount++] = clipped;
        }
    }
}

float LTC_EvaluateContourFormFactor(float3 upperPoly[12], uint upperCount, float4 ltc1, bool twoSided) {
    if (upperCount < 3u) return 0.0;

    float3 warpedPoly[12];
    [unroll] for (uint w = 0u; w < 12u; ++w) {
        warpedPoly[w] = float3(0.0, 0.0, 0.0);
    }
    [loop] for (uint i = 0u; i < upperCount; ++i) {
        float3 p = upperPoly[i];
        warpedPoly[i] = float3(ltc1.x * p.x + ltc1.y * p.z, p.y, ltc1.z * p.x + ltc1.w * p.z);
    }

    float3 clipped[12];
    uint clippedCount = 0u;
    LTC_ClipPolygonZ0(warpedPoly, upperCount, clipped, clippedCount);
    if (clippedCount < 3u) return 0.0;

    [loop] for (uint j = 0u; j < clippedCount; ++j) {
        float lenSq = dot(clipped[j], clipped[j]);
        clipped[j] = (lenSq > 1e-12) ? (clipped[j] * rsqrt(lenSq)) : float3(0.0, 0.0, 1.0);
    }

    float3 sum = float3(0.0, 0.0, 0.0);
    [loop] for (uint e = 0u; e < clippedCount; ++e) {
        uint eNext = (e + 1u < clippedCount) ? (e + 1u) : 0u;
        sum += LTC_IntegrateEdgeVec(clipped[e], clipped[eNext]);
    }

    float formFactor = sum.z;
    return twoSided ? abs(formFactor) : max(0.0, formFactor);
}

struct LTCAreaLightResult {
    float DiffuseFormFactor;
    float SpecularFormFactor;
    float RangeAttenuation;
    float3 SpecularAlbedo;
    float3 DiffuseLighting;
    float3 SpecularLighting;
    float3 Radiance;
};

LTCAreaLightResult EvaluateLTCAreaLightTerms(
    float4 positionAndWidth,
    float4 directionAndHeight,
    float4 upAndRange,
    float4 colorAndIntensity,
    float3 worldPos,
    float3 N,
    float3 V,
    float3 albedo,
    float metallic,
    float roughness,
    float3 F0,
    float localIntensityScale) {
    LTCAreaLightResult outRes;
    outRes.DiffuseFormFactor = 0.0;
    outRes.SpecularFormFactor = 0.0;
    outRes.RangeAttenuation = 0.0;
    outRes.SpecularAlbedo = float3(0.0, 0.0, 0.0);
    outRes.DiffuseLighting = float3(0.0, 0.0, 0.0);
    outRes.SpecularLighting = float3(0.0, 0.0, 0.0);
    outRes.Radiance = float3(0.0, 0.0, 0.0);

    float3 lightPos = positionAndWidth.xyz;
    float halfWidth = max(positionAndWidth.w * 0.5, 0.005);
    float rawDirLenSq = dot(directionAndHeight.xyz, directionAndHeight.xyz);
    float3 lightDir = (rawDirLenSq > 1e-12) ? (directionAndHeight.xyz * rsqrt(rawDirLenSq)) : float3(0.0, 0.0, -1.0);
    bool isDisk = (directionAndHeight.w < 0.0);
    float halfHeight = max(abs(directionAndHeight.w) * 0.5, 0.005);

    float3 fallbackAxis = (abs(lightDir.y) < 0.99) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 rawUp = upAndRange.xyz - lightDir * dot(upAndRange.xyz, lightDir);
    float upLenSq = dot(rawUp, rawUp);
    float3 lightUp = (upLenSq > 1e-12) ? (rawUp * rsqrt(upLenSq)) : normalize(cross(fallbackAxis, lightDir));
    float3 lightRight = normalize(cross(lightDir, lightUp));
    lightUp = normalize(cross(lightRight, lightDir));

    float range = upAndRange.w;
    float rawIntensity = colorAndIntensity.w;
    bool twoSided = (rawIntensity < 0.0);
    float intensity = abs(rawIntensity);
    if (range <= 1e-3 || intensity <= 0.0) return outRes;

    float dirDot = dot(worldPos - lightPos, lightDir);
    if (!twoSided && dirDot <= 0.0) return outRes;

    // Closest distance to bounded emitter (rectangle or elliptical disk) for range window.
    float3 relToLight = worldPos - lightPos;
    float lu = dot(relToLight, lightRight);
    float lv = dot(relToLight, lightUp);
    if (isDisk) {
        float nx = lu / halfWidth;
        float ny = lv / halfHeight;
        float radial = sqrt(nx * nx + ny * ny);
        if (radial > 1.0) {
            lu = (nx / radial) * halfWidth;
            lv = (ny / radial) * halfHeight;
        }
    } else {
        lu = clamp(lu, -halfWidth, halfWidth);
        lv = clamp(lv, -halfHeight, halfHeight);
    }
    float3 closestPt = lightPos + lightRight * lu + lightUp * lv;
    float closestDist = length(worldPos - closestPt);
    if (closestDist >= range) return outRes;

    float ratio = saturate(closestDist / range);
    float win = saturate(1.0 - ratio * ratio * ratio * ratio);
    float rangeAtten = win * win;
    outRes.RangeAttenuation = rangeAtten;

    // Shading orthonormal LTC frame (T1, T2, N).
    float NdotV = clamp(dot(N, V), 0.001, 1.0);
    float3 tangentRaw = V - N * dot(N, V);
    float tLenSq = dot(tangentRaw, tangentRaw);
    float3 fallbackT = (abs(N.y) < 0.99) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 T1 = (tLenSq > 1e-8) ? (tangentRaw * rsqrt(tLenSq)) : normalize(cross(fallbackT, N));
    float3 T2 = normalize(cross(N, T1));

    float3 shadingPoly[12];
    [unroll] for (uint z = 0u; z < 12u; ++z) {
        shadingPoly[z] = float3(0.0, 0.0, 0.0);
    }
    uint vertCount = 0u;
    if (isDisk) {
        vertCount = 8u;
        const float kOctScale = 1.05392891;
        [unroll] for (uint k = 0u; k < 8u; ++k) {
            float angle = (float(k) + 0.5) * (PI * 0.25);
            float cx = cos(angle) * kOctScale;
            float cy = sin(angle) * kOctScale;
            float3 rel = (lightPos + lightRight * (halfWidth * cx) + lightUp * (halfHeight * cy)) - worldPos;
            shadingPoly[k] = float3(dot(rel, T1), dot(rel, T2), dot(rel, N));
        }
    } else {
        vertCount = 4u;
        float3 p0 = lightPos - lightRight * halfWidth - lightUp * halfHeight - worldPos;
        float3 p1 = lightPos + lightRight * halfWidth - lightUp * halfHeight - worldPos;
        float3 p2 = lightPos + lightRight * halfWidth + lightUp * halfHeight - worldPos;
        float3 p3 = lightPos - lightRight * halfWidth + lightUp * halfHeight - worldPos;
        shadingPoly[0] = float3(dot(p0, T1), dot(p0, T2), dot(p0, N));
        shadingPoly[1] = float3(dot(p1, T1), dot(p1, T2), dot(p1, N));
        shadingPoly[2] = float3(dot(p2, T1), dot(p2, T2), dot(p2, N));
        shadingPoly[3] = float3(dot(p3, T1), dot(p3, T2), dot(p3, N));
    }

    float3 upperPoly[12];
    uint upperCount = 0u;
    LTC_ClipPolygonZ0(shadingPoly, vertCount, upperPoly, upperCount);
    if (upperCount < 3u) return outRes;

    // Diffuse contour integral (upperPoly is already clipped to L.z >= 0).
    float3 diffVec[12];
    [unroll] for (uint d = 0u; d < 12u; ++d) {
        diffVec[d] = float3(0.0, 0.0, 0.0);
    }
    [loop] for (uint i = 0u; i < upperCount; ++i) {
        float lenSq = dot(upperPoly[i], upperPoly[i]);
        diffVec[i] = (lenSq > 1e-12) ? (upperPoly[i] * rsqrt(lenSq)) : float3(0.0, 0.0, 1.0);
    }
    float3 diffSum = float3(0.0, 0.0, 0.0);
    [loop] for (uint e = 0u; e < upperCount; ++e) {
        uint eNext = (e + 1u < upperCount) ? (e + 1u) : 0u;
        diffSum += LTC_IntegrateEdgeVec(diffVec[e], diffVec[eNext]);
    }
    float diffFF = diffSum.z;
    outRes.DiffuseFormFactor = twoSided ? abs(diffFF) : max(0.0, diffFF);

    // Specular contour integral with bilinearly sampled LTC1 inverse matrix & LTC2 directional albedo.
    float4 ltc1 = SampleLTC1(roughness, NdotV);
    float4 ltc2 = SampleLTC2(roughness, NdotV);
    outRes.SpecularFormFactor = LTC_EvaluateContourFormFactor(upperPoly, upperCount, ltc1, twoSided);

    float3 safeF0 = max(F0, float3(0.0, 0.0, 0.0));
    float3 specAlbedo = safeF0 * ltc2.x + max(float3(1.0, 1.0, 1.0) - safeF0, float3(0.0, 0.0, 0.0)) * ltc2.y;
    outRes.SpecularAlbedo = specAlbedo;

    float3 kD = max(float3(1.0, 1.0, 1.0) - specAlbedo, float3(0.0, 0.0, 0.0)) * (1.0 - saturate(metallic));
    outRes.Radiance = max(colorAndIntensity.rgb, float3(0.0, 0.0, 0.0)) * (intensity * rangeAtten * localIntensityScale);
    outRes.DiffuseLighting = kD * max(albedo, float3(0.0, 0.0, 0.0)) * outRes.DiffuseFormFactor;
    outRes.SpecularLighting = specAlbedo * outRes.SpecularFormFactor;
    return outRes;
}

#endif
