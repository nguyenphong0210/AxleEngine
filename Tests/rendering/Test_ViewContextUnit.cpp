#include "RenderTestCommon.hpp"
#include <Axle/ViewContext.hpp>
#include <memory>

bool RunViewContextUnitTests() {
    using Axle::ViewContext;
    using Axle::ViewContextInput;
    using Axle::ViewResetReason;
    using Axle::RenderViewKind;

    std::cout << "  [ViewContext] Running view identity, reset and frame ownership tests...\n";
    ViewContext context;
    RENDER_CHECK(!context.IsInitialized(), "ViewContext starts uninitialized");

    ViewContextInput input;
    input.Width = 128;
    input.Height = 64;
    input.SceneRevision = 10;
    input.MaterialGeneration = 3;
    input.FeatureGeneration = 5;
    input.Targets = std::make_shared<Axle::RHI::RHIViewTargets>();

    auto reasons = context.Observe(input, 7);
    RENDER_CHECK((reasons & ViewResetReason::Initial) != ViewResetReason::None,
        "first view observation requests initial reset");
    RENDER_CHECK(context.GetFrameIndex() == 0 && context.GetDeviceEpoch() == 7,
        "initial view state stores frame index and device epoch");
    RENDER_CHECK(context.ConsumeResetReasons() == reasons, "reset reasons can be consumed exactly once");
    RENDER_CHECK(context.GetResetReasons() == ViewResetReason::None, "consuming reasons clears the latch");

    reasons = context.Observe(input, 7);
    RENDER_CHECK(reasons == ViewResetReason::None, "unchanged view does not reset temporal state");

    auto resized = input;
    resized.Width = 256;
    reasons = context.Observe(resized, 7);
    RENDER_CHECK((reasons & ViewResetReason::Resize) != ViewResetReason::None,
        "extent change requests resize reset");

    resized.Kind = RenderViewKind::AssetPreview;
    resized.SceneRevision++;
    resized.MaterialGeneration++;
    resized.FeatureGeneration++;
    resized.CameraCut = true;
    reasons = context.Observe(resized, 8);
    RENDER_CHECK((reasons & ViewResetReason::ViewSwitch) != ViewResetReason::None &&
                 (reasons & ViewResetReason::SceneRevision) != ViewResetReason::None &&
                 (reasons & ViewResetReason::MaterialGeneration) != ViewResetReason::None &&
                 (reasons & ViewResetReason::FeaturePolicy) != ViewResetReason::None &&
                 (reasons & ViewResetReason::CameraCut) != ViewResetReason::None &&
                 (reasons & ViewResetReason::DeviceEpoch) != ViewResetReason::None,
        "view switch, scene/material/feature generations, camera cut and device epoch are independent reset reasons");

    context.RequestReset(ViewResetReason::TargetGeneration);
    RENDER_CHECK((context.GetResetReasons() & ViewResetReason::TargetGeneration) != ViewResetReason::None,
        "explicit target-generation reset is retained");
    RENDER_CHECK(!context.BeginFrame(), "uninitialized context cannot begin a frame");
    RENDER_CHECK(context.AbandonFrame(), "abandoning an inactive frame is idempotent");

    std::cout << "  [ViewContext] View identity and frame ownership tests passed.\n";
    return true;
}
