#include <Axle/SceneExtraction.hpp>
#include <Axle/AssetThumbnailManager.hpp>
#include <Axle/Application.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/RenderService.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/ViewRequest.hpp>
#include <Axle/Framebuffer.hpp>
#include <Extensions/NRIHelper.h>
#include <Axle/ImageSource.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>
#include <Axle/Log.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <Axle/FileSystem.hpp>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

namespace Axle {

    AssetThumbnailManager* AssetThumbnailManager::s_Instance = nullptr;

    uint64_t AssetThumbnailManager::GetPreview(const std::filesystem::path& path) {
        return s_Instance ? s_Instance->GetOrCreateThumbnail(path) : 0;
    }

    AssetThumbnailManager::AssetThumbnailManager() {
        s_Instance = this;
    }

    AssetThumbnailManager::~AssetThumbnailManager() {
        Clear();
        if (s_Instance == this) {
            s_Instance = nullptr;
        }
    }

    void AssetThumbnailManager::InvalidateThumbnail(const std::filesystem::path& path) {
        std::string key = path.lexically_normal().string();
        m_GuiThumbnails.erase(key);
        std::vector<uint64_t> handles;
        std::vector<std::shared_ptr<RHI::RHITexture>> textures;
        std::vector<std::shared_ptr<Texture2D>> loaded;
        std::vector<u32> generated;
        if (auto it = m_ThumbnailCache.find(key); it != m_ThumbnailCache.end()) {
            handles.push_back(it->second);
            if (auto id = std::find(m_GeneratedTextures.begin(), m_GeneratedTextures.end(), it->second);
                id != m_GeneratedTextures.end()) {
                generated.push_back(*id);
                m_GeneratedTextures.erase(id);
            }
            m_ThumbnailCache.erase(it);
        }
        if (auto it = m_RHIThumbnails.find(key); it != m_RHIThumbnails.end()) {
            textures.push_back(std::move(it->second));
            m_RHIThumbnails.erase(it);
        }
        if (auto it = m_LoadedTextures.find(key); it != m_LoadedTextures.end()) {
            loaded.push_back(std::move(it->second));
            m_LoadedTextures.erase(it);
        }
        if (auto it = m_GLThumbnails.find(key); it != m_GLThumbnails.end()) {
            auto retired = std::move(it->second); m_GLThumbnails.erase(it);
            if (m_DeferredRelease) m_DeferredRelease([retired] {});
        }
        m_ThumbnailDimensions.erase(key);
        m_MetaCache.erase(key);
        m_AssetRevisions.erase(key);

        auto qIt = std::find(m_PendingQueue.begin(), m_PendingQueue.end(), path);
        if (qIt != m_PendingQueue.end()) {
            m_PendingQueue.erase(qIt);
        }

        RetireTextures(std::move(handles), std::move(textures), std::move(loaded), std::move(generated));
    }

    void AssetThumbnailManager::Invalidate(const std::filesystem::path& path) {
        if (s_Instance) {
            s_Instance->InvalidateThumbnail(path);
        }
    }

    void AssetThumbnailManager::SetImGuiLayer(ImGuiLayer* layer) {
        m_ImGuiLayer = layer;
        m_TextureRegistrar = layer ? layer->GetTextureRegistrar() : GuiTextureRegistrar{};
        m_DeferredRelease = layer ? layer->GetDeferredReleaser() : GuiDeferredReleaser{};
    }

    void AssetThumbnailManager::RetireTextures(std::vector<uint64_t> handles,
            std::vector<std::shared_ptr<RHI::RHITexture>> textures,
            std::vector<std::shared_ptr<Texture2D>> loaded, std::vector<u32> generated) {
        // Native registrations retain complete payload generations themselves.
        if (loaded.empty() && generated.empty()) return;
        auto release = [loaded = std::move(loaded), generated = std::move(generated)] {
            if (Window::HasActiveContext()) for (auto id : generated) if (id) glDeleteTextures(1, &id);
        };
        if (m_DeferredRelease) m_DeferredRelease(std::move(release)); else release();
    }

