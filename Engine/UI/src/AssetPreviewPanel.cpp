#include <Axle/AssetPreviewPanel.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/Texture2D.hpp>
#include <Extensions/NRIHelper.h>
#include <stb_image.h>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Window.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/Log.hpp>
#include <imgui.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <functional>

namespace Axle {

    static const char* s_PreviewVertSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;

        out vec3 v_WorldPos;
        out vec3 v_LocalPos;

        void main() {
            v_LocalPos = a_Position;
            vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
            v_WorldPos = worldPos.xyz;
            gl_Position = u_ViewProjection * worldPos;
        }
    )";

    static const char* s_PreviewFragSrc = R"(
        #version 450 core
        layout(location = 0) out vec4 o_Color;

        in vec3 v_WorldPos;
        in vec3 v_LocalPos;

        uniform int u_PatternMode;
        uniform float u_TileScale;
        uniform vec4 u_Color1;
        uniform vec4 u_Color2;
        uniform vec3 u_LightDir;
        uniform vec3 u_CameraPosition;
        uniform vec3 u_FillDirection;
        uniform float u_KeyIntensity;
        uniform float u_FillIntensity;
        uniform mat4 u_Transform;
        uniform int u_WireframeMode;

        void main() {
            if (u_WireframeMode == 1) {
                o_Color = vec4(0.25, 0.88, 1.0, 1.0);
                return;
            }

            float pattern = 0.0;
            vec3 normal = vec3(0.0, 1.0, 0.0);
            vec4 baseColor = u_Color1;
            if (u_PatternMode == 0) {
                vec2 coord = floor(v_WorldPos.xz * u_TileScale);
                pattern = mod(coord.x + coord.y, 2.0);
                baseColor = mix(u_Color1, u_Color2, pattern);
            } else if (u_PatternMode == 1) {
                vec3 dx = dFdx(v_WorldPos);
                vec3 dy = dFdy(v_WorldPos);
                normal = normalize(cross(dx, dy));
                if (dot(normal, normalize(u_CameraPosition - v_WorldPos)) < 0.0) normal = -normal;

                vec3 absN = abs(normal);
                vec2 uv = (absN.y >= absN.x && absN.y >= absN.z) ? v_LocalPos.xz :
                          ((absN.x >= absN.z) ? v_LocalPos.zy : v_LocalPos.xy);
                vec2 coord = floor(uv * u_TileScale + vec2(0.0001));
                pattern = mod(coord.x + coord.y, 2.0);
                baseColor = mix(u_Color1, u_Color2, pattern);
            } else if (u_PatternMode == 2) {
                vec3 nLocal = normalize(v_LocalPos);
                float u = atan(nLocal.z, nLocal.x) / (2.0 * 3.14159265359) + 0.5;
                float v = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265359 + 0.5;
                vec2 coord = floor(vec2(u * u_TileScale, v * (u_TileScale * 0.5)));
                pattern = mod(coord.x + coord.y, 2.0);
                normal = normalize((u_Transform * vec4(v_LocalPos, 0.0)).xyz);
                baseColor = mix(u_Color1, u_Color2, pattern);
            } else {
                // PatternMode 3+: Unity Default Shading (Clean, solid matte surface)
                vec3 dx = dFdx(v_WorldPos);
                vec3 dy = dFdy(v_WorldPos);
                normal = normalize(cross(dx, dy));
                if (dot(normal, normalize(u_CameraPosition - v_WorldPos)) < 0.0) normal = -normal;
                baseColor = u_Color1;
            }
            vec3 keyLight = normalize(u_LightDir);
            vec3 fillLight = normalize(u_FillDirection);

            float NdotKey = max(dot(normal, keyLight), 0.0) * u_KeyIntensity;
            float NdotFill = max(dot(normal, fillLight), 0.0) * 0.7 * u_FillIntensity;
            float ambient = 0.32;
            float lighting = ambient + 0.58 * NdotKey + NdotFill;

            vec3 viewDir = normalize(u_CameraPosition - v_WorldPos);
            vec3 halfDir = normalize(keyLight + viewDir);
            float spec = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.25 * u_KeyIntensity;

            o_Color = vec4(baseColor.rgb * lighting + vec3(spec), baseColor.a);
        }
    )";

    AssetPreviewPanel::AssetPreviewPanel() {
        if (Window::HasActiveContext()) {
            FramebufferSpecification spec;
            spec.Width = m_ViewportWidth;
            spec.Height = m_ViewportHeight;
            m_Framebuffer = Framebuffer::Create(spec);

            m_Shader = std::make_unique<Shader>(s_PreviewVertSrc, s_PreviewFragSrc);
        }
    }

    AssetPreviewPanel::~AssetPreviewPanel() {
        FreeIconTextures();
        if (m_RHISceneRenderer) {
            m_RHISceneRenderer->Shutdown();
            m_RHISceneRenderer.reset();
        }
        if (m_PreviewScene) {
            m_PreviewScene->Clear();
            m_PreviewScene.reset();
        }
    }

    namespace {
        struct IconMip {
            int Width, Height;
            std::vector<unsigned char> Pixels;
        };

        std::vector<IconMip> BuildIconMips(const unsigned char *pixels, int width, int height) {
            // Integrate source coverage instead of sampling a few texels when 128px icons
            // are displayed at 18px. Keep RGB unassociated: ImGui uses straight-alpha blending.
            std::array<uint64_t, 3> colorSum{};
            uint64_t alphaSum = 0;
            for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                const auto *pixel = pixels + i * 4;
                alphaSum += pixel[3];
                for (int c = 0; c < 3; ++c)
                    colorSum[c] += uint64_t(pixel[c]) * pixel[3];
            }
            std::array<unsigned char, 3> transparentColor{};
            for (int c = 0; c < 3; ++c)
                transparentColor[c] = alphaSum ? static_cast<unsigned char>(colorSum[c] / alphaSum) : 255;

            std::vector<IconMip> mips;
            for (int w = width, h = height;; w = std::max(1, w / 2), h = std::max(1, h / 2)) {
                IconMip mip{w, h, std::vector<unsigned char>(static_cast<size_t>(w) * h * 4)};
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const int x0 = x * width / w, x1 = (x + 1) * width / w;
                        const int y0 = y * height / h, y1 = (y + 1) * height / h;
                        std::array<uint64_t, 3> rgb{};
                        uint64_t alpha = 0;
                        for (int sy = y0; sy < y1; ++sy)
                            for (int sx = x0; sx < x1; ++sx) {
                                const auto *pixel = pixels + (static_cast<size_t>(sy) * width + sx) * 4;
                                alpha += pixel[3];
                                for (int c = 0; c < 3; ++c)
                                    rgb[c] += uint64_t(pixel[c]) * pixel[3];
                            }
                        auto *pixel = mip.Pixels.data() + (static_cast<size_t>(y) * w + x) * 4;
                        const auto count = static_cast<uint64_t>(x1 - x0) * (y1 - y0);
                        pixel[3] = static_cast<unsigned char>((alpha + count / 2) / count);
                        for (int c = 0; c < 3; ++c)
                            pixel[c] =
                                alpha ? static_cast<unsigned char>((rgb[c] + alpha / 2) / alpha) : transparentColor[c];
                    }
                mips.push_back(std::move(mip));
                if (w == 1 && h == 1)
                    break;
            }
            return mips;
        }
    } // namespace

    uint64_t AssetPreviewPanel::GetOrCreateIconTexture(const std::string &iconFilename) {
        bool isRHI = (m_ImGuiLayer && m_ImGuiLayer->GetBackendType() != ImGuiBackendType::OpenGL);
        auto FindIconPath = [](const std::string &filename) -> std::filesystem::path {
            // Resolve both source-tree resources and the Resources directory shipped with the editor.
            auto path = FileSystem::ResolveAssetPath(std::filesystem::path("Icons") / filename);
            if (std::filesystem::is_regular_file(path))
                return path;
#ifdef _WIN32
            wchar_t executable[32768]{};
            const auto length = GetModuleFileNameW(nullptr, executable, 32768);
            if (length && length < 32768) {
                path = std::filesystem::path(executable).parent_path() / "Resources" / "Icons" / filename;
                if (std::filesystem::is_regular_file(path))
                    return path;
            }
#endif
            return {};
        };
        auto CacheRegion = [&](const unsigned char *pixels, int width, int height) {
            int left = width, top = height, right = -1, bottom = -1;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    if (pixels[(static_cast<size_t>(y) * width + x) * 4 + 3] <= 16)
                        continue;
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x);
                    bottom = std::max(bottom, y);
                }
            if (right < left || bottom < top)
                return;
            // Keep one transparent texel around the glyph for linear filtering.
            left = std::max(left - 1, 0);
            top = std::max(top - 1, 0);
            right = std::min(right + 2, width);
            bottom = std::min(bottom + 2, height);
            m_IconRegions[iconFilename] = {glm::vec2(left, top) / glm::vec2(width, height),
                                           glm::vec2(right, bottom) / glm::vec2(width, height),
                                           glm::vec2(right - left, bottom - top)};
        };

        if (isRHI) {
            auto it = m_IconCache.find(iconFilename);
            if (it != m_IconCache.end() && it->second.second != 0) {
                return it->second.second;
            }

            std::filesystem::path iconPath = FindIconPath(iconFilename);
            if (iconPath.empty())
                return 0;

            int width = 0, height = 0, channels = 0;
            stbi_set_flip_vertically_on_load_thread(false);
            stbi_uc *pixels = stbi_load(iconPath.string().c_str(), &width, &height, &channels, 4);
            if (!pixels)
                return 0;
            CacheRegion(pixels, width, height);
            auto mips = BuildIconMips(pixels, width, height);

            auto &rhiDevice = RHI::RHIDevice::Get();
            if (!rhiDevice.IsInitialized()) {
                stbi_image_free(pixels);
                return 0;
            }

            auto rhiTex = std::make_shared<RHI::RHITexture>();
            auto desc = RHI::RHITextureDesc::RenderTarget2D((uint32_t)width, (uint32_t)height,
                                                            RHI::RHITextureFormat::RGBA8_UNORM, "Icon_" + iconFilename);
            desc.Usage = RHI::RHITextureUsage::ShaderResource;
            desc.MipLevels = static_cast<uint32_t>(mips.size());
            if (rhiTex->Init(desc)) {
                const auto &helper = rhiDevice.GetNRIHelper();
                nri::Queue *queue = rhiDevice.GetGraphicsQueue();
                if (queue && helper.UploadData) {
                    std::vector<nri::TextureSubresourceUploadDesc> subresources(mips.size());
                    for (size_t i = 0; i < mips.size(); ++i) {
                        auto &subresource = subresources[i];
                        subresource.slices = mips[i].Pixels.data();
                        subresource.sliceNum = 1;
                        subresource.rowPitch = static_cast<uint32_t>(mips[i].Width) * 4;
                        subresource.slicePitch = subresource.rowPitch * mips[i].Height;
                    }

                    nri::TextureUploadDesc upDesc = {};
                    upDesc.subresources = subresources.data();
                    upDesc.texture = rhiTex->GetNative();
                    upDesc.after.access = nri::AccessBits::SHADER_RESOURCE;
                    upDesc.after.layout = nri::Layout::SHADER_RESOURCE;
                    upDesc.after.stages = nri::StageBits::ALL;
                    upDesc.planes = nri::PlaneBits::ALL;

                    if (helper.UploadData(*queue, &upDesc, 1, nullptr, 0) != nri::Result::SUCCESS) {
                        stbi_image_free(pixels);
                        return 0;
                    }
                } else {
                    stbi_image_free(pixels);
                    return 0;
                }

                uint64_t nativeHandle = rhiTex->GetNativeSRVHandle();
                if (nativeHandle != 0 && m_ImGuiLayer) {
                    uint64_t gpuHandle = m_ImGuiLayer->RegisterTextureDescriptor(nativeHandle);
                    if (gpuHandle != 0) {
                        m_IconCache[iconFilename] = {rhiTex, gpuHandle};
                        stbi_image_free(pixels);
                        return gpuHandle;
                    }
                }
            }
            stbi_image_free(pixels);
            return 0;
        } else if (Window::HasActiveContext()) {
            auto it = m_GLIconCache.find(iconFilename);
            if (it != m_GLIconCache.end() && it->second && it->second->IsLoaded()) {
                return it->second->GetRendererID();
            }

            std::filesystem::path iconPath = FindIconPath(iconFilename);
            if (iconPath.empty())
                return 0;
            int width = 0, height = 0, channels = 0;
            stbi_set_flip_vertically_on_load_thread(false);
            auto *pixels = stbi_load(iconPath.string().c_str(), &width, &height, &channels, 4);
            if (!pixels)
                return 0;
            CacheRegion(pixels, width, height);
            auto mips = BuildIconMips(pixels, width, height);
            stbi_image_free(pixels);

            TextureSpecification specification;
            specification.Width = width;
            specification.Height = height;
            specification.FlipVertically = false;  // UI icons use top-left UVs on every backend.
            specification.GenerateMipmaps = false; // Upload the same alpha-aware chain as RHI.
            specification.WrapS = specification.WrapT = TextureWrap::ClampToEdge;
            auto tex = Texture2D::Create(specification, mips.front().Pixels.data());
            if (tex && tex->IsLoaded()) {
                GLint previousTexture = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
                glBindTexture(GL_TEXTURE_2D, tex->GetRendererID());
                for (size_t i = 1; i < mips.size(); ++i) {
                    glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), GL_RGBA8, mips[i].Width, mips[i].Height, 0,
                                 GL_RGBA, GL_UNSIGNED_BYTE, mips[i].Pixels.data());
                }
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
                glTextureParameteri(tex->GetRendererID(), GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTextureParameteri(tex->GetRendererID(), GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(mips.size() - 1));
                m_GLIconCache[iconFilename] = tex;
                return tex->GetRendererID();
            }
        }
        return 0;
    }

    void AssetPreviewPanel::FreeIconTextures() {
        if (m_ImGuiLayer && m_ImGuiLayer->GetBackendType() != ImGuiBackendType::OpenGL) {
            for (auto &[name, pair] : m_IconCache) {
                if (pair.second != 0) {
                    m_ImGuiLayer->UnregisterTextureDescriptor(pair.second);
                }
            }
        }
        m_IconCache.clear();
        m_GLIconCache.clear();
        m_IconRegions.clear();
    }

    void AssetPreviewPanel::UpdatePreviewLighting() {
        if (m_PreviewLight && m_PreviewLight.HasComponent<DirectionalLightComponent>()) {
            auto& dlc = m_PreviewLight.GetComponent<DirectionalLightComponent>();
            dlc.Intensity = m_EnableKeyLight ? 1.0f : 0.0f;
            dlc.AmbientIntensity = m_EnableKeyLight ? 0.65f : 0.25f;
        }
        if (m_PreviewFillLight && m_PreviewFillLight.HasComponent<DirectionalLightComponent>()) {
            auto& flc = m_PreviewFillLight.GetComponent<DirectionalLightComponent>();
            flc.Intensity = m_EnableSecondaryLight ? 1.0f : 0.0f;
        }
    }

    void AssetPreviewPanel::InitRHI(ImGuiLayer* imguiLayer) {
        m_ImGuiLayer = imguiLayer;
        if (!m_ImGuiLayer) return;

        if (RHI::RHIDevice::Get().IsInitialized()) {
            if (!m_RHIViewport) {
                m_RHIViewport = std::make_unique<RHIViewportDescriptor>();
            }
            RHIViewportDesc desc;
            desc.Width = m_ViewportWidth;
            desc.Height = m_ViewportHeight;
            desc.ColorFormat = RHI::RHITextureFormat::RGBA8_UNORM;
            desc.EnableDepth = true;
            desc.DepthFormat = RHI::RHITextureFormat::D32_FLOAT;
            desc.DebugName = "AssetPreviewViewport";
            m_RHIViewport->Init(m_ImGuiLayer, desc);

            if (!m_RHISceneRenderer) {
                m_RHISceneRenderer = std::make_unique<RHISceneRenderer>();
                m_RHISceneRenderer->Init(RHI::RHIDevice::Get(), false);
            }
            m_SceneNeedsRebuild = true;
        }
    }

    void AssetPreviewPanel::SetSceneContext(Scene* scene, PhysicsScene* physicsScene, std::shared_ptr<VertexArray> fallbackMesh, const SceneSerializerContext* serializerContext) {
        m_Scene = scene;
        m_PhysicsScene = physicsScene;
        m_FallbackMesh = fallbackMesh;
        m_SerializerContext = serializerContext;

        if (m_HasAsset && m_IsAssembly) {
            std::function<void(AssemblyNode&)> preloadMeshVAOs = [&](AssemblyNode& node) {
                if (node.HasMesh && !node.Mesh.VAO) {
                    if (m_SerializerContext) {
                        if (node.Mesh.AssetPath == "builtin://sphere") node.Mesh.VAO = m_SerializerContext->SphereMesh;
                        else if (node.Mesh.AssetPath == "builtin://cylinder") node.Mesh.VAO = m_SerializerContext->CylinderMesh;
                        else if (node.Mesh.AssetPath == "builtin://cone") node.Mesh.VAO = m_SerializerContext->ConeMesh;
                        else if (node.Mesh.AssetPath == "builtin://plane") node.Mesh.VAO = m_SerializerContext->PlaneMesh;
                        else if (!node.Mesh.AssetPath.empty() && node.Mesh.AssetPath != "builtin://cube") {
                            auto resolved = FileSystem::ResolveAssetPath(node.Mesh.AssetPath);
                            if (std::filesystem::exists(resolved)) {
                                auto loaded = AssetSerializer::LoadMesh(resolved);
                                if (loaded.Success) node.Mesh.VAO = loaded.VAO;
                            }
                        }
                        if (!node.Mesh.VAO) node.Mesh.VAO = m_SerializerContext->CubeMesh;
                    }
                    if (!node.Mesh.VAO) node.Mesh.VAO = m_FallbackMesh;
                }
                for (auto& child : node.Children) {
                    preloadMeshVAOs(child);
                }
            };
            preloadMeshVAOs(m_CurrentAssembly.RootNode);
        }
    }

    void AssetPreviewPanel::SetAsset(const std::filesystem::path& assetPath) {
        m_CurrentAssetPath = assetPath;
        if (assetPath.empty() || !std::filesystem::exists(assetPath)) {
            m_HasAsset = false;
            m_IsAssembly = false;
            m_CurrentPackage = {};
            m_CurrentAssembly = {};
            return;
        }

        std::string ext = assetPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool isMaterial = (ext == ".axleasset" && AssetDSL::PeekAssetKindFromFile(assetPath.string()) == AssetKind::Material);
        bool is3D = (ext == ".axlemesh" || ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || isMaterial);
        if (!is3D) {
            m_HasAsset = false;
            m_IsAssembly = false;
            m_CurrentPackage = {};
            m_CurrentAssembly = {};
            return;
        }

        if (isMaterial) {
            MaterialComponent mat;
            if (SceneHierarchyPanel::LoadMaterialAsset(assetPath, mat)) {
                m_IsAssembly = false;
                m_CurrentPackage = {};
                m_CurrentPackage.Name = assetPath.stem().string();
                m_CurrentPackage.BoundingSize = glm::vec3(1.0f);
                m_CurrentPackage.HalfExtent = glm::vec3(0.5f);
                m_CurrentPackage.Success = true;

                AxlePackagePart previewPart;
                previewPart.LoadedMesh.Success = true;

                switch (m_PreviewMeshType) {
                    case PreviewMeshType::Cube:
                        previewPart.Name = "MaterialCube";
                        previewPart.LoadedMesh.VAO = PrimitiveMesh::CreateCube();
                        m_TotalVertices = 36;
                        m_TotalTriangles = 12;
                        break;
                    case PreviewMeshType::Cylinder:
                        previewPart.Name = "MaterialCylinder";
                        previewPart.LoadedMesh.VAO = PrimitiveMesh::CreateCylinder(32);
                        m_TotalVertices = 192;
                        m_TotalTriangles = 96;
                        break;
                    case PreviewMeshType::Plane:
                        previewPart.Name = "MaterialPlane";
                        previewPart.LoadedMesh.VAO = PrimitiveMesh::CreatePlane(1, 1);
                        m_TotalVertices = 4;
                        m_TotalTriangles = 2;
                        break;
                    case PreviewMeshType::Sphere:
                    default:
                        previewPart.Name = "MaterialSphere";
                        previewPart.LoadedMesh.VAO = (m_SerializerContext && m_SerializerContext->SphereMesh) ? m_SerializerContext->SphereMesh : m_FallbackMesh;
                        if (!previewPart.LoadedMesh.VAO) previewPart.LoadedMesh.VAO = PrimitiveMesh::CreateSphere(64, 96);
                        m_TotalVertices = 768;
                        m_TotalTriangles = 512;
                        break;
                }

                previewPart.LoadedMesh.SourceData.ColorPrimary = mat.AlbedoColor;
                previewPart.LoadedMesh.SourceData.ColorSecondary = mat.AlbedoColor * 0.85f;
                previewPart.LoadedMesh.SourceData.PatternMode = 2; // Spherical PBR
                previewPart.LoadedMesh.SourceData.TileScale = 1.0f;
                previewPart.LoadedMesh.SourceData.Vertices = { glm::vec3(-0.5f), glm::vec3(0.5f) };
                previewPart.LoadedMesh.SourceData.Indices = { 0, 1, 0 };
                m_CurrentPackage.Parts.push_back(previewPart);

                m_HasAsset = true;
                m_Center = glm::vec3(0.0f);
                m_Size = glm::vec3(1.0f);
                m_MaxDim = 1.0f;
                m_Pitch = 0.0f;
                m_Yaw = 0.0f;
                m_ObjectYaw = 30.0f;
                m_ObjectPitch = -15.0f;
                m_Zoom = 1.0f;
                m_PanOffset = glm::vec3(0.0f);
                m_ActiveMaterial = mat;
                m_SceneNeedsRebuild = true;
                return;
            }
        }

        if (ext == ".axle") {
            m_IsAssembly = true;
            m_CurrentAssembly = AssemblySerializer::LoadAssemblyData(assetPath, m_SerializerContext);
            m_HasAsset = m_CurrentAssembly.IsValid;
            if (!m_HasAsset) return;

            // Preload and cache all mesh VAOs ONCE so RenderPreview3D doesn't reload from disk/cache every frame
            std::function<void(AssemblyNode&)> preloadMeshVAOs = [&](AssemblyNode& node) {
                if (node.HasMesh && !node.Mesh.AssetPath.empty()) {
                    if (node.Mesh.AssetPath == "builtin://sphere") {
                        node.Mesh.VAO = m_SerializerContext ? m_SerializerContext->SphereMesh : nullptr;
                    } else if (node.Mesh.AssetPath == "builtin://cylinder") {
                        node.Mesh.VAO = m_SerializerContext ? m_SerializerContext->CylinderMesh : nullptr;
                    } else if (node.Mesh.AssetPath == "builtin://cone") {
                        node.Mesh.VAO = m_SerializerContext ? m_SerializerContext->ConeMesh : nullptr;
                    } else if (node.Mesh.AssetPath == "builtin://plane") {
                        node.Mesh.VAO = m_SerializerContext ? m_SerializerContext->PlaneMesh : nullptr;
                    } else if (node.Mesh.AssetPath == "builtin://cube") {
                        node.Mesh.VAO = m_SerializerContext ? m_SerializerContext->CubeMesh : nullptr;
                    } else {
                        auto resolved = FileSystem::ResolveAssetPath(node.Mesh.AssetPath);
                        if (!std::filesystem::exists(resolved)) {
                            if (resolved.extension() == ".axle") {
                                auto alt = resolved;
                                alt.replace_extension(".axlemesh");
                                if (std::filesystem::exists(alt)) resolved = alt;
                            }
                        }
                        if (std::filesystem::exists(resolved)) {
                            auto loaded = AssetSerializer::LoadMesh(resolved);
                            if (loaded.Success && loaded.VAO) {
                                node.Mesh.VAO = loaded.VAO;
                            }
                        }
                    }
                    if (!node.Mesh.VAO) {
                        node.Mesh.VAO = m_SerializerContext ? m_SerializerContext->CubeMesh : m_FallbackMesh;
                    }
                }
                for (auto& child : node.Children) {
                    preloadMeshVAOs(child);
                }
            };
            preloadMeshVAOs(m_CurrentAssembly.RootNode);

            // Compute aggregate bounding box from assembly
            glm::vec3 minBound(1e9f);
            glm::vec3 maxBound(-1e9f);
            size_t totalVertices = 0;
            size_t totalIndices = 0;

            std::function<void(const AssemblyNode&, const glm::mat4&)> computeBounds = [&](const AssemblyNode& node, const glm::mat4& parentMat) {
                glm::mat4 localMat = node.Transform.GetTransform();
                glm::mat4 worldMat = parentMat * localMat;
                glm::vec3 pos = glm::vec3(worldMat[3]);
                glm::vec3 halfExt = node.HasCollisionShape ? node.CollisionShape.HalfExtent : glm::vec3(0.5f);
                minBound = glm::min(minBound, pos - halfExt);
                maxBound = glm::max(maxBound, pos + halfExt);
                totalVertices += 24;
                totalIndices += 36;
                for (const auto& child : node.Children) {
                    computeBounds(child, worldMat);
                }
            };
            computeBounds(m_CurrentAssembly.RootNode, glm::mat4(1.0f));

            if (minBound.x > maxBound.x) {
                minBound = glm::vec3(-1.0f);
                maxBound = glm::vec3(1.0f);
            }

            m_Center = (minBound + maxBound) * 0.5f;
            m_Size = maxBound - minBound;
            m_MaxDim = std::max({ m_Size.x, m_Size.y, m_Size.z, 0.2f });
            m_TotalVertices = totalVertices;
            m_TotalTriangles = totalIndices / 3;

            // Reset camera framing
            m_Pitch = 20.0f;
            m_Yaw = 35.0f;
            m_ObjectYaw = 0.0f;
            m_ObjectPitch = 0.0f;
            m_Zoom = 1.0f;
            m_PanOffset = glm::vec3(0.0f);
            m_SceneNeedsRebuild = true;
            return;
        }

        m_IsAssembly = false;
        m_CurrentPackage = AssetSerializer::LoadPackage(assetPath);
        if (!m_CurrentPackage.Success || m_CurrentPackage.Parts.empty()) {
            m_HasAsset = false;
            return;
        }

        m_HasAsset = true;

        // Compute aggregate bounding box
        glm::vec3 minBound(1e9f);
        glm::vec3 maxBound(-1e9f);
        size_t totalVertices = 0;
        size_t totalIndices = 0;

        for (const auto& part : m_CurrentPackage.Parts) {
            if (!part.LoadedMesh.Success || !part.LoadedMesh.VAO) continue;
            const auto& verts = part.LoadedMesh.SourceData.Vertices;
            glm::mat4 localMat = glm::translate(glm::mat4(1.0f), part.LocalTranslation)
                               * glm::mat4_cast(part.LocalRotation)
                               * glm::scale(glm::mat4(1.0f), part.LocalScale);

            for (const auto& v : verts) {
                glm::vec3 worldV = glm::vec3(localMat * glm::vec4(v, 1.0f));
                minBound = glm::min(minBound, worldV);
                maxBound = glm::max(maxBound, worldV);
            }
            totalVertices += verts.size();
            totalIndices += part.LoadedMesh.SourceData.Indices.size();
        }

        if (minBound.x > maxBound.x) {
            minBound = glm::vec3(-0.5f);
            maxBound = glm::vec3(0.5f);
        }

        m_Center = (minBound + maxBound) * 0.5f;
        m_Size = maxBound - minBound;
        m_MaxDim = std::max({ m_Size.x, m_Size.y, m_Size.z, 0.05f });
        m_TotalVertices = totalVertices;
        m_TotalTriangles = totalIndices / 3;

        // Reset camera framing
        m_Pitch = 20.0f;
        m_Yaw = 35.0f;
        m_ObjectYaw = 0.0f;
        m_ObjectPitch = 0.0f;
        m_Zoom = 1.0f;
        m_PanOffset = glm::vec3(0.0f);
        m_SceneNeedsRebuild = true;
    }

    bool AssetPreviewPanel::IsMaterialPreview() const {
        return !m_CurrentAssetPath.empty() && m_CurrentAssetPath.extension() == ".axleasset"
            && AssetDSL::PeekAssetKindFromFile(m_CurrentAssetPath.string()) == AssetKind::Material;
    }

    glm::quat AssetPreviewPanel::GetPreviewRotation() const {
        float cameraYaw = glm::radians(m_Yaw);
        glm::vec3 cameraRight(std::cos(cameraYaw), 0.0f, -std::sin(cameraYaw));
        glm::quat rotation = glm::angleAxis(glm::radians(m_ObjectPitch), cameraRight)
             * glm::angleAxis(glm::radians(m_ObjectYaw), glm::vec3(0.0f, 1.0f, 0.0f));
        // The built-in plane lies on XZ; face it toward the fixed material camera.
        if (IsMaterialPreview() && m_PreviewMeshType == PreviewMeshType::Plane)
            rotation *= glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        return rotation;
    }

    float AssetPreviewPanel::GetPreviewScale() const {
        float meshScale = IsMaterialPreview() && m_PreviewMeshType == PreviewMeshType::Cube ? 2.0f / 3.0f : 1.0f;
        return meshScale / m_Zoom;
    }

    glm::mat4 AssetPreviewPanel::GetPreviewTransform() const {
        return glm::translate(glm::mat4(1.0f), m_PanOffset)
             * glm::mat4_cast(GetPreviewRotation())
             * glm::scale(glm::mat4(1.0f), glm::vec3(GetPreviewScale()));
    }

    void AssetPreviewPanel::RenderPreview3D(float width, float height) {
        if (!Window::HasActiveContext() || !m_Framebuffer || !m_Shader) return;

        u32 w = static_cast<u32>(std::max(width, 64.0f));
        u32 h = static_cast<u32>(std::max(height, 64.0f));

        if (w != m_ViewportWidth || h != m_ViewportHeight) {
            m_ViewportWidth = w;
            m_ViewportHeight = h;
            m_Framebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
        }

        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        GLint prevViewport[4];
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
        GLboolean prevCullFace = glIsEnabled(GL_CULL_FACE);
        GLint prevPolygonMode[2];
        glGetIntegerv(GL_POLYGON_MODE, prevPolygonMode);

        m_Framebuffer->Bind();
        glViewport(0, 0, m_ViewportWidth, m_ViewportHeight);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // Show the same checkerboard as the RHI preview.
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (!m_HasAsset || (!m_IsAssembly && m_CurrentPackage.Parts.empty())) {
            glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
            glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
            if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            return;
        }

        // Fixed camera; rotation, pan and zoom are applied to the object.
        float pitchRad = glm::radians(m_Pitch);
        float yawRad = glm::radians(m_Yaw);
        bool isMaterial = IsMaterialPreview();
        float distance = m_MaxDim * (isMaterial ? 3.0f : 1.95f);

        glm::vec3 camPos = glm::vec3(
            distance * std::cos(pitchRad) * std::sin(yawRad),
            distance * std::sin(pitchRad),
            distance * std::cos(pitchRad) * std::cos(yawRad)
        );

        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        float aspect = static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight);
        glm::mat4 proj = glm::perspective(glm::radians(isMaterial ? 20.0f : 45.0f), aspect, 0.01f, distance * 8.0f);
        glm::mat4 viewProj = proj * view;

        m_Shader->Bind();
        m_Shader->SetMat4("u_ViewProjection", viewProj);
        float azimRad = glm::radians(m_SunAzimuth);
        float elevRad = glm::radians(m_SunElevation);
        glm::vec3 sunLightDir = glm::normalize(glm::vec3(
            std::cos(elevRad) * std::sin(azimRad),
            std::sin(elevRad),
            std::cos(elevRad) * std::cos(azimRad)
        ));
        m_Shader->SetFloat3("u_LightDir", sunLightDir);
        m_Shader->SetFloat3("u_CameraPosition", camPos);
        m_Shader->SetFloat3("u_FillDirection", glm::vec3(0.0f, -1.0f, 0.0f));
        m_Shader->SetFloat("u_KeyIntensity", m_EnableKeyLight ? 1.0f : 0.0f);
        m_Shader->SetFloat("u_FillIntensity", m_EnableSecondaryLight ? 1.0f : 0.0f);
        glm::mat4 previewTransform = GetPreviewTransform();

        if (m_WireframeMode) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            m_Shader->SetInt("u_WireframeMode", 1);
        } else {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            m_Shader->SetInt("u_WireframeMode", 0);
        }

        if (m_IsAssembly) {
            std::function<void(const AssemblyNode&, const glm::mat4&)> renderNodeRec = [&](const AssemblyNode& node, const glm::mat4& parentMat) {
                glm::mat4 localMat = node.Transform.GetTransform();
                glm::mat4 worldMat = parentMat * localMat;
                glm::mat4 model = previewTransform * glm::translate(glm::mat4(1.0f), -m_Center) * worldMat;

                if (node.HasMesh) {
                    std::shared_ptr<VertexArray> vao = node.Mesh.VAO;
                    if (!vao) vao = m_FallbackMesh;

                    if (vao) {
                        m_Shader->SetMat4("u_Transform", model);
                        m_Shader->SetInt("u_PatternMode", node.Mesh.PatternMode);
                        m_Shader->SetFloat("u_TileScale", node.Mesh.TileScale);
                        m_Shader->SetFloat4("u_Color1", node.Mesh.Color1);
                        m_Shader->SetFloat4("u_Color2", node.Mesh.Color2);
                        RenderCommand::DrawIndexed(vao);
                    }
                }

                for (const auto& child : node.Children) {
                    renderNodeRec(child, worldMat);
                }
            };
            renderNodeRec(m_CurrentAssembly.RootNode, glm::mat4(1.0f));
        } else {
            // Render all subparts with center offset
            for (const auto& part : m_CurrentPackage.Parts) {
                if (!part.LoadedMesh.VAO) continue;
                glm::mat4 localMat = glm::translate(glm::mat4(1.0f), part.LocalTranslation)
                                   * glm::mat4_cast(part.LocalRotation)
                                   * glm::scale(glm::mat4(1.0f), part.LocalScale);
                glm::mat4 model = previewTransform * glm::translate(glm::mat4(1.0f), -m_Center) * localMat;

                m_Shader->SetMat4("u_Transform", model);
                m_Shader->SetInt("u_PatternMode", part.LoadedMesh.SourceData.PatternMode);
                m_Shader->SetFloat("u_TileScale", part.LoadedMesh.SourceData.TileScale);
                m_Shader->SetFloat4("u_Color1", part.LoadedMesh.SourceData.ColorPrimary);
                m_Shader->SetFloat4("u_Color2", part.LoadedMesh.SourceData.ColorSecondary);

                RenderCommand::DrawIndexed(part.LoadedMesh.VAO);
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (prevCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, prevPolygonMode[0]);
    }

    void AssetPreviewPanel::SpawnCurrentAssetIntoScene() {
        if (!m_Scene || !m_HasAsset) return;

        if (m_IsAssembly) {
            auto spawned = m_Scene->InstantiateAssembly(m_CurrentAssetPath, m_PhysicsScene, glm::vec3(0.0f, 1.0f, 0.0f), m_SerializerContext);
            AXLE_INFO("AssetPreviewPanel: Instantiated Assembly '{0}'", m_CurrentAssetPath.filename().string());
            return;
        }

        if (m_CurrentPackage.Parts.empty()) return;

        glm::vec3 spawnPos(0.0f, m_CurrentPackage.HalfExtent.y, 0.0f);
        if (m_CurrentPackage.IsPackage) {
            std::string rootTag = m_CurrentPackage.Name.empty() ? m_CurrentAssetPath.stem().string() : m_CurrentPackage.Name;
            auto rootEntity = m_Scene->CreateEntity(rootTag);
            rootEntity.AddComponent<TransformComponent>(spawnPos, glm::vec3(1.0f));
            auto& rootHierarchy = rootEntity.AddComponent<HierarchyComponent>();

            if (m_PhysicsScene) {
                BoxBodyProps bProps;
                bProps.Position = spawnPos;
                bProps.HalfExtent = m_CurrentPackage.HalfExtent;
                bProps.IsDynamic = true;
                bProps.Friction = 0.70f;
                bProps.Restitution = 0.15f;
                auto bodyId = m_PhysicsScene->CreateBox(bProps);
                rootEntity.AddComponent<RigidBody3D>(bodyId, true);
                rootEntity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, m_CurrentPackage.HalfExtent, 0.70f, 0.15f);
            }

            for (const auto& part : m_CurrentPackage.Parts) {
                auto childEntity = m_Scene->CreateEntity(part.Name);
                auto& ct = childEntity.AddComponent<TransformComponent>(part.LocalTranslation, part.LocalScale);
                ct.Rotation = part.LocalRotation;

                auto partFullPath = m_CurrentAssetPath.parent_path() / part.RelativePath;
                childEntity.AddComponent<MeshInstance3D>(
                    part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_FallbackMesh,
                    partFullPath.string(),
                    part.LoadedMesh.SourceData.PatternMode,
                    part.LoadedMesh.SourceData.TileScale,
                    part.LoadedMesh.SourceData.ColorPrimary,
                    part.LoadedMesh.SourceData.ColorSecondary
                );

                childEntity.AddComponent<HierarchyComponent>(static_cast<entt::entity>(rootEntity));
                rootHierarchy.Children.push_back(static_cast<entt::entity>(childEntity));
            }
            AXLE_INFO("AssetPreviewPanel: Instantiated package '{0}' ({1} subparts)", rootTag, m_CurrentPackage.Parts.size());
        } else {
            const auto& part = m_CurrentPackage.Parts[0];
            std::string tag = m_CurrentPackage.Name.empty() ? m_CurrentAssetPath.stem().string() : m_CurrentPackage.Name;
            auto entity = m_Scene->CreateEntity(tag);
            entity.AddComponent<TransformComponent>(spawnPos, part.LocalScale);
            entity.AddComponent<MeshInstance3D>(
                part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_FallbackMesh,
                m_CurrentAssetPath.string(),
                part.LoadedMesh.SourceData.PatternMode,
                part.LoadedMesh.SourceData.TileScale,
                part.LoadedMesh.SourceData.ColorPrimary,
                part.LoadedMesh.SourceData.ColorSecondary
            );

            if (m_PhysicsScene) {
                BoxBodyProps bProps;
                bProps.Position = spawnPos;
                bProps.HalfExtent = part.LoadedMesh.SourceData.HalfExtent;
                bProps.IsDynamic = true;
                auto bodyId = m_PhysicsScene->CreateBox(bProps);
                entity.AddComponent<RigidBody3D>(bodyId, true);
                entity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, part.LoadedMesh.SourceData.HalfExtent, 0.70f, 0.15f);
            }
            AXLE_INFO("AssetPreviewPanel: Instantiated single mesh '{0}'", tag);
        }
    }

    void AssetPreviewPanel::SetPreviewMeshType(PreviewMeshType type) {
        m_PreviewMeshType = type;
        m_SceneNeedsRebuild = true;
        if (IsMaterialPreview()) {
            m_ObjectYaw = type == PreviewMeshType::Plane ? 0.0f : 30.0f;
            m_ObjectPitch = type == PreviewMeshType::Plane ? 0.0f : -15.0f;
            m_Zoom = 1.0f;
            m_PanOffset = glm::vec3(0.0f);
        }
        if (!m_SerializerContext) return;
        std::shared_ptr<VertexArray> vao = m_SerializerContext->SphereMesh;
        if (type == PreviewMeshType::Cube) vao = m_FallbackMesh;
        else if (type == PreviewMeshType::Cylinder) vao = m_SerializerContext->CylinderMesh;
        else if (type == PreviewMeshType::Plane) vao = m_SerializerContext->PlaneMesh;
        else if (type == PreviewMeshType::Custom && m_CustomMeshVAO) vao = m_CustomMeshVAO;

        if (!m_CurrentPackage.Parts.empty()) {
            m_CurrentPackage.Parts[0].LoadedMesh.VAO = vao;
        }
    }

    void AssetPreviewPanel::LoadCustomPreviewMesh(const std::filesystem::path& meshPath) {
        if (!std::filesystem::exists(meshPath)) return;
        m_CustomMeshPath = meshPath;
        AxleLoadedPackage pkg = AssetSerializer::LoadPackage(meshPath);
        if (pkg.Success && !pkg.Parts.empty()) {
            m_CustomMeshVAO = pkg.Parts[0].LoadedMesh.VAO;
            m_TotalVertices = pkg.Parts[0].LoadedMesh.SourceData.Vertices.size();
            m_TotalTriangles = pkg.Parts[0].LoadedMesh.SourceData.Indices.size() / 3;
            m_SceneNeedsRebuild = true;
            SetPreviewMeshType(PreviewMeshType::Custom);
        }
    }

    void AssetPreviewPanel::UpdateMaterialPreview(const MaterialComponent& mat) {
        m_ActiveMaterial = mat;
        if (m_PreviewEntity && m_PreviewScene && m_PreviewEntity.HasComponent<MaterialComponent>()) {
            auto& comp = m_PreviewEntity.GetComponent<MaterialComponent>();
            comp = mat;
            comp.SyncToRHI();
        } else {
            m_SceneNeedsRebuild = true;
        }

        if (!m_CurrentPackage.Parts.empty()) {
            m_CurrentPackage.Parts[0].LoadedMesh.SourceData.ColorPrimary = mat.AlbedoColor;
            m_CurrentPackage.Parts[0].LoadedMesh.SourceData.ColorSecondary = mat.AlbedoColor * 0.85f;
        }
    }

    void AssetPreviewPanel::SetSunAzimuth(float deg) {
        m_SunAzimuth = deg;
        if (m_PreviewLight && m_PreviewScene && m_PreviewLight.HasComponent<DirectionalLightComponent>()) {
            m_PreviewLight.GetComponent<DirectionalLightComponent>().Azimuth = m_SunAzimuth;
        }
    }

    void AssetPreviewPanel::SetSunElevation(float deg) {
        m_SunElevation = std::clamp(deg, 5.0f, 85.0f);
        if (m_PreviewLight && m_PreviewScene && m_PreviewLight.HasComponent<DirectionalLightComponent>()) {
            m_PreviewLight.GetComponent<DirectionalLightComponent>().Elevation = m_SunElevation;
        }
    }

    void AssetPreviewPanel::RebuildPreviewScene() {
        if (!RHI::RHIDevice::Get().IsInitialized()) return;

        if (!m_PreviewScene) {
            m_PreviewScene = std::make_shared<Scene>();
        }
        m_PreviewScene->Clear();
        // Mesh parts share one transform. Lights remain outside this hierarchy.
        m_PreviewEntity = m_PreviewScene->CreateEntity("PreviewObject");

        bool isMaterial = (!m_CurrentAssetPath.empty() && 
                           m_CurrentAssetPath.extension() == ".axleasset" && 
                           AssetDSL::PeekAssetKindFromFile(m_CurrentAssetPath.string()) == AssetKind::Material);

        if (isMaterial) {
            auto& tc = m_PreviewEntity.GetComponent<TransformComponent>();
            tc.Translation = glm::vec3(0.0f);
            tc.Scale = glm::vec3(1.0f);

            auto& mc = m_PreviewEntity.AddComponent<MeshInstance3D>();
            switch (m_PreviewMeshType) {
                case PreviewMeshType::Sphere:
                    mc.RHIMeshAsset = RHIMeshRegistry::Get().GetSphere();
                    break;
                case PreviewMeshType::Cube:
                    mc.RHIMeshAsset = RHIMeshRegistry::Get().GetCube();
                    break;
                case PreviewMeshType::Cylinder:
                    mc.RHIMeshAsset = RHIMeshRegistry::Get().GetCylinder();
                    break;
                case PreviewMeshType::Plane:
                    mc.RHIMeshAsset = RHIMeshRegistry::Get().GetPlane();
                    break;
                case PreviewMeshType::Custom:
                    if (!m_CustomMeshPath.empty()) {
                        mc.RHIMeshAsset = RHIMeshRegistry::Get().GetOrCreateMesh(m_CustomMeshPath.string());
                    }
                    if (!mc.RHIMeshAsset) mc.RHIMeshAsset = RHIMeshRegistry::Get().GetSphere();
                    break;
            }

            auto& matComp = m_PreviewEntity.AddComponent<MaterialComponent>(m_ActiveMaterial);
            matComp.SyncToRHI();
        } else if (m_IsAssembly) {
            std::function<void(const AssemblyNode&, glm::vec3)> spawnNodes = [&](const AssemblyNode& node, glm::vec3 parentPos) {
                glm::vec3 worldPos = parentPos + node.Transform.Translation;
                if (node.HasMesh) {
                    auto partEnt = m_PreviewScene->CreateEntity(node.Tag.empty() ? "Node" : node.Tag);
                    partEnt.SetParent(m_PreviewEntity, false);
                    auto& ptc = partEnt.GetComponent<TransformComponent>();
                    ptc.Translation = worldPos - m_Center;
                    ptc.Rotation = node.Transform.Rotation;
                    ptc.Scale = node.Transform.Scale;

                    auto& pmc = partEnt.AddComponent<MeshInstance3D>();
                    if (!node.Mesh.AssetPath.empty()) {
                        if (node.Mesh.AssetPath == "builtin://sphere") pmc.RHIMeshAsset = RHIMeshRegistry::Get().GetSphere();
                        else if (node.Mesh.AssetPath == "builtin://cylinder") pmc.RHIMeshAsset = RHIMeshRegistry::Get().GetCylinder();
                        else if (node.Mesh.AssetPath == "builtin://cone") pmc.RHIMeshAsset = RHIMeshRegistry::Get().GetCone();
                        else if (node.Mesh.AssetPath == "builtin://plane") pmc.RHIMeshAsset = RHIMeshRegistry::Get().GetPlane();
                        else if (node.Mesh.AssetPath == "builtin://cube") pmc.RHIMeshAsset = RHIMeshRegistry::Get().GetCube();
                        else {
                            auto resolved = FileSystem::ResolveAssetPath(node.Mesh.AssetPath);
                            pmc.RHIMeshAsset = RHIMeshRegistry::Get().GetOrCreateMesh(resolved.string());
                        }
                    }
                    if (!pmc.RHIMeshAsset) pmc.RHIMeshAsset = RHIMeshRegistry::Get().GetCube();
                    pmc.Color1 = node.Mesh.Color1;
                    pmc.Color2 = node.Mesh.Color2;
                    pmc.PatternMode = node.Mesh.PatternMode;
                }
                for (const auto& child : node.Children) {
                    spawnNodes(child, worldPos);
                }
            };
            spawnNodes(m_CurrentAssembly.RootNode, glm::vec3(0.0f));
        } else {
            for (const auto& part : m_CurrentPackage.Parts) {
                auto partEnt = m_PreviewScene->CreateEntity(part.Name.empty() ? "Part" : part.Name);
                partEnt.SetParent(m_PreviewEntity, false);
                auto& ptc = partEnt.GetComponent<TransformComponent>();
                ptc.Translation = part.LocalTranslation - m_Center;
                ptc.Rotation = part.LocalRotation;
                ptc.Scale = part.LocalScale;

                auto& pmc = partEnt.AddComponent<MeshInstance3D>();
                if (!m_CurrentAssetPath.empty()) {
                    auto partFullPath = m_CurrentAssetPath.parent_path() / part.RelativePath;
                    pmc.RHIMeshAsset = RHIMeshRegistry::Get().GetOrCreateMesh(partFullPath.string());
                }
                if (!pmc.RHIMeshAsset) pmc.RHIMeshAsset = RHIMeshRegistry::Get().GetCube();
                pmc.Color1 = part.LoadedMesh.SourceData.ColorPrimary;
                pmc.Color2 = part.LoadedMesh.SourceData.ColorSecondary;
                pmc.PatternMode = part.LoadedMesh.SourceData.PatternMode;
            }
        }

        // Godot-style rig: a neutral light from above/front and a dimmer light
        // from below. Add fill first so the key is the primary directional sun.
        m_PreviewFillLight = m_PreviewScene->CreateEntity("PreviewFillLight");
        auto& flc = m_PreviewFillLight.AddComponent<DirectionalLightComponent>();
        flc.Azimuth = 0.0f;
        flc.Elevation = -90.0f;
        flc.Color = glm::vec3(0.7f);
        flc.Intensity = m_EnableSecondaryLight ? 1.0f : 0.0f;
        flc.AmbientIntensity = 0.65f;
        flc.CastShadows = false;

        m_PreviewLight = m_PreviewScene->CreateEntity("PreviewSunLight");
        auto& dlc = m_PreviewLight.AddComponent<DirectionalLightComponent>();
        dlc.Azimuth = m_SunAzimuth;
        dlc.Elevation = m_SunElevation;
        dlc.Color = glm::vec3(1.0f);
        dlc.Intensity = m_EnableKeyLight ? 1.0f : 0.0f;
        dlc.AmbientIntensity = m_EnableKeyLight ? 0.65f : 0.25f;

        dlc.CastShadows = false;

        m_SceneNeedsRebuild = false;
    }

    void AssetPreviewPanel::RenderPreviewRHI(float width, float height) {
        if (!m_RHIViewport || !m_RHIViewport->IsValid()) return;

        if (!m_RHISceneRenderer) {
            m_RHISceneRenderer = std::make_unique<RHISceneRenderer>();
            m_RHISceneRenderer->Init(RHI::RHIDevice::Get(), false);
        }

        if (m_SceneNeedsRebuild || !m_PreviewScene) {
            RebuildPreviewScene();
        }
        if (!m_PreviewScene) return;

        // Apply manual rotation even while automatic rotation is paused.
        if (m_PreviewEntity && m_PreviewEntity.HasComponent<TransformComponent>()) {
            auto& tc = m_PreviewEntity.GetComponent<TransformComponent>();
            tc.Translation = m_PanOffset;
            tc.Rotation = GetPreviewRotation();
            tc.Scale = glm::vec3(GetPreviewScale());
        }

        // Camera setup
        float pitchRad = glm::radians(m_Pitch);
        float yawRad = glm::radians(m_Yaw);
        bool isMaterial = IsMaterialPreview();
        float distance = m_MaxDim * (isMaterial ? 3.0f : 2.2f);

        glm::vec3 camPos = glm::vec3(
            distance * std::cos(pitchRad) * std::sin(yawRad),
            distance * std::sin(pitchRad),
            distance * std::cos(pitchRad) * std::cos(yawRad)
        );

        // Two fixed world-space lights, independent of all mouse gestures.
        if (m_PreviewLight && m_PreviewLight.HasComponent<DirectionalLightComponent>()) {
            auto& dlc = m_PreviewLight.GetComponent<DirectionalLightComponent>();
            dlc.Elevation = m_SunElevation;
            dlc.Azimuth = m_SunAzimuth;
        }

        float aspect = (height > 0.0f) ? (width / height) : 1.0f;
        PerspectiveCamera camera(isMaterial ? 20.0f : 45.0f, aspect, 0.05f, distance * 8.0f);
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        camera.SetViewMatrix(view, camPos);

        SceneRenderSettings settings;
        settings.EnableShadows = false;
        settings.EnableSkybox = false;
        settings.EnableStudioLightLobes = false;
        settings.EnableToneMapping = true;
        settings.EnableFXAA = true;
        settings.MSAASamples = 4;
        settings.ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // Transparent clear for Godot checkerboard

        m_RHISceneRenderer->RenderScene(
            m_PreviewScene,
            camera,
            m_RHIViewport->GetColorTexture(),
            m_RHIViewport->GetDepthTexture(),
            settings
        );
    }

    void AssetPreviewPanel::RenderPreviewViewport() {
        if (!m_HasAsset) return;

        if (m_TurntableActive && !m_IsViewportHovered) {
            m_ObjectYaw += ImGui::GetIO().DeltaTime * m_TurntableSpeed * 30.0f;
            if (m_ObjectYaw > 360.0f) m_ObjectYaw -= 360.0f;
        }

        bool isMaterial = IsMaterialPreview();
        float uiScale = ImGui::GetFontSize() / 15.5f;
        float availW = ImGui::GetContentRegionAvail().x;
        float viewportH = std::clamp(availW * 0.54f, 150.0f, 240.0f);

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + availW, p0.y + viewportH);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 1. Godot-style Checkerboard background grid (Refined Neutral Mid-Gray Studio)
        const float tileSize = 32.0f * uiScale;
        for (float y = p0.y; y < p1.y; y += tileSize) {
            for (float x = p0.x; x < p1.x; x += tileSize) {
                int cx = static_cast<int>((x - p0.x) / tileSize);
                int cy = static_cast<int>((y - p0.y) / tileSize);
                ImU32 col = ((cx + cy) % 2 == 0) ? IM_COL32(128, 128, 128, 255) : IM_COL32(64, 64, 64, 255);
                ImVec2 tileMax(std::min(x + tileSize, p1.x), std::min(y + tileSize, p1.y));
                drawList->AddRectFilled(ImVec2(x, y), tileMax, col);
            }
        }

        // 2. Render 3D Scene into Viewport
        if (RHI::RHIDevice::Get().IsInitialized()) {
            if (!m_RHIViewport && m_ImGuiLayer) {
                InitRHI(m_ImGuiLayer);
            }
            if (m_RHIViewport && m_RHIViewport->IsValid()) {
                if ((uint32_t)availW != m_ViewportWidth || (uint32_t)viewportH != m_ViewportHeight) {
                    m_ViewportWidth = (uint32_t)availW;
                    m_ViewportHeight = (uint32_t)viewportH;
                    m_RHIViewport->Resize(m_ViewportWidth, m_ViewportHeight);
                }
                RenderPreviewRHI(availW, viewportH);
                uint64_t rhiTexID = m_RHIViewport->GetImTextureID();
                if (rhiTexID != 0) {
                    glm::vec2 uv0 = m_RHIViewport->GetUV0();
                    glm::vec2 uv1 = m_RHIViewport->GetUV1();
                    ImGui::Image((ImTextureID)(intptr_t)rhiTexID, ImVec2(availW, viewportH), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
                }
            }
        } else if (Window::HasActiveContext() && m_Framebuffer) {
            RenderPreview3D(availW, viewportH);
            u32 texID = m_Framebuffer->GetColorAttachmentRendererID();
            ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(availW, viewportH), ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImGui::Dummy(ImVec2(availW, viewportH));
        }

        m_IsViewportHovered = ImGui::IsItemHovered();

        // Drag & Drop custom mesh into preview turntable
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const wchar_t* pathStr = (const wchar_t*)payload->Data;
                std::filesystem::path droppedPath(pathStr);
                std::string ext = droppedPath.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
                if (ext == ".axlemesh" || ext == ".axle" || ext == ".obj" || ext == ".glb" || ext == ".gltf") {
                    LoadCustomPreviewMesh(droppedPath);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Flat, inset icon strips match the reference without covering the material.
        // Place every button explicitly: ImGui's normal line flow would put the
        // second light at the left edge instead of below the first one.
        const float inset = 2.0f * uiScale;
        const float buttonSize = 28.0f * uiScale;
        const float rowStep = 32.0f * uiScale;
        const float iconSize = 16.0f * uiScale;
        drawList->PushClipRect(p0, p1, true);
        auto DrawPreviewIcon = [&](const char* id, const char* icon, const char* fallback,
                                   ImVec2 position, bool enabled, const char* tooltip) {
            ImGui::SetCursorScreenPos(position);
            ImGui::PushID(id);
            bool clicked = ImGui::InvisibleButton("##button", ImVec2(buttonSize, buttonSize));
            bool hovered = ImGui::IsItemHovered();
            if (hovered || ImGui::IsItemActive()) m_IsViewportHovered = false;
            if (hovered) {
                drawList->AddRectFilled(position, ImVec2(position.x + buttonSize, position.y + buttonSize),
                                       IM_COL32(255, 255, 255, 28), 3.0f * uiScale);
                ImGui::SetTooltip("%s", tooltip);
            }
            uint64_t texture = GetOrCreateIconTexture(icon);
            ImU32 tint = enabled ? IM_COL32(235, 235, 235, 255) : IM_COL32(155, 155, 155, 210);
            ImVec2 iconPos(position.x + (buttonSize - iconSize) * 0.5f,
                           position.y + (buttonSize - iconSize) * 0.5f);
            if (texture) {
                drawList->AddImage((ImTextureID)(intptr_t)texture, iconPos,
                                  ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                                  ImVec2(0, 0), ImVec2(1, 1), tint);
            } else {
                ImVec2 textSize = ImGui::CalcTextSize(fallback);
                drawList->AddText(ImVec2(position.x + (buttonSize - textSize.x) * 0.5f,
                                         position.y + (buttonSize - textSize.y) * 0.5f), tint, fallback);
            }
            ImGui::PopID();
            return clicked;
        };

        if (isMaterial) {
            float left = p0.x + inset;
            float right = p1.x - inset - buttonSize;
            if (DrawPreviewIcon("sphere", "icon_mesh_sphere.png", "O", ImVec2(left, p0.y + inset),
                                m_PreviewMeshType == PreviewMeshType::Sphere, "Preview on sphere"))
                SetPreviewMeshType(PreviewMeshType::Sphere);
            if (DrawPreviewIcon("cube", "icon_mesh_cube.png", "#", ImVec2(left, p0.y + inset + rowStep),
                                m_PreviewMeshType == PreviewMeshType::Cube, "Preview on cube"))
                SetPreviewMeshType(PreviewMeshType::Cube);
            if (DrawPreviewIcon("plane", "icon_mesh_plane.png", "-", ImVec2(left, p0.y + inset + rowStep * 2.0f),
                                m_PreviewMeshType == PreviewMeshType::Plane, "Preview on quad"))
                SetPreviewMeshType(PreviewMeshType::Plane);
            if (DrawPreviewIcon("light1", "icon_light_1.png", "1", ImVec2(right, p0.y + inset),
                                m_EnableKeyLight, "Toggle first light")) {
                m_EnableKeyLight = !m_EnableKeyLight;
                UpdatePreviewLighting();
            }
            if (DrawPreviewIcon("light2", "icon_light_2.png", "2", ImVec2(right, p0.y + inset + rowStep),
                                m_EnableSecondaryLight, "Toggle second light")) {
                m_EnableSecondaryLight = !m_EnableSecondaryLight;
                UpdatePreviewLighting();
            }
        }
        drawList->PopClipRect();

        // Keep extra preview actions available without adding toolbar clutter.
        if (m_IsViewportHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            ImGui::OpenPopup("PreviewOptions");
        if (ImGui::BeginPopup("PreviewOptions")) {
            ImGui::MenuItem("Auto-rotate", nullptr, &m_TurntableActive);
            if (ImGui::BeginMenu("Preview mesh")) {
                if (ImGui::MenuItem("Sphere")) SetPreviewMeshType(PreviewMeshType::Sphere);
                if (ImGui::MenuItem("Cube")) SetPreviewMeshType(PreviewMeshType::Cube);
                if (ImGui::MenuItem("Quad")) SetPreviewMeshType(PreviewMeshType::Plane);
                if (ImGui::MenuItem("Cylinder")) SetPreviewMeshType(PreviewMeshType::Cylinder);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Reset preview")) {
                m_Pitch = isMaterial ? 0.0f : 20.0f;
                m_Yaw = isMaterial ? 0.0f : 35.0f;
                m_ObjectPitch = isMaterial && m_PreviewMeshType != PreviewMeshType::Plane ? -15.0f : 0.0f;
                m_ObjectYaw = isMaterial && m_PreviewMeshType != PreviewMeshType::Plane ? 30.0f : 0.0f;
                m_Zoom = 1.0f;
                m_PanOffset = glm::vec3(0.0f);
                m_SunAzimuth = 45.0f;
                m_SunElevation = 35.264f;
                m_EnableKeyLight = true;
                m_EnableSecondaryLight = true;
                m_SceneNeedsRebuild = true;
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsPopupOpen("PreviewOptions")) m_IsViewportHovered = false;

        // Full-width resource type row, like the BaseMaterial3D row in the reference.
        const float footerGap = 4.0f * uiScale;
        const float footerHeight = 32.0f * uiScale;
        ImVec2 footer0(p0.x, p1.y + footerGap);
        ImVec2 footer1(p1.x, footer0.y + footerHeight);
        drawList->AddRectFilled(footer0, footer1, IM_COL32(56, 56, 56, 255), 3.0f * uiScale);
        std::string badgeText = isMaterial ? "BaseMaterial3D" : (m_IsAssembly ? "Assembly3D" : "MeshInstance3D");
        ImVec2 textSize = ImGui::CalcTextSize(badgeText.c_str());
        float labelWidth = textSize.x + iconSize + 6.0f * uiScale;
        float labelX = p0.x + (availW - labelWidth) * 0.5f;
        uint64_t typeIcon = GetOrCreateIconTexture("icon_mesh_cube.png");
        if (typeIcon) {
            ImVec2 iconPos(labelX, footer0.y + (footerHeight - iconSize) * 0.5f);
            drawList->AddImage((ImTextureID)(intptr_t)typeIcon, iconPos,
                              ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                              ImVec2(0, 0), ImVec2(1, 1), IM_COL32(165, 165, 165, 255));
        }
        drawList->AddText(ImVec2(labelX + iconSize + 6.0f * uiScale, footer0.y + (footerHeight - textSize.y) * 0.5f),
                          IM_COL32(210, 210, 210, 255), badgeText.c_str());
        ImGui::SetCursorScreenPos(footer0);
        ImGui::Dummy(ImVec2(availW, footerHeight));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_CurrentAssetPath.filename().string().c_str());

        if (m_IsViewportHovered) {
            // Zoom scales the object, leaving the camera and lights fixed.
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                m_Zoom -= wheel * 0.12f;
                m_Zoom = std::clamp(m_Zoom, 0.25f, 4.0f);
            }

            // Left drag rotates the object around its center, not the camera.
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                m_ObjectYaw += delta.x * 0.75f;
                m_ObjectPitch += delta.y * 0.75f;
                m_ObjectPitch = std::clamp(m_ObjectPitch, -85.0f, 85.0f);
            }

            // Pan moves the object along the fixed camera's screen axes.
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                float cameraYaw = glm::radians(m_Yaw);
                float cameraPitch = glm::radians(m_Pitch);
                glm::vec3 right(std::cos(cameraYaw), 0.0f, -std::sin(cameraYaw));
                glm::vec3 up(-std::sin(cameraPitch) * std::sin(cameraYaw), std::cos(cameraPitch),
                             -std::sin(cameraPitch) * std::cos(cameraYaw));
                m_PanOffset += (right * delta.x - up * delta.y) * 0.005f * m_MaxDim;
            }
        }
    }

    void AssetPreviewPanel::OnImGuiRender(bool* p_open) {
        if (p_open && !*p_open) return;

        ImGui::Begin("Asset Preview", p_open);

        if (!m_HasAsset) {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.45f, 0.70f, 0.95f, 1.0f), "  No 3D Asset Selected");
            ImGui::Spacing();
            ImGui::TextDisabled("  Click any 3D model asset (.axle, .glb, .obj)\n  in the Content Browser to inspect it in 3D.");
            ImGui::End();
            return;
        }

        RenderPreviewViewport();

        ImGui::Spacing();

        // Action Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.78f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.58f, 0.90f, 1.0f));
        if (ImGui::Button("+ Instantiate into Scene", ImVec2(-1.0f, 28.0f))) {
            SpawnCurrentAssetIntoScene();
        }
        ImGui::PopStyleColor(2);

        ImGui::End();
    }

}
