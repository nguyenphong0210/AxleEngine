#include "RenderTestCommon.hpp"
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHISwitcher.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <Axle/RHI/RHIBarrier.hpp>
#include <Axle/RHI/RHISampler.hpp>

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

} // namespace Axle::RenderTests

bool RunRenderContractsUnitTests() {
    std::cout << "  [RenderContracts] Running RHI contracts unit tests...\n";
    if (!Axle::RenderTests::Test_RHITypeAndStringConversions()) return false;
    if (!Axle::RenderTests::Test_RHIVertexLayoutContracts()) return false;
    if (!Axle::RenderTests::Test_MikkTSpaceTangentsGeneration()) return false;
    if (!Axle::RenderTests::Test_RHIBarrierContracts()) return false;
    if (!Axle::RenderTests::Test_RHISamplerContracts()) return false;
    std::cout << "  [RenderContracts] All contracts unit tests passed.\n";
    return true;
}

