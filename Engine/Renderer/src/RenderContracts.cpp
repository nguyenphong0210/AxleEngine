#include <glm/gtc/packing.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/Material.hpp>
#include <Axle/ResolvedSurface.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_set>

namespace Axle {

    // =========================================================================
    // 1. Canonical Vertex Layout Contracts
    // =========================================================================
    namespace VertexLayoutContract {

        RHI::RHIVertexInputDesc GetCanonicalVertexInputDesc() {
            RHI::RHIVertexInputDesc desc;

            // Stream 0: Static 64-byte PBR vertex
            RHI::RHIVertexStream stream0;
            stream0.BindingSlot = 0;
            stream0.Stride = static_cast<uint16_t>(sizeof(RHI::RHIVertex3D));
            stream0.StepRate = nri::VertexStreamStepRate::PER_VERTEX;
            desc.Streams.push_back(stream0);

            // 0: Position (float3)
            RHI::RHIVertexLayoutAttribute pos;
            pos.SemanticName = "POSITION";
            pos.SemanticIndex = 0;
            pos.Location = 0;
            pos.Offset = offsetof(RHI::RHIVertex3D, Position);
            pos.Format = nri::Format::RGB32_SFLOAT;
            pos.StreamIndex = 0;
            desc.Attributes.push_back(pos);

            // 1: Normal (float3)
            RHI::RHIVertexLayoutAttribute norm;
            norm.SemanticName = "NORMAL";
            norm.SemanticIndex = 0;
            norm.Location = 1;
            norm.Offset = offsetof(RHI::RHIVertex3D, Normal);
            norm.Format = nri::Format::RGB32_SFLOAT;
            norm.StreamIndex = 0;
            desc.Attributes.push_back(norm);

            // 2: Tangent (float4)
            RHI::RHIVertexLayoutAttribute tan;
            tan.SemanticName = "TANGENT";
            tan.SemanticIndex = 0;
            tan.Location = 2;
            tan.Offset = offsetof(RHI::RHIVertex3D, Tangent);
            tan.Format = nri::Format::RGBA32_SFLOAT;
            tan.StreamIndex = 0;
            desc.Attributes.push_back(tan);

            // 3: TexCoord (float2)
            RHI::RHIVertexLayoutAttribute uv;
            uv.SemanticName = "TEXCOORD";
            uv.SemanticIndex = 0;
            uv.Location = 3;
            uv.Offset = offsetof(RHI::RHIVertex3D, TexCoord);
            uv.Format = nri::Format::RG32_SFLOAT;
            uv.StreamIndex = 0;
            desc.Attributes.push_back(uv);

            // 4: Color (float4)
            RHI::RHIVertexLayoutAttribute col;
            col.SemanticName = "COLOR";
            col.SemanticIndex = 0;
            col.Location = 4;
            col.Offset = offsetof(RHI::RHIVertex3D, Color);
            col.Format = nri::Format::RGBA32_SFLOAT;
            col.StreamIndex = 0;
            desc.Attributes.push_back(col);

            return desc;
        }

        bool VerifyVertexLayout(const RHI::RHIVertexInputDesc& desc) {
            if (desc.Streams.empty() || desc.Attributes.size() < 5) return false;
            if (desc.Streams[0].Stride != StaticVertexStride) return false;

            bool hasPos = false, hasNorm = false, hasTan = false, hasUV = false, hasCol = false;
            for (const auto& attr : desc.Attributes) {
                if (attr.SemanticName == "POSITION" && attr.Offset == PositionOffset && attr.Format == nri::Format::RGB32_SFLOAT) hasPos = true;
                if (attr.SemanticName == "NORMAL"   && attr.Offset == NormalOffset   && attr.Format == nri::Format::RGB32_SFLOAT) hasNorm = true;
                if (attr.SemanticName == "TANGENT"  && attr.Offset == TangentOffset  && attr.Format == nri::Format::RGBA32_SFLOAT) hasTan = true;
                if (attr.SemanticName == "TEXCOORD" && attr.Offset == TexCoordOffset && attr.Format == nri::Format::RG32_SFLOAT)   hasUV = true;
                if (attr.SemanticName == "COLOR"    && attr.Offset == ColorOffset    && attr.Format == nri::Format::RGBA32_SFLOAT) hasCol = true;
            }
            return hasPos && hasNorm && hasTan && hasUV && hasCol;
        }

    } // namespace VertexLayoutContract

    // =========================================================================
    // 2. Constant Buffer Contracts
    // =========================================================================
    namespace ConstantBufferContract {

        bool VerifyMaterialConstantPacking() {
            static_assert(sizeof(RHI::RHIMaterialConstants) == 256, "RHIMaterialConstants must be 256 bytes");
            static_assert(offsetof(RHI::RHIMaterialConstants, UVTransform) == 0);
            static_assert(offsetof(RHI::RHIMaterialConstants, Surface) == 16);
            static_assert(offsetof(RHI::RHIMaterialConstants, CoatAnisotropy) == 32);
            static_assert(offsetof(RHI::RHIMaterialConstants, Flags) == 48);
            static_assert(offsetof(RHI::RHIMaterialConstants, DetailUVTransform) == 64);
            static_assert(offsetof(RHI::RHIMaterialConstants, DetailParameters) == 80);
            static_assert(offsetof(RHI::RHIMaterialConstants, Refraction) == 96);
            static_assert(offsetof(RHI::RHIMaterialConstants, RefractionFrame) == 128);
            static_assert(offsetof(RHI::RHIMaterialConstants, Sheen) == 144);
            static_assert(offsetof(RHI::RHIMaterialConstants, Subsurface) == 160);
            static_assert(offsetof(RHI::RHIMaterialConstants, Absorption) == 176);
            return true;
        }

    } // namespace ConstantBufferContract

    // =========================================================================
    // 3. Texture Slot Contracts
    // =========================================================================
    namespace TextureSlotContract {

        static const TextureSlotContractInfo s_SlotTable[SlotCount] = {
            { RHI::RHITextureSlot::Albedo,         "Albedo",         "Base Color / Diffuse Reflectance",        TextureColorSpace::sRGB,   nri::Format::RGBA8_SRGB },
            { RHI::RHITextureSlot::Normal,         "Normal",         "Tangent Space Normal (+Y MikkTSpace)",    TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::Roughness,      "Roughness",      "Microfacet Roughness (Channel R)",        TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::Metallic,       "Metallic",       "Conductor / Dielectric Mask (Channel R)", TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::AO,             "AO",             "Ambient Occlusion (Channel R)",           TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::Emissive,       "Emissive",       "Self-Illumination Radiance",              TextureColorSpace::sRGB,   nri::Format::RGBA8_SRGB },
            { RHI::RHITextureSlot::Clearcoat,      "Clearcoat",      "Clearcoat Strength (R) / Gloss (G)",      TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::AnisotropyFlow, "AnisotropyFlow", "Anisotropy Direction Flow (RG)",          TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::DetailNormal,   "DetailNormal",   "Secondary Detail Normal Perturbation",    TextureColorSpace::Linear, nri::Format::RGBA8_UNORM },
            { RHI::RHITextureSlot::DetailMask,     "DetailMask",     "Secondary Detail Blend Mask",             TextureColorSpace::Linear, nri::Format::RGBA8_UNORM }
        };

        const TextureSlotContractInfo& GetSlotInfo(RHI::RHITextureSlot slot) {
            uint32_t idx = static_cast<uint32_t>(slot);
            if (idx >= SlotCount) idx = 0;
            return s_SlotTable[idx];
        }

        bool IsSRGBSlot(RHI::RHITextureSlot slot) {
            return GetSlotInfo(slot).ColorSpace == TextureColorSpace::sRGB;
        }

        std::string_view GetSlotName(RHI::RHITextureSlot slot) {
            return GetSlotInfo(slot).Name;
        }

    } // namespace TextureSlotContract

    // =========================================================================
    // 4. Color Space & Tone Mapping Ownership Contracts
    // =========================================================================
    namespace ColorSpaceContract {

        float LinearToSRGB(float linearValue) {
            if (linearValue <= 0.0031308f) {
                return linearValue * 12.92f;
            }
            return 1.055f * std::pow(linearValue, 1.0f / 2.4f) - 0.055f;
        }

        float SRGBToLinear(float srgbValue) {
            if (srgbValue <= 0.04045f) {
                return srgbValue / 12.92f;
            }
            return std::pow((srgbValue + 0.055f) / 1.055f, 2.4f);
        }

        glm::vec3 LinearToSRGB(const glm::vec3& linearColor) {
            return glm::vec3(
                LinearToSRGB(linearColor.r),
                LinearToSRGB(linearColor.g),
                LinearToSRGB(linearColor.b)
            );
        }

        glm::vec3 SRGBToLinear(const glm::vec3& srgbColor) {
            return glm::vec3(
                SRGBToLinear(srgbColor.r),
                SRGBToLinear(srgbColor.g),
                SRGBToLinear(srgbColor.b)
            );
        }

        glm::vec4 LinearToSRGB(const glm::vec4& linearColor) {
            return glm::vec4(
                LinearToSRGB(linearColor.r),
                LinearToSRGB(linearColor.g),
                LinearToSRGB(linearColor.b),
                linearColor.a // Alpha is always stored linear
            );
        }

        glm::vec4 SRGBToLinear(const glm::vec4& srgbColor) {
            return glm::vec4(
                SRGBToLinear(srgbColor.r),
                SRGBToLinear(srgbColor.g),
                SRGBToLinear(srgbColor.b),
                srgbColor.a // Alpha is always stored linear
            );
        }

    } // namespace ColorSpaceContract

    // =========================================================================
    // 5. Depth, Clip and Coordinate System Contracts
    // =========================================================================
    namespace DepthClipContract {

        void GetDepthRange(RendererBackendType backend, float& outNearZ, float& outFarZ) {
            if (backend == RendererBackendType::OpenGL_Compatibility) {
                outNearZ = -1.0f;
                outFarZ = 1.0f;
            } else {
                outNearZ = 0.0f;
                outFarZ = 1.0f;
            }
        }

        nri::CompareOp GetDefaultDepthCompareOp() {
            return nri::CompareOp::LESS;
        }

    } // namespace DepthClipContract

    // =========================================================================
    // 6. Surface Mode, Blending and Culling Contracts
    // =========================================================================
    namespace SurfaceBlendContract {

        RHI::RHIBlendAttachmentDesc GetBlendAttachmentDesc(SurfaceType surface) {
            switch (surface) {
                case SurfaceType::Opaque:
                case SurfaceType::Cutout:
                    return RHI::RHIBlendAttachmentDesc::Opaque();

                case SurfaceType::Transparent:
                    return RHI::RHIBlendAttachmentDesc::AlphaBlend();

                case SurfaceType::Additive:
                    return RHI::RHIBlendAttachmentDesc::Additive();

                default:
                    return RHI::RHIBlendAttachmentDesc::Opaque();
            }
        }

        RHI::RHIDepthStencilDesc GetDepthStencilDesc(SurfaceType surface, bool depthWriteOverride) {
            RHI::RHIDepthStencilDesc desc;
            desc.DepthTestEnable = true;
            desc.DepthCompareOp = nri::CompareOp::LESS;
            desc.DepthBoundsTest = false;
            desc.StencilEnable = false;
            desc.DepthStencilFormat = nri::Format::D32_SFLOAT;

            // Only Opaque and Cutout write depth; Transparent and Additive do not
            if (surface == SurfaceType::Transparent || surface == SurfaceType::Additive) {
                desc.DepthWriteEnable = false;
            } else {
                desc.DepthWriteEnable = depthWriteOverride;
            }

            return desc;
        }

        RHI::RHIRasterizerDesc GetRasterizerDesc(CullMode cull, bool wireframe) {
            RHI::RHIRasterizerDesc desc;
            desc.FillMode = wireframe ? nri::FillMode::WIREFRAME : nri::FillMode::SOLID;
            desc.FrontCounterClockwise = DepthClipContract::FrontCounterClockwise;
            desc.DepthClamp = false;
            desc.LineSmoothing = false;
            desc.ConservativeRaster = false;

            switch (cull) {
                case CullMode::Back:
                    desc.CullMode = nri::CullMode::BACK;
                    break;
                case CullMode::Front:
                    desc.CullMode = nri::CullMode::FRONT;
                    break;
                case CullMode::None:
                    desc.CullMode = nri::CullMode::NONE;
                    break;
                default:
                    desc.CullMode = nri::CullMode::BACK;
                    break;
            }

            return desc;
        }

    } // namespace SurfaceBlendContract

    // =========================================================================
    // 7. Capability and Fallback Reporting Contract
    // =========================================================================
    namespace CapabilityContract {

        bool ValidateMaterialCompatibility(RendererBackendType backend, const Material& material, std::string& outDiagnostic) {
            outDiagnostic.clear();

            if (backend == RendererBackendType::OpenGL_Compatibility) {
                if (material.GetUseCustomShader()) {
                    outDiagnostic = "OpenGL Compatibility: Custom HLSL shaders are not supported. Authored shader properties preserved; rendering via OpenGL standard forward PBR.";
                    return false;
                }
                if (material.GetRefractionEnabled()) {
                    outDiagnostic = "OpenGL Compatibility: Screen-space refraction copies are not supported. Authored refraction settings preserved; rendering solid dielectric.";
                    return false;
                }
            }

            return true;
        }

    } // namespace CapabilityContract

    // =========================================================================
    // 8. Occlusion and Shading Separation Contracts (M15-U4)
    // =========================================================================
    namespace OcclusionContract {

        namespace {
            bool HasSeparatedLighting(const ShadingComponents& comp) {
                return comp.SunDiffuse != glm::vec3(0) || comp.SunSpecular != glm::vec3(0) ||
                       comp.LocalDiffuse != glm::vec3(0) || comp.LocalSpecular != glm::vec3(0);
            }
        }

        float CombineDirectionalVisibility(float csmVisibility, float contactShadow,
                                           bool enableContactShadows, bool enableCSM) {
            const float csm = enableCSM ? std::clamp(csmVisibility, 0.0f, 1.0f) : 1.0f;
            const float cs  = enableContactShadows ? std::clamp(contactShadow, 0.0f, 1.0f) : 1.0f;
            // The minimum operator represents that both CSM and screen-space contact shadows
            // sample the occlusion of the same directional sun light source.
            // Using min(csm, cs) guarantees that:
            // 1. A point fully shadowed by CSM (csm = 0) remains 0 (no redundant/double darkening).
            // 2. Penumbra regions do not compound attenuation (e.g. 0.5 * 0.5 = 0.25 is avoided).
            // 3. When contact shadows are disabled or cs = 1.0, csm is preserved untouched.
            return std::min(csm, cs);
        }

        glm::vec3 EvaluateShading(const ShadingComponents& comp, float ambientOcclusion, float contactShadow) {
            const float ao = comp.EnableAO ? std::clamp(ambientOcclusion, 0.0f, 1.0f) : 1.0f;
            const glm::vec3 ambientTerm = comp.Ambient * ao;

            // Determine sun and local components with backwards-compatibility for U4-A:
            glm::vec3 sunDiff = comp.SunDiffuse;
            glm::vec3 sunSpec = comp.SunSpecular;
            glm::vec3 localDiff = comp.LocalDiffuse;
            glm::vec3 localSpec = comp.LocalSpecular;

            if (!HasSeparatedLighting(comp)) {
                // Backwards compatibility with U4-A: route the legacy direct
                // fields to the only available direct-light class. When a
                // caller explicitly says that no sun exists, those fields
                // represent local direct light rather than disappearing.
                if (comp.HasSun) {
                    sunDiff = comp.DirectDiffuse;
                    sunSpec = comp.DirectSpecular;
                } else {
                    localDiff = comp.DirectDiffuse;
                    localSpec = comp.DirectSpecular;
                }
            }

            // Directional sun contribution:
            glm::vec3 sunTerm{0.0f};
            if (comp.HasSun) {
                const float sunVisibility = CombineDirectionalVisibility(
                    comp.CSMVisibility, contactShadow, comp.EnableContactShadows, comp.EnableCSM);
                sunTerm = (sunDiff + sunSpec) * sunVisibility;
            }

            // Local light contribution (point, spot): NEVER attenuated by directional contact shadow
            const glm::vec3 localTerm = localDiff + localSpec;

            return ambientTerm + sunTerm + localTerm + comp.Emissive;
        }

        bool IsSeparateIndirectTargetRequired(DataflowMode mode) {
            // Under PreLightingSeparated forward shading, the forward pixel shader scales
            // ambient in GPU registers before accumulating with direct light and emissive.
            // A separate physical render target is NOT required.
            if (mode == DataflowMode::PreLightingSeparated) {
                return false;
            }
            // Under deferred post-lighting compositing, a separate indirect target is required
            // to avoid multiplying the direct and emissive terms by ambient occlusion.
            return true;
        }

        std::string_view GetIndirectTargetDecisionRationale() {
            return "DECISION: A separate physical indirect render target is NOT required for forward shading under the pre-lighting occlusion contract. "
                   "Evaluating ambient occlusion prior to or within forward shading allows in-place register modulation (Color = Ambient * AO + Direct + Emissive), "
                   "saving 16-36 MB of VRAM (RGBA16_FLOAT) and eliminating Multiple Render Target (MRT) write bandwidth and fullscreen composite blits. "
                   "Auxiliary indirect targets remain available optionally for decoupled diagnostics and debug readback.";
        }

        bool VerifyAmbientOnlyModulation(const ShadingComponents& comp) {
            const glm::vec3 shadedAt0 = EvaluateShading(comp, 0.0f, 1.0f);
            const glm::vec3 shadedAt1 = EvaluateShading(comp, 1.0f, 1.0f);

            // Emissive radiance must be completely unchanged by AO variation
            ShadingComponents emissiveOnly{};
            emissiveOnly.Emissive = comp.Emissive;
            if (EvaluateShading(emissiveOnly, 0.0f, 1.0f) != EvaluateShading(emissiveOnly, 1.0f, 1.0f)) {
                return false;
            }

            // Direct light (diffuse + specular) must be completely unchanged by AO variation
            ShadingComponents directOnly{};
            directOnly.DirectDiffuse = comp.DirectDiffuse;
            directOnly.DirectSpecular = comp.DirectSpecular;
            if (EvaluateShading(directOnly, 0.0f, 1.0f) != EvaluateShading(directOnly, 1.0f, 1.0f)) {
                return false;
            }

            // Difference between AO=1 and AO=0 must strictly equal the Ambient term
            const glm::vec3 delta = shadedAt1 - shadedAt0;
            constexpr float epsilon = 1e-6f;
            return std::abs(delta.x - comp.Ambient.x) < epsilon &&
                   std::abs(delta.y - comp.Ambient.y) < epsilon &&
                   std::abs(delta.z - comp.Ambient.z) < epsilon;
        }

        bool VerifySunOnlyContactModulation(const ShadingComponents& comp) {
            const glm::vec3 atContact1 = EvaluateShading(comp, 1.0f, 1.0f);
            const glm::vec3 atContact0 = EvaluateShading(comp, 1.0f, 0.0f);

            // 1. Ambient term must be completely unchanged by contact shadows
            ShadingComponents ambOnly{};
            ambOnly.Ambient = comp.Ambient;
            if (EvaluateShading(ambOnly, 1.0f, 1.0f) != EvaluateShading(ambOnly, 1.0f, 0.0f)) {
                return false;
            }

            // 2. Emissive term must be completely unchanged by contact shadows
            ShadingComponents emOnly{};
            emOnly.Emissive = comp.Emissive;
            if (EvaluateShading(emOnly, 1.0f, 1.0f) != EvaluateShading(emOnly, 1.0f, 0.0f)) {
                return false;
            }

            // 3. Local lights (point, spot) must be completely unchanged by contact shadows
            ShadingComponents localOnly{};
            localOnly.LocalDiffuse = comp.LocalDiffuse;
            localOnly.LocalSpecular = comp.LocalSpecular;
            localOnly.HasSun = false;
            if (EvaluateShading(localOnly, 1.0f, 1.0f) != EvaluateShading(localOnly, 1.0f, 0.0f)) {
                return false;
            }

            // 4. If Sun is present and contact shadows enabled, difference must affect only sun contribution
            if (comp.HasSun && comp.EnableContactShadows) {
                glm::vec3 sunContribution = comp.SunDiffuse + comp.SunSpecular;
                if (!HasSeparatedLighting(comp)) {
                    sunContribution = comp.DirectDiffuse + comp.DirectSpecular;
                }
                const glm::vec3 diff = atContact1 - atContact0;
                const float effectiveCSM = comp.EnableCSM ? std::clamp(comp.CSMVisibility, 0.0f, 1.0f) : 1.0f;
                const glm::vec3 expectedDrop = sunContribution * effectiveCSM;
                constexpr float eps = 1e-5f;
                if (std::abs(diff.x - expectedDrop.x) > eps ||
                    std::abs(diff.y - expectedDrop.y) > eps ||
                    std::abs(diff.z - expectedDrop.z) > eps) {
                    return false;
                }
            }
            return true;
        }

        bool VerifyNoCSMDoubleAttenuation(float csmVisibility, float contactShadow) {
            // Test 1: Fully shadowed by CSM (csm = 0.0) -> contact shadow cannot double-darken
            const float visAtZeroCSM = CombineDirectionalVisibility(0.0f, contactShadow, true, true);
            if (visAtZeroCSM != 0.0f) return false;

            // Test 2: In penumbra (csm = 0.5, cs = 0.5) -> combined must be 0.5, NOT 0.25 (avoids double attenuation)
            const float visPenumbra = CombineDirectionalVisibility(0.5f, 0.5f, true, true);
            if (visPenumbra != 0.5f) return false;

            // Test 3: Unshadowed CSM (csm = 1.0) -> contact shadow applies cleanly
            const float visAtFullCSM = CombineDirectionalVisibility(1.0f, contactShadow, true, true);
            if (std::abs(visAtFullCSM - std::clamp(contactShadow, 0.0f, 1.0f)) > 1e-6f) return false;

            // Test 4: Contact shadows disabled -> returns pure CSM visibility
            const float visDisabled = CombineDirectionalVisibility(csmVisibility, contactShadow, false, true);
            if (std::abs(visDisabled - std::clamp(csmVisibility, 0.0f, 1.0f)) > 1e-6f) return false;

            // Test 5: CSM disabled -> returns pure contact shadow visibility
            const float visCSMDisabled = CombineDirectionalVisibility(csmVisibility, contactShadow, true, false);
            if (std::abs(visCSMDisabled - std::clamp(contactShadow, 0.0f, 1.0f)) > 1e-6f) return false;

            return true;
        }

        bool VerifySunAbsenceBehavior(const ShadingComponents& comp) {
            ShadingComponents noSun = comp;
            noSun.HasSun = false;
            const glm::vec3 at1 = EvaluateShading(noSun, 1.0f, 1.0f);
            const glm::vec3 at0 = EvaluateShading(noSun, 1.0f, 0.0f);
            return at1 == at0;
        }

