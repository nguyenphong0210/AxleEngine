#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIShader.hpp>

#include <NRI.h>

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <list>
#include <cstdint>

namespace Axle::RHI {

    // =========================================================================
    // RHIPipelineLayout Abstraction
    // =========================================================================
    class RHIPipelineLayout {
    public:
        RHIPipelineLayout() = default;
        ~RHIPipelineLayout();

        RHIPipelineLayout(const RHIPipelineLayout&) = delete;
        RHIPipelineLayout& operator=(const RHIPipelineLayout&) = delete;

        RHIPipelineLayout(RHIPipelineLayout&& other) noexcept;
        RHIPipelineLayout& operator=(RHIPipelineLayout&& other) noexcept;

        // Initialize layout directly from native NRI description
        bool Init(const nri::PipelineLayoutDesc& desc);

        // Initialize layout from reflection auto-generated layout
        bool Init(const RHIAutoPipelineLayout& autoLayout);

        // Release layout resource from GPU
        void Shutdown();

        bool IsValid() const { return m_Layout != nullptr; }
        nri::PipelineLayout* GetNative() const { return m_Layout; }
        uint32_t GetRootRegisterSpace() const { return m_RootRegisterSpace; }

        const std::vector<nri::RootConstantDesc>& GetRootConstants() const { return m_RootConstants; }
        const std::vector<nri::DescriptorSetDesc>& GetDescriptorSets() const { return m_DescriptorSets; }

    private:
        nri::PipelineLayout* m_Layout = nullptr;
        uint32_t m_RootRegisterSpace = 0;
        std::vector<nri::RootConstantDesc> m_RootConstants;
        std::vector<nri::DescriptorSetDesc> m_DescriptorSets;
    };

    // =========================================================================
    // Pipeline State Object (PSO) Descriptions
    // =========================================================================

    struct RHIRasterizerDesc {
        nri::FillMode FillMode = nri::FillMode::SOLID;
        nri::CullMode CullMode = nri::CullMode::BACK;
        bool FrontCounterClockwise = false;
        bool DepthClamp = false;
        nri::DepthBiasDesc DepthBias = { 0.0f, 0.0f, 0.0f };
        bool LineSmoothing = false;
        bool ConservativeRaster = false;
    };

    struct RHIDepthStencilDesc {
        bool DepthTestEnable = true;
        bool DepthWriteEnable = true;
        nri::CompareOp DepthCompareOp = nri::CompareOp::LESS;
        bool DepthBoundsTest = false;
        bool StencilEnable = false;
        nri::Format DepthStencilFormat = nri::Format::D32_SFLOAT;
    };

    struct RHIBlendAttachmentDesc {
        bool BlendEnabled = false;
        nri::BlendFactor SrcColor = nri::BlendFactor::ONE;
        nri::BlendFactor DstColor = nri::BlendFactor::ZERO;
        nri::BlendOp ColorOp = nri::BlendOp::ADD;
        nri::BlendFactor SrcAlpha = nri::BlendFactor::ONE;
        nri::BlendFactor DstAlpha = nri::BlendFactor::ZERO;
        nri::BlendOp AlphaOp = nri::BlendOp::ADD;
        nri::ColorWriteBits ColorWriteMask = nri::ColorWriteBits::RGBA;

        static RHIBlendAttachmentDesc Opaque() {
            RHIBlendAttachmentDesc desc;
            desc.BlendEnabled = false;
            return desc;
        }

        static RHIBlendAttachmentDesc AlphaBlend() {
            RHIBlendAttachmentDesc desc;
            desc.BlendEnabled = true;
            desc.SrcColor = nri::BlendFactor::SRC_ALPHA;
            desc.DstColor = nri::BlendFactor::ONE_MINUS_SRC_ALPHA;
            desc.ColorOp = nri::BlendOp::ADD;
            desc.SrcAlpha = nri::BlendFactor::ONE;
            desc.DstAlpha = nri::BlendFactor::ONE_MINUS_SRC_ALPHA;
            desc.AlphaOp = nri::BlendOp::ADD;
            return desc;
        }

        static RHIBlendAttachmentDesc Additive() {
            RHIBlendAttachmentDesc desc;
            desc.BlendEnabled = true;
            desc.SrcColor = nri::BlendFactor::SRC_ALPHA;
            desc.DstColor = nri::BlendFactor::ONE;
            desc.ColorOp = nri::BlendOp::ADD;
            desc.SrcAlpha = nri::BlendFactor::ONE;
            desc.DstAlpha = nri::BlendFactor::ONE;
            desc.AlphaOp = nri::BlendOp::ADD;
            return desc;
        }

