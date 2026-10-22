#include <Axle/RHI/RHIBarrier.hpp>
#include <Axle/Log.hpp>

namespace Axle::RHI {

    nri::Layout ToNRILayout(RHIResourceLayout layout) {
        switch (layout) {
            case RHIResourceLayout::Undefined:                      return nri::Layout::UNDEFINED;
            case RHIResourceLayout::General:                        return nri::Layout::GENERAL;
            case RHIResourceLayout::Present:                        return nri::Layout::PRESENT;
            case RHIResourceLayout::ColorAttachment:                return nri::Layout::COLOR_ATTACHMENT;
            case RHIResourceLayout::DepthStencilWrite:              return nri::Layout::DEPTH_STENCIL_ATTACHMENT;
            case RHIResourceLayout::DepthStencilRead:               return nri::Layout::DEPTH_STENCIL_READONLY;
            case RHIResourceLayout::DepthReadonlyStencilAttachment: return nri::Layout::DEPTH_READONLY_STENCIL_ATTACHMENT;
            case RHIResourceLayout::DepthAttachmentStencilReadonly: return nri::Layout::DEPTH_ATTACHMENT_STENCIL_READONLY;
            case RHIResourceLayout::ShaderResource:                 return nri::Layout::SHADER_RESOURCE;
            case RHIResourceLayout::ShaderResourceStorage:          return nri::Layout::SHADER_RESOURCE_STORAGE;
            case RHIResourceLayout::CopySource:                     return nri::Layout::COPY_SOURCE;
            case RHIResourceLayout::CopyDest:                       return nri::Layout::COPY_DESTINATION;
            case RHIResourceLayout::ResolveSource:                  return nri::Layout::RESOLVE_SOURCE;
            case RHIResourceLayout::ResolveDest:                    return nri::Layout::RESOLVE_DESTINATION;
            default:                                                return nri::Layout::UNDEFINED;
        }
    }

    nri::AccessBits ToNRIAccessBits(RHIAccess access) {
        uint32_t bits = 0;
        if ((access & RHIAccess::IndexBuffer) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::INDEX_BUFFER);
        if ((access & RHIAccess::VertexBuffer) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::VERTEX_BUFFER);
        if ((access & RHIAccess::ConstantBuffer) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::CONSTANT_BUFFER);
        if ((access & RHIAccess::ArgumentBuffer) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::ARGUMENT_BUFFER);
        if ((access & RHIAccess::ColorAttachmentRead) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::COLOR_ATTACHMENT_READ);
        if ((access & RHIAccess::ColorAttachmentWrite) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::COLOR_ATTACHMENT_WRITE);
        if ((access & RHIAccess::DepthStencilAttachmentRead) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::DEPTH_STENCIL_ATTACHMENT_READ);
        if ((access & RHIAccess::DepthStencilAttachmentWrite) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::DEPTH_STENCIL_ATTACHMENT_WRITE);
        if ((access & RHIAccess::ShaderResourceRead) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::SHADER_RESOURCE);
        if ((access & RHIAccess::ShaderResourceStorage) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::SHADER_RESOURCE_STORAGE);
        if ((access & RHIAccess::CopySource) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::COPY_SOURCE);
        if ((access & RHIAccess::CopyDest) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::COPY_DESTINATION);
        if ((access & RHIAccess::ResolveSource) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::RESOLVE_SOURCE);
        if ((access & RHIAccess::ResolveDest) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::RESOLVE_DESTINATION);
        if ((access & RHIAccess::HostRead) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::HOST_READ);
        if ((access & RHIAccess::HostWrite) != RHIAccess::None) bits |= static_cast<uint32_t>(nri::AccessBits::HOST_WRITE);
        return static_cast<nri::AccessBits>(bits);
    }

