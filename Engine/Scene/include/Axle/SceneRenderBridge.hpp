#pragma once
#include <Axle/Types.hpp>
#include <Axle/Shader.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Axle {

    class SceneData;
    class SkinningPipeline;
    class ParticleRenderer;
    class RHIParticleRenderer;
    class Framebuffer;
    class MaterialTextureCache;
    namespace RHI { class RHICommandBuffer; }

    class SceneRenderBridge {
    public:
        // Render geometry pass with active lighting, materials & shadow sampling
        static void RenderGeometryPass(SceneData& scene, const PerspectiveCamera& camera, Shader& shader,
                                       const glm::mat4& lightSpaceMatrix = glm::mat4(1.0f),
                                       const glm::vec3& lightDir = glm::vec3(0.45f, 1.0f, 0.65f),
                                       int shadowMapSlot = 1,
                                       const std::vector<glm::mat4>& cascadeMatrices = {},
                                       const std::vector<float>& cascadeSplitDistances = {});

        // Render shadow depth pass from light's perspective
        static void RenderShadowPass(SceneData& scene, Shader& shadowShader, const glm::mat4& lightSpaceMatrix);

        // Render skeletal/skinned mesh components (forward pass and shadow pass)
        static void RenderSkeletalMeshes(SceneData& scene, SkinningPipeline& skinningPipeline, const PerspectiveCamera& camera,
                                        const glm::mat4& lightSpaceMatrix = glm::mat4(1.0f),
                                        const glm::vec3& lightDir = glm::vec3(0.45f, 1.0f, 0.65f),
                                        bool shadowPass = false);

        // Render particle emitters (GPU-instanced billboarding)
        static void RenderParticles(SceneData& scene, ParticleRenderer& particleRenderer, const PerspectiveCamera& camera,
                                    const std::shared_ptr<Framebuffer>& targetFBO = nullptr);

        static void RenderParticles(SceneData& scene, RHIParticleRenderer& particleRenderer, const PerspectiveCamera& camera,
                                    RHI::RHICommandBuffer& cmd, MaterialTextureCache* textures = nullptr);
    };

}
