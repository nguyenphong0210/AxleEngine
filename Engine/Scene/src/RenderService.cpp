#include <Axle/RenderService.hpp>
#include <Axle/Log.hpp>
#include <Axle/Window.hpp>
#include <Axle/MeshResourceResolver.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/ImageSource.hpp>
#include <GLFW/glfw3.h>
#include <cmath>
#include <string_view>

namespace Axle {
    RenderService& RenderService::Get() { static RenderService service; return service; }
    RenderService::RenderService() : m_StateSnapshot(PackState(RenderServiceState::Stopped, 1)) {}
    RenderService::~RenderService() { Shutdown(); m_Lifetime.reset(); }
    bool RenderService::ConfigureProductArguments(int argc, char* const* argv, std::string& error) {
        RenderFeatureRequests requests;
        if (!ParseRenderFeatureArguments(argc, argv, requests, error)) return false;
        auto policy = EvaluateRenderProductRollout(requests);
        std::lock_guard lock(m_OwnerMutex);
        if (m_Initialized || m_Owner != std::thread::id{}) { error = "Product render policy requires stopped service"; return false; }
        m_ProductRollout = std::move(policy);
        error.clear();
        if (!m_ProductRollout.CanStart) {
            for (const auto& reason : m_ProductRollout.Reasons) error += reason + "\n";
            return false;
        }
        return true;
    }
    RenderProductRollout RenderService::GetProductRollout() const {
        std::lock_guard lock(m_OwnerMutex); return m_ProductRollout;
    }

    uint64_t RenderService::PackState(RenderServiceState state, uint64_t epoch) {
        constexpr uint64_t stateMask = 0xffu;
        return (epoch << 8u) | (static_cast<uint64_t>(state) & stateMask);
    }
    RenderStateSnapshot RenderService::UnpackState(uint64_t packed) {
        return {static_cast<RenderServiceState>(packed & 0xffu), packed >> 8u};
    }

    void RenderService::SetState(RenderServiceState state) {
        auto current = m_StateSnapshot.load(std::memory_order_acquire);
        for (;;) {
            const auto previous = UnpackState(current);
            const auto epoch = previous.State == state ? previous.StateEpoch : previous.StateEpoch + 1;
            const auto desired = PackState(state, epoch);
            if (m_StateSnapshot.compare_exchange_weak(current, desired, std::memory_order_acq_rel)) return;
        }
    }