        bool VerifyContactDisabledBehavior(const ShadingComponents& comp) {
            ShadingComponents disabled = comp;
            disabled.EnableContactShadows = false;
            const glm::vec3 at1 = EvaluateShading(disabled, 1.0f, 1.0f);
            const glm::vec3 at0 = EvaluateShading(disabled, 1.0f, 0.0f);
            return at1 == at0;
        }

        // =====================================================================
        // M15-U4-C: Half-Resolution, Bilateral Reconstruction & Depth Edge Protection
        // =====================================================================
        Resolution2D ComputeHalfResolution(uint32_t fullWidth, uint32_t fullHeight) {
            return Resolution2D{
                std::max(1u, fullWidth >> 1),
                std::max(1u, fullHeight >> 1)
            };
        }

        BilateralTapResult EvaluateBilateralWeight(float centerZ, float tapZ, int tapOffsetX, int tapOffsetY) {
            static constexpr float spatialWeights[5] = { 0.06136f, 0.24477f, 0.38774f, 0.24477f, 0.06136f };
            const int clampedX = std::clamp(tapOffsetX, -2, 2) + 2;
            const int clampedY = std::clamp(tapOffsetY, -2, 2) + 2;
            const float spatialWeight = spatialWeights[clampedX] * spatialWeights[clampedY];

            if (!std::isfinite(centerZ) || !std::isfinite(tapZ))
                return {spatialWeight, 0.0f, 0.0f, true};

            const float depthDiff = std::abs(centerZ - tapZ);
            const float denom = 0.05f * std::max(std::abs(centerZ), 0.1f) + 1e-4f;
            const float depthWeight = std::exp(-depthDiff / denom);
            const float totalWeight = depthWeight < 1e-3f ? 0.0f : depthWeight * spatialWeight;

            BilateralTapResult result{};
            result.SpatialWeight = spatialWeight;
            result.DepthWeight = depthWeight;
            result.TotalWeight = totalWeight;
            result.IsEdgeRejected = (depthWeight < 1e-3f);
            return result;
        }

        bool VerifyHalfResolutionDimensions(uint32_t fullW, uint32_t fullH) {
            if (fullW == 0 || fullH == 0) return false;
            const Resolution2D half = ComputeHalfResolution(fullW, fullH);
            if (half.Width == 0 || half.Height == 0) return false;
            if (fullW > 1 && half.Width != (fullW >> 1)) return false;
            if (fullH > 1 && half.Height != (fullH >> 1)) return false;
            if (fullW == 1 && half.Width != 1) return false;
            if (fullH == 1 && half.Height != 1) return false;
            return true;
        }

        bool VerifyBilateralReconstructionStability() {
            // 1. Spatial kernel must integrate to 1.0
            float spatialSum = 0.0f;
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    spatialSum += EvaluateBilateralWeight(1.0f, 1.0f, x, y).SpatialWeight;
                }
            }
            if (std::abs(spatialSum - 1.0f) > 1e-4f) return false;

