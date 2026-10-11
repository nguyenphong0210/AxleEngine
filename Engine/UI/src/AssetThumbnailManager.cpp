#include <Axle/AssetThumbnailManager.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Log.hpp>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

namespace Axle {

    static const char* s_ThumbnailVertSrc = R"(
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

    static const char* s_ThumbnailFragSrc = R"(
        #version 450 core
        layout(location = 0) out vec4 o_Color;

        in vec3 v_WorldPos;
        in vec3 v_LocalPos;

        uniform int u_PatternMode;
        uniform float u_TileScale;
        uniform vec4 u_Color1;
        uniform vec4 u_Color2;
        uniform vec3 u_LightDir;
        uniform mat4 u_Transform;

        void main() {
            float pattern = 0.0;
            vec3 normal = vec3(0.0, 1.0, 0.0);
            if (u_PatternMode == 0) {
                vec2 coord = floor(v_WorldPos.xz * u_TileScale);
                pattern = mod(coord.x + coord.y, 2.0);
            } else if (u_PatternMode == 1) {
                vec3 coord = floor((v_LocalPos + vec3(0.5)) * u_TileScale + vec3(0.0001));
                pattern = mod(coord.x + coord.y + coord.z, 2.0);
                vec3 dx = dFdx(v_WorldPos);
                vec3 dy = dFdy(v_WorldPos);
                normal = normalize(cross(dx, dy));
                if (dot(normal, normalize(-v_WorldPos)) < 0.0) normal = -normal;
            } else {
                vec3 nLocal = normalize(v_LocalPos);
                float u = atan(nLocal.z, nLocal.x) / (2.0 * 3.14159265359) + 0.5;
                float v = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265359 + 0.5;
                vec2 coord = floor(vec2(u * u_TileScale, v * (u_TileScale * 0.5)));
                pattern = mod(coord.x + coord.y, 2.0);
                normal = normalize((u_Transform * vec4(v_LocalPos, 0.0)).xyz);
            }

            vec4 baseColor = mix(u_Color1, u_Color2, pattern);
            vec3 keyLight = normalize(u_LightDir);
            vec3 fillLight = normalize(vec3(-0.4, 0.5, -0.6));

            float NdotKey = max(dot(normal, keyLight), 0.0);
            float NdotFill = max(dot(normal, fillLight), 0.0) * 0.35;
            float ambient = 0.34;
            float lighting = ambient + 0.56 * NdotKey + NdotFill;

            vec3 viewDir = normalize(-v_WorldPos);
            vec3 halfDir = normalize(keyLight + viewDir);
            float spec = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.22;

            o_Color = vec4(baseColor.rgb * lighting + vec3(spec), baseColor.a);
        }
    )";

    AssetThumbnailManager::AssetThumbnailManager() {
        InitRenderer();
    }

    AssetThumbnailManager::~AssetThumbnailManager() {
        Clear();
        if (m_RBO) {
            glDeleteRenderbuffers(1, &m_RBO);
            m_RBO = 0;
        }
        if (m_FBO) {
            glDeleteFramebuffers(1, &m_FBO);
            m_FBO = 0;
        }
    }

    void AssetThumbnailManager::InitRenderer() {
        glGenFramebuffers(1, &m_FBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

        glGenRenderbuffers(1, &m_RBO);
        glBindRenderbuffer(GL_RENDERBUFFER, m_RBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_ThumbnailResolution, m_ThumbnailResolution);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_RBO);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        m_Shader = std::make_unique<Shader>(s_ThumbnailVertSrc, s_ThumbnailFragSrc);
    }

    void AssetThumbnailManager::Clear() {
        for (auto& [path, texID] : m_ThumbnailCache) {
            if (texID != 0) {
                glDeleteTextures(1, &texID);
            }
        }
        m_ThumbnailCache.clear();
        m_MetaCache.clear();
        m_PendingQueue.clear();
    }

    const AssetCachedMeta* AssetThumbnailManager::GetCachedMeta(const std::filesystem::path& path) const {
        std::string key = path.lexically_normal().string();
        auto it = m_MetaCache.find(key);
        if (it != m_MetaCache.end()) {
            return &it->second;
        }
        return nullptr;
    }

    u32 AssetThumbnailManager::GetOrCreateThumbnail(const std::filesystem::path& path) {
        std::string key = path.lexically_normal().string();
        auto it = m_ThumbnailCache.find(key);
        if (it != m_ThumbnailCache.end()) {
            return it->second;
        }

        // Check if path is supported 3D format
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
        if (ext != ".axle" && ext != ".axlebin" && ext != ".obj" && ext != ".gltf" && ext != ".glb" && ext != ".fbx") {
            return 0;
        }

        // Render synchronously if pending queue is empty or small
        if (m_PendingQueue.size() < 3) {
            u32 texID = 0;
            AssetCachedMeta meta;
            if (RenderThumbnail(path, texID, meta)) {
                m_ThumbnailCache[key] = texID;
                m_MetaCache[key] = meta;
                return texID;
            }
        }

        // Queue for background frame slicing
        if (std::find(m_PendingQueue.begin(), m_PendingQueue.end(), path) == m_PendingQueue.end()) {
            m_PendingQueue.push_back(path);
        }

        return 0;
    }

    void AssetThumbnailManager::OnUpdate() {
        if (m_PendingQueue.empty()) return;

        size_t count = 0;
        while (!m_PendingQueue.empty() && count < m_MaxGenerationsPerFrame) {
            auto path = m_PendingQueue.front();
            m_PendingQueue.erase(m_PendingQueue.begin());

            std::string key = path.lexically_normal().string();
            if (m_ThumbnailCache.find(key) != m_ThumbnailCache.end()) {
                continue;
            }

            u32 texID = 0;
            AssetCachedMeta meta;
            if (RenderThumbnail(path, texID, meta)) {
                m_ThumbnailCache[key] = texID;
                m_MetaCache[key] = meta;
            } else {
                m_ThumbnailCache[key] = 0; // Mark failed to avoid infinite retry
            }
            count++;
        }
    }

    bool AssetThumbnailManager::RenderThumbnail(const std::filesystem::path& path, u32& outTexID, AssetCachedMeta& outMeta) {
        if (!std::filesystem::exists(path)) return false;

        auto pkg = AssetSerializer::LoadPackage(path);
        if (!pkg.Success || pkg.Parts.empty()) {
            return false;
        }

        // 1. Compute aggregate bounds across all loaded parts
        glm::vec3 minBound(1e9f);
        glm::vec3 maxBound(-1e9f);
        size_t totalVertices = 0;
        size_t totalIndices = 0;

        for (const auto& part : pkg.Parts) {
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

        glm::vec3 center = (minBound + maxBound) * 0.5f;
        glm::vec3 size = maxBound - minBound;
        float maxDim = std::max({ size.x, size.y, size.z, 0.05f });

        // Populate metadata
        outMeta.Name = pkg.Name.empty() ? path.stem().string() : pkg.Name;
        outMeta.VertexCount = totalVertices;
        outMeta.TriangleCount = totalIndices / 3;
        outMeta.BoundingSize = size;
        outMeta.Center = center;
        outMeta.MaxDimension = maxDim;
        outMeta.IsPackage = pkg.IsPackage;
        outMeta.SubPartCount = pkg.Parts.size();

        if (!pkg.Parts.empty()) {
            const auto& firstPart = pkg.Parts[0].LoadedMesh.SourceData;
            outMeta.ColorPrimary = firstPart.ColorPrimary;
            outMeta.ColorSecondary = firstPart.ColorSecondary;
            outMeta.PatternMode = firstPart.PatternMode;
            outMeta.TileScale = firstPart.TileScale;
        }

        // 2. Allocate 2D Texture attachment
        GLuint texID = 0;
        glGenTextures(1, &texID);
        glBindTexture(GL_TEXTURE_2D, texID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_ThumbnailResolution, m_ThumbnailResolution, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // 3. Bind offscreen FBO and attach texture
        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texID, 0);

        glViewport(0, 0, m_ThumbnailResolution, m_ThumbnailResolution);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.11f, 0.13f, 0.17f, 1.0f); // Sleek studio charcoal
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 4. Studio isometric framing camera
        float pitchRad = glm::radians(24.0f);
        float yawRad = glm::radians(38.0f);
        float distance = maxDim * 1.85f;
        glm::vec3 camPos = glm::vec3(
            distance * std::cos(pitchRad) * std::sin(yawRad),
            distance * std::sin(pitchRad),
            distance * std::cos(pitchRad) * std::cos(yawRad)
        );
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.01f, distance * 6.0f);
        glm::mat4 viewProj = proj * view;

        m_Shader->Bind();
        m_Shader->SetMat4("u_ViewProjection", viewProj);
        m_Shader->SetFloat3("u_LightDir", glm::vec3(0.5f, 0.85f, 0.6f));

        // 5. Draw parts with center offset
        for (const auto& part : pkg.Parts) {
            if (!part.LoadedMesh.VAO) continue;
            glm::mat4 localMat = glm::translate(glm::mat4(1.0f), part.LocalTranslation)
                               * glm::mat4_cast(part.LocalRotation)
                               * glm::scale(glm::mat4(1.0f), part.LocalScale);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), -center) * localMat;

            m_Shader->SetMat4("u_Transform", model);
            m_Shader->SetInt("u_PatternMode", part.LoadedMesh.SourceData.PatternMode);
            m_Shader->SetFloat("u_TileScale", part.LoadedMesh.SourceData.TileScale);
            m_Shader->SetFloat4("u_Color1", part.LoadedMesh.SourceData.ColorPrimary);
            m_Shader->SetFloat4("u_Color2", part.LoadedMesh.SourceData.ColorSecondary);

            RenderCommand::DrawIndexed(part.LoadedMesh.VAO);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        outTexID = texID;
        return true;
    }

}
