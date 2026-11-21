#pragma once
#include <Axle/Types.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Axle {
    class PhysicsScene;
    class Framebuffer;

    enum class RenderViewKind : uint8_t {
        Main,
        AssetPreview,
        VFXPreview,
        AssemblyPreview,
        Thumbnail
    };

    struct ViewRequest {
        RenderViewKind Kind = RenderViewKind::Main;
        uint32_t Width = 1280;
        uint32_t Height = 720;
        PerspectiveCamera Camera;
        SceneRenderSettings Settings;

        // Modern RHI target destinations:
        std::shared_ptr<RHI::RHIViewTargets> ViewTargets;
        RHI::RHITexture* ColorTarget = nullptr;
        RHI::RHITexture* DepthTarget = nullptr;

        // OpenGL target destination (for GL compatibility mode):
        std::shared_ptr<Framebuffer> GLTargetFBO;

        // Optional physics debug scene and editor/tool wireframe lines:
        PhysicsScene* PhysicsScene = nullptr;
        std::vector<RenderDebugLine> ExtraDebugLines;

        std::string Name = "DefaultView";
        bool CameraCut = false; // Set on the first request after a discontinuous camera change.
        float DeltaTime = 1.0f / 60.0f; // Real frame delta time in seconds for frame-rate invariant post passes.
    };

    struct RenderResult {
        bool Success = false;
        RHI::RHICompletionToken CompletionToken;
        uint32_t DrawCallCount = 0;
        uint32_t RenderedEntityCount = 0;
        std::string ErrorMessage;
    };
}
