#include "RenderTestCommon.hpp"
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHISwitcher.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <Axle/RHI/RHIBarrier.hpp>
#include <Axle/RHI/RHISampler.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderGraph.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/Material.hpp>
#include <algorithm>

namespace Axle::RenderTests {

    static bool Test_RHITypeAndStringConversions() {
        // String to RHIType
        RENDER_CHECK(RHI::StringToRHIType("dx12") == RHI::RHIType::Direct3D12, "dx12 maps to Direct3D12");
        RENDER_CHECK(RHI::StringToRHIType("d3d12") == RHI::RHIType::Direct3D12, "d3d12 maps to Direct3D12");
        RENDER_CHECK(RHI::StringToRHIType("Direct3D12") == RHI::RHIType::Direct3D12, "Direct3D12 maps to Direct3D12");
        RENDER_CHECK(RHI::StringToRHIType("Direct3D 12") == RHI::RHIType::Direct3D12, "Direct3D 12 maps to Direct3D12");

        RENDER_CHECK(RHI::StringToRHIType("vk") == RHI::RHIType::Vulkan, "vk maps to Vulkan");
        RENDER_CHECK(RHI::StringToRHIType("vulkan") == RHI::RHIType::Vulkan, "vulkan maps to Vulkan");
        RENDER_CHECK(RHI::StringToRHIType("Vulkan") == RHI::RHIType::Vulkan, "Vulkan maps to Vulkan");
        RENDER_CHECK(RHI::StringToRHIType("Vulkan 1.3") == RHI::RHIType::Vulkan, "Vulkan 1.3 maps to Vulkan");

        RENDER_CHECK(RHI::StringToRHIType("gl") == RHI::RHIType::OpenGL, "gl maps to OpenGL");
        RENDER_CHECK(RHI::StringToRHIType("opengl") == RHI::RHIType::OpenGL, "opengl maps to OpenGL");
        RENDER_CHECK(RHI::StringToRHIType("OpenGL") == RHI::RHIType::OpenGL, "OpenGL maps to OpenGL");
        RENDER_CHECK(RHI::StringToRHIType("OpenGL 4.5") == RHI::RHIType::OpenGL, "OpenGL 4.5 maps to OpenGL");

        RENDER_CHECK(RHI::StringToRHIType("metal") == RHI::RHIType::None, "unsupported backend maps to None");
        RENDER_CHECK(RHI::StringToRHIType("unknown") == RHI::RHIType::None, "unknown backend maps to None");

        // RHIType to String
        RENDER_CHECK(std::string(RHI::RHITypeToString(RHI::RHIType::Direct3D12)) == "Direct3D 12", "Direct3D 12 string");
        RENDER_CHECK(std::string(RHI::RHITypeToString(RHI::RHIType::Vulkan)) == "Vulkan 1.3", "Vulkan 1.3 string");
        RENDER_CHECK(std::string(RHI::RHITypeToString(RHI::RHIType::OpenGL)) == "OpenGL 4.5", "OpenGL 4.5 string");
        RENDER_CHECK(std::string(RHI::RHITypeToString(RHI::RHIType::None)) == "None", "None string");

        // GraphicsAPI bidirectional mapping
        RENDER_CHECK(RHI::RHISwitcher::ToGraphicsAPI(RHI::RHIType::Direct3D12) == GraphicsAPI::Direct3D12, "RHIType to GraphicsAPI D3D12");
        RENDER_CHECK(RHI::RHISwitcher::ToGraphicsAPI(RHI::RHIType::Vulkan) == GraphicsAPI::Vulkan, "RHIType to GraphicsAPI Vulkan");
        RENDER_CHECK(RHI::RHISwitcher::ToGraphicsAPI(RHI::RHIType::OpenGL) == GraphicsAPI::OpenGL, "RHIType to GraphicsAPI OpenGL");
        RENDER_CHECK(RHI::RHISwitcher::ToGraphicsAPI(RHI::RHIType::None) == GraphicsAPI::None, "RHIType to GraphicsAPI None");

        RENDER_CHECK(RHI::RHISwitcher::ToRHIType(GraphicsAPI::Direct3D12) == RHI::RHIType::Direct3D12, "GraphicsAPI to RHIType D3D12");
        RENDER_CHECK(RHI::RHISwitcher::ToRHIType(GraphicsAPI::Vulkan) == RHI::RHIType::Vulkan, "GraphicsAPI to RHIType Vulkan");
        RENDER_CHECK(RHI::RHISwitcher::ToRHIType(GraphicsAPI::OpenGL) == RHI::RHIType::OpenGL, "GraphicsAPI to RHIType OpenGL");
        RENDER_CHECK(RHI::RHISwitcher::ToRHIType(GraphicsAPI::None) == RHI::RHIType::None, "GraphicsAPI to RHIType None");

        return true;
    }

