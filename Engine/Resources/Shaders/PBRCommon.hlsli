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

// 6. Specular Occlusion & Multi-Bounce Ambient Conservation
float ComputeSpecularOcclusion(float NdotV, float ao, float roughness) {
    float safeAO = saturate(ao);
    float safeNdotV = saturate(NdotV);
    float safeRoughness = clamp(roughness, 0.04, 1.0);
    float exponent = exp2(-16.0 * safeRoughness - 1.0);
    return saturate(pow(max(safeNdotV + safeAO, 1e-5), exponent) - 1.0 + safeAO);
}

float3 ComputeMultiBounceAO(float ao, float3 albedo, float strength) {
    float safeAO = saturate(ao);
    float3 safeAlbedo = saturate(albedo);
    float3 a = 2.0404 * safeAlbedo - 0.3324;
    float3 b = -4.7951 * safeAlbedo + 0.6417;
    float3 c = 2.7552 * safeAlbedo + 0.6903;
    float3 bounced = saturate(max(float3(safeAO, safeAO, safeAO), ((safeAO * a + b) * safeAO + c) * safeAO));
    return lerp(float3(safeAO, safeAO, safeAO), bounced, saturate(strength));
}

// =============================================================================
// LTC Rectangular & Disk Area Light Contract (M15-U5-40 .. M15-U5-45)
// Matches Axle::AreaLightLTCContract in Engine/Renderer/src/RenderContracts.cpp.
// =============================================================================
// The upper 64 rows hold the fitted matrices, the lower 64 their amplitudes.
Texture2D<float4> u_AreaLightLTC : register(t6, space4);

float4 SampleLTCTable(float roughness, float NdotV, uint firstRow) {
    float safeR = clamp(roughness, 0.04, 1.0);
    float safeNV = clamp(NdotV, 0.0, 1.0);
    float u = saturate(safeR);
    float v = saturate(sqrt(max(0.0, 1.0 - safeNV)));
    float gx = u * 63.0;
    float gy = v * 63.0;
    uint ix0 = min((uint)floor(gx), 63u);
    uint iy0 = min((uint)floor(gy), 63u);
    uint ix1 = min(ix0 + 1u, 63u);
    uint iy1 = min(iy0 + 1u, 63u);
    float fx = gx - float(ix0);
    float fy = gy - float(iy0);

    float4 c00 = u_AreaLightLTC.Load(int3(ix0, firstRow + iy0, 0));
    float4 c10 = u_AreaLightLTC.Load(int3(ix1, firstRow + iy0, 0));
    float4 c01 = u_AreaLightLTC.Load(int3(ix0, firstRow + iy1, 0));
    float4 c11 = u_AreaLightLTC.Load(int3(ix1, firstRow + iy1, 0));
    return lerp(lerp(c00, c10, fx), lerp(c01, c11, fx), fy);
}

float4 SampleLTC1(float roughness, float NdotV) {
    return SampleLTCTable(roughness, NdotV, 0);
}

float4 SampleLTC2(float roughness, float NdotV) {
    return SampleLTCTable(roughness, NdotV, 64);
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

void LTC_ClipPolygonZ0(float3 inPoly[36], uint inCount, out float3 outPoly[36], out uint outCount) {
    outCount = 0u;
    [unroll] for (uint k = 0u; k < 36u; ++k) {
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
            if (outCount < 36u) outPoly[outCount++] = curr;
            if (!nextAbove && outCount < 36u) {
                float denom = curr.z - next.z;
                float t = (abs(denom) > 1e-7) ? saturate(curr.z / denom) : 0.0;
                float3 clipped = lerp(curr, next, t);
                clipped.z = 0.0;
                outPoly[outCount++] = clipped;
            }
        } else if (nextAbove && outCount < 36u) {
            float denom = next.z - curr.z;
            float t = (abs(denom) > 1e-7) ? saturate(-curr.z / denom) : 0.0;
            float3 clipped = lerp(curr, next, t);
            clipped.z = 0.0;
            outPoly[outCount++] = clipped;
        }
    }
}

