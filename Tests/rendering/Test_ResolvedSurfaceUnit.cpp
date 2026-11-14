#include "RenderTestCommon.hpp"
#include <Axle/ResolvedSurface.hpp>
#include <Axle/MaterialSurfaceShader.hpp>
#include <Axle/ShaderSurface.hpp>
#include <cstring>
#include <limits>

bool RunResolvedSurfaceUnitTests() {
    using namespace Axle;
    std::cout << "  [ResolvedSurface] Running canonical material contract tests...\n";

    RHI::RHIMaterialDesc desc;
    desc.BaseColor = glm::vec4(0.8f, 0.2f, 0.1f, 1.0f);
    desc.Roughness = 0.01f;
    desc.Metallic = 1.4f;
    desc.AO = -0.25f;
    desc.NormalStrength = 2.0f;
    desc.ClearcoatStrength = 0.75f;
    desc.ClearcoatRoughness = 0.2f;
    desc.SheenEnabled = true;
    desc.SheenColor = glm::vec3(0.4f, 0.5f, 0.6f);
    desc.SheenRoughness = 0.3f;
    desc.SubsurfaceEnabled = true;
    desc.SubsurfaceRadius = 2.0f;
    desc.AbsorptionEnabled = true;
    desc.AbsorptionColor = glm::vec3(0.1f, 0.2f, 0.3f);
    desc.RefractionEnabled = true;
    desc.RefractionIOR = 1.45f;
    RHI::RHIMaterial material(desc);

    const auto resolved = MaterialResolver::Resolve(material);
    RENDER_CHECK(resolved.Valid && resolved.Generation != 0, "resolved surface is valid and generation tracked");
    RENDER_CHECK(resolved.Roughness == 0.04f && resolved.Metallic == 1.0f && resolved.AO == 0.0f,
        "core PBR values are clamped at the canonical boundary");
    RENDER_CHECK(resolved.ClearcoatStrength == desc.ClearcoatStrength && resolved.SheenEnabled &&
                 resolved.SubsurfaceEnabled && resolved.AbsorptionEnabled && resolved.RefractionEnabled,
        "advanced material fields survive canonical resolution");

    const auto constants = resolved.ToMaterialConstants();
    RENDER_CHECK(constants.Surface.y == 0.0f && constants.CoatAnisotropy.x == desc.ClearcoatStrength &&
                 constants.Sheen.w == desc.SheenRoughness && constants.Refraction.y == desc.RefractionIOR,
        "canonical surface packs the GPU material contract");
    const auto original = material.BuildConstants();
    RENDER_CHECK(constants.Surface.x == original.Surface.x && constants.Subsurface.w == original.Subsurface.w,
        "canonical packing preserves existing RHIMaterial constant semantics");

    const auto firstGeneration = resolved.Generation;
    desc.Roughness = 0.8f;
    RHI::RHIMaterial changed(desc);
    RENDER_CHECK(MaterialResolver::ComputeGeneration(changed) != firstGeneration,
        "material edits produce a new generation identity");

    RHI::RHIMaterialDesc textured;
    textured.ShadingModel = RHI::RHIShadingModel::TexturedPBR;
    textured.SurfaceType = SurfaceType::Cutout;
    textured.AlphaCutoff = 0.27f;
    textured.UVScale = {2.0f, 3.0f}; textured.UVOffset = {0.1f, 0.2f};
    RHI::RHIMaterial texturedMaterial(textured);
    RHI::RHIObjectPushConstants referencePush, resolvedPush;
    texturedMaterial.ApplyToPushConstants(referencePush);
    MaterialResolver::Resolve(texturedMaterial).ApplyToPushConstants(resolvedPush);
    RENDER_CHECK(resolvedPush.Params == referencePush.Params && resolvedPush.MaterialExtra == referencePush.MaterialExtra,
        "canonical lowering preserves textured cutout mode, authored UVs and alpha cutoff");
    textured.TextureMode = TextureMode::PackedORM;
    RHI::RHIMaterial packedMaterial(textured);
    packedMaterial.ApplyToPushConstants(referencePush);
    MaterialResolver::Resolve(packedMaterial).ApplyToPushConstants(resolvedPush);
    RENDER_CHECK(resolvedPush.Params == referencePush.Params,
        "packed ORM selects the same textured shader path through canonical lowering");

    textured.DetailUVScale = {4.0f, 5.0f}; textured.DetailUVOffset = {0.3f, 0.4f};
    textured.DetailNormalStrength = 1.5f;
    RHI::RHIMaterial detailMaterial(textured);
    const auto detailConstants = MaterialResolver::Resolve(detailMaterial).ToMaterialConstants();
    const auto nativeDetail = detailMaterial.BuildConstants();
    RENDER_CHECK(detailConstants.DetailUVTransform == nativeDetail.DetailUVTransform &&
        detailConstants.DetailParameters == nativeDetail.DetailParameters,
        "canonical GPU packing retains authored detail normal transform and strength");

    auto generation = [&](const RHI::RHIMaterialDesc& value) {
        return MaterialResolver::ComputeGeneration(RHI::RHIMaterial(value));
    };
    const auto texturedGeneration = generation(textured);
    auto edited = textured; edited.AlphaCutoff = 0.7f;
    RENDER_CHECK(generation(edited) != texturedGeneration, "cutout threshold invalidates canonical identity");
    edited = textured; edited.CullMode = CullMode::Front;
    RENDER_CHECK(generation(edited) != texturedGeneration, "raster policy invalidates canonical identity");
    edited = textured; edited.DepthWrite = false;
    RENDER_CHECK(generation(edited) != texturedGeneration, "depth write policy invalidates canonical identity");
    edited = textured; edited.SurfaceType = SurfaceType::Transparent;
    RENDER_CHECK(generation(edited) != texturedGeneration, "surface classification invalidates canonical identity");
    edited = textured; edited.AlbedoPath = "changed-albedo.png";
    RENDER_CHECK(generation(edited) != texturedGeneration, "texture source invalidates canonical identity");
    edited = textured; edited.RefractionTint = {0.7f, 0.2f, 0.1f};
    RENDER_CHECK(generation(edited) != texturedGeneration, "refraction tint invalidates canonical identity");
    edited = textured; edited.EnableEnvironmentReflections = false;
    RENDER_CHECK(generation(edited) != texturedGeneration, "reflection policy invalidates canonical identity");

    const auto assembled = AssembleMaterialSurfaceShader("", "MaterialSurface AxleEvaluateMaterial(float2 uv) { MaterialSurface s; return s; }");
    RENDER_CHECK(assembled.find("Clearcoat") != std::string::npos && assembled.find("FeatureFlags") != std::string::npos,
        "Material Graph template exposes the canonical advanced surface fields");

    RHI::RHIMaterialDesc shaderDesc;
    shaderDesc.BaseColor = {0.2f, 0.4f, 0.6f, 1};
    shaderDesc.Roughness = -3; shaderDesc.Metallic = 7; shaderDesc.AO = 2;
    shaderDesc.UVScale = {2, 3}; shaderDesc.UVOffset = {0.1f, 0.2f};
    shaderDesc.AlbedoPath = "canonical-base.png";
    const auto base = MaterialResolver::Resolve(RHI::RHIMaterial(shaderDesc));
    RENDER_CHECK(base.Textures[0].Path == shaderDesc.AlbedoPath && (base.TextureFlags & 1u),
        "canonical surface owns texture assignment requests as well as scalar values");
    MaterialBindingPlan plan;
    plan.Buffers.push_back({"Material", {0, 0}, 128});
    plan.Uniforms = {{0, 0, 16, UniformKind::Float4, EngineUniform::BaseColor},
        {0, 16, 16, UniformKind::Float4, EngineUniform::BaseUVTransform},
        {0, 32, 16, UniformKind::Float4, EngineUniform::LegacySurfaceParams},
        {0, 48, 4, UniformKind::Float32, PropertyKey{"Material", "Gain"}},
        {0, 64, 64, UniformKind::Matrix4x4, EngineUniform::Model}};
    plan.Textures.push_back({"u_BaseMap", {1, 0}, TextureRole::Color});
    ShaderPropertyInstance gain; gain.Desc.Name = "Gain"; gain.FloatVal = 0.7f;
    EngineMaterialValues view;
    view.BaseColor = glm::vec4(99); view.BaseUVTransform = glm::vec4(99); view.LegacySurfaceParams = glm::vec4(99);
    const auto shader = ResolveShaderSurface(base, SurfaceEvaluation::CustomShader, 17, plan, view, {&gain, 1});
    RENDER_CHECK(std::holds_alternative<ResolvedShaderSurface>(shader), "custom material resolves through canonical shader boundary");
    const auto& binding = std::get<ResolvedShaderSurface>(shader);
    glm::vec4 packedColor, packedUV, packedSurface;
    const auto& bytes = binding.Constants.Buffers[0].Bytes;
    std::memcpy(&packedColor, bytes.data(), 16); std::memcpy(&packedUV, bytes.data()+16, 16);
    std::memcpy(&packedSurface, bytes.data()+32, 16);
    RENDER_CHECK(packedColor == base.BaseColor && packedUV == glm::vec4(base.UVScale, base.UVOffset) &&
        packedSurface == glm::vec4(0.04f, 1, 1, 2),
        "custom common uniforms use canonical clamping and UV policy instead of snapshot/view mirrors");
    RENDER_CHECK(binding.Surface.Evaluation == SurfaceEvaluation::CustomShader && binding.Surface.ShaderGeneration == 17 &&
        binding.Surface.Generation != base.Generation && binding.Textures[0].ResolvedPath == shaderDesc.AlbedoPath,
        "shader result carries evaluation domain, program/material identity and canonical texture request");
    view.Model[3].x = 42;
    auto movedShader = ResolveShaderSurface(base, SurfaceEvaluation::CustomShader, 17, plan, view, {&gain, 1});
    RENDER_CHECK(std::get<ResolvedShaderSurface>(movedShader).Surface.Generation == binding.Surface.Generation,
        "camera/object view state does not change material identity");
    gain.FloatVal = 0.3f;
    auto changedShader = ResolveShaderSurface(base, SurfaceEvaluation::CustomShader, 17, plan, view, {&gain, 1});
    RENDER_CHECK(std::get<ResolvedShaderSurface>(changedShader).Surface.Generation != binding.Surface.Generation,
        "effective shader parameter changes invalidate canonical material identity");
    auto reloadedShader = ResolveShaderSurface(base, SurfaceEvaluation::CustomShader, 18, plan, view, {&gain, 1});
    RENDER_CHECK(std::get<ResolvedShaderSurface>(reloadedShader).Surface.Generation != std::get<ResolvedShaderSurface>(changedShader).Surface.Generation,
        "shader reload invalidates canonical identity");
    shaderDesc.ClearcoatStrength = 0.5f;
    const auto advancedBase = MaterialResolver::Resolve(RHI::RHIMaterial(shaderDesc));
    const auto unsupported = ResolveShaderSurface(advancedBase, SurfaceEvaluation::MaterialGraph, 19, plan, view, {&gain, 1});
    RENDER_CHECK(std::get<ResolvedShaderSurface>(unsupported).Surface.UnsupportedFeatures == static_cast<uint32_t>(SurfaceFeature::Clearcoat) &&
        !std::get<ResolvedShaderSurface>(unsupported).Surface.Diagnostic.empty(),
        "unsupported canonical shader controls have an explicit feature mask and diagnostic");
    gain.FloatVal = std::numeric_limits<float>::quiet_NaN();
    RENDER_CHECK(std::holds_alternative<MaterialBindingError>(ResolveShaderSurface(base, SurfaceEvaluation::CustomShader, 17, plan, view, {&gain, 1})),
        "non-finite effective shader properties reject the transaction");
    shaderDesc.BaseColor.x = std::numeric_limits<float>::infinity();
    const auto invalidBase = MaterialResolver::Resolve(RHI::RHIMaterial(shaderDesc));
    RENDER_CHECK(!invalidBase.Valid && !invalidBase.Diagnostic.empty(), "invalid canonical input cannot be reported ready");
    RENDER_CHECK(std::holds_alternative<MaterialBindingError>(ResolveShaderSurface(invalidBase, SurfaceEvaluation::CustomShader, 17, plan, view, {})),
        "invalid base surface cannot publish a shader binding");

    std::cout << "  [ResolvedSurface] Canonical material contract tests passed.\n";
    return true;
}
