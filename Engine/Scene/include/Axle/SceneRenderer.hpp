#pragma once
#include <Axle/Types.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/Shader.hpp>
#include <Axle/ShadowMap.hpp>
#include <Axle/Skybox.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsDebugRenderer.hpp>

#include <memory>
#include <glm/glm.hpp>

namespace Axle {

    class Scene;

    struct SceneRenderSettings {
        bool EnableShadows = true;
        bool EnableSkybox = true;
        bool ShowPhysicsDebug = false;
        PhysicsDrawSettings PhysicsDrawSettings;
        float Exposure = 1.0f;
        int ToneMappingMode = 0; // 0: ACES Filmic, 1: Reinhard, 2: Uncharted 2, 3: Linear Bypass
        float Gamma = 2.2f;
    };

    class SceneRenderer {
    public:
        SceneRenderer();
        ~SceneRenderer() = default;

        void Init();

        // Main rendering entry point
        // If targetFBO is provided, renders offscreen into targetFBO.
        // If targetFBO is nullptr, renders directly to screen backbuffer (Viewport 0,0,m_ViewportWidth,m_ViewportHeight).
        void RenderScene(const std::shared_ptr<Scene>& scene,
                         const PerspectiveCamera& camera,
                         const std::shared_ptr<Framebuffer>& targetFBO = nullptr,
                         PhysicsScene* physicsScene = nullptr,
                         PhysicsDebugRenderer* physicsDebugRenderer = nullptr,
                         const SceneRenderSettings& settings = SceneRenderSettings{});

        void SetViewportSize(u32 width, u32 height) {
            m_ViewportWidth = width;
            m_ViewportHeight = height;
        }

        u32 GetViewportWidth() const { return m_ViewportWidth; }
        u32 GetViewportHeight() const { return m_ViewportHeight; }

        Shader* GetShader() const { return m_Shader.get(); }
        Shader* GetShadowShader() const { return m_ShadowShader.get(); }
        ShadowMap* GetShadowMap() const { return m_ShadowMap.get(); }
        Skybox* GetSkybox() const { return m_Skybox.get(); }

        const glm::mat4& GetLightSpaceMatrix() const { return m_LightSpaceMatrix; }
        const glm::vec3& GetLightDirection() const { return m_LightDir; }

    private:
        void InitShaders();

    private:
        std::shared_ptr<Shader> m_Shader;
        std::shared_ptr<Shader> m_ShadowShader;
        std::shared_ptr<ShadowMap> m_ShadowMap;
        std::shared_ptr<Skybox> m_Skybox;

        glm::mat4 m_LightSpaceMatrix{ 1.0f };
        glm::vec3 m_LightDir{ 0.45f, 1.0f, 0.65f };

        u32 m_ViewportWidth = 1280;
        u32 m_ViewportHeight = 720;
    };

}
