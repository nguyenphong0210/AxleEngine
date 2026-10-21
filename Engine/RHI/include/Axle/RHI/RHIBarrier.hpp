#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <NRI.h>

#include <vector>
#include <unordered_map>
#include <cstdint>

#ifdef _WIN32
#undef min
#undef max
#endif

namespace Axle::RHI {

    // =========================================================================
    // Modern Cross-API Resource Layout & State Enums (Vulkan Sync2 / D3D12 Enhanced)
    // =========================================================================

    enum class RHIResourceLayout : uint8_t {
        Undefined = 0,
        General,
        Present,
        ColorAttachment,
        DepthStencilWrite,
        DepthStencilRead,
        DepthReadonlyStencilAttachment,
        DepthAttachmentStencilReadonly,
        ShaderResource,
        ShaderResourceStorage, // UAV / Read-Write
        CopySource,
        CopyDest,
        ResolveSource,
        ResolveDest
    };

    enum class RHIAccess : uint32_t {
        None                            = 0,
        IndexBuffer                     = 1 << 0,
        VertexBuffer                    = 1 << 1,
        ConstantBuffer                  = 1 << 2,
        ArgumentBuffer                  = 1 << 3,
        ColorAttachmentRead             = 1 << 4,
        ColorAttachmentWrite            = 1 << 5,
        ColorAttachment                 = ColorAttachmentRead | ColorAttachmentWrite,
        DepthStencilAttachmentRead      = 1 << 6,
        DepthStencilAttachmentWrite     = 1 << 7,
        DepthStencilAttachment          = DepthStencilAttachmentRead | DepthStencilAttachmentWrite,
        ShaderResourceRead              = 1 << 8,
        ShaderResourceStorage           = 1 << 9, // UAV Read/Write
        CopySource                      = 1 << 10,
        CopyDest                        = 1 << 11,
        ResolveSource                   = 1 << 12,
        ResolveDest                     = 1 << 13,
        HostRead                        = 1 << 14,
        HostWrite                       = 1 << 15
    };