    void AssetThumbnailManager::Clear() {
        auto framebuffers = std::move(m_GLThumbnails); m_GLThumbnails.clear();
        if (m_DeferredRelease && !framebuffers.empty()) m_DeferredRelease([framebuffers = std::move(framebuffers)] {});
        m_GuiThumbnails.clear();
        std::vector<uint64_t> handles;
        std::vector<std::shared_ptr<RHI::RHITexture>> textures;
        std::vector<std::shared_ptr<Texture2D>> loaded;
        for (const auto& entry : m_ThumbnailCache) if (entry.second) handles.push_back(entry.second);
        for (auto& entry : m_RHIThumbnails) textures.push_back(std::move(entry.second));
        for (auto& entry : m_LoadedTextures) loaded.push_back(std::move(entry.second));
        RetireTextures(std::move(handles), std::move(textures), std::move(loaded), std::move(m_GeneratedTextures));
        m_GeneratedTextures.clear();
        m_ThumbnailCache.clear();
        m_ThumbnailDimensions.clear();
        m_RHIThumbnails.clear();
        m_MetaCache.clear();
        m_LoadedTextures.clear();
        m_AssetRevisions.clear();
        m_PendingQueue.clear();
    }

    glm::vec2 AssetThumbnailManager::GetThumbnailDimensions(const std::filesystem::path& path) const {
        std::string key = path.lexically_normal().string();
        auto it = m_ThumbnailDimensions.find(key);
        if (it != m_ThumbnailDimensions.end()) {
            return it->second;
        }
        return glm::vec2(1.0f, 1.0f);
    }

    const AssetCachedMeta* AssetThumbnailManager::GetCachedMeta(const std::filesystem::path& path) const {
        std::string key = path.lexically_normal().string();
        auto it = m_MetaCache.find(key);
        if (it != m_MetaCache.end()) {
            return &it->second;
        }
        return nullptr;
    }