    static bool Test_RHIVertexLayoutContracts() {
        // Compile-time static assertions are guaranteed, also verify runtime offsets and sizes
        RENDER_CHECK(sizeof(RHI::RHIVertex3D) == 64, "RHIVertex3D size must be exactly 64 bytes");
        RENDER_CHECK(sizeof(RHI::RHISkinnedVertex3D) == 96, "RHISkinnedVertex3D size must be exactly 96 bytes");

        RENDER_CHECK(offsetof(RHI::RHIVertex3D, Position) == 0, "RHIVertex3D Position offset 0");
        RENDER_CHECK(offsetof(RHI::RHIVertex3D, Normal) == 12, "RHIVertex3D Normal offset 12");
        RENDER_CHECK(offsetof(RHI::RHIVertex3D, Tangent) == 24, "RHIVertex3D Tangent offset 24");
        RENDER_CHECK(offsetof(RHI::RHIVertex3D, TexCoord) == 40, "RHIVertex3D TexCoord offset 40");
        RENDER_CHECK(offsetof(RHI::RHIVertex3D, Color) == 48, "RHIVertex3D Color offset 48");

        RENDER_CHECK(offsetof(RHI::RHISkinnedVertex3D, Position) == 0, "RHISkinnedVertex3D Position offset 0");
        RENDER_CHECK(offsetof(RHI::RHISkinnedVertex3D, Normal) == 12, "RHISkinnedVertex3D Normal offset 12");
        RENDER_CHECK(offsetof(RHI::RHISkinnedVertex3D, Tangent) == 24, "RHISkinnedVertex3D Tangent offset 24");
        RENDER_CHECK(offsetof(RHI::RHISkinnedVertex3D, TexCoord) == 40, "RHISkinnedVertex3D TexCoord offset 40");
        RENDER_CHECK(offsetof(RHI::RHISkinnedVertex3D, Color) == 48, "RHISkinnedVertex3D Color offset 48");
        RENDER_CHECK(offsetof(RHI::RHISkinnedVertex3D, JointIndices) == 64, "RHISkinnedVertex3D JointIndices offset 64");
        RENDER_CHECK(offsetof(RHI::RHISkinnedVertex3D, JointWeights) == 80, "RHISkinnedVertex3D JointWeights offset 80");

        // NRI Vertex Attributes verification for standard vertex
        auto stdAttribs = RHI::GetRHIVertex3DAttributes(0);
        RENDER_CHECK(stdAttribs.size() == 5, "RHIVertex3D has 5 attributes");
        RENDER_CHECK(stdAttribs[0].offset == 0 && stdAttribs[0].format == nri::Format::RGB32_SFLOAT, "Position attribute desc");
        RENDER_CHECK(stdAttribs[1].offset == 12 && stdAttribs[1].format == nri::Format::RGB32_SFLOAT, "Normal attribute desc");
        RENDER_CHECK(stdAttribs[2].offset == 24 && stdAttribs[2].format == nri::Format::RGBA32_SFLOAT, "Tangent attribute desc");
        RENDER_CHECK(stdAttribs[3].offset == 40 && stdAttribs[3].format == nri::Format::RG32_SFLOAT, "TexCoord attribute desc");
        RENDER_CHECK(stdAttribs[4].offset == 48 && stdAttribs[4].format == nri::Format::RGBA32_SFLOAT, "Color attribute desc");

        auto stdStream = RHI::GetRHIVertex3DStreamDesc(0);
        RENDER_CHECK(stdStream.stride == 64, "RHIVertex3D stream stride 64");
        RENDER_CHECK(stdStream.stepRate == nri::VertexStreamStepRate::PER_VERTEX, "RHIVertex3D step rate per vertex");

        // NRI Vertex Attributes verification for skinned vertex
        auto skinnedAttribs = RHI::GetRHISkinnedVertex3DAttributes(0);
        RENDER_CHECK(skinnedAttribs.size() == 7, "RHISkinnedVertex3D has 7 attributes");
        RENDER_CHECK(skinnedAttribs[5].offset == 64 && skinnedAttribs[5].format == nri::Format::RGBA32_UINT, "JointIndices attribute desc");
        RENDER_CHECK(skinnedAttribs[6].offset == 80 && skinnedAttribs[6].format == nri::Format::RGBA32_SFLOAT, "JointWeights attribute desc");

        auto skinnedStream = RHI::GetRHISkinnedVertex3DStreamDesc(0);
        RENDER_CHECK(skinnedStream.stride == 96, "RHISkinnedVertex3D stream stride 96");

        return true;
    }