    bool RenderService::Init() {
        bool initialized = false;
        bool canStart = false;
        std::thread::id owner;
        {
            std::lock_guard lock(m_OwnerMutex);
            initialized = m_Initialized.load(std::memory_order_acquire);
            owner = m_Owner;
            canStart = m_ProductRollout.CanStart;
        }
        if (!canStart) { SetState(RenderServiceState::Failed); return false; }
        auto& device = RHI::RHIDevice::Get();
        void* context = Window::HasActiveContext() ? glfwGetCurrentContext() : nullptr;
        if (initialized && owner == std::this_thread::get_id() &&
            ((m_RHIRenderer && m_Epoch == device.GetEpoch() && device.CanCreateResources()) ||
             (m_GLRenderer && m_GLContext == context && context))) return true;
        if (owner != std::thread::id{} && owner != std::this_thread::get_id()) {
            AXLE_CORE_ERROR("RenderService::Init rejected on a non-owner thread");
            return false;
        }
        Shutdown();
        SetState(RenderServiceState::Starting);
        {
            std::lock_guard lock(m_OwnerMutex);
            m_Owner = std::this_thread::get_id();
        }
        auto failInitialization = [&] {
            SetState(RenderServiceState::Failed);
            std::lock_guard lock(m_OwnerMutex);
            m_Owner = {};
            return false;
        };
        if (device.IsNativeReady()) {
            if (!device.CanCreateResources()) return failInitialization();
            auto candidate = std::make_unique<RHISceneRenderer>();
            if (!candidate->Init()) return failInitialization();
            m_RHIRenderer = std::move(candidate); m_Epoch = device.GetEpoch();
            std::weak_ptr<int> alive = m_Lifetime;
            device.GetResources().BeforeDeviceClose([this, alive] { if (alive.lock()) Shutdown(); });
        } else if (context) {
            auto candidate = std::make_unique<SceneRenderer>();
            if (!candidate->IsReady()) return failInitialization();
            m_GLRenderer = std::move(candidate); m_GLContext = context;
        } else return failInitialization();
        m_Initialized.store(true, std::memory_order_release); SetState(RenderServiceState::Running); return true;
    }
    void RenderService::Shutdown() {
        std::thread::id owner;
        {
            std::lock_guard lock(m_OwnerMutex);
            owner = m_Owner;
        }
        if (owner != std::thread::id{} && owner != std::this_thread::get_id()) {
            AXLE_CORE_ERROR("RenderService::Shutdown rejected on a non-owner thread");
            return;
        }
        if (m_Initialized.load(std::memory_order_acquire)) SetState(RenderServiceState::Draining);
        m_DefaultMainTargets.reset(); m_GLMeshes.clear(); m_NativeSourceMeshes.clear();
        m_RHIRenderer.reset(); m_GLRenderer.reset();
        m_Initialized.store(false, std::memory_order_release); m_Epoch = 0; m_GLContext = nullptr;
        {
            std::lock_guard lock(m_OwnerMutex);
            m_Owner = {};
        }
        SetState(RenderServiceState::Stopped);
    }
    bool RenderService::IsOwnerThread() const {
        std::thread::id owner;
        {
            std::lock_guard lock(m_OwnerMutex);
            owner = m_Owner;
        }
        if (owner == std::thread::id{} || owner != std::this_thread::get_id()) return false;
        const auto& device = RHI::RHIDevice::Get();
        return !device.IsNativeReady() || device.IsOwnerThread();
    }
    RenderServiceState RenderService::GetState() const {
        return GetStateSnapshot().State;
    }
    RenderStateSnapshot RenderService::GetStateSnapshot() const {
        const auto& device = RHI::RHIDevice::Get();
        for (;;) {
            const auto serviceBefore = UnpackState(m_StateSnapshot.load(std::memory_order_acquire));
            const auto rhiBefore = device.GetStateSnapshot();
            const auto serviceAfter = UnpackState(m_StateSnapshot.load(std::memory_order_acquire));
            const auto rhiAfter = device.GetStateSnapshot();
            if (serviceBefore.StateEpoch != serviceAfter.StateEpoch || rhiBefore.StateEpoch != rhiAfter.StateEpoch) continue;

            RenderServiceState state = serviceBefore.State;
            switch (rhiBefore.State) {
                case RHI::RHIDeviceState::SurfaceRebuildRequired: state = RenderServiceState::SurfaceRebuildRequired; break;
                case RHI::RHIDeviceState::Lost: state = RenderServiceState::DeviceLost; break;
                case RHI::RHIDeviceState::Stopping: state = RenderServiceState::Draining; break;
                default: break;
            }
            // Both domains are monotonic. Packing them keeps the producer-visible
            // epoch monotonic while preserving which domain advanced it.
            const auto epoch = (serviceBefore.StateEpoch << 32u) | (rhiBefore.StateEpoch & 0xffffffffu);
            return {state, epoch};
        }
    }
    uint64_t RenderService::GetStateEpoch() const {
        return GetStateSnapshot().StateEpoch;
    }
    bool RenderService::RequireOwner(std::string_view operation) const {
        if (IsOwnerThread()) return true;
        AXLE_CORE_ERROR("RenderService operation '{0}' rejected on a non-owner thread", operation);
        return false;
    }
    RenderOwnershipReport RenderService::GetOwnershipReport() const {
        const auto& device = RHI::RHIDevice::Get();
        RenderOwnershipReport report;
        const auto state = GetStateSnapshot();
        report.State = state.State;
        report.StateEpoch = state.StateEpoch;
        {
            std::lock_guard lock(m_OwnerMutex);
            report.OwnerBound = m_Owner != std::thread::id{};
        }
        report.IsOwnerThread = IsOwnerThread();
        report.NativeReady = device.IsNativeReady();
        report.ContextReady = device.IsContextReady();
        report.RHIIsOwnerThread = device.IsOwnerThread();
        report.RHIState = device.GetStateSnapshot();
        report.OwnershipAligned = report.OwnerBound && report.IsOwnerThread &&
            (!report.NativeReady || report.RHIIsOwnerThread);
        report.Backend = device.GetBackend();
        report.DeviceCapabilities = device.GetCapabilities();

        if (!report.OwnerBound) report.OwnerDiagnostic = "RenderService has no bound owner thread";
        else if (!report.OwnershipAligned) report.OwnerDiagnostic = "RenderService and RHI resource ownership are not aligned";
        else if (report.Backend == RHI::RHIType::OpenGL) report.OwnerDiagnostic = "OpenGL remains synchronous until context transfer is proven";
        else if (!report.NativeReady) report.OwnerDiagnostic = "Native RHI device is not ready";
        else if (report.RHIState.State == RHI::RHIDeviceState::Lost) report.OwnerDiagnostic = "Native RHI device is lost";
        else if (report.RHIState.State == RHI::RHIDeviceState::SurfaceRebuildRequired) report.OwnerDiagnostic = "Native RHI surface rebuild is required";
        else if (!report.DeviceCapabilities.SupportsRenderWorkerOwnership) report.OwnerDiagnostic = "Native render-worker ownership requires the C11-03 probe";
        else report.OwnerDiagnostic = "Native render-worker ownership is enabled";
        return report;
    }
    RendererCapabilities RenderService::GetActiveCapabilities() const {
        const auto& device = RHI::RHIDevice::Get();
        if (m_RHIRenderer && device.IsNativeReady() && m_Epoch == device.GetEpoch())
            return QueryRendererCapabilities(device.GetBackend() == RHI::RHIType::Vulkan ?
                RendererBackendType::ModernRHI_Vulkan : RendererBackendType::ModernRHI_D3D12);
        if (m_GLRenderer && Window::HasActiveContext() && m_GLContext == glfwGetCurrentContext())
            return QueryRendererCapabilities(RendererBackendType::OpenGL_Compatibility);
        return QueryRendererCapabilities(RendererBackendType::None);
    }
    RenderResult RenderService::RenderView(const RenderWorld& world, const ViewRequest& request) {
        RenderResult result;
        auto fail = [&](std::string error) { result.ErrorMessage = std::move(error); return result; };
        if (!world.ErrorMessage.empty()) return fail(world.ErrorMessage);
        if (!request.Width || !request.Height) return fail("A minimized or empty view cannot record GPU work");
        if (!Init()) return fail("RenderService backend/context is unavailable");
        // Resource resolution happens after extraction, on owned records only.
        auto prepared = world;
        auto meshKey = [](const RenderMeshInstance& mesh) {
            auto key = std::to_string(FileSystem::GetSourceGeneration()) + ":" + mesh.MeshURI;
            if (mesh.Source) return key + ":source:" + std::to_string(MeshSourceRevision(*mesh.Source));
            if (!mesh.MeshURI.empty() && !ParseBuiltinMesh(mesh.MeshURI)) {
                std::error_code error;
                const auto path = FileSystem::ResolveAssetPath(mesh.MeshURI);
                const auto revision = std::filesystem::last_write_time(path,error);
                if (!error) key += ":" + std::to_string(revision.time_since_epoch().count());
            }
            return key;
        };
        if (m_RHIRenderer) for (auto& mesh : prepared.Meshes) if (mesh.Source && !mesh.NativeMesh) {
            if (auto found=m_NativeSourceMeshes.find(meshKey(mesh));found!=m_NativeSourceMeshes.end()) mesh.NativeMesh=found->second;
        }
        if (m_GLRenderer) for (auto& mesh : prepared.Meshes) {
            if (!mesh.GLMesh) {
                const auto found = m_GLMeshes.find(meshKey(mesh));
                if (found != m_GLMeshes.end()) mesh.GLMesh = found->second;
            }
        }
        if (!ResolveRenderWorldResources(prepared)) return fail(prepared.ErrorMessage);
        if (m_RHIRenderer) for (const auto& mesh : prepared.Meshes) if (mesh.Source && mesh.NativeMesh) {
            if (m_NativeSourceMeshes.size()>=256) m_NativeSourceMeshes.clear();
            m_NativeSourceMeshes[meshKey(mesh)]=mesh.NativeMesh;
        }
        if (m_GLRenderer) for (auto& mesh : prepared.Meshes) {
            if (!mesh.MeshURI.empty() && mesh.GLMesh) {
                if (m_GLMeshes.size() >= 256) m_GLMeshes.clear();
                m_GLMeshes[meshKey(mesh)] = mesh.GLMesh;
            }
            if (mesh.Material.MaterialAsset) {
                auto material = std::make_shared<Material>(*mesh.Material.MaterialAsset);
                auto load = [&](const std::string& path, bool srgb) {
                    TextureSpecification specification; specification.sRGB = srgb; specification.FlipVertically = false;
                    return ImageTextureCache::GetGL(LoadImageSource(path, {}), specification);
                };
                if (material->IsAlbedoMapEnabled() && !material->GetAlbedoMap() && !material->GetAlbedoPath().empty())
                    material->SetAlbedoTexture(load(material->GetAlbedoPath(),true),material->GetAlbedoPath());
                if (material->IsNormalMapEnabled() && !material->GetNormalMap() && !material->GetNormalPath().empty())
                    material->SetNormalTexture(load(material->GetNormalPath(),false),material->GetNormalPath());
                if (material->IsRoughnessMapEnabled() && !material->GetRoughnessMap() && !material->GetRoughnessPath().empty())
                    material->SetRoughnessTexture(load(material->GetRoughnessPath(),false),material->GetRoughnessPath());
                if (material->IsMetallicMapEnabled() && !material->GetMetallicMap() && !material->GetMetallicPath().empty())
                    material->SetMetallicTexture(load(material->GetMetallicPath(),false),material->GetMetallicPath());
                if (material->IsORMMapEnabled() && !material->GetORMMap() && !material->GetORMPath().empty())
                    material->SetORMTexture(load(material->GetORMPath(),false),material->GetORMPath());
                mesh.Material.MaterialAsset = std::move(material);
            }
        }
        if (m_RHIRenderer) {
            auto& device = RHI::RHIDevice::Get();
            if (!device.CanSubmit()) return fail("Render device cannot submit");
            auto targets = request.ViewTargets;
            auto* color = request.ColorTarget; auto* depth = request.DepthTarget;
            if (!color && targets && targets->IsValid()) {
                color = targets->Get(RHI::RHIViewTarget::Color); depth = targets->Get(RHI::RHIViewTarget::Depth);
            }
            if (!color && request.Kind == RenderViewKind::Main) {
                RHI::RHIViewTargetDesc desc;
                desc.Width = request.Width; desc.Height = request.Height;
                desc.Samples = request.Settings.MSAASamples; desc.HDR = request.Settings.EnableToneMapping; desc.FXAA = request.Settings.EnableFXAA;
                if (!m_DefaultMainTargets || !m_DefaultMainTargets->IsValid() || m_DefaultMainTargets->GetDesc() != desc) {
                    auto candidate = std::make_shared<RHI::RHIViewTargets>();
                    if (!candidate->Configure(desc)) return fail("Default view target creation failed; previous generation retained");
                    m_DefaultMainTargets = std::move(candidate);
                }
                targets = m_DefaultMainTargets;
                color = targets->Get(RHI::RHIViewTarget::Color); depth = targets->Get(RHI::RHIViewTarget::Depth);
            }
            if (!color || !color->IsValid() || !color->GetRTV() || color->GetHandle().Epoch != m_Epoch ||
                color->GetWidth() != request.Width || color->GetHeight() != request.Height || color->GetDesc().SampleCount != 1)
                return fail("Invalid, stale or mismatched view color target");
            if (depth && (!depth->IsValid() || !depth->GetDSV() || depth->GetHandle().Epoch != m_Epoch ||
                depth->GetWidth() != request.Width || depth->GetHeight() != request.Height || depth->GetDesc().SampleCount != 1))
                return fail("Invalid, stale or mismatched view depth target");
            m_RHIRenderer->RenderWorldView(prepared, request.Camera, color, depth, request.Settings, request.PhysicsScene, targets);
            if (!m_RHIRenderer->WasLastRenderSuccessful()) return fail("View preparation, recording or submission failed");
            result.Success = true; result.CompletionToken = m_RHIRenderer->GetLastCompletion();
            result.DrawCallCount = m_RHIRenderer->GetDrawCallCount(); result.RenderedEntityCount = m_RHIRenderer->GetRenderedEntityCount();
            if (request.Settings.Wireframe || (request.Settings.EnableShadows && prepared.DirectionalLight && prepared.DirectionalLight->CastsShadows) ||
                !prepared.Skins.empty() || (prepared.Skybox.Enabled && prepared.Skybox.Type == 1))
                result.ErrorMessage = GetActiveCapabilities().AdvancedFeatureDiagnostic;
            return result;
        }
        if (request.GLTargetFBO && (request.GLTargetFBO->GetSpecification().Width != request.Width || request.GLTargetFBO->GetSpecification().Height != request.Height))
            return fail("GL view target extent does not match request");
        m_GLRenderer->SetViewportSize(request.Width, request.Height);
        m_GLRenderer->RenderWorldView(prepared, request.Camera, request.GLTargetFBO, request.PhysicsScene, nullptr, request.Settings);
        result.Success = true; result.DrawCallCount = static_cast<uint32_t>(prepared.Meshes.size());
        result.RenderedEntityCount = result.DrawCallCount;
        // GL executes on the current context; an NRI completion token has no meaning here.
        if (request.Settings.EnableFXAA || request.Settings.EnableToneMapping || request.Settings.MSAASamples > 1 ||
            std::any_of(prepared.Meshes.begin(),prepared.Meshes.end(),[](const auto& mesh){return mesh.Material.UseCustomShader;}))
            result.ErrorMessage = GetActiveCapabilities().AdvancedFeatureDiagnostic;
        return result;
    }
    RenderResult RenderService::RenderView(Scene& scene, const ViewRequest& request) {
        SceneExtractionOptions options; options.CameraOverride = &request.Camera;
        auto world = ExtractRenderWorld(scene, options);
        auto result = RenderView(world, request);
        if (result.Success && m_RHIRenderer) m_RHIRenderer->PublishMaterialFeedback(scene, world);
        return result;
    }
    RHI::RHITexture* RenderService::GetDefaultMainColorTarget() const {
        return m_DefaultMainTargets && m_DefaultMainTargets->IsValid() ? m_DefaultMainTargets->Get(RHI::RHIViewTarget::Color) : nullptr;
    }
}
