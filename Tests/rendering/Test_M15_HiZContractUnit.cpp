#include "RenderTestCommon.hpp"
#include <Axle/RenderBackendPolicy.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/ViewContext.hpp>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace Axle;
using namespace Axle::HiZContract;

#ifndef AXLE_HIZ_SHADER_PATH
#define AXLE_HIZ_SHADER_PATH "Engine/Resources/Shaders/HiZ.hlsl"
#endif

#ifndef AXLE_RHI_VIEW_TARGETS_SOURCE_PATH
#define AXLE_RHI_VIEW_TARGETS_SOURCE_PATH "Engine/RHI/src/RHIViewTargets.cpp"
#endif

namespace {
    std::string ReadText(const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) return {};
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
}

bool RunM15HiZContractUnitTests() {
    std::cout << "[M15HiZ] Running Milestone 15 Unit 5 Batch A (Hi-Z Depth Pyramid) Contract Tests...\n";

    // 1. M15-U5-06.1: Freeze depth convention ([0, 1], min-depth nearest blocker)
    {
        RENDER_CHECK(NearDepth == 0.0f && FarDepth == 1.0f,
            "M15-U5-06.1: depth convention freezes [0.0, 1.0] near-to-far range");
        RENDER_CHECK(!GetDepthConventionSummary().empty() &&
            GetDepthConventionSummary().find("minimum") != std::string_view::npos,
            "M15-U5-06.1: depth convention explicitly specifies minimum-depth nearest-blocker policy");

        std::vector<std::vector<float>> levels;
        std::vector<PyramidExtent> extents;
        RENDER_CHECK(!BuildMinReductionPyramid({}, 0, 0, 1, levels, extents),
            "M15-U5-06.1: rejects zero extents");
        RENDER_CHECK(!BuildMinReductionPyramid({0.5f}, 1, 1, 0, levels, extents) &&
            !BuildMinReductionPyramid({0.5f}, 1, 1, MaxLevels + 1, levels, extents),
            "M15-U5-06.1: rejects zero or over-limit level counts");
        RENDER_CHECK(!BuildMinReductionPyramid({-0.01f}, 1, 1, 1, levels, extents) &&
            !BuildMinReductionPyramid({1.01f}, 1, 1, 1, levels, extents) &&
            !BuildMinReductionPyramid({std::numeric_limits<float>::quiet_NaN()}, 1, 1, 1, levels, extents) &&
            !BuildMinReductionPyramid({std::numeric_limits<float>::infinity()}, 1, 1, 1, levels, extents),
            "M15-U5-06.1: rejects out-of-range and non-finite depth values");
        RENDER_CHECK(!VerifyConservativeMinReduction({0.5f, 0.5f}, 2, 2),
            "M15-U5-06.1: rejects buffer size mismatch");
    }

    // 2. M15-U5-06.3: Define useful level count and SSR binding minimum
    {
        RENDER_CHECK(ComputeUsefulLevelCount(0, 1) == 0 && ComputeUsefulLevelCount(1, 0) == 0 &&
            ComputeAllocatedLevelCount(0, 1) == 0,
            "M15-U5-06.3: zero extents report 0 useful/allocated levels");
        RENDER_CHECK(ComputeUsefulLevelCount(1, 1) == 1 && ComputeAllocatedLevelCount(1, 1) == 8,
            "M15-U5-06.3: 1x1 has 1 useful level and retains 8 SSR slots");
        RENDER_CHECK(ComputeUsefulLevelCount(3, 5) == 3 && ComputeAllocatedLevelCount(3, 5) == 8,
            "M15-U5-06.3: 3x5 has 3 useful levels (3x5 -> 1x2 -> 1x1)");
        RENDER_CHECK(ComputeUsefulLevelCount(64, 64) == 7 && ComputeAllocatedLevelCount(64, 64) == 8,
            "M15-U5-06.3: 64x64 has 7 useful levels");
        RENDER_CHECK(ComputeUsefulLevelCount(128, 128) == 8 && ComputeAllocatedLevelCount(128, 128) == 8,
            "M15-U5-06.3: 128x128 has exact 8 useful levels");
        RENDER_CHECK(ComputeUsefulLevelCount(1919, 1079) == 11 && ComputeAllocatedLevelCount(1919, 1079) == 11,
            "M15-U5-06.3: 1919x1079 expands beyond 8 to 11 useful levels reaching 1x1");
        RENDER_CHECK(ComputeUsefulLevelCount(1920, 1080) == 11 && ComputeUsefulLevelCount(2560, 1440) == 12 &&
            ComputeUsefulLevelCount(3840, 2160) == 12,
            "M15-U5-06.3: 1080p (11), 1440p (12), and 4K (12) reach 1x1");
        const PyramidExtent expectedOne{1, 1};
        const PyramidExtent expectedEmpty{0, 0};
        RENDER_CHECK(ComputeLevelExtent(1919, 1079, 10) == expectedOne &&
            ComputeLevelExtent(1919, 1079, MaxLevels) == expectedEmpty,
            "M15-U5-06.3: ComputeLevelExtent hits 1x1 at useful-1 and rejects invalid levels");
    }

    // 3. M15-U5-05.1 - U5-05.4 & U5-06.2: Canonical min reduction + conservative odd-edge policy
    {
        RENDER_CHECK(!GetEdgePolicySummary().empty() &&
            GetEdgePolicySummary().find("odd") != std::string_view::npos,
            "M15-U5-06.2: edge policy explicitly documents conservative odd-border folding");

        const std::vector<float> canonical4x4 = {
            0.90f, 0.80f, 0.70f, 0.60f,
            0.50f, 0.40f, 0.30f, 0.20f,
            0.15f, 0.25f, 0.35f, 0.45f,
            0.05f, 0.55f, 0.65f, 0.75f
        };
        std::vector<std::vector<float>> levels;
        std::vector<PyramidExtent> extents;
        RENDER_CHECK(BuildMinReductionPyramid(canonical4x4, 4, 4, 3, levels, extents),
            "M15-U5-05.1: canonical 4x4 min reduction builds cleanly");
        RENDER_CHECK(extents == std::vector<PyramidExtent>({{4, 4}, {2, 2}, {1, 1}}) &&
            levels[1] == std::vector<float>({0.40f, 0.20f, 0.05f, 0.35f}) &&
            levels[2] == std::vector<float>({0.05f}),
            "M15-U5-05.1: 4x4 min reduction matches exact reference");
        RENDER_CHECK(VerifyConservativeMinReduction(canonical4x4, 4, 4),
            "M15-U5-05.1: canonical 4x4 passes conservative coverage check");

        // Odd border tests: 1x1, 3x5, 5x3, 1x5, 5x1, 127x63, 1919x1079
        for (const auto [w, h] : std::vector<std::pair<uint32_t, uint32_t>>{
                 {1, 1}, {3, 5}, {5, 3}, {1, 5}, {5, 1}, {127, 63}, {1919, 1079}}) {
            RENDER_CHECK(VerifyOddResolutionPyramid(w, h),
                "M15-U5-05.2..05.4 / U5-06.2: odd-corner blocker propagates conservatively to 1x1");
        }

        // Verify odd column/row folding independently on 3x5 (x=2 col and y=4 row)
        const std::vector<float> colBlocker = {
            0.90f, 0.80f, 0.03f,
            0.70f, 0.60f, 0.50f,
            0.40f, 0.50f, 0.60f,
            0.70f, 0.80f, 0.90f,
            0.85f, 0.75f, 0.65f
        };
        RENDER_CHECK(BuildMinReductionPyramid(colBlocker, 3, 5, 3, levels, extents) &&
            levels[1][0] == 0.03f && levels[2][0] == 0.03f,
            "M15-U5-06.2: trailing odd column blocker (x=2) propagates cleanly through mip 1 and 2");

        const std::vector<float> rowBlocker = {
            0.90f, 0.80f, 0.70f,
            0.60f, 0.50f, 0.40f,
            0.50f, 0.60f, 0.70f,
            0.80f, 0.90f, 0.85f,
            0.75f, 0.02f, 0.65f
        };
        RENDER_CHECK(BuildMinReductionPyramid(rowBlocker, 3, 5, 3, levels, extents) &&
            levels[1][1] == 0.02f && levels[2][0] == 0.02f,
            "M15-U5-06.2: trailing odd row blocker (y=4) propagates cleanly through mip 1 and 2");
    }

    // 4. M15-U5-07.1 & U5-07.2: Visualization shader contract, ViewContext policy hash & RenderGraph barrier chain
    {
        const std::string hizShader = ReadText(AXLE_HIZ_SHADER_PATH);
        RENDER_CHECK(!hizShader.empty() &&
            hizShader.find("includeExtraX") != std::string::npos &&
            hizShader.find("includeExtraY") != std::string::npos &&
            hizShader.find("VisualizeVSMain") != std::string::npos &&
            hizShader.find("VisualizePSMain") != std::string::npos,
            "M15-U5-06.2 / U5-07.1: HiZ.hlsl implements conservative odd-edge reduction and per-level visualization");

        const std::string viewTargetsCpp = ReadText(AXLE_RHI_VIEW_TARGETS_SOURCE_PATH);
        RENDER_CHECK(!viewTargetsCpp.empty() &&
            viewTargetsCpp.find("ComputeUsefulHiZLevelCount") != std::string::npos &&
            viewTargetsCpp.find("ComputeAllocatedHiZLevelCount") != std::string::npos,
            "M15-U5-06.3: RHIViewTargets implements dynamic useful & allocated level counts");

        SceneRenderSettings baseSettings{};
        SceneRenderSettings vis0 = baseSettings; vis0.HiZVisualizationLevel = 0;
        SceneRenderSettings vis3 = baseSettings; vis3.HiZVisualizationLevel = 3;
        const auto compatibility = RenderBackendPolicy::FromBackend(RendererBackendType::OpenGL_Compatibility);
        RENDER_CHECK(compatibility.ResolveSettings(vis3).HiZVisualizationLevel == -1 &&
            !compatibility.CollectFallbacks(vis3).empty(), "unsupported backend disables Hi-Z visualization with an explicit fallback");
        RENDER_CHECK(HashViewFeaturePolicy(baseSettings) != HashViewFeaturePolicy(vis0) &&
            HashViewFeaturePolicy(vis0) != HashViewFeaturePolicy(vis3),
            "M15-U5-07.1: HashViewFeaturePolicy reflects HiZVisualizationLevel feature changes");

        // Validate 11-level Hi-Z RenderGraph dependency chain (e.g. 1919x1079) -> SSRCompute + HiZVisualize
        RenderGraph graph;
        const auto color = graph.Import("SceneColor", nullptr, true);
        const auto depth = graph.Import("SceneDepth", nullptr, true);
        const auto ssrDepth = graph.Import("SSRDepth", nullptr, false);
        const auto ssrNormal = graph.Import("SceneNormal", nullptr, true);
        const auto ssrInput = graph.Import("SSRInput", nullptr, false);
        const auto ssrStorage = graph.Import("SSRStorage", nullptr, false);
        std::vector<RenderGraph::Resource> hizLevels;
        constexpr uint32_t oddLevels = 11;
        for (uint32_t l = 0; l < oddLevels; ++l) {
            hizLevels.push_back(graph.Import("HiZ" + std::to_string(l), nullptr, false));
        }

        graph.AddPass("SSRCopy", {
            {color, RHI::RHIResourceLayout::CopySource},
            {ssrInput, RHI::RHIResourceLayout::CopyDest, RenderGraph::Access::Write},
            {depth, RHI::RHIResourceLayout::CopySource},
            {ssrDepth, RHI::RHIResourceLayout::CopyDest, RenderGraph::Access::Write}});
        for (uint32_t l = 0; l < oddLevels; ++l) {
            graph.AddPass("HiZ" + std::to_string(l), {
                {l == 0 ? ssrDepth : hizLevels[l - 1], RHI::RHIResourceLayout::ShaderResource},
                {hizLevels[l], RHI::RHIResourceLayout::ShaderResourceStorage, RenderGraph::Access::Write}});
        }
        std::vector<RenderGraph::Use> ssrUses = {
            {ssrDepth, RHI::RHIResourceLayout::ShaderResource},
            {ssrNormal, RHI::RHIResourceLayout::ShaderResource},
            {ssrInput, RHI::RHIResourceLayout::ShaderResource},
            {ssrStorage, RHI::RHIResourceLayout::ShaderResourceStorage, RenderGraph::Access::Write}
        };
        for (uint32_t l = 0; l < RHI::RHIViewTargets::MinSSRHiZLevels; ++l) {
            ssrUses.push_back({hizLevels[l], RHI::RHIResourceLayout::ShaderResource});
        }
        graph.AddPass("SSRCompute", std::move(ssrUses));
        graph.AddPass("HiZVisualize", {
            {hizLevels[10], RHI::RHIResourceLayout::ShaderResource},
            {color, RHI::RHIResourceLayout::ColorAttachment, RenderGraph::Access::Write, RenderGraph::Load::Clear}});

        RENDER_CHECK(graph.Compile(false, false),
            "M15-U5-07.2: 11-level Hi-Z RAW chain into SSRCompute and HiZVisualize compiles cleanly");

        // Negative barrier check: reading HiZ before it is written must fail compile
        RenderGraph badGraph;
        const auto uninitHiZ = badGraph.Import("HiZ0", nullptr, false);
        const auto targetColor = badGraph.Import("Color", nullptr, true);
        badGraph.AddPass("SSRCompute", {
            {uninitHiZ, RHI::RHIResourceLayout::ShaderResource},
            {targetColor, RHI::RHIResourceLayout::ColorAttachment, RenderGraph::Access::Write, RenderGraph::Load::Clear}});
        RENDER_CHECK(!badGraph.Compile(false, false) && !badGraph.GetError().empty(),
            "M15-U5-07.2: RenderGraph rejects reading uninitialized Hi-Z before generation barrier");
    }

    // 5. M15-U5-F (S5.6): Frozen ray/depth projection, hierarchical Hi-Z DDA traversal,
    //    thickness & border fade, and SSRColor0..3 pyramid RenderGraph barriers
    {
        constexpr uint32_t w = 64u;
        constexpr uint32_t h = 64u;
        const glm::mat4 proj = []() {
            glm::mat4 p = glm::perspectiveRH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
            return p;
        }();
        const glm::mat4 invProj = glm::inverse(proj);

        // 5a. M15-U5-32.1: Projection round-trip & screen ray clipping
        const glm::vec3 sampleView(-0.8f, 0.4f, -6.5f);
        const glm::vec3 uvz = SSRRaymarchContract::ProjectViewToUVZ(sampleView, proj);
        const glm::vec3 recovered = SSRRaymarchContract::ReconstructViewPos(glm::vec2(uvz.x, uvz.y), uvz.z, invProj);
        RENDER_CHECK(uvz.x > 0.0f && uvz.x < 1.0f && uvz.y > 0.0f && uvz.y < 1.0f &&
                     uvz.z > 0.0f && uvz.z < 1.0f &&
                     glm::length(recovered - sampleView) < 1e-3f,
            "M15-U5-32.1: ProjectViewToUVZ and ReconstructViewPos round-trip cleanly under RH [0,1] depth");

        float clippedT = 0.0f;
        RENDER_CHECK(SSRRaymarchContract::ClipScreenRay({0.5f, 0.5f, 0.5f}, {2.0f, 0.5f, 0.5f}, &clippedT) &&
                     std::abs(clippedT - (1.0f / 3.0f)) < 1e-4f,
            "M15-U5-32.1: ClipScreenRay clips exit rays precisely to screen boundary");
        RENDER_CHECK(!SSRRaymarchContract::ClipScreenRay({1.2f, 0.5f, 0.5f}, {1.8f, 0.5f, 0.5f}, &clippedT),
            "M15-U5-32.1: ClipScreenRay rejects exterior off-screen rays");
        RENDER_CHECK(!SSRRaymarchContract::ClipScreenRay({1.2f,.5f,.5f},{.5f,.5f,.5f},&clippedT),
            "exit-only clipping rejects exterior starts even when their segment would enter the screen");

        // 5b. M15-U5-32.2..32.7: Synthetic Hi-Z DDA open sky miss vs wall hit vs thickness rejection
        const uint32_t usefulLevels = ComputeUsefulLevelCount(w, h);
        const glm::vec3 floorOriginView(0.0f, -1.2f, -5.0f);
        const glm::vec3 floorUVZ = SSRRaymarchContract::ProjectViewToUVZ(floorOriginView, proj);
        const glm::vec3 floorNormalView = glm::normalize(glm::vec3(0.0f, 0.96f, 0.28f));

        // Open sky depth buffer with only the floor pixel populated
        std::vector<float> skyDepth(size_t(w) * h, 1.0f);
        const uint32_t ox = static_cast<uint32_t>(floorUVZ.x * w);
        const uint32_t oy = static_cast<uint32_t>(floorUVZ.y * h);
        skyDepth[size_t(oy) * w + ox] = floorUVZ.z;
        std::vector<std::vector<float>> skyPyramid;
        std::vector<PyramidExtent> skyExtents;
        RENDER_CHECK(BuildMinReductionPyramid(skyDepth, w, h, usefulLevels, skyPyramid, skyExtents),
            "M15-U5-32.7: build synthetic open-sky Hi-Z pyramid");

        const auto skyRay = SSRRaymarchContract::BuildInitialSSRRay(
            glm::vec2(floorUVZ.x, floorUVZ.y), floorUVZ.z, floorNormalView, proj, invProj, w, h, usefulLevels, 30.0f);
        RENDER_CHECK(skyRay.Valid && skyRay.MaxUsefulMip == usefulLevels - 1u,
            "M15-U5-32.2: BuildInitialSSRRay produces valid view/UVZ ray setup");
        RENDER_CHECK(glm::distance(SSRRaymarchContract::ProjectViewToUVZ(skyRay.EndView, proj), skyRay.EndUVZ) < 1e-4f,
            "clipped view-space endpoint projects to its UVZ endpoint under perspective");
        const auto shortRay = SSRRaymarchContract::BuildInitialSSRRay(glm::vec2(floorUVZ),floorUVZ.z,floorNormalView,
            proj,invProj,w,h,usefulLevels,1);
        RENDER_CHECK(shortRay.Valid && shortRay.MaxDistance == 5,
            "CPU and GPU share the five-meter minimum SSR distance policy");

        const auto skyRes = SSRRaymarchContract::TraceHiZDepthBuffer(
            skyPyramid, w, h, skyRay, proj, invProj, 0.5f, 32u);
        RENDER_CHECK(!skyRes.Hit && skyRes.HighestMipReached >= 2u && skyRes.StepsExecuted > 0u && skyRes.StepsExecuted < 32u,
            "M15-U5-32.3 / 32.5: open-sky Hi-Z DDA ascends coarse mips and misses quickly");

        // Place a solid target blocker directly along the traced ray at t = 0.25 * (End - Start)
        const glm::vec3 wallView = glm::mix(skyRay.StartView, skyRay.EndView, 0.22f);
        const glm::vec3 wallUVZ = SSRRaymarchContract::ProjectViewToUVZ(wallView, proj);
        std::vector<float> wallDepth = skyDepth;
        const uint32_t wx = std::clamp<uint32_t>(static_cast<uint32_t>(wallUVZ.x * w), 2u, w - 3u);
        const uint32_t wy = std::clamp<uint32_t>(static_cast<uint32_t>(wallUVZ.y * h), 2u, h - 3u);
        for (uint32_t yy = wy - 2u; yy <= wy + 2u; ++yy) {
            for (uint32_t xx = wx - 2u; xx <= wx + 2u; ++xx) {
                wallDepth[size_t(yy) * w + xx] = wallUVZ.z;
            }
        }
        std::vector<std::vector<float>> wallPyramid;
        std::vector<PyramidExtent> wallExtents;
        RENDER_CHECK(BuildMinReductionPyramid(wallDepth, w, h, usefulLevels, wallPyramid, wallExtents),
            "M15-U5-32.7: build synthetic wall-hit Hi-Z pyramid");
        const auto hitRes = SSRRaymarchContract::TraceHiZDepthBuffer(
            wallPyramid, w, h, skyRay, proj, invProj, 0.5f, 32u);
        RENDER_CHECK(hitRes.Hit && hitRes.HitDistance > 0.2f && hitRes.Confidence > 0.1f &&
                     glm::length(hitRes.HitUV - glm::vec2(wallUVZ.x, wallUVZ.y)) < 0.08f,
            "M15-U5-32.4 / 32.6 / 33.2: Hi-Z DDA descends to mip 0 and refines target intersection accurately");

        // Thin-surface thickness rejection: a closer depth 2.5m in front of the ray cannot falsely block it
        const glm::vec3 frontView = wallView + glm::vec3(0.0f, 0.0f, 2.5f);
        const glm::vec3 frontUVZ = SSRRaymarchContract::ProjectViewToUVZ(frontView, proj);
        std::vector<float> falseBlockDepth = skyDepth;
        for (uint32_t yy = wy - 1u; yy <= wy + 1u; ++yy) {
            for (uint32_t xx = wx - 1u; xx <= wx + 1u; ++xx) {
                falseBlockDepth[size_t(yy) * w + xx] = frontUVZ.z;
            }
        }
        std::vector<std::vector<float>> falseBlockPyramid;
        RENDER_CHECK(BuildMinReductionPyramid(falseBlockDepth, w, h, usefulLevels, falseBlockPyramid, wallExtents),
            "M15-U5-33.1: build thin-blocker behind-ray Hi-Z pyramid");
        const auto rejectedRes = SSRRaymarchContract::TraceHiZDepthBuffer(
            falseBlockPyramid, w, h, skyRay, proj, invProj, 0.5f, 32u);
        RENDER_CHECK(!rejectedRes.Hit,
            "M15-U5-33.1: thickness check rejects intersection deeper behind surface than thickness");

        // Hierarchical traversal must cover the same blockers as mip-0 DDA at odd extents.
        for (const auto size : {glm::uvec2(63, 47), glm::uvec2(95, 63)}) {
          for (int axis = 0; axis < 2; ++axis) for (bool reverse : {false,true}) {
            SSRRaymarchContract::RaySetup ray{};
            ray.StartUVZ = {.02f, .5f, SSRRaymarchContract::ProjectViewToUVZ({0,0,-5.1f}, proj).z};
            ray.EndUVZ = {.98f, .5f, ray.StartUVZ.z};
            if (axis == 1) {
                std::swap(ray.StartUVZ.x,ray.StartUVZ.y);
                std::swap(ray.EndUVZ.x,ray.EndUVZ.y);
            }
            if (reverse) std::swap(ray.StartUVZ,ray.EndUVZ);
            ray.StartView = SSRRaymarchContract::ReconstructViewPos(glm::vec2(ray.StartUVZ), ray.StartUVZ.z, invProj);
            ray.EndView = SSRRaymarchContract::ReconstructViewPos(glm::vec2(ray.EndUVZ), ray.EndUVZ.z, invProj);
            ray.MaxUsefulMip = ComputeUsefulLevelCount(size.x, size.y) - 1;
            ray.OriginView = ray.StartView;
            ray.MaxDistance = ray.MaxTraceDist = 40;
            ray.Valid = true;
            for (uint32_t column = 8; column + 8 < size[axis]; ++column) {
                std::vector<float> depth(size_t(size.x) * size.y, 1);
                const float blocker = SSRRaymarchContract::ProjectViewToUVZ({0,0,-5}, proj).z;
                for (uint32_t row = 0; row < size[1-axis]; ++row)
                    depth[axis == 0 ? size_t(row)*size.x+column : size_t(column)*size.x+row] = blocker;
                std::vector<std::vector<float>> pyramid;
                std::vector<PyramidExtent> sizes;
                RENDER_CHECK(BuildMinReductionPyramid(depth, size.x, size.y, ray.MaxUsefulMip + 1, pyramid, sizes), "odd SSR fixture pyramid");
                auto mipZero = ray;
                mipZero.MaxUsefulMip = 0;
                const auto reference = SSRRaymarchContract::TraceHiZDepthBuffer(pyramid, size.x, size.y, mipZero, proj, invProj, .5f, 256);
                const auto hierarchical = SSRRaymarchContract::TraceHiZDepthBuffer(pyramid, size.x, size.y, ray, proj, invProj, .5f, 256);
                RENDER_CHECK(reference.Hit && hierarchical.Hit && glm::distance(reference.HitUV, hierarchical.HitUV) < .01f,
                    "odd-resolution Hi-Z traversal cannot skip a one-pixel blocker found by mip-0 DDA");
            }
          }
        }

        // A camera-facing ray can cross a whole thickness band inside one pixel.
        // Refinement must find that crossing for either sign of depth direction.
        {
            constexpr uint32_t depthW = 4, depthH = 256;
            const float surfaceDepth = SSRRaymarchContract::ProjectViewToUVZ({0,0,-10},proj).z;
            std::vector<std::vector<float>> pyramid(1,std::vector<float>(depthW*depthH,surfaceDepth));
            SSRRaymarchContract::RaySetup ray;
            ray.StartUVZ = {.25f,.5f,SSRRaymarchContract::ProjectViewToUVZ({0,0,-30},proj).z};
            ray.EndUVZ = {.75f,.5f,SSRRaymarchContract::ProjectViewToUVZ({0,0,-8},proj).z};
            ray.StartView = SSRRaymarchContract::ReconstructViewPos(glm::vec2(ray.StartUVZ),ray.StartUVZ.z,invProj);
            ray.EndView = SSRRaymarchContract::ReconstructViewPos(glm::vec2(ray.EndUVZ),ray.EndUVZ.z,invProj);
            ray.OriginView = ray.StartView;
            ray.MaxDistance = ray.MaxTraceDist = 40;
            ray.Valid = true;
            const auto hit = SSRRaymarchContract::TraceHiZDepthBuffer(pyramid,depthW,depthH,ray,proj,invProj,.5f,64);
            RENDER_CHECK(hit.Hit && hit.Confidence > 0 && std::abs(hit.HitDepth-surfaceDepth)<1e-5f,
                "camera-facing rays refine a decreasing-depth crossing instead of rejecting the behind endpoint");
        }

        // 5c. M15-U5-34.2 / 35.4: Full SSRCopy -> HiZ -> SSRColor0..3 -> SSRCompute -> SSRApply RenderGraph chain
        RenderGraph ssrGraph;
        const auto gColor = ssrGraph.Import("SceneColor", nullptr, true);
        const auto gDepth = ssrGraph.Import("SceneDepth", nullptr, true);
        const auto gNormal = ssrGraph.Import("SceneNormal", nullptr, true);
        const auto gInput = ssrGraph.Import("SSRInput", nullptr, false);
        const auto gSsrDepth = ssrGraph.Import("SSRDepth", nullptr, false);
        const auto gStorage = ssrGraph.Import("SSRStorage", nullptr, false);
        const auto gEnv = ssrGraph.Import("ReflectionEnvironment", nullptr, true);
        const auto gResp = ssrGraph.Import("ReflectionResponse", nullptr, true);
        std::vector<RenderGraph::Resource> gHiZ, gSSRColor;
        for (uint32_t l = 0; l < 8u; ++l) gHiZ.push_back(ssrGraph.Import("HiZ" + std::to_string(l), nullptr, false));
        for (uint32_t l = 0; l < SSRRaymarchContract::ColorPyramidLevels; ++l) {
            gSSRColor.push_back(ssrGraph.Import("SSRColor" + std::to_string(l), nullptr, false));
        }

        ssrGraph.AddPass("SSRCopy", {
            {gColor, RHI::RHIResourceLayout::CopySource},
            {gInput, RHI::RHIResourceLayout::CopyDest, RenderGraph::Access::Write},
            {gDepth, RHI::RHIResourceLayout::CopySource},
            {gSsrDepth, RHI::RHIResourceLayout::CopyDest, RenderGraph::Access::Write}});
        for (uint32_t l = 0; l < 8u; ++l) {
            ssrGraph.AddPass("HiZ" + std::to_string(l), {
                {l == 0 ? gSsrDepth : gHiZ[l - 1], RHI::RHIResourceLayout::ShaderResource},
                {gHiZ[l], RHI::RHIResourceLayout::ShaderResourceStorage, RenderGraph::Access::Write}});
        }
        for (uint32_t l = 0; l < SSRRaymarchContract::ColorPyramidLevels; ++l) {
            ssrGraph.AddPass("SSRColor" + std::to_string(l), {
                {l == 0 ? gInput : gSSRColor[l - 1], RHI::RHIResourceLayout::ShaderResource},
                {gSSRColor[l], RHI::RHIResourceLayout::ShaderResourceStorage, RenderGraph::Access::Write}});
        }
        std::vector<RenderGraph::Use> compUses = {
            {gSsrDepth, RHI::RHIResourceLayout::ShaderResource},
            {gNormal, RHI::RHIResourceLayout::ShaderResource},
            {gInput, RHI::RHIResourceLayout::ShaderResource},
            {gStorage, RHI::RHIResourceLayout::ShaderResourceStorage, RenderGraph::Access::Write}};
        for (uint32_t l = 0; l < 8u; ++l) compUses.push_back({gHiZ[l], RHI::RHIResourceLayout::ShaderResource});
        ssrGraph.AddPass("SSRCompute", std::move(compUses));
        std::vector<RenderGraph::Use> applyUses = {
            {gEnv, RHI::RHIResourceLayout::ShaderResource},
            {gResp, RHI::RHIResourceLayout::ShaderResource},
            {gSsrDepth, RHI::RHIResourceLayout::ShaderResource},
            {gStorage, RHI::RHIResourceLayout::ShaderResource},
            {gInput, RHI::RHIResourceLayout::ShaderResource},
            {gColor, RHI::RHIResourceLayout::ColorAttachment, RenderGraph::Access::Write, RenderGraph::Load::Clear}};
        for (uint32_t l = 0; l < SSRRaymarchContract::ColorPyramidLevels; ++l) {
            applyUses.push_back({gSSRColor[l], RHI::RHIResourceLayout::ShaderResource});
        }
        ssrGraph.AddPass("SSRApply", std::move(applyUses));
        RENDER_CHECK(ssrGraph.Compile(false, false),
            "M15-U5-34.2 / 35.4: Full Hi-Z + SSRColor0..3 + SSRApply RenderGraph compiles with clean barriers");

        RenderGraph missingColorGraph;
        const auto missingColor = missingColorGraph.Import("SSRColor0", nullptr, false);
        const auto badOut = missingColorGraph.Import("SceneColor", nullptr, true);
        missingColorGraph.AddPass("SSRApply", {
            {missingColor, RHI::RHIResourceLayout::ShaderResource},
            {badOut, RHI::RHIResourceLayout::ColorAttachment, RenderGraph::Access::Write, RenderGraph::Load::Clear}});
        RENDER_CHECK(!missingColorGraph.Compile(false, false) && !missingColorGraph.GetError().empty(),
            "M15-U5-35.4: RenderGraph rejects SSRApply reading unwritten SSRColor0 pyramid");
    }

    std::cout << "[M15HiZ] All M15-U5-A & M15-U5-F Hi-Z/SSR contract tests passed.\n";
    return true;
}
