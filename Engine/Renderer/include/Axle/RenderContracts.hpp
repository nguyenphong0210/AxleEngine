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

    // =========================================================================
    // 8. Occlusion and Shading Separation Contracts (M15-U4)
    // =========================================================================
    namespace OcclusionContract {
        enum class DataflowMode : uint8_t {
            PostLightingComposite = 0, // Fallback composite path after forward lighting
            PreLightingSeparated  = 1  // M15-U4 pre-lighting dataflow: AO modulates only indirect/ambient
        };

        struct ShadingComponents {
            glm::vec3 Ambient{0.0f};        // Indirect diffuse + indirect specular (IBL or analytical)
            glm::vec3 DirectDiffuse{0.0f};  // Total direct diffuse (backward compatible)
            glm::vec3 DirectSpecular{0.0f}; // Total direct specular (backward compatible)
            glm::vec3 Emissive{0.0f};       // Authored material emissive radiance

            // Detailed light term separation (M15-U4-B: directional sun vs local lights)
            glm::vec3 SunDiffuse{0.0f};     // Matching directional sun diffuse contribution
            glm::vec3 SunSpecular{0.0f};    // Matching directional sun specular contribution
            glm::vec3 LocalDiffuse{0.0f};   // Local / punctual light diffuse (point, spot)
            glm::vec3 LocalSpecular{0.0f};  // Local / punctual light specular (point, spot)

            bool HasSun{true};              // Directional sun light present in scene
            bool HasLocalLights{false};     // Local lights present in scene
            bool EnableAO{true};            // Ambient occlusion evaluation enabled
            bool EnableContactShadows{true};// Contact shadow evaluation enabled
            bool EnableCSM{true};           // Cascaded shadow maps enabled
            float CSMVisibility{1.0f};      // [0, 1] visibility from CSM
        };

        // Evaluates final surface radiance under the pre-lighting occlusion contract:
        // - AO modulates strictly Ambient; Emissive remains 100% invariant.
        // - Contact shadow modulates strictly the matching directional sun contribution.
        // - Local lights (point, spot) are NEVER attenuated by directional contact shadows.
        // - Combined directional visibility avoids CSM/contact double attenuation via min(csm, contact).
        // - When contact shadows are disabled, normal direct-light behavior is preserved.
        glm::vec3 EvaluateShading(const ShadingComponents& comp, float ambientOcclusion, float contactShadow = 1.0f);

        // Combines CSM visibility and contact shadow visibility, preventing double attenuation.
        // Returns the effective directional light visibility factor in [0.0, 1.0].
        float CombineDirectionalVisibility(float csmVisibility, float contactShadow,
                                           bool enableContactShadows = true, bool enableCSM = true);

        // Validates whether a separate physical indirect render target is required for the given dataflow mode.
        // Under PreLightingSeparated forward shading, a separate indirect target is NOT required.
        bool IsSeparateIndirectTargetRequired(DataflowMode mode);

        // Returns architectural rationale documentation regarding separate indirect targets.
        std::string_view GetIndirectTargetDecisionRationale();

        // Verification helpers:
        // 1. Verifies that direct lighting and emissive are invariant under AO variation.
        bool VerifyAmbientOnlyModulation(const ShadingComponents& comp);

        // 2. M15-U4-04.1: Verifies contact shadow modulates strictly the matching directional sun contribution;
        //    local lights, ambient, and emissive remain strictly invariant.
        bool VerifySunOnlyContactModulation(const ShadingComponents& comp);

        // 3. M15-U4-04.2: Verifies that CSM and contact shadows avoid double attenuation:
        //    - Fully in CSM shadow (CSM=0) -> combined visibility is 0 (contact shadow cannot double-darken).
        //    - Partial penumbra (CSM=0.5, contact=0.5) -> combined visibility is 0.5 (not 0.25).
        //    - Local lights are never attenuated by contact shadows.
        bool VerifyNoCSMDoubleAttenuation(float csmVisibility, float contactShadow);

        // 4. M15-U4-04.1: Verifies that when sun is absent (HasSun = false), contact shadows have zero effect.
        bool VerifySunAbsenceBehavior(const ShadingComponents& comp);

        // 5. M15-U4-04.1: Verifies that when contact shadows are disabled, normal direct-light behavior is preserved.
        bool VerifyContactDisabledBehavior(const ShadingComponents& comp);

        // =====================================================================
        // M15-U4-C: Half-Resolution, Bilateral Reconstruction & Depth Edge Protection
        // =====================================================================
        struct Resolution2D {
            uint32_t Width = 0;
            uint32_t Height = 0;
            bool operator==(const Resolution2D&) const = default;
        };

        // Computes half-resolution dimensions for GTAO targets from full-resolution extent.
        // Handles even, odd, and degenerate dimensions (e.g. 1920x1080 -> 960x540, 1919x1079 -> 959x539, 1x1 -> 1x1).
        Resolution2D ComputeHalfResolution(uint32_t fullWidth, uint32_t fullHeight);

        // Bilateral reconstruction weights and depth-edge filter evaluation
        struct BilateralTapResult {
            float SpatialWeight = 0.0f;
            float DepthWeight = 0.0f;
            float TotalWeight = 0.0f;
            bool IsEdgeRejected = false;
        };

        // Evaluates cross-bilateral filter weight for a sample tap:
        // centerZ: view-space depth of center pixel
        // tapZ: view-space depth of neighbor tap
        // tapOffsetX, tapOffsetY: integer half-resolution texel offset in [-2, 2].
        // Rejected/invalid depth taps contribute zero weight, matching GTAO.hlsl.
        BilateralTapResult EvaluateBilateralWeight(float centerZ, float tapZ, int tapOffsetX, int tapOffsetY);

        // Verification helpers for M15-U4-C:
        // 1. M15-U4-05.1: Verifies half-resolution computation across standard and odd dimensions.
        bool VerifyHalfResolutionDimensions(uint32_t fullW, uint32_t fullH);

        // 2. M15-U4-05.2: Verifies 5x5 spatial kernel normalization and smooth reconstruction stability.
        bool VerifyBilateralReconstructionStability();

        // 3. M15-U4-05.3: Verifies depth-edge rejection (edge isolation) across depth discontinuities.
        bool VerifyDepthEdgeProtection(float centerZ, float foregroundZ, float backgroundZ);

        // 4. M15-U4-05.4: Verifies odd-resolution boundary safety (1919x1079, 127x63, 1x1).
        bool VerifyOddResolutionSafety(uint32_t oddW, uint32_t oddH);

        // =====================================================================
        // M15-U4-D: Regression Fixtures & Invariance Contracts (U4-06.1 - U4-06.5)
        // =====================================================================
        // 1. M15-U4-06.1: Verifies that when sun is absent (HasSun = false), contact shadows have zero effect
        //    and total radiance matches ambient + local + emissive, with all outputs finite.
        bool VerifyNoSunRegression(const ShadingComponents& comp);

        // 2. M15-U4-06.2: Verifies that when AO is disabled, ambient receives neutral factor and all outputs are finite.
        bool VerifyNoAORegression(const ShadingComponents& comp);

        // 3. M15-U4-06.3: Verifies that when contact shadows are disabled, directional sun receives normal CSM visibility
        //    without contact attenuation and all outputs are finite.
        bool VerifyNoContactRegression(const ShadingComponents& comp);

        // 4. M15-U4-06.4: Verifies that emissive radiance is strictly invariant under both AO and contact variations,
        //    is never darkened, and all outputs are finite.
        bool VerifyEmissiveInvariance(const ShadingComponents& comp);

        // 5. M15-U4-06.5: Verifies that unoccluded specular highlights (e.g. mirror/metallic sphere) are strictly invariant
        //    under AO variations, are never darkened, and all outputs are finite.
        bool VerifyUnoccludedSpecularInvariance(const ShadingComponents& comp);
    }

} // namespace Axle
