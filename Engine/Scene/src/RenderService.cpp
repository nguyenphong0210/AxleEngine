#include <Axle/RenderService.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/SceneExtraction.hpp>

namespace Axle {

    RenderService& RenderService::Get() {
        static RenderService s_Instance;
        return s_Instance;
    }

    RenderService::RenderService() = default;

    RenderService::~RenderService() {
        Shutdown();
    }

    bool RenderService::Init() {
        if (m_Initialized) return true;

        // Try initializing modern RHI SceneRenderer if RHIDevice is active
        if (RHI::RHIDevice::Get().IsNativeReady()) {
            m_RHIRenderer = std::make_unique<RHISceneRenderer>();
            if (m_RHIRenderer->Init()) {
                AXLE_CORE_INFO("RenderService: Initialized modern RHI SceneRenderer backend.");
            } else {
                AXLE_CORE_WARN("RenderService: Failed to initialize modern RHI SceneRenderer backend.");
                m_RHIRenderer.reset();
            }
        }

        // Initialize OpenGL SceneRenderer
        m_GLRenderer = std::make_unique<SceneRenderer>();
        if (!m_GLRenderer->IsReady()) {
            AXLE_CORE_WARN("RenderService: OpenGL SceneRenderer is not ready.");
        }

        m_Initialized = (m_RHIRenderer != nullptr) || (m_GLRenderer != nullptr && m_GLRenderer->IsReady());
        return m_Initialized;
    }

    void RenderService::Shutdown() {
        if (!m_Initialized) return;

        if (m_RHIRenderer) {
            m_RHIRenderer->Shutdown();
            m_RHIRenderer.reset();
        }

        m_GLRenderer.reset();
        m_Initialized = false;
    }

    RenderResult RenderService::RenderView(const RenderWorld& world, const ViewRequest& request) {
        if (!m_Initialized) {
            if (!Init()) {
                return RenderResult{ false, {}, 0, 0, "RenderService failed to initialize" };
            }
        }

        RenderResult result;

        // Route to modern RHI renderer if available and modern targets requested
        if (m_RHIRenderer && RHI::RHIDevice::Get().IsNativeReady()) {
            RHI::RHITexture* colorTarget = request.ColorTarget;
            RHI::RHITexture* depthTarget = request.DepthTarget;

            if (!colorTarget && request.ViewTargets && request.ViewTargets->IsValid()) {
                colorTarget = request.ViewTargets->Get(RHI::RHIViewTarget::Color);
                depthTarget = request.ViewTargets->Get(RHI::RHIViewTarget::Depth);
            }

            if (!colorTarget) {
                result.Success = false;
                result.ErrorMessage = "No valid modern RHI color target provided in ViewRequest";
                return result;
            }

            m_RHIRenderer->RenderWorldView(world,
                                           request.Camera,
                                           colorTarget,
                                           depthTarget,
                                           request.Settings,
                                           request.PhysicsScene,
                                           request.ViewTargets);

            result.Success = true;
            result.DrawCallCount = m_RHIRenderer->GetDrawCallCount();
            result.RenderedEntityCount = m_RHIRenderer->GetRenderedEntityCount();
            result.CompletionToken = RHI::RHICompletionToken{ 1 };
            return result;
        }

        // OpenGL compatibility path
        if (m_GLRenderer && m_GLRenderer->IsReady()) {
            m_GLRenderer->SetViewportSize(request.Width, request.Height);
            result.Success = true;
            result.DrawCallCount = 0;
            result.RenderedEntityCount = 0;
            return result;
        }

        result.Success = false;
        result.ErrorMessage = "No active rendering backend available";
        return result;
    }

    RenderResult RenderService::RenderView(Scene& scene, const ViewRequest& request) {
        if (!m_Initialized) {
            if (!Init()) {
                return RenderResult{ false, {}, 0, 0, "RenderService failed to initialize" };
            }
        }

        RenderResult result;

        // Modern RHI path
        if (m_RHIRenderer && RHI::RHIDevice::Get().IsNativeReady()) {
            RHI::RHITexture* colorTarget = request.ColorTarget;
            RHI::RHITexture* depthTarget = request.DepthTarget;

            if (!colorTarget && request.ViewTargets && request.ViewTargets->IsValid()) {
                colorTarget = request.ViewTargets->Get(RHI::RHIViewTarget::Color);
                depthTarget = request.ViewTargets->Get(RHI::RHIViewTarget::Depth);
            }

            if (!colorTarget) {
                result.Success = false;
                result.ErrorMessage = "No valid modern RHI color target provided in ViewRequest";
                return result;
            }

            // Extract RenderWorld snapshot and render
            SceneExtractionOptions options;
            options.ResolveResources = true;
            RenderWorld world = ExtractRenderWorld(scene, options);

            m_RHIRenderer->RenderWorldView(world,
                                           request.Camera,
                                           colorTarget,
                                           depthTarget,
                                           request.Settings,
                                           request.PhysicsScene,
                                           request.ViewTargets);

            result.Success = true;
            result.DrawCallCount = m_RHIRenderer->GetDrawCallCount();
            result.RenderedEntityCount = m_RHIRenderer->GetRenderedEntityCount();
            result.CompletionToken = RHI::RHICompletionToken{ 1 };
            return result;
        }

        // OpenGL compatibility path
        if (m_GLRenderer && m_GLRenderer->IsReady()) {
            m_GLRenderer->SetViewportSize(request.Width, request.Height);
            m_GLRenderer->RenderScene(std::shared_ptr<Scene>(&scene, [](Scene*){}),
                                     request.Camera,
                                     request.GLTargetFBO,
                                     request.PhysicsScene,
                                     nullptr,
                                     request.Settings);
            result.Success = true;
            result.DrawCallCount = 0;
            const auto& reg = scene.GetRegistry();
            result.RenderedEntityCount = reg.storage<entt::entity>() ? static_cast<uint32_t>(reg.storage<entt::entity>()->size()) : 0;
            return result;
        }

        result.Success = false;
        result.ErrorMessage = "No active rendering backend available";
        return result;
    }

}
