#include <Axle/Shader.hpp>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

namespace Axle {

    Shader::Shader(const std::string& vertexSrc, const std::string& fragmentSrc) {
        const u32 vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSrc);
        if (vertexShader == 0) return;

        const u32 fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc);
        if (fragmentShader == 0) {
            glDeleteShader(vertexShader);
            return;
        }

        m_RendererID = glCreateProgram();
        glAttachShader(m_RendererID, vertexShader);
        glAttachShader(m_RendererID, fragmentShader);
        glLinkProgram(m_RendererID);

        GLint isLinked = 0;
        glGetProgramiv(m_RendererID, GL_LINK_STATUS, &isLinked);
        if (isLinked == GL_FALSE) {
            GLint maxLength = 0;
            glGetProgramiv(m_RendererID, GL_INFO_LOG_LENGTH, &maxLength);

            std::vector<GLchar> infoLog(maxLength);
            glGetProgramInfoLog(m_RendererID, maxLength, &maxLength, infoLog.data());

            std::cerr << "[Shader Link Error]: " << infoLog.data() << "\n";

            glDeleteProgram(m_RendererID);
            m_RendererID = 0;
        }

        glDetachShader(m_RendererID, vertexShader);
        glDetachShader(m_RendererID, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    Shader::~Shader() {
        if (m_RendererID != 0) {
            glDeleteProgram(m_RendererID);
        }
    }

    Shader::Shader(Shader&& other) noexcept 
        : m_RendererID(other.m_RendererID), m_UniformLocationCache(std::move(other.m_UniformLocationCache)) {
        other.m_RendererID = 0;
    }

    Shader& Shader::operator=(Shader&& other) noexcept {
        if (this != &other) {
            if (m_RendererID != 0) {
                glDeleteProgram(m_RendererID);
            }
            m_RendererID = other.m_RendererID;
            m_UniformLocationCache = std::move(other.m_UniformLocationCache);
            other.m_RendererID = 0;
        }
        return *this;
    }

    void Shader::Bind() const {
        glUseProgram(m_RendererID);
    }

    void Shader::Unbind() const {
        glUseProgram(0);
    }

    void Shader::SetInt(const std::string& name, i32 value) {
        glUniform1i(GetUniformLocation(name), value);
    }

    void Shader::SetFloat(const std::string& name, f32 value) {
        glUniform1f(GetUniformLocation(name), value);
    }

    void Shader::SetFloat2(const std::string& name, const glm::vec2& value) {
        glUniform2f(GetUniformLocation(name), value.x, value.y);
    }

    void Shader::SetFloat3(const std::string& name, const glm::vec3& value) {
        glUniform3f(GetUniformLocation(name), value.x, value.y, value.z);
    }

    void Shader::SetFloat4(const std::string& name, const glm::vec4& value) {
        glUniform4f(GetUniformLocation(name), value.x, value.y, value.z, value.w);
    }

    void Shader::SetMat3(const std::string& name, const glm::mat3& matrix) {
        glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
    }

    void Shader::SetMat4(const std::string& name, const glm::mat4& matrix) {
        glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
    }

    i32 Shader::GetUniformLocation(const std::string& name) {
        if (m_UniformLocationCache.contains(name)) {
            return m_UniformLocationCache[name];
        }

        const i32 location = glGetUniformLocation(m_RendererID, name.c_str());
        if (location == -1) {
            std::cerr << "[Shader Warning] Uniform '" << name << "' does not exist or was stripped out!\n";
        }

        m_UniformLocationCache[name] = location;
        return location;
    }

    u32 Shader::CompileShader(u32 type, const std::string& source) {
        const u32 id = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);

        GLint isCompiled = 0;
        glGetShaderiv(id, GL_COMPILE_STATUS, &isCompiled);
        if (isCompiled == GL_FALSE) {
            GLint maxLength = 0;
            glGetShaderiv(id, GL_INFO_LOG_LENGTH, &maxLength);

            std::vector<GLchar> infoLog(maxLength);
            glGetShaderInfoLog(id, maxLength, &maxLength, infoLog.data());

            const std::string stageName = (type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment";
            std::cerr << "[Shader Compile Error (" << stageName << ")]: " << infoLog.data() << "\n";

            glDeleteShader(id);
            return 0;
        }

        return id;
    }

}