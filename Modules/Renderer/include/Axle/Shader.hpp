#pragma once
#include <Axle/Types.hpp>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Axle {

    class Shader {
    public:
        Shader(const std::string& vertexSrc, const std::string& fragmentSrc);
        ~Shader();

        // Prevent copying
        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        // Allow moving
        Shader(Shader&& other) noexcept;
        Shader& operator=(Shader&& other) noexcept;

        void Bind() const;
        void Unbind() const;

        // Uniform upload methods
        void SetInt(const std::string& name, i32 value);
        void SetFloat(const std::string& name, f32 value);
        void SetFloat2(const std::string& name, const glm::vec2& value);
        void SetFloat3(const std::string& name, const glm::vec3& value);
        void SetFloat4(const std::string& name, const glm::vec4& value);
        void SetMat3(const std::string& name, const glm::mat3& matrix);
        void SetMat4(const std::string& name, const glm::mat4& matrix);

        u32 GetRendererID() const { return m_RendererID; }

    private:
        u32 CompileShader(u32 type, const std::string& source);
        i32 GetUniformLocation(const std::string& name);

    private:
        u32 m_RendererID = 0;
        std::unordered_map<std::string, i32> m_UniformLocationCache;
    };

}