    inline constexpr RHIAccess operator|(RHIAccess a, RHIAccess b) {
        return static_cast<RHIAccess>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline constexpr RHIAccess operator&(RHIAccess a, RHIAccess b) {
        return static_cast<RHIAccess>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    inline constexpr RHIAccess operator~(RHIAccess a) {
        return static_cast<RHIAccess>(~static_cast<uint32_t>(a));
    }
    inline RHIAccess& operator|=(RHIAccess& a, RHIAccess b) {
        a = a | b;
        return a;
    }
    inline RHIAccess& operator&=(RHIAccess& a, RHIAccess b) {
        a = a & b;
        return a;
    }

    enum class RHIPipelineStage : uint32_t {
        None                    = 0,
        IndexInput              = 1 << 0,
        VertexShader            = 1 << 1,
        FragmentShader          = 1 << 2,
        DepthStencilAttachment  = 1 << 3,
        ColorAttachment         = 1 << 4,
        ComputeShader           = 1 << 5,
        RayTracing              = 1 << 6,
        Copy                    = 1 << 7,
        Resolve                 = 1 << 8,
        Host                    = 1 << 9,
        AllGraphics             = IndexInput | VertexShader | FragmentShader | DepthStencilAttachment | ColorAttachment,
        AllShaders              = VertexShader | FragmentShader | ComputeShader | RayTracing,
        AllCommands             = 0x7FFFFFFF
    };

    inline constexpr RHIPipelineStage operator|(RHIPipelineStage a, RHIPipelineStage b) {
        return static_cast<RHIPipelineStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline constexpr RHIPipelineStage operator&(RHIPipelineStage a, RHIPipelineStage b) {
        return static_cast<RHIPipelineStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    inline constexpr RHIPipelineStage operator~(RHIPipelineStage a) {
        return static_cast<RHIPipelineStage>(~static_cast<uint32_t>(a));
    }
    inline RHIPipelineStage& operator|=(RHIPipelineStage& a, RHIPipelineStage b) {
        a = a | b;
        return a;
    }
    inline RHIPipelineStage& operator&=(RHIPipelineStage& a, RHIPipelineStage b) {
        a = a & b;
        return a;
    }

    // =========================================================================
    // NRI Native Mapping & Default Layout Deductions
    // =========================================================================

    nri::Layout ToNRILayout(RHIResourceLayout layout);
    nri::AccessBits ToNRIAccessBits(RHIAccess access);
    nri::StageBits ToNRIStageBits(RHIPipelineStage stage);

    RHIAccess GetDefaultAccessForLayout(RHIResourceLayout layout);
    RHIPipelineStage GetDefaultStageForLayout(RHIResourceLayout layout);

    // =========================================================================
    // Texture, Buffer, and Global Barriers
    // =========================================================================

    struct RHITextureBarrier {
        nri::Texture* Texture = nullptr;
        RHIResourceLayout BeforeLayout = RHIResourceLayout::Undefined;
        RHIResourceLayout AfterLayout = RHIResourceLayout::Undefined;
        RHIAccess BeforeAccess = RHIAccess::None;
        RHIAccess AfterAccess = RHIAccess::None;
        RHIPipelineStage BeforeStage = RHIPipelineStage::None;
        RHIPipelineStage AfterStage = RHIPipelineStage::None;
        uint16_t MipOffset = 0;
        uint16_t MipNum = nri::REMAINING;
        uint16_t LayerOffset = 0;
        uint16_t LayerNum = nri::REMAINING;
        nri::PlaneBits Planes = nri::PlaneBits::ALL;

        RHITextureBarrier() = default;

        // Auto-deduce access bits and pipeline stages from before/after layouts
        RHITextureBarrier(nri::Texture* texture, RHIResourceLayout before, RHIResourceLayout after)
            : Texture(texture)
            , BeforeLayout(before)
            , AfterLayout(after)
            , BeforeAccess(GetDefaultAccessForLayout(before))
            , AfterAccess(GetDefaultAccessForLayout(after))
            , BeforeStage(GetDefaultStageForLayout(before))
            , AfterStage(GetDefaultStageForLayout(after))
        {}

        // Fully explicit constructor
        RHITextureBarrier(nri::Texture* texture,
                          RHIResourceLayout before, RHIResourceLayout after,
                          RHIAccess beforeAccess, RHIAccess afterAccess,
                          RHIPipelineStage beforeStage, RHIPipelineStage afterStage,
                          uint16_t mipOffset = 0, uint16_t mipNum = nri::REMAINING,
                          uint16_t layerOffset = 0, uint16_t layerNum = nri::REMAINING,
                          nri::PlaneBits planes = nri::PlaneBits::ALL)
            : Texture(texture)
            , BeforeLayout(before)
            , AfterLayout(after)
            , BeforeAccess(beforeAccess)
            , AfterAccess(afterAccess)
            , BeforeStage(beforeStage)
            , AfterStage(afterStage)
            , MipOffset(mipOffset)
            , MipNum(mipNum)
            , LayerOffset(layerOffset)
            , LayerNum(layerNum)
            , Planes(planes)
        {}

        nri::TextureBarrierDesc ToNRIDesc() const;
    };

    struct RHIBufferBarrier {
        nri::Buffer* Buffer = nullptr;
        RHIAccess BeforeAccess = RHIAccess::None;
        RHIAccess AfterAccess = RHIAccess::None;
        RHIPipelineStage BeforeStage = RHIPipelineStage::None;
        RHIPipelineStage AfterStage = RHIPipelineStage::None;

        RHIBufferBarrier() = default;

        RHIBufferBarrier(nri::Buffer* buffer,
                         RHIAccess beforeAccess, RHIAccess afterAccess,
                         RHIPipelineStage beforeStage, RHIPipelineStage afterStage)
            : Buffer(buffer)
            , BeforeAccess(beforeAccess)
            , AfterAccess(afterAccess)
            , BeforeStage(beforeStage)
            , AfterStage(afterStage)
        {}

        nri::BufferBarrierDesc ToNRIDesc() const;
    };

    struct RHIGlobalBarrier {
        RHIAccess BeforeAccess = RHIAccess::None;
        RHIAccess AfterAccess = RHIAccess::None;
        RHIPipelineStage BeforeStage = RHIPipelineStage::None;
        RHIPipelineStage AfterStage = RHIPipelineStage::None;

        RHIGlobalBarrier() = default;

        RHIGlobalBarrier(RHIAccess beforeAccess, RHIAccess afterAccess,
                         RHIPipelineStage beforeStage, RHIPipelineStage afterStage)
            : BeforeAccess(beforeAccess)
            , AfterAccess(afterAccess)
            , BeforeStage(beforeStage)
            , AfterStage(afterStage)
        {}

        nri::GlobalBarrierDesc ToNRIDesc() const;
    };

    // =========================================================================
    // Barrier Group Builder
    // =========================================================================

    class RHIBarrierGroup {
    public:
        RHIBarrierGroup() = default;

        RHIBarrierGroup& AddTexture(const RHITextureBarrier& barrier) {
            m_Textures.push_back(barrier);
            return *this;
        }

        RHIBarrierGroup& AddTexture(nri::Texture* texture, RHIResourceLayout before, RHIResourceLayout after) {
            m_Textures.emplace_back(texture, before, after);
            return *this;
        }

        RHIBarrierGroup& AddBuffer(const RHIBufferBarrier& barrier) {
            m_Buffers.push_back(barrier);
            return *this;
        }

        RHIBarrierGroup& AddBuffer(nri::Buffer* buffer,
                                   RHIAccess beforeAccess, RHIAccess afterAccess,
                                   RHIPipelineStage beforeStage, RHIPipelineStage afterStage) {
            m_Buffers.emplace_back(buffer, beforeAccess, afterAccess, beforeStage, afterStage);
            return *this;
        }

        RHIBarrierGroup& AddGlobal(const RHIGlobalBarrier& barrier) {
            m_Globals.push_back(barrier);
            return *this;
        }

        RHIBarrierGroup& AddGlobal(RHIAccess beforeAccess, RHIAccess afterAccess,
                                   RHIPipelineStage beforeStage, RHIPipelineStage afterStage) {
            m_Globals.emplace_back(beforeAccess, afterAccess, beforeStage, afterStage);
            return *this;
        }

        void Clear() {
            m_Textures.clear();
            m_Buffers.clear();
            m_Globals.clear();
        }

        bool IsEmpty() const {
            return m_Textures.empty() && m_Buffers.empty() && m_Globals.empty();
        }

        size_t GetTextureCount() const { return m_Textures.size(); }
        size_t GetBufferCount() const { return m_Buffers.size(); }
        size_t GetGlobalCount() const { return m_Globals.size(); }

        const std::vector<RHITextureBarrier>& GetTextures() const { return m_Textures; }
        const std::vector<RHIBufferBarrier>& GetBuffers() const { return m_Buffers; }
        const std::vector<RHIGlobalBarrier>& GetGlobals() const { return m_Globals; }

    private:
        std::vector<RHITextureBarrier> m_Textures;
        std::vector<RHIBufferBarrier> m_Buffers;
        std::vector<RHIGlobalBarrier> m_Globals;
    };

    // =========================================================================
    // Explicit Resource State Machine & Redundant Barrier Elision Tracker
    // =========================================================================

    class RHIResourceStateTracker {
    public:
        RHIResourceStateTracker() = default;

        // Register initial known state of a texture
        void TrackTexture(nri::Texture* texture, RHIResourceLayout initialLayout = RHIResourceLayout::Undefined);

        // Query current tracked layout of a texture
        RHIResourceLayout GetTextureState(nri::Texture* texture) const;

        // Transition a texture to target layout:
        // Automatically elides redundant transitions if the texture is already in targetLayout.
        // Returns true if a barrier was generated and added to outBarriers, false if elided.
        bool TransitionTexture(nri::Texture* texture, RHIResourceLayout targetLayout, RHIBarrierGroup& outBarriers);

        // Reset all tracked states
        void Reset();

    private:
        std::unordered_map<nri::Texture*, RHIResourceLayout> m_TextureStates;
    };

}
