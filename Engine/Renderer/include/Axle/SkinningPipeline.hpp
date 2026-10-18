#pragma once
#include <Axle/Types.hpp>
#include <Axle/Shader.hpp>
#include <Axle/VertexArray.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Axle {

    class SkinningPipeline {
    public:
        static constexpr size_t MAX_BONES = 128;

        SkinningPipeline();
        ~SkinningPipeline() = default;

        void Init();

        // Get shader instances
        std::shared_ptr<Shader> GetShader() const { return m_Shader; }
        std::shared_ptr<Shader> GetShadowShader() const { return m_ShadowShader; }

        // Bind and upload bone matrices palette (up to 128 bones)
        void UploadBonePalette(Shader& shader, const std::vector<glm::mat4>& boneMatrices);
        void UploadBonePalette(Shader& shader, const glm::mat4* boneMatrices, size_t count);

        // Upload identity bone palette (fallback static pose)
        void UploadIdentityPalette(Shader& shader);

        // Render an animated skinned mesh draw call
        void RenderSkinned(Shader& shader,
                           const std::shared_ptr<VertexArray>& vao,
                           const glm::mat4& worldTransform,
                           const std::vector<glm::mat4>& boneMatrices);

    private:
        void LoadShaders();

    private:
        std::shared_ptr<Shader> m_Shader;
        std::shared_ptr<Shader> m_ShadowShader;
        bool m_Initialized = false;
    };

}
