#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/Log.hpp>

#include <cstring>
#include <algorithm>
#include <iostream>
#include <Axle/RHI/RHIFaultInjection.hpp>

namespace Axle::RHI {

    // =========================================================================
    // RHIPipelineLayout Implementation
    // =========================================================================

    RHIPipelineLayout::~RHIPipelineLayout() {
        Shutdown();
    }

    RHIPipelineLayout::RHIPipelineLayout(RHIPipelineLayout&& other) noexcept
        : m_Layout(other.m_Layout)
        , m_RootRegisterSpace(other.m_RootRegisterSpace)
        , m_RootConstants(std::move(other.m_RootConstants))
        , m_DescriptorSets(std::move(other.m_DescriptorSets))
        , m_DescriptorRanges(std::move(other.m_DescriptorRanges))
    {
        other.m_Layout = nullptr;
        other.m_RootRegisterSpace = 0;
    }

    RHIPipelineLayout& RHIPipelineLayout::operator=(RHIPipelineLayout&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Layout = other.m_Layout;
            m_RootRegisterSpace = other.m_RootRegisterSpace;
            m_RootConstants = std::move(other.m_RootConstants);
            m_DescriptorSets = std::move(other.m_DescriptorSets);
            m_DescriptorRanges = std::move(other.m_DescriptorRanges);
            other.m_Layout = nullptr;
            other.m_RootRegisterSpace = 0;
        }
        return *this;
    }

    bool RHIPipelineLayout::Init(const nri::PipelineLayoutDesc& desc) {
        auto& device = RHIDevice::Get();
        if (!device.IsNativeReady()) return false;
        const auto& core = device.GetNRICore();
        const auto& limits = core.GetDeviceDesc(*device.GetNRIDevice()).pipelineLayout;
        if ((desc.rootConstantNum && !desc.rootConstants) || (desc.rootDescriptorNum && !desc.rootDescriptors) ||
            (desc.rootSamplerNum && !desc.rootSamplers) || (desc.descriptorSetNum && !desc.descriptorSets) ||
            desc.descriptorSetNum > limits.descriptorSetMaxNum || desc.rootDescriptorNum > limits.rootDescriptorMaxNum ||
            desc.rootSamplerNum > limits.rootSamplerMaxNum || desc.rootConstantNum > limits.rootConstantMaxSize / 4) return false;
        uint64_t bytes = 0;
        for (uint32_t i = 0; i < desc.rootConstantNum; ++i) {
            if (!desc.rootConstants[i].size || desc.rootConstants[i].size % 4) return false;
            bytes += desc.rootConstants[i].size;
        }
        if (bytes > limits.rootConstantMaxSize) return false;
        RHIPipelineLayout candidate;
        candidate.m_RootRegisterSpace = desc.rootRegisterSpace;
        if (desc.rootConstantNum) candidate.m_RootConstants.assign(desc.rootConstants, desc.rootConstants + desc.rootConstantNum);
        if (desc.descriptorSetNum) candidate.m_DescriptorSets.assign(desc.descriptorSets, desc.descriptorSets + desc.descriptorSetNum);
        candidate.m_DescriptorRanges.resize(desc.descriptorSetNum);
        for (uint32_t i = 0; i < desc.descriptorSetNum; ++i) {
            auto& set = candidate.m_DescriptorSets[i];
            if (set.rangeNum && !set.ranges) return false;
            if (set.rangeNum) candidate.m_DescriptorRanges[i].assign(set.ranges, set.ranges + set.rangeNum);
            set.ranges = candidate.m_DescriptorRanges[i].data();
        }
        const auto result = ScopedRHIFault::Invoke("CreatePipelineLayout", [&] { return core.CreatePipelineLayout(*device.GetNRIDevice(), desc, candidate.m_Layout); });
        if (result != nri::Result::SUCCESS || !candidate.m_Layout) return false;
        *this = std::move(candidate);
        return true;
    }

    bool RHIPipelineLayout::Init(const RHIAutoPipelineLayout& autoLayout) {
        nri::PipelineLayoutDesc desc = autoLayout.GetDesc();
        return Init(desc);
    }

    void RHIPipelineLayout::Shutdown() {
        if (m_Layout) {
            auto& rhiDevice = RHIDevice::Get();
            if (rhiDevice.IsNativeReady()) {
                rhiDevice.GetNRICore().DestroyPipelineLayout(m_Layout);
            }
            m_Layout = nullptr;
        }
        m_RootConstants.clear();
        m_DescriptorSets.clear();
        m_DescriptorRanges.clear();
        m_RootRegisterSpace = 0;
    }

    // =========================================================================
    // 64-bit Fast PSO Hashing Functions
    // =========================================================================

    uint64_t Hash64Bytes(const void* data, size_t size, uint64_t seed) {
        if (!data || size == 0) return seed;
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        uint64_t hash = seed;
        for (size_t i = 0; i < size; ++i) {
            hash ^= static_cast<uint64_t>(ptr[i]);
            hash *= 0x100000001b3ULL; // FNV-1a 64-bit prime
        }
        return hash;
    }

    void HashCombine64(uint64_t& seed, uint64_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    }

    uint64_t ComputeGraphicsPipelineHash(const RHIGraphicsPipelineDesc& desc) {
        uint64_t hash = 0xcbf29ce484222325ULL;

        // 1. Pipeline Layout
        uint64_t layoutPtr = desc.Layout ? reinterpret_cast<uint64_t>(desc.Layout->GetNative()) : 0ULL;
        HashCombine64(hash, layoutPtr);

        // 2. Shaders
        HashCombine64(hash, static_cast<uint64_t>(desc.Shaders.size()));
        for (const auto& shader : desc.Shaders) {
            HashCombine64(hash, static_cast<uint64_t>(shader.Stage));
            HashCombine64(hash, static_cast<uint64_t>(shader.Format));
            HashCombine64(hash, Hash64Bytes(shader.GetData(), shader.GetSize()));
            HashCombine64(hash, Hash64Bytes(shader.EntryPoint.data(), shader.EntryPoint.size()));
        }

        // 3. Rasterizer State
        HashCombine64(hash, static_cast<uint64_t>(desc.Rasterizer.FillMode));
        HashCombine64(hash, static_cast<uint64_t>(desc.Rasterizer.CullMode));
        HashCombine64(hash, desc.Rasterizer.FrontCounterClockwise ? 1ULL : 0ULL);
        HashCombine64(hash, desc.Rasterizer.DepthClamp ? 1ULL : 0ULL);
        HashCombine64(hash, Hash64Bytes(&desc.Rasterizer.DepthBias, sizeof(desc.Rasterizer.DepthBias)));
        HashCombine64(hash, desc.Rasterizer.LineSmoothing ? 1ULL : 0ULL);
        HashCombine64(hash, desc.Rasterizer.ConservativeRaster ? 1ULL : 0ULL);

        // 4. Depth-Stencil State
        HashCombine64(hash, desc.DepthStencil.DepthTestEnable ? 1ULL : 0ULL);
        HashCombine64(hash, desc.DepthStencil.DepthWriteEnable ? 1ULL : 0ULL);
        HashCombine64(hash, static_cast<uint64_t>(desc.DepthStencil.DepthCompareOp));
        HashCombine64(hash, desc.DepthStencil.DepthBoundsTest ? 1ULL : 0ULL);
        HashCombine64(hash, desc.DepthStencil.StencilEnable ? 1ULL : 0ULL);
        HashCombine64(hash, static_cast<uint64_t>(desc.DepthStencil.DepthStencilFormat));

        // 5. Input Assembly
        HashCombine64(hash, static_cast<uint64_t>(desc.InputAssembly.Topology));
        HashCombine64(hash, static_cast<uint64_t>(desc.InputAssembly.PrimitiveRestart));
        HashCombine64(hash, static_cast<uint64_t>(desc.InputAssembly.TessControlPointNum));

        // 6. Color Attachments & Blend States
        HashCombine64(hash, static_cast<uint64_t>(desc.ColorAttachments.size()));
        for (const auto& ca : desc.ColorAttachments) {
            HashCombine64(hash, ca.BlendEnabled ? 1ULL : 0ULL);
            HashCombine64(hash, static_cast<uint64_t>(ca.SrcColor));
            HashCombine64(hash, static_cast<uint64_t>(ca.DstColor));
            HashCombine64(hash, static_cast<uint64_t>(ca.ColorOp));
            HashCombine64(hash, static_cast<uint64_t>(ca.SrcAlpha));
            HashCombine64(hash, static_cast<uint64_t>(ca.DstAlpha));
            HashCombine64(hash, static_cast<uint64_t>(ca.AlphaOp));
            HashCombine64(hash, static_cast<uint64_t>(ca.ColorWriteMask));
        }

        HashCombine64(hash, static_cast<uint64_t>(desc.ColorAttachmentFormats.size()));
        for (const auto& fmt : desc.ColorAttachmentFormats) {
            HashCombine64(hash, static_cast<uint64_t>(fmt));
        }

        // 7. Multisample
        HashCombine64(hash, static_cast<uint64_t>(desc.SampleCount));

        // 8. Vertex Input Attributes & Streams
        HashCombine64(hash, static_cast<uint64_t>(desc.VertexInput.Attributes.size()));
        for (const auto& attr : desc.VertexInput.Attributes) {
            HashCombine64(hash, Hash64Bytes(attr.SemanticName.data(), attr.SemanticName.size()));
            HashCombine64(hash, static_cast<uint64_t>(attr.SemanticIndex));
            HashCombine64(hash, static_cast<uint64_t>(attr.Location));
            HashCombine64(hash, static_cast<uint64_t>(attr.Offset));
            HashCombine64(hash, static_cast<uint64_t>(attr.Format));
            HashCombine64(hash, static_cast<uint64_t>(attr.StreamIndex));
        }

        HashCombine64(hash, static_cast<uint64_t>(desc.VertexInput.Streams.size()));
        for (const auto& stream : desc.VertexInput.Streams) {
            HashCombine64(hash, static_cast<uint64_t>(stream.BindingSlot));
            HashCombine64(hash, static_cast<uint64_t>(stream.Stride));
            HashCombine64(hash, static_cast<uint64_t>(stream.StepRate));
        }

        return hash;
    }

    uint64_t ComputeComputePipelineHash(const RHIComputePipelineDesc& desc) {
        uint64_t hash = 0xcbf29ce484222325ULL;

        // 1. Pipeline Layout
        uint64_t layoutPtr = desc.Layout ? reinterpret_cast<uint64_t>(desc.Layout->GetNative()) : 0ULL;
        HashCombine64(hash, layoutPtr);

        // 2. Compute Shader
        HashCombine64(hash, static_cast<uint64_t>(desc.ComputeShader.Stage));
        HashCombine64(hash, static_cast<uint64_t>(desc.ComputeShader.Format));
        HashCombine64(hash, Hash64Bytes(desc.ComputeShader.GetData(), desc.ComputeShader.GetSize()));
        HashCombine64(hash, Hash64Bytes(desc.ComputeShader.EntryPoint.data(), desc.ComputeShader.EntryPoint.size()));

        return hash;
    }

    // =========================================================================
    // RHIPipelineCache Implementation (LRU)
    // =========================================================================

    RHIPipelineCache& RHIPipelineCache::Get() {
        static RHIPipelineCache s_Instance;
        return s_Instance;
    }

    RHIPipelineCache::RHIPipelineCache(size_t maxCapacity)
        : m_MaxCapacity(maxCapacity)
    {
    }

    RHIPipelineCache::~RHIPipelineCache() {
        Clear();
    }

    void RHIPipelineCache::SetMaxCapacity(size_t capacity) {
        m_MaxCapacity = capacity;
        auto& rhiDevice = RHIDevice::Get();
        const auto& nriCore = rhiDevice.GetNRICore();

        if (rhiDevice.IsNativeReady() && m_LRUList.size() > m_MaxCapacity) {
            rhiDevice.WaitIdle();
        }

        while (m_LRUList.size() > m_MaxCapacity) {
            const auto& victim = m_LRUList.back();
            m_Lookup.erase(victim.Hash);
            if (victim.Pipeline && rhiDevice.IsNativeReady()) {
                nriCore.DestroyPipeline(victim.Pipeline);
            }
            m_LRUList.pop_back();
        }
    }

    bool RHIPipelineCache::Contains(uint64_t hash) const {
        return m_Lookup.find(hash) != m_Lookup.end();
    }

    void RHIPipelineCache::Clear() {
        auto& rhiDevice = RHIDevice::Get();
        const auto& nriCore = rhiDevice.GetNRICore();

        for (auto& entry : m_LRUList) {
            if (entry.Pipeline && rhiDevice.IsNativeReady()) {
                nriCore.DestroyPipeline(entry.Pipeline);
            }
        }
        m_LRUList.clear();
        m_Lookup.clear();
    }

    nri::Pipeline* RHIPipelineCache::GetOrCreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
        if (!desc.Layout || !desc.Layout->IsValid() || desc.Shaders.empty() || desc.Shaders.size() > UINT32_MAX ||
            desc.VertexInput.Attributes.size() > UINT8_MAX || desc.VertexInput.Streams.size() > UINT8_MAX ||
            desc.ColorAttachmentFormats.size() > 8 || desc.ColorAttachments.size() > 8 ||
            (!desc.ColorAttachments.empty() && desc.ColorAttachments.size() != desc.ColorAttachmentFormats.size()) ||
            (desc.SampleCount != 1 && desc.SampleCount != 2 && desc.SampleCount != 4 && desc.SampleCount != 8) || !m_MaxCapacity) return nullptr;
        for (const auto& shader : desc.Shaders) if (!shader.GetData() || !shader.GetSize()) return nullptr;
        uint64_t hash = ComputeGraphicsPipelineHash(desc);

        // 1. Check LRU cache
        auto it = m_Lookup.find(hash);
        if (it != m_Lookup.end()) {
            m_HitCount++;
            // Splice to front of LRU list (mark most recently used)
            m_LRUList.splice(m_LRUList.begin(), m_LRUList, it->second);
            return it->second->Pipeline;
        }

        // 2. Cache miss -> Compile on GPU
        m_MissCount++;

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsNativeReady()) {
            AXLE_CORE_ERROR("RHIPipelineCache::GetOrCreateGraphicsPipeline failed: RHIDevice is not initialized!");
            return nullptr;
        }
        auto* nativeDevice = rhiDevice.GetNRIDevice();
        const auto& nriCore = rhiDevice.GetNRICore();
        if (!nativeDevice) return nullptr;

        const auto& limits = nriCore.GetDeviceDesc(*nativeDevice);
        if (desc.VertexInput.Attributes.size() > limits.shaderStage.vertex.attributeMaxNum ||
            desc.VertexInput.Streams.size() > limits.shaderStage.vertex.streamMaxNum ||
            desc.ColorAttachmentFormats.size() > limits.shaderStage.fragment.attachmentMaxNum) return nullptr;
        auto supports = [&](nri::Format format, nri::FormatSupportBits usage) {
            if (format <= nri::Format::UNKNOWN || format >= nri::Format::MAX_NUM) return false;
            if (desc.SampleCount == 2) usage |= nri::FormatSupportBits::MULTISAMPLE_2X;
            if (desc.SampleCount == 4) usage |= nri::FormatSupportBits::MULTISAMPLE_4X;
            if (desc.SampleCount == 8) usage |= nri::FormatSupportBits::MULTISAMPLE_8X;
            return (static_cast<uint32_t>(rhiDevice.GetTextureFormatSupport(format)) & static_cast<uint32_t>(usage)) == static_cast<uint32_t>(usage);
        };
        for (size_t i = 0; i < desc.ColorAttachmentFormats.size(); ++i) {
            auto usage = nri::FormatSupportBits::COLOR_ATTACHMENT;
            if (i < desc.ColorAttachments.size() && desc.ColorAttachments[i].BlendEnabled) usage |= nri::FormatSupportBits::BLEND;
            if (!supports(desc.ColorAttachmentFormats[i], usage)) return nullptr;
        }
        if ((desc.DepthStencil.DepthTestEnable || desc.DepthStencil.DepthWriteEnable || desc.DepthStencil.StencilEnable) &&
            !supports(desc.DepthStencil.DepthStencilFormat, nri::FormatSupportBits::DEPTH_STENCIL_ATTACHMENT)) return nullptr;
        for (const auto& attribute : desc.VertexInput.Attributes) {
            if (attribute.StreamIndex >= desc.VertexInput.Streams.size() || attribute.Format <= nri::Format::UNKNOWN ||
                attribute.Format >= nri::Format::MAX_NUM || !(nriCore.GetFormatSupport(*nativeDevice, attribute.Format) & nri::FormatSupportBits::VERTEX_BUFFER)) return nullptr;
        }

        // Convert Shaders
        std::vector<nri::ShaderDesc> nriShaders;
        nriShaders.reserve(desc.Shaders.size());
        for (const auto& s : desc.Shaders) {
            nriShaders.push_back(s.ToNRIShaderDesc());
        }

        // Setup Rasterization
        nri::RasterizationDesc raster = {};
        raster.fillMode = desc.Rasterizer.FillMode;
        raster.cullMode = desc.Rasterizer.CullMode;
        raster.frontCounterClockwise = desc.Rasterizer.FrontCounterClockwise;
        raster.depthClamp = desc.Rasterizer.DepthClamp;
        raster.depthBias = desc.Rasterizer.DepthBias;
        raster.lineSmoothing = desc.Rasterizer.LineSmoothing;
        raster.conservativeRaster = desc.Rasterizer.ConservativeRaster;

        // Setup Input Assembly
        nri::InputAssemblyDesc ia = {};
        ia.topology = desc.InputAssembly.Topology;
        ia.primitiveRestart = desc.InputAssembly.PrimitiveRestart;
        ia.tessControlPointNum = desc.InputAssembly.TessControlPointNum;

        // Setup Output Merger
        nri::OutputMergerDesc om = {};
        om.depth.compareOp = desc.DepthStencil.DepthTestEnable ? desc.DepthStencil.DepthCompareOp : nri::CompareOp::NONE;
        om.depth.write = desc.DepthStencil.DepthWriteEnable;
        om.depth.boundsTest = desc.DepthStencil.DepthBoundsTest;
        om.depthStencilFormat = (desc.DepthStencil.DepthTestEnable || desc.DepthStencil.DepthWriteEnable || desc.DepthStencil.StencilEnable)
            ? desc.DepthStencil.DepthStencilFormat : nri::Format::UNKNOWN;

        std::vector<nri::ColorAttachmentDesc> nriColors;
        size_t colorCount = desc.ColorAttachments.size();
        if (colorCount == 0 && !desc.ColorAttachmentFormats.empty()) {
            colorCount = desc.ColorAttachmentFormats.size();
        }

        nriColors.reserve(colorCount);
        for (size_t i = 0; i < colorCount; ++i) {
            nri::ColorAttachmentDesc cad = {};
            cad.format = (i < desc.ColorAttachmentFormats.size()) ? desc.ColorAttachmentFormats[i] : nri::Format::RGBA8_UNORM;
            if (i < desc.ColorAttachments.size()) {
                const auto& ca = desc.ColorAttachments[i];
                cad.blendEnabled = ca.BlendEnabled;
                cad.colorBlend = { 
                    ca.BlendEnabled ? ca.SrcColor : nri::BlendFactor::ONE, 
                    ca.BlendEnabled ? ca.DstColor : nri::BlendFactor::ZERO, 
                    ca.BlendEnabled ? ca.ColorOp : nri::BlendOp::ADD 
                };
                cad.alphaBlend = { 
                    ca.BlendEnabled ? ca.SrcAlpha : nri::BlendFactor::ONE, 
                    ca.BlendEnabled ? ca.DstAlpha : nri::BlendFactor::ZERO, 
                    ca.BlendEnabled ? ca.AlphaOp : nri::BlendOp::ADD 
                };
                cad.colorWriteMask = ca.ColorWriteMask;
            } else {
                cad.blendEnabled = false;
                cad.colorBlend = { nri::BlendFactor::ONE, nri::BlendFactor::ZERO, nri::BlendOp::ADD };
                cad.alphaBlend = { nri::BlendFactor::ONE, nri::BlendFactor::ZERO, nri::BlendOp::ADD };
                cad.colorWriteMask = nri::ColorWriteBits::RGBA;
            }
            nriColors.push_back(cad);
        }
        om.colors = nriColors.empty() ? nullptr : nriColors.data();
        om.colorNum = static_cast<uint32_t>(nriColors.size());

        // Setup Vertex Input
        nri::VertexInputDesc vi = {};
        std::vector<nri::VertexAttributeDesc> nriAttrs;
        std::vector<nri::VertexStreamDesc> nriStreams;

        if (!desc.VertexInput.Attributes.empty() || !desc.VertexInput.Streams.empty()) {
            nriAttrs.reserve(desc.VertexInput.Attributes.size());
            for (const auto& attr : desc.VertexInput.Attributes) {
                nri::VertexAttributeDesc vad = {};
                vad.d3d.semanticName = attr.SemanticName.c_str();
                vad.d3d.semanticIndex = attr.SemanticIndex;
                vad.vk.location = attr.Location;
                vad.offset = attr.Offset;
                vad.format = attr.Format;
                vad.streamIndex = attr.StreamIndex;
                nriAttrs.push_back(vad);
            }
            vi.attributes = nriAttrs.data();
            vi.attributeNum = static_cast<uint8_t>(nriAttrs.size());

            nriStreams.reserve(desc.VertexInput.Streams.size());
            for (const auto& stream : desc.VertexInput.Streams) {
                nri::VertexStreamDesc vsd = {};
                vsd.bindingSlot = stream.BindingSlot;
                vsd.stride = stream.Stride;
                vsd.stepRate = stream.StepRate;
                nriStreams.push_back(vsd);
            }
            vi.streams = nriStreams.data();
            vi.streamNum = static_cast<uint8_t>(nriStreams.size());
        }

        nri::MultisampleDesc ms = {};
        ms.sampleNum = (desc.SampleCount > 0) ? desc.SampleCount : 1;
        ms.sampleMask = nri::ALL;

        // Setup GraphicsPipelineDesc
        nri::GraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.pipelineLayout = desc.Layout ? desc.Layout->GetNative() : nullptr;
        pipelineDesc.shaders = nriShaders.data();
        pipelineDesc.shaderNum = static_cast<uint32_t>(nriShaders.size());
        pipelineDesc.rasterization = raster;
        pipelineDesc.inputAssembly = ia;
        pipelineDesc.outputMerger = om;
        pipelineDesc.multisample = &ms;
        if (!nriAttrs.empty() || !nriStreams.empty()) {
            pipelineDesc.vertexInput = &vi;
        }

        nri::Pipeline* newPipeline = nullptr;
        nri::Result res = ScopedRHIFault::Invoke("CreateGraphicsPipeline", [&] { return nriCore.CreateGraphicsPipeline(*nativeDevice, pipelineDesc, newPipeline); });
        if (res != nri::Result::SUCCESS || !newPipeline) {
            if (newPipeline) nriCore.DestroyPipeline(newPipeline);
            AXLE_CORE_ERROR("RHIPipelineCache: CreateGraphicsPipeline failed with code {0}!", static_cast<uint32_t>(res));
            return nullptr;
        }

        // 3. Evict least-recently-used entry if cache is full
        if (m_LRUList.size() >= m_MaxCapacity) {
            const auto& victim = m_LRUList.back();
            m_Lookup.erase(victim.Hash);
            if (victim.Pipeline) {
                if (rhiDevice.IsNativeReady()) {
                    rhiDevice.WaitIdle();
                }
                nriCore.DestroyPipeline(victim.Pipeline);
            }
            m_LRUList.pop_back();
        }

        // 4. Insert into LRU cache
        PipelineEntry entry;
        entry.Hash = hash;
        entry.Pipeline = newPipeline;
        entry.IsCompute = false;

        m_LRUList.push_front(entry);
        m_Lookup[hash] = m_LRUList.begin();

        AXLE_CORE_INFO("RHIPipelineCache: Created and cached Graphics Pipeline [Hash: 0x{0:x}] (Total cached: {1})",
            hash, m_LRUList.size());
        return newPipeline;
    }

    nri::Pipeline* RHIPipelineCache::GetOrCreateComputePipeline(const RHIComputePipelineDesc& desc) {
        if (!desc.Layout || !desc.Layout->IsValid() || !desc.ComputeShader.GetData() || !desc.ComputeShader.GetSize() || !m_MaxCapacity) return nullptr;
        uint64_t hash = ComputeComputePipelineHash(desc);

        // 1. Check LRU cache
        auto it = m_Lookup.find(hash);
        if (it != m_Lookup.end()) {
            m_HitCount++;
            m_LRUList.splice(m_LRUList.begin(), m_LRUList, it->second);
            return it->second->Pipeline;
        }

        // 2. Cache miss -> Compile on GPU
        m_MissCount++;

        auto& rhiDevice = RHIDevice::Get();
        if (!rhiDevice.IsNativeReady()) {
            AXLE_CORE_ERROR("RHIPipelineCache::GetOrCreateComputePipeline failed: RHIDevice is not initialized!");
            return nullptr;
        }
        auto* nativeDevice = rhiDevice.GetNRIDevice();
        const auto& nriCore = rhiDevice.GetNRICore();
        if (!nativeDevice) return nullptr;

        nri::ComputePipelineDesc compDesc = {};
        compDesc.pipelineLayout = desc.Layout ? desc.Layout->GetNative() : nullptr;
        compDesc.shader = desc.ComputeShader.ToNRIShaderDesc();

        nri::Pipeline* newPipeline = nullptr;
        nri::Result res = ScopedRHIFault::Invoke("CreateComputePipeline", [&] { return nriCore.CreateComputePipeline(*nativeDevice, compDesc, newPipeline); });
        if (res != nri::Result::SUCCESS || !newPipeline) {
            if (newPipeline) nriCore.DestroyPipeline(newPipeline);
            AXLE_CORE_ERROR("RHIPipelineCache: CreateComputePipeline failed with code {0}!", static_cast<uint32_t>(res));
            return nullptr;
        }

        // 3. Evict least-recently-used entry if cache is full
        if (m_LRUList.size() >= m_MaxCapacity) {
            const auto& victim = m_LRUList.back();
            m_Lookup.erase(victim.Hash);
            if (victim.Pipeline) {
                if (rhiDevice.IsNativeReady()) {
                    rhiDevice.WaitIdle();
                }
                nriCore.DestroyPipeline(victim.Pipeline);
            }
            m_LRUList.pop_back();
        }

        // 4. Insert into LRU cache
        PipelineEntry entry;
        entry.Hash = hash;
        entry.Pipeline = newPipeline;
        entry.IsCompute = true;

        m_LRUList.push_front(entry);
        m_Lookup[hash] = m_LRUList.begin();

        AXLE_CORE_INFO("RHIPipelineCache: Created and cached Compute Pipeline [Hash: 0x{0:x}] (Total cached: {1})",
            hash, m_LRUList.size());
        return newPipeline;
    }

}