    nri::StageBits ToNRIStageBits(RHIPipelineStage stage) {
        if (stage == RHIPipelineStage::None) return nri::StageBits::NONE;
        if (stage == RHIPipelineStage::AllCommands) return nri::StageBits::ALL;

        uint32_t bits = 0;
        if ((stage & RHIPipelineStage::IndexInput) != RHIPipelineStage::None) bits |= static_cast<uint32_t>(nri::StageBits::INDEX_INPUT);
        if ((stage & RHIPipelineStage::VertexShader) != RHIPipelineStage::None) bits |= static_cast<uint32_t>(nri::StageBits::VERTEX_SHADER);
        if ((stage & RHIPipelineStage::FragmentShader) != RHIPipelineStage::None) bits |= static_cast<uint32_t>(nri::StageBits::FRAGMENT_SHADER);
        if ((stage & RHIPipelineStage::DepthStencilAttachment) != RHIPipelineStage::None) bits |= static_cast<uint32_t>(nri::StageBits::DEPTH_STENCIL_ATTACHMENT);
        if ((stage & RHIPipelineStage::ColorAttachment) != RHIPipelineStage::None) bits |= static_cast<uint32_t>(nri::StageBits::COLOR_ATTACHMENT);
        if ((stage & RHIPipelineStage::ComputeShader) != RHIPipelineStage::None) bits |= static_cast<uint32_t>(nri::StageBits::COMPUTE_SHADER);
        if ((stage & RHIPipelineStage::Copy) != RHIPipelineStage::None) bits |= static_cast<uint32_t>(nri::StageBits::COPY);
        if ((stage & RHIPipelineStage::Resolve) != RHIPipelineStage::None) bits |= static_cast<uint32_t>(nri::StageBits::RESOLVE);
        if ((stage & RHIPipelineStage::Host) != RHIPipelineStage::None) bits |= static_cast<uint32_t>(nri::StageBits::HOST);
        return static_cast<nri::StageBits>(bits);
    }

    RHIAccess GetDefaultAccessForLayout(RHIResourceLayout layout) {
        switch (layout) {
            case RHIResourceLayout::Undefined:
                return RHIAccess::None;
            case RHIResourceLayout::General:
                return RHIAccess::ShaderResourceRead | RHIAccess::ShaderResourceStorage;
            case RHIResourceLayout::Present:
                return RHIAccess::None;
            case RHIResourceLayout::ColorAttachment:
                return RHIAccess::ColorAttachment;
            case RHIResourceLayout::DepthStencilWrite:
                return RHIAccess::DepthStencilAttachment;
            case RHIResourceLayout::DepthStencilRead:
                return RHIAccess::DepthStencilAttachmentRead;
            case RHIResourceLayout::DepthReadonlyStencilAttachment:
                return RHIAccess::DepthStencilAttachmentRead | RHIAccess::DepthStencilAttachmentWrite;
            case RHIResourceLayout::DepthAttachmentStencilReadonly:
                return RHIAccess::DepthStencilAttachmentWrite | RHIAccess::DepthStencilAttachmentRead;
            case RHIResourceLayout::ShaderResource:
                return RHIAccess::ShaderResourceRead;
            case RHIResourceLayout::ShaderResourceStorage:
                return RHIAccess::ShaderResourceStorage;
            case RHIResourceLayout::CopySource:
                return RHIAccess::CopySource;
            case RHIResourceLayout::CopyDest:
                return RHIAccess::CopyDest;
            case RHIResourceLayout::ResolveSource:
                return RHIAccess::ResolveSource;
            case RHIResourceLayout::ResolveDest:
                return RHIAccess::ResolveDest;
            default:
                return RHIAccess::None;
        }
    }

    RHIPipelineStage GetDefaultStageForLayout(RHIResourceLayout layout) {
        switch (layout) {
            case RHIResourceLayout::Undefined:
                return RHIPipelineStage::None;
            case RHIResourceLayout::General:
                return RHIPipelineStage::AllShaders;
            case RHIResourceLayout::Present:
                return RHIPipelineStage::None;
            case RHIResourceLayout::ColorAttachment:
                return RHIPipelineStage::ColorAttachment;
            case RHIResourceLayout::DepthStencilWrite:
            case RHIResourceLayout::DepthStencilRead:
            case RHIResourceLayout::DepthReadonlyStencilAttachment:
            case RHIResourceLayout::DepthAttachmentStencilReadonly:
                return RHIPipelineStage::DepthStencilAttachment;
            case RHIResourceLayout::ShaderResource:
                return RHIPipelineStage::AllShaders;
            case RHIResourceLayout::ShaderResourceStorage:
                return RHIPipelineStage::ComputeShader | RHIPipelineStage::FragmentShader;
            case RHIResourceLayout::CopySource:
            case RHIResourceLayout::CopyDest:
                return RHIPipelineStage::Copy;
            case RHIResourceLayout::ResolveSource:
            case RHIResourceLayout::ResolveDest:
                return RHIPipelineStage::Resolve;
            default:
                return RHIPipelineStage::None;
        }
    }

