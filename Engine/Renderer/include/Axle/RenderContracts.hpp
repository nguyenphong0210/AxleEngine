#pragma once

#include <Axle/Types.hpp>
#include <Axle/RenderCapabilities.hpp>
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIMaterialConstants.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Axle {

    class Material;

    // =========================================================================
    // 1. Canonical Vertex Layout Contracts
    // =========================================================================
    namespace VertexLayoutContract {
        constexpr uint32_t StaticVertexStride = sizeof(RHI::RHIVertex3D);         // 64 bytes
        constexpr uint32_t SkinnedVertexStride = sizeof(RHI::RHISkinnedVertex3D); // 96 bytes

        constexpr uint32_t PositionOffset = offsetof(RHI::RHIVertex3D, Position); // 0
        constexpr uint32_t NormalOffset   = offsetof(RHI::RHIVertex3D, Normal);   // 12
        constexpr uint32_t TangentOffset  = offsetof(RHI::RHIVertex3D, Tangent);  // 24
        constexpr uint32_t TexCoordOffset = offsetof(RHI::RHIVertex3D, TexCoord); // 40
        constexpr uint32_t ColorOffset    = offsetof(RHI::RHIVertex3D, Color);    // 48

        // Canonical vertex input description for graphics pipeline creation
        RHI::RHIVertexInputDesc GetCanonicalVertexInputDesc();
        bool VerifyVertexLayout(const RHI::RHIVertexInputDesc& desc);
    }

    // =========================================================================
    // 2. Constant Buffer and Root Constant Contracts
    // =========================================================================
    namespace ConstantBufferContract {
        // Frame constants: register b1, space 0 (256 bytes CBV aligned)
        constexpr uint32_t FrameConstantsRegister = 1;
        constexpr uint32_t FrameConstantsSpace = 0;
        constexpr size_t   FrameConstantsSize = 256;

        // Object root constants: space 0 (128 bytes push constants)
        constexpr uint32_t ObjectRootConstantsSpace = 0;
        constexpr size_t   ObjectRootConstantsSize = 128;

        // Light constants: register b2, space 0
        constexpr uint32_t LightConstantsRegister = 2;
        constexpr uint32_t LightConstantsSpace = 0;

        // Material constants: register b3, space 0 (256 bytes CBV aligned)
        constexpr uint32_t MaterialConstantsRegister = 3;
        constexpr uint32_t MaterialConstantsSpace = 0;
        constexpr size_t   MaterialConstantsSize = sizeof(RHI::RHIMaterialConstants); // 256

        // Tone mapping constants: register b0, space 0 (256 bytes CBV aligned)
        constexpr uint32_t ToneMappingConstantsRegister = 0;
        constexpr uint32_t ToneMappingConstantsSpace = 0;
        constexpr size_t   ToneMappingConstantsSize = 256;

        bool VerifyMaterialConstantPacking();
    }

    // =========================================================================
    // 3. Texture Slot and Color Space Contracts
    // =========================================================================
    enum class TextureColorSpace : uint8_t {
        Linear = 0,
        sRGB   = 1
    };

    struct TextureSlotContractInfo {
        RHI::RHITextureSlot Slot;
        const char* Name;
        const char* SemanticRole;
        TextureColorSpace ColorSpace;
        nri::Format ExpectedFormat;
    };

    namespace TextureSlotContract {
        constexpr uint32_t SlotCount = static_cast<uint32_t>(RHI::RHITextureSlot::Count); // 10

        // Role & color space mapping for all standard texture slots
        const TextureSlotContractInfo& GetSlotInfo(RHI::RHITextureSlot slot);
        bool IsSRGBSlot(RHI::RHITextureSlot slot);
        std::string_view GetSlotName(RHI::RHITextureSlot slot);
    }

    // =========================================================================
    // 4. Color Space & Tone Mapping Ownership Contracts
    // =========================================================================
    namespace ColorSpaceContract {
        // Linear pipeline rule: Lighting accumulation and intermediate shading evaluate strictly in Linear HDR space
        // Tone mapping and final sRGB conversion are strictly owned by post-processing passes (ToneMapping.hlsl)

        float LinearToSRGB(float linearValue);
        float SRGBToLinear(float srgbValue);
        glm::vec3 LinearToSRGB(const glm::vec3& linearColor);
        glm::vec3 SRGBToLinear(const glm::vec3& srgbColor);
        glm::vec4 LinearToSRGB(const glm::vec4& linearColor);
        glm::vec4 SRGBToLinear(const glm::vec4& srgbColor);
    }

    // =========================================================================
    // 5. Depth, Clip and Coordinate System Contracts
    // =========================================================================
    namespace DepthClipContract {
        // Right-handed coordinate system: +X right, +Y up, -Z into screen (camera looks down -Z)
        constexpr bool IsRightHanded = true;

        // Front face winding & culling conventions
        constexpr bool FrontCounterClockwise = true;

        // Clip NDC depth range:
        // Modern RHI (D3D12/Vulkan): [0.0, 1.0]
        // OpenGL: [-1.0, 1.0]
        void GetDepthRange(RendererBackendType backend, float& outNearZ, float& outFarZ);
        nri::CompareOp GetDefaultDepthCompareOp();
    }

    // =========================================================================
    // 6. Surface Mode, Blending and Culling Contracts
    // =========================================================================
    namespace SurfaceBlendContract {
        RHI::RHIBlendAttachmentDesc GetBlendAttachmentDesc(SurfaceType surface);
        RHI::RHIDepthStencilDesc    GetDepthStencilDesc(SurfaceType surface, bool depthWriteOverride = true);
        RHI::RHIRasterizerDesc      GetRasterizerDesc(CullMode cull, bool wireframe = false);
    }

    // =========================================================================
    // 7. Capability and Fallback Reporting Contract
    // =========================================================================
    namespace CapabilityContract {
        // Validates whether the authored material properties can be fully executed on the target backend
        // Returns true if supported; if unsupported, returns false and outputs an explicit diagnostic
        // CRITICAL CONTRACT: Under unsupported backends, authored material properties are NEVER mutated or reset.
        bool ValidateMaterialCompatibility(RendererBackendType backend, const Material& material, std::string& outDiagnostic);
    }

} // namespace Axle
