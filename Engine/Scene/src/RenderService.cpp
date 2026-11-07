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

        m_DefaultMainTargets.reset();
        if (m_RHIRenderer) {
            m_RHIRenderer->Shutdown();
            m_RHIRenderer.reset();
        }

        m_GLRenderer.reset();
        m_Initialized = false;
    }

    RendererCapabilities RenderService::GetActiveCapabilities() const {
        if (m_RHIRenderer && RHI::RHIDevice::Get().IsNativeReady()) {
            auto backend = RHI::RHIDevice::Get().GetBackend();
            if (backend == RHI::RHIType::Vulkan) {
                return QueryRendererCapabilities(RendererBackendType::ModernRHI_Vulkan);
            }
            return QueryRendererCapabilities(RendererBackendType::ModernRHI_D3D12);
        }
        if (m_GLRenderer && m_GLRenderer->IsReady()) {
            return QueryRendererCapabilities(RendererBackendType::OpenGL_Compatibility);
        }
        return QueryRendererCapabilities(RendererBackendType::None);
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

            if (!colorTarget && request.Kind == RenderViewKind::Main && request.Width > 0 && request.Height > 0) {
                if (!m_DefaultMainTargets || m_DefaultMainTargets->GetDesc().Width != request.Width || m_DefaultMainTargets->GetDesc().Height != request.Height) {
                    m_DefaultMainTargets = std::make_shared<RHI::RHIViewTargets>();
                    RHI::RHIViewTargetDesc desc;
                    desc.Width = request.Width;
                    desc.Height = request.Height;
                    m_DefaultMainTargets->Configure(desc);
                }
                if (m_DefaultMainTargets->IsValid()) {
                    colorTarget = m_DefaultMainTargets->Get(RHI::RHIViewTarget::Color);
                    depthTarget = m_DefaultMainTargets->Get(RHI::RHIViewTarget::Depth);
                }
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
            if (request.Settings.Wireframe && !GetActiveCapabilities().SupportsWireframeMode) {
                result.ErrorMessage = "Wireframe mode is not supported on modern RHI backend; rendered solid geometry.";
            }
            return result;
        }

        // OpenGL compatibility path
        if (m_GLRenderer && m_GLRenderer->IsReady()) {
            m_GLRenderer->SetViewportSize(request.Width, request.Height);
            m_GLRenderer->RenderWorldView(world,
                                          request.Camera,
                                          request.GLTargetFBO,
                                          request.PhysicsScene,
                                          nullptr,
                                          request.Settings);
            result.Success = true;
            result.DrawCallCount = static_cast<uint32_t>(world.GetMeshCount());
            result.RenderedEntityCount = static_cast<uint32_t>(world.GetMeshCount());
            result.CompletionToken = RHI::RHICompletionToken{ 1, 1 };

            if (request.Settings.EnableFXAA || request.Settings.EnableToneMapping) {
                result.ErrorMessage = GetActiveCapabilities().AdvancedFeatureDiagnostic;
            }
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

            if (!colorTarget && request.Kind == RenderViewKind::Main && request.Width > 0 && request.Height > 0) {
                if (!m_DefaultMainTargets || m_DefaultMainTargets->GetDesc().Width != request.Width || m_DefaultMainTargets->GetDesc().Height != request.Height) {
                    m_DefaultMainTargets = std::make_shared<RHI::RHIViewTargets>();
                    RHI::RHIViewTargetDesc desc;
                    desc.Width = request.Width;
                    desc.Height = request.Height;
                    m_DefaultMainTargets->Configure(desc);
                }
                if (m_DefaultMainTargets->IsValid()) {
                    colorTarget = m_DefaultMainTargets->Get(RHI::RHIViewTarget::Color);
                    depthTarget = m_DefaultMainTargets->Get(RHI::RHIViewTarget::Depth);
                }
            }

            if (!colorTarget) {
                result.Success = false;
                result.ErrorMessage = "No valid modern RHI color target provided in ViewRequest";
                return result;
            }

            std::shared_ptr<Scene> scenePtr(&scene, [](Scene*){});
            m_RHIRenderer->RenderScene(scenePtr,
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
            if (request.Settings.Wireframe && !GetActiveCapabilities().SupportsWireframeMode) {
                result.ErrorMessage = "Wireframe mode is not supported on modern RHI backend; rendered solid geometry.";
            }
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
            if (request.Settings.EnableFXAA || request.Settings.EnableToneMapping) {
                result.ErrorMessage = GetActiveCapabilities().AdvancedFeatureDiagnostic;
            }
            return result;
        }

        result.Success = false;
        result.ErrorMessage = "No active rendering backend available";
        return result;
    }

    RHI::RHITexture* RenderService::GetDefaultMainColorTarget() const {
        return m_DefaultMainTargets && m_DefaultMainTargets->IsValid() ? m_DefaultMainTargets->Get(RHI::RHIViewTarget::Color) : nullptr;
    }

}
