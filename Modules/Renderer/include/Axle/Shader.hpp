#pragma once
#include <Axle/Types.hpp>
#include <string>

namespace Axle {

    class Shader {
    public:
        Shader(const std::string& vertexSrc, const std::string& fragmentSrc);
        ~Shader();

        // Prevent copying to avoid double deletion of GPU resources
        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        // Allow moving
        Shader(Shader&& other) noexcept;
        Shader& operator=(Shader&& other) noexcept;

        void Bind() const;
        void Unbind() const;

        u32 GetRendererID() const { return m_RendererID; }

    private:
        u32 CompileShader(u32 type, const std::string& source);

    private:
        u32 m_RendererID = 0;
    };

}