    nri::TextureBarrierDesc RHITextureBarrier::ToNRIDesc() const {
        nri::TextureBarrierDesc desc = {};
        desc.texture = Texture;
        desc.before.layout = ToNRILayout(BeforeLayout);
        desc.before.access = ToNRIAccessBits(BeforeAccess);
        desc.before.stages = ToNRIStageBits(BeforeStage);

        desc.after.layout = ToNRILayout(AfterLayout);
        desc.after.access = ToNRIAccessBits(AfterAccess);
        desc.after.stages = ToNRIStageBits(AfterStage);

        desc.mipOffset = MipOffset;
        desc.mipNum = MipNum;
        desc.layerOffset = LayerOffset;
        desc.layerNum = LayerNum;
        desc.planes = Planes;
        return desc;
    }

    nri::BufferBarrierDesc RHIBufferBarrier::ToNRIDesc() const {
        nri::BufferBarrierDesc desc = {};
        desc.buffer = Buffer;
        desc.before.access = ToNRIAccessBits(BeforeAccess);
        desc.before.stages = ToNRIStageBits(BeforeStage);
        desc.after.access = ToNRIAccessBits(AfterAccess);
        desc.after.stages = ToNRIStageBits(AfterStage);
        return desc;
    }

    nri::GlobalBarrierDesc RHIGlobalBarrier::ToNRIDesc() const {
        nri::GlobalBarrierDesc desc = {};
        desc.before.access = ToNRIAccessBits(BeforeAccess);
        desc.before.stages = ToNRIStageBits(BeforeStage);
        desc.after.access = ToNRIAccessBits(AfterAccess);
        desc.after.stages = ToNRIStageBits(AfterStage);
        return desc;
    }

    // =========================================================================
    // RHIResourceStateTracker Implementation
    // =========================================================================

    void RHIResourceStateTracker::TrackTexture(nri::Texture* texture, RHIResourceLayout initialLayout) {
        if (!texture) return;
        m_TextureStates[texture] = initialLayout;
    }

    RHIResourceLayout RHIResourceStateTracker::GetTextureState(nri::Texture* texture) const {
        auto it = m_TextureStates.find(texture);
        if (it != m_TextureStates.end()) {
            return it->second;
        }
        return RHIResourceLayout::Undefined;
    }

    bool RHIResourceStateTracker::TransitionTexture(nri::Texture* texture, RHIResourceLayout targetLayout, RHIBarrierGroup& outBarriers) {
        if (!texture) return false;

        RHIResourceLayout currentLayout = RHIResourceLayout::Undefined;
        auto it = m_TextureStates.find(texture);
        if (it != m_TextureStates.end()) {
            currentLayout = it->second;
        }

        // Redundant transition elision: if already in target layout, do nothing!
        if (currentLayout == targetLayout && currentLayout != RHIResourceLayout::Undefined) {
            return false;
        }

        // Add barrier
        outBarriers.AddTexture(texture, currentLayout, targetLayout);
        m_TextureStates[texture] = targetLayout;
        return true;
    }

    void RHIResourceStateTracker::TrackBuffer(nri::Buffer* buffer, RHIAccess initialAccess, RHIPipelineStage initialStage) {
        if (!buffer) return;
        m_BufferStates[buffer] = { initialAccess, initialStage };
    }

    RHIAccess RHIResourceStateTracker::GetBufferAccess(nri::Buffer* buffer) const {
        auto it = m_BufferStates.find(buffer);
        if (it != m_BufferStates.end()) {
            return it->second.Access;
        }
        return RHIAccess::None;
    }

    RHIPipelineStage RHIResourceStateTracker::GetBufferStage(nri::Buffer* buffer) const {
        auto it = m_BufferStates.find(buffer);
        if (it != m_BufferStates.end()) {
            return it->second.Stage;
        }
        return RHIPipelineStage::None;
    }

    bool RHIResourceStateTracker::TransitionBuffer(nri::Buffer* buffer, RHIAccess targetAccess, RHIPipelineStage targetStage, RHIBarrierGroup& outBarriers) {
        if (!buffer) return false;

        RHIAccess currentAccess = RHIAccess::None;
        RHIPipelineStage currentStage = RHIPipelineStage::None;
        auto it = m_BufferStates.find(buffer);
        if (it != m_BufferStates.end()) {
            currentAccess = it->second.Access;
            currentStage = it->second.Stage;
        }

        // Redundant transition elision: if already in target access and target stage, do nothing!
        if (currentAccess == targetAccess && currentStage == targetStage && currentAccess != RHIAccess::None) {
            return false;
        }

        outBarriers.AddBuffer(buffer, currentAccess, targetAccess, currentStage, targetStage);
        m_BufferStates[buffer] = { targetAccess, targetStage };
        return true;
    }

    void RHIResourceStateTracker::Reset() {
        m_TextureStates.clear();
        m_BufferStates.clear();
    }

}
