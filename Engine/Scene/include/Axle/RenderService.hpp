#pragma once
#include <Axle/RenderWorld.hpp>
#include <Axle/ViewRequest.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/SceneRenderer.hpp>
#include <Axle/RenderCapabilities.hpp>
#include <Axle/RenderRollout.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Axle {

    enum class RenderServiceState {
        Stopped,
        Starting,
        Running,
        SurfaceRebuildRequired,
        DeviceLost,
        Draining,
        Failed
    };

    struct RenderStateSnapshot {
        RenderServiceState State = RenderServiceState::Stopped;
        uint64_t StateEpoch = 0;
    };

    struct RenderOwnershipReport {
        RenderServiceState State = RenderServiceState::Stopped;
        uint64_t StateEpoch = 0;
        bool OwnerBound = false;
        bool IsOwnerThread = false;
        bool NativeReady = false;
        bool ContextReady = false;
        bool RHIIsOwnerThread = false;
        bool OwnershipAligned = false;
        RHI::RHIStateSnapshot RHIState{};
        RHI::RHIType Backend = RHI::RHIType::None;
        RHI::RHIDeviceCapabilities DeviceCapabilities{};
        std::string OwnerDiagnostic;
    };

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
        RenderServiceState GetState() const;
        RenderStateSnapshot GetStateSnapshot() const;
        uint64_t GetStateEpoch() const;
        bool IsOwnerThread() const;
        bool RequireOwner(std::string_view operation) const;
        RenderOwnershipReport GetOwnershipReport() const;
        bool ConfigureProductArguments(int argc, char* const* argv, std::string& error);
        RenderProductRollout GetProductRollout() const;

        // Primary rendering entry points
        RenderResult RenderView(const RenderWorld& world, const ViewRequest& request);
        RenderResult RenderView(Scene& scene, const ViewRequest& request);

        // Product-facing viewport and interaction seams. Applications do not
        // reach through to a concrete backend renderer for these operations.
        void SetViewportSize(uint32_t width, uint32_t height);
        uint32_t GetPhysicsDebugLineCount() const;
        bool CanPickEntity() const;
        uint32_t PickEntity(const std::shared_ptr<Scene>& scene, const PerspectiveCamera& camera,
                            uint32_t width, uint32_t height, uint32_t x, uint32_t y) const;
        void SetUseIBL(bool enabled);

        // Capabilities and honest diagnostics
        RendererCapabilities GetActiveCapabilities() const;

        RHI::RHITexture* GetDefaultMainColorTarget() const;

    private:
        static uint64_t PackState(RenderServiceState state, uint64_t epoch);
        static RenderStateSnapshot UnpackState(uint64_t packed);
        void SetState(RenderServiceState state);
        std::atomic<bool> m_Initialized{false};
        mutable std::atomic<uint64_t> m_StateSnapshot{0};
        uint64_t m_Epoch = 0;
        void* m_GLContext = nullptr;
        mutable std::mutex m_OwnerMutex;
        std::thread::id m_Owner;
        RenderProductRollout m_ProductRollout = EvaluateRenderProductRollout({});
        std::shared_ptr<int> m_Lifetime = std::make_shared<int>(0);
        std::unordered_map<std::string, std::shared_ptr<VertexArray>> m_GLMeshes;
        std::unordered_map<std::string, std::shared_ptr<RHI::RHIMesh>> m_NativeSourceMeshes;
        std::unique_ptr<RHISceneRenderer> m_RHIRenderer;
        std::unique_ptr<SceneRenderer> m_GLRenderer;
        std::shared_ptr<RHI::RHIViewTargets> m_DefaultMainTargets;
    };

}
