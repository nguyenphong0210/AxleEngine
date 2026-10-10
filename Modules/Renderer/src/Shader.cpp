#include <Axle/Shader.hpp>
#include <glad/glad.h>
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

        // Check program linking status
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

        // Always detach and delete intermediate shaders after linking
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

    Shader::Shader(Shader&& other) noexcept : m_RendererID(other.m_RendererID) {
        other.m_RendererID = 0;
    }

    Shader& Shader::operator=(Shader&& other) noexcept {
        if (this != &other) {
            if (m_RendererID != 0) {
                glDeleteProgram(m_RendererID);
            }
            m_RendererID = other.m_RendererID;
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