#pragma once

#include "PassRecordOps.hpp"
#include <Axle/DDGIShaderConstants.hpp>
#include <Axle/GIService.hpp>
#include <Axle/LightingService.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHITransform.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Axle {
    class RHISceneRenderer;
    class ViewContext;
    struct RHISkyboxConstants;
    struct DDGIHistoryFrame;
    namespace RHI {
        class RHIMeshView;
        class RHITexture;
    }
}

namespace Axle::Passes {

    struct DDGICaptureDraw {
        const RHI::RHIMeshView* Mesh = nullptr;
        std::array<float, 32> PushConstants{}; // Matches sizeof(RHIObjectPushConstants) == 128 bytes
        nri::Descriptor* MaterialCBV = nullptr;
        nri::DescriptorSet* MaterialSet = nullptr;
    };

    struct DDGICaptureFaceTarget {
        uint32_t ProbeSlot = 0;
        uint32_t FaceIndex = 0;
        uint32_t TileX = 0;
        uint32_t TileY = 0;
        RHI::RHIBuffer* SceneCB = nullptr;
        RHI::RHIBuffer* SkyCB = nullptr;
    };

    struct DDGIPlan {
        bool Active = false;
        bool ResetSeed = false;
        bool HasHistory = false;
        bool HasSkyboxDraw = false;
        uint64_t StateHash = 0;
        uint32_t ProbeCount = 0;
        uint32_t StartProbeCursor = 0;
        uint32_t UpdateCount = 0;
        uint32_t NextProbeCursor = 0;
        uint32_t CaptureCols = 0;
        uint32_t CaptureRows = 0;
        uint32_t CaptureWidth = 0;
        uint32_t CaptureHeight = 0;
        float CaptureNear = 0.05f;
        float CaptureFar = 100.0f;
        float ProbeMaxDistance = 4.0f;
        RHI::RHIDDGIAtlasExtent AtlasExtent{};
        DDGIContract::DDGIShaderConstants Constants{};
        RHI::RHIBuffer* CaptureConstantsCB = nullptr;
        std::vector<DDGICaptureFaceTarget> Faces;
        std::vector<DDGICaptureDraw> Draws;
    };

    class DDGIPass final {
    public:
        static bool EnsureScratchTargets(RHISceneRenderer& renderer);

        static bool BuildPlan(
            RHISceneRenderer& renderer,
            ViewContext& viewCtx,
            const RenderWorld& world,
            const DDGIHistoryFrame& previous,
            uint64_t stateHash,
            const GIFrame& giFrame,
            const RHI::RHISceneConstants& mainSceneConstants,
            const RHISkyboxConstants* skyboxBaseConstants,
            bool hasSkyboxDraw,
            RHI::RHIViewTargets* viewTargets,
            std::vector<DDGICaptureDraw>&& draws,
            DDGIPlan& outPlan);

        static void Declare(
            RenderGraph& graph,
            const DDGIPlan& plan,
            RenderGraph::Resource captureColor,
            RenderGraph::Resource captureDepth,
            RenderGraph::Resource irradAtlas,
            RenderGraph::Resource distAtlas,
            RenderGraph::Resource shadowAtlas,
            RenderGraph::Resource localShadowAtlas,
            bool hasCascadeAtlas,
            bool hasLocalAtlas);

        static bool PrepareGraphBindings(
            RHISceneRenderer& renderer,
            RenderGraph& graph,
            const DDGIPlan& plan,
            RHI::RHITexture* irradAtlas,
            RHI::RHITexture* distAtlas,
            nri::DescriptorSet* captureOcclusionSet,
            bool hasSkybox);

        static bool RecordCapture(
            RHISceneRenderer& renderer,
            const DDGIPlan& plan,
            const PassRecordOps& ops,
            nri::Pipeline* skyboxPipeline,
            nri::DescriptorSet* captureOcclusionSet,
            uint32_t& outDrawCount);

        static bool RecordSeedAndUpdate(
            RHISceneRenderer& renderer,
            const DDGIPlan& plan,
            const PassRecordOps& ops,
            uint32_t& outDispatchCount);
    };

} // namespace Axle::Passes