            // 2. On a flat/smooth surface (depthDiff = 0), depth weight is 1.0 and total weight sum is 1.0
            float totalFlatSum = 0.0f;
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    const auto tap = EvaluateBilateralWeight(2.5f, 2.5f, x, y);
                    if (std::abs(tap.DepthWeight - 1.0f) > 1e-5f) return false;
                    totalFlatSum += tap.TotalWeight;
                }
            }
            if (std::abs(totalFlatSum - 1.0f) > 1e-4f) return false;

            // 3. Peak weight must be at center (0, 0)
            const auto centerTap = EvaluateBilateralWeight(2.0f, 2.0f, 0, 0);
            const auto cornerTap = EvaluateBilateralWeight(2.0f, 2.0f, 2, 2);
            if (centerTap.TotalWeight <= cornerTap.TotalWeight) return false;

            return true;
        }

        bool VerifyDepthEdgeProtection(float centerZ, float foregroundZ, float backgroundZ) {
            // When center is foreground:
            // Foreground neighbor tap must have strong weight (near 1.0)
            const auto fgTap = EvaluateBilateralWeight(centerZ, foregroundZ, 1, 0);
            if (fgTap.DepthWeight < 0.8f || fgTap.IsEdgeRejected) return false;

            // Background neighbor tap across a depth discontinuity must be strongly rejected
            const auto bgTap = EvaluateBilateralWeight(centerZ, backgroundZ, 1, 0);
            if (!bgTap.IsEdgeRejected || bgTap.DepthWeight > 1e-3f) return false;

            return true;
        }

        bool VerifyOddResolutionSafety(uint32_t oddW, uint32_t oddH) {
            if (oddW == 0 || oddH == 0) return false;
            const Resolution2D half = ComputeHalfResolution(oddW, oddH);
            if (half.Width == 0 || half.Height == 0) return false;

            // Half-resolution must be non-zero and bounded by full resolution
            if (half.Width > oddW || half.Height > oddH) return false;

            // Compute dispatch thread groups: (halfW + 7) / 8
            const uint32_t groupsX = (half.Width + 7) / 8;
            const uint32_t groupsY = (half.Height + 7) / 8;
            const uint32_t totalThreadsX = groupsX * 8;
            const uint32_t totalThreadsY = groupsY * 8;
            if (totalThreadsX < half.Width || totalThreadsY < half.Height) return false;

            // Verify reciprocal texel size is positive and finite
            const float rcpW = 1.0f / static_cast<float>(half.Width);
            const float rcpH = 1.0f / static_cast<float>(half.Height);
            if (rcpW <= 0.0f || rcpH <= 0.0f || std::isinf(rcpW) || std::isinf(rcpH)) return false;

            // Boundary UV check: pixel (oddW - 1, oddH - 1)
            const float lastFullU = (static_cast<float>(oddW - 1) + 0.5f) / static_cast<float>(oddW);
            const float lastFullV = (static_cast<float>(oddH - 1) + 0.5f) / static_cast<float>(oddH);
            if (lastFullU < 0.0f || lastFullU > 1.0f || lastFullV < 0.0f || lastFullV > 1.0f) return false;

            return true;
        }

        // =====================================================================
        // M15-U4-D: Regression Fixtures & Invariance Contracts (U4-06.1 - U4-06.5)
        // =====================================================================
        bool VerifyNoSunRegression(const ShadingComponents& comp) {
            ShadingComponents noSun = comp;
            noSun.HasSun = false;
            const glm::vec3 at1 = EvaluateShading(noSun, 1.0f, 1.0f);
            const glm::vec3 at0 = EvaluateShading(noSun, 1.0f, 0.0f);
            const glm::vec3 atHalf = EvaluateShading(noSun, 0.5f, 0.0f);
            if (at1 != at0) return false;
            if (!std::isfinite(at1.x) || !std::isfinite(at1.y) || !std::isfinite(at1.z)) return false;
            if (!std::isfinite(atHalf.x) || !std::isfinite(atHalf.y) || !std::isfinite(atHalf.z)) return false;

            // Expected radiance is ambient * ao + local + emissive
            glm::vec3 expectedLocal = comp.LocalDiffuse + comp.LocalSpecular;
            if (!HasSeparatedLighting(comp)) {
                expectedLocal = comp.DirectDiffuse + comp.DirectSpecular;
            }
            const glm::vec3 expected = comp.Ambient + expectedLocal + comp.Emissive;
            constexpr float eps = 1e-5f;
            return glm::distance(at1, expected) < eps;
        }

        bool VerifyNoAORegression(const ShadingComponents& comp) {
            ShadingComponents noAO = comp;
            noAO.EnableAO = false;
            const glm::vec3 at0 = EvaluateShading(noAO, 0.0f, 1.0f);
            const glm::vec3 at1 = EvaluateShading(noAO, 1.0f, 1.0f);
            if (at0 != at1) return false;
            if (!std::isfinite(at0.x) || !std::isfinite(at0.y) || !std::isfinite(at0.z)) return false;

            // When AO is disabled, ambient is unattenuated (identical to AO=1 with EnableAO=true)
            ShadingComponents withAO = comp;
            withAO.EnableAO = true;
            const glm::vec3 normalAt1 = EvaluateShading(withAO, 1.0f, 1.0f);
            return at0 == normalAt1;
        }

        bool VerifyNoContactRegression(const ShadingComponents& comp) {
            ShadingComponents noContact = comp;
            noContact.EnableContactShadows = false;
            const glm::vec3 at0 = EvaluateShading(noContact, 1.0f, 0.0f);
            const glm::vec3 at1 = EvaluateShading(noContact, 1.0f, 1.0f);
            if (at0 != at1) return false;
            if (!std::isfinite(at0.x) || !std::isfinite(at0.y) || !std::isfinite(at0.z)) return false;

            // When contact is disabled, directional sun visibility is purely CSM
            ShadingComponents withContact = comp;
            withContact.EnableContactShadows = true;
            const glm::vec3 normalAt1 = EvaluateShading(withContact, 1.0f, 1.0f);
            return at0 == normalAt1;
        }

        bool VerifyEmissiveInvariance(const ShadingComponents& comp) {
            // 1. Pure emissive surface must remain strictly unchanged regardless of AO or contact shadows
            ShadingComponents emissiveOnly{};
            emissiveOnly.Emissive = comp.Emissive;
            const glm::vec3 e00 = EvaluateShading(emissiveOnly, 0.0f, 0.0f);
            const glm::vec3 e10 = EvaluateShading(emissiveOnly, 1.0f, 0.0f);
            const glm::vec3 e01 = EvaluateShading(emissiveOnly, 0.0f, 1.0f);
            const glm::vec3 e11 = EvaluateShading(emissiveOnly, 1.0f, 1.0f);
            if (e00 != comp.Emissive || e10 != comp.Emissive || e01 != comp.Emissive || e11 != comp.Emissive) {
                return false;
            }
            if (!std::isfinite(e00.x) || !std::isfinite(e00.y) || !std::isfinite(e00.z)) return false;

            // Compare the mixed scene against the same lighting without emission.
            // Finiteness alone would also accept occlusion-dependent emission.
            ShadingComponents withoutEmission = comp;
            withoutEmission.Emissive = glm::vec3(0.0f);
            for (float ao : {0.0f, 0.5f, 1.0f}) for (float contact : {0.0f, 0.5f, 1.0f}) {
                const auto actual = EvaluateShading(comp, ao, contact);
                const auto expected = EvaluateShading(withoutEmission, ao, contact) + comp.Emissive;
                for (int channel = 0; channel < 3; ++channel) {
                    if (!std::isfinite(actual[channel]) || !std::isfinite(expected[channel]) ||
                        std::abs(actual[channel] - expected[channel]) > 1e-5f * std::max(1.0f, std::abs(expected[channel]))) return false;
                }
            }

            return true;
        }

        bool VerifyUnoccludedSpecularInvariance(const ShadingComponents& comp) {
            // Specular highlight from directional sun or local light (unoccluded: CSM=1, contact=1)
            ShadingComponents specComp = comp;
            specComp.SunSpecular = comp.SunSpecular;
            specComp.LocalSpecular = comp.LocalSpecular;
            specComp.CSMVisibility = 1.0f;
            specComp.EnableCSM = true;
            specComp.EnableContactShadows = true;

            // Direct specular must be 100% invariant under AO variation
            const glm::vec3 sAtAO0 = EvaluateShading(specComp, 0.0f, 1.0f);
            const glm::vec3 sAtAO1 = EvaluateShading(specComp, 1.0f, 1.0f);
            // Delta between AO=0 and AO=1 must affect only Ambient, NOT specular
            const glm::vec3 delta = sAtAO1 - sAtAO0;
            constexpr float eps = 1e-5f;
            const auto expectedDelta = specComp.EnableAO ? specComp.Ambient : glm::vec3(0.0f);
            for (int channel = 0; channel < 3; ++channel) {
                if (!std::isfinite(sAtAO0[channel]) || !std::isfinite(sAtAO1[channel]) ||
                    std::abs(delta[channel] - expectedDelta[channel]) > eps * std::max(1.0f, std::abs(sAtAO1[channel]))) return false;
            }

            // Pure unoccluded specular highlights:
            ShadingComponents pureSpecular{};
            pureSpecular.SunSpecular = comp.SunSpecular;
            pureSpecular.LocalSpecular = comp.LocalSpecular;
            if (!HasSeparatedLighting(comp)) pureSpecular.DirectSpecular = comp.DirectSpecular;
            pureSpecular.HasSun = comp.HasSun;
            pureSpecular.CSMVisibility = 1.0f;
            const glm::vec3 p0 = EvaluateShading(pureSpecular, 0.0f, 1.0f);
            const glm::vec3 p1 = EvaluateShading(pureSpecular, 1.0f, 1.0f);
            if (p0 != p1) return false;
            if (!std::isfinite(p0.x) || !std::isfinite(p0.y) || !std::isfinite(p0.z)) return false;

            return true;
        }

        // =====================================================================
        // M15-U4-E: Normal-Map & Animated/Deformed Mesh Contracts (U4-07.1 - U4-07.2)
        // =====================================================================

        glm::vec3 EvaluateTangentSpaceNormal(const NormalMapSample& sample) {
            if (!std::isfinite(sample.TangentRG.x) || !std::isfinite(sample.TangentRG.y) ||
                !std::isfinite(sample.Strength)) return glm::vec3(0, 0, 1);

            const glm::vec2 xy = sample.TangentRG * 2.0f - 1.0f;
            const float z = std::sqrt(std::max(0.0f, 1.0f - glm::dot(xy, xy)));
            const glm::vec3 tangentN = glm::normalize(glm::vec3(xy * std::max(0.0f, sample.Strength), std::max(z, 1e-4f)));

            glm::vec3 N = sample.GeometricNormal;
            if (glm::dot(N, N) > 1e-12f) N = glm::normalize(N);
            else N = glm::vec3(0, 0, 1);

            glm::vec3 T = glm::vec3(sample.Tangent) - N * glm::dot(N, glm::vec3(sample.Tangent));
            if (glm::dot(T, T) > 1e-12f) {
                T = glm::normalize(T);
            } else {
                T = std::abs(N.z) < 0.99f ? glm::normalize(glm::cross(glm::vec3(0, 0, 1), N))
                                          : glm::normalize(glm::cross(glm::vec3(0, 1, 0), N));
            }

            const glm::vec3 B = glm::cross(N, T) * (sample.Tangent.w < 0.0f ? -1.0f : 1.0f);
            const glm::vec3 perturbed = T * tangentN.x + B * tangentN.y + N * tangentN.z;
            return glm::dot(perturbed, perturbed) > 1e-12f ? glm::normalize(perturbed) : N;
        }

        bool VerifyNormalMapOcclusionStability(const NormalMapSample& sample, const glm::mat4& viewMatrix,
                                               const glm::vec3& viewDir, const glm::vec3& lightDirView) {
            for (int c = 0; c < 3; ++c)
                if (!std::isfinite(viewDir[c]) || !std::isfinite(lightDirView[c])) return false;
            if (glm::length(viewDir) < 1e-6f || glm::length(lightDirView) < 1e-6f) return false;
            const auto V = glm::normalize(viewDir);
            const auto L = glm::normalize(lightDirView);
            const glm::vec3 worldN = EvaluateTangentSpaceNormal(sample);
            for (int c = 0; c < 3; ++c) if (!std::isfinite(worldN[c])) return false;

            const glm::vec3 viewN = glm::mat3(viewMatrix) * worldN;
            for (int c = 0; c < 3; ++c) if (!std::isfinite(viewN[c])) return false;
            const float viewNLen = glm::length(viewN);
            if (viewNLen < 1e-6f) return false;
            const glm::vec3 normViewN = viewN / viewNLen;

            // Check grazing angle / backfacing normal perturbation:
            // When normal map perturbs normal away from view (NdotV <= 0),
            // GTAO contract falls back to neutral ao = 1.0 to prevent black faceting.
            const float NdotV = glm::dot(normViewN, V);
            if (NdotV > 0.0f) {
                const auto axis = std::abs(V.y) < .9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                const glm::vec3 orthoAxis = glm::normalize(glm::cross(V, axis));
                const glm::vec3 projN = normViewN - orthoAxis * glm::dot(normViewN, orthoAxis);
                const float projLen = glm::length(projN);
                if (projLen >= 1e-4f) {
                    const float gamma = std::atan2(glm::dot(projN / projLen, glm::cross(orthoAxis, V)),
                                                   std::max(glm::dot(projN / projLen, V), 1e-6f));
                    if (!std::isfinite(gamma)) return false;
                }
            }

            // Contact shadow ray launch validity:
            const float NdotL = glm::dot(normViewN, L);
            const float bias = 0.01f;
            const glm::vec3 rayOffset = normViewN * (bias * 2.0f);
            for (int c = 0; c < 3; ++c) if (!std::isfinite(rayOffset[c])) return false;

            // Shading composition with normal-mapped lighting components:
            ShadingComponents comp{};
            comp.Ambient = glm::vec3(0.15f);
            comp.SunDiffuse = glm::vec3(0.8f) * std::max(0.0f, NdotL);
            comp.SunSpecular = glm::vec3(0.4f);
            comp.Emissive = glm::vec3(0.05f);
            comp.HasSun = true;
            comp.EnableAO = true;
            comp.EnableContactShadows = true;

            const glm::vec3 rad0 = EvaluateShading(comp, 0.0f, 1.0f);
            const glm::vec3 rad1 = EvaluateShading(comp, 1.0f, 1.0f);
            for (int c = 0; c < 3; ++c) {
                if (!std::isfinite(rad0[c]) || !std::isfinite(rad1[c])) return false;
            }
            if (glm::distance(rad1 - rad0, comp.Ambient) > 1e-5f) return false;

            return true;
        }

        void EvaluateDeformedVertex(const DeformedVertexSample& sample,
                                    glm::vec3& outPosition, glm::vec3& outNormal, glm::vec4& outTangent) {
            outPosition = glm::vec3(sample.DeformationTransform * glm::vec4(sample.RestPosition, 1.0f));

            const glm::mat3 M = glm::mat3(sample.DeformationTransform);
            const glm::vec3 a = M * glm::vec3(1, 0, 0);
            const glm::vec3 b = M * glm::vec3(0, 1, 0);
            const glm::vec3 c = M * glm::vec3(0, 0, 1);
            const float det = glm::dot(a, glm::cross(b, c));
            const float handedness = det < 0.0f ? -1.0f : 1.0f;

            const glm::vec3 nTrans = (glm::cross(b, c) * sample.RestNormal.x +
                                      glm::cross(c, a) * sample.RestNormal.y +
                                      glm::cross(a, b) * sample.RestNormal.z) * handedness;
            outNormal = glm::dot(nTrans, nTrans) > 1e-12f ? glm::normalize(nTrans) : glm::vec3(0, 1, 0);

            const glm::vec3 tTrans = M * glm::vec3(sample.RestTangent);
            outTangent = glm::vec4(glm::dot(tTrans, tTrans) > 1e-12f ? glm::normalize(tTrans) : glm::vec3(1, 0, 0),
                                   sample.RestTangent.w * handedness);
        }

        bool VerifyDeformedMeshOcclusionStability(const DeformedVertexSample& sample, float backgroundDepth,
                                                 const glm::mat4& viewMatrix, const glm::mat4& projMatrix) {
            if (!std::isfinite(backgroundDepth) || backgroundDepth <= 0) return false;
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
                if (!std::isfinite(projMatrix[c][r]) || !std::isfinite(viewMatrix[c][r]) ||
                    !std::isfinite(sample.DeformationTransform[c][r])) return false;
            const float determinant = glm::determinant(projMatrix);
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f) return false;
            glm::vec3 P{0.0f}, N{0.0f};
            glm::vec4 T{0.0f};
            EvaluateDeformedVertex(sample, P, N, T);

            for (int c = 0; c < 3; ++c) {
                if (!std::isfinite(P[c]) || !std::isfinite(N[c]) || !std::isfinite(T[c])) return false;
            }
            if (!std::isfinite(T.w)) return false;
            if (std::abs(glm::dot(N, glm::vec3(T))) > 1e-3f) return false;

            const glm::vec3 Pview = glm::vec3(viewMatrix * glm::vec4(P, 1.0f));
            const float Z = -Pview.z;
            if (!std::isfinite(Z) || Z <= 0.0f) return false;

            const auto clip = projMatrix * glm::vec4(Pview, 1);
            if (!std::isfinite(clip.w) || clip.w <= 1e-6f) return false;
            const auto reconstructed = glm::inverse(projMatrix) * glm::vec4(glm::vec3(clip) / clip.w, 1);
            if (!std::isfinite(reconstructed.w) || std::abs(reconstructed.w) < 1e-6f) return false;
            const auto error = glm::distance(glm::vec3(reconstructed) / reconstructed.w, Pview);
            if (!std::isfinite(error) || error > 1e-4f * std::max(1.0f, glm::length(Pview))) return false;

            // Bilateral filter edge rejection across disocclusion boundaries
            const auto bgTap = EvaluateBilateralWeight(Z, backgroundDepth, 1, 0);
            if (!bgTap.IsEdgeRejected || bgTap.DepthWeight > 0.01f) return false;

            // Bilateral filter continuous reconstruction on surface
            const auto contTap = EvaluateBilateralWeight(Z, Z, 1, 0);
            if (contTap.IsEdgeRejected || contTap.TotalWeight <= 0.0f) return false;

            return true;
        }

        float EvaluateGTAODistanceFalloff(float distance, float worldRadius, float falloffStartFraction) {
            if (!std::isfinite(distance) || !std::isfinite(worldRadius) || !std::isfinite(falloffStartFraction) ||
                distance <= 0.0f || worldRadius <= 0.0f) {
                return 0.0f;
            }
            const float safeRadius = std::max(worldRadius, 0.05f);
            if (distance >= safeRadius) return 0.0f;
            const float startFrac = std::clamp(falloffStartFraction, 0.05f, 0.95f);
            const float startDist = safeRadius * startFrac;
            const float t = std::clamp((safeRadius - distance) / std::max(safeRadius - startDist, 1e-4f), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float ComputeSpecularOcclusion(float NdotV, float ao, float roughness) {
            if (!std::isfinite(NdotV) || !std::isfinite(ao) || !std::isfinite(roughness)) return 1.0f;
            const float safeAO = std::clamp(ao, 0.0f, 1.0f);
            const float safeNdotV = std::clamp(NdotV, 0.0f, 1.0f);
            const float safeRoughness = std::clamp(roughness, 0.04f, 1.0f);
            const float exponent = std::exp2(-16.0f * safeRoughness - 1.0f);
            const float term = std::pow(std::max(safeNdotV + safeAO, 1e-5f), exponent) - 1.0f + safeAO;
            return std::clamp(term, 0.0f, 1.0f);
        }

        glm::vec3 ComputeMultiBounceAO(float ao, const glm::vec3& albedo, float strength) {
            if (!std::isfinite(ao) || !std::isfinite(strength)) return glm::vec3(1.0f);
            const float safeAO = std::clamp(ao, 0.0f, 1.0f);
            const float safeStrength = std::clamp(strength, 0.0f, 1.0f);
            const glm::vec3 safeAlbedo = glm::clamp(glm::vec3(
                std::isfinite(albedo.x) ? albedo.x : 0.0f,
                std::isfinite(albedo.y) ? albedo.y : 0.0f,
                std::isfinite(albedo.z) ? albedo.z : 0.0f), glm::vec3(0.0f), glm::vec3(1.0f));
            const glm::vec3 a = 2.0404f * safeAlbedo - 0.3324f;
            const glm::vec3 b = -4.7951f * safeAlbedo + 0.6417f;
            const glm::vec3 c = 2.7552f * safeAlbedo + 0.6903f;
            const glm::vec3 bounced = glm::clamp(glm::max(glm::vec3(safeAO), ((safeAO * a + b) * safeAO + c) * safeAO),
                                                 glm::vec3(0.0f), glm::vec3(1.0f));
            return glm::mix(glm::vec3(safeAO), bounced, safeStrength);
        }

        NormalDepthBilateralTapResult EvaluateNormalDepthBilateralWeight(
            const glm::vec3& centerPos, const glm::vec3& centerNormal,
            const glm::vec3& tapPos, const glm::vec3& tapNormal, bool hasTapNormal,
            const glm::vec2& deltaFullResPx) {
            NormalDepthBilateralTapResult result{};
            for (int i = 0; i < 3; ++i) {
                if (!std::isfinite(centerPos[i]) || !std::isfinite(centerNormal[i]) ||
                    !std::isfinite(tapPos[i]) || (hasTapNormal && !std::isfinite(tapNormal[i]))) {
                    result.IsEdgeRejected = true;
                    return result;
                }
            }
            if (!std::isfinite(deltaFullResPx.x) || !std::isfinite(deltaFullResPx.y) ||
                glm::dot(centerNormal, centerNormal) < 1e-8f) {
                result.IsEdgeRejected = true;
                return result;
            }
            const glm::vec3 nCenter = glm::normalize(centerNormal);
            result.SpatialWeight = std::exp(-glm::dot(deltaFullResPx, deltaFullResPx) / (2.0f * 2.8f * 2.8f));

            const float centerZ = std::abs(centerPos.z);
            const float depthDiff = std::abs(centerZ - std::abs(tapPos.z));
            const float planeDist = std::abs(glm::dot(tapPos - centerPos, nCenter));
            const float effectiveDist = std::min(planeDist, depthDiff);
            const float depthSigma = 0.02f * std::max(centerZ, 0.1f) + 0.005f;
            result.DepthWeight = std::exp(-effectiveDist / depthSigma);

            float normalWeight = 1.0f;
            if (hasTapNormal && glm::dot(tapNormal, tapNormal) >= 1e-8f) {
                const float ndot = glm::dot(nCenter, glm::normalize(tapNormal));
                if (ndot < 0.25f) {
                    result.NormalWeight = 0.0f;
                    result.IsEdgeRejected = true;
                    return result;
                }
                normalWeight = std::pow(std::clamp(ndot, 0.0f, 1.0f), 12.0f);
            }
            result.NormalWeight = normalWeight;

            if (result.DepthWeight < 1e-3f) {
                result.IsEdgeRejected = true;
                return result;
            }
            result.TotalWeight = result.SpatialWeight * result.DepthWeight * result.NormalWeight;
            return result;
        }

    } // namespace OcclusionContract

    // =========================================================================
    // 9. Hi-Z Depth Pyramid Contracts (M15-U5-A)
    // =========================================================================
    namespace HiZContract {

        uint32_t ComputeUsefulLevelCount(uint32_t fullWidth, uint32_t fullHeight) {
            return RHI::RHIViewTargets::ComputeUsefulHiZLevelCount(fullWidth, fullHeight);
        }

        uint32_t ComputeAllocatedLevelCount(uint32_t fullWidth, uint32_t fullHeight) {
            return RHI::RHIViewTargets::ComputeAllocatedHiZLevelCount(fullWidth, fullHeight);
        }

        PyramidExtent ComputeLevelExtent(uint32_t fullWidth, uint32_t fullHeight, uint32_t level) {
            if (fullWidth == 0 || fullHeight == 0 || level >= MaxLevels) return {};
            return PyramidExtent{
                std::max(1u, fullWidth >> level),
                std::max(1u, fullHeight >> level)
            };
        }

        std::string_view GetDepthConventionSummary() {
            return "Modern RHI depth [0.0, 1.0]: 0.0 is near plane, 1.0 is far/sky, and Hi-Z stores the conservative minimum depth (nearest blocker) across covered texels.";
        }

        std::string_view GetEdgePolicySummary() {
            return "Level extents halve via max(1u, prev >> 1). When source extent > 1 is odd, the final destination column/row folds source coordinate (2 * dst + 2 = src - 1) into the min reduction (up to 3x3 at bottom-right corners); out-of-range taps on 1-pixel axes clamp to src - 1.";
        }

        bool BuildMinReductionPyramid(const std::vector<float>& depthLevel0,
                                      uint32_t width, uint32_t height, uint32_t levelCount,
                                      std::vector<std::vector<float>>& outLevels,
                                      std::vector<PyramidExtent>& outExtents) {
            outLevels.clear();
            outExtents.clear();
            if (width == 0 || height == 0 || levelCount == 0 || levelCount > MaxLevels) return false;
            const size_t expectedCount = size_t(width) * size_t(height);
            if (depthLevel0.size() != expectedCount) return false;
            for (float d : depthLevel0) {
                if (!std::isfinite(d) || d < NearDepth || d > FarDepth) return false;
            }

            outLevels.reserve(levelCount);
            outExtents.reserve(levelCount);
            outLevels.push_back(depthLevel0);
            outExtents.push_back({width, height});

            for (uint32_t level = 1; level < levelCount; ++level) {
                const auto& src = outLevels[level - 1];
                const auto srcExt = outExtents[level - 1];
                const uint32_t dstW = std::max(1u, srcExt.Width >> 1);
                const uint32_t dstH = std::max(1u, srcExt.Height >> 1);
                std::vector<float> dst(size_t(dstW) * size_t(dstH), FarDepth);

                if (srcExt.Width == 1 && srcExt.Height == 1) {
                    dst[0] = src[0];
                } else {
                    auto loadSrc = [&](uint32_t sx, uint32_t sy) {
                        const uint32_t cx = std::min(sx, srcExt.Width - 1u);
                        const uint32_t cy = std::min(sy, srcExt.Height - 1u);
                        return src[size_t(cy) * srcExt.Width + cx];
                    };
                    const bool oddW = srcExt.Width > 1u && (srcExt.Width & 1u) != 0u;
                    const bool oddH = srcExt.Height > 1u && (srcExt.Height & 1u) != 0u;
                    for (uint32_t y = 0; y < dstH; ++y) {
                        const bool extraY = oddH && (y + 1u == dstH);
                        const uint32_t maxOY = extraY ? 2u : 1u;
                        for (uint32_t x = 0; x < dstW; ++x) {
                            const bool extraX = oddW && (x + 1u == dstW);
                            const uint32_t maxOX = extraX ? 2u : 1u;
                            float minVal = FarDepth;
                            for (uint32_t oy = 0; oy <= maxOY; ++oy) {
                                for (uint32_t ox = 0; ox <= maxOX; ++ox) {
                                    minVal = std::min(minVal, loadSrc(x * 2u + ox, y * 2u + oy));
                                }
                            }
                            dst[size_t(y) * dstW + x] = minVal;
                        }
                    }
                }
                outLevels.push_back(std::move(dst));
                outExtents.push_back({dstW, dstH});
            }
            return true;
        }

        bool VerifyConservativeMinReduction(const std::vector<float>& depthLevel0,
                                            uint32_t width, uint32_t height) {
            const uint32_t useful = ComputeUsefulLevelCount(width, height);
            if (useful == 0) return false;
            std::vector<std::vector<float>> levels;
            std::vector<PyramidExtent> extents;
            if (!BuildMinReductionPyramid(depthLevel0, width, height, useful, levels, extents)) return false;

            const auto& lastExt = extents.back();
            if (lastExt.Width != 1 || lastExt.Height != 1) return false;

            float baseMin = FarDepth;
            for (float d : levels[0]) baseMin = std::min(baseMin, d);

            for (size_t l = 1; l < levels.size(); ++l) {
                const auto srcExt = extents[l - 1];
                const auto dstExt = extents[l];
                const bool oddW = srcExt.Width > 1u && (srcExt.Width & 1u) != 0u;
                const bool oddH = srcExt.Height > 1u && (srcExt.Height & 1u) != 0u;

                float levelMin = FarDepth;
                for (uint32_t y = 0; y < dstExt.Height; ++y) {
                    const uint32_t syEnd = (oddH && y + 1u == dstExt.Height) ? (srcExt.Height - 1u) : std::min(srcExt.Height - 1u, y * 2u + 1u);
                    for (uint32_t x = 0; x < dstExt.Width; ++x) {
                        const uint32_t sxEnd = (oddW && x + 1u == dstExt.Width) ? (srcExt.Width - 1u) : std::min(srcExt.Width - 1u, x * 2u + 1u);
                        const float coarseVal = levels[l][size_t(y) * dstExt.Width + x];
                        if (!std::isfinite(coarseVal) || coarseVal < NearDepth || coarseVal > FarDepth) return false;
                        levelMin = std::min(levelMin, coarseVal);
                        for (uint32_t sy = y * 2u; sy <= syEnd; ++sy) {
                            for (uint32_t sx = x * 2u; sx <= sxEnd; ++sx) {
                                if (coarseVal > levels[l - 1][size_t(sy) * srcExt.Width + sx] + 1e-6f) {
                                    return false;
                                }
                            }
                        }
                    }
                }
                if (std::abs(levelMin - baseMin) > 1e-6f) return false;
            }
            return true;
        }

        bool VerifyOddResolutionPyramid(uint32_t width, uint32_t height) {
            const uint32_t useful = ComputeUsefulLevelCount(width, height);
            const uint32_t allocated = ComputeAllocatedLevelCount(width, height);
            if (useful == 0 || allocated < SSRBindingLevels || allocated < useful) return false;
            if (ComputeLevelExtent(width, height, useful - 1) != PyramidExtent{1, 1}) return false;

            std::vector<float> testDepth(size_t(width) * size_t(height), 0.85f);
            // Place nearest blocker strictly on the bottom-right odd border corner.
            testDepth[size_t(height - 1u) * width + (width - 1u)] = 0.04f;
            if (!VerifyConservativeMinReduction(testDepth, width, height)) return false;

            std::vector<std::vector<float>> levels;
            std::vector<PyramidExtent> extents;
            if (!BuildMinReductionPyramid(testDepth, width, height, allocated, levels, extents)) return false;
            for (uint32_t l = 0; l < allocated; ++l) {
                if (extents[l] != ComputeLevelExtent(width, height, l)) return false;
                if (levels[l].back() != 0.04f) return false;
            }
            return true;
        }

    } // namespace HiZContract

    // =========================================================================
    // Reflection Capture Contract (M15-U5-B)
    // =========================================================================
    namespace ReflectionCaptureContract {
        namespace {
            bool IsFiniteVec3(const glm::vec3& v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            }

            void SetReason(std::string* outReason, const char* msg) {
                if (outReason) {
                    *outReason = msg;
                }
            }
        } // namespace

        CubeFaceBasis GetCubeFaceBasis(uint32_t faceIndex) {
            CubeFaceBasis basis;
            basis.FaceIndex = faceIndex;
            basis.FlipHorizontalForCubemapCopy = true;
            switch (faceIndex) {
                case 0u: // +X
                    basis.Forward = { 1.0f, 0.0f, 0.0f };
                    basis.Up = { 0.0f, 1.0f, 0.0f };
                    basis.Right = { 0.0f, 0.0f, -1.0f };
                    break;
                case 1u: // -X
                    basis.Forward = { -1.0f, 0.0f, 0.0f };
                    basis.Up = { 0.0f, 1.0f, 0.0f };
                    basis.Right = { 0.0f, 0.0f, 1.0f };
                    break;
                case 2u: // +Y
                    basis.Forward = { 0.0f, 1.0f, 0.0f };
                    basis.Up = { 0.0f, 0.0f, -1.0f };
                    basis.Right = { 1.0f, 0.0f, 0.0f };
                    break;
                case 3u: // -Y
                    basis.Forward = { 0.0f, -1.0f, 0.0f };
                    basis.Up = { 0.0f, 0.0f, 1.0f };
                    basis.Right = { 1.0f, 0.0f, 0.0f };
                    break;
                case 4u: // +Z
                    basis.Forward = { 0.0f, 0.0f, 1.0f };
                    basis.Up = { 0.0f, 1.0f, 0.0f };
                    basis.Right = { 1.0f, 0.0f, 0.0f };
                    break;
                case 5u: // -Z
                    basis.Forward = { 0.0f, 0.0f, -1.0f };
                    basis.Up = { 0.0f, 1.0f, 0.0f };
                    basis.Right = { -1.0f, 0.0f, 0.0f };
                    break;
                default:
                    basis.Forward = glm::vec3(0.0f);
                    basis.Up = glm::vec3(0.0f);
                    basis.Right = glm::vec3(0.0f);
                    basis.FlipHorizontalForCubemapCopy = false;
                    break;
            }
            return basis;
        }

        bool ValidateCubeFaceBasis(const CubeFaceBasis& basis, std::string* outReason) {
            if (basis.FaceIndex >= FaceCount) {
                SetReason(outReason, "cube face index out of range [0, 5]");
                return false;
            }
            if (!IsFiniteVec3(basis.Forward) || !IsFiniteVec3(basis.Up) || !IsFiniteVec3(basis.Right)) {
                SetReason(outReason, "cube face basis contains non-finite vector components");
                return false;
            }
            const float fLen = glm::length(basis.Forward);
            const float uLen = glm::length(basis.Up);
            const float rLen = glm::length(basis.Right);
            if (std::abs(fLen - 1.0f) > 1e-4f || std::abs(uLen - 1.0f) > 1e-4f || std::abs(rLen - 1.0f) > 1e-4f) {
                SetReason(outReason, "cube face basis vectors must have unit length");
                return false;
            }
            if (std::abs(glm::dot(basis.Forward, basis.Up)) > 1e-4f ||
                std::abs(glm::dot(basis.Forward, basis.Right)) > 1e-4f ||
                std::abs(glm::dot(basis.Up, basis.Right)) > 1e-4f) {
                SetReason(outReason, "cube face basis vectors must be mutually orthogonal");
                return false;
            }
            // D3D12/Vulkan TextureCube faces use a left-handed tangent frame: Right == Up x Forward.
            // Rasterization uses right-handed lookAtRH (+X_cam = Forward x Up = -Right), so
            // FlipHorizontalForCubemapCopy must be enabled to align captured faces into TextureCube.
            const glm::vec3 expectedCubemapRight = glm::cross(basis.Up, basis.Forward);
            if (glm::dot(expectedCubemapRight, basis.Right) <= 0.999f) {
                SetReason(outReason, "cube face right vector does not match left-handed cubemap tangent (Up x Forward)");
                return false;
            }
            if (!basis.FlipHorizontalForCubemapCopy) {
                SetReason(outReason, "right-handed cubemap capture requires FlipHorizontalForCubemapCopy");
                return false;
            }
            const CubeFaceBasis canonical = GetCubeFaceBasis(basis.FaceIndex);
            if (glm::dot(basis.Forward, canonical.Forward) < 0.9999f ||
                glm::dot(basis.Up, canonical.Up) < 0.9999f ||
                glm::dot(basis.Right, canonical.Right) < 0.9999f) {
                SetReason(outReason, "cube face basis does not match canonical TextureCube face orientation");
                return false;
            }
            SetReason(outReason, "");
            return true;
        }

        bool ValidateCapturePolicy(const ReflectionCapturePolicy& policy, std::string* outReason) {
            if (policy.SampleCount != 1u) {
                SetReason(outReason, "reflection capture target must be single-sample (SampleCount == 1)");
                return false;
            }
            if (policy.ColorFormat != nri::Format::RGBA16_SFLOAT) {
                SetReason(outReason, "reflection capture color target must be linear HDR RGBA16_FLOAT");
                return false;
            }
            if (policy.DepthFormat != nri::Format::D32_SFLOAT) {
                SetReason(outReason, "reflection capture depth target must be D32_FLOAT");
                return false;
            }
            const bool isPowerOfTwo = (policy.Resolution != 0u) && ((policy.Resolution & (policy.Resolution - 1u)) == 0u);
            if (policy.Resolution < MinResolution || policy.Resolution > MaxResolution || !isPowerOfTwo) {
                SetReason(outReason, "reflection capture resolution must be a power of two in [16, 1024]");
                return false;
            }
            if (!std::isfinite(policy.NearPlane) || !std::isfinite(policy.FarPlane) ||
                policy.NearPlane < MinNearPlane || policy.FarPlane <= policy.NearPlane) {
                SetReason(outReason, "reflection capture clip planes must satisfy 0.01 <= NearPlane < FarPlane");
                return false;
            }
            if (policy.AllowSSR || policy.AllowProbeSampling) {
                SetReason(outReason, "reflection capture must isolate SSR and recursive reflection probe sampling");
                return false;
            }
            if (!policy.IsolateMainViewTAA) {
                SetReason(outReason, "reflection capture must isolate main-view TAA history");
                return false;
            }
            if (policy.IncludeEditorOverlays) {
                SetReason(outReason, "reflection capture must exclude Editor overlays, wireframes, and debug lines");
                return false;
            }
            SetReason(outReason, "");
            return true;
        }

        PerspectiveCamera BuildCubeFaceCamera(
            const glm::vec3& probePosition,
            uint32_t faceIndex,
            float nearPlane,
            float farPlane) {
            const CubeFaceBasis basis = GetCubeFaceBasis(faceIndex < FaceCount ? faceIndex : 0u);
            const float safeNear = std::isfinite(nearPlane) ? std::max(nearPlane, MinNearPlane) : 0.1f;
            const float safeFar = (std::isfinite(farPlane) && farPlane > safeNear) ? farPlane : (safeNear + 100.0f);
            PerspectiveCamera cam(FaceFovDegrees, FaceAspectRatio, safeNear, safeFar);
            const glm::mat4 view = glm::lookAtRH(probePosition, probePosition + basis.Forward, basis.Up);
            cam.SetViewMatrix(view, probePosition);
            return cam;
        }

        SceneRenderSettings SanitizeCaptureSettings(
            const SceneRenderSettings& mainSettings,
            const ReflectionCapturePolicy& policy) {
            SceneRenderSettings out = mainSettings;
            out.MSAASamples = 1u;
            out.EnableToneMapping = false;
            out.ToneMappingMode = 3; // Linear bypass (shaders also observe capture linear HDR mode)
            out.EnableFXAA = false;
            out.EnableBloom = false;
            out.EnableAutoExposure = false;
            out.EnableTAA = false;
            out.EnableSSR = policy.AllowSSR;
            out.EnableGTAO = false;
            out.EnableContactShadows = false;
            out.EnableVolumetricFog = false;
            out.EnableDDGI = false;
            out.HiZVisualizationLevel = -1;
            out.Wireframe = policy.IncludeEditorOverlays ? mainSettings.Wireframe : false;
            out.ShowPhysicsDebug = policy.IncludeEditorOverlays ? mainSettings.ShowPhysicsDebug : false;
            out.EnableSkybox = policy.IncludeSkybox && mainSettings.EnableSkybox;
            out.EnableParticles = policy.IncludeParticles && mainSettings.EnableParticles;
            out.EnableShadows = policy.IncludeSelectedShadows && mainSettings.EnableShadows;
            return out;
        }

        RenderWorld FilterCaptureWorld(
            const RenderWorld& sourceWorld,
            const ReflectionCapturePolicy& policy) {
            RenderWorld out;
            out.Camera = sourceWorld.Camera;
            out.DirectionalLight = sourceWorld.DirectionalLight;
            out.AdditionalDirectionalLights = sourceWorld.AdditionalDirectionalLights;
            out.PointLights = sourceWorld.PointLights;
            out.SpotLights = sourceWorld.SpotLights;
            out.RectLights = sourceWorld.RectLights;
            out.Skybox = sourceWorld.Skybox;
            out.Skybox.Enabled = policy.IncludeSkybox && sourceWorld.Skybox.Enabled;
            out.EntityToStableUUID = sourceWorld.EntityToStableUUID;
            out.SceneRevision = sourceWorld.SceneRevision;
            out.SceneInstanceID = sourceWorld.SceneInstanceID;
            out.SceneLifetimeEpoch = sourceWorld.SceneLifetimeEpoch;
            out.TotalEntities = sourceWorld.TotalEntities;

            if (!policy.AllowProbeSampling) {
                out.ReflectionProbes.clear();
            } else {
                out.ReflectionProbes = sourceWorld.ReflectionProbes;
            }
            if (policy.IncludeParticles) {
                out.Particles = sourceWorld.Particles;
            }
            if (policy.IncludeEditorOverlays) {
                out.DebugLines = sourceWorld.DebugLines;
            }

            if (!(policy.IncludeSelectedShadows && policy.IncludeDirectionalShadows)) {
                if (out.DirectionalLight.has_value()) {
                    out.DirectionalLight->CastsShadows = false;
                }
                for (auto& dl : out.AdditionalDirectionalLights) {
                    dl.CastsShadows = false;
                }
            }
            if (!(policy.IncludeSelectedShadows && policy.IncludeLocalShadows)) {
                for (auto& pl : out.PointLights) {
                    pl.Shadows.CastShadows = false;
                }
                for (auto& sl : out.SpotLights) {
                    sl.Shadows.CastShadows = false;
                }
            }

            std::unordered_set<uint64_t> excludedIds;
            if (policy.ExcludeEntityID != 0u) {
                excludedIds.insert(policy.ExcludeEntityID);
            }
            for (uint64_t id : policy.AdditionalExcludedEntityIDs) {
                if (id != 0u) {
                    excludedIds.insert(id);
                }
            }

            if (policy.IncludeOpaqueAndCutout) {
                out.Skins = sourceWorld.Skins;
            }

            out.Meshes.reserve(sourceWorld.Meshes.size());
            for (const auto& mesh : sourceWorld.Meshes) {
                if (mesh.EntityID != 0u && excludedIds.count(static_cast<uint64_t>(mesh.EntityID)) > 0u) {
                    continue;
                }
                const ResolvedSurface surface = MaterialResolver::Resolve(mesh.Material);
                const bool isTransparent =
                    (surface.Surface == SurfaceType::Transparent || surface.Surface == SurfaceType::Additive);
                if (isTransparent && !policy.IncludeTransparent) {
                    continue;
                }
                if (!isTransparent && !policy.IncludeOpaqueAndCutout) {
                    continue;
                }
                out.Meshes.push_back(mesh);
            }

            return out;
        }

        uint32_t ComputePrefilterMipCount(uint32_t resolution) {
            if (resolution == 0u) {
                return 0u;
            }
            uint32_t chainDepth = 0u;
            for (uint32_t dim = resolution; dim > 0u; dim >>= 1u) {
                ++chainDepth;
            }
            return std::min(MaxPrefilterMipCount, std::max(1u, chainDepth));
        }

        float ComputePrefilterRoughness(uint32_t mip, uint32_t mipCount) {
            if (mipCount <= 1u || mip == 0u) {
                return 0.0f;
            }
            const uint32_t clampedMip = std::min(mip, mipCount - 1u);
            return std::clamp(
                static_cast<float>(clampedMip) / static_cast<float>(mipCount - 1u),
                0.0f, 1.0f);
        }

        bool ValidatePrefilterPolicy(const ReflectionPrefilterPolicy& policy, std::string* outReason) {
            if (policy.ColorFormat != nri::Format::RGBA16_SFLOAT) {
                SetReason(outReason, "reflection prefilter cubemap must use linear HDR RGBA16_FLOAT");
                return false;
            }
            const bool isPowerOfTwo = (policy.Resolution != 0u) && ((policy.Resolution & (policy.Resolution - 1u)) == 0u);
            if (policy.Resolution < MinResolution || policy.Resolution > MaxResolution || !isPowerOfTwo) {
                SetReason(outReason, "reflection prefilter resolution must be a power of two in [16, 1024]");
                return false;
            }
            uint32_t fullChainDepth = 0u;
            for (uint32_t dim = policy.Resolution; dim > 0u; dim >>= 1u) {
                ++fullChainDepth;
            }
            const uint32_t maxAllowedMips = std::min(MaxPrefilterMipCount, fullChainDepth);
            if (policy.MipCount == 0u || policy.MipCount > maxAllowedMips) {
                SetReason(outReason, "reflection prefilter mip count exceeds allowed [1, 5] cube chain");
                return false;
            }
            if (policy.SampleCount < MinPrefilterSampleCount || policy.SampleCount > MaxPrefilterSampleCount) {
                SetReason(outReason, "reflection prefilter sample count out of bounds [16, 1024]");
                return false;
            }
            SetReason(outReason, "");
            return true;
        }

        glm::vec3 ComputeCubeTexelDirection(uint32_t faceIndex, float u, float v) {
            if (faceIndex >= FaceCount || !std::isfinite(u) || !std::isfinite(v)) {
                return glm::vec3(0.0f);
            }
            const CubeFaceBasis basis = GetCubeFaceBasis(faceIndex);
            const float x = 2.0f * u - 1.0f;
            const float y = 2.0f * v - 1.0f;
            return glm::normalize(basis.Forward + x * basis.Right - y * basis.Up);
        }

        glm::vec3 SampleCubemapBilinear3D(
            const std::array<std::vector<glm::vec3>, FaceCount>& faces,
            uint32_t faceResolution,
            const glm::vec3& direction) {
            if (faceResolution == 0u) {
                return glm::vec3(0.0f);
            }
            const size_t expectedPixels = static_cast<size_t>(faceResolution) * faceResolution;
            for (const auto& face : faces) {
                if (face.size() != expectedPixels) {
                    return glm::vec3(0.0f);
                }
            }
            const float len = glm::length(direction);
            if (!std::isfinite(len) || len <= 1e-7f) {
                return glm::vec3(0.0f);
            }
            const glm::vec3 d = direction / len;
            const float absX = std::abs(d.x);
            const float absY = std::abs(d.y);
            const float absZ = std::abs(d.z);

            uint32_t face = 0u;
            float sc = 0.0f;
            float tc = 0.0f;
            float ma = 1.0f;
            if (absX >= absY && absX >= absZ) {
                ma = absX;
                if (d.x >= 0.0f) {
                    face = 0u; sc = -d.z; tc = -d.y;
                } else {
                    face = 1u; sc = d.z; tc = -d.y;
                }
            } else if (absY >= absX && absY >= absZ) {
                ma = absY;
                if (d.y >= 0.0f) {
                    face = 2u; sc = d.x; tc = d.z;
                } else {
                    face = 3u; sc = d.x; tc = -d.z;
                }
            } else {
                ma = absZ;
                if (d.z >= 0.0f) {
                    face = 4u; sc = d.x; tc = -d.y;
                } else {
                    face = 5u; sc = -d.x; tc = -d.y;
                }
            }

            const float u = std::clamp(0.5f * (sc / ma + 1.0f), 0.0f, 1.0f);
            const float v = std::clamp(0.5f * (tc / ma + 1.0f), 0.0f, 1.0f);
            const float fx = u * static_cast<float>(faceResolution) - 0.5f;
            const float fy = v * static_cast<float>(faceResolution) - 0.5f;
            const int x0 = static_cast<int>(std::floor(fx));
            const int y0 = static_cast<int>(std::floor(fy));
            const int x1 = x0 + 1;
            const int y1 = y0 + 1;
            const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
            const float ty = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);

            const auto tap = [&](int x, int y) {
                if (x >= 0 && y >= 0 && x < int(faceResolution) && y < int(faceResolution))
                    return faces[face][size_t(y) * faceResolution + x];
                const auto faceTap = [&](const glm::vec3& dir, uint32_t f) {
                    const auto basis = GetCubeFaceBasis(f);
                    const float major = glm::dot(dir, basis.Forward);
                    const int ix = std::clamp(int((glm::dot(dir, basis.Right) / major + 1) * .5f * faceResolution), 0, int(faceResolution)-1);
                    const int iy = std::clamp(int((1 - glm::dot(dir, basis.Up) / major) * .5f * faceResolution), 0, int(faceResolution)-1);
                    return faces[f][size_t(iy) * faceResolution + ix];
                };
                if ((x < 0 || x >= int(faceResolution)) && (y < 0 || y >= int(faceResolution))) {
                    // A cube corner meets three faces. Average their corner texels
                    // symmetrically so the limit is independent of the major face.
                    const auto corner = ComputeCubeTexelDirection(face, x < 0 ? 0.0f : 1.0f, y < 0 ? 0.0f : 1.0f);
                    glm::vec3 sum(0);
                    for (uint32_t f = 0; f < FaceCount; ++f)
                        if (glm::dot(corner, GetCubeFaceBasis(f).Forward) > 0) sum += faceTap(corner, f);
                    return sum / 3.0f;
                }
                // Extend the bilinear footprint across the cube edge, rather
                // than clamping it to one face and introducing a reference seam.
                const auto direction = ComputeCubeTexelDirection(face, (x + .5f) / faceResolution, (y + .5f) / faceResolution);
                uint32_t adjacent = 0;
                float major = -1;
                for (uint32_t f = 0; f < FaceCount; ++f) {
                    const float alignment = glm::dot(direction, GetCubeFaceBasis(f).Forward);
                    if (alignment > major) { major = alignment; adjacent = f; }
                }
                return faceTap(direction, adjacent);
            };
            const glm::vec3 c00 = tap(x0, y0), c10 = tap(x1, y0);
            const glm::vec3 c01 = tap(x0, y1), c11 = tap(x1, y1);
            return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
        }

        glm::vec3 EvaluatePrefilterDirectionCPU(
            const std::array<std::vector<glm::vec3>, FaceCount>& sourceFaces,
            uint32_t sourceResolution,
            const glm::vec3& normalDir,
            float roughness,
            uint32_t sampleCount) {
            if (sourceResolution == 0u || sampleCount == 0u || !IsFiniteVec3(normalDir) || !std::isfinite(roughness)) {
                return glm::vec3(0.0f);
            }
            const float nLen = glm::length(normalDir);
            if (!std::isfinite(nLen) || nLen <= 1e-7f) {
                return glm::vec3(0.0f);
            }
            const glm::vec3 N = normalDir / nLen;
            const float clampedRough = std::clamp(roughness, 0.0f, 1.0f);
            if (clampedRough <= 0.0f) {
                return glm::max(SampleCubemapBilinear3D(sourceFaces, sourceResolution, N), glm::vec3(0.0f));
            }

            const auto radicalInverse = [](uint32_t bits) -> float {
                bits = (bits << 16u) | (bits >> 16u);
                bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
                bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
                bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
                bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
                return static_cast<float>(bits) * 2.3283064365386963e-10f;
            };

            const glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 tangent = glm::normalize(glm::cross(up, N));
            const glm::vec3 bitangent = glm::cross(N, tangent);
            const float a = clampedRough * clampedRough;
            const float a2 = a * a;
            constexpr float kPi = 3.14159265358979323846f;

            glm::vec3 prefiltered(0.0f);
            float totalWeight = 0.0f;
            for (uint32_t i = 0u; i < sampleCount; ++i) {
                const float xiX = static_cast<float>(i) / static_cast<float>(sampleCount);
                const float xiY = radicalInverse(i);
                const float cosTheta = std::sqrt((1.0f - xiY) / std::max(1.0f + (a2 - 1.0f) * xiY, 1e-6f));
                const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
                const float phi = 2.0f * kPi * xiX;
                const glm::vec3 H = glm::normalize(
                    tangent * (std::cos(phi) * sinTheta) +
                    bitangent * (std::sin(phi) * sinTheta) +
                    N * cosTheta);
                const glm::vec3 L = glm::normalize(2.0f * glm::dot(N, H) * H - N);
                const float NdotL = glm::dot(N, L);
                if (NdotL > 0.0f) {
                    const glm::vec3 radiance = glm::max(SampleCubemapBilinear3D(sourceFaces, sourceResolution, L), glm::vec3(0.0f));
                    prefiltered += radiance * NdotL;
                    totalWeight += NdotL;
                }
            }
            return totalWeight > 0.0f ? (prefiltered / totalWeight) : glm::vec3(0.0f);
        }

        uint64_t EstimateCubemapTextureBytes(
            uint32_t resolution,
            uint32_t mipCount,
            nri::Format colorFormat) {
            if (resolution == 0u || mipCount == 0u) {
                return 0ull;
            }
            const uint64_t bytesPerTexel = (colorFormat == nri::Format::RGBA32_SFLOAT) ? 16ull : 8ull;
            uint64_t totalTexelsPerFace = 0ull;
            uint32_t dim = resolution;
            for (uint32_t mip = 0u; mip < mipCount; ++mip) {
                totalTexelsPerFace += static_cast<uint64_t>(dim) * dim;
                dim = std::max(1u, dim >> 1u);
            }
            return totalTexelsPerFace * FaceCount * bytesPerTexel;
        }

        uint64_t EstimateDynamicProbeCacheBytes(
            uint32_t resolution,
            uint32_t mipCount,
            nri::Format colorFormat) {
            // Each dynamic probe generation owns both its raw source cubemap and its GGX prefiltered cubemap.
            return 2ull * EstimateCubemapTextureBytes(resolution, mipCount, colorFormat);
        }

        bool ComputeLRUCacheEvictionPlan(
            const std::vector<CaptureCacheSlotInfo>& entries,
            uint64_t budgetBytes,
            uint64_t incomingBytes,
            std::vector<std::string>* outEvictKeys,
            std::string* outReason) {
            if (outEvictKeys) outEvictKeys->clear();
            if (incomingBytes > budgetBytes) {
                SetReason(outReason, "single dynamic probe capture exceeds cache budget");
                return false;
            }

            uint64_t totalExistingBytes = 0ull;
            std::vector<const CaptureCacheSlotInfo*> evictable;
            evictable.reserve(entries.size());
            for (const auto& entry : entries) {
                totalExistingBytes += entry.ByteSize;
                if (!entry.PinProtected) {
                    evictable.push_back(&entry);
                }
            }

            if (totalExistingBytes + incomingBytes <= budgetBytes) {
                SetReason(outReason, "");
                return true;
            }

            std::sort(evictable.begin(), evictable.end(),
                [](const CaptureCacheSlotInfo* a, const CaptureCacheSlotInfo* b) {
                    if (a->LastUsedSequence != b->LastUsedSequence) {
                        return a->LastUsedSequence < b->LastUsedSequence;
                    }
                    return a->ProbeKey < b->ProbeKey;
                });

            uint64_t currentBytes = totalExistingBytes;
            for (const CaptureCacheSlotInfo* candidate : evictable) {
                if (currentBytes + incomingBytes <= budgetBytes) {
                    break;
                }
                currentBytes = (candidate->ByteSize <= currentBytes) ? (currentBytes - candidate->ByteSize) : 0ull;
                if (outEvictKeys) {
                    outEvictKeys->push_back(candidate->ProbeKey);
                }
            }

            if (currentBytes + incomingBytes > budgetBytes) {
                if (outEvictKeys) outEvictKeys->clear();
                SetReason(outReason, "insufficient evictable cache capacity under active pins");
                return false;
            }
            SetReason(outReason, "");
            return true;
        }

        std::vector<ScheduledCaptureAllocation> ComputeFrameCaptureSchedule(
            const std::vector<ScheduledCaptureWork>& queue,
            uint32_t maxProbesPerFrame,
            uint32_t maxFacesPerFrame) {
            std::vector<ScheduledCaptureAllocation> plan;
            if (queue.empty() || maxProbesPerFrame == 0u || maxFacesPerFrame == 0u) {
                return plan;
            }

            std::vector<ScheduledCaptureWork> ordered = queue;
            std::stable_sort(ordered.begin(), ordered.end(),
                [](const ScheduledCaptureWork& a, const ScheduledCaptureWork& b) {
                    return a.PriorityScore > b.PriorityScore;
                });

            uint32_t remainingFacesBudget = maxFacesPerFrame;
            for (const auto& item : ordered) {
                if (plan.size() >= maxProbesPerFrame || remainingFacesBudget == 0u) {
                    break;
                }
                if (item.RemainingFaces == 0u) {
                    continue;
                }
                const uint32_t facesToRun = std::min(item.RemainingFaces, remainingFacesBudget);
                plan.push_back(ScheduledCaptureAllocation{item.ProbeKey, facesToRun});
                remainingFacesBudget -= facesToRun;
            }
            return plan;
        }
    } // namespace ReflectionCaptureContract

    namespace SSRRaymarchContract {
        static bool IsFiniteVec3(const glm::vec3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
        glm::vec3 ProjectViewToUVZ(const glm::vec3& viewPos, const glm::mat4& projZO) {
            const glm::vec4 clip = projZO * glm::vec4(viewPos, 1.0f);
            const float invW = 1.0f / std::max(std::abs(clip.w), 1e-6f);
            const glm::vec3 ndc = glm::vec3(clip) * invW;
            return glm::vec3(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f, ndc.z);
        }

        glm::vec3 ReconstructViewPos(const glm::vec2& uv, float depth, const glm::mat4& invProjZO) {
            const glm::vec4 clip(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
            const glm::vec4 viewPos = invProjZO * clip;
            const float invW = 1.0f / std::max(std::abs(viewPos.w), 1e-6f);
            return glm::vec3(viewPos) * invW;
        }

        glm::uvec2 ComputeHiZMipSize(uint32_t baseWidth, uint32_t baseHeight, uint32_t mip) {
            return glm::uvec2(
                std::max(1u, baseWidth >> std::min(mip, 15u)),
                std::max(1u, baseHeight >> std::min(mip, 15u)));
        }

        bool ClipScreenRay(const glm::vec3& startUVZ, const glm::vec3& endUVZ, float* outTMax) {
            if (!outTMax) return false;
            *outTMax = 0;
            // This contract returns an exit parameter only; the start must
            // already be inside the frustum, including after normal bias.
            if (!IsFiniteVec3(startUVZ) || !IsFiniteVec3(endUVZ) ||
                startUVZ.x < 0 || startUVZ.x > 1 || startUVZ.y < 0 || startUVZ.y > 1 ||
                startUVZ.z < 0 || startUVZ.z > .9999f) return false;
            float tMin = 0.0f;
            float tMax = 1.0f;
            const glm::vec3 d = endUVZ - startUVZ;
            const float eps = 1e-7f;
            const float mins[3]{0.0f, 0.0f, 0.0f};
            const float maxs[3]{1.0f, 1.0f, 0.9999f};
            for (int c = 0; c < 3; ++c) {
                if (std::abs(d[c]) < eps) {
                    if (startUVZ[c] < mins[c] || startUVZ[c] > maxs[c]) {
                        *outTMax = 0.0f;
                        return false;
                    }
                } else {
                    float t0 = (mins[c] - startUVZ[c]) / d[c];
                    float t1 = (maxs[c] - startUVZ[c]) / d[c];
                    if (t0 > t1) std::swap(t0, t1);
                    tMin = std::max(tMin, t0);
                    tMax = std::min(tMax, t1);
                    if (tMin > tMax) {
                        *outTMax = 0.0f;
                        return false;
                    }
                }
            }
            *outTMax = std::clamp(tMax, 0.0f, 1.0f);
            return *outTMax > 1e-5f;
        }

        RaySetup BuildInitialSSRRay(
            const glm::vec2& uv,
            float depth,
            const glm::vec3& viewNormal,
            const glm::mat4& projZO,
            const glm::mat4& invProjZO,
            uint32_t width,
            uint32_t height,
            uint32_t usefulHiZLevels,
            float maxDistance) {
            RaySetup setup{};
            if (width == 0 || height == 0 || !std::isfinite(depth) || !std::isfinite(maxDistance) ||
                !std::isfinite(uv.x) || !std::isfinite(uv.y) || !IsFiniteVec3(viewNormal) ||
                depth <= 0.0f || depth >= 0.9999f || maxDistance <= 0.0f) {
                return setup;
            }
            const glm::vec3 originView = ReconstructViewPos(uv, depth, invProjZO);
            const float normLen = glm::length(viewNormal);
            const float originLen = glm::length(originView);
            if (!std::isfinite(normLen) || !std::isfinite(originLen) || normLen < 1e-5f || originLen < 1e-5f) {
                return setup;
            }
            const glm::vec3 n = viewNormal / normLen;
            const glm::vec3 vDir = originView / originLen;
            const glm::vec3 reflDir = glm::normalize(glm::reflect(vDir, n));
            if (glm::dot(reflDir, n) <= 1e-4f) return setup;

            const float rcpW = 1.0f / static_cast<float>(width);
            const float rcpH = 1.0f / static_cast<float>(height);
            const float texelWorldSize = std::abs(originView.z) * std::max(
                rcpW / std::max(std::abs(projZO[0][0]), 1e-4f),
                rcpH / std::max(std::abs(projZO[1][1]), 1e-4f));
            const glm::vec3 startView = originView + n * std::max(0.0015f, texelWorldSize * 1.5f);

            const float nearViewZ = std::abs(ReconstructViewPos(glm::vec2(0.5f, 0.5f), 0.0f, invProjZO).z);
            const float safeNear = std::max(nearViewZ * 1.25f, 0.05f);
            if (startView.z >= -safeNear) {
                return setup;
            }

            const float distanceLimit = std::max(maxDistance, 5.0f);
            float traceDist = distanceLimit;
            if (reflDir.z > 1e-5f) {
                traceDist = std::min(traceDist, (-safeNear - startView.z) / reflDir.z);
            }
            if (traceDist <= 0.02f) {
                return setup;
            }

            const glm::vec3 fullEndView = startView + reflDir * traceDist;
            const glm::vec3 rawStartUVZ = ProjectViewToUVZ(startView, projZO);
            const glm::vec3 rawEndUVZ = ProjectViewToUVZ(fullEndView, projZO);
            float tMaxClip = 1.0f;
            if (!ClipScreenRay(rawStartUVZ, rawEndUVZ, &tMaxClip)) {
                return setup;
            }

            setup.OriginView = originView;
            setup.StartView = startView;
            setup.StartUVZ = rawStartUVZ;
            setup.EndUVZ = glm::mix(rawStartUVZ, rawEndUVZ, tMaxClip);
            // UVZ interpolation is projective, not a linear view-space distance.
            setup.EndView = ReconstructViewPos(glm::vec2(setup.EndUVZ), setup.EndUVZ.z, invProjZO);
            setup.MaxTraceDist = traceDist;
            setup.MaxDistance = distanceLimit;
            setup.StartMip = 0u;
            setup.MaxUsefulMip = usefulHiZLevels > 0u ? std::min(usefulHiZLevels - 1u, 7u) : 0u;
            setup.Valid = true;
            return setup;
        }

        HiZTraceResult TraceHiZDepthBuffer(
            const std::vector<std::vector<float>>& hiZPyramid,
            uint32_t width,
            uint32_t height,
            const RaySetup& ray,
            const glm::mat4& projZO,
            const glm::mat4& invProjZO,
            float thickness,
            uint32_t maxSteps) {
            (void)projZO;
            HiZTraceResult out{};
            if (!ray.Valid || hiZPyramid.empty() || width == 0 || height == 0 || maxSteps == 0) {
                return out;
            }
            maxSteps = std::clamp(maxSteps, 16u, 96u);
            const uint32_t maxAvailMip = std::min(
                ray.MaxUsefulMip,
                static_cast<uint32_t>(hiZPyramid.size() - 1u));
            const glm::vec3 dirUVZ = ray.EndUVZ - ray.StartUVZ;
            const float pxLen = std::max(
                1.0f,
                glm::length(glm::vec2(dirUVZ.x * static_cast<float>(width), dirUVZ.y * static_cast<float>(height))));
            const float minOriginT = std::min(0.5f, 1.25f / pxLen);
            float tCurr = minOriginT;
            uint32_t mip = std::min(ray.StartMip, maxAvailMip);
            const float safeThickness = std::max(thickness, 0.05f);

            const auto sampleLevel = [&](uint32_t level, const glm::uvec2& cell) -> float {
                const glm::uvec2 dims = ComputeHiZMipSize(width, height, level);
                if (hiZPyramid[level].size() != size_t(dims.x) * dims.y) return 1.0f;
                const uint32_t cx = std::min(cell.x, dims.x - 1u);
                const uint32_t cy = std::min(cell.y, dims.y - 1u);
                return hiZPyramid[level][size_t(cy) * dims.x + cx];
            };
            const auto trySampleContinuousHiZ0Depth = [&](const glm::vec2& sampleUV, float& outDepth) -> bool {
                const glm::vec2 tc = sampleUV * glm::vec2(static_cast<float>(width), static_cast<float>(height)) - glm::vec2(0.5f);
                const int maxX = static_cast<int>(width) - 1;
                const int maxY = static_cast<int>(height) - 1;
                const int x0 = std::clamp(static_cast<int>(std::floor(tc.x)), 0, maxX);
                const int y0 = std::clamp(static_cast<int>(std::floor(tc.y)), 0, maxY);
                const int x1 = std::min(x0 + 1, maxX);
                const int y1 = std::min(y0 + 1, maxY);
                const float fx = std::clamp(tc.x - static_cast<float>(x0), 0.0f, 1.0f);
                const float fy = std::clamp(tc.y - static_cast<float>(y0), 0.0f, 1.0f);
                const float d00 = sampleLevel(0u, glm::uvec2(x0, y0));
                const float d10 = sampleLevel(0u, glm::uvec2(x1, y0));
                const float d01 = sampleLevel(0u, glm::uvec2(x0, y1));
                const float d11 = sampleLevel(0u, glm::uvec2(x1, y1));
                const float dMin = std::min({d00, d10, d01, d11});
                const float dMax = std::max({d00, d10, d01, d11});
                if (dMax >= 0.9999f || dMin <= 0.0f) return false;
                const float zAtMin = ReconstructViewPos(sampleUV, dMin, invProjZO).z;
                const float zAtMax = ReconstructViewPos(sampleUV, dMax, invProjZO).z;
                const float maxContDiff = std::clamp(std::abs(zAtMin) * 0.08f + 0.05f, 0.05f, std::min(safeThickness * 0.65f, 0.35f));
                if (std::abs(zAtMin - zAtMax) > maxContDiff) return false;
                outDepth = glm::mix(glm::mix(d00, d10, fx), glm::mix(d01, d11, fx), fy);
                return true;
            };

            for (uint32_t step = 0; step < maxSteps && tCurr < 1.0f; ++step) {
                ++out.StepsExecuted;
                out.HighestMipReached = std::max(out.HighestMipReached, mip);

                const glm::vec3 pCurr = ray.StartUVZ + dirUVZ * tCurr;
                if (pCurr.x < 0.0f || pCurr.x > 1.0f || pCurr.y < 0.0f || pCurr.y > 1.0f || pCurr.z >= 0.9999f) {
                    break;
                }

                const glm::uvec2 dims = ComputeHiZMipSize(width, height, mip);
                const float cellScale = float(1u << mip);
                const glm::vec2 cellF(
                    std::clamp(pCurr.x * width / cellScale, 0.0f, static_cast<float>(dims.x) - 1e-4f),
                    std::clamp(pCurr.y * height / cellScale, 0.0f, static_cast<float>(dims.y) - 1e-4f));
                const glm::uvec2 cellIdx(static_cast<uint32_t>(std::floor(cellF.x)), static_cast<uint32_t>(std::floor(cellF.y)));

                // Reduction folds the odd border into the last cell; interior
                // boundaries remain multiples of 2^mip in base-pixel space.
                const float boundaryX = dirUVZ.x >= 0 && cellIdx.x + 1 == dims.x ? float(width) : (float(cellIdx.x) + (dirUVZ.x >= 0 ? 1 : 0)) * cellScale;
                const float boundaryY = dirUVZ.y >= 0 && cellIdx.y + 1 == dims.y ? float(height) : (float(cellIdx.y) + (dirUVZ.y >= 0 ? 1 : 0)) * cellScale;
                const float edgeX = boundaryX / static_cast<float>(width) +
                                    (dirUVZ.x >= 0.0f ? 1e-4f : -1e-4f) / static_cast<float>(width);
                const float edgeY = boundaryY / static_cast<float>(height) +
                                    (dirUVZ.y >= 0.0f ? 1e-4f : -1e-4f) / static_cast<float>(height);
                const float tx = std::abs(dirUVZ.x) > 1e-7f ? (edgeX - ray.StartUVZ.x) / dirUVZ.x : 1.0f;
                const float ty = std::abs(dirUVZ.y) > 1e-7f ? (edgeY - ray.StartUVZ.y) / dirUVZ.y : 1.0f;
                const float tNextCell = std::clamp(std::max(tCurr + 1e-5f, std::min(tx, ty)), tCurr, 1.0f);

                const float zEnter = ray.StartUVZ.z + dirUVZ.z * tCurr;
                const float zExit = ray.StartUVZ.z + dirUVZ.z * tNextCell;
                const float rayZMin = std::min(zEnter, zExit);
                const float rayZMax = std::max(zEnter, zExit);
                const float cellMinZ = sampleLevel(mip, cellIdx);

                if (rayZMax < cellMinZ || cellMinZ >= 0.9999f) {
                    tCurr = tNextCell;
                    mip = std::min(mip + 1u, maxAvailMip);
                    continue;
                }

                if (mip > 0u) {
                    if (zEnter < cellMinZ && dirUVZ.z > 1e-7f) {
                        const float tPlane = (cellMinZ - ray.StartUVZ.z) / dirUVZ.z;
                        tCurr = std::clamp(tPlane, tCurr, tNextCell);
                    }
                    --mip;
                    continue;
                }

                // Mip 0: check view-space penetration thickness and refine the cell crossing
                const glm::vec2 centerUV = (glm::vec2(cellIdx) + 0.5f) / glm::vec2(dims);
                const glm::vec3 sceneView = ReconstructViewPos(centerUV, cellMinZ, invProjZO);
                const glm::vec3 rayEnterView = ReconstructViewPos(glm::vec2(pCurr.x, pCurr.y), zEnter, invProjZO);
                const glm::vec3 rayExitUVZ = ray.StartUVZ + dirUVZ * tNextCell;
                const glm::vec3 rayExitView = ReconstructViewPos(glm::vec2(rayExitUVZ.x, rayExitUVZ.y), zExit, invProjZO);
                const float penEnter = sceneView.z - rayEnterView.z;
                const float penExit = sceneView.z - rayExitView.z;
                const bool crosses = (rayZMin <= cellMinZ && rayZMax >= cellMinZ);
                const bool withinBehind = (std::min(penEnter, penExit) <= safeThickness && std::max(penEnter, penExit) > -1e-4f);

                if (crosses || withinBehind) {
                    // Depth is constant within a mip-0 texel and UVZ is linear
                    // along this cell interval. Solve the crossing exactly for
                    // either depth direction; a fixed binary predicate assumes
                    // increasing depth and can reject camera-facing crossings.
                    const float tHit = crosses && std::abs(dirUVZ.z) > 1e-7f
                        ? std::clamp((cellMinZ-ray.StartUVZ.z)/dirUVZ.z,tCurr,tNextCell)
                        : tCurr;
                    glm::vec3 pFinal = ray.StartUVZ + dirUVZ * tHit;
                    glm::uvec2 fCell(
                        static_cast<uint32_t>(std::clamp(pFinal.x * static_cast<float>(width), 0.0f, static_cast<float>(width) - 1e-4f)),
                        static_cast<uint32_t>(std::clamp(pFinal.y * static_cast<float>(height), 0.0f, static_cast<float>(height) - 1e-4f)));
                    float finalSceneZ = sampleLevel(0u, fCell);
                    if (finalSceneZ < 0.9999f) {
                        glm::vec3 finalSceneView = ReconstructViewPos(glm::vec2(pFinal.x, pFinal.y), finalSceneZ, invProjZO);
                        const glm::vec3 finalRayView = ReconstructViewPos(glm::vec2(pFinal.x, pFinal.y), pFinal.z, invProjZO);
                        float finalPen = finalSceneView.z - finalRayView.z;
                        if (finalPen <= safeThickness && finalPen >= -0.05f) {
                            // Continuous sub-texel refinement across cliff-free mip-0 surfaces to remove
                            // discrete texel-row staircase beats and contact-seam ripples.
                            float tA = std::max(minOriginT, tCurr - 1.25f / pxLen);
                            float tB = tNextCell;
                            const glm::vec3 pA = ray.StartUVZ + dirUVZ * tA;
                            const glm::vec3 pB = ray.StartUVZ + dirUVZ * tB;
                            float contDA = 0.0f, contDB = 0.0f;
                            if (tB > tA + 1e-6f &&
                                trySampleContinuousHiZ0Depth(glm::vec2(pA.x, pA.y), contDA) &&
                                trySampleContinuousHiZ0Depth(glm::vec2(pB.x, pB.y), contDB)) {
                                const float zSceneA = ReconstructViewPos(glm::vec2(pA.x, pA.y), contDA, invProjZO).z;
                                const float zSceneB = ReconstructViewPos(glm::vec2(pB.x, pB.y), contDB, invProjZO).z;
                                float gapA = ReconstructViewPos(glm::vec2(pA.x, pA.y), pA.z, invProjZO).z - zSceneA;
                                float gapB = ReconstructViewPos(glm::vec2(pB.x, pB.y), pB.z, invProjZO).z - zSceneB;
                                if (std::abs(zSceneA - zSceneB) <= safeThickness &&
                                    gapA * gapB <= 0.0f &&
                                    std::abs(gapA) <= safeThickness &&
                                    std::abs(gapB) <= safeThickness &&
                                    std::abs(gapA - gapB) > 1e-6f) {
                                    for (int b = 0; b < 4; ++b) {
                                        const float tMid = 0.5f * (tA + tB);
                                        const glm::vec3 pMid = ray.StartUVZ + dirUVZ * tMid;
                                        float dMid = 0.0f;
                                        if (!trySampleContinuousHiZ0Depth(glm::vec2(pMid.x, pMid.y), dMid)) break;
                                        const float gapMid = ReconstructViewPos(glm::vec2(pMid.x, pMid.y), pMid.z, invProjZO).z -
                                                             ReconstructViewPos(glm::vec2(pMid.x, pMid.y), dMid, invProjZO).z;
                                        if ((gapMid >= 0.0f) == (gapA >= 0.0f)) {
                                            tA = tMid;
                                            gapA = gapMid;
                                        } else {
                                            tB = tMid;
                                            gapB = gapMid;
                                        }
                                    }
                                    const float alpha = std::clamp(std::abs(gapA) / std::max(std::abs(gapA - gapB), 1e-6f), 0.0f, 1.0f);
                                    const float tRefined = tA + (tB - tA) * alpha;
                                    const glm::vec3 pRefined = ray.StartUVZ + dirUVZ * tRefined;
                                    float dRefined = 0.0f;
                                    if (trySampleContinuousHiZ0Depth(glm::vec2(pRefined.x, pRefined.y), dRefined)) {
                                        const glm::vec3 refinedSceneView = ReconstructViewPos(glm::vec2(pRefined.x, pRefined.y), dRefined, invProjZO);
                                        const glm::vec3 refinedRayView = ReconstructViewPos(glm::vec2(pRefined.x, pRefined.y), pRefined.z, invProjZO);
                                        const float refinedPen = refinedSceneView.z - refinedRayView.z;
                                        if (refinedPen <= safeThickness && refinedPen >= -0.05f) {
                                            pFinal = pRefined;
                                            finalSceneZ = dRefined;
                                            finalSceneView = refinedSceneView;
                                            finalPen = refinedPen;
                                            fCell = glm::uvec2(
                                                static_cast<uint32_t>(std::clamp(pFinal.x * static_cast<float>(width), 0.0f, static_cast<float>(width) - 1e-4f)),
                                                static_cast<uint32_t>(std::clamp(pFinal.y * static_cast<float>(height), 0.0f, static_cast<float>(height) - 1e-4f)));
                                        }
                                    }
                                }
                            }
                            const auto smooth = [](float edge0, float edge1, float x) {
                                const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
                                return t * t * (3.0f - 2.0f * t);
                            };
                            const float fadeX = smooth(0.0f, 0.12f, pFinal.x) * (1.0f - smooth(0.88f, 1.0f, pFinal.x));
                            const float fadeY = smooth(0.0f, 0.12f, pFinal.y) * (1.0f - smooth(0.88f, 1.0f, pFinal.y));
                            const float screenFade = std::clamp(fadeX * fadeY, 0.0f, 1.0f);
                            const float stepFade = std::clamp(static_cast<float>(maxSteps - step) / 6.0f, 0.0f, 1.0f);
                            out.Hit = true;
                            out.HitUV = glm::vec2(pFinal.x, pFinal.y);
                            out.HitDepth = finalSceneZ;
                            float supportSum = 0.0f;
                            const float edgeTol = std::max(safeThickness * 1.5f, 0.35f);
                            for (const auto offset : {glm::ivec2(-1,0), glm::ivec2(1,0), glm::ivec2(0,-1), glm::ivec2(0,1)}) {
                                const auto cell = glm::clamp(glm::ivec2(fCell) + offset, glm::ivec2(0), glm::ivec2(width-1,height-1));
                                const float neighborDepth = sampleLevel(0, glm::uvec2(cell));
                                if (neighborDepth < .9999f) {
                                    const float neighborZ = ReconstructViewPos((glm::vec2(cell)+.5f)/glm::vec2(width,height), neighborDepth, invProjZO).z;
                                    supportSum += 1.0f - smooth(edgeTol * 0.35f, edgeTol * 1.35f, std::abs(finalSceneView.z - neighborZ));
                                }
                            }
                            const float disocclusionFade = 0.25f + 0.75f * smooth(0.2f, 2.2f, supportSum);
                            const auto reflection = glm::normalize(ray.EndView-ray.StartView);
                            const float cameraFade = 1-smooth(.45f,.98f,reflection.z);
                            out.HitDistance = glm::length(finalSceneView - ray.OriginView);
                            const float distanceFade = std::clamp(1-out.HitDistance/std::max(ray.MaxDistance,5.0f)*.25f,0.0f,1.0f);
                            out.Confidence = std::clamp(screenFade * stepFade * distanceFade * cameraFade * disocclusionFade, 0.0f, 1.0f);
                            return out;
                        }
                    }
                }

                tCurr = tNextCell;
                mip = 0u;
            }

            return out;
        }

        float EvaluateRoughnessFade(float roughness) {
            const float r = std::clamp(roughness, 0.04f, 1.0f);
            const float t = std::clamp((r - 0.65f) / (0.98f - 0.65f), 0.0f, 1.0f);
            return 1.0f - t * t * (3.0f - 2.0f * t);
        }

        float ComputeRoughnessConePixelRadius(
            float roughness,
            float hitDistanceMeters,
            float hitViewZ,
            const glm::mat4& projZO,
            uint32_t width,
            uint32_t height) {
            const float r = std::clamp(roughness, 0.04f, 1.0f);
            const float coneTan = std::clamp(r * r * 0.75f, 0.0f, 0.75f);
            const float worldRadius = coneTan * std::max(hitDistanceMeters, 0.0f);
            const float projScale = 0.5f * std::max(
                std::abs(projZO[0][0]) * static_cast<float>(width),
                std::abs(projZO[1][1]) * static_cast<float>(height));
            return std::clamp(projScale * worldRadius / std::max(std::abs(hitViewZ), 0.1f), 0.0f, MaxConeRadiusPixels);
        }

        glm::vec3 ComputeSpecularF0(const glm::vec3& baseColor, float metallic) {
            const float m = std::clamp(metallic, 0.0f, 1.0f);
            const glm::vec3 safeBase = glm::max(baseColor, glm::vec3(0.0f));
            return glm::mix(glm::vec3(0.04f), safeBase, m);
        }
    } // namespace SSRRaymarchContract

    namespace AreaLightLTCContract {
        namespace {
            constexpr float kPi = 3.14159265358979323846f;
            constexpr float kLocalIntensityScale = 0.05f;
            // Area-preserving 32-segment disk contour; bounded angular error also for sharp reflections.
            constexpr float kDiskRadiusScale = 1.00322263f; // sqrt(pi / (16 * sin(pi/16)))

            float SafeClampRoughness(float r) {
                return std::isfinite(r) ? std::clamp(r, 0.04f, 1.0f) : 0.5f;
            }

            float SafeClampNdotV(float nv) {
                return std::isfinite(nv) ? std::clamp(nv, 0.0f, 1.0f) : 0.5f;
            }

            glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
                const float lenSq = glm::dot(v, v);
                if (!std::isfinite(lenSq) || lenSq <= 1e-12f) {
                    return fallback;
                }
                return v * (1.0f / std::sqrt(lenSq));
            }

            float EvaluateLTCContourIntegral(
                const std::vector<glm::vec3>& shadingPoly,
                const glm::vec4& ltc1,
                bool twoSided) {
                if (shadingPoly.size() < 3) {
                    return 0.0f;
                }

                // First clip against the physical surface horizon (L.z >= 0 in shading tangent frame).
                std::vector<glm::vec3> upperPoly = ClipPolygonToHorizon(shadingPoly);
                if (upperPoly.size() < 3) {
                    return 0.0f;
                }

                // Apply inverse LTC matrix M^-1 (identity if ltc1 == (1, 0, 0, 1)).
                std::vector<glm::vec3> warpedPoly;
                warpedPoly.reserve(upperPoly.size());
                for (const glm::vec3& p : upperPoly) {
                    warpedPoly.emplace_back(
                        ltc1.x * p.x + ltc1.y * p.z,
                        p.y,
                        ltc1.z * p.x + ltc1.w * p.z);
                }

                // Clip against the warped clamped-cosine support plane (w.z >= 0).
                std::vector<glm::vec3> clipped = ClipPolygonToHorizon(warpedPoly);
                if (clipped.size() < 3) {
                    return 0.0f;
                }

                for (glm::vec3& v : clipped) {
                    const float lenSq = glm::dot(v, v);
                    v = (lenSq > 1e-12f) ? (v / std::sqrt(lenSq)) : glm::vec3(0.0f, 0.0f, 1.0f);
                }

                glm::vec3 sum(0.0f);
                const size_t n = clipped.size();
                for (size_t i = 0; i < n; ++i) {
                    sum += IntegrateEdgeVector(clipped[i], clipped[(i + 1) % n]);
                }

                const float formFactor = sum.z;
                if (!std::isfinite(formFactor)) {
                    return 0.0f;
                }
                return twoSided ? std::abs(formFactor) : std::max(0.0f, formFactor);
            }
        } // namespace

        namespace {
#define LTC_UINT3 glm::uvec3
#include "../../Resources/Shaders/LTCTables.inc"
#undef LTC_UINT3
        }

        glm::vec4 EvaluateLTC1GridPoint(uint32_t roughIdx, uint32_t viewIdx) {
            const auto p = kLTCPacked[std::min(viewIdx, kTableDim - 1u) * kTableDim + std::min(roughIdx, kTableDim - 1u)];
            return glm::vec4(glm::unpackHalf2x16(p.x), glm::unpackHalf2x16(p.y));
        }

        glm::vec4 EvaluateLTC2GridPoint(uint32_t roughIdx, uint32_t viewIdx) {
            const auto p = kLTCPacked[std::min(viewIdx, kTableDim - 1u) * kTableDim + std::min(roughIdx, kTableDim - 1u)];
            return glm::vec4(glm::unpackHalf2x16(p.z), 0.0f, 0.0f);
        }

        LTCTables BuildValidatedLTCTables() {
            LTCTables out;
            for (uint32_t iy = 0; iy < kTableDim; ++iy) {
                for (uint32_t ix = 0; ix < kTableDim; ++ix) {
                    const size_t idx = static_cast<size_t>(iy) * kTableDim + ix;
                    out.LTC1[idx] = EvaluateLTC1GridPoint(ix, iy);
                    out.LTC2[idx] = EvaluateLTC2GridPoint(ix, iy);
                }
            }
            return out;
        }

        bool ValidateLTCTables(const LTCTables& tables, std::string* outReason) {
            for (uint32_t iy = 0; iy < kTableDim; ++iy) {
                for (uint32_t ix = 0; ix < kTableDim; ++ix) {
                    const size_t idx = static_cast<size_t>(iy) * kTableDim + ix;
                    const glm::vec4& l1 = tables.LTC1[idx];
                    const glm::vec4& l2 = tables.LTC2[idx];
                    for (int c = 0; c < 4; ++c) {
                        if (!std::isfinite(l1[c]) || !std::isfinite(l2[c])) {
                            if (outReason) *outReason = "non-finite LTC table entry";
                            return false;
                        }
                    }
                    const float det = l1.x * l1.w - l1.y * l1.z;
                    if (!std::isfinite(det) || det <= 0.0f) {
                        if (outReason) *outReason = "singular or inverted LTC1 transform matrix";
                        return false;
                    }
                    if (l2.x <= 0.0f || l2.x > 1.02f || l2.y < 0.0f || l2.y > 1.0001f) {
                        if (outReason) *outReason = "LTC2 directional albedo out of physical [0, 1] bounds";
                        return false;
                    }
                }
            }
            // A GGX fit at roughness=1 is not a diffuse identity transform.
            // Accuracy is checked against independent GGX quadrature in contract tests.
            if (outReason) outReason->clear();
            return true;
        }

        glm::vec4 SampleLTC1(float roughness, float NdotV) {
            const float u = std::clamp(SafeClampRoughness(roughness), 0.0f, 1.0f);
            const float v = std::clamp(std::sqrt(std::max(0.0f, 1.0f - SafeClampNdotV(NdotV))), 0.0f, 1.0f);
            const float gx = u * static_cast<float>(kTableDim - 1u);
            const float gy = v * static_cast<float>(kTableDim - 1u);
            const uint32_t ix0 = std::min(static_cast<uint32_t>(std::floor(gx)), kTableDim - 1u);
            const uint32_t iy0 = std::min(static_cast<uint32_t>(std::floor(gy)), kTableDim - 1u);
            const uint32_t ix1 = std::min(ix0 + 1u, kTableDim - 1u);
            const uint32_t iy1 = std::min(iy0 + 1u, kTableDim - 1u);
            const float fx = gx - static_cast<float>(ix0);
            const float fy = gy - static_cast<float>(iy0);

            const glm::vec4 c00 = EvaluateLTC1GridPoint(ix0, iy0);
            const glm::vec4 c10 = EvaluateLTC1GridPoint(ix1, iy0);
            const glm::vec4 c01 = EvaluateLTC1GridPoint(ix0, iy1);
            const glm::vec4 c11 = EvaluateLTC1GridPoint(ix1, iy1);
            return glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);
        }

        glm::vec4 SampleLTC2(float roughness, float NdotV) {
            const float u = std::clamp(SafeClampRoughness(roughness), 0.0f, 1.0f);
            const float v = std::clamp(std::sqrt(std::max(0.0f, 1.0f - SafeClampNdotV(NdotV))), 0.0f, 1.0f);
            const float gx = u * static_cast<float>(kTableDim - 1u);
            const float gy = v * static_cast<float>(kTableDim - 1u);
            const uint32_t ix0 = std::min(static_cast<uint32_t>(std::floor(gx)), kTableDim - 1u);
            const uint32_t iy0 = std::min(static_cast<uint32_t>(std::floor(gy)), kTableDim - 1u);
            const uint32_t ix1 = std::min(ix0 + 1u, kTableDim - 1u);
            const uint32_t iy1 = std::min(iy0 + 1u, kTableDim - 1u);
            const float fx = gx - static_cast<float>(ix0);
            const float fy = gy - static_cast<float>(iy0);

            const glm::vec4 c00 = EvaluateLTC2GridPoint(ix0, iy0);
            const glm::vec4 c10 = EvaluateLTC2GridPoint(ix1, iy0);
            const glm::vec4 c01 = EvaluateLTC2GridPoint(ix0, iy1);
            const glm::vec4 c11 = EvaluateLTC2GridPoint(ix1, iy1);
            return glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);
        }

        glm::vec3 IntegrateEdgeVector(const glm::vec3& v1, const glm::vec3& v2) {
            const float x = std::clamp(glm::dot(v1, v2), -0.999999f, 0.999999f);
            const float y = std::abs(x);
            const float a = 0.8543985f + (0.4965155f + 0.0145206f * y) * y;
            const float b = 3.4175940f + (4.1616724f + y) * y;
            const float v = a / b;
            const float thetaSinTheta = (x > 0.0f)
                ? v
                : (0.5f / std::sqrt(std::max(1.0f - x * x, 1e-7f)) - v);
            return glm::cross(v1, v2) * thetaSinTheta;
        }

        std::vector<glm::vec3> ClipPolygonToHorizon(const std::vector<glm::vec3>& poly) {
            std::vector<glm::vec3> out;
            const size_t n = poly.size();
            if (n < 3) {
                return out;
            }
            out.reserve(n + 4);
            for (size_t i = 0; i < n; ++i) {
                const glm::vec3& curr = poly[i];
                const glm::vec3& next = poly[(i + 1) % n];
                const bool currAbove = (curr.z >= 0.0f);
                const bool nextAbove = (next.z >= 0.0f);
                if (currAbove) {
                    out.push_back(curr);
                    if (!nextAbove) {
                        const float denom = curr.z - next.z;
                        const float t = (std::abs(denom) > 1e-7f) ? (curr.z / denom) : 0.0f;
                        glm::vec3 clipped = glm::mix(curr, next, std::clamp(t, 0.0f, 1.0f));
                        clipped.z = 0.0f;
                        out.push_back(clipped);
                    }
                } else if (nextAbove) {
                    const float denom = next.z - curr.z;
                    const float t = (std::abs(denom) > 1e-7f) ? (-curr.z / denom) : 0.0f;
                    glm::vec3 clipped = glm::mix(curr, next, std::clamp(t, 0.0f, 1.0f));
                    clipped.z = 0.0f;
                    out.push_back(clipped);
                }
            }
            return out;
        }

        void ComputeOrthonormalAreaBasis(
            const glm::vec3& rawDir,
            const glm::vec3& rawUp,
            glm::vec3& outRight,
            glm::vec3& outUp,
            glm::vec3& outDir) {
            outDir = SafeNormalize(rawDir, glm::vec3(0.0f, 0.0f, -1.0f));
            const glm::vec3 fallbackUpAxis = (std::abs(outDir.y) < 0.99f)
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 orthoCandidate = rawUp - outDir * glm::dot(rawUp, outDir);
            outUp = SafeNormalize(orthoCandidate, SafeNormalize(glm::cross(fallbackUpAxis, outDir), glm::vec3(0.0f, 1.0f, 0.0f)));
            outRight = SafeNormalize(glm::cross(outDir, outUp), glm::vec3(1.0f, 0.0f, 0.0f));
            outUp = SafeNormalize(glm::cross(outRight, outDir), outUp);
        }

        float ComputeEmitterClosestDistance(const LightingRectFrame& light, const glm::vec3& worldPos) {
            glm::vec3 right, up, dir;
            ComputeOrthonormalAreaBasis(light.Direction, light.Up, right, up, dir);
            const float halfW = std::max(std::isfinite(light.Width) ? light.Width * 0.5f : 0.005f, 0.005f);
            const float halfH = std::max(std::isfinite(light.Height) ? light.Height * 0.5f : 0.005f, 0.005f);
            const glm::vec3 rel = worldPos - light.Position;
            float u = glm::dot(rel, right);
            float v = glm::dot(rel, up);
            if (light.Shape == AreaLightShape::Disk) {
                const float nx = u / halfW;
                const float ny = v / halfH;
                if (nx * nx + ny * ny > 1.0f) {
                    // Euclidean projection onto an ellipse, not radial projection.
                    const float a2 = halfW * halfW;
                    const float b2 = halfH * halfH;
                    float lo = 0.0f;
                    float hi = glm::length(glm::vec2(halfW * u, halfH * v));
                    for (int iteration = 0; iteration < 24; ++iteration) {
                        const float lambda = (lo + hi) * 0.5f;
                        const float x = halfW * u / (lambda + a2);
                        const float y = halfH * v / (lambda + b2);
                        if (x * x + y * y > 1.0f) lo = lambda;
                        else hi = lambda;
                    }
                    u = a2 * u / (hi + a2);
                    v = b2 * v / (hi + b2);
                }
            } else {
                u = std::clamp(u, -halfW, halfW);
                v = std::clamp(v, -halfH, halfH);
            }
            const glm::vec3 closestPt = light.Position + right * u + up * v;
            const float d = glm::length(worldPos - closestPt);
            return std::isfinite(d) ? d : 0.0f;
        }

        AreaLightEvaluation EvaluateLTCAreaLightCPU(
            const LightingRectFrame& light,
            const glm::vec3& worldPos,
            const glm::vec3& normalWS,
            const glm::vec3& viewDirWS,
            const glm::vec3& baseColor,
            float metallic,
            float roughness) {
            AreaLightEvaluation result;
            const float range = std::isfinite(light.Range) ? light.Range : 0.0f;
            const float absIntensity = std::isfinite(light.SignedIntensity) ? std::abs(light.SignedIntensity) : 0.0f;
            if (range <= 1e-3f || absIntensity <= 0.0f) {
                return result;
            }

            glm::vec3 lightRight, lightUp, lightDir;
            ComputeOrthonormalAreaBasis(light.Direction, light.Up, lightRight, lightUp, lightDir);

            const bool twoSided = (light.SignedIntensity < 0.0f);
            const float dirDot = glm::dot(worldPos - light.Position, lightDir);
            if (!twoSided && dirDot <= 0.0f) {
                return result;
            }

            const float closestDist = ComputeEmitterClosestDistance(light, worldPos);
            if (closestDist >= range) {
                return result;
            }
            const float ratio = std::clamp(closestDist / range, 0.0f, 1.0f);
            const float win = std::max(0.0f, 1.0f - ratio * ratio * ratio * ratio);
            result.RangeAttenuation = win * win;

            const glm::vec3 N = SafeNormalize(normalWS, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 V = SafeNormalize(viewDirWS, N);
            const float NdotV = std::clamp(glm::dot(N, V), 0.001f, 1.0f);
            const float safeRoughness = SafeClampRoughness(roughness);
            const float safeMetallic = std::clamp(metallic, 0.0f, 1.0f);

            const glm::vec3 tangentRaw = V - N * glm::dot(N, V);
            const glm::vec3 fallbackTangentAxis = (std::abs(N.y) < 0.99f)
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 T1 = SafeNormalize(tangentRaw, SafeNormalize(glm::cross(fallbackTangentAxis, N), glm::vec3(1.0f, 0.0f, 0.0f)));
            const glm::vec3 T2 = SafeNormalize(glm::cross(N, T1), glm::vec3(0.0f, 0.0f, 1.0f));

            const float halfW = std::max(std::isfinite(light.Width) ? light.Width * 0.5f : 0.005f, 0.005f);
            const float halfH = std::max(std::isfinite(light.Height) ? light.Height * 0.5f : 0.005f, 0.005f);

            std::vector<glm::vec3> shadingPoly;
            auto pushWorldVertex = [&](const glm::vec3& wp) {
                const glm::vec3 rel = wp - worldPos;
                shadingPoly.emplace_back(glm::dot(rel, T1), glm::dot(rel, T2), glm::dot(rel, N));
            };

            if (light.Shape == AreaLightShape::Disk) {
                shadingPoly.reserve(32);
                for (int k = 0; k < 32; ++k) {
                    const float angle = (static_cast<float>(k) + 0.5f) * (kPi * 0.0625f);
                    const float cx = std::cos(angle) * kDiskRadiusScale;
                    const float cy = std::sin(angle) * kDiskRadiusScale;
                    pushWorldVertex(light.Position + lightRight * (halfW * cx) + lightUp * (halfH * cy));
                }
            } else {
                shadingPoly.reserve(4);
                pushWorldVertex(light.Position - lightRight * halfW - lightUp * halfH);
                pushWorldVertex(light.Position + lightRight * halfW - lightUp * halfH);
                pushWorldVertex(light.Position + lightRight * halfW + lightUp * halfH);
                pushWorldVertex(light.Position - lightRight * halfW + lightUp * halfH);
            }

            result.DiffuseFormFactor = EvaluateLTCContourIntegral(shadingPoly, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), twoSided);
            const glm::vec4 ltc1 = SampleLTC1(safeRoughness, NdotV);
            const glm::vec4 ltc2 = SampleLTC2(safeRoughness, NdotV);
            result.SpecularFormFactor = EvaluateLTCContourIntegral(shadingPoly, ltc1, twoSided);

            const glm::vec3 safeAlbedo = glm::max(baseColor, glm::vec3(0.0f));
            const glm::vec3 F0 = SSRRaymarchContract::ComputeSpecularF0(safeAlbedo, safeMetallic);
            result.SpecularAlbedo = F0 * ltc2.x + (glm::vec3(1.0f) - F0) * ltc2.y;

            const glm::vec3 kD = glm::max(glm::vec3(1.0f) - result.SpecularAlbedo, glm::vec3(0.0f)) * (1.0f - safeMetallic);
            const glm::vec3 lightRadiance = glm::max(light.Color, glm::vec3(0.0f)) * (absIntensity * result.RangeAttenuation * kLocalIntensityScale);

            result.DiffuseRadiance = kD * safeAlbedo * result.DiffuseFormFactor * lightRadiance;
            result.SpecularRadiance = result.SpecularAlbedo * result.SpecularFormFactor * lightRadiance;
            result.TotalRadiance = result.DiffuseRadiance + result.SpecularRadiance;
            return result;
        }

        AreaLightEvaluation EvaluateAreaLightNumericalReferenceCPU(
            const LightingRectFrame& light,
            const glm::vec3& worldPos,
            const glm::vec3& normalWS,
            const glm::vec3& viewDirWS,
            const glm::vec3& baseColor,
            float metallic,
            float roughness,
            uint32_t samplesPerAxis) {
            AreaLightEvaluation result;
            const uint32_t gridN = std::clamp(samplesPerAxis, 8u, 128u);
            const float range = std::isfinite(light.Range) ? light.Range : 0.0f;
            const float absIntensity = std::isfinite(light.SignedIntensity) ? std::abs(light.SignedIntensity) : 0.0f;
            if (range <= 1e-3f || absIntensity <= 0.0f) {
                return result;
            }

            glm::vec3 lightRight, lightUp, lightDir;
            ComputeOrthonormalAreaBasis(light.Direction, light.Up, lightRight, lightUp, lightDir);

            const bool twoSided = (light.SignedIntensity < 0.0f);
            const float dirDot = glm::dot(worldPos - light.Position, lightDir);
            if (!twoSided && dirDot <= 0.0f) {
                return result;
            }

            const float closestDist = ComputeEmitterClosestDistance(light, worldPos);
            if (closestDist >= range) {
                return result;
            }
            const float ratio = std::clamp(closestDist / range, 0.0f, 1.0f);
            const float win = std::max(0.0f, 1.0f - ratio * ratio * ratio * ratio);
            result.RangeAttenuation = win * win;

            const glm::vec3 N = SafeNormalize(normalWS, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 V = SafeNormalize(viewDirWS, N);
            const float NdotV = std::clamp(glm::dot(N, V), 0.001f, 1.0f);
            const float safeRoughness = SafeClampRoughness(roughness);
            const float safeMetallic = std::clamp(metallic, 0.0f, 1.0f);
            const float alpha = safeRoughness * safeRoughness;
            const float a2 = alpha * alpha;

            const glm::vec3 safeAlbedo = glm::max(baseColor, glm::vec3(0.0f));
            const glm::vec3 F0 = SSRRaymarchContract::ComputeSpecularF0(safeAlbedo, safeMetallic);
            const float halfW = std::max(std::isfinite(light.Width) ? light.Width * 0.5f : 0.005f, 0.005f);
            const float halfH = std::max(std::isfinite(light.Height) ? light.Height * 0.5f : 0.005f, 0.005f);

            float diffuseSum = 0.0f;
            glm::vec3 specIntegral(0.0f);

            for (uint32_t iy = 0; iy < gridN; ++iy) {
                for (uint32_t ix = 0; ix < gridN; ++ix) {
                    glm::vec3 samplePos;
                    float sampleArea = 0.0f;
                    if (light.Shape == AreaLightShape::Disk) {
                        const float u = (static_cast<float>(ix) + 0.5f) / static_cast<float>(gridN);
                        const float v = (static_cast<float>(iy) + 0.5f) / static_cast<float>(gridN);
                        const float angle = 2.0f * kPi * v;
                        const float lx = halfW * u * std::cos(angle);
                        const float ly = halfH * u * std::sin(angle);
                        samplePos = light.Position + lightRight * lx + lightUp * ly;
                        const float dU = 1.0f / static_cast<float>(gridN);
                        const float dTheta = (2.0f * kPi) / static_cast<float>(gridN);
                        sampleArea = (halfW * halfH) * u * dU * dTheta;
                    } else {
                        const float u = ((static_cast<float>(ix) + 0.5f) / static_cast<float>(gridN)) * 2.0f - 1.0f;
                        const float v = ((static_cast<float>(iy) + 0.5f) / static_cast<float>(gridN)) * 2.0f - 1.0f;
                        samplePos = light.Position + lightRight * (halfW * u) + lightUp * (halfH * v);
                        sampleArea = (4.0f * halfW * halfH) / static_cast<float>(gridN * gridN);
                    }

                    const glm::vec3 toSample = samplePos - worldPos;
                    const float distSq = glm::dot(toSample, toSample);
                    if (distSq <= 1e-8f) {
                        continue;
                    }
                    const float invDist = 1.0f / std::sqrt(distSq);
                    const glm::vec3 L = toSample * invDist;
                    const float NdotL = std::max(glm::dot(N, L), 0.0f);
                    const float rawEmitterCos = glm::dot(-L, lightDir);
                    const float emitterCos = twoSided ? std::abs(rawEmitterCos) : std::max(rawEmitterCos, 0.0f);
                    if (NdotL <= 0.0f || emitterCos <= 0.0f) {
                        continue;
                    }

                    const float dOmega = (emitterCos * sampleArea) / distSq;
                    diffuseSum += (NdotL * dOmega) / kPi;

                    const glm::vec3 H = SafeNormalize(V + L, N);
                    const float NdotH = std::max(glm::dot(N, H), 0.0f);
                    const float VdotH = std::max(glm::dot(V, H), 0.0f);
                    const float denomD = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
                    const float D = a2 / std::max(kPi * denomD * denomD, 1e-8f);
                    const float ggxV = NdotL * std::sqrt(NdotV * NdotV * (1.0f - a2) + a2);
                    const float ggxL = NdotV * std::sqrt(NdotL * NdotL * (1.0f - a2) + a2);
                    const float vis = 0.5f / std::max(ggxV + ggxL, 1e-5f);
                    const glm::vec3 F = F0 + (glm::vec3(1.0f) - F0) * std::pow(std::clamp(1.0f - VdotH, 0.0f, 1.0f), 5.0f);
                    specIntegral += (D * vis * F) * NdotL * dOmega;
                }
            }

            result.DiffuseFormFactor = diffuseSum;
            const glm::vec4 ltc2 = SampleLTC2(safeRoughness, NdotV);
            result.SpecularAlbedo = F0 * ltc2.x + (glm::vec3(1.0f) - F0) * ltc2.y;
            const glm::vec3 safeDenom = glm::max(result.SpecularAlbedo, glm::vec3(1e-4f));
            result.SpecularFormFactor = (specIntegral.r / safeDenom.r + specIntegral.g / safeDenom.g + specIntegral.b / safeDenom.b) / 3.0f;

            const glm::vec3 kD = glm::max(glm::vec3(1.0f) - result.SpecularAlbedo, glm::vec3(0.0f)) * (1.0f - safeMetallic);
            const glm::vec3 lightRadiance = glm::max(light.Color, glm::vec3(0.0f)) * (absIntensity * result.RangeAttenuation * kLocalIntensityScale);

            result.DiffuseRadiance = kD * safeAlbedo * result.DiffuseFormFactor * lightRadiance;
            result.SpecularRadiance = specIntegral * lightRadiance;
            result.TotalRadiance = result.DiffuseRadiance + result.SpecularRadiance;
            return result;
        }
    } // namespace AreaLightLTCContract

    // =========================================================================
    // 9. Planar Reflection Surface, Mirrored Camera & Clipping Contracts (M15-U5-46)
    // =========================================================================
    namespace PlanarReflectionContract {
        namespace {
            bool IsFiniteVec3(const glm::vec3& v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            }

            bool IsFiniteMat4(const glm::mat4& m) {
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) {
                        if (!std::isfinite(m[c][r])) return false;
                    }
                }
                return true;
            }

            glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
                if (!IsFiniteVec3(v)) return fallback;
                const float lenSq = glm::dot(v, v);
                if (!std::isfinite(lenSq) || lenSq <= 1e-12f) return fallback;
                return v * (1.0f / std::sqrt(lenSq));
            }
        } // namespace

        PlanarReflectionSurface BuildSurfaceFromReflector(const RenderPlanarReflector& reflector) {
            PlanarReflectionSurface surface;
            surface.EntityID = reflector.EntityID;
            surface.StableUUID = reflector.StableUUID;
            surface.WorldTransform = reflector.WorldTransform;
            surface.Position = IsFiniteVec3(reflector.Position) ? reflector.Position : glm::vec3(0.0f);

            const glm::vec3 n = SafeNormalize(reflector.Normal, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 rawT = IsFiniteVec3(reflector.Tangent) ? reflector.Tangent : glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 orthoT = rawT - n * glm::dot(rawT, n);
            if (glm::dot(orthoT, orthoT) <= 1e-10f) {
                const glm::vec3 fallbackAxis = (std::abs(n.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                orthoT = glm::cross(fallbackAxis, n);
            }
            surface.Normal = n;
            surface.Tangent = SafeNormalize(orthoT, glm::vec3(1.0f, 0.0f, 0.0f));
            surface.Bitangent = SafeNormalize(glm::cross(surface.Normal, surface.Tangent), glm::vec3(0.0f, 0.0f, 1.0f));
            surface.HalfExtents = glm::vec2(
                std::isfinite(reflector.HalfExtents.x) ? std::max(0.01f, reflector.HalfExtents.x) : 5.0f,
                std::isfinite(reflector.HalfExtents.y) ? std::max(0.01f, reflector.HalfExtents.y) : 5.0f);
            surface.CaptureNearPlane = std::isfinite(reflector.CaptureNearPlane) ? std::max(0.01f, reflector.CaptureNearPlane) : 0.1f;
            surface.CaptureFarPlane = std::isfinite(reflector.CaptureFarPlane) ? std::max(surface.CaptureNearPlane + 0.1f, reflector.CaptureFarPlane) : 1000.0f;
            surface.ClipPlaneOffset = std::isfinite(reflector.ClipPlaneOffset) ? reflector.ClipPlaneOffset : 0.0f;
            surface.EdgeFadeDistance = std::isfinite(reflector.EdgeFadeDistance) ? std::max(0.0f, reflector.EdgeFadeDistance) : DefaultEdgeFadeDistance;
            surface.MaxRoughness = std::isfinite(reflector.MaxRoughness) ? std::clamp(reflector.MaxRoughness, 0.02f, 1.0f) : DefaultMaxRoughness;
            surface.Intensity = std::isfinite(reflector.Intensity) ? std::clamp(reflector.Intensity, 0.0f, 10.0f) : 1.0f;
            surface.ClipToExtents = reflector.ClipToExtents;
            surface.Valid = reflector.Enabled &&
                            IsFiniteMat4(surface.WorldTransform) &&
                            IsFiniteVec3(reflector.Position) &&
                            IsFiniteVec3(reflector.Normal);
            return surface;
        }

        float SignedDistanceToPlane(const PlanarReflectionSurface& surface, const glm::vec3& pointWS) {
            if (!surface.Valid || !IsFiniteVec3(pointWS)) return 0.0f;
            return glm::dot(surface.Normal, pointWS - surface.Position);
        }

        glm::vec3 ReflectPoint(const PlanarReflectionSurface& surface, const glm::vec3& pointWS) {
            if (!surface.Valid || !IsFiniteVec3(pointWS)) return pointWS;
            const float dist = glm::dot(surface.Normal, pointWS - surface.Position);
            return pointWS - (2.0f * dist) * surface.Normal;
        }

        glm::vec3 ReflectDirection(const PlanarReflectionSurface& surface, const glm::vec3& dirWS) {
            if (!surface.Valid || !IsFiniteVec3(dirWS)) return dirWS;
            return dirWS - (2.0f * glm::dot(surface.Normal, dirWS)) * surface.Normal;
        }

        glm::mat4 BuildReflectionMatrix(const PlanarReflectionSurface& surface) {
            if (!surface.Valid) return glm::mat4(1.0f);
            const glm::vec3 n = surface.Normal;
            const float d = -glm::dot(n, surface.Position);
            glm::mat4 refl(1.0f);
            refl[0][0] = 1.0f - 2.0f * n.x * n.x;
            refl[1][0] = -2.0f * n.x * n.y;
            refl[2][0] = -2.0f * n.x * n.z;
            refl[3][0] = -2.0f * d * n.x;

            refl[0][1] = -2.0f * n.y * n.x;
            refl[1][1] = 1.0f - 2.0f * n.y * n.y;
            refl[2][1] = -2.0f * n.y * n.z;
            refl[3][1] = -2.0f * d * n.y;

            refl[0][2] = -2.0f * n.z * n.x;
            refl[1][2] = -2.0f * n.z * n.y;
            refl[2][2] = 1.0f - 2.0f * n.z * n.z;
            refl[3][2] = -2.0f * d * n.z;
            return refl;
        }

        glm::mat4 BuildObliqueClipProjectionZO(
            const glm::mat4& projZO,
            const glm::mat4& mirroredView,
            const glm::vec4& worldPlane,
            glm::vec4* outViewPlane,
            bool* outApplied,
            glm::mat4* outObliqueGL) {
            if (outApplied) *outApplied = false;
            if (outObliqueGL) *outObliqueGL = projZO;

            if (!IsFiniteMat4(projZO) || !IsFiniteMat4(mirroredView)) {
                return projZO;
            }
            const float detView = glm::determinant(glm::mat3(mirroredView));
            const float detProj = glm::determinant(projZO);
            if (!std::isfinite(detView) || std::abs(detView) < 1e-6f ||
                !std::isfinite(detProj) || std::abs(detProj) < 1e-12f) {
                return projZO;
            }

            const glm::mat4 invView = glm::inverse(mirroredView);
            const glm::vec4 rawViewPlane = glm::transpose(invView) * worldPlane;
            const float nLen = glm::length(glm::vec3(rawViewPlane));
            if (!std::isfinite(nLen) || nLen < 1e-6f) {
                return projZO;
            }
            const glm::vec4 viewPlane = rawViewPlane / nLen;
            if (outViewPlane) *outViewPlane = viewPlane;

            // Find the opposite far frustum corner in view space corresponding to z_ndc = 1 (ZO far plane).
            const float sgnX = ((viewPlane.x * projZO[0][0]) >= 0.0f) ? 1.0f : -1.0f;
            const float sgnY = ((viewPlane.y * projZO[1][1]) >= 0.0f) ? 1.0f : -1.0f;
            glm::vec4 q = glm::inverse(projZO) * glm::vec4(sgnX, sgnY, 1.0f, 1.0f);
            if (!std::isfinite(q.w) || std::abs(q.w) < 1e-6f) {
                return projZO;
            }
            q /= q.w;

            const glm::vec4 row3(projZO[0][3], projZO[1][3], projZO[2][3], projZO[3][3]);
            const float row3DotQ = glm::dot(row3, q);
            const float planeDotQ = glm::dot(viewPlane, q);
            if (!std::isfinite(row3DotQ) || !std::isfinite(planeDotQ) ||
                row3DotQ <= 1e-4f || planeDotQ <= 1e-4f) {
                return projZO;
            }

            // ZO near plane lies at z_clip = M2 . p = 0 and keeps z_clip >= 0.
            // Scaling viewPlane by (row3 . q) / (viewPlane . q) preserves the far plane at z_clip = w_clip.
            const glm::vec4 scaledPlane = viewPlane * (row3DotQ / planeDotQ);
            glm::mat4 obliqueZO = projZO;
            obliqueZO[0][2] = scaledPlane.x;
            obliqueZO[1][2] = scaledPlane.y;
            obliqueZO[2][2] = scaledPlane.z;
            obliqueZO[3][2] = scaledPlane.w;
            if (!IsFiniteMat4(obliqueZO)) {
                return projZO;
            }

            if (outObliqueGL) {
                glm::mat4 obliqueGL = obliqueZO;
                const glm::vec4 glRow2 = 2.0f * scaledPlane - row3;
                obliqueGL[0][2] = glRow2.x;
                obliqueGL[1][2] = glRow2.y;
                obliqueGL[2][2] = glRow2.z;
                obliqueGL[3][2] = glRow2.w;
                *outObliqueGL = obliqueGL;
            }
            if (outApplied) *outApplied = true;
            return obliqueZO;
        }

        MirroredCameraResult BuildMirroredCamera(
            const PerspectiveCamera& mainCamera,
            const PlanarReflectionSurface& surface,
            const PlanarReflectionPolicy& policy) {
            MirroredCameraResult result;
            if (!surface.Valid) {
                result.Diagnostic = "PLANAR_SURFACE_INVALID: non-finite or disabled reflector surface";
                return result;
            }
            const float signedDist = SignedDistanceToPlane(surface, mainCamera.GetPosition());
            result.CameraSignedDistance = signedDist;
            if (!std::isfinite(signedDist) || signedDist <= std::max(1e-5f, policy.MinCameraPlaneDistance)) {
                result.Diagnostic = "PLANAR_CAMERA_BEHIND_PLANE: camera must remain on the front side of the reflector plane";
                return result;
            }

            const glm::mat4 reflMatrix = BuildReflectionMatrix(surface);
            const glm::mat4 mirroredView = mainCamera.GetViewMatrix() * reflMatrix;
            const glm::vec3 mirroredPos = ReflectPoint(surface, mainCamera.GetPosition());
            if (!IsFiniteMat4(mirroredView) || !IsFiniteVec3(mirroredPos)) {
                result.Diagnostic = "PLANAR_CAMERA_INVALID: mirrored transform is non-finite";
                return result;
            }

            PlanarReflectionSurface clippedSurface = surface;
            clippedSurface.ClipPlaneOffset = surface.ClipPlaneOffset + policy.ClipPlaneOffset;
            const glm::vec4 worldPlane = clippedSurface.WorldPlane();

            PerspectiveCamera mirroredCam = mainCamera;
            // Capture depth range is authored independently from the main view.
            if (mainCamera.GetProjectionType() == CameraProjectionType::Orthographic)
                mirroredCam.SetOrthographic(mainCamera.GetOrthographicSize(), mainCamera.GetAspectRatio(), surface.CaptureNearPlane, surface.CaptureFarPlane);
            else
                mirroredCam.SetPerspective(mainCamera.GetFOV(), mainCamera.GetAspectRatio(), surface.CaptureNearPlane, surface.CaptureFarPlane);
            mirroredCam.SetViewMatrix(mirroredView, mirroredPos);
            const glm::mat4 baseProjZO = mirroredCam.GetUnclippedProjectionMatrixZO();

            glm::vec4 viewPlane{};
            bool obliqueApplied = false;
            glm::mat4 obliqueGL = mainCamera.GetProjectionMatrix();
            const glm::mat4 obliqueZO = BuildObliqueClipProjectionZO(
                baseProjZO, mirroredView, worldPlane, &viewPlane, &obliqueApplied, &obliqueGL);
            if (!obliqueApplied) {
                result.Diagnostic = "PLANAR_CLIP_INVALID: oblique clipping could not be constructed";
                return result;
            }
            mirroredCam.SetCustomProjection(obliqueGL, obliqueZO);

            result.Valid = true;
            result.ObliqueClippingApplied = obliqueApplied;
            result.ReverseWinding = (glm::determinant(glm::mat3(mirroredView)) < 0.0f);
            result.Camera = mirroredCam;
            result.ReflectionMatrix = reflMatrix;
            result.WorldPlane = worldPlane;
            result.ViewPlane = viewPlane;
            return result;
        }

        float EvaluatePlanarReflectionConfidence(
            const PlanarReflectionSurface& surface,
            const glm::vec3& worldPos,
            const glm::vec3& normalWS,
            float roughness,
            float maxRoughnessOverride, float viewDistance, float globalStrength) {
            if (!surface.Valid || !IsFiniteVec3(worldPos) || !IsFiniteVec3(normalWS)) return 0.0f;
            const float nDot = glm::dot(SafeNormalize(normalWS, glm::vec3(0.0f, 1.0f, 0.0f)), surface.Normal);
            auto smooth = [](float lo, float hi, float value) {
                const float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            };
            const float normalWeight = smooth(0.95f, 0.995f, nDot);
            if (normalWeight <= 0.0f) return 0.0f;

            const float planeDist = std::abs(glm::dot(surface.Normal, worldPos - surface.Position));
            const float thicknessTolerance = std::max(0.02f, 0.002f * std::max(viewDistance, 0.0f));
            const float planeWeight = 1.0f - smooth(thicknessTolerance * 0.5f, thicknessTolerance, planeDist);
            if (planeWeight <= 0.0f) return 0.0f;

            float extentWeight = 1.0f;
            if (surface.ClipToExtents) {
                const glm::vec3 rel = worldPos - surface.Position;
                const float localU = std::abs(glm::dot(rel, surface.Tangent));
                const float localV = std::abs(glm::dot(rel, surface.Bitangent));
                if (localU >= surface.HalfExtents.x || localV >= surface.HalfExtents.y) {
                    return 0.0f;
                }
                const float fade = std::clamp(surface.EdgeFadeDistance, 0.0f, std::min(surface.HalfExtents.x, surface.HalfExtents.y));
                if (fade > 1e-4f) {
                    const float marginU = std::clamp((surface.HalfExtents.x - localU) / fade, 0.0f, 1.0f);
                    const float marginV = std::clamp((surface.HalfExtents.y - localV) / fade, 0.0f, 1.0f);
                    extentWeight = smooth(0.0f, 1.0f, std::min(marginU, marginV));
                }
            }

            const float maxRough = (maxRoughnessOverride > 0.0f)
                ? std::min(surface.MaxRoughness, std::clamp(maxRoughnessOverride, 0.02f, 1.0f))
                : surface.MaxRoughness;
            const float safeRough = std::clamp(roughness, 0.0f, 1.0f);
            const float fadeStart = maxRough * 0.65f;
            float roughWeight = 1.0f;
            if (safeRough >= maxRough) {
                roughWeight = 0.0f;
            } else if (safeRough > fadeStart) {
                const float t = (safeRough - fadeStart) / std::max(maxRough - fadeStart, 1e-4f);
                roughWeight = 1.0f - (t * t * (3.0f - 2.0f * t));
            }

            return std::clamp(normalWeight * planeWeight * extentWeight * roughWeight * std::clamp(surface.Intensity, 0.0f, 2.0f) * std::clamp(globalStrength, 0.0f, 2.0f), 0.0f, 1.0f);
        }

        glm::uvec2 ComputeScaledTargetExtent(uint32_t baseWidth, uint32_t baseHeight, float resolutionScale) {
            return {
                RHI::RHIViewTargets::ComputePlanarTargetDimension(baseWidth, resolutionScale),
                RHI::RHIViewTargets::ComputePlanarTargetDimension(baseHeight, resolutionScale)
            };
        }

        SceneRenderSettings SanitizeCaptureSettings(
            const SceneRenderSettings& mainSettings,
            const PlanarReflectionPolicy& policy) {
            SceneRenderSettings out = mainSettings;
            out.MSAASamples = 1u;
            out.EnableToneMapping = false;
            out.ToneMappingMode = 3; // Linear HDR output preserved in PlanarColor RGBA16F
            out.EnableFXAA = false;
            out.EnableBloom = false;
            out.EnableAutoExposure = false;
            out.EnableTAA = false;
            out.EnableSSR = policy.AllowSSR;
            out.EnablePlanarReflections = policy.AllowPlanarReflections;
            out.EnableGTAO = false;
            out.EnableContactShadows = false;
            out.EnableVolumetricFog = false;
            out.EnableDDGI = false;
            out.HiZVisualizationLevel = -1;
            out.Wireframe = policy.IncludeEditorOverlays ? mainSettings.Wireframe : false;
            out.ShowPhysicsDebug = policy.IncludeEditorOverlays ? mainSettings.ShowPhysicsDebug : false;
            out.EnableSkybox = policy.IncludeSkybox && mainSettings.EnableSkybox;
            out.EnableParticles = policy.IncludeParticles && mainSettings.EnableParticles;
            out.EnableShadows = policy.IncludeShadows && mainSettings.EnableShadows;
            return out;
        }

        RenderWorld FilterCaptureWorld(
            const RenderWorld& sourceWorld,
            const RenderPlanarReflector& reflector,
            const PlanarReflectionPolicy& policy) {
            RenderWorld out;
            out.Camera = sourceWorld.Camera;
            out.DirectionalLight = sourceWorld.DirectionalLight;
            out.AdditionalDirectionalLights = sourceWorld.AdditionalDirectionalLights;
            out.PointLights = sourceWorld.PointLights;
            out.SpotLights = sourceWorld.SpotLights;
            out.RectLights = sourceWorld.RectLights;
            out.Skybox = sourceWorld.Skybox;
            out.Skybox.Enabled = policy.IncludeSkybox && reflector.CaptureSkybox && sourceWorld.Skybox.Enabled;
            out.EntityToStableUUID = sourceWorld.EntityToStableUUID;
            out.SceneRevision = sourceWorld.SceneRevision;
            out.SceneInstanceID = sourceWorld.SceneInstanceID;
            out.SceneLifetimeEpoch = sourceWorld.SceneLifetimeEpoch;
            out.TotalEntities = sourceWorld.TotalEntities;

            // Recursion disabled: nested planar reflectors are stripped inside a planar capture.
            out.PlanarReflectors.clear();
            if (!policy.AllowDynamicProbes) {
                out.ReflectionProbes.clear();
            } else {
                out.ReflectionProbes = sourceWorld.ReflectionProbes;
            }
            if (policy.IncludeParticles) out.Particles = sourceWorld.Particles;
            if (policy.IncludeEditorOverlays) out.DebugLines = sourceWorld.DebugLines;

            if (!(policy.IncludeShadows && reflector.CaptureShadows)) {
                if (out.DirectionalLight.has_value()) out.DirectionalLight->CastsShadows = false;
                for (auto& dl : out.AdditionalDirectionalLights) dl.CastsShadows = false;
                for (auto& pl : out.PointLights) pl.Shadows.CastShadows = false;
                for (auto& sl : out.SpotLights) sl.Shadows.CastShadows = false;
            }

            std::unordered_set<uint64_t> excludedIds;
            if (reflector.EntityID != 0u) excludedIds.insert(reflector.EntityID);
            if (policy.ExcludeEntityID != 0u) excludedIds.insert(policy.ExcludeEntityID);
            for (uint64_t id : policy.AdditionalExcludedEntityIDs) {
                if (id != 0u) excludedIds.insert(id);
            }

            if (policy.IncludeOpaqueAndCutout) out.Skins = sourceWorld.Skins;
            out.Meshes.reserve(sourceWorld.Meshes.size());
            for (const auto& mesh : sourceWorld.Meshes) {
                // EnTT entity index zero is valid; its stable identity still
                // excludes the receiver without excluding anonymous meshes.
                const bool isReflector = reflector.StableUUID != UUID(0) && mesh.StableUUID == reflector.StableUUID;
                if (isReflector || (mesh.EntityID != 0u && excludedIds.count(static_cast<uint64_t>(mesh.EntityID)) > 0u)) {
                    continue;
                }
                const ResolvedSurface surface = MaterialResolver::Resolve(mesh.Material);
                const bool isTransparent =
                    (surface.Surface == SurfaceType::Transparent || surface.Surface == SurfaceType::Additive);
                if (isTransparent && !policy.IncludeTransparent) continue;
                if (!isTransparent && !policy.IncludeOpaqueAndCutout) continue;
                out.Meshes.push_back(mesh);
            }
            return out;
        }

        PlanarReflectionInvalidation EvaluateInvalidation(
            const PlanarReflectionDomainStamps& published,
            const PlanarReflectionDomainStamps& current,
            bool manualRequested) {
            PlanarReflectionInvalidation out;
            out.PlaneChanged = (published.PlaneStamp != current.PlaneStamp);
            out.CameraChanged = (published.CameraStamp != current.CameraStamp);
            out.SceneRevisionChanged = (published.SceneRevision != current.SceneRevision) ||
                                       (published.SceneInstanceID != current.SceneInstanceID) ||
                                       (published.SceneLifetimeEpoch != current.SceneLifetimeEpoch);
            out.GeometryChanged = (published.GeometryStamp != current.GeometryStamp);
            out.MaterialChanged = (published.MaterialStamp != current.MaterialStamp);
            out.LightingChanged = (published.LightingStamp != current.LightingStamp);
            out.AssetChanged = (published.AssetStamp != current.AssetStamp);
            out.SettingsChanged = (published.SettingsStamp != current.SettingsStamp);
            out.TargetExtentChanged = (published.TargetWidth != current.TargetWidth) ||
                                      (published.TargetHeight != current.TargetHeight);
            out.DeviceEpochChanged = (published.DeviceEpoch != current.DeviceEpoch);
            out.ManualRequested = manualRequested;
            return out;
        }
    } // namespace PlanarReflectionContract

    // =========================================================================
    // 10. Volumetric Froxel Fog Resources, Depth Slice & Tier Contracts (M15-U6-A)
    // =========================================================================
    namespace FroxelFogContract {

        namespace {
            static_assert(static_cast<uint8_t>(FroxelFogQuality::High) == RHI::RHIViewTargets::DefaultFroxelQualityTier);
            static_assert(static_cast<uint8_t>(FroxelFogQuality::Ultra) == RHI::RHIViewTargets::MaxFroxelQualityTier);

            uint8_t ResolveQualityTier(FroxelFogQuality quality) {
                const auto tier = static_cast<uint8_t>(quality);
                return tier <= RHI::RHIViewTargets::MaxFroxelQualityTier
                    ? tier : RHI::RHIViewTargets::DefaultFroxelQualityTier;
            }

            float EvaluateExponentialDepth(float nearZ, float farZ, double normalizedSlice) {
                return static_cast<float>(
                    static_cast<double>(nearZ) * std::pow(static_cast<double>(farZ) / static_cast<double>(nearZ), normalizedSlice));
            }
        }

        uint32_t GetQualityTierCount() {
            return RHI::RHIViewTargets::MaxFroxelQualityTier + 1u;
        }

        const RHI::RHIFroxelTierSpec& GetTierSpec(FroxelFogQuality quality) {
            return *RHI::RHIViewTargets::GetFroxelTierSpec(ResolveQualityTier(quality));
        }

        RHI::RHIFroxelGridExtent ComputeGridExtent(uint32_t viewWidth, uint32_t viewHeight, FroxelFogQuality quality) {
            const auto index = static_cast<uint32_t>(quality);
            if (index > RHI::RHIViewTargets::MaxFroxelQualityTier) return {};
            return RHI::RHIViewTargets::ComputeFroxelGridExtent(viewWidth, viewHeight, static_cast<uint8_t>(index));
        }

        bool ResolveDepthRange(float nearDistance, float maxDistance, float& outNear, float& outFar) {
            if (!std::isfinite(nearDistance) || !std::isfinite(maxDistance)) return false;
            if (nearDistance < MinNearDistance || maxDistance <= nearDistance || maxDistance > MaxAllowedDistance) {
                return false;
            }
            // Each half-slice step must exceed one float ULP so both boundaries
            // and centers remain distinct, even at the highest depth resolution.
            static const double minDepthRatio = std::pow(
                1.0 + std::numeric_limits<float>::epsilon(), 2u * GetTierSpec(FroxelFogQuality::Ultra).Limit.Depth);
            if (static_cast<double>(maxDistance) <= static_cast<double>(nearDistance) * minDepthRatio) return false;
            outNear = nearDistance;
            outFar = maxDistance;
            return true;
        }

        float SliceBoundaryToViewDistance(uint32_t sliceBoundary, uint32_t sliceCount, float nearDistance, float maxDistance) {
            float nearZ = 0.0f, farZ = 0.0f;
            if (sliceCount == 0u || sliceBoundary > sliceCount || !ResolveDepthRange(nearDistance, maxDistance, nearZ, farZ)) {
                return 0.0f;
            }
            return EvaluateExponentialDepth(nearZ, farZ, static_cast<double>(sliceBoundary) / static_cast<double>(sliceCount));
        }

        float SliceCenterToViewDistance(uint32_t sliceIndex, uint32_t sliceCount, float nearDistance, float maxDistance) {
            float nearZ = 0.0f, farZ = 0.0f;
            if (sliceCount == 0u || sliceIndex >= sliceCount || !ResolveDepthRange(nearDistance, maxDistance, nearZ, farZ)) {
                return 0.0f;
            }
            return EvaluateExponentialDepth(nearZ, farZ, (static_cast<double>(sliceIndex) + 0.5) / static_cast<double>(sliceCount));
        }

        float EvaluateSliceThickness(uint32_t sliceIndex, uint32_t sliceCount, float nearDistance, float maxDistance) {
            if (sliceCount == 0u || sliceIndex >= sliceCount) return 0.0f;
            const float z0 = SliceBoundaryToViewDistance(sliceIndex, sliceCount, nearDistance, maxDistance);
            const float z1 = SliceBoundaryToViewDistance(sliceIndex + 1u, sliceCount, nearDistance, maxDistance);
            if (!(z1 > z0)) return 0.0f;
            return z1 - z0;
        }

        float ViewDistanceToSliceCoord(float viewDistance, uint32_t sliceCount, float nearDistance, float maxDistance) {
            float nearZ = 0.0f, farZ = 0.0f;
            if (sliceCount == 0u || !std::isfinite(viewDistance) || !ResolveDepthRange(nearDistance, maxDistance, nearZ, farZ)) {
                return 0.0f;
            }
            const double clampedZ = std::clamp(static_cast<double>(viewDistance), static_cast<double>(nearZ), static_cast<double>(farZ));
            const double ratio = std::log(clampedZ / static_cast<double>(nearZ)) / std::log(static_cast<double>(farZ) / static_cast<double>(nearZ));
            return static_cast<float>(std::clamp(ratio, 0.0, 1.0));
        }

        float SliceCoordToViewDistance(float sliceCoord, float nearDistance, float maxDistance) {
            float nearZ = 0.0f, farZ = 0.0f;
            if (!std::isfinite(sliceCoord) || !ResolveDepthRange(nearDistance, maxDistance, nearZ, farZ)) {
                return 0.0f;
            }
            const double clamped = std::clamp(static_cast<double>(sliceCoord), 0.0, 1.0);
            return EvaluateExponentialDepth(nearZ, farZ, clamped);
        }

        FroxelMemoryBudget EstimateMemoryBudget(uint32_t viewWidth, uint32_t viewHeight, FroxelFogQuality quality, bool queryDevice) {
            FroxelMemoryBudget budget;
            budget.Extent = ComputeGridExtent(viewWidth, viewHeight, quality);
            if (!budget.Extent.Width || !budget.Extent.Height || !budget.Extent.Depth) {
                budget.AllocatedVolumeCount = 0u;
                return budget;
            }
            budget.FroxelCount = static_cast<uint64_t>(budget.Extent.Width) *
                                 static_cast<uint64_t>(budget.Extent.Height) *
                                 static_cast<uint64_t>(budget.Extent.Depth);
            budget.VolumeBytesPerTexture = budget.FroxelCount * BytesPerFroxelRGBA16F;
            budget.AllocatedVolumeCount = VolumeCount;
            budget.TotalLogicalBytes = budget.VolumeBytesPerTexture * static_cast<uint64_t>(VolumeCount);

            if (queryDevice && RHI::RHIDevice::Get().CanCreateResources()) {
                nri::MemoryDesc memDesc{};
                const auto texDesc = RHI::RHITextureDesc::StorageTexture3D(
                    budget.Extent.Width, budget.Extent.Height, budget.Extent.Depth,
                    RHI::RHITextureFormat::RGBA16_FLOAT, "FroxelBudgetProbe");
                if (RHI::RHITexture::QueryMemoryRequirements(texDesc, memDesc) && memDesc.size > 0u) {
                    budget.DeviceQueryAvailable = true;
                    budget.DeviceBytesPerTexture = memDesc.size;
                    budget.DeviceAllocationAlignment = memDesc.alignment;
                    budget.TotalDeviceReportedBytes = memDesc.size * static_cast<uint64_t>(VolumeCount);
                }
            }
            return budget;
        }

        bool ValidateResourcePolicy(const RHI::RHIViewTargetDesc& desc, const FroxelFogResourcePolicy& policy, std::string* outReason) {
            auto fail = [&](std::string msg) {
                if (outReason) *outReason = std::move(msg);
                return false;
            };
            if (!desc.Width || !desc.Height) return fail("View extent must be non-zero for froxel resources");
            if (!desc.RenderAuxiliary) return fail("Froxel resources require auxiliary view targets");
            if (!desc.EnableDepth || desc.Samples != 1u) return fail("Froxel resources require single-sample depth");
            if (desc.FroxelQuality > RHI::RHIViewTargets::MaxFroxelQualityTier) {
                return fail("Froxel quality tier is out of supported range [0..3]");
            }
            float nearZ = 0.0f, farZ = 0.0f;
            if (!ResolveDepthRange(policy.NearDistance, policy.MaxDistance, nearZ, farZ)) {
                return fail("Froxel depth range [near, max] is invalid or non-finite");
            }
            const auto budget = EstimateMemoryBudget(desc.Width, desc.Height, static_cast<FroxelFogQuality>(desc.FroxelQuality), false);
            if (!budget.FroxelCount || !budget.TotalLogicalBytes) {
                return fail("Computed froxel grid extent is empty");
            }
            if (policy.MaxBudgetBytes > 0u && budget.TotalLogicalBytes > policy.MaxBudgetBytes) {
                return fail("Froxel logical VRAM budget exceeds configured limit");
            }
            if (outReason) outReason->clear();
            return true;
        }

        namespace {
            float SanitizeNonNegative(float value) {
                return (std::isfinite(value) && value > 0.0f) ? value : 0.0f;
            }

            glm::vec3 SanitizeNonNegativeVec3(const glm::vec3& value) {
                return { SanitizeNonNegative(value.x), SanitizeNonNegative(value.y), SanitizeNonNegative(value.z) };
            }

            float EffectiveDensity(float density, float intensity) {
                constexpr double maxHalf = 65504.0;
                return static_cast<float>(std::min(maxHalf,
                    static_cast<double>(SanitizeNonNegative(density)) * SanitizeNonNegative(intensity)));
            }

            bool ResolveMediaDepthRange(const SceneRenderSettings& settings, const RenderVolumetricFog* authored,
                                        float& nearZ, float& farZ, bool& usedFallback) {
                usedFallback = false;
                if (authored && authored->Enabled) {
                    if (ResolveDepthRange(settings.FogNearDistance, authored->MaxDistance, nearZ, farZ))
                        return true;
                    usedFallback = true;
                }
                return ResolveDepthRange(settings.FogNearDistance, settings.FogMaxDistance, nearZ, farZ);
            }

            void AppendDiagnostic(std::string& dst, std::string_view msg) {
                if (msg.empty()) return;
                if (!dst.empty()) dst += "; ";
                dst.append(msg);
            }

            glm::vec3 ComputeStepInScatterPerMeter(const FroxelMediaState& state,
                                                   float worldY,
                                                   float phaseSun) {
                const float heightAboveGround = std::max(worldY, 0.0f);
                const float localDensity = state.EffectiveBaseDensity * std::exp(-heightAboveGround * state.HeightFalloff);
                if (!(localDensity > 0.0f)) return {};
                const glm::vec3 scatterCoeff = state.Albedo * localDensity;
                const glm::vec3 incomingRadiance =
                    (state.HasDirectionalSun ? state.SunRadiance * phaseSun : glm::vec3(0.0f)) +
                    (state.HasAmbientLight ? state.AmbientRadiance : glm::vec3(0.0f));
                return scatterCoeff * incomingRadiance;
            }
        }

        void ResolveViewTargetOptions(const SceneRenderSettings& settings, bool isSubCapture, bool hasDepth,
                                      bool& outEnable, uint8_t& outQuality, const RenderVolumetricFog* authored) {
            float nearZ = 0.0f, farZ = 0.0f;
            bool usedFallback = false;
            outEnable = settings.EnableVolumetricFog && !isSubCapture && hasDepth && settings.MSAASamples == 1u &&
                ResolveMediaDepthRange(settings, authored, nearZ, farZ, usedFallback);
            outQuality = outEnable ? ResolveQualityTier(settings.FogQuality) : RHI::RHIViewTargets::DefaultFroxelQualityTier;
        }

        bool RequiresLinearColor(const SceneRenderSettings& settings, const RenderVolumetricFog* authored) {
            return authored && authored->Enabled
                ? EffectiveDensity(authored->Density, authored->Intensity) > 0.0f
                : EffectiveDensity(settings.FogDensity, 1.0f) > 0.0f;
        }

        FroxelMediaState ResolveMediaState(const RenderWorld& world,
                                           const SceneRenderSettings& settings,
                                           const LightingFrame& lightingFrame) {
            FroxelMediaState state;
            const bool useWorld = world.VolumetricFog && world.VolumetricFog->Enabled;
            state.FromWorldComponent = useWorld;

            const float rawDensity = useWorld ? world.VolumetricFog->Density : settings.FogDensity;
            const float rawFalloff = useWorld ? world.VolumetricFog->HeightFalloff : settings.FogHeightFalloff;
            const float rawAsymmetry = useWorld ? world.VolumetricFog->ScatteringAsymmetry : settings.FogScattering;
            const glm::vec3 rawColor = useWorld ? world.VolumetricFog->Color : settings.FogColor;
            const float rawIntensity = useWorld ? world.VolumetricFog->Intensity : 1.0f;
            state.Density = SanitizeNonNegative(rawDensity);
            state.HeightFalloff = SanitizeNonNegative(rawFalloff);
            state.ScatteringAsymmetry = std::isfinite(rawAsymmetry)
                ? std::clamp(rawAsymmetry, -MaxAsymmetry, MaxAsymmetry) : 0.0f;
            state.Albedo = glm::min(SanitizeNonNegativeVec3(rawColor), glm::vec3(1.0f));
            state.Intensity = SanitizeNonNegative(rawIntensity);
            state.EffectiveBaseDensity = EffectiveDensity(state.Density, state.Intensity);

            bool usedFallback = false;
            if (!ResolveMediaDepthRange(settings, useWorld ? &*world.VolumetricFog : nullptr,
                                        state.NearDistance, state.MaxDistance, usedFallback)) {
                state.Diagnostic = "froxel_fog_disabled:invalid_depth_range";
                return state;
            }
            if (usedFallback) {
                state.AuthoredMaxDistanceInvalidFallback = true;
                AppendDiagnostic(state.Diagnostic, "froxel_fog_depth_fallback: invalid authored VolumetricFog MaxDistance");
            }
            state.Valid = true;

            const bool authoredSun = (lightingFrame.DirectionalCount > 0u) && world.DirectionalLight.has_value();
            if (authoredSun && std::isfinite(lightingFrame.Sun.Illuminance) && lightingFrame.Sun.Illuminance > 0.0f) {
                const float lenSq = glm::dot(lightingFrame.Sun.Direction, lightingFrame.Sun.Direction);
                if (std::isfinite(lenSq) && lenSq > 1e-12f) {
                    state.HasDirectionalSun = true;
                    state.SunDirectionToLight = lightingFrame.Sun.Direction * (1.0f / std::sqrt(lenSq));
                    state.SunRadiance = SanitizeNonNegativeVec3(lightingFrame.Sun.Color) *
                        (lightingFrame.Sun.Illuminance * DirectLuxToRadiance);
                }
            }
            if (!state.HasDirectionalSun) {
                AppendDiagnostic(state.Diagnostic,
                    "froxel_fog_no_directional_sun: unlit scattering uses authored environment or zero radiance");
            }

            if (std::isfinite(lightingFrame.Sun.AmbientIntensity) && lightingFrame.Sun.AmbientIntensity > 0.0f) {
                const glm::vec3 amb = SanitizeNonNegativeVec3(lightingFrame.Sun.AmbientColor) * lightingFrame.Sun.AmbientIntensity;
                if (amb.r > 0.0f || amb.g > 0.0f || amb.b > 0.0f) {
                    state.HasAmbientLight = true;
                    state.AmbientRadiance = amb;
                }
            }
            return state;
        }

        float EvaluateHenyeyGreenstein(float cosTheta, float asymmetryG) {
            constexpr double kPi = 3.14159265358979323846;
            const double c = std::isfinite(cosTheta) ? std::clamp(static_cast<double>(cosTheta), -1.0, 1.0) : 0.0;
            const double g = std::isfinite(asymmetryG)
                ? std::clamp(static_cast<double>(asymmetryG), -static_cast<double>(MaxAsymmetry), static_cast<double>(MaxAsymmetry))
                : 0.0;
            const double g2 = g * g;
            const double denom = std::max(1.0 + g2 - 2.0 * g * c, 1e-12);
            return static_cast<float>((1.0 - g2) / (4.0 * kPi * denom * std::sqrt(denom)));
        }

        FroxelSampleIntegral EvaluateHomogeneousFogStep(const glm::vec3& inScatterPerMeter, float sigmaT, float pathLength) {
            FroxelSampleIntegral result;
            if (!(pathLength > 0.0f) || !(sigmaT > 0.0f)) {
                if (pathLength > 0.0f) {
                    result.InScattering = glm::max(inScatterPerMeter, glm::vec3(0.0f)) * pathLength;
                }
                return result;
            }
            const float opticalDepth = sigmaT * pathLength;
            const float stepT = std::exp(-opticalDepth);
            const float integFactor = (opticalDepth > 1e-4f)
                ? ((1.0f - stepT) / sigmaT)
                : (pathLength * (1.0f - 0.5f * opticalDepth + (1.0f / 6.0f) * opticalDepth * opticalDepth));
            result.InScattering = glm::max(inScatterPerMeter, glm::vec3(0.0f)) * integFactor;
            result.Transmittance = stepT;
            return result;
        }

        FroxelSampleIntegral EvaluateAnalyticHomogeneousFog(const FroxelMediaState& state,
                                                            const glm::vec3& rayDirWS,
                                                            float viewZ,
                                                            float rayLengthPerUnitZ) {
            FroxelSampleIntegral result;
            if (!state.Valid || !(state.EffectiveBaseDensity > 0.0f) || !(viewZ > state.NearDistance)) return result;
            const float dirLenSq = glm::dot(rayDirWS, rayDirWS);
            if (!std::isfinite(dirLenSq) || !(dirLenSq > 1e-12f) || !std::isfinite(rayLengthPerUnitZ) || !(rayLengthPerUnitZ > 0.0f)) {
                return result;
            }
            const glm::vec3 unitRay = rayDirWS * (1.0f / std::sqrt(dirLenSq));
            const float clampedZ = std::min(viewZ, state.MaxDistance);
            const float pathLength = (clampedZ - state.NearDistance) * rayLengthPerUnitZ;
            const float phaseSun = state.HasDirectionalSun
                ? EvaluateHenyeyGreenstein(glm::dot(unitRay, state.SunDirectionToLight), state.ScatteringAsymmetry)
                : 0.0f;
            const glm::vec3 incomingRadiance =
                (state.HasDirectionalSun ? state.SunRadiance * phaseSun : glm::vec3(0.0f)) +
                (state.HasAmbientLight ? state.AmbientRadiance : glm::vec3(0.0f));
            const float sigmaT = state.EffectiveBaseDensity;
            const glm::vec3 inScatterPerMeter = (state.Albedo * sigmaT) * incomingRadiance;
            return EvaluateHomogeneousFogStep(inScatterPerMeter, sigmaT, pathLength);
        }

        FroxelSampleIntegral EvaluateFroxelColumnQuadrature(const FroxelMediaState& state,
                                                            const glm::vec3& rayOriginWS,
                                                            const glm::vec3& rayDirWorldPerUnitZ,
                                                            uint32_t sliceCount,
                                                            float targetViewZ) {
            FroxelSampleIntegral out;
            if (!state.Valid || sliceCount == 0u || !(state.EffectiveBaseDensity > 0.0f) || !(targetViewZ > state.NearDistance)) {
                return out;
            }
            const float rayLenPerUnitZ = glm::length(rayDirWorldPerUnitZ);
            if (!std::isfinite(rayLenPerUnitZ) || !(rayLenPerUnitZ > 0.0f)) return out;
            const glm::vec3 unitRay = rayDirWorldPerUnitZ * (1.0f / rayLenPerUnitZ);
            const float phaseSun = state.HasDirectionalSun
                ? EvaluateHenyeyGreenstein(glm::dot(unitRay, state.SunDirectionToLight), state.ScatteringAsymmetry)
                : 0.0f;

            const float clampedZ = std::min(targetViewZ, state.MaxDistance);
            for (uint32_t iz = 0u; iz < sliceCount; ++iz) {
                const float z0 = SliceBoundaryToViewDistance(iz, sliceCount, state.NearDistance, state.MaxDistance);
                if (z0 >= clampedZ) break;
                const float zCenter = SliceCenterToViewDistance(iz, sliceCount, state.NearDistance, state.MaxDistance);
                const float z1 = SliceBoundaryToViewDistance(iz + 1u, sliceCount, state.NearDistance, state.MaxDistance);
                const float ds = (std::min(z1, clampedZ) - z0) * rayLenPerUnitZ;
                const glm::vec3 sampleWS = rayOriginWS + rayDirWorldPerUnitZ * zCenter;
                const float height = std::max(sampleWS.y, 0.0f);
                const float sigmaT = state.EffectiveBaseDensity * std::exp(-height * state.HeightFalloff);
                const glm::vec3 scatterPerM = ComputeStepInScatterPerMeter(state, sampleWS.y, phaseSun);
                const auto step = EvaluateHomogeneousFogStep(scatterPerM, sigmaT, ds);
                out.InScattering += out.Transmittance * step.InScattering;
                out.Transmittance *= step.Transmittance;
            }
            return out;
        }

        FroxelSampleIntegral EvaluateReferenceRayQuadrature(const FroxelMediaState& state,
                                                            const glm::vec3& rayOriginWS,
                                                            const glm::vec3& rayDirWorldPerUnitZ,
                                                            float targetViewZ,
                                                            uint32_t quadratureSteps) {
            FroxelSampleIntegral accum;
            if (!state.Valid || quadratureSteps == 0u || !(state.EffectiveBaseDensity > 0.0f) || !(targetViewZ > state.NearDistance)) {
                return accum;
            }
            const float rayLenPerUnitZ = glm::length(rayDirWorldPerUnitZ);
            if (!std::isfinite(rayLenPerUnitZ) || !(rayLenPerUnitZ > 0.0f)) return accum;
            const glm::vec3 unitRay = rayDirWorldPerUnitZ * (1.0f / rayLenPerUnitZ);
            const float phaseSun = state.HasDirectionalSun
                ? EvaluateHenyeyGreenstein(glm::dot(unitRay, state.SunDirectionToLight), state.ScatteringAsymmetry)
                : 0.0f;

            const double zStart = static_cast<double>(state.NearDistance);
            const double zEnd = static_cast<double>(std::min(targetViewZ, state.MaxDistance));
            const double dz = (zEnd - zStart) / static_cast<double>(quadratureSteps);
            const double ds = dz * rayLenPerUnitZ;
            const glm::dvec3 source = glm::dvec3(state.Albedo) *
                (glm::dvec3(state.SunRadiance) * static_cast<double>(phaseSun) + glm::dvec3(state.AmbientRadiance));
            double accumT = 1.0;
            glm::dvec3 accumS(0.0);
            for (uint32_t i = 0u; i < quadratureSteps; ++i) {
                const double zMid = zStart + (static_cast<double>(i) + 0.5) * dz;
                const double height = std::max(rayOriginWS.y + rayDirWorldPerUnitZ.y * zMid, 0.0);
                const double tau = state.EffectiveBaseDensity * std::exp(-height * state.HeightFalloff) * ds;
                accumS += accumT * (-std::expm1(-tau)) * source;
                accumT *= std::exp(-tau);
            }
            accum.InScattering = glm::vec3(accumS);
            accum.Transmittance = static_cast<float>(accumT);
            return accum;
        }

        namespace {
            glm::vec3 EvaluateLocalScatteringCoeff(const FroxelMediaState& state, const glm::vec3& sampleWS) {
                if (!state.Valid || !(state.EffectiveBaseDensity > 0.0f)) return {};
                const float heightAboveGround = std::max(sampleWS.y, 0.0f);
                const float localDensity = state.EffectiveBaseDensity * std::exp(-heightAboveGround * state.HeightFalloff);
                return SanitizeNonNegativeVec3(state.Albedo) * std::max(localDensity, 0.0f);
            }
        }

        glm::vec3 EvaluatePointLightInScatter(const FroxelMediaState& state,
                                              const LightingPointFrame& light,
                                              const glm::vec3& sampleWS,
                                              const glm::vec3& unitRayWS,
                                              float shadowVisibility) {
            const glm::vec3 sigmaS = EvaluateLocalScatteringCoeff(state, sampleWS);
            if (!(sigmaS.x > 0.0f || sigmaS.y > 0.0f || sigmaS.z > 0.0f)) return {};
            const float dirLenSq = glm::dot(unitRayWS, unitRayWS);
            if (!std::isfinite(dirLenSq) || !(dirLenSq > 1e-12f)) return {};
            const glm::vec3 rayDir = unitRayWS * (1.0f / std::sqrt(dirLenSq));
            if (light.Directional) {
                const float lLenSq = glm::dot(light.Position, light.Position);
                if (!std::isfinite(lLenSq) || !(lLenSq > 1e-12f)) return {};
                const glm::vec3 L = light.Position * (1.0f / std::sqrt(lLenSq));
                const float phase = EvaluateHenyeyGreenstein(glm::dot(rayDir, L), state.ScatteringAsymmetry);
                return sigmaS * SanitizeNonNegativeVec3(light.Color) * (SanitizeNonNegative(light.Intensity) * phase);
            }
            if (!std::isfinite(light.Radius) || !(light.Radius > 0.0f)) return {};
            const glm::vec3 lightVec = light.Position - sampleWS;
            const float dist = glm::length(lightVec);
            if (!std::isfinite(dist) || !(dist < light.Radius) || !(dist > 1e-4f)) return {};
            const glm::vec3 L = lightVec / dist;
            const float minRadiusSq = std::max(0.0025f, light.MinRadius * light.MinRadius);
            const float distSq = std::max(dist * dist, minRadiusSq);
            const float ratio = dist / light.Radius;
            const float ratio4 = ratio * ratio * ratio * ratio;
            const float window = std::clamp(1.0f - ratio4, 0.0f, 1.0f);
            float attenuation = (window * window) / distSq;
            if (light.Falloff > 0.1f && std::abs(light.Falloff - 1.0f) > 0.01f) {
                attenuation = std::pow(std::max(attenuation, 0.0f), light.Falloff);
            }
            const float phase = EvaluateHenyeyGreenstein(glm::dot(rayDir, L), state.ScatteringAsymmetry);
            const float vis = std::isfinite(shadowVisibility) ? std::clamp(shadowVisibility, 0.0f, 1.0f) : 1.0f;
            return sigmaS * SanitizeNonNegativeVec3(light.Color) *
                (SanitizeNonNegative(light.Intensity) * attenuation * LocalIntensityScale * phase * vis);
        }

        glm::vec3 EvaluateSpotLightInScatter(const FroxelMediaState& state,
                                             const LightingSpotFrame& light,
                                             const glm::vec3& sampleWS,
                                             const glm::vec3& unitRayWS,
                                             float shadowVisibility) {
            const glm::vec3 sigmaS = EvaluateLocalScatteringCoeff(state, sampleWS);
            if (!(sigmaS.x > 0.0f || sigmaS.y > 0.0f || sigmaS.z > 0.0f) ||
                !std::isfinite(light.Range) || !(light.Range > 0.0f)) {
                return {};
            }
            const float dirLenSq = glm::dot(unitRayWS, unitRayWS);
            if (!std::isfinite(dirLenSq) || !(dirLenSq > 1e-12f)) return {};
            const glm::vec3 rayDir = unitRayWS * (1.0f / std::sqrt(dirLenSq));
            const glm::vec3 lightVec = light.Position - sampleWS;
            const float dist = glm::length(lightVec);
            if (!std::isfinite(dist) || !(dist < light.Range) || !(dist > 1e-4f)) return {};
            const glm::vec3 L = lightVec / dist;
            const float spotLen = glm::length(light.Direction);
            if (!std::isfinite(spotLen) || !(spotLen > 1e-5f)) return {};
            const glm::vec3 spotDir = light.Direction / spotLen;
            const float cosCone = glm::dot(-L, spotDir);
            if (!(cosCone > light.CosOuterAngle)) return {};
            float spotAtten = std::clamp(
                (cosCone - light.CosOuterAngle) / std::max(1e-4f, light.CosInnerAngle - light.CosOuterAngle),
                0.0f, 1.0f);
            spotAtten *= spotAtten;
            const float distSq = std::max(dist * dist, 0.0025f);
            const float ratio = dist / light.Range;
            const float ratio4 = ratio * ratio * ratio * ratio;
            const float window = std::clamp(1.0f - ratio4, 0.0f, 1.0f);
            const float distAtten = (window * window) / distSq;
            const float phase = EvaluateHenyeyGreenstein(glm::dot(rayDir, L), state.ScatteringAsymmetry);
            const float vis = std::isfinite(shadowVisibility) ? std::clamp(shadowVisibility, 0.0f, 1.0f) : 1.0f;
            return sigmaS * SanitizeNonNegativeVec3(light.Color) *
                (SanitizeNonNegative(light.Intensity) * distAtten * spotAtten * LocalIntensityScale * phase * vis);
        }

        glm::vec3 EvaluateRectLightInScatter(const FroxelMediaState& state,
                                             const LightingRectFrame& light,
                                             const glm::vec3& sampleWS,
                                             const glm::vec3& unitRayWS) {
            constexpr float kPi = 3.14159265358979323846f;
            const glm::vec3 sigmaS = EvaluateLocalScatteringCoeff(state, sampleWS);
            const float intensity = std::isfinite(light.SignedIntensity) ? std::abs(light.SignedIntensity) : 0.0f;
            const bool twoSided = light.SignedIntensity < 0.0f;
            if (!(sigmaS.x > 0.0f || sigmaS.y > 0.0f || sigmaS.z > 0.0f) ||
                !std::isfinite(light.Range) || !(light.Range > 1e-3f) || !(intensity > 0.0f)) {
                return {};
            }
            const float dirLenSq = glm::dot(unitRayWS, unitRayWS);
            if (!std::isfinite(dirLenSq) || !(dirLenSq > 1e-12f)) return {};
            const glm::vec3 rayDir = unitRayWS * (1.0f / std::sqrt(dirLenSq));
            const glm::vec3 lightVec = light.Position - sampleWS;
            const float dist = glm::length(lightVec);
            if (!std::isfinite(dist) || !(dist < light.Range) || !(dist > 1e-4f)) return {};
            const glm::vec3 L = lightVec / dist;
            const float emitLen = glm::length(light.Direction);
            const glm::vec3 lightDir = (std::isfinite(emitLen) && emitLen > 1e-6f)
                ? (light.Direction / emitLen) : glm::vec3(0.0f, 0.0f, -1.0f);
            const float signedForward = glm::dot(-L, lightDir);
            if (!twoSided && !(signedForward > 0.0f)) return {};
            const float emitCos = twoSided ? std::abs(signedForward) : std::max(signedForward, 0.0f);
            const float w = std::max(light.Width, 0.01f);
            const float h = std::max(light.Height, 0.01f);
            const float area = w * h * ((light.Shape == AreaLightShape::Disk) ? (kPi * 0.25f) : 1.0f);
            const float eqRadiusSq = area / kPi;
            const float slant = std::sqrt(dist * dist + eqRadiusSq);
            const float solidAngleFactor = 2.0f * eqRadiusSq / (slant * (slant + dist)) * emitCos;
            const float ratio = std::clamp(dist / light.Range, 0.0f, 1.0f);
            const float ratio4 = ratio * ratio * ratio * ratio;
            const float win = std::clamp(1.0f - ratio4, 0.0f, 1.0f);
            const float rangeAtten = win * win;
            const float phase = EvaluateHenyeyGreenstein(glm::dot(rayDir, L), state.ScatteringAsymmetry);
            return sigmaS * SanitizeNonNegativeVec3(light.Color) *
                (intensity * rangeAtten * LocalIntensityScale * solidAngleFactor * phase);
        }

    } // namespace FroxelFogContract

} // namespace Axle
