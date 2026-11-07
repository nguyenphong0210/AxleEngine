#include <Axle/RenderService.hpp>
#include <Axle/Log.hpp>
#include <Axle/Window.hpp>
#include <Axle/MeshResourceResolver.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/ImageSource.hpp>
#include <GLFW/glfw3.h>
#include <cmath>

namespace Axle {
    RenderService& RenderService::Get() { static RenderService service; return service; }
    RenderService::RenderService() = default;
    RenderService::~RenderService() { Shutdown(); m_Lifetime.reset(); }

    bool RenderService::Init() {
        auto& device = RHI::RHIDevice::Get();
        void* context = Window::HasActiveContext() ? glfwGetCurrentContext() : nullptr;
        if (m_Initialized && m_Owner == std::this_thread::get_id() &&
            ((m_RHIRenderer && m_Epoch == device.GetEpoch() && device.CanCreateResources()) ||
             (m_GLRenderer && m_GLContext == context && context))) return true;
        if (m_Initialized && m_Owner != std::this_thread::get_id()) return false;
        Shutdown();
        m_Owner = std::this_thread::get_id();
        if (device.IsNativeReady()) {
            if (!device.CanCreateResources()) return false;
            auto candidate = std::make_unique<RHISceneRenderer>();
            if (!candidate->Init()) return false;
            m_RHIRenderer = std::move(candidate); m_Epoch = device.GetEpoch();
            std::weak_ptr<int> alive = m_Lifetime;
            device.GetResources().BeforeDeviceClose([this, alive] { if (alive.lock()) Shutdown(); });
        } else if (context) {
            auto candidate = std::make_unique<SceneRenderer>();
            if (!candidate->IsReady()) return false;
            m_GLRenderer = std::move(candidate); m_GLContext = context;
        } else return false;
        m_Initialized = true; return true;
    }
    void RenderService::Shutdown() {
        m_DefaultMainTargets.reset(); m_GLMeshes.clear();
        m_RHIRenderer.reset(); m_GLRenderer.reset();
        m_Initialized = false; m_Epoch = 0; m_GLContext = nullptr;
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
        if (m_GLRenderer) for (auto& mesh : prepared.Meshes) {
            if (!mesh.GLMesh) {
                const auto found = m_GLMeshes.find(mesh.MeshURI);
                if (found != m_GLMeshes.end()) mesh.GLMesh = found->second;
            }
        }
        if (!ResolveRenderWorldResources(prepared)) return fail(prepared.ErrorMessage);
        if (m_GLRenderer) for (auto& mesh : prepared.Meshes) {
            if (!mesh.MeshURI.empty() && mesh.GLMesh) {
                if (m_GLMeshes.size() >= 256) m_GLMeshes.clear();
                m_GLMeshes[mesh.MeshURI] = mesh.GLMesh;
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
