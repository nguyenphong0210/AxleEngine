#pragma once

#include <Axle/Types.hpp>
#include <Axle/RenderCapabilities.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/LightingService.hpp>
#include <Axle/ReflectionService.hpp>
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHIMaterialConstants.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <glm/glm.hpp>
#include <array>
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

        // =====================================================================
        // M15-U4-E: Normal-Map & Animated/Deformed Mesh Contracts (U4-07.1 - U4-07.2)
        // =====================================================================
        struct NormalMapSample {
            glm::vec2 TangentRG{0.5f, 0.5f}; // Raw texture sample in [0, 1]
            float Strength{1.0f};            // Normal map strength scale
            glm::vec3 GeometricNormal{0.0f, 0.0f, 1.0f};
            glm::vec4 Tangent{1.0f, 0.0f, 0.0f, 1.0f}; // xyz = tangent, w = handedness
        };

        // 1. M15-U4-07.1: Evaluates world-space perturbed normal from tangent-space normal map
        //    using canonical TBN construction matching ForwardPBR.hlsl and SurfaceNormal.hlsl.
        glm::vec3 EvaluateTangentSpaceNormal(const NormalMapSample& sample);

        // 2. M15-U4-07.1: Verifies that normal-mapped surface occlusion and contact shadows
        //    remain stable, bounded, and finite across normal map strengths, grazing angles,
        //    and back-facing perturbations without faceting or NaNs.
        bool VerifyNormalMapOcclusionStability(const NormalMapSample& sample, const glm::mat4& viewMatrix,
                                               const glm::vec3& viewDir, const glm::vec3& lightDirView);

        struct DeformedVertexSample {
            glm::vec3 RestPosition{0.0f};
            glm::vec3 RestNormal{0.0f, 0.0f, 1.0f};
            glm::vec4 RestTangent{1.0f, 0.0f, 0.0f, 1.0f};
            glm::mat4 DeformationTransform{1.0f}; // Linear blend skinning or dynamic deform matrix
        };

        // 3. M15-U4-07.2: Evaluates deformed vertex position, normal, and tangent.
        void EvaluateDeformedVertex(const DeformedVertexSample& sample,
                                    glm::vec3& outPosition, glm::vec3& outNormal, glm::vec4& outTangent);

        // 4. M15-U4-07.2: Verifies that deformed/animated mesh depth reconstruction and bilateral occlusion
        //    remain stable without faceting, halos, disocclusion bleeding, or NaNs.
        bool VerifyDeformedMeshOcclusionStability(const DeformedVertexSample& sample, float backgroundDepth,
                                                 const glm::mat4& viewMatrix, const glm::mat4& projMatrix);

        // =====================================================================
        // M15-U4-GTAO-HALO-FIX: World-Space Falloff, Normal-Depth Upsample, Specular & Multi-Bounce
        // =====================================================================
        struct NormalDepthBilateralTapResult {
            float SpatialWeight = 0.0f;
            float DepthWeight = 0.0f;
            float NormalWeight = 0.0f;
            float TotalWeight = 0.0f;
            bool IsEdgeRejected = false;
        };

        // Smooth cubic distance falloff for GTAO horizon samples:
        // 1 inside falloffStart, smoothly decaying to 0 at worldRadius.
        float EvaluateGTAODistanceFalloff(float distance, float worldRadius, float falloffStartFraction);

        // Lagarde/de Rousiers specular occlusion factor preserving unoccluded Fresnel grazing (ao=1)
        // and blocking corner IBL leaks (ao=0) while attenuating tight smooth crevices by (NdotV, roughness).
        float ComputeSpecularOcclusion(float NdotV, float ao, float roughness);

        // Jimenez multi-bounce colored AO approximation blended by strength in [0, 1].
        glm::vec3 ComputeMultiBounceAO(float ao, const glm::vec3& albedo, float strength = 1.0f);

        // Evaluates subpixel spatial + plane-depth + normal edge-stopping bilateral weight matching GTAO CSUpsample.
        NormalDepthBilateralTapResult EvaluateNormalDepthBilateralWeight(
            const glm::vec3& centerPos, const glm::vec3& centerNormal,
            const glm::vec3& tapPos, const glm::vec3& tapNormal, bool hasTapNormal,
            const glm::vec2& deltaFullResPx);
    }

    // =========================================================================
    // 9. Hi-Z Depth Pyramid Contracts (M15-U5-A: U5-05.1 - U5-07.2)
    // =========================================================================
    namespace HiZContract {
        constexpr float NearDepth = 0.0f;
        constexpr float FarDepth = 1.0f;
        constexpr uint32_t MaxLevels = RHI::RHIViewTargets::MaxHiZLevels;
        constexpr uint32_t SSRBindingLevels = RHI::RHIViewTargets::MinSSRHiZLevels;

        struct PyramidExtent {
            uint32_t Width = 0;
            uint32_t Height = 0;
            bool operator==(const PyramidExtent&) const = default;
        };

        // Useful reduction depth from full resolution down to 1x1 (inclusive). Returns 0 for zero extents.
        uint32_t ComputeUsefulLevelCount(uint32_t fullWidth, uint32_t fullHeight);

        // Allocated RHI Hi-Z levels: max(SSRBindingLevels, ComputeUsefulLevelCount). Returns 0 for zero extents.
        uint32_t ComputeAllocatedLevelCount(uint32_t fullWidth, uint32_t fullHeight);

        // Computes extent of Hi-Z level using floor-halving clamped to [1, full]. Returns {0,0} on invalid input.
        PyramidExtent ComputeLevelExtent(uint32_t fullWidth, uint32_t fullHeight, uint32_t level);

        std::string_view GetDepthConventionSummary();
        std::string_view GetEdgePolicySummary();

        // Builds the CPU conservative min-reduction reference matching HiZ.hlsl:
        // - Level 0 copies finite [0, 1] input depth.
        // - Subsequent levels compute min over 2x2 blocks, conservatively folding trailing
        //   odd column/row (3rd tap at max edge when srcExtent > 1 is odd) so no closer blocker
        //   can be dropped at odd viewports (e.g. 3x5, 1919x1079).
        bool BuildMinReductionPyramid(const std::vector<float>& depthLevel0,
                                      uint32_t width, uint32_t height, uint32_t levelCount,
                                      std::vector<std::vector<float>>& outLevels,
                                      std::vector<PyramidExtent>& outExtents);

        // Verifies conservative nearest-blocker coverage: global minimum is preserved at every
        // level and every coarse texel is <= all covered finer texels.
        bool VerifyConservativeMinReduction(const std::vector<float>& depthLevel0,
                                            uint32_t width, uint32_t height);

        // Verifies dimensions, boundary coverage, and trailing-edge propagation for arbitrary odd/minimal sizes.
        bool VerifyOddResolutionPyramid(uint32_t width, uint32_t height);
    }

    // =========================================================================
    // Reflection Capture Contract (M15-U5-B)
    // =========================================================================
    namespace ReflectionCaptureContract {
        constexpr uint32_t FaceCount = 6u;
        constexpr uint32_t DefaultResolution = 128u;
        constexpr uint32_t MinResolution = 16u;
        constexpr uint32_t MaxResolution = 1024u;
        constexpr float FaceFovDegrees = 90.0f;
        constexpr float FaceAspectRatio = 1.0f;
        constexpr float MinNearPlane = 0.01f;
        constexpr uint32_t MaxPrefilterMipCount = 5u;
        constexpr uint32_t MinPrefilterSampleCount = 16u;
        constexpr uint32_t DefaultPrefilterSampleCount = 64u;
        constexpr uint32_t MaxPrefilterSampleCount = 1024u;

        struct ReflectionPrefilterPolicy {
            uint32_t Resolution = DefaultResolution;
            uint32_t MipCount = MaxPrefilterMipCount;
            uint32_t SampleCount = DefaultPrefilterSampleCount;
            nri::Format ColorFormat = nri::Format::RGBA16_SFLOAT;
        };

        struct CubeFaceBasis {
            uint32_t FaceIndex = 0;
            glm::vec3 Forward{ 1.0f, 0.0f, 0.0f };
            glm::vec3 Up{ 0.0f, 1.0f, 0.0f };
            glm::vec3 Right{ 0.0f, 0.0f, -1.0f };
            bool FlipHorizontalForCubemapCopy = true;
        };

        struct ReflectionCapturePolicy {
            uint32_t Resolution = DefaultResolution;
            uint8_t SampleCount = 1u;
            nri::Format ColorFormat = nri::Format::RGBA16_SFLOAT;
            nri::Format DepthFormat = nri::Format::D32_SFLOAT;
            bool IncludeOpaqueAndCutout = true;
            bool IncludeSkybox = true;
            bool IncludeSelectedShadows = true;
            bool IncludeDirectionalShadows = true;
            bool IncludeLocalShadows = true;
            bool IncludeTransparent = false;
            bool IncludeParticles = false;
            bool IncludeEditorOverlays = false;
            bool AllowSSR = false;
            bool AllowProbeSampling = false;
            bool IsolateMainViewTAA = true;
            float NearPlane = 0.1f;
            float FarPlane = 100.0f;
            uint64_t ExcludeEntityID = 0;
            std::vector<uint64_t> AdditionalExcludedEntityIDs;
        };

        CubeFaceBasis GetCubeFaceBasis(uint32_t faceIndex);
        bool ValidateCubeFaceBasis(const CubeFaceBasis& basis, std::string* outReason = nullptr);
        bool ValidateCapturePolicy(const ReflectionCapturePolicy& policy, std::string* outReason = nullptr);

        PerspectiveCamera BuildCubeFaceCamera(
            const glm::vec3& probePosition,
            uint32_t faceIndex,
            float nearPlane,
            float farPlane);

        SceneRenderSettings SanitizeCaptureSettings(
            const SceneRenderSettings& mainSettings,
            const ReflectionCapturePolicy& policy);

        RenderWorld FilterCaptureWorld(
            const RenderWorld& sourceWorld,
            const ReflectionCapturePolicy& policy);

        // M15-U5-C: Cubemap roughness prefilter contracts (pure 3D cubemap space; no equirectangular conversion).
        uint32_t ComputePrefilterMipCount(uint32_t resolution);
        float ComputePrefilterRoughness(uint32_t mip, uint32_t mipCount);
        bool ValidatePrefilterPolicy(const ReflectionPrefilterPolicy& policy, std::string* outReason = nullptr);
        glm::vec3 ComputeCubeTexelDirection(uint32_t faceIndex, float u, float v);
        glm::vec3 SampleCubemapBilinear3D(
            const std::array<std::vector<glm::vec3>, FaceCount>& faces,
            uint32_t faceResolution,
            const glm::vec3& direction);
        glm::vec3 EvaluatePrefilterDirectionCPU(
            const std::array<std::vector<glm::vec3>, FaceCount>& sourceFaces,
            uint32_t sourceResolution,
            const glm::vec3& normalDir,
            float roughness,
            uint32_t sampleCount = DefaultPrefilterSampleCount);

        // M15-U5-D: Capture cache sizing, LRU eviction planning, and bounded frame scheduling.
        struct CaptureCacheSlotInfo {
            std::string ProbeKey;
            uint64_t ByteSize = 0;
            uint64_t LastUsedSequence = 0;
            bool PinProtected = false;
        };

        struct ScheduledCaptureWork {
            std::string ProbeKey;
            uint32_t RemainingFaces = FaceCount;
            uint64_t PriorityScore = 0;
        };

        struct ScheduledCaptureAllocation {
            std::string ProbeKey;
            uint32_t FacesToRun = 0;
        };

        uint64_t EstimateCubemapTextureBytes(
            uint32_t resolution,
            uint32_t mipCount,
            nri::Format colorFormat = nri::Format::RGBA16_SFLOAT);

        uint64_t EstimateDynamicProbeCacheBytes(
            uint32_t resolution,
            uint32_t mipCount,
            nri::Format colorFormat = nri::Format::RGBA16_SFLOAT);

        bool ComputeLRUCacheEvictionPlan(
            const std::vector<CaptureCacheSlotInfo>& entries,
            uint64_t budgetBytes,
            uint64_t incomingBytes,
            std::vector<std::string>* outEvictKeys,
            std::string* outReason = nullptr);

        std::vector<ScheduledCaptureAllocation> ComputeFrameCaptureSchedule(
            const std::vector<ScheduledCaptureWork>& queue,
            uint32_t maxProbesPerFrame,
            uint32_t maxFacesPerFrame);
    }

    // M15-U5-F (S5.6): Frozen ray/depth projection, hierarchical Hi-Z DDA traversal,
    // roughness-cone footprint sizing, and material F0/roughness resolve contracts.
    namespace SSRRaymarchContract {
        inline constexpr uint32_t ColorPyramidLevels = 4u;
        inline constexpr float MaxConeRadiusPixels = 32.0f;

        struct RaySetup {
            glm::vec3 OriginView{0.0f};
            glm::vec3 StartView{0.0f};
            glm::vec3 StartUVZ{0.0f};
            glm::vec3 EndView{0.0f};
            glm::vec3 EndUVZ{0.0f};
            float MaxTraceDist = 0.0f;
            float MaxDistance = 0.0f;
            uint32_t StartMip = 0u;
            uint32_t MaxUsefulMip = 0u;
            bool Valid = false;
        };

        struct HiZTraceResult {
            bool Hit = false;
            glm::vec2 HitUV{0.0f};
            float HitDepth = 1.0f;
            float HitDistance = 0.0f;
            float Confidence = 0.0f;
            uint32_t StepsExecuted = 0u;
            uint32_t HighestMipReached = 0u;
        };

        glm::vec3 ProjectViewToUVZ(const glm::vec3& viewPos, const glm::mat4& projZO);
        glm::vec3 ReconstructViewPos(const glm::vec2& uv, float depth, const glm::mat4& invProjZO);
        glm::uvec2 ComputeHiZMipSize(uint32_t baseWidth, uint32_t baseHeight, uint32_t mip);
        bool ClipScreenRay(const glm::vec3& startUVZ, const glm::vec3& endUVZ, float* outTMax);
        RaySetup BuildInitialSSRRay(
            const glm::vec2& uv,
            float depth,
            const glm::vec3& viewNormal,
            const glm::mat4& projZO,
            const glm::mat4& invProjZO,
            uint32_t width,
            uint32_t height,
            uint32_t usefulHiZLevels,
            float maxDistance);
        HiZTraceResult TraceHiZDepthBuffer(
            const std::vector<std::vector<float>>& hiZPyramid,
            uint32_t width,
            uint32_t height,
            const RaySetup& ray,
            const glm::mat4& projZO,
            const glm::mat4& invProjZO,
            float thickness,
            uint32_t maxSteps);
        float EvaluateRoughnessFade(float roughness);
        float ComputeRoughnessConePixelRadius(
            float roughness,
            float hitDistanceMeters,
            float hitViewZ,
            const glm::mat4& projZO,
            uint32_t width,
            uint32_t height);
        glm::vec3 ComputeSpecularF0(const glm::vec3& baseColor, float metallic);
    }

    // =========================================================================
    // 8. LTC Rectangular & Disk Area Light Contracts (M15-U5-40..45.3)
    // =========================================================================
    namespace AreaLightLTCContract {
        static constexpr uint32_t kTableDim = 64u;
        static constexpr uint32_t kTableSize = kTableDim * kTableDim;
        static constexpr uint32_t kMaxClipVertices = 36u;

        struct LTCTables {
            // Row-major [viewIdx * kTableDim + roughIdx]:
            // LTC1 stores inverse transform M^-1 = (m00, m02, m20, m22) with M^-1[1][1] = 1.
            // LTC2 stores directional albedo / norm = (ggxNorm, fresnelTerm, 0, 0); unused upstream channels are omitted.
            std::array<glm::vec4, kTableSize> LTC1{};
            std::array<glm::vec4, kTableSize> LTC2{};
        };

        struct AreaLightEvaluation {
            float DiffuseFormFactor = 0.0f;
            float SpecularFormFactor = 0.0f;
            float RangeAttenuation = 0.0f;
            glm::vec3 SpecularAlbedo{ 0.0f };
            glm::vec3 DiffuseRadiance{ 0.0f };
            glm::vec3 SpecularRadiance{ 0.0f };
            glm::vec3 TotalRadiance{ 0.0f };
        };

        glm::vec4 EvaluateLTC1GridPoint(uint32_t roughIdx, uint32_t viewIdx);
        glm::vec4 EvaluateLTC2GridPoint(uint32_t roughIdx, uint32_t viewIdx);
        LTCTables BuildValidatedLTCTables();
        bool ValidateLTCTables(const LTCTables& tables, std::string* outReason = nullptr);

        glm::vec4 SampleLTC1(float roughness, float NdotV);
        glm::vec4 SampleLTC2(float roughness, float NdotV);

        glm::vec3 IntegrateEdgeVector(const glm::vec3& v1, const glm::vec3& v2);
        std::vector<glm::vec3> ClipPolygonToHorizon(const std::vector<glm::vec3>& poly);
        void ComputeOrthonormalAreaBasis(
            const glm::vec3& rawDir,
            const glm::vec3& rawUp,
            glm::vec3& outRight,
            glm::vec3& outUp,
            glm::vec3& outDir);
        float ComputeEmitterClosestDistance(
            const LightingRectFrame& light,
            const glm::vec3& worldPos);

        AreaLightEvaluation EvaluateLTCAreaLightCPU(
            const LightingRectFrame& light,
            const glm::vec3& worldPos,
            const glm::vec3& normalWS,
            const glm::vec3& viewDirWS,
            const glm::vec3& baseColor,
            float metallic,
            float roughness);

        // Dense 32x32 surface quadrature reference over the emitter domain.
        AreaLightEvaluation EvaluateAreaLightNumericalReferenceCPU(
            const LightingRectFrame& light,
            const glm::vec3& worldPos,
            const glm::vec3& normalWS,
            const glm::vec3& viewDirWS,
            const glm::vec3& baseColor,
            float metallic,
            float roughness,
            uint32_t samplesPerAxis = 32u);
    }

    // =========================================================================
    // 9. Planar Reflection Surface, Mirrored Camera & Clipping Contracts (M15-U5-46)
    // =========================================================================
    namespace PlanarReflectionContract {
        inline constexpr float MinResolutionScale = 0.25f;
        inline constexpr float MaxResolutionScale = 1.0f;
        inline constexpr float DefaultResolutionScale = 0.5f;
        inline constexpr float DefaultMaxRoughness = 0.45f;
        inline constexpr float DefaultEdgeFadeDistance = 0.15f;
        inline constexpr float DefaultMinCameraPlaneDistance = 0.005f;

        struct PlanarReflectionSurface {
            uint64_t EntityID = 0;
            UUID StableUUID = 0;
            glm::mat4 WorldTransform{ 1.0f };
            glm::vec3 Position{ 0.0f };
            glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
            glm::vec3 Tangent{ 1.0f, 0.0f, 0.0f };
            glm::vec3 Bitangent{ 0.0f, 0.0f, 1.0f };
            glm::vec2 HalfExtents{ 5.0f, 5.0f };
            float ClipPlaneOffset = 0.0f;
            float CaptureNearPlane = 0.1f;
            float CaptureFarPlane = 1000.0f;
            float EdgeFadeDistance = DefaultEdgeFadeDistance;
            float MaxRoughness = DefaultMaxRoughness;
            float Intensity = 1.0f;
            bool ClipToExtents = true;
            bool Valid = false;

            // Canonical world plane (nx, ny, nz, d) in meters:
            // dot(Normal, p) + d >= 0 is kept in front of the mirror.
            glm::vec4 WorldPlane() const {
                return glm::vec4(Normal, -glm::dot(Normal, Position) - ClipPlaneOffset);
            }
        };

        struct PlanarReflectionPolicy {
            float ResolutionScale = DefaultResolutionScale;
            float ClipPlaneOffset = 0.0f;
            float MinCameraPlaneDistance = DefaultMinCameraPlaneDistance;
            bool IncludeOpaqueAndCutout = true;
            bool IncludeSkybox = true;
            bool IncludeShadows = true;
            bool IncludeTransparent = false;
            bool IncludeParticles = false;
            bool IncludeEditorOverlays = false;
            bool AllowSSR = false;
            bool AllowPlanarReflections = false;
            bool AllowDynamicProbes = false;
            bool IsolateMainViewTAA = true;
            uint64_t ExcludeEntityID = 0;
            std::vector<uint64_t> AdditionalExcludedEntityIDs;
        };

        struct MirroredCameraResult {
            bool Valid = false;
            bool ObliqueClippingApplied = false;
            bool ReverseWinding = false;
            float CameraSignedDistance = 0.0f;
            PerspectiveCamera Camera;
            glm::mat4 ReflectionMatrix{ 1.0f };
            glm::vec4 WorldPlane{ 0.0f, 1.0f, 0.0f, 0.0f };
            glm::vec4 ViewPlane{ 0.0f, 0.0f, -1.0f, 0.0f };
            std::string Diagnostic;
        };

        using ::Axle::PlanarReflectionDomainStamps;
        using ::Axle::PlanarReflectionInvalidation;

        PlanarReflectionSurface BuildSurfaceFromReflector(const RenderPlanarReflector& reflector);
        float SignedDistanceToPlane(const PlanarReflectionSurface& surface, const glm::vec3& pointWS);
        glm::vec3 ReflectPoint(const PlanarReflectionSurface& surface, const glm::vec3& pointWS);
        glm::vec3 ReflectDirection(const PlanarReflectionSurface& surface, const glm::vec3& dirWS);
        glm::mat4 BuildReflectionMatrix(const PlanarReflectionSurface& surface);
        glm::mat4 BuildObliqueClipProjectionZO(
            const glm::mat4& projZO,
            const glm::mat4& mirroredView,
            const glm::vec4& worldPlane,
            glm::vec4* outViewPlane = nullptr,
            bool* outApplied = nullptr,
            glm::mat4* outObliqueGL = nullptr);
        MirroredCameraResult BuildMirroredCamera(
            const PerspectiveCamera& mainCamera,
            const PlanarReflectionSurface& surface,
            const PlanarReflectionPolicy& policy = PlanarReflectionPolicy{});
        float EvaluatePlanarReflectionConfidence(
            const PlanarReflectionSurface& surface,
            const glm::vec3& worldPos,
            const glm::vec3& normalWS,
            float roughness,
            float maxRoughnessOverride = -1.0f, float viewDistance = 0.0f, float globalStrength = 1.0f);
        glm::uvec2 ComputeScaledTargetExtent(uint32_t baseWidth, uint32_t baseHeight, float resolutionScale);
        SceneRenderSettings SanitizeCaptureSettings(
            const SceneRenderSettings& mainSettings,
            const PlanarReflectionPolicy& policy);
        RenderWorld FilterCaptureWorld(
            const RenderWorld& sourceWorld,
            const RenderPlanarReflector& reflector,
            const PlanarReflectionPolicy& policy);
        PlanarReflectionInvalidation EvaluateInvalidation(
            const PlanarReflectionDomainStamps& published,
            const PlanarReflectionDomainStamps& current,
            bool manualRequested = false);
    }

    // =========================================================================
    // 10. Volumetric Froxel Fog Resources, Depth Slice & Tier Contracts (M15-U6-A)
    // =========================================================================
    namespace FroxelFogContract {
        inline constexpr uint32_t ReferenceGridWidth = 160u;
        inline constexpr uint32_t ReferenceGridHeight = 90u;
        inline constexpr uint32_t ReferenceGridDepth = 64u;
        inline constexpr uint32_t VolumeCount = 4u; // Scattering, Integrated, History0, History1
        inline constexpr uint64_t BytesPerFroxelRGBA16F = 8ull;
        inline constexpr float MinNearDistance = 0.05f;
        inline constexpr float DefaultNearDistance = 0.25f;
        inline constexpr float DefaultMaxDistance = 100.0f;
        inline constexpr float MaxAllowedDistance = 2000.0f;

        struct FroxelMemoryBudget {
            RHI::RHIFroxelGridExtent Extent;
            uint64_t FroxelCount = 0;
            uint64_t VolumeBytesPerTexture = 0;
            uint32_t AllocatedVolumeCount = VolumeCount;
            uint64_t TotalLogicalBytes = 0;
            bool DeviceQueryAvailable = false;
            uint64_t DeviceBytesPerTexture = 0;
            uint64_t DeviceAllocationAlignment = 0;
            uint64_t TotalDeviceReportedBytes = 0;
        };

        struct FroxelFogResourcePolicy {
            float NearDistance = DefaultNearDistance;
            float MaxDistance = DefaultMaxDistance;
            uint64_t MaxBudgetBytes = 128ull * 1024ull * 1024ull;
        };

        uint32_t GetQualityTierCount();
        const RHI::RHIFroxelTierSpec& GetTierSpec(FroxelFogQuality quality);
        RHI::RHIFroxelGridExtent ComputeGridExtent(uint32_t viewWidth, uint32_t viewHeight, FroxelFogQuality quality);
        bool ResolveDepthRange(float nearDistance, float maxDistance, float& outNear, float& outFar);
        float SliceBoundaryToViewDistance(uint32_t sliceBoundary, uint32_t sliceCount, float nearDistance, float maxDistance);
        float SliceCenterToViewDistance(uint32_t sliceIndex, uint32_t sliceCount, float nearDistance, float maxDistance);
        float EvaluateSliceThickness(uint32_t sliceIndex, uint32_t sliceCount, float nearDistance, float maxDistance);
        float ViewDistanceToSliceCoord(float viewDistance, uint32_t sliceCount, float nearDistance, float maxDistance);
        float SliceCoordToViewDistance(float sliceCoord, float nearDistance, float maxDistance);
        FroxelMemoryBudget EstimateMemoryBudget(uint32_t viewWidth, uint32_t viewHeight, FroxelFogQuality quality, bool queryDevice = false);
        bool ValidateResourcePolicy(const RHI::RHIViewTargetDesc& desc, const FroxelFogResourcePolicy& policy, std::string* outReason = nullptr);
        // Use backend-resolved settings so every owner builds the same target generation.
        void ResolveViewTargetOptions(const SceneRenderSettings& settings, bool isSubCapture, bool hasDepth,
                                      bool& outEnable, uint8_t& outQuality);
    }

} // namespace Axle
