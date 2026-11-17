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

    std::cout << "[M15HiZ] All M15-U5-A (M15-U5-05.1 through U5-07.2) Hi-Z contract tests passed.\n";
    return true;
}
