#include <Axle/VertexArray.hpp>
#include <glad/glad.h>

namespace Axle {

    static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type) {
        switch (type) {
            case ShaderDataType::Float:
            case ShaderDataType::Float2:
            case ShaderDataType::Float3:
            case ShaderDataType::Float4:
            case ShaderDataType::Mat3:
            case ShaderDataType::Mat4:    return GL_FLOAT;
            case ShaderDataType::Int:
            case ShaderDataType::Int2:
            case ShaderDataType::Int3:
            case ShaderDataType::Int4:    return GL_INT;
            case ShaderDataType::Bool:    return GL_BOOL;
            default:                      return 0;
        }
    }

    VertexArray::VertexArray() {
        if (glad_glCreateVertexArrays) {
            glCreateVertexArrays(1, &m_RendererID);
        }
    }

    VertexArray::~VertexArray() {
        if (glad_glDeleteVertexArrays && m_RendererID) {
            glDeleteVertexArrays(1, &m_RendererID);
        }
    }

    void VertexArray::Bind() const {
        if (glad_glBindVertexArray) {
            glBindVertexArray(m_RendererID);
        }
    }

    void VertexArray::Unbind() const {
        if (glad_glBindVertexArray) {
            glBindVertexArray(0);
        }
    }

    void VertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer) {
        if (!glad_glBindVertexArray) {
            m_VertexBuffers.push_back(vertexBuffer);
            return;
        }

        glBindVertexArray(m_RendererID);
        vertexBuffer->Bind();

        const auto& layout = vertexBuffer->GetLayout();
        for (const auto& element : layout.GetElements()) {
            glEnableVertexAttribArray(m_VertexBufferIndex);
            glVertexAttribPointer(
                m_VertexBufferIndex,
                static_cast<GLint>(element.GetComponentCount()),
                ShaderDataTypeToOpenGLBaseType(element.Type),
                element.Normalized ? GL_TRUE : GL_FALSE,
                static_cast<GLsizei>(layout.GetStride()),
                reinterpret_cast<const void*>(static_cast<uintptr_t>(element.Offset))
            );
            m_VertexBufferIndex++;
        }

        m_VertexBuffers.push_back(vertexBuffer);
    }

    void VertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) {
        if (glad_glBindVertexArray) {
            glBindVertexArray(m_RendererID);
            if (indexBuffer) indexBuffer->Bind();
        }

        m_IndexBuffer = indexBuffer;
    }

}