    static bool Test_MikkTSpaceTangentsGeneration() {
        // Create a simple planar quad (2 triangles, CCW)
        std::vector<RHI::RHIVertex3D> vertices = {
            { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
            { {  1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
            { {  1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
            { { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } }
        };
        std::vector<uint32_t> indices = { 0, 1, 2, 2, 3, 0 };

        RHI::ComputeMeshTangents(vertices, indices);

        for (size_t i = 0; i < vertices.size(); ++i) {
            const auto& v = vertices[i];
            glm::vec3 tan(v.Tangent.x, v.Tangent.y, v.Tangent.z);
            float len = glm::length(tan);
            RENDER_CHECK(Near(len, 1.0f, 0.05f), "Tangent has unit length");

            // Tangent must be perpendicular to normal (+Z): dot(normal, tangent) == 0
            float dotNormal = glm::dot(v.Normal, tan);
            RENDER_CHECK(Near(dotNormal, 0.0f, 0.01f), "Tangent is orthogonal to Normal");

            // Handedness sign w must be +1.0 or -1.0
            RENDER_CHECK(std::abs(v.Tangent.w) == 1.0f, "Tangent handedness sign is valid");
        }

        return true;
    }

    static bool Test_RHIBarrierContracts() {
        // Bitwise flag operations
        auto access = RHI::RHIAccess::ColorAttachmentRead | RHI::RHIAccess::ColorAttachmentWrite;
        RENDER_CHECK(access == RHI::RHIAccess::ColorAttachment, "ColorAttachment compound flag");
        RENDER_CHECK((access & RHI::RHIAccess::ColorAttachmentRead) == RHI::RHIAccess::ColorAttachmentRead, "Access bitwise AND");
        RENDER_CHECK((access & RHI::RHIAccess::VertexBuffer) == RHI::RHIAccess::None, "Access bitwise non-member");

        // Conversion to NRI flags
        nri::AccessBits nriBits = RHI::ToNRIAccessBits(RHI::RHIAccess::ColorAttachmentWrite);
        uint32_t rawNriBits = static_cast<uint32_t>(nriBits);
        uint32_t rawExpected = static_cast<uint32_t>(nri::AccessBits::COLOR_ATTACHMENT_WRITE);
        RENDER_CHECK((rawNriBits & rawExpected) == rawExpected, "NRI access bits conversion");

        // Pipeline stage conversion
        nri::StageBits vsStage = RHI::ToNRIStageBits(RHI::RHIPipelineStage::VertexShader);
        RENDER_CHECK(vsStage == nri::StageBits::VERTEX_SHADER, "Vertex shader stage mapping");
        nri::StageBits psStage = RHI::ToNRIStageBits(RHI::RHIPipelineStage::FragmentShader);
        RENDER_CHECK(psStage == nri::StageBits::FRAGMENT_SHADER, "Fragment shader stage mapping");

        // Default layout access and stages
        RENDER_CHECK(RHI::GetDefaultAccessForLayout(RHI::RHIResourceLayout::ColorAttachment) == RHI::RHIAccess::ColorAttachment, "Default color access");
        RENDER_CHECK(RHI::GetDefaultStageForLayout(RHI::RHIResourceLayout::ColorAttachment) == RHI::RHIPipelineStage::ColorAttachment, "Default color stage");
        RENDER_CHECK(RHI::GetDefaultAccessForLayout(RHI::RHIResourceLayout::Undefined) == RHI::RHIAccess::None, "Undefined default access");

        return true;
    }

    static bool Test_RHISamplerContracts() {
        // Address mode conversion
        RENDER_CHECK(RHI::ToNRIAddressMode(RHI::RHIAddressMode::Repeat) == nri::AddressMode::REPEAT, "Repeat address mode");
        RENDER_CHECK(RHI::ToNRIAddressMode(RHI::RHIAddressMode::MirroredRepeat) == nri::AddressMode::MIRRORED_REPEAT, "MirroredRepeat address mode");
        RENDER_CHECK(RHI::ToNRIAddressMode(RHI::RHIAddressMode::ClampToEdge) == nri::AddressMode::CLAMP_TO_EDGE, "ClampToEdge address mode");
        RENDER_CHECK(RHI::ToNRIAddressMode(RHI::RHIAddressMode::ClampToBorder) == nri::AddressMode::CLAMP_TO_BORDER, "ClampToBorder address mode");

        // Filter conversion
        RENDER_CHECK(RHI::ToNRIFilter(RHI::RHIFilter::Nearest) == nri::Filter::NEAREST, "Nearest filter");
        RENDER_CHECK(RHI::ToNRIFilter(RHI::RHIFilter::Linear) == nri::Filter::LINEAR, "Linear filter");

        // FilterOp conversion
        RENDER_CHECK(RHI::ToNRIFilterOp(RHI::RHIFilterOp::Average) == nri::FilterOp::AVERAGE, "Average filter op");
        RENDER_CHECK(RHI::ToNRIFilterOp(RHI::RHIFilterOp::Minimum) == nri::FilterOp::MIN, "Minimum filter op");
        RENDER_CHECK(RHI::ToNRIFilterOp(RHI::RHIFilterOp::Maximum) == nri::FilterOp::MAX, "Maximum filter op");

        // CompareOp conversion
        RENDER_CHECK(RHI::ToNRICompareOp(RHI::RHICompareOp::None) == nri::CompareOp::NONE, "None compare op");
        RENDER_CHECK(RHI::ToNRICompareOp(RHI::RHICompareOp::Less) == nri::CompareOp::LESS, "Less compare op");
        RENDER_CHECK(RHI::ToNRICompareOp(RHI::RHICompareOp::GreaterOrEqual) == nri::CompareOp::GREATER_EQUAL, "GreaterOrEqual compare op");
        RENDER_CHECK(RHI::ToNRICompareOp(RHI::RHICompareOp::Always) == nri::CompareOp::ALWAYS, "Always compare op");

        return true;
    }

    static bool Test_SequentialRenderGraph() {
        using Graph = RenderGraph;
        using Layout = RHI::RHIResourceLayout;
        using Access = Graph::Access;
        using Load = Graph::Load;
        Graph valid;
        auto image = valid.Import("scene", nullptr);
        auto copy = valid.Import("copy", nullptr);
        valid.AddPass("clear", {{image,Layout::ColorAttachment,Access::Write,Load::Clear}});
        valid.AddPass("copy", {{image,Layout::CopySource},{copy,Layout::CopyDest,Access::Write}});
        valid.AddPass("blend", {{image,Layout::ColorAttachment,Access::Write,Load::Load},{copy,Layout::ShaderResource}});
        valid.AddPass("export", {{image,Layout::ShaderResource}});
        RENDER_CHECK(valid.Compile(), "sequential clear/copy/LOAD/export is a valid graph");
        Graph strictPhysical;
        auto missingPhysical = strictPhysical.Import("missing physical", nullptr);
        strictPhysical.AddPass("strict", {{missingPhysical, Layout::ColorAttachment, Access::Write, Load::Clear}});
        RENDER_CHECK(!strictPhysical.Compile(true), "strict graph rejects a pass without a live physical generation");
        const auto& blendDeps = valid.GetDependencies(2);
        RENDER_CHECK(std::find(blendDeps.begin(),blendDeps.end(),0) != blendDeps.end() &&
                     std::find(blendDeps.begin(),blendDeps.end(),1) != blendDeps.end(), "graph derives RAW, WAW and WAR dependencies");
        RENDER_CHECK(!valid.IsComplete(), "compiling is not GPU execution");
        RHI::RHICommandBuffer commands;
        RENDER_CHECK(!valid.BeginPass("export",commands) && !valid.IsComplete(), "out-of-order recording cannot pretend to complete");
        Graph readFirst; auto unread = readFirst.Import("uninitialized",nullptr);
        readFirst.AddPass("sample",{{unread,Layout::ShaderResource}});
        RENDER_CHECK(!readFirst.Compile(), "sampling before first write rejected");
        Graph loadFirst; auto unloaded = loadFirst.Import("uninitialized",nullptr);
        loadFirst.AddPass("load",{{unloaded,Layout::ColorAttachment,Access::Write,Load::Load}});
        RENDER_CHECK(!loadFirst.Compile(), "first-use LOAD rejected");
        Graph discarded; auto lost = discarded.Import("transient",nullptr);
        discarded.AddPass("discard",{{lost,Layout::ColorAttachment,Access::Write,Load::Clear,Graph::Store::Discard}});
        discarded.AddPass("sample",{{lost,Layout::ShaderResource}});
        RENDER_CHECK(!discarded.Compile(), "reading a discarded attachment rejected");
        Graph forward;
        forward.AddPass("consumer",{}, {"producer"}); forward.AddPass("producer",{});
        RENDER_CHECK(!forward.Compile(), "forward/cyclic explicit dependencies rejected");
        Graph duplicate; auto alias = duplicate.Import("one view",nullptr,true);
        duplicate.AddPass("feedback",{{alias,Layout::ShaderResource},{alias,Layout::ColorAttachment,Access::Write,Load::Load}});
        RENDER_CHECK(!duplicate.Compile(), "read/write feedback through one texture rejected");
        Graph wrongLayout; auto texture = wrongLayout.Import("sampled",nullptr,true);
        wrongLayout.AddPass("bad write",{{texture,Layout::ShaderResource,Access::Write}});
        RENDER_CHECK(!wrongLayout.Compile(), "read-only native layout cannot declare a write");
        Graph missing;
        missing.AddPass("bad resource",{{99,Layout::ShaderResource}});
        RENDER_CHECK(!missing.Compile(), "undefined resource rejected");
        Graph duplicateNames; duplicateNames.AddPass("same",{});duplicateNames.AddPass("same",{});
        RENDER_CHECK(!duplicateNames.Compile(), "ambiguous pass names rejected");
        return true;
    }

    static bool Test_RenderContractsSpecifications() {
        RHI::RHIShaderReflection vertexReflection, pixelReflection;
        vertexReflection.Stage = RHI::RHIShaderStage::Vertex;
        pixelReflection.Stage = RHI::RHIShaderStage::Fragment;
        RHI::RHIShaderResourceBinding shared;
        shared.Name = "Shared"; shared.Type = RHI::RHIResourceType::ConstantBuffer;
        shared.Stage = RHI::RHIShaderStage::Vertex; vertexReflection.Resources.push_back(shared);
        shared.Stage = RHI::RHIShaderStage::Fragment; pixelReflection.Resources.push_back(shared);
        auto reflection = RHI::RHIShaderReflection::Merge({vertexReflection,pixelReflection});
        auto layout = reflection.BuildPipelineLayoutDesc();
        const auto bothStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;
        RENDER_CHECK(layout.ShaderStages == bothStages && layout.DescriptorSets.size() == 1 &&
            layout.DescriptorSets[0].Ranges.size() == 1 && layout.DescriptorSets[0].Ranges[0].shaderStages == bothStages,
            "merged reflection keeps concrete stages on layouts and shared resources");
        vertexReflection.Resources.clear(); pixelReflection.Resources.clear();
        auto resourceFree = RHI::RHIShaderReflection::Merge({vertexReflection,pixelReflection}).BuildPipelineLayoutDesc();
        RENDER_CHECK(resourceFree.ShaderStages == bothStages, "resource-free shaders still declare pipeline stages");
        // 1. Vertex Layout Contract
        auto vtxDesc = VertexLayoutContract::GetCanonicalVertexInputDesc();
        RENDER_CHECK(VertexLayoutContract::VerifyVertexLayout(vtxDesc), "Canonical vertex layout matches 64B PBR layout");
        RENDER_CHECK(VertexLayoutContract::StaticVertexStride == 64, "Static vertex stride must be 64 bytes");
        RENDER_CHECK(VertexLayoutContract::SkinnedVertexStride == 96, "Skinned vertex stride must be 96 bytes");
        RENDER_CHECK(VertexLayoutContract::PositionOffset == 0, "Position offset is 0");
        RENDER_CHECK(VertexLayoutContract::NormalOffset == 12, "Normal offset is 12");
        RENDER_CHECK(VertexLayoutContract::TangentOffset == 24, "Tangent offset is 24");
        RENDER_CHECK(VertexLayoutContract::TexCoordOffset == 40, "TexCoord offset is 40");
        RENDER_CHECK(VertexLayoutContract::ColorOffset == 48, "Color offset is 48");

        // 2. Constant Buffer Contracts
        RENDER_CHECK(ConstantBufferContract::VerifyMaterialConstantPacking(), "RHIMaterialConstants packing verified");
        RENDER_CHECK(ConstantBufferContract::FrameConstantsSize == 256, "Frame constants 256 bytes CBV aligned");
        RENDER_CHECK(ConstantBufferContract::ObjectRootConstantsSize == 128, "Object root constants 128 bytes");
        RENDER_CHECK(ConstantBufferContract::ToneMappingConstantsSize == 256, "Tone mapping constants 256 bytes");

        // 3. Texture Slot Contracts
        RENDER_CHECK(TextureSlotContract::SlotCount == 10, "10 standard texture slots");
        RENDER_CHECK(TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::Albedo), "Albedo slot is sRGB");
        RENDER_CHECK(TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::Emissive), "Emissive slot is sRGB");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::Normal), "Normal slot is linear");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::Roughness), "Roughness slot is linear");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::Metallic), "Metallic slot is linear");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::AO), "AO slot is linear");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::Clearcoat), "Clearcoat slot is linear");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::AnisotropyFlow), "AnisotropyFlow slot is linear");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::DetailNormal), "DetailNormal slot is linear");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::DetailMask), "DetailMask slot is linear");

        // 4. Color Space & Tone Mapping Ownership Contracts
        float linearVal = 0.18f; // Standard 18% gray
        float srgbVal = ColorSpaceContract::LinearToSRGB(linearVal);
        RENDER_CHECK(Near(srgbVal, 0.4613f, 0.01f), "18% linear gray converts to ~0.46 sRGB");
        float roundTrip = ColorSpaceContract::SRGBToLinear(srgbVal);
        RENDER_CHECK(Near(roundTrip, linearVal, 0.001f), "sRGB round-trip recovers linear value");

        glm::vec4 linearRGBA(0.5f, 0.25f, 0.125f, 0.8f);
        glm::vec4 srgbRGBA = ColorSpaceContract::LinearToSRGB(linearRGBA);
        RENDER_CHECK(Near(srgbRGBA.a, 0.8f, 0.0001f), "Alpha channel remains untouched during sRGB conversion");
        glm::vec4 recoveredRGBA = ColorSpaceContract::SRGBToLinear(srgbRGBA);
        RENDER_CHECK(Near(recoveredRGBA.r, linearRGBA.r, 0.001f), "Red channel round trip");
        RENDER_CHECK(Near(recoveredRGBA.g, linearRGBA.g, 0.001f), "Green channel round trip");
        RENDER_CHECK(Near(recoveredRGBA.b, linearRGBA.b, 0.001f), "Blue channel round trip");
        RENDER_CHECK(Near(recoveredRGBA.a, linearRGBA.a, 0.0001f), "Alpha channel round trip");

        // 5. Depth and Clip Contracts
        float d3dNear = 0.0f, d3dFar = 0.0f;
        DepthClipContract::GetDepthRange(RendererBackendType::ModernRHI_D3D12, d3dNear, d3dFar);
        RENDER_CHECK(Near(d3dNear, 0.0f, 0.001f) && Near(d3dFar, 1.0f, 0.001f), "D3D12 depth range [0, 1]");

        float glNear = 0.0f, glFar = 0.0f;
        DepthClipContract::GetDepthRange(RendererBackendType::OpenGL_Compatibility, glNear, glFar);
        RENDER_CHECK(Near(glNear, -1.0f, 0.001f) && Near(glFar, 1.0f, 0.001f), "OpenGL legacy depth range [-1, 1]");

        RENDER_CHECK(DepthClipContract::GetDefaultDepthCompareOp() == nri::CompareOp::LESS, "Default depth compare op is LESS");
        RENDER_CHECK(DepthClipContract::IsRightHanded, "Right-handed coordinate system");

        // 6. Surface Blend Contracts
        auto opaqueBlend = SurfaceBlendContract::GetBlendAttachmentDesc(SurfaceType::Opaque);
        RENDER_CHECK(!opaqueBlend.BlendEnabled, "Opaque blend disabled");
        auto opaqueDS = SurfaceBlendContract::GetDepthStencilDesc(SurfaceType::Opaque);
        RENDER_CHECK(opaqueDS.DepthWriteEnable && opaqueDS.DepthTestEnable, "Opaque writes depth");

        auto cutoutBlend = SurfaceBlendContract::GetBlendAttachmentDesc(SurfaceType::Cutout);
        RENDER_CHECK(!cutoutBlend.BlendEnabled, "Cutout blend disabled");
        auto cutoutDS = SurfaceBlendContract::GetDepthStencilDesc(SurfaceType::Cutout);
        RENDER_CHECK(cutoutDS.DepthWriteEnable && cutoutDS.DepthTestEnable, "Cutout writes depth");

        auto transBlend = SurfaceBlendContract::GetBlendAttachmentDesc(SurfaceType::Transparent);
        RENDER_CHECK(transBlend.BlendEnabled, "Transparent blend enabled");
        RENDER_CHECK(transBlend.SrcColor == nri::BlendFactor::SRC_ALPHA && transBlend.DstColor == nri::BlendFactor::ONE_MINUS_SRC_ALPHA, "Alpha blend factors");
        auto transDS = SurfaceBlendContract::GetDepthStencilDesc(SurfaceType::Transparent);
        RENDER_CHECK(!transDS.DepthWriteEnable && transDS.DepthTestEnable, "Transparent does not write depth");

        auto addBlend = SurfaceBlendContract::GetBlendAttachmentDesc(SurfaceType::Additive);
        RENDER_CHECK(addBlend.BlendEnabled, "Additive blend enabled");
        RENDER_CHECK(addBlend.SrcColor == nri::BlendFactor::SRC_ALPHA && addBlend.DstColor == nri::BlendFactor::ONE, "Additive blend factors");
        auto addDS = SurfaceBlendContract::GetDepthStencilDesc(SurfaceType::Additive);
        RENDER_CHECK(!addDS.DepthWriteEnable && addDS.DepthTestEnable, "Additive does not write depth");

        auto backRaster = SurfaceBlendContract::GetRasterizerDesc(CullMode::Back, false);
        RENDER_CHECK(backRaster.CullMode == nri::CullMode::BACK && backRaster.FillMode == nri::FillMode::SOLID, "Back cull solid");
        auto wireRaster = SurfaceBlendContract::GetRasterizerDesc(CullMode::None, true);
        RENDER_CHECK(wireRaster.CullMode == nri::CullMode::NONE && wireRaster.FillMode == nri::FillMode::WIREFRAME, "None cull wireframe");

        // 7. Capability & Fallback Non-Destructive Reporting Contract
        Material mat("TestAuthoredMaterial");
        mat.SetAlbedoColor(glm::vec4(0.9f, 0.1f, 0.1f, 1.0f));
        mat.SetUseCustomShader(true);
        mat.SetCustomShaderPath("shaders/SpecialCarPaint.hlsl");

        std::string diag;
        bool glCompat = CapabilityContract::ValidateMaterialCompatibility(RendererBackendType::OpenGL_Compatibility, mat, diag);
        RENDER_CHECK(!glCompat, "Custom HLSL reported incompatible under OpenGL");
        RENDER_CHECK(!diag.empty() && diag.find("Custom HLSL shaders are not supported") != std::string::npos, "Explicit GL diagnostic generated");
        // CRITICAL CONTRACT: Authored material properties must NOT be mutated or reset!
        RENDER_CHECK(mat.GetUseCustomShader(), "Authored UseCustomShader preserved under GL");
        RENDER_CHECK(mat.GetCustomShaderPath() == "shaders/SpecialCarPaint.hlsl", "Authored shader path preserved under GL");
        RENDER_CHECK(Near(mat.GetAlbedoColor().r, 0.9f, 0.001f), "Authored albedo color preserved under GL");

        // Under D3D12/Vulkan, compatibility succeeds
        diag.clear();
        bool d3dCompat = CapabilityContract::ValidateMaterialCompatibility(RendererBackendType::ModernRHI_D3D12, mat, diag);
        RENDER_CHECK(d3dCompat, "Custom HLSL compatible under D3D12");
        RENDER_CHECK(diag.empty(), "No diagnostic for supported D3D12");

        return true;
    }

} // namespace Axle::RenderTests

bool RunRenderContractsUnitTests() {
    std::cout << "  [RenderContracts] Running RHI contracts unit tests...\n";
    if (!Axle::RenderTests::Test_RHITypeAndStringConversions()) return false;
    if (!Axle::RenderTests::Test_RHIVertexLayoutContracts()) return false;
    if (!Axle::RenderTests::Test_MikkTSpaceTangentsGeneration()) return false;
    if (!Axle::RenderTests::Test_RHIBarrierContracts()) return false;
    if (!Axle::RenderTests::Test_RHISamplerContracts()) return false;
    if (!Axle::RenderTests::Test_RenderContractsSpecifications()) return false;
    if (!Axle::RenderTests::Test_SequentialRenderGraph()) return false;
    std::cout << "  [RenderContracts] All contracts unit tests passed.\n";
    return true;
}

