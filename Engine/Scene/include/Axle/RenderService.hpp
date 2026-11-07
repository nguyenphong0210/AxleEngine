#pragma once
#include <Axle/RenderWorld.hpp>
#include <Axle/ViewRequest.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/SceneRenderer.hpp>
#include <Axle/RenderCapabilities.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <memory>

namespace Axle {

    // Central rendering coordinator.
    // Orchestrates views, handles renderer selection (modern RHI vs OpenGL compatibility),
    // and returns structured RenderResult.
    class RenderService {
    public:
        static RenderService& Get();

        RenderService();
        ~RenderService();

        RenderService(const RenderService&) = delete;
        RenderService& operator=(const RenderService&) = delete;

        bool Init();
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }

        // Primary rendering entry points
        RenderResult RenderView(const RenderWorld& world, const ViewRequest& request);
        RenderResult RenderView(Scene& scene, const ViewRequest& request);

        // Access underlying renderer backends
        RHISceneRenderer* GetRHIRenderer() { return m_RHIRenderer.get(); }
        SceneRenderer* GetGLRenderer() { return m_GLRenderer.get(); }

        // Capabilities and honest diagnostics
        RendererCapabilities GetActiveCapabilities() const;

        RHI::RHITexture* GetDefaultMainColorTarget() const;

    private:
        bool m_Initialized = false;
        std::unique_ptr<RHISceneRenderer> m_RHIRenderer;
        std::unique_ptr<SceneRenderer> m_GLRenderer;
        std::shared_ptr<RHI::RHIViewTargets> m_DefaultMainTargets;
    };

}
