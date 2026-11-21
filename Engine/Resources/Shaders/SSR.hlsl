// =============================================================================
// AxleEngine - Hierarchical Hi-Z Screen Space Reflections & Material Resolve
// Dual-compiled for Direct3D 12 (DXIL 6.0) & Vulkan 1.3 (SPIR-V)
// Hierarchical Hi-Z DDA traversal, HDR color pyramid downsampling, and
// roughness-cone material resolve over exported forward reflection terms.
// =============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

cbuffer SSRConstants : register(b0, space0) {
    float4x4 u_View;
    float4x4 u_InvView;
    float4x4 u_Projection;
    float4x4 u_InvProjection;
    float4   u_ScreenSize;      // (width, height, 1.0 / width, 1.0 / height)
    float4   u_SSRParams;       // (max distance m, steps, strength, thickness)
    float4   u_ProbeBoxMin;     // (xyz: box min, w: parallax corrected 1.0 or 0.0)
    float4   u_ProbeBoxMax;     // (xyz: box max, w: probe intensity)
    float4   u_ProbePosition;   // (xyz: probe center, w: has probe 1.0 or 0.0)
    float4   u_SSRExtra;        // (maxUsefulHiZMip, maxConeRadiusPx, reserved, reserved)
    float4   u_Padding[10];     // Total 512 bytes aligned
};

cbuffer SSRDownsampleConstants : register(b0, space0) {
    float4 u_DownsampleSourceSize; // width, height, 1/width, 1/height
    float4 u_DownsampleDestSize;   // width, height, 1/width, 1/height
    float4 u_DownsampleParams;     // x: isSourceFullRes, y: mipLevel
    float4 u_DownsampleReserved[13];
};

// -----------------------------------------------------------------------------
// Common Math and Projection Contracts (Parity with SSRRaymarchContract)
// -----------------------------------------------------------------------------
float3 ReconstructViewPos(float2 uv, float depth) {
    float4 clip = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(u_InvProjection, clip);
    return viewPos.xyz / max(abs(viewPos.w), 1e-6);
}

float3 ProjectViewToUVZ(float3 viewPos) {
    float4 clip = mul(u_Projection, float4(viewPos, 1.0));
    float invW = 1.0 / max(abs(clip.w), 1e-6);
    float3 ndc = clip.xyz * invW;
    return float3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
}