float LTC_EvaluateContourFormFactor(float3 upperPoly[36], uint upperCount, float4 ltc1, bool twoSided) {
    if (upperCount < 3u) return 0.0;

    float3 warpedPoly[36];
    [unroll] for (uint w = 0u; w < 36u; ++w) {
        warpedPoly[w] = float3(0.0, 0.0, 0.0);
    }
    [loop] for (uint i = 0u; i < upperCount; ++i) {
        float3 p = upperPoly[i];
        warpedPoly[i] = float3(ltc1.x * p.x + ltc1.y * p.z, p.y, ltc1.z * p.x + ltc1.w * p.z);
    }

    float3 clipped[36];
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
        if (nx * nx + ny * ny > 1.0) {
            float a2 = halfWidth * halfWidth;
            float b2 = halfHeight * halfHeight;
            float lo = 0.0;
            float hi = length(float2(halfWidth * lu, halfHeight * lv));
            [loop] for (uint iteration = 0u; iteration < 24u; ++iteration) {
                float lambda = (lo + hi) * 0.5;
                float x = halfWidth * lu / (lambda + a2);
                float y = halfHeight * lv / (lambda + b2);
                if (x * x + y * y > 1.0) lo = lambda;
                else hi = lambda;
            }
            lu = a2 * lu / (hi + a2);
            lv = b2 * lv / (hi + b2);
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

    float3 shadingPoly[36];
    [unroll] for (uint z = 0u; z < 36u; ++z) {
        shadingPoly[z] = float3(0.0, 0.0, 0.0);
    }
    uint vertCount = 0u;
    if (isDisk) {
        vertCount = 32u;
        const float kDiskScale = 1.00322263;
        [unroll] for (uint k = 0u; k < 32u; ++k) {
            float angle = (float(k) + 0.5) * (PI * 0.0625);
            float cx = cos(angle) * kDiskScale;
            float cy = sin(angle) * kDiskScale;
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

    float3 upperPoly[36];
    uint upperCount = 0u;
    LTC_ClipPolygonZ0(shadingPoly, vertCount, upperPoly, upperCount);
    if (upperCount < 3u) return outRes;

    // Diffuse contour integral (upperPoly is already clipped to L.z >= 0).
    float3 diffVec[36];
    [unroll] for (uint d = 0u; d < 36u; ++d) {
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

// =============================================================================
// Charlie Sheen Microfacet BRDF & Energy-Conserving Layering (M15-U9-01)
// Estevez & Kulla 2017 ("Production Friendly Microfacet Sheen BRDF")
// =============================================================================
float D_Charlie(float NdotH, float roughness) {
    float r = clamp(roughness, 0.04, 1.0);
    float invAlpha = 1.0 / r;
    float sin2H = max(1.0 - NdotH * NdotH, 0.0078125);
    return (2.0 + invAlpha) / (2.0 * PI) * pow(sin2H, 0.5 * invAlpha);
}

float CharlieLambdaL(float x, float r) {
    float a = (1.0 - r) * (1.0 - r);
    float aP = lerp(21.5473, 25.3245, a);
    float bP = lerp(3.82987, 3.32435, a);
    float cP = lerp(0.19823, 0.16801, a);
    float dP = lerp(-1.97760, -1.27393, a);
    float eP = lerp(-4.32054, -4.85967, a);
    return aP / (1.0 + bP * pow(max(x, 1e-4), cP)) + dP * x + eP;
}

float CharlieLambda(float x, float r) {
    float safeX = clamp(x, 1e-4, 1.0);
    if (safeX < 0.5) {
        return exp(CharlieLambdaL(safeX, r));
    }
    return exp(2.0 * CharlieLambdaL(0.5, r) - CharlieLambdaL(1.0 - safeX, r));
}

float V_Charlie(float NdotL, float NdotV, float roughness) {
    float r = clamp(roughness, 0.04, 1.0);
    float nl = max(NdotL, 1e-4);
    float nv = max(NdotV, 1e-4);
    float lambdaV = CharlieLambda(nv, r);
    float lambdaL = pow(CharlieLambda(nl, r), 1.0 + 2.0 * pow(max(0.0, 1.0 - nl), 8.0));
    return 1.0 / max(4.0 * nl * nv * (1.0 + lambdaV + lambdaL), 1e-4);
}

static const float kCharlieDirectionalAlbedo[81] = {
    0.94686, 0.80236, 0.51774, 0.29026, 0.13585, 0.04851, 0.01113, 0.00107, 0.00000,
    0.77604, 0.71234, 0.52575, 0.35491, 0.21707, 0.11480, 0.04814, 0.01327, 0.00054,
    0.71266, 0.68189, 0.53754, 0.39638, 0.27422, 0.17260, 0.09351, 0.03958, 0.00634,
    0.70532, 0.69193, 0.56630, 0.43882, 0.32503, 0.22493, 0.13973, 0.07394, 0.02236,
    0.73129, 0.72829, 0.60932, 0.48622, 0.37505, 0.27452, 0.18490, 0.11150, 0.04746,
    0.77572, 0.77877, 0.65960, 0.53552, 0.42334, 0.32072, 0.22719, 0.14864, 0.07726,
    0.82342, 0.82936, 0.70687, 0.57972, 0.46529, 0.36033, 0.26385, 0.18221, 0.10728,
    0.85550, 0.86255, 0.73777, 0.60912, 0.49406, 0.38864, 0.29149, 0.20930, 0.13393,
    0.85011, 0.85837, 0.73713, 0.61256, 0.50165, 0.40012, 0.30630, 0.22712, 0.15451
};

float EvaluateCharlieDirectionalAlbedo(float cosTheta, float roughness) {
    float u = saturate(sqrt(saturate(cosTheta))) * 8.0;
    float v = saturate((sqrt(clamp(roughness, 0.04, 1.0)) - 0.2) / 0.8) * 8.0;
    uint x0 = min((uint)floor(u), 7u);
    uint y0 = min((uint)floor(v), 7u);
    float tx = u - float(x0);
    float ty = v - float(y0);
    uint row0 = y0 * 9u;
    uint row1 = (y0 + 1u) * 9u;
    float c00 = kCharlieDirectionalAlbedo[row0 + x0];
    float c10 = kCharlieDirectionalAlbedo[row0 + x0 + 1u];
    float c01 = kCharlieDirectionalAlbedo[row1 + x0];
    float c11 = kCharlieDirectionalAlbedo[row1 + x0 + 1u];
    return saturate(lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty));
}

float3 ApplyCharlieSheenDirect(
    float3 baseLighting,
    float3 N,
    float3 H,
    float NdotL,
    float NdotV,
    float3 sheenColor,
    float sheenRoughness,
    float specularToggle) {
    if (sheenRoughness < 0.0 || NdotL <= 0.0) {
        return baseLighting;
    }
    float3 sc = saturate(sheenColor) * saturate(specularToggle);
    float maxSheen = max(sc.r, max(sc.g, sc.b));
    if (maxSheen <= 0.0) {
        return baseLighting;
    }
    float r = clamp(sheenRoughness, 0.04, 1.0);
    float eV = EvaluateCharlieDirectionalAlbedo(NdotV, r);
    float eL = EvaluateCharlieDirectionalAlbedo(NdotL, r);
    float baseAtten = saturate(1.0 - maxSheen * max(eV, eL));
    float dSheen = D_Charlie(max(dot(N, H), 0.0), r);
    float vSheen = V_Charlie(NdotL, NdotV, r);
    return baseLighting * baseAtten + sc * (dSheen * vSheen * NdotL);
}

void ApplyCharlieSheenDirectSplit(
    inout float3 diffLighting,
    inout float3 specLighting,
    float3 N,
    float3 H,
    float NdotL,
    float NdotV,
    float3 sheenColor,
    float sheenRoughness,
    float specularToggle) {
    if (sheenRoughness < 0.0 || NdotL <= 0.0) {
        return;
    }
    float3 sc = saturate(sheenColor) * saturate(specularToggle);
    float maxSheen = max(sc.r, max(sc.g, sc.b));
    if (maxSheen <= 0.0) {
        return;
    }
    float r = clamp(sheenRoughness, 0.04, 1.0);
    float eV = EvaluateCharlieDirectionalAlbedo(NdotV, r);
    float eL = EvaluateCharlieDirectionalAlbedo(NdotL, r);
    float baseAtten = saturate(1.0 - maxSheen * max(eV, eL));
    float dSheen = D_Charlie(max(dot(N, H), 0.0), r);
    float vSheen = V_Charlie(NdotL, NdotV, r);
    diffLighting *= baseAtten;
    specLighting = specLighting * baseAtten + sc * (dSheen * vSheen * NdotL);
}

float3 ApplyCharlieSheenArea(
    float3 areaBaseLighting,
    float4 positionAndWidth,
    float4 directionAndHeight,
    float4 upAndRange,
    float3 worldPos,
    float3 N,
    float3 V,
    float diffuseFormFactor,
    float3 sheenColor,
    float sheenRoughness,
    float specularToggle) {
    if (sheenRoughness < 0.0 || diffuseFormFactor <= 0.0) {
        return areaBaseLighting;
    }
    float3 sc = saturate(sheenColor) * saturate(specularToggle);
    float maxSheen = max(sc.r, max(sc.g, sc.b));
    if (maxSheen <= 0.0) {
        return areaBaseLighting;
    }
    float3 lightDir = dot(directionAndHeight.xyz, directionAndHeight.xyz) > 1e-12
        ? normalize(directionAndHeight.xyz) : float3(0.0, 0.0, -1.0);
    float3 fallbackAxis = (abs(lightDir.y) < 0.99) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 rawUp = upAndRange.xyz - lightDir * dot(upAndRange.xyz, lightDir);
    float3 lightUp = dot(rawUp, rawUp) > 1e-12 ? normalize(rawUp) : normalize(cross(fallbackAxis, lightDir));
    float3 lightRight = normalize(cross(lightDir, lightUp));
    lightUp = normalize(cross(lightRight, lightDir));

    float2 halfSize = max(float2(positionAndWidth.w, abs(directionAndHeight.w)) * 0.5, 0.005);
    float3 center = positionAndWidth.xyz;
    float3 samplePos = center;
    if (dot(center - worldPos, N) <= 0.0) {
        float2 horizonBias = float2(dot(lightRight, N), dot(lightUp, N)) * max(halfSize.x, halfSize.y);
        if (directionAndHeight.w < 0.0) {
            float rad = length(horizonBias / halfSize);
            if (rad > 1.0) horizonBias /= rad;
        } else {
            horizonBias = clamp(horizonBias, -halfSize, halfSize);
        }
        samplePos = center + lightRight * horizonBias.x + lightUp * horizonBias.y;
    }
    float3 toRep = samplePos - worldPos;
    float repLen = length(toRep);
    if (repLen < 1e-5) {
        return areaBaseLighting;
    }
    float3 L = toRep / repLen;
    float nl = max(dot(N, L), 0.0);
    if (nl <= 0.0) {
        return areaBaseLighting;
    }
    float nv = clamp(dot(N, V), 0.001, 1.0);
    float3 H = normalize(V + L);
    float r = clamp(sheenRoughness, 0.04, 1.0);
    float solidAngle = min(2.0 * PI, PI * diffuseFormFactor / max(nl, 0.05));
    float rawSheen = D_Charlie(max(dot(N, H), 0.0), r) * V_Charlie(nl, nv, r) * nl * solidAngle;
    float eV = EvaluateCharlieDirectionalAlbedo(nv, r);
    float eL = EvaluateCharlieDirectionalAlbedo(nl, r);
    float sheenTerm = min(rawSheen, eV);
    float baseAtten = saturate(1.0 - maxSheen * max(eV, eL));
    return areaBaseLighting * baseAtten + sc * sheenTerm;
}

void ApplyCharlieSheenAreaSplit(
    inout float3 areaDiffLighting,
    inout float3 areaSpecLighting,
    float4 positionAndWidth,
    float4 directionAndHeight,
    float4 upAndRange,
    float3 worldPos,
    float3 N,
    float3 V,
    float diffuseFormFactor,
    float3 sheenColor,
    float sheenRoughness,
    float specularToggle) {
    if (sheenRoughness < 0.0 || diffuseFormFactor <= 0.0) {
        return;
    }
    float3 sc = saturate(sheenColor) * saturate(specularToggle);
    float maxSheen = max(sc.r, max(sc.g, sc.b));
    if (maxSheen <= 0.0) {
        return;
    }
    float3 lightDir = dot(directionAndHeight.xyz, directionAndHeight.xyz) > 1e-12
        ? normalize(directionAndHeight.xyz) : float3(0.0, 0.0, -1.0);
    float3 fallbackAxis = (abs(lightDir.y) < 0.99) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 rawUp = upAndRange.xyz - lightDir * dot(upAndRange.xyz, lightDir);
    float3 lightUp = dot(rawUp, rawUp) > 1e-12 ? normalize(rawUp) : normalize(cross(fallbackAxis, lightDir));
    float3 lightRight = normalize(cross(lightDir, lightUp));
    lightUp = normalize(cross(lightRight, lightDir));

    float2 halfSize = max(float2(positionAndWidth.w, abs(directionAndHeight.w)) * 0.5, 0.005);
    float3 center = positionAndWidth.xyz;
    float3 samplePos = center;
    if (dot(center - worldPos, N) <= 0.0) {
        float2 horizonBias = float2(dot(lightRight, N), dot(lightUp, N)) * max(halfSize.x, halfSize.y);
        if (directionAndHeight.w < 0.0) {
            float rad = length(horizonBias / halfSize);
            if (rad > 1.0) horizonBias /= rad;
        } else {
            horizonBias = clamp(horizonBias, -halfSize, halfSize);
        }
        samplePos = center + lightRight * horizonBias.x + lightUp * horizonBias.y;
    }
    float3 toRep = samplePos - worldPos;
    float repLen = length(toRep);
    if (repLen < 1e-5) {
        return;
    }
    float3 L = toRep / repLen;
    float nl = max(dot(N, L), 0.0);
    if (nl <= 0.0) {
        return;
    }
    float nv = clamp(dot(N, V), 0.001, 1.0);
    float3 H = normalize(V + L);
    float r = clamp(sheenRoughness, 0.04, 1.0);
    float solidAngle = min(2.0 * PI, PI * diffuseFormFactor / max(nl, 0.05));
    float rawSheen = D_Charlie(max(dot(N, H), 0.0), r) * V_Charlie(nl, nv, r) * nl * solidAngle;
    float eV = EvaluateCharlieDirectionalAlbedo(nv, r);
    float eL = EvaluateCharlieDirectionalAlbedo(nl, r);
    float sheenTerm = min(rawSheen, eV);
    float baseAtten = saturate(1.0 - maxSheen * max(eV, eL));
    areaDiffLighting *= baseAtten;
    areaSpecLighting = areaSpecLighting * baseAtten + sc * sheenTerm;
}

void ApplyCharlieSheenIBLSplit(
    inout float3 diffuseAmbient,
    inout float3 specularAmbient,
    inout float3 reflectionEnvironment,
    inout float3 reflectionResponse,
    float3 sheenEnv,
    float NdotV,
    float ao,
    float3 sheenColor,
    float sheenRoughness,
    float envReflectionToggle) {
    if (sheenRoughness < 0.0) {
        return;
    }
    float3 sc = saturate(sheenColor) * saturate(envReflectionToggle);
    float maxSheen = max(sc.r, max(sc.g, sc.b));
    if (maxSheen <= 0.0) {
        return;
    }
    float r = clamp(sheenRoughness, 0.04, 1.0);
    float nv = clamp(NdotV, 0.001, 1.0);
    float eSheen = EvaluateCharlieDirectionalAlbedo(nv, r);
    float baseAtten = saturate(1.0 - maxSheen * eSheen);
    float sheenSpecAO = ComputeSpecularOcclusion(nv, ao, r);
    float3 sheenAmbient = sheenEnv * sc * eSheen * sheenSpecAO;
    diffuseAmbient *= baseAtten;
    specularAmbient = specularAmbient * baseAtten + sheenAmbient;
    reflectionEnvironment = reflectionEnvironment * baseAtten + sheenAmbient;
    reflectionResponse = reflectionResponse * baseAtten + sc * eSheen * sheenSpecAO;
}

void ApplyCharlieSheenIBL(
    inout float3 ambient,
    inout float3 reflectionEnvironment,
    inout float3 reflectionResponse,
    float3 sheenEnv,
    float NdotV,
    float ao,
    float3 sheenColor,
    float sheenRoughness,
    float envReflectionToggle) {
    if (sheenRoughness < 0.0) {
        return;
    }
    float3 sc = saturate(sheenColor) * saturate(envReflectionToggle);
    float maxSheen = max(sc.r, max(sc.g, sc.b));
    if (maxSheen <= 0.0) {
        return;
    }
    float r = clamp(sheenRoughness, 0.04, 1.0);
    float nv = clamp(NdotV, 0.001, 1.0);
    float eSheen = EvaluateCharlieDirectionalAlbedo(nv, r);
    float baseAtten = saturate(1.0 - maxSheen * eSheen);
    float sheenSpecAO = ComputeSpecularOcclusion(nv, ao, r);
    float3 sheenAmbient = sheenEnv * sc * eSheen * sheenSpecAO;
    ambient = ambient * baseAtten + sheenAmbient;
    reflectionEnvironment = reflectionEnvironment * baseAtten + sheenAmbient;
    reflectionResponse = reflectionResponse * baseAtten + sc * eSheen * sheenSpecAO;
}

// =============================================================================
// Beer-Lambert Absorption & Cauchy Wavelength Dispersion (M15-U9-02, M15-U9-03)
// =============================================================================
float EvaluateSnellTransmissionCosine(float NdotV, float ior) {
    float safeIOR = clamp(ior, 1.0, 3.5);
    float invIOR = 1.0 / safeIOR;
    float sin2I = max(0.0, 1.0 - saturate(NdotV) * saturate(NdotV));
    return sqrt(max(0.0, 1.0 - invIOR * invIOR * sin2I));
}

float EvaluateAbsorptionPathLength(float thickness, float NdotV, float ior) {
    float safeThickness = clamp(thickness, 0.0, 100.0);
    float cosThetaT = EvaluateSnellTransmissionCosine(NdotV, ior);
    return safeThickness / max(cosThetaT, 0.20);
}

float3 EvaluateBeerLambertTransmittance(float3 absorptionSigma, float thickness, float NdotV, float ior) {
    float3 safeSigma = max(absorptionSigma, float3(0.0, 0.0, 0.0));
    float opticalDist = EvaluateAbsorptionPathLength(thickness, NdotV, ior);
    return exp(-safeSigma * opticalDist);
}

float3 EvaluateCauchyIORs(float baseIOR, float dispersion) {
    float iorG = clamp(baseIOR, 1.0, 3.0);
    float bEff = (iorG > 1.001) ? (clamp(dispersion, 0.0, 1.0) * 0.25) : 0.0;
    float iorR = clamp(iorG - 0.9389 * bEff, 1.0, 3.5);
    float iorB = clamp(iorG + 1.6325 * bEff, 1.0, 3.5);
    return float3(iorR, iorG, iorB);
}

float3 ComputeSafeRefractRay(float3 V, float3 N, float ior) {
    float eta = 1.0 / clamp(ior, 1.0, 3.5);
    float3 r = refract(-V, N, eta);
    return dot(r, r) > 1e-6 ? normalize(r) : -V;
}

float2 ProjectSnellRefractedUV(
    float3 V,
    float3 N,
    float ior,
    float thickness,
    float refractStrength,
    float3 worldPos,
    float4x4 viewProj,
    float4 startClip,
    float2 screenUV,
    float2 halfPixel) {
    float3 ray = ComputeSafeRefractRay(V, N, ior);
    float4 endClip = mul(viewProj, float4(worldPos + ray * clamp(thickness, 0.0, 100.0), 1.0));
    float2 offset = (endClip.xy / max(endClip.w, 1e-5) - startClip.xy / max(startClip.w, 1e-5)) * float2(0.5, -0.5) * saturate(refractStrength);
    return clamp(screenUV + offset, halfPixel, 1.0 - halfPixel);
}

#endif
