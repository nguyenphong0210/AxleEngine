#include <Axle/SkinningPipeline.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <algorithm>

namespace Axle {

    SkinningPipeline::SkinningPipeline() {
    }

    void SkinningPipeline::Init() {
        if (m_Initialized) return;
        LoadShaders();
        m_Initialized = true;
    }

    void SkinningPipeline::LoadShaders() {
        // 1. Skeletal PBR Vertex & Fragment Shader
        std::string vertSrc;
        std::string fragSrc;

        auto vertPath = FileSystem::ResolveAssetPath("shaders/SkeletalMesh.vert");
        auto fragPath = FileSystem::ResolveAssetPath("shaders/Checkerboard.frag");

        if (FileSystem::Exists(vertPath) && FileSystem::Exists(fragPath)) {
            vertSrc = FileSystem::ReadTextFile(vertPath);
            fragSrc = FileSystem::ReadTextFile(fragPath);
        }

        if (vertSrc.empty() || fragSrc.empty()) {
            vertSrc = R"(
                #version 450 core
                layout(location = 0) in vec3 a_Position;
                layout(location = 1) in vec3 a_Normal;
                layout(location = 2) in vec2 a_TexCoord;
                layout(location = 3) in ivec4 a_BoneIDs;
                layout(location = 4) in vec4 a_BoneWeights;

                const int MAX_BONES = 128;
                uniform mat4 u_BoneMatrices[MAX_BONES];
                uniform int u_SkinningEnabled;

                uniform mat4 u_ViewProjection;
                uniform mat4 u_Transform;
                uniform mat4 u_LightSpaceMatrix;

                out vec3 v_WorldPos;
                out vec3 v_LocalPos;
                out vec2 v_TexCoord;
                out vec4 v_FragPosLightSpace;

                void main() {
                    v_TexCoord = a_TexCoord;
                    vec4 skinnedPos = vec4(0.0);
                    vec3 skinnedNormal = vec3(0.0);

                    if (u_SkinningEnabled == 1) {
                        float totalWeight = 0.0;
                        for (int i = 0; i < 4; ++i) {
                            int boneID = a_BoneIDs[i];
                            float weight = a_BoneWeights[i];
                            if (boneID >= 0 && boneID < MAX_BONES && weight > 0.0) {
                                skinnedPos += (u_BoneMatrices[boneID] * vec4(a_Position, 1.0)) * weight;
                                skinnedNormal += (mat3(u_BoneMatrices[boneID]) * a_Normal) * weight;
                                totalWeight += weight;
                            }
                        }
                        if (totalWeight <= 0.0001) {
                            skinnedPos = vec4(a_Position, 1.0);
                            skinnedNormal = a_Normal;
                        }
                    } else {
                        skinnedPos = vec4(a_Position, 1.0);
                        skinnedNormal = a_Normal;
                    }

                    v_LocalPos = skinnedPos.xyz;
                    vec4 worldPos = u_Transform * vec4(skinnedPos.xyz, 1.0);
                    v_WorldPos = worldPos.xyz;
                    v_FragPosLightSpace = u_LightSpaceMatrix * worldPos;
                    gl_Position = u_ViewProjection * worldPos;
                }
            )";

            fragSrc = R"(
                #version 450 core
                layout(location = 0) out vec4 o_Color;
                in vec3 v_WorldPos;
                in vec3 v_LocalPos;
                in vec2 v_TexCoord;
                in vec4 v_FragPosLightSpace;

                uniform vec4 u_Color1;
                uniform vec3 u_LightDir;
                uniform vec3 u_LightColor;

                void main() {
                    vec3 N = normalize(cross(dFdx(v_WorldPos), dFdy(v_WorldPos)));
                    vec3 L = normalize(u_LightDir);
                    float NdotL = max(dot(N, L), 0.15);
                    vec3 albedo = u_Color1.rgb > vec3(0.0) ? u_Color1.rgb : vec3(0.85);
                    vec3 color = albedo * (u_LightColor * NdotL + vec3(0.2));
                    o_Color = vec4(color, 1.0);
                }
            )";
        }

        m_Shader = std::make_shared<Shader>(vertSrc, fragSrc);

        // 2. Skeletal Shadow Depth Shader
        std::string shadowVertSrc;
        std::string shadowFragSrc;

        auto sVertPath = FileSystem::ResolveAssetPath("shaders/SkeletalShadowDepth.vert");
        auto sFragPath = FileSystem::ResolveAssetPath("shaders/ShadowDepth.frag");

        if (FileSystem::Exists(sVertPath) && FileSystem::Exists(sFragPath)) {
            shadowVertSrc = FileSystem::ReadTextFile(sVertPath);
            shadowFragSrc = FileSystem::ReadTextFile(sFragPath);
        }

        if (shadowVertSrc.empty() || shadowFragSrc.empty()) {
            shadowVertSrc = R"(
                #version 450 core
                layout(location = 0) in vec3 a_Position;
                layout(location = 1) in vec3 a_Normal;
                layout(location = 2) in vec2 a_TexCoord;
                layout(location = 3) in ivec4 a_BoneIDs;
                layout(location = 4) in vec4 a_BoneWeights;

                const int MAX_BONES = 128;
                uniform mat4 u_BoneMatrices[MAX_BONES];
                uniform int u_SkinningEnabled;

                uniform mat4 u_LightSpaceMatrix;
                uniform mat4 u_Transform;

                void main() {
                    vec4 skinnedPos = vec4(0.0);
                    if (u_SkinningEnabled == 1) {
                        float totalWeight = 0.0;
                        for (int i = 0; i < 4; ++i) {
                            int boneID = a_BoneIDs[i];
                            float weight = a_BoneWeights[i];
                            if (boneID >= 0 && boneID < MAX_BONES && weight > 0.0) {
                                skinnedPos += (u_BoneMatrices[boneID] * vec4(a_Position, 1.0)) * weight;
                                totalWeight += weight;
                            }
                        }
                        if (totalWeight <= 0.0001) {
                            skinnedPos = vec4(a_Position, 1.0);
                        }
                    } else {
                        skinnedPos = vec4(a_Position, 1.0);
                    }

                    gl_Position = u_LightSpaceMatrix * u_Transform * vec4(skinnedPos.xyz, 1.0);
                }
            )";

            shadowFragSrc = R"(
                #version 450 core
                void main() {}
            )";
        }

        m_ShadowShader = std::make_shared<Shader>(shadowVertSrc, shadowFragSrc);
        AXLE_CORE_INFO("[SkinningPipeline] Initialized Skeletal Mesh GPU skinning shaders successfully.");
    }

    void SkinningPipeline::UploadBonePalette(Shader& shader, const std::vector<glm::mat4>& boneMatrices) {
        UploadBonePalette(shader, boneMatrices.data(), boneMatrices.size());
    }

    void SkinningPipeline::UploadBonePalette(Shader& shader, const glm::mat4* boneMatrices, size_t count) {
        if (!boneMatrices || count == 0) {
            UploadIdentityPalette(shader);
            return;
        }

        size_t uploadCount = std::min(count, MAX_BONES);
        shader.SetInt("u_SkinningEnabled", 1);
        shader.SetMat4Array("u_BoneMatrices[0]", boneMatrices, static_cast<u32>(uploadCount));
    }

    void SkinningPipeline::UploadIdentityPalette(Shader& shader) {
        static std::vector<glm::mat4> s_IdentityPalette(MAX_BONES, glm::mat4(1.0f));
        shader.SetInt("u_SkinningEnabled", 0);
        shader.SetMat4Array("u_BoneMatrices[0]", s_IdentityPalette.data(), static_cast<u32>(MAX_BONES));
    }

    void SkinningPipeline::RenderSkinned(Shader& shader,
                                         const std::shared_ptr<VertexArray>& vao,
                                         const glm::mat4& worldTransform,
                                         const std::vector<glm::mat4>& boneMatrices) {
        if (!vao) return;

        shader.Bind();
        shader.SetMat4("u_Transform", worldTransform);
        UploadBonePalette(shader, boneMatrices);

        RenderCommand::DrawIndexed(vao);
    }

}