bool ClipScreenRay(float3 startUVZ, float3 endUVZ, out float outTMax) {
    outTMax = 1.0;
    if (!all(isfinite(startUVZ)) || !all(isfinite(endUVZ)) || any(startUVZ < 0.0) ||
        any(startUVZ.xy > 1.0) || startUVZ.z > 0.9999) {
        outTMax = 0.0;
        return false;
    }
    float tMin = 0.0;
    float tMax = 1.0;
    float3 d = endUVZ - startUVZ;
    const float eps = 1e-7;
    float mins[3] = { 0.0, 0.0, 0.0 };
    float maxs[3] = { 1.0, 1.0, 0.9999 };
    [unroll]
    for (int c = 0; c < 3; ++c) {
        if (abs(d[c]) < eps) {
            if (startUVZ[c] < mins[c] || startUVZ[c] > maxs[c]) {
                outTMax = 0.0;
                return false;
            }
        } else {
            float t0 = (mins[c] - startUVZ[c]) / d[c];
            float t1 = (maxs[c] - startUVZ[c]) / d[c];
            if (t0 > t1) {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            tMin = max(tMin, t0);
            tMax = min(tMax, t1);
            if (tMin > tMax) {
                outTMax = 0.0;
                return false;
            }
        }
    }
    outTMax = clamp(tMax, 0.0, 1.0);
    return outTMax > 1e-5;
}

uint2 HiZMipDimensions(uint2 baseSize, uint mip) {
    return max(uint2(1u, 1u), baseSize >> min(mip, 15u));
}

float3 SanitizeHDRColor(float3 c) {
    return (all(isfinite(c))) ? clamp(c, 0.0, 65000.0) : float3(0.0, 0.0, 0.0);
}

// =============================================================================
// Compute Pass A: HDR Color Pyramid Downsampling (SSRColor0..3)
// =============================================================================
Texture2D<float4>   u_DownsampleSource  : register(t0, space1);
SamplerState        u_DownsampleSampler : register(s0, space1);
RWTexture2D<float4> u_DownsampleOutput  : register(u0, space2);

[numthreads(8, 8, 1)]
void DownsampleColorCS(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 dstPixel = dispatchThreadId.xy;
    uint2 dstSize = max(uint2(u_DownsampleDestSize.xy), uint2(1u, 1u));
    if (dstPixel.x >= dstSize.x || dstPixel.y >= dstSize.y) return;

    float2 srcRcp = u_DownsampleSourceSize.zw;
    float2 uv = (float2(dstPixel) + 0.5) * u_DownsampleDestSize.zw;

    // 5-tap + 4-tap Jimenez-style anti-firefly bilinear footprint downsample
    float3 c  = SanitizeHDRColor(u_DownsampleSource.SampleLevel(u_DownsampleSampler, uv, 0.0).rgb);
    float3 d0 = SanitizeHDRColor(u_DownsampleSource.SampleLevel(u_DownsampleSampler, uv + float2(-1.0, -1.0) * srcRcp, 0.0).rgb);
    float3 d1 = SanitizeHDRColor(u_DownsampleSource.SampleLevel(u_DownsampleSampler, uv + float2( 1.0, -1.0) * srcRcp, 0.0).rgb);
    float3 d2 = SanitizeHDRColor(u_DownsampleSource.SampleLevel(u_DownsampleSampler, uv + float2(-1.0,  1.0) * srcRcp, 0.0).rgb);
    float3 d3 = SanitizeHDRColor(u_DownsampleSource.SampleLevel(u_DownsampleSampler, uv + float2( 1.0,  1.0) * srcRcp, 0.0).rgb);
    float3 e0 = SanitizeHDRColor(u_DownsampleSource.SampleLevel(u_DownsampleSampler, uv + float2(-2.0,  0.0) * srcRcp, 0.0).rgb);
    float3 e1 = SanitizeHDRColor(u_DownsampleSource.SampleLevel(u_DownsampleSampler, uv + float2( 2.0,  0.0) * srcRcp, 0.0).rgb);
    float3 e2 = SanitizeHDRColor(u_DownsampleSource.SampleLevel(u_DownsampleSampler, uv + float2( 0.0, -2.0) * srcRcp, 0.0).rgb);
    float3 e3 = SanitizeHDRColor(u_DownsampleSource.SampleLevel(u_DownsampleSampler, uv + float2( 0.0,  2.0) * srcRcp, 0.0).rgb);

    float3 avgCenter = (d0 + d1 + d2 + d3) * 0.25;
    float3 avgCross  = (e0 + e1 + e2 + e3 + c) * 0.2;
    float3 filtered  = lerp(avgCross, avgCenter, 0.55);
    u_DownsampleOutput[dstPixel] = float4(SanitizeHDRColor(filtered), 1.0);
}

// =============================================================================
// Compute Pass B: Hierarchical Hi-Z Ray Traversal (CSMain)
// =============================================================================

// Keep each Hi-Z level as a statically bound texture for portable DX12/Vulkan reflection.
Texture2D<float>    u_HiZ0           : register(t0, space1);
Texture2D<float>    u_HiZ1           : register(t1, space1);
Texture2D<float>    u_HiZ2           : register(t2, space1);
Texture2D<float>    u_HiZ3           : register(t3, space1);
Texture2D<float>    u_HiZ4           : register(t4, space1);
Texture2D<float>    u_HiZ5           : register(t5, space1);
Texture2D<float>    u_HiZ6           : register(t6, space1);
Texture2D<float>    u_HiZ7           : register(t7, space1);
Texture2D<float4>   u_ComputeNormal  : register(t8, space1);
Texture2D<float4>   u_ComputeColor   : register(t9, space1);
SamplerState        u_ComputeSampler : register(s0, space1);
RWTexture2D<float4> u_ComputeOutput  : register(u0, space2);

float LoadHiZTexel(uint mip, uint2 cell, uint2 baseSize) {
    uint2 dims = HiZMipDimensions(baseSize, mip);
    int3 coord = int3(min(cell, dims - 1u), 0);
    switch (min(mip, 7u)) {
        case 0u: return u_HiZ0.Load(coord);
        case 1u: return u_HiZ1.Load(coord);
        case 2u: return u_HiZ2.Load(coord);
        case 3u: return u_HiZ3.Load(coord);
        case 4u: return u_HiZ4.Load(coord);
        case 5u: return u_HiZ5.Load(coord);
        case 6u: return u_HiZ6.Load(coord);
        default: return u_HiZ7.Load(coord);
    }
}

bool TrySampleContinuousHiZ0Depth(float2 sampleUV, uint2 baseSize, float safeThickness, out float outDepth) {
    outDepth = 1.0;
    float2 tc = sampleUV * float2(baseSize) - 0.5;
    int maxX = int(baseSize.x) - 1;
    int maxY = int(baseSize.y) - 1;
    int x0 = clamp(int(floor(tc.x)), 0, maxX);
    int y0 = clamp(int(floor(tc.y)), 0, maxY);
    int x1 = min(x0 + 1, maxX);
    int y1 = min(y0 + 1, maxY);
    float fx = saturate(tc.x - float(x0));
    float fy = saturate(tc.y - float(y0));
    float d00 = LoadHiZTexel(0u, uint2(x0, y0), baseSize);
    float d10 = LoadHiZTexel(0u, uint2(x1, y0), baseSize);
    float d01 = LoadHiZTexel(0u, uint2(x0, y1), baseSize);
    float d11 = LoadHiZTexel(0u, uint2(x1, y1), baseSize);
    float dMin = min(min(d00, d10), min(d01, d11));
    float dMax = max(max(d00, d10), max(d01, d11));
    if (dMax >= 0.9999 || dMin <= 0.0) return false;
    float zAtMin = ReconstructViewPos(sampleUV, dMin).z;
    float zAtMax = ReconstructViewPos(sampleUV, dMax).z;
    float maxContDiff = min(abs(zAtMin) * 0.08 + 0.05, min(safeThickness * 0.65, 0.35));
    if (abs(zAtMin - zAtMax) > maxContDiff) return false;
    outDepth = lerp(lerp(d00, d10, fx), lerp(d01, d11, fx), fy);
    return true;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint2 pixelPos = dispatchThreadId.xy;
    uint2 baseSize = max(uint2(u_ScreenSize.xy), uint2(1u, 1u));
    if (pixelPos.x >= baseSize.x || pixelPos.y >= baseSize.y) return;

    float2 rcpFrame = u_ScreenSize.zw;
    float2 uv = (float2(pixelPos) + 0.5) * rcpFrame;

    float depth = u_HiZ0.Load(int3(pixelPos, 0));
    if (depth >= 0.9999 || depth <= 0.0) {
        u_ComputeOutput[pixelPos] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 originView = ReconstructViewPos(uv, depth);

    // Determine normal in view space (authored world normal or edge-aware depth normal)
    float4 normalSample = u_ComputeNormal.Load(int3(pixelPos, 0));
    float3 viewNormal;
    if (dot(normalSample.xyz, normalSample.xyz) > 0.01) {
        viewNormal = normalize(mul((float3x3)u_View, normalSample.xyz));
    } else {
        float3 pR = ReconstructViewPos(uv + float2(rcpFrame.x, 0.0), u_HiZ0.SampleLevel(u_ComputeSampler, uv + float2(rcpFrame.x, 0.0), 0.0));
        float3 pL = ReconstructViewPos(uv - float2(rcpFrame.x, 0.0), u_HiZ0.SampleLevel(u_ComputeSampler, uv - float2(rcpFrame.x, 0.0), 0.0));
        float3 pD = ReconstructViewPos(uv + float2(0.0, rcpFrame.y), u_HiZ0.SampleLevel(u_ComputeSampler, uv + float2(0.0, rcpFrame.y), 0.0));
        float3 pU = ReconstructViewPos(uv - float2(0.0, rcpFrame.y), u_HiZ0.SampleLevel(u_ComputeSampler, uv - float2(0.0, rcpFrame.y), 0.0));
        float3 dx = (abs(pR.z - originView.z) < abs(pL.z - originView.z)) ? (pR - originView) : (originView - pL);
        float3 dy = (abs(pD.z - originView.z) < abs(pU.z - originView.z)) ? (pD - originView) : (originView - pU);
        viewNormal = normalize(cross(dy, dx));
        if (dot(viewNormal, -normalize(originView)) < 0.0) {
            viewNormal = -viewNormal;
        }
    }

    float3 viewDir = normalize(originView);
    float3 reflDir = normalize(reflect(viewDir, viewNormal));
    if (dot(reflDir, viewNormal) <= 1e-4) {
        u_ComputeOutput[pixelPos] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Offset origin along geometric normal proportional to pixel world footprint to prevent curved self-hits
    float texelWorldSize = abs(originView.z) * max(
        rcpFrame.x / max(abs(u_Projection[0][0]), 1e-4),
        rcpFrame.y / max(abs(u_Projection[1][1]), 1e-4));
    float3 startView = originView + viewNormal * max(0.0015, texelWorldSize * 1.5);

    float nearViewZ = abs(ReconstructViewPos(float2(0.5, 0.5), 0.0).z);
    float safeNear = max(nearViewZ * 1.25, 0.05);
    if (startView.z >= -safeNear) {
        u_ComputeOutput[pixelPos] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float maxDist = max(u_SSRParams.x, 5.0);
    float traceDist = maxDist;
    if (reflDir.z > 1e-5) {
        traceDist = min(traceDist, (-safeNear - startView.z) / reflDir.z);
    }
    if (traceDist <= 0.02) {
        u_ComputeOutput[pixelPos] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 endView = startView + reflDir * traceDist;
    float3 rawStartUVZ = ProjectViewToUVZ(startView);
    float3 rawEndUVZ = ProjectViewToUVZ(endView);
    float tMaxClip = 1.0;
    if (!ClipScreenRay(rawStartUVZ, rawEndUVZ, tMaxClip)) {
        u_ComputeOutput[pixelPos] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 startUVZ = rawStartUVZ;
    float3 endUVZ = lerp(rawStartUVZ, rawEndUVZ, tMaxClip);
    float3 dirUVZ = endUVZ - startUVZ;

    float pxLen = max(1.0, length(dirUVZ.xy * float2(baseSize)));
    float minOriginT = min(0.5, 1.25 / pxLen);
    float tCurr = minOriginT;
    uint maxMip = clamp((uint)u_SSRExtra.x, 0u, 7u);
    uint mip = 0u;
    uint maxSteps = clamp((uint)u_SSRParams.y, 16u, 96u);
    float thickness = max(u_SSRParams.w, 0.05);

    float2 hitUV = float2(0.0, 0.0);
    float hitDistNorm = 0.0;
    float confidence = 0.0;

    [loop]
    for (uint step = 0; step < maxSteps && tCurr < 1.0; ++step) {
        float3 pCurr = startUVZ + dirUVZ * tCurr;
        if (any(pCurr.xy < 0.0) || any(pCurr.xy > 1.0) || pCurr.z >= 0.9999) {
            break;
        }

        uint2 dims = HiZMipDimensions(baseSize, mip);
        // Match HiZ.hlsl: odd trailing rows/columns fold into the last cell.
        float cellScale = float(1u << mip);
        float2 cellF = clamp(pCurr.xy * float2(baseSize) / cellScale, float2(0.0, 0.0), float2(dims) - 1e-4);
        uint2 cellIdx = uint2(floor(cellF));

        float2 stepSign = float2(dirUVZ.x >= 0.0 ? 1.0 : 0.0, dirUVZ.y >= 0.0 ? 1.0 : 0.0);
        float2 crossOffset = float2(dirUVZ.x >= 0.0 ? 1e-4 : -1e-4, dirUVZ.y >= 0.0 ? 1e-4 : -1e-4);
        float2 boundary = (float2(cellIdx) + stepSign) * cellScale;
        if (dirUVZ.x >= 0.0 && cellIdx.x + 1u == dims.x) boundary.x = baseSize.x;
        if (dirUVZ.y >= 0.0 && cellIdx.y + 1u == dims.y) boundary.y = baseSize.y;
        float2 nextEdge = boundary * rcpFrame + crossOffset * rcpFrame;
        float tx = abs(dirUVZ.x) > 1e-7 ? (nextEdge.x - startUVZ.x) / dirUVZ.x : 1.0;
        float ty = abs(dirUVZ.y) > 1e-7 ? (nextEdge.y - startUVZ.y) / dirUVZ.y : 1.0;
        float tNextCell = clamp(max(tCurr + 1e-5, min(tx, ty)), tCurr, 1.0);

        float zEnter = startUVZ.z + dirUVZ.z * tCurr;
        float zExit = startUVZ.z + dirUVZ.z * tNextCell;
        float rayZMin = min(zEnter, zExit);
        float rayZMax = max(zEnter, zExit);
        float cellMinZ = LoadHiZTexel(mip, cellIdx, baseSize);

        // Ray span in this cell is strictly in front of closest blocker (or cell is sky) -> advance & ascend
        if (rayZMax < cellMinZ || cellMinZ >= 0.9999) {
            tCurr = tNextCell;
            mip = min(mip + 1u, maxMip);
            continue;
        }

        // Potential intersection at coarse level -> descend to refine
        if (mip > 0u) {
            if (zEnter < cellMinZ && dirUVZ.z > 1e-7) {
                tCurr = clamp((cellMinZ - startUVZ.z) / dirUVZ.z, tCurr, tNextCell);
            }
            --mip;
            continue;
        }

        // Mip 0: view-space thickness test and exact cell-plane hit refinement
        float2 centerUV = (float2(cellIdx) + 0.5) * rcpFrame;
        float3 sceneView = ReconstructViewPos(centerUV, cellMinZ);
        float3 rayEnterView = ReconstructViewPos(pCurr.xy, zEnter);
        float3 pExit = startUVZ + dirUVZ * tNextCell;
        float3 rayExitView = ReconstructViewPos(pExit.xy, zExit);
        float penEnter = sceneView.z - rayEnterView.z;
        float penExit = sceneView.z - rayExitView.z;
        bool crosses = (rayZMin <= cellMinZ && rayZMax >= cellMinZ);
        bool withinBehind = (min(penEnter, penExit) <= thickness && max(penEnter, penExit) > -1e-4);

        if (crosses || withinBehind) {
            // Mip-0 depth is constant within this cell. Exact UVZ-plane
            // refinement works for both increasing and decreasing depth rays.
            float tHit = crosses && abs(dirUVZ.z) > 1e-7
                ? clamp((cellMinZ-startUVZ.z)/dirUVZ.z,tCurr,tNextCell)
                : tCurr;
            float3 pFinal = startUVZ + dirUVZ * tHit;
            uint2 fCell = uint2(clamp(pFinal.xy * float2(baseSize), float2(0.0, 0.0), float2(baseSize) - 1e-4));
            float finalSceneZ = LoadHiZTexel(0u, fCell, baseSize);
            if (finalSceneZ < 0.9999) {
                float3 finalSceneView = ReconstructViewPos(pFinal.xy, finalSceneZ);
                float3 finalRayView = ReconstructViewPos(pFinal.xy, pFinal.z);
                float finalPen = finalSceneView.z - finalRayView.z;
                if (finalPen <= thickness && finalPen >= -0.05) {
                    // Continuous sub-texel refinement across cliff-free mip-0 surfaces to eliminate
                    // discrete texel-row staircase beats and near-contact ripples.
                    float tA = max(minOriginT, tCurr - 1.25 / pxLen);
                    float tB = tNextCell;
                    float3 pA = startUVZ + dirUVZ * tA;
                    float3 pB = startUVZ + dirUVZ * tB;
                    float contDA = 0.0;
                    float contDB = 0.0;
                    if (tB > tA + 1e-6 &&
                        TrySampleContinuousHiZ0Depth(pA.xy, baseSize, thickness, contDA) &&
                        TrySampleContinuousHiZ0Depth(pB.xy, baseSize, thickness, contDB)) {
                        float zSceneA = ReconstructViewPos(pA.xy, contDA).z;
                        float zSceneB = ReconstructViewPos(pB.xy, contDB).z;
                        float gapA = ReconstructViewPos(pA.xy, pA.z).z - zSceneA;
                        float gapB = ReconstructViewPos(pB.xy, pB.z).z - zSceneB;
                        if (abs(zSceneA - zSceneB) <= thickness &&
                            gapA * gapB <= 0.0 &&
                            abs(gapA) <= thickness &&
                            abs(gapB) <= thickness &&
                            abs(gapA - gapB) > 1e-6) {
                            [unroll]
                            for (int b = 0; b < 4; ++b) {
                                float tMid = 0.5 * (tA + tB);
                                float3 pMid = startUVZ + dirUVZ * tMid;
                                float dMid = 0.0;
                                if (!TrySampleContinuousHiZ0Depth(pMid.xy, baseSize, thickness, dMid)) break;
                                float gapMid = ReconstructViewPos(pMid.xy, pMid.z).z - ReconstructViewPos(pMid.xy, dMid).z;
                                if ((gapMid >= 0.0) == (gapA >= 0.0)) {
                                    tA = tMid;
                                    gapA = gapMid;
                                } else {
                                    tB = tMid;
                                    gapB = gapMid;
                                }
                            }
                            float alpha = saturate(abs(gapA) / max(abs(gapA - gapB), 1e-6));
                            float tRefined = tA + (tB - tA) * alpha;
                            float3 pRefined = startUVZ + dirUVZ * tRefined;
                            float dRefined = 0.0;
                            if (TrySampleContinuousHiZ0Depth(pRefined.xy, baseSize, thickness, dRefined)) {
                                float3 refinedSceneView = ReconstructViewPos(pRefined.xy, dRefined);
                                float3 refinedRayView = ReconstructViewPos(pRefined.xy, pRefined.z);
                                float refinedPen = refinedSceneView.z - refinedRayView.z;
                                if (refinedPen <= thickness && refinedPen >= -0.05) {
                                    pFinal = pRefined;
                                    finalSceneZ = dRefined;
                                    finalSceneView = refinedSceneView;
                                    fCell = uint2(clamp(pFinal.xy * float2(baseSize), float2(0.0, 0.0), float2(baseSize) - 1e-4));
                                }
                            }
                        }
                    }

                    // Edge & disocclusion support check across 4 orthogonal neighbor texels
                    float supportSum = 0.0;
                    int2 fInt = int2(fCell);
                    int2 maxInt = int2(baseSize) - int2(1, 1);
                    int2 offsets[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
                    float edgeTol = max(thickness * 1.5, 0.35);
                    [unroll]
                    for (int n = 0; n < 4; ++n) {
                        uint2 nCell = uint2(clamp(fInt + offsets[n], int2(0, 0), maxInt));
                        float nDepth = LoadHiZTexel(0u, nCell, baseSize);
                        if (nDepth < 0.9999) {
                            float nZ = ReconstructViewPos((float2(nCell) + 0.5) * rcpFrame, nDepth).z;
                            supportSum += 1.0 - smoothstep(edgeTol * 0.35, edgeTol * 1.35, abs(finalSceneView.z - nZ));
                        }
                    }
                    float disocclusionFade = lerp(0.25, 1.0, smoothstep(0.2, 2.2, supportSum));

                    float2 dCoords = smoothstep(0.0, 0.12, pFinal.xy) * (1.0 - smoothstep(0.88, 1.0, pFinal.xy));
                    float screenFade = saturate(dCoords.x * dCoords.y);
                    float hitDistMeters = length(finalSceneView - originView);
                    float stepFade = saturate(float(int(maxSteps) - int(step)) / 6.0);
                    float distFade = saturate(1.0 - (hitDistMeters / maxDist) * 0.25) * stepFade;
                    float cameraFade = saturate(1.0 - smoothstep(0.45, 0.98, reflDir.z));

                    hitUV = pFinal.xy;
                    hitDistNorm = saturate(hitDistMeters / maxDist);
                    confidence = saturate(screenFade * distFade * cameraFade * disocclusionFade);
                    break;
                }
            }
        }

        tCurr = tNextCell;
        mip = 0u;
    }

    // Store normalized traversal output in [0, 1] so both RGBA16_FLOAT and RGBA8_UNORM views agree cleanly.
    u_ComputeOutput[pixelPos] = float4(hitUV, hitDistNorm, confidence);
}

// =============================================================================
// Fullscreen Material-Aware Roughness-Cone Composite Pass (VSMain & PSMain)
// =============================================================================

struct VSOutput {
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    output.TexCoord = float2(uv.x, 1.0f - uv.y);
    return output;
}

Texture2D<float>  u_SceneDepth            : register(t0, space1);
Texture2D<float4> u_SSRTexture            : register(t1, space1);
Texture2D<float4> u_SceneColor            : register(t2, space1);
Texture2D<float4> u_EnvironmentSpecular   : register(t3, space1);
Texture2D<float4> u_ReflectionResponse    : register(t4, space1);
Texture2D<float4> u_SSRColor0             : register(t5, space1);
Texture2D<float4> u_SSRColor1             : register(t6, space1);
Texture2D<float4> u_SSRColor2             : register(t7, space1);
Texture2D<float4> u_SSRColor3             : register(t8, space1);
SamplerState      u_LinearSampler         : register(s0, space1);

float3 SampleSSRColorMip(float2 uv, uint mip) {
    switch (min(mip, 4u)) {
        case 0u: return SanitizeHDRColor(u_SceneColor.SampleLevel(u_LinearSampler, uv, 0.0).rgb);
        case 1u: return SanitizeHDRColor(u_SSRColor0.SampleLevel(u_LinearSampler, uv, 0.0).rgb);
        case 2u: return SanitizeHDRColor(u_SSRColor1.SampleLevel(u_LinearSampler, uv, 0.0).rgb);
        case 3u: return SanitizeHDRColor(u_SSRColor2.SampleLevel(u_LinearSampler, uv, 0.0).rgb);
        default: return SanitizeHDRColor(u_SSRColor3.SampleLevel(u_LinearSampler, uv, 0.0).rgb);
    }
}

float3 SampleSSRColorTrilinear(float2 uv, float lod) {
    float clamped = clamp(lod, 0.0, 4.0);
    uint m0 = (uint)floor(clamped);
    uint m1 = min(m0 + 1u, 4u);
    float fracLod = clamped - float(m0);
    return lerp(SampleSSRColorMip(uv, m0), SampleSSRColorMip(uv, m1), fracLod);
}

bool EvaluateSSRTap(
    uint2 tapPixel,
    float roughness,
    float ssrScale,
    float maxDist,
    float2 projScale,
    float maxConePx,
    bool useConeFilter,
    out float3 outRadiance,
    out float outWeight)
{
    outRadiance = float3(0.0, 0.0, 0.0);
    outWeight = 0.0;

    float4 hit = u_SSRTexture.Load(int3(tapPixel, 0));
    float weight = saturate(hit.a) * ssrScale;
    if (weight <= 1e-4) {
        return false;
    }

    float2 hitUV = saturate(hit.xy);
    float hitDepth = u_SceneDepth.SampleLevel(u_LinearSampler, hitUV, 0.0);
    if (hitDepth >= 0.9999 || hitDepth <= 0.0) {
        return false;
    }

    float hitDistMeters = hit.z * maxDist;
    float3 hitViewPos = ReconstructViewPos(hitUV, hitDepth);

    // Filter hit over HDR color pyramid footprint driven by GGX roughness cone aperture
    float coneTan = clamp(roughness * roughness * 0.75, 0.0, 0.75);
    float worldConeRadius = coneTan * hitDistMeters;
    float pixelRadius = clamp(max(projScale.x, projScale.y) * worldConeRadius / max(abs(hitViewPos.z), 0.1), 0.0, maxConePx);

    float mipLod = clamp(log2(max(pixelRadius * 0.5, 1.0)), 0.0, 4.0);
    float2 uvRadius = float2(pixelRadius, pixelRadius) * u_ScreenSize.zw;

    static const float2 coneOffsets[8] = {
        float2( 1.0,  0.0) * 0.45,
        float2(-1.0,  0.0) * 0.45,
        float2( 0.0,  1.0) * 0.45,
        float2( 0.0, -1.0) * 0.45,
        float2( 0.7071,  0.7071) * 0.90,
        float2(-0.7071,  0.7071) * 0.90,
        float2( 0.7071, -0.7071) * 0.90,
        float2(-0.7071, -0.7071) * 0.90
    };

    float3 accumColor = SampleSSRColorTrilinear(hitUV, mipLod);
    float accumWeight = 1.0;
    float coneBlend = useConeFilter ? smoothstep(0.10, 0.75, pixelRadius) : 0.0;
    if (coneBlend > 1e-4 && roughness > 0.045) {
        // Continuous sky/depth acceptance across pyramid mip transitions avoids contour steps on curved surfaces.
        float skyTapWeight = lerp(0.0, 0.35, smoothstep(0.75, 2.0, mipLod));
        float relaxDepth = smoothstep(0.75, 1.75, mipLod);
        [unroll]
        for (uint t = 0; t < 8u; ++t) {
            float2 tapUV = clamp(hitUV + coneOffsets[t] * uvRadius, float2(0.001, 0.001), float2(0.999, 0.999));
            float tapDepth = u_SceneDepth.SampleLevel(u_LinearSampler, tapUV, 0.0);
            float tapDepthWeight = 1.0;
            if (tapDepth >= 0.9999) {
                tapDepthWeight = skyTapWeight;
            } else {
                float tapZ = ReconstructViewPos(tapUV, tapDepth).z;
                float depthGate = exp(-abs(tapZ - hitViewPos.z) / max(0.5, abs(hitViewPos.z) * 0.15));
                tapDepthWeight = lerp(depthGate, 1.0, relaxDepth);
            }
            float spatialW = ((t < 4u) ? 0.65 : 0.40) * coneBlend;
            float w = spatialW * tapDepthWeight;
            accumColor += SampleSSRColorTrilinear(tapUV, mipLod) * w;
            accumWeight += w;
        }
    }

    outRadiance = max(accumColor / max(accumWeight, 1e-4), 0.0);
    outWeight = weight;
    return true;
}

float4 PSMain(VSOutput input) : SV_Target {
    uint2 size;
    u_SceneDepth.GetDimensions(size.x, size.y);
    uint2 pixel = min(uint2(input.Position.xy), size - 1);
    float depth = u_SceneDepth.Load(int3(pixel, 0));
    float4 sceneColor = u_SceneColor.Load(int3(pixel, 0));
    if (depth <= 0.0 || depth >= 0.9999) return sceneColor;

    float4 envSample = u_EnvironmentSpecular.Load(int3(pixel, 0));
    float4 responseSample = u_ReflectionResponse.Load(int3(pixel, 0));
    float signedRoughness = envSample.a;
    float3 response = responseSample.rgb;
    // Explicit reflection-disabled flag (negative alpha) or zero BRDF response preserves scene pixels untouched.
    if (signedRoughness <= 0.0 || max(response.r, max(response.g, response.b)) <= 1e-6) {
        return sceneColor;
    }

    float roughness = clamp(abs(signedRoughness), 0.04, 1.0);
    float roughnessFade = 1.0 - smoothstep(0.65, 0.98, roughness);
    float ssrScale = saturate(u_SSRParams.z) * roughnessFade;
    if (ssrScale <= 1e-4) return sceneColor;

    float maxDist = max(u_SSRParams.x, 5.0);
    float2 projScale = float2(abs(u_Projection[0][0]), abs(u_Projection[1][1])) * 0.5 * u_ScreenSize.xy;
    float maxConePx = u_SSRExtra.y > 0.0 ? u_SSRExtra.y : 32.0;

    float3 centerRadiance = float3(0.0, 0.0, 0.0);
    float centerWeight = 0.0;
    bool centerValid = EvaluateSSRTap(
        pixel, roughness, ssrScale, maxDist, projScale, maxConePx, true, centerRadiance, centerWeight);

    // Keep exact single-tap sharp mirror parity at min roughness (0.04); enable cross-bilateral
    // spatial resolve on glossier surfaces to suppress TAA-off DDA streaking and contour variance.
    float spatialBlend = smoothstep(0.045, 0.10, roughness);
    float finalWeight = centerWeight;
    float3 finalRadiance = centerRadiance;

    if (spatialBlend > 1e-4) {
        static const int2 neighborOffsets[24] = {
            int2( 1,  0), int2(-1,  0), int2( 0,  1), int2( 0, -1),
            int2( 1,  1), int2(-1,  1), int2( 1, -1), int2(-1, -1),
            int2( 2,  0), int2(-2,  0), int2( 0,  2), int2( 0, -2),
            int2( 2,  2), int2(-2,  2), int2( 2, -2), int2(-2, -2),
            int2( 3,  0), int2(-3,  0), int2( 0,  3), int2( 0, -3),
            int2( 4,  0), int2(-4,  0), int2( 0,  4), int2( 0, -4)
        };
        static const float neighborWeights[24] = {
            0.90, 0.90, 0.90, 0.90,
            0.78, 0.78, 0.78, 0.78,
            0.65, 0.65, 0.65, 0.65,
            0.42, 0.42, 0.42, 0.42,
            0.40, 0.40, 0.40, 0.40,
            0.25, 0.25, 0.25, 0.25
        };

        float centerViewZ = ReconstructViewPos((float2(pixel) + 0.5) * u_ScreenSize.zw, depth).z;
        int2 maxPixel = int2(size) - int2(1, 1);
        int2 pL = max(int2(pixel) - int2(1, 0), int2(0, 0));
        int2 pR = min(int2(pixel) + int2(1, 0), maxPixel);
        int2 pU = max(int2(pixel) - int2(0, 1), int2(0, 0));
        int2 pD = min(int2(pixel) + int2(0, 1), maxPixel);
        float dL = u_SceneDepth.Load(int3(pL, 0));
        float dR = u_SceneDepth.Load(int3(pR, 0));
        float dU = u_SceneDepth.Load(int3(pU, 0));
        float dD = u_SceneDepth.Load(int3(pD, 0));
        float zL = (dL > 0.0 && dL < 0.9999) ? ReconstructViewPos((float2(pL) + 0.5) * u_ScreenSize.zw, dL).z : centerViewZ;
        float zR = (dR > 0.0 && dR < 0.9999) ? ReconstructViewPos((float2(pR) + 0.5) * u_ScreenSize.zw, dR).z : centerViewZ;
        float zU = (dU > 0.0 && dU < 0.9999) ? ReconstructViewPos((float2(pU) + 0.5) * u_ScreenSize.zw, dU).z : centerViewZ;
        float zD = (dD > 0.0 && dD < 0.9999) ? ReconstructViewPos((float2(pD) + 0.5) * u_ScreenSize.zw, dD).z : centerViewZ;
        float dzdx = (abs(zR - centerViewZ) < abs(centerViewZ - zL)) ? (zR - centerViewZ) : (centerViewZ - zL);
        float dzdy = (abs(zD - centerViewZ) < abs(centerViewZ - zU)) ? (zD - centerViewZ) : (centerViewZ - zU);

        float depthTol = 0.015 * abs(centerViewZ) + 0.008;
        float totalSurfaceWeight = 1.0;
        float weightSum = centerValid ? centerWeight : 0.0;
        float3 radianceSum = centerValid ? (centerRadiance * centerWeight) : float3(0.0, 0.0, 0.0);

        [unroll]
        for (uint n = 0; n < 24u; ++n) {
            int2 nCoord = int2(pixel) + neighborOffsets[n];
            if (any(nCoord < int2(0, 0)) || any(nCoord > maxPixel)) continue;
            uint2 nPixel = uint2(nCoord);
            float nDepth = u_SceneDepth.Load(int3(nPixel, 0));
            if (nDepth <= 0.0 || nDepth >= 0.9999) continue;
            float4 nEnv = u_EnvironmentSpecular.Load(int3(nPixel, 0));
            if (nEnv.a <= 0.0) continue;

            float roughDiff = abs(abs(nEnv.a) - roughness);
            if (roughDiff > 0.08) continue;

            float nViewZ = ReconstructViewPos((float2(nPixel) + 0.5) * u_ScreenSize.zw, nDepth).z;
            float predictedViewZ = centerViewZ + dzdx * float(neighborOffsets[n].x) + dzdy * float(neighborOffsets[n].y);
            float planeDiff = abs(nViewZ - predictedViewZ);
            float surfW = neighborWeights[n] *
                          exp(-planeDiff / depthTol) *
                          exp(-roughDiff / 0.04);
            if (surfW <= 1e-4) continue;

            totalSurfaceWeight += surfW;
            float3 nRadiance = float3(0.0, 0.0, 0.0);
            float nWeight = 0.0;
            if (EvaluateSSRTap(nPixel, roughness, ssrScale, maxDist, projScale, maxConePx, false, nRadiance, nWeight)) {
                weightSum += surfW * nWeight;
                radianceSum += surfW * nWeight * nRadiance;
            }
        }

        float gatheredWeight = weightSum / max(totalSurfaceWeight, 1e-4);
        if (weightSum > 1e-5) {
            float3 gatheredRadiance = radianceSum / weightSum;
            finalWeight = lerp(centerValid ? centerWeight : 0.0, gatheredWeight, spatialBlend);
            finalRadiance = centerValid ? lerp(centerRadiance, gatheredRadiance, spatialBlend) : gatheredRadiance;
        }
    }

    if (finalWeight <= 1e-4) return sceneColor;

    // Replace only the prior indirect specular term exported by the opaque material pass
    // using the exact Fresnel/F0/metallic/BRDF/AO response. Diffuse, direct & emission stay untouched.
    float3 previous = envSample.rgb;
    float3 replacement = max(finalRadiance, 0.0) * response;
    float3 color = sceneColor.rgb + finalWeight * (replacement - previous);
    return float4(clamp(color, 0.0, 65000.0), sceneColor.a);
}