        static RHIBlendAttachmentDesc PremultipliedAlpha() {
            RHIBlendAttachmentDesc desc;
            desc.BlendEnabled = true;
            desc.SrcColor = nri::BlendFactor::ONE;
            desc.DstColor = nri::BlendFactor::ONE_MINUS_SRC_ALPHA;
            desc.ColorOp = nri::BlendOp::ADD;
            desc.SrcAlpha = nri::BlendFactor::ONE;
            desc.DstAlpha = nri::BlendFactor::ONE_MINUS_SRC_ALPHA;
            desc.AlphaOp = nri::BlendOp::ADD;
            return desc;
        }
    };

    struct RHIInputAssemblyDesc {
        nri::Topology Topology = nri::Topology::TRIANGLE_LIST;
        nri::PrimitiveRestart PrimitiveRestart = nri::PrimitiveRestart::DISABLED;
        uint8_t TessControlPointNum = 0;
    };

    struct RHIVertexLayoutAttribute {
        std::string SemanticName;
        uint32_t SemanticIndex = 0;
        uint32_t Location = 0;
        uint32_t Offset = 0;
        nri::Format Format = nri::Format::RGBA32_SFLOAT;
        uint16_t StreamIndex = 0;
    };

    struct RHIVertexStream {
        uint16_t BindingSlot = 0;
        uint16_t Stride = 0;
        nri::VertexStreamStepRate StepRate = nri::VertexStreamStepRate::PER_VERTEX;
    };

    struct RHIVertexInputDesc {
        std::vector<RHIVertexLayoutAttribute> Attributes;
        std::vector<RHIVertexStream> Streams;
    };

    struct RHIGraphicsPipelineDesc {
        const RHIPipelineLayout* Layout = nullptr;
        std::vector<RHIShaderBytecode> Shaders;
        RHIRasterizerDesc Rasterizer;
        RHIDepthStencilDesc DepthStencil;
        RHIInputAssemblyDesc InputAssembly;
        std::vector<RHIBlendAttachmentDesc> ColorAttachments;
        std::vector<nri::Format> ColorAttachmentFormats;
        uint8_t SampleCount = 1;
        RHIVertexInputDesc VertexInput;
    };

    struct RHIComputePipelineDesc {
        const RHIPipelineLayout* Layout = nullptr;
        RHIShaderBytecode ComputeShader;
    };

    // =========================================================================
    // 64-bit Fast PSO Hashing Functions
    // =========================================================================
    uint64_t Hash64Bytes(const void* data, size_t size, uint64_t seed = 0xcbf29ce484222325ULL);
    void HashCombine64(uint64_t& seed, uint64_t value);

    uint64_t ComputeGraphicsPipelineHash(const RHIGraphicsPipelineDesc& desc);
    uint64_t ComputeComputePipelineHash(const RHIComputePipelineDesc& desc);

    // =========================================================================
    // LRU Pipeline Memory Cache (RHIPipelineCache)
    // =========================================================================
    class RHIPipelineCache {
    public:
        static RHIPipelineCache& Get();

        explicit RHIPipelineCache(size_t maxCapacity = 256);
        ~RHIPipelineCache();

        RHIPipelineCache(const RHIPipelineCache&) = delete;
        RHIPipelineCache& operator=(const RHIPipelineCache&) = delete;

        // Query or compile a Graphics Pipeline State Object
        nri::Pipeline* GetOrCreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc);

        // Query or compile a Compute Pipeline State Object
        nri::Pipeline* GetOrCreateComputePipeline(const RHIComputePipelineDesc& desc);

        // Check if a pipeline with the given 64-bit hash is currently cached
        bool Contains(uint64_t hash) const;

        // Current number of active cached pipelines in memory
        size_t GetCacheSize() const { return m_LRUList.size(); }

        // Maximum allowed capacity before least-recently-used eviction begins
        size_t GetMaxCapacity() const { return m_MaxCapacity; }
        void SetMaxCapacity(size_t capacity);

        // Cache statistics
        uint64_t GetHitCount() const { return m_HitCount; }
        uint64_t GetMissCount() const { return m_MissCount; }
        float GetHitRate() const {
            uint64_t total = m_HitCount + m_MissCount;
            return total > 0 ? static_cast<float>(m_HitCount) / static_cast<float>(total) : 0.0f;
        }
        void ResetStats() {
            m_HitCount = 0;
            m_MissCount = 0;
        }

        // Release all cached pipelines from GPU
        void Clear();

    private:
        struct PipelineEntry {
            uint64_t Hash = 0;
            nri::Pipeline* Pipeline = nullptr;
            bool IsCompute = false;
        };

        size_t m_MaxCapacity = 256;
        uint64_t m_HitCount = 0;
        uint64_t m_MissCount = 0;

        std::list<PipelineEntry> m_LRUList;
        std::unordered_map<uint64_t, std::list<PipelineEntry>::iterator> m_Lookup;
    };

}
