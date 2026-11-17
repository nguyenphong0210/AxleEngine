#include <Axle/RenderContracts.hpp>
#include <Axle/Material.hpp>
#include <cmath>
#include <algorithm>

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
            const float NdotV = glm::dot(normViewN, viewDir);
            if (NdotV > 0.0f) {
                const glm::vec3 orthoAxis = glm::normalize(glm::cross(viewDir, glm::vec3(0, 1, 0)));
                const glm::vec3 projN = normViewN - orthoAxis * glm::dot(normViewN, orthoAxis);
                const float projLen = glm::length(projN);
                if (projLen >= 1e-4f) {
                    const float gamma = std::atan2(glm::dot(projN / projLen, glm::cross(orthoAxis, viewDir)),
                                                   std::max(glm::dot(projN / projLen, viewDir), 1e-6f));
                    if (!std::isfinite(gamma)) return false;
                }
            }

            // Contact shadow ray launch validity:
            const float NdotL = glm::dot(normViewN, lightDirView);
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
            glm::vec3 P{0.0f}, N{0.0f};
            glm::vec4 T{0.0f};
            EvaluateDeformedVertex(sample, P, N, T);

            for (int c = 0; c < 3; ++c) {
                if (!std::isfinite(P[c]) || !std::isfinite(N[c]) || !std::isfinite(T[c])) return false;
            }
            if (std::abs(glm::dot(N, glm::vec3(T))) > 1e-3f) return false;

            const glm::vec3 Pview = glm::vec3(viewMatrix * glm::vec4(P, 1.0f));
            const float Z = -Pview.z;
            if (!std::isfinite(Z) || Z <= 0.0f) return false;

            // Bilateral filter edge rejection across disocclusion boundaries
            const auto bgTap = EvaluateBilateralWeight(Z, backgroundDepth, 1, 0);
            if (!bgTap.IsEdgeRejected || bgTap.DepthWeight > 0.01f) return false;

            // Bilateral filter continuous reconstruction on surface
            const auto contTap = EvaluateBilateralWeight(Z, Z, 1, 0);
            if (contTap.IsEdgeRejected || contTap.TotalWeight <= 0.0f) return false;

            return true;
        }

    } // namespace OcclusionContract

} // namespace Axle