    uint64_t AssetThumbnailManager::GetOrCreateThumbnail(const std::filesystem::path& path) {


        std::string key = path.lexically_normal().string();
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });

        // Resolve source image path if this is a Texture descriptor or raw media
        std::filesystem::path imagePath;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr") {
            imagePath = FileSystem::ResolveAssetPath(path);
        } else if (ext == ".axleasset" && AssetDSL::PeekAssetKindFromFile(path.string()) == AssetKind::Texture) {
            TextureDescriptor desc;
            if (TextureDescriptor::Load(path.string(), desc) && !desc.Source.empty()) {
                imagePath = FileSystem::ResolveAssetPath(desc.Source);
            }
        }

        if (!imagePath.empty()) {
            const auto source = LoadImageSource(path, {});
            if (RHI::RHIDevice::Get().IsNativeReady()) {
                auto texture = ImageTextureCache::GetNative(source);
                if (!texture) return 0;
                auto old = m_RHIThumbnails.find(key); auto registration = m_GuiThumbnails.find(key);
                if (old != m_RHIThumbnails.end() && old->second == texture && registration != m_GuiThumbnails.end() && registration->second.IsValid()) return registration->second.GetID();
                auto token = m_TextureRegistrar ? m_TextureRegistrar(*texture) : GuiTextureLease{};
                if (!token.IsValid()) return 0;
                const auto id = token.GetID();
                m_ThumbnailDimensions[key] = {texture->GetWidth(), texture->GetHeight()};
                m_GuiThumbnails[key] = std::move(token); m_RHIThumbnails[key] = std::move(texture); m_ThumbnailCache[key] = id; return id;
            }
            TextureSpecification spec; spec.FlipVertically = false; spec.GenerateMipmaps = false;
            spec.WrapS = spec.WrapT = TextureWrap::ClampToEdge;
            auto texture = ImageTextureCache::GetGL(source, spec);
            if (!texture) return 0;
            auto previous = m_LoadedTextures.find(key);
            if (previous != m_LoadedTextures.end() && previous->second != texture) {
                auto retired = std::move(previous->second);
                if (m_DeferredRelease) m_DeferredRelease([retired] {});
            }
            const auto id = texture->GetRendererID(); m_ThumbnailDimensions[key] = {texture->GetWidth(), texture->GetHeight()};
            m_LoadedTextures[key] = std::move(texture); m_ThumbnailCache[key] = id; return id;
        }
        std::error_code error;
        const auto revision = std::filesystem::last_write_time(path, error);
        if (!error) {
            auto found = m_AssetRevisions.find(key);
            if (found != m_AssetRevisions.end() && found->second != revision) InvalidateThumbnail(path);
            m_AssetRevisions[key] = revision;
        }
        if (auto it = m_ThumbnailCache.find(key); it != m_ThumbnailCache.end()) {
            if (auto lease = m_GuiThumbnails.find(key); lease != m_GuiThumbnails.end()) return lease->second.GetID();
            return it->second;
        }
        while (m_ThumbnailCache.size() >= MaxCacheEntries) InvalidateThumbnail(m_ThumbnailCache.begin()->first);


        // 3. 3D Meshes & Assemblies
        if (!RHI::RHIDevice::Get().IsNativeReady() && !Window::HasActiveContext()) {
            return 0;
        }

        // Check if path is supported 3D format
        bool isSupported = ((ext == ".axleasset" && AssetDSL::PeekAssetKindFromFile(path.string()) == AssetKind::Material) || ext == ".axlemesh" || ext == ".axle" || ext == ".axlebin" ||
                            ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx");
        if (!isSupported) {
            return 0;
        }

        // Queue for background frame slicing
        if (m_PendingQueue.size() < MaxCacheEntries && std::find(m_PendingQueue.begin(), m_PendingQueue.end(), path) == m_PendingQueue.end()) {
            m_PendingQueue.push_back(path);
        }

        return 0;
    }

    void AssetThumbnailManager::OnUpdate() {
        if ((!RHI::RHIDevice::Get().IsNativeReady() && !Window::HasActiveContext()) || m_PendingQueue.empty()) return;

        size_t count = 0;
        while (!m_PendingQueue.empty() && count < m_MaxGenerationsPerFrame) {
            auto path = m_PendingQueue.front();
            m_PendingQueue.erase(m_PendingQueue.begin());

            std::string key = path.lexically_normal().string();
            if (m_ThumbnailCache.find(key) != m_ThumbnailCache.end()) {
                continue;
            }

            uint64_t handle = 0;
            AssetCachedMeta meta;
            if (RenderThumbnail(path, handle, meta)) {
                m_ThumbnailCache[key] = handle;
                m_MetaCache[key] = meta;
            } else {
                m_ThumbnailCache[key] = 0; // Mark failed to avoid infinite retry
            }
            count++;
        }
    }

    bool AssetThumbnailManager::RenderThumbnail(const std::filesystem::path& path, uint64_t& outTexHandle, AssetCachedMeta& outMeta) {
        if (!std::filesystem::exists(path)) return false;
        const auto ext = path.extension().string();
        RenderWorld world;
        glm::vec3 minimum(1e9f), maximum(-1e9f);
        outMeta.Name = path.stem().string();
        auto bounds = [&](const glm::mat4& matrix, const auto& vertices, size_t indices) {
            for (const auto& vertex : vertices) {
                const auto position = glm::vec3(matrix * glm::vec4(vertex, 1));
                minimum = glm::min(minimum, position); maximum = glm::max(maximum, position);
            }
            outMeta.VertexCount += vertices.size(); outMeta.TriangleCount += indices / 3;
        };
        if (ext == ".axleasset") {
            MaterialComponent material;
            if (!SceneHierarchyPanel::LoadMaterialAsset(path, material)) return false;
            Scene scene; auto sphere = scene.CreateEntity("Material thumbnail");
            sphere.AddComponent<MeshInstance3D>().AssetPath = "builtin://sphere";
            sphere.AddComponent<MaterialComponent>() = material;
            world = ExtractRenderWorld(scene); minimum = glm::vec3(-1); maximum = glm::vec3(1);
        } else if (ext == ".axle" && AssemblySerializer::IsAssemblyFile(path)) {
            const auto assembly = AssemblySerializer::LoadAssemblyData(path);
            if (!assembly.IsValid) return false;
            Scene scene;
            std::function<void(const AssemblyNode&, Entity)> append = [&](const AssemblyNode& node, Entity parent) {
                auto entity = scene.CreateEntity(node.Tag);
                entity.GetComponent<TransformComponent>() = node.Transform;
                if (parent) entity.SetParent(parent, false);
                if (node.HasMesh) entity.AddComponent<MeshInstance3D>() = node.Mesh;
                if (node.HasMaterial) entity.AddComponent<MaterialComponent>() = node.Material;
                for (const auto& child : node.Children) append(child, entity);
            };
            append(assembly.RootNode, {}); world = ExtractRenderWorld(scene);
            for (const auto& mesh : world.Meshes) {
                if (const auto builtin = ParseBuiltinMesh(mesh.MeshURI)) {
                    const auto* definition = GetBuiltinMeshDefinition(*builtin);
                    const auto half = definition->Radius > 0 ? glm::vec3(definition->Radius, definition->Height > 0 ? definition->Height * 0.5f : definition->Radius, definition->Radius) : glm::vec3(definition->Size * 0.5f);
                    std::vector<glm::vec3> corners;
                    for (int z : {-1,1}) for (int y : {-1,1}) for (int x : {-1,1}) corners.emplace_back(half * glm::vec3(x,y,z));
                    bounds(mesh.WorldTransform, corners, 0);
                } else {
                    const auto source = AssetSerializer::LoadMesh(FileSystem::ResolveAssetPath(mesh.MeshURI));
                    if (!source.Success) return false;
                    bounds(mesh.WorldTransform, source.SourceData.Vertices, source.SourceData.Indices.size());
                }
            }
        } else {
            const auto package = AssetSerializer::LoadPackage(path);
            if (!package.Success) return false;
            for (const auto& part : package.Parts) {
                if (!part.LoadedMesh.Success) return false;
                RenderMeshInstance mesh; mesh.MeshURI = part.RelativePath;
                mesh.Source = std::make_shared<AxleMeshSource>(part.LoadedMesh.SourceData);
                mesh.WorldTransform = glm::translate(glm::mat4(1), part.LocalTranslation) * glm::mat4_cast(part.LocalRotation) * glm::scale(glm::mat4(1), part.LocalScale);
                mesh.Material.AlbedoColor = mesh.Source->ColorPrimary; mesh.Material.SecondaryColor = mesh.Source->ColorSecondary;
                mesh.Material.PatternMode = mesh.Source->PatternMode; mesh.Material.TileScale = glm::vec2(mesh.Source->TileScale);
                bounds(mesh.WorldTransform, mesh.Source->Vertices, mesh.Source->Indices.size());
                world.Meshes.push_back(std::move(mesh));
            }
        }
        if (world.Meshes.empty() || !world.ErrorMessage.empty() || minimum.x > maximum.x) return false;
        outMeta.Center = (minimum + maximum) * 0.5f; outMeta.BoundingSize = maximum - minimum;
        outMeta.MaxDimension = std::max({outMeta.BoundingSize.x, outMeta.BoundingSize.y, outMeta.BoundingSize.z, 0.05f});
        outMeta.SubPartCount = world.Meshes.size(); outMeta.IsPackage = world.Meshes.size() > 1;
        for (auto& mesh : world.Meshes) mesh.WorldTransform = glm::translate(glm::mat4(1), -outMeta.Center) * mesh.WorldTransform;
        RenderDirectionalLight key; key.Direction = glm::normalize(glm::vec3(0.5f,0.85f,0.6f)); key.CastsShadows = false;
        world.DirectionalLight = key;
        RenderDirectionalLight fill; fill.Direction = glm::normalize(glm::vec3(-0.4f,0.5f,-0.6f)); fill.Intensity = 0.35f;
        world.AdditionalDirectionalLights.push_back(fill);
        ViewRequest request; request.Kind = RenderViewKind::Thumbnail; request.Width = request.Height = m_ThumbnailResolution;
        request.Settings.EnableShadows = request.Settings.EnableSkybox = false;
        request.Settings.ClearColor = {0.11f,0.13f,0.17f,1}; request.Name = "Thumbnail " + path.filename().string();
        const float distance = outMeta.MaxDimension * 1.85f;
        const glm::vec3 position = glm::normalize(glm::vec3(0.6f,0.45f,0.8f)) * distance;
        request.Camera = PerspectiveCamera(45,1,0.01f,distance*6);
        request.Camera.SetViewMatrix(glm::lookAt(position,glm::vec3(0),glm::vec3(0,1,0)),position);
        const auto cacheKey = path.lexically_normal().string();
        if (RHI::RHIDevice::Get().IsNativeReady()) {
            auto targets = std::make_shared<RHI::RHIViewTargets>();
            RHI::RHIViewTargetDesc desc; desc.Width = desc.Height = m_ThumbnailResolution;
            if (!targets->Configure(desc)) return false;
            request.ViewTargets = targets;
            if (!RenderService::Get().RenderView(world,request).Success) return false;
            auto* color = targets->Get(RHI::RHIViewTarget::Color);
            auto lease = m_TextureRegistrar ? m_TextureRegistrar(*color) : GuiTextureLease{};
            if (!lease.IsValid()) return false;
            outTexHandle = lease.GetID(); m_GuiThumbnails[cacheKey] = std::move(lease);
            m_RHIThumbnails[cacheKey] = std::shared_ptr<RHI::RHITexture>(targets,color);
        } else {
            if (!Window::HasActiveContext()) return false;
            FramebufferSpecification spec; spec.Width = spec.Height = m_ThumbnailResolution;
            auto framebuffer = Framebuffer::Create(spec); request.GLTargetFBO = framebuffer;
            if (!RenderService::Get().RenderView(world,request).Success) return false;
            outTexHandle = framebuffer->GetColorAttachmentRendererID();
            if (!outTexHandle) return false;
            m_GLThumbnails[cacheKey] = std::move(framebuffer);
        }
        m_ThumbnailDimensions[cacheKey] = {m_ThumbnailResolution,m_ThumbnailResolution};
        return true;
    }
}
