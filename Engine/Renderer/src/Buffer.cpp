#include <Axle/Buffer.hpp>
#include <glad/glad.h>

namespace Axle {

    // ---------------- VertexBuffer ----------------

    VertexBuffer::VertexBuffer(u32 size) {
        if (glad_glCreateBuffers) {
            glCreateBuffers(1, &m_RendererID);
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
            glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
        }
    }

    VertexBuffer::VertexBuffer(const void* vertices, u32 size) {
        if (glad_glCreateBuffers) {
            glCreateBuffers(1, &m_RendererID);
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
            glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
        }
    }

    VertexBuffer::~VertexBuffer() {
        if (glad_glDeleteBuffers && m_RendererID) {
            glDeleteBuffers(1, &m_RendererID);
        }
    }

    void VertexBuffer::Bind() const {
        if (glad_glBindBuffer && m_RendererID) {
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        }
    }

    void VertexBuffer::Unbind() const {
        if (glad_glBindBuffer) {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    }

    void VertexBuffer::SetData(const void* data, u32 size) {
        if (glad_glBindBuffer && glad_glBufferSubData && m_RendererID) {
            glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
            glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
        }
    }

    // ---------------- IndexBuffer ----------------

    IndexBuffer::IndexBuffer(const u32* indices, u32 count)
        : m_Count(count) {
        if (glad_glCreateBuffers) {
            glCreateBuffers(1, &m_RendererID);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(u32), indices, GL_STATIC_DRAW);
        }
    }

    IndexBuffer::~IndexBuffer() {
        if (glad_glDeleteBuffers && m_RendererID) {
            glDeleteBuffers(1, &m_RendererID);
        }
    }

    void IndexBuffer::Bind() const {
        if (glad_glBindBuffer && m_RendererID) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        }
    }

    void IndexBuffer::Unbind() const {
        if (glad_glBindBuffer) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
    }

}