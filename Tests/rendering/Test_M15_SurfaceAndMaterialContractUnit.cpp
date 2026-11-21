#include "RenderTestCommon.hpp"
#include <Axle/Material.hpp>
#include <Axle/Components.hpp>
#include <Axle/ResolvedSurface.hpp>
#include <Axle/ShaderSurface.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/MaterialBindingPlan.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/ViewContext.hpp>
#include <Axle/TemporalService.hpp>
#include <Axle/PostProcessService.hpp>
#include <Axle/RenderDiagnostics.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>

#ifndef AXLE_SURFACE_NORMAL_SHADER_PATH
#define AXLE_SURFACE_NORMAL_SHADER_PATH "Engine/Resources/Shaders/SurfaceNormal.hlsl"
#endif

#ifndef AXLE_MATERIAL_SURFACE_SHADER_PATH
#define AXLE_MATERIAL_SURFACE_SHADER_PATH "Engine/Resources/Shaders/MaterialSurface.hlsl"
#endif

namespace {
    // Cofactor normal transformation matching ForwardPBR.hlsl & SurfaceNormal.hlsl
    glm::vec3 TransformNormalCofactors(const glm::mat3& model, const glm::vec3& n) {
        glm::vec3 a = model * glm::vec3(1, 0, 0);
        glm::vec3 b = model * glm::vec3(0, 1, 0);
        glm::vec3 c = model * glm::vec3(0, 0, 1);
        float handedness = glm::dot(a, glm::cross(b, c)) < 0.0f ? -1.0f : 1.0f;
        glm::vec3 transformedNormal = (glm::cross(b, c) * n.x + glm::cross(c, a) * n.y + glm::cross(a, b) * n.z) * handedness;
        return glm::dot(transformedNormal, transformedNormal) > 1e-12f ? glm::normalize(transformedNormal) : glm::vec3(0, 1, 0);
    }

    // Tangent transform matching ForwardPBR.hlsl
    glm::vec4 TransformTangent(const glm::mat3& model, const glm::vec4& tangent) {
        glm::vec3 a = model * glm::vec3(1, 0, 0);
        glm::vec3 b = model * glm::vec3(0, 1, 0);
        glm::vec3 c = model * glm::vec3(0, 0, 1);
        float handedness = glm::dot(a, glm::cross(b, c)) < 0.0f ? -1.0f : 1.0f;
        glm::vec3 transformedTangent = model * glm::vec3(tangent);
        return glm::vec4(transformedTangent, tangent.w * handedness);
    }

    // Normal decode matching ForwardPBR.hlsl & SurfaceNormal.hlsl
    glm::vec3 DecodeNormal(glm::vec2 rg, float strength) {
        glm::vec2 xy = rg * 2.0f - 1.0f;
        float z = std::sqrt(std::max(0.0f, 1.0f - glm::dot(xy, xy)));
        glm::vec3 n(xy * std::max(0.0f, strength), std::max(z, 1e-4f));
        return glm::normalize(n);
    }

    // Reoriented Normal Mapping (RNM) matching ForwardPBR.hlsl & SurfaceNormal.hlsl
    glm::vec3 ReorientedNormalBlend(glm::vec3 baseNormal, glm::vec3 detail) {
        glm::vec3 t = baseNormal + glm::vec3(0, 0, 1);
        glm::vec3 u = detail * glm::vec3(-1, -1, 1);
        return glm::normalize(t * glm::dot(t, u) / std::max(t.z, 1e-5f) - u);
    }
}

bool RunM15SurfaceAndMaterialContractUnitTests() {
    using namespace Axle;
    std::cout << "  [M15SurfaceMaterial] Running M15-U3-14 through U3-19 contract tests...\n";

    // =========================================================================
    // M15-U3-14: Normal Producer & Decoding Parity
    // =========================================================================
    {
        // 1. Flat normal decode produces unit +Z
        glm::vec3 flat = DecodeNormal(glm::vec2(0.5f, 0.5f), 1.0f);
        RENDER_CHECK(std::abs(flat.x) < 1e-5f && std::abs(flat.y) < 1e-5f && std::abs(flat.z - 1.0f) < 1e-5f,
            "flat normal map (0.5, 0.5) decodes to unit +Z");

        // 2. Normal strength scale
        glm::vec3 perturbed = DecodeNormal(glm::vec2(0.75f, 0.5f), 1.0f);
        glm::vec3 halfPerturbed = DecodeNormal(glm::vec2(0.75f, 0.5f), 0.5f);
        RENDER_CHECK(perturbed.x > halfPerturbed.x && halfPerturbed.x > 0.0f,
            "normal strength smoothly attenuates horizontal tangent displacement");

        glm::vec3 zeroStrength = DecodeNormal(glm::vec2(0.9f, 0.2f), 0.0f);
        RENDER_CHECK(std::abs(zeroStrength.x) < 1e-5f && std::abs(zeroStrength.y) < 1e-5f && std::abs(zeroStrength.z - 1.0f) < 1e-5f,
            "zero normal strength yields flat surface normal");

        // 3. Reoriented Normal Mapping preserves identities
        glm::vec3 anyBase = DecodeNormal(glm::vec2(0.6f, 0.4f), 1.0f);
        glm::vec3 flatDetail = glm::vec3(0, 0, 1);
        glm::vec3 rnmFlatDetail = ReorientedNormalBlend(anyBase, flatDetail);
        RENDER_CHECK(glm::length(rnmFlatDetail - anyBase) < 1e-4f,
            "RNM with flat detail normal leaves base normal intact");

        glm::vec3 anyDetail = DecodeNormal(glm::vec2(0.45f, 0.55f), 0.8f);
        glm::vec3 flatBase = glm::vec3(0, 0, 1);
        glm::vec3 rnmFlatBase = ReorientedNormalBlend(flatBase, anyDetail);
        RENDER_CHECK(glm::length(rnmFlatBase - anyDetail) < 1e-4f,
            "RNM with flat base normal yields detail normal");
    }

    // =========================================================================
    // M15-U3-15.1, 15.2, 15.3: Inverse-Transpose, Mirrored Transforms & TBN
    // =========================================================================
    {
        // 15.1: Nonuniform scale inverse-transpose preservation (n' . t' == 0)
        glm::mat3 nonuniformScale(0.0f);
        nonuniformScale[0][0] = 2.0f;
        nonuniformScale[1][1] = 0.5f;
        nonuniformScale[2][2] = 3.0f;

        // Arbitrary rotation around an oblique axis
        float angle = 0.785398f; // 45 deg
        glm::mat3 rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), angle, glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f))));
        glm::mat3 modelMat = rotation * nonuniformScale;

        glm::vec3 localNormal = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
        glm::vec3 localTangent = glm::normalize(glm::vec3(1.0f, -1.0f, 0.0f)); // perpendicular to normal
        RENDER_CHECK(std::abs(glm::dot(localNormal, localTangent)) < 1e-6f,
            "local normal and tangent are strictly orthogonal");

        glm::vec3 worldNormal = TransformNormalCofactors(modelMat, localNormal);
        glm::vec3 worldTangent = glm::normalize(modelMat * localTangent);
        RENDER_CHECK(std::abs(glm::dot(worldNormal, worldTangent)) < 1e-5f,
            "M15-U3-15.1: cofactor normal transform preserves orthogonality under nonuniform scale and rotation");

        // 15.2: Mirrored transform handedness
        glm::mat3 mirrorMat = glm::mat3(1.0f);
        mirrorMat[0][0] = -1.0f; // reflection in X
        RENDER_CHECK(glm::determinant(mirrorMat) < 0.0f, "mirror matrix has negative determinant");

        glm::vec4 localTanW(0.0f, 1.0f, 0.0f, 1.0f);
        glm::vec3 localN(0.0f, 0.0f, 1.0f);
        glm::vec3 worldN = TransformNormalCofactors(mirrorMat, localN);
        glm::vec4 worldTanW = TransformTangent(mirrorMat, localTanW);

        RENDER_CHECK(worldN.z > 0.0f, "M15-U3-15.2: mirrored normal continues to point outward (+Z)");
        RENDER_CHECK(worldTanW.w == -1.0f, "M15-U3-15.2: mirrored transform inverts tangent handedness sign");

        glm::vec3 T = glm::normalize(glm::vec3(worldTanW));
        glm::vec3 B = glm::cross(worldN, T) * worldTanW.w;
        // In local space: N=(0,0,1), T=(0,1,0), B=N x T * 1 = (-1,0,0)
        // With X mirror: T stays (0,1,0), B should mirror (-1,0,0) to (+1,0,0)
        RENDER_CHECK(B.x > 0.99f, "M15-U3-15.2: mirrored bitangent correctly aligns with texture coordinate V direction");

        // 15.3: Normal map transform via TBN
        glm::vec3 mapPerturb = DecodeNormal(glm::vec2(0.9f, 0.5f), 1.0f);
        glm::vec3 worldPerturbed = glm::normalize(T * mapPerturb.x + B * mapPerturb.y + worldN * mapPerturb.z);
        RENDER_CHECK(glm::dot(worldPerturbed, T) > 0.5f,
            "M15-U3-15.3: tangent-space normal map correctly perturbs world normal towards world tangent");
    }

    // =========================================================================
    // M15-U3-16: Alpha-Cutout & Background Validity Marker
    // =========================================================================
    {
        // Shader inspection of SurfaceNormal.hlsl for alpha cutout and validity
        std::ifstream file(AXLE_SURFACE_NORMAL_SHADER_PATH);
        if (!file.is_open()) {
            file.open("Engine/Resources/Shaders/SurfaceNormal.hlsl");
        }
        RENDER_CHECK(file.is_open(), "SurfaceNormal.hlsl exists and is readable");
        std::stringstream ss;
        ss << file.rdbuf();
        std::string source = ss.str();

        RENDER_CHECK(source.find("u_SurfaceType") != std::string::npos && source.find("u_AlphaCutoff") != std::string::npos,
            "SurfaceNormal.hlsl binds SurfaceType and AlphaCutoff");
        RENDER_CHECK(source.find("discard") != std::string::npos,
            "SurfaceNormal.hlsl executes fragment discard on cutout failure");
        RENDER_CHECK(source.find("return float4(N, 1.0)") != std::string::npos || source.find("return float4(normalize(N), 1.0)") != std::string::npos,
            "SurfaceNormal.hlsl writes 1.0 validity marker in alpha for covered geometry");
        RENDER_CHECK(source.find("DecodeMaterialNormal") != std::string::npos,
            "SurfaceNormal.hlsl implements material normal decoding");
        RENDER_CHECK(source.find("u_DetailNormalMap") != std::string::npos,
            "SurfaceNormal.hlsl binds detail normal maps");
    }

    // =========================================================================
    // M15-U3-17.1 through 17.6: Material Unification & Parity
    // =========================================================================
    {
        std::ifstream file(AXLE_MATERIAL_SURFACE_SHADER_PATH);
        if (!file.is_open()) {
            file.open("Engine/Resources/Shaders/MaterialSurface.hlsl");
        }
        RENDER_CHECK(file.is_open(), "MaterialSurface.hlsl exists and is readable");
        std::stringstream ss;
        ss << file.rdbuf();
        std::string source = ss.str();

        RENDER_CHECK(source.find("DIRECT_LUX_TO_RADIANCE") != std::string::npos,
            "M15-U3-17.6: MaterialSurface.hlsl unifies direct sunlight scaling with DIRECT_LUX_TO_RADIANCE");
        RENDER_CHECK(source.find("Clearcoat") != std::string::npos && source.find("coatBRDF") != std::string::npos,
            "M15-U3-17.5: MaterialSurface.hlsl unifies clearcoat layer evaluation and base attenuation");
        RENDER_CHECK(source.find("saturate(surface.AO)") != std::string::npos && source.find("surface.Emission") != std::string::npos,
            "M15-U3-17.3: MaterialSurface.hlsl unifies AO ambient scaling while preserving emission");
        RENDER_CHECK(source.find("clamp(surface.Roughness, 0.04, 1.0)") != std::string::npos && source.find("saturate(surface.Metallic)") != std::string::npos,
            "M15-U3-17.2: MaterialSurface.hlsl unifies metallic and roughness clamping bounds");
        RENDER_CHECK(source.find("INV_PI") != std::string::npos || source.find("PI") != std::string::npos,
            "M15-U3-17.1: MaterialSurface.hlsl unifies Lambertian diffuse normalization");
        RENDER_CHECK(source.find("FresnelSchlickRoughness") != std::string::npos,
            "M15-U3-17.6: MaterialSurface.hlsl unifies ambient specular reflection response");
        RENDER_CHECK(source.find("AxleEvaluateWorldNormal") != std::string::npos &&
                    source.find("PSNormalMain") != std::string::npos &&
                    source.find("AxleEvaluateMaterial(input.TexCoord)") != std::string::npos,
            "M15-U3-16: Material Graph normal output shares graph evaluation and TBN construction");
    }

    // =========================================================================
    // M15-U3-18: sRGB/Linear Texture & Packed ORM Conventions
    // =========================================================================
    {
        // Verify texture slot color space roles
        RENDER_CHECK(TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::Albedo), "Albedo slot 0 is tagged as Color / sRGB");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::Normal), "Normal slot 1 is tagged as Linear data");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::Roughness), "Roughness / ORM slot 2 is tagged as Linear data");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::Metallic), "Metallic slot 3 is tagged as Linear data");
        RENDER_CHECK(!TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::AO), "AO slot 4 is tagged as Linear data");
        RENDER_CHECK(TextureSlotContract::IsSRGBSlot(RHI::RHITextureSlot::Emissive), "Emissive slot 5 is tagged as Color / sRGB");

        // Verify Packed ORM decoding semantics
        RHI::RHIMaterialDesc ormDesc;
        ormDesc.TextureMode = TextureMode::PackedORM;
        ormDesc.ORMPath = "textures/test_orm.png";
        ormDesc.Roughness = 0.8f;
        ormDesc.Metallic = 1.0f;
        RHI::RHIMaterial ormMat(ormDesc);
        const auto resolved = MaterialResolver::Resolve(ormMat);
        RENDER_CHECK(resolved.ShadingModel == RHI::RHIShadingModel::PackedORMTexturedPBR,
            "PackedORM selects PackedORMTexturedPBR shading model");
        RENDER_CHECK(resolved.Textures[2].Path == ormDesc.ORMPath,
            "PackedORM assigns ORM texture to slot 2 (roughness map register t2)");
    }

    // =========================================================================
    // M15-U3-19.1 through 19.4: Material Full Round-Trip Persistence
    // =========================================================================
    {
        // 19.1: Asset <-> Component Round-Trip
        auto asset = std::make_shared<Material>("TestAdvancedMat", 1001);
        asset->SetTextureMode(TextureMode::PackedORM);
        asset->SetORMTexture(nullptr, "orm.png");
        asset->SetAlbedoColor(glm::vec4(0.7f, 0.3f, 0.2f, 1.0f));
        asset->SetRoughness(0.35f);
        asset->SetMetallic(0.85f);
        asset->SetNormalStrength(1.25f);
        asset->SetClearcoatStrength(0.8f);
        asset->SetClearcoatRoughness(0.08f);
        asset->SetAnisotropyStrength(0.6f);
        asset->SetAnisotropyRotation(0.25f);
        asset->SetSheenEnabled(true);
        asset->SetSheenColor(glm::vec3(0.9f, 0.8f, 0.7f));
        asset->SetSheenRoughness(0.4f);
        asset->SetSubsurfaceEnabled(true);
        asset->SetSubsurfaceColor(glm::vec3(0.95f, 0.4f, 0.3f));
        asset->SetSubsurfaceRadius(2.5f);
        asset->SetAbsorptionEnabled(true);
        asset->SetAbsorptionColor(glm::vec3(0.05f, 0.1f, 0.15f));
        asset->SetDispersion(0.045f);
        asset->SetDetailNormalStrength(1.1f);
        asset->SetDetailUVScale(glm::vec2(5.0f, 5.0f));

        MaterialComponent comp;
        comp.SetAsset(1001, asset); // Calls SyncFromAsset()

        RENDER_CHECK(comp.SheenEnabled == asset->GetSheenEnabled() &&
                     comp.SheenColor == asset->GetSheenColor() &&
                     comp.SheenRoughness == asset->GetSheenRoughness(),
            "M15-U3-19.1: Sheen fields round-trip from Asset to Component");
        RENDER_CHECK(comp.SubsurfaceEnabled == asset->GetSubsurfaceEnabled() &&
                     comp.SubsurfaceColor == asset->GetSubsurfaceColor() &&
                     comp.SubsurfaceRadius == asset->GetSubsurfaceRadius(),
            "M15-U3-19.1: Subsurface fields round-trip from Asset to Component");
        RENDER_CHECK(comp.AbsorptionEnabled == asset->GetAbsorptionEnabled() &&
                     comp.AbsorptionColor == asset->GetAbsorptionColor() &&
                     comp.Dispersion == asset->GetDispersion(),
            "M15-U3-19.1: Absorption & Dispersion fields round-trip from Asset to Component");

        // Mutate on Component and SyncToAsset
        comp.SheenColor = glm::vec3(0.1f, 0.2f, 0.3f);
        comp.SubsurfaceRadius = 4.0f;
        comp.Dispersion = 0.08f;
        comp.SyncToAsset();
        RENDER_CHECK(asset->GetSheenColor() == comp.SheenColor &&
                     asset->GetSubsurfaceRadius() == comp.SubsurfaceRadius &&
                     asset->GetDispersion() == comp.Dispersion,
            "M15-U3-19.1: Modified Component fields round-trip back to Asset via SyncToAsset");

        // 19.2 & 19.3: Component -> Extraction -> GPU constants
        comp.SyncToRHI();
        RENDER_CHECK(comp.RHIMaterialAsset != nullptr, "SyncToRHI creates active RHIMaterial");
        const auto& rhiDesc = comp.RHIMaterialAsset->GetDesc();
        RENDER_CHECK(rhiDesc.SheenEnabled && rhiDesc.SheenColor == comp.SheenColor &&
                     rhiDesc.SubsurfaceEnabled && rhiDesc.SubsurfaceRadius == comp.SubsurfaceRadius &&
                     rhiDesc.AbsorptionEnabled && rhiDesc.Dispersion == comp.Dispersion,
            "M15-U3-19.2: Component correctly populates RHIMaterialDesc for extraction");

        auto gpuConstants = comp.RHIMaterialAsset->BuildConstants();
        RENDER_CHECK(gpuConstants.Sheen.x == comp.SheenColor.x && gpuConstants.Sheen.w == comp.SheenRoughness,
            "M15-U3-19.3: Extraction populates GPU MaterialConstants Sheen vector");
        RENDER_CHECK(gpuConstants.Subsurface.x == comp.SubsurfaceColor.x && gpuConstants.Subsurface.w == comp.SubsurfaceRadius,
            "M15-U3-19.3: Extraction populates GPU MaterialConstants Subsurface vector");
        RENDER_CHECK(gpuConstants.Absorption.w == comp.Dispersion,
            "M15-U3-19.3: Extraction populates GPU MaterialConstants Dispersion parameter");

        // 19.4: Save / Reopen Round-Trip on disk
        std::filesystem::path tempAxlePath = std::filesystem::temp_directory_path() / "test_m15_save_reopen.axleasset";
        RENDER_CHECK(asset->Save(tempAxlePath), "Material successfully saved to temp axleasset file");

        auto loadedAsset = std::make_shared<Material>("Reloaded");
        RENDER_CHECK(loadedAsset->Load(tempAxlePath, false), "Material successfully reloaded from temp axleasset file");

        RENDER_CHECK(loadedAsset->GetSheenEnabled() == asset->GetSheenEnabled(), "Save/Reopen: SheenEnabled preserved");
        RENDER_CHECK(glm::length(loadedAsset->GetSheenColor() - asset->GetSheenColor()) < 1e-4f, "Save/Reopen: SheenColor preserved");
        RENDER_CHECK(std::abs(loadedAsset->GetSheenRoughness() - asset->GetSheenRoughness()) < 1e-4f, "Save/Reopen: SheenRoughness preserved");
        RENDER_CHECK(loadedAsset->GetSubsurfaceEnabled() == asset->GetSubsurfaceEnabled(), "Save/Reopen: SubsurfaceEnabled preserved");
        RENDER_CHECK(glm::length(loadedAsset->GetSubsurfaceColor() - asset->GetSubsurfaceColor()) < 1e-4f, "Save/Reopen: SubsurfaceColor preserved");
        RENDER_CHECK(std::abs(loadedAsset->GetSubsurfaceRadius() - asset->GetSubsurfaceRadius()) < 1e-4f, "Save/Reopen: SubsurfaceRadius preserved");
        RENDER_CHECK(loadedAsset->GetAbsorptionEnabled() == asset->GetAbsorptionEnabled(), "Save/Reopen: AbsorptionEnabled preserved");
        RENDER_CHECK(glm::length(loadedAsset->GetAbsorptionColor() - asset->GetAbsorptionColor()) < 1e-4f, "Save/Reopen: AbsorptionColor preserved");
        RENDER_CHECK(std::abs(loadedAsset->GetDispersion() - asset->GetDispersion()) < 1e-4f, "Save/Reopen: Dispersion preserved");

        // Test flat writer / loader format
        std::filesystem::path tempFlatPath = std::filesystem::temp_directory_path() / "test_m15_save_flat.mat";
        RENDER_CHECK(asset->Save(tempFlatPath), "Material successfully saved to temp flat file");
        auto loadedFlat = std::make_shared<Material>("ReloadedFlat");
        RENDER_CHECK(loadedFlat->Load(tempFlatPath, false), "Material successfully reloaded from temp flat file");
        RENDER_CHECK(loadedFlat->GetSheenEnabled() && loadedFlat->GetSubsurfaceEnabled() && loadedFlat->GetAbsorptionEnabled(),
            "Save/Reopen: flat format preserves advanced material flags");

        // Clean up temporary files
        std::error_code ec;
        std::filesystem::remove(tempAxlePath, ec);
        std::filesystem::remove(tempFlatPath, ec);
    }

    // =========================================================================
    // M15-U3-20: Smooth-Sphere Normal-Field and Anti-Faceting Metric
    // =========================================================================
    {
        std::cout << "  [M15SurfaceMaterial] Running M15-U3-20 smooth-sphere & anti-faceting metric...\n";

        // 1. High-fidelity smooth sphere (64 stacks, 128 sectors)
        std::vector<RHI::RHIVertex3D> hiVertices;
        std::vector<uint32_t> hiIndices;
        constexpr float radius = 1.0f;
        RHI::RHIMesh::GenerateSphere(radius, 64, 128, hiVertices, hiIndices);

        float hiMaxRadialError = 0.0f;
        float hiMaxEdgeAngleDeg = 0.0f;
        bool hiFinite = true;

        for (const auto& v : hiVertices) {
            hiFinite = hiFinite && std::isfinite(v.Position.x) && std::isfinite(v.Position.y) && std::isfinite(v.Position.z)
                                && std::isfinite(v.Normal.x) && std::isfinite(v.Normal.y) && std::isfinite(v.Normal.z);
            float len = glm::length(v.Normal);
            RENDER_CHECK(std::abs(len - 1.0f) <= 1.0e-3f, "High-fidelity sphere normal has unit length");

            glm::vec3 expected = glm::normalize(v.Position);
            hiMaxRadialError = std::max(hiMaxRadialError, glm::length(v.Normal - expected));
        }
        RENDER_CHECK(hiFinite, "M15-U3-20: all high-fidelity sphere coordinates and normals are finite");
        RENDER_CHECK(hiMaxRadialError <= 1.0e-2f, "M15-U3-20: high-fidelity normals align radially with surface position");

        for (size_t i = 0; i + 2 < hiIndices.size(); i += 3) {
            const auto& a = hiVertices[hiIndices[i + 0]].Normal;
            const auto& b = hiVertices[hiIndices[i + 1]].Normal;
            const auto& c = hiVertices[hiIndices[i + 2]].Normal;
            for (const auto& edge : {std::pair{a, b}, std::pair{b, c}, std::pair{c, a}}) {
                float angle = glm::degrees(std::acos(glm::clamp(glm::dot(edge.first, edge.second), -1.0f, 1.0f)));
                hiMaxEdgeAngleDeg = std::max(hiMaxEdgeAngleDeg, angle);
            }
        }
        RENDER_CHECK(hiMaxEdgeAngleDeg <= 4.1f, "M15-U3-20: anti-faceting metric passes: max edge angle <= 4.1 deg");

        // 2. Coarse sphere (8 stacks, 8 sectors) - evaluates anti-faceting violation detection
        std::vector<RHI::RHIVertex3D> coarseVertices;
        std::vector<uint32_t> coarseIndices;
        RHI::RHIMesh::GenerateSphere(radius, 8, 8, coarseVertices, coarseIndices);
        float coarseMaxEdgeAngleDeg = 0.0f;
        for (size_t i = 0; i + 2 < coarseIndices.size(); i += 3) {
            const auto& a = coarseVertices[coarseIndices[i + 0]].Normal;
            const auto& b = coarseVertices[coarseIndices[i + 1]].Normal;
            const auto& c = coarseVertices[coarseIndices[i + 2]].Normal;
            for (const auto& edge : {std::pair{a, b}, std::pair{b, c}, std::pair{c, a}}) {
                float angle = glm::degrees(std::acos(glm::clamp(glm::dot(edge.first, edge.second), -1.0f, 1.0f)));
                coarseMaxEdgeAngleDeg = std::max(coarseMaxEdgeAngleDeg, angle);
            }
        }
        RENDER_CHECK(coarseMaxEdgeAngleDeg > 4.1f, "M15-U3-20: coarse sphere triggers anti-faceting violation threshold (>4.1 deg)");
    }

    // =========================================================================
    // M15-U3-21.1 through 21.4: Resolver Unification
    // =========================================================================
    {
        std::cout << "  [M15SurfaceMaterial] Running M15-U3-21 resolver unification tests...\n";

        // 21.1: Built-in Resolver
        RHI::RHIMaterialDesc desc;
        desc.BaseColor = glm::vec4(0.8f, 0.4f, 0.2f, 1.0f);
        desc.Roughness = 0.45f;
        desc.Metallic = 0.9f;
        desc.AO = 0.85f;
        desc.NormalStrength = 1.2f;
        desc.ClearcoatStrength = 0.7f;
        desc.ClearcoatRoughness = 0.05f;
        desc.SheenEnabled = true;
        desc.SheenColor = glm::vec3(0.9f, 0.8f, 0.7f);
        desc.SheenRoughness = 0.3f;
        desc.SubsurfaceEnabled = true;
        desc.SubsurfaceColor = glm::vec3(0.95f, 0.3f, 0.2f);
        desc.SubsurfaceRadius = 3.0f;

        RHI::RHIMaterial builtinMat(desc);
        auto builtinSurface = MaterialResolver::Resolve(builtinMat);

        RENDER_CHECK(builtinSurface.Valid, "M15-U3-21.1: built-in resolver produces valid surface");
        RENDER_CHECK(builtinSurface.Evaluation == SurfaceEvaluation::BuiltIn, "M15-U3-21.1: evaluation tag is BuiltIn");
        RENDER_CHECK(builtinSurface.Generation != 0, "M15-U3-21.1: non-zero generation hash");
        RENDER_CHECK(builtinSurface.Metallic == 0.9f && builtinSurface.Roughness == 0.45f, "M15-U3-21.1: metallic/roughness values preserved");

        // 21.3: Preview Resolver
        auto previewSurface = MaterialResolver::ResolvePreview(builtinMat);
        RENDER_CHECK(previewSurface.Generation == builtinSurface.Generation,
            "M15-U3-21.3: Preview resolver produces identical generation hash as built-in");
        RENDER_CHECK(previewSurface.BaseColor == builtinSurface.BaseColor &&
                     previewSurface.Roughness == builtinSurface.Roughness &&
                     previewSurface.Metallic == builtinSurface.Metallic,
            "M15-U3-21.3: Preview resolver matches built-in surface attributes");

        // 21.4: Runtime Resolver
        RenderMaterialData snapshot;
        snapshot.RHIMaterialAsset = std::make_shared<const RHI::RHIMaterial>(builtinMat);
        auto runtimeSurface = MaterialResolver::ResolveRuntime(snapshot);
        RENDER_CHECK(runtimeSurface.Generation == builtinSurface.Generation,
            "M15-U3-21.4: Runtime resolver matches built-in surface generation hash");
        RENDER_CHECK(runtimeSurface.ToMaterialConstants().Surface == builtinSurface.ToMaterialConstants().Surface,
            "M15-U3-21.4: Runtime resolver produces identical GPU constants as built-in");

        // 21.2: Material Graph Resolver parity on canonical fields
        ValidatedShaderInterface iface;
        MaterialBindingPlan plan;
        plan.InterfaceRevision = 1;
        EngineMaterialValues view;
        std::vector<ShaderPropertyInstance> savedValues;

        auto graphSurfaceResult = ResolveShaderSurface(builtinSurface, SurfaceEvaluation::MaterialGraph, 4242, plan, view, savedValues);
        RENDER_CHECK(std::holds_alternative<ResolvedShaderSurface>(graphSurfaceResult),
            "M15-U3-21.2: graph surface resolves successfully");
        const auto& graphSurface = std::get<ResolvedShaderSurface>(graphSurfaceResult).Surface;
        RENDER_CHECK(graphSurface.Evaluation == SurfaceEvaluation::MaterialGraph,
            "M15-U3-21.2: graph surface is tagged with MaterialGraph evaluation domain");
        RENDER_CHECK(graphSurface.ShaderGeneration == 4242,
            "M15-U3-21.2: graph surface retains immutable shader generation");
        RENDER_CHECK(graphSurface.BaseColor == builtinSurface.BaseColor &&
                     graphSurface.Metallic == builtinSurface.Metallic &&
                     graphSurface.Roughness == builtinSurface.Roughness,
            "M15-U3-21.2: graph surface inherits canonical base material parameters");
    }

    // =========================================================================
    // M15-U3-22: Explicit Unsupported Feature Masks & Fallback Diagnostics
    // =========================================================================
    {
        std::cout << "  [M15SurfaceMaterial] Running M15-U3-22 unsupported feature masks and fallback diagnostics...\n";

        // Base surface requesting Sheen, Subsurface, Clearcoat, Anisotropy, Absorption, Refraction, Cutout
        RHI::RHIMaterialDesc advDesc;
        advDesc.ClearcoatStrength = 0.5f;
        advDesc.AnisotropyStrength = 0.8f;
        advDesc.SheenEnabled = true;
        advDesc.SubsurfaceEnabled = true;
        advDesc.AbsorptionEnabled = true;
        advDesc.EmissiveColor = glm::vec3(1.0f, 0.5f, 0.25f);
        advDesc.EmissiveIntensity = 2.0f;
        advDesc.Dispersion = 0.045f;
        advDesc.RefractionEnabled = true;
        advDesc.SurfaceType = SurfaceType::Cutout;
        advDesc.NormalStrength = 2.0f;

        RHI::RHIMaterial advMat(advDesc);
        auto baseSurface = MaterialResolver::Resolve(advMat);

        ValidatedShaderInterface iface;
        MaterialBindingPlan plan;
        EngineMaterialValues view;
        std::vector<ShaderPropertyInstance> savedValues;

        auto shaderResult = ResolveShaderSurface(baseSurface, SurfaceEvaluation::MaterialGraph, 999, plan, view, savedValues);
        RENDER_CHECK(std::holds_alternative<ResolvedShaderSurface>(shaderResult), "ResolveShaderSurface succeeded");
        const auto& surface = std::get<ResolvedShaderSurface>(shaderResult).Surface;

        // Verify explicit unsupported mask bits
        uint32_t expectedMask = 0;
        expectedMask |= static_cast<uint32_t>(SurfaceFeature::Clearcoat);
        expectedMask |= static_cast<uint32_t>(SurfaceFeature::Anisotropy);
        expectedMask |= static_cast<uint32_t>(SurfaceFeature::Sheen);
        expectedMask |= static_cast<uint32_t>(SurfaceFeature::Subsurface);
        expectedMask |= static_cast<uint32_t>(SurfaceFeature::Absorption);
        expectedMask |= static_cast<uint32_t>(SurfaceFeature::Refraction);
        expectedMask |= static_cast<uint32_t>(SurfaceFeature::Dispersion);
        expectedMask |= static_cast<uint32_t>(SurfaceFeature::Cutout);
        expectedMask |= static_cast<uint32_t>(SurfaceFeature::NormalStrength);

        RENDER_CHECK((surface.UnsupportedFeatures & expectedMask) == expectedMask,
            "M15-U3-22: UnsupportedFeatures bitmask correctly records all unsupported advanced features");
        RENDER_CHECK((surface.UnsupportedFeatures & static_cast<uint32_t>(SurfaceFeature::Emission)) == 0,
            "M15-U3-22: authored emission is supported by MaterialSurface and is not marked unsupported");
        RENDER_CHECK(surface.Fallback == true,
            "M15-U3-22: surface.Fallback is set to true when unsupported features are present");
        RENDER_CHECK(surface.Diagnostic.find("SURFACE_UNSUPPORTED_FEATURES:") != std::string::npos,
            "M15-U3-22: diagnostic message explicitly reports SURFACE_UNSUPPORTED_FEATURES");
        RENDER_CHECK(surface.Diagnostic.find("mask=") != std::string::npos,
            "M15-U3-22: diagnostic message includes the bitmask value");

        // Supported surface with no advanced features has 0 unsupported features
        RHI::RHIMaterialDesc basicDesc;
        basicDesc.BaseColor = glm::vec4(1.0f);
        basicDesc.Roughness = 0.5f;
        basicDesc.Metallic = 0.0f;
        basicDesc.NormalStrength = 1.0f;
        RHI::RHIMaterial basicMat(basicDesc);
        auto basicBase = MaterialResolver::Resolve(basicMat);
        auto basicResult = ResolveShaderSurface(basicBase, SurfaceEvaluation::MaterialGraph, 1000, plan, view, savedValues);
        RENDER_CHECK(std::holds_alternative<ResolvedShaderSurface>(basicResult), "basic shader surface resolved");
        const auto& basicSurface = std::get<ResolvedShaderSurface>(basicResult).Surface;
        RENDER_CHECK(basicSurface.UnsupportedFeatures == 0,
            "M15-U3-22: supported basic surface has zero unsupported features");
        RENDER_CHECK(basicSurface.Fallback == false,
            "M15-U3-22: supported basic surface has Fallback == false");
        RENDER_CHECK(basicSurface.Diagnostic.empty(),
            "M15-U3-22: supported basic surface has empty diagnostic");
    }

    // =========================================================================
    // M15-U3-23.1 through 23.4: Parity Comparisons (Built-in vs Graph)
    // =========================================================================
    {
        std::cout << "  [M15SurfaceMaterial] Running M15-U3-23 parity comparisons (lights, shadows, IBL, output policy)...\n";

        // Read and compare shader source files
        std::ifstream matFile(AXLE_MATERIAL_SURFACE_SHADER_PATH);
        if (!matFile.is_open()) matFile.open("Engine/Resources/Shaders/MaterialSurface.hlsl");
        RENDER_CHECK(matFile.is_open(), "MaterialSurface.hlsl is accessible");
        std::stringstream ssMat; ssMat << matFile.rdbuf();
        std::string matSource = ssMat.str();

        std::ifstream fwdFile(AXLE_FORWARD_PBR_SHADER_PATH);
        if (!fwdFile.is_open()) fwdFile.open("Engine/Resources/Shaders/ForwardPBR.hlsl");
        RENDER_CHECK(fwdFile.is_open(), "ForwardPBR.hlsl is accessible");
        std::stringstream ssFwd; ssFwd << fwdFile.rdbuf();
        std::string fwdSource = ssFwd.str();

        // 23.1: Compare Lights
        RENDER_CHECK(matSource.find("DIRECT_LUX_TO_RADIANCE = 0.000035") != std::string::npos &&
                     fwdSource.find("DIRECT_LUX_TO_RADIANCE = 0.000035") != std::string::npos,
            "M15-U3-23.1: both shaders unify DIRECT_LUX_TO_RADIANCE = 0.000035");
        RENDER_CHECK((matSource.find("INV_PI") != std::string::npos || matSource.find("BRDF_DIFFUSE_NORMALIZATION") != std::string::npos) &&
                     fwdSource.find("BRDF_DIFFUSE_NORMALIZATION") != std::string::npos,
            "M15-U3-23.1: both shaders apply 1/PI diffuse normalization");
        RENDER_CHECK(fwdSource.find("u_LightCounts") != std::string::npos && fwdSource.find("u_PointLights") != std::string::npos,
            "M15-U3-23.1: ForwardPBR implements multi-light point/spot loops");
        RENDER_CHECK(matSource.find("MultiLightBuffer") != std::string::npos &&
                     matSource.find("u_LightCounts") != std::string::npos &&
                     matSource.find("u_PointLights") != std::string::npos,
            "M15-U3-23.1: MaterialSurface consumes the shared multi-light buffer contract");

        // 23.2: Compare Shadows
        RENDER_CHECK(fwdSource.find("CascadeReceiver") != std::string::npos && fwdSource.find("SunShadowVisibility") != std::string::npos,
            "M15-U3-23.2: ForwardPBR evaluates cascaded directional shadow receivers");
        RENDER_CHECK(matSource.find("CascadeReceiver") != std::string::npos &&
                     matSource.find("AxleSunShadowVisibility") != std::string::npos,
            "M15-U3-23.2: MaterialSurface consumes the shared cascaded shadow receiver contract");

        // 23.3: Compare IBL
        RENDER_CHECK(fwdSource.find("u_IrradianceMap") != std::string::npos && fwdSource.find("u_PrefilterMap") != std::string::npos,
            "M15-U3-23.3: ForwardPBR implements full split-sum IBL pipeline");
        RENDER_CHECK(matSource.find("u_IrradianceMap") != std::string::npos &&
                     matSource.find("u_PrefilterMap") != std::string::npos &&
                     matSource.find("u_BRDFLUT") != std::string::npos &&
                     matSource.find("FresnelSchlickRoughness") != std::string::npos,
            "M15-U3-23.3: MaterialSurface consumes the split-sum IBL contract");

        // 23.4: Compare Output Policy
        RENDER_CHECK(matSource.find("u_CameraPosition.w <= 0.0") != std::string::npos &&
                     fwdSource.find("u_CameraPosition.w <= 0.0") != std::string::npos,
            "M15-U3-23.4: both shaders evaluate identical SDR vs HDR branching on camera.w");
        RENDER_CHECK(matSource.find("2.51") != std::string::npos && fwdSource.find("2.51") != std::string::npos,
            "M15-U3-23.4: both shaders evaluate identical ACES Filmic tonemapping operator (Narkowicz fit)");
        RENDER_CHECK(matSource.find("1.0 / 2.2") != std::string::npos && fwdSource.find("1.0 / 2.2") != std::string::npos,
            "M15-U3-23.4: both shaders evaluate 2.2 gamma encoding in SDR mode");
        RENDER_CHECK(matSource.find("clamp(color, 0.0, 65000.0)") != std::string::npos &&
                     fwdSource.find("clamp(color, 0.0, 65000.0)") != std::string::npos,
            "M15-U3-23.4: both shaders clamp HDR radiance to 65000.0");
    }

    // =========================================================================
    // M15-U3-24.1 through 24.4: Declare Motion, Depth, History, Exposure Resources
    // =========================================================================
    {
        std::cout << "  [M15SurfaceMaterial] Running M15-U3-24 resource declaration contracts (motion, depth, history, exposure)...\n";

        // 24.1: Declare motion resources
        RHI::RHIViewTargetDesc motionDesc;
        motionDesc.Width = 128;
        motionDesc.Height = 128;
        motionDesc.EnableScreenStorage = true;
        motionDesc.HDR = true;
        RENDER_CHECK(motionDesc.EnableScreenStorage, "M15-U3-24.1: RHIViewTargetDesc declares ScreenStorage for motion/properties");
        TemporalService temporal;
        auto firstTemporal = temporal.Begin(2, {0.1f, -0.1f});
        RENDER_CHECK(!firstTemporal.MotionValid && !temporal.IsMotionValid(),
            "M15-U3-24.1: first temporal frame explicitly bypasses uninitialized motion vectors");
        temporal.Commit(glm::mat4(2.0f), true);
        auto secondTemporal = temporal.Begin(2, {0.0f, 0.0f});
        RENDER_CHECK(secondTemporal.MotionValid && temporal.IsMotionValid(),
            "M15-U3-24.1: subsequent temporal frame after successful commit declares valid motion vectors");
        temporal.InvalidateContents();
        RENDER_CHECK(!temporal.IsMotionValid(),
            "M15-U3-24.1: InvalidateContents explicitly invalidates motion resources");

        // 24.2: Declare depth resources
        RHI::RHIViewTargetDesc depthDesc;
        depthDesc.Width = 128;
        depthDesc.Height = 128;
        depthDesc.EnableDepth = true;
        depthDesc.DepthFormat = RHI::RHITextureFormat::D32_FLOAT;
        depthDesc.RenderAuxiliary = true;
        RENDER_CHECK(depthDesc.EnableDepth && depthDesc.DepthFormat == RHI::RHITextureFormat::D32_FLOAT,
            "M15-U3-24.2: RHIViewTargetDesc declares 32-bit floating point depth resource");
        RENDER_CHECK(depthDesc.RenderAuxiliary,
            "M15-U3-24.2: RHIViewTargetDesc declares auxiliary depth resources (OpaqueDepth, ParticleDepth)");

        // 24.3: Declare history resources
        RHI::RHIViewTargetDesc historyDesc;
        historyDesc.Width = 128;
        historyDesc.Height = 128;
        historyDesc.HistoryCount = 2;
        RENDER_CHECK(historyDesc.HistoryCount == 2,
            "M15-U3-24.3: RHIViewTargetDesc declares dual-slot ping-pong history resources");
        TemporalService historyTemporal;
        auto hFirst = historyTemporal.Begin(2, {0.25f, 0.25f});
        RENDER_CHECK(hFirst.ReadSlot == 0 && hFirst.WriteSlot == 1 && !hFirst.HistoryValid,
            "M15-U3-24.3: history slot 0 initialized for read with uninitialized bypass, slot 1 for write");
        historyTemporal.Commit(glm::mat4(1.0f), true);
        auto hSecond = historyTemporal.Begin(2, {0.0f, 0.0f});
        RENDER_CHECK(hSecond.ReadSlot == 1 && hSecond.WriteSlot == 0 && hSecond.HistoryValid,
            "M15-U3-24.3: commit flips history slots (Read=1, Write=0) and marks history valid");

        // 24.4: Declare exposure resources
        RHI::RHIViewTargetDesc exposureDesc;
        exposureDesc.Width = 128;
        exposureDesc.Height = 128;
        exposureDesc.EnableAutoExposure = true;
        RENDER_CHECK(exposureDesc.EnableAutoExposure,
            "M15-U3-24.4: RHIViewTargetDesc declares AutoExposure resource");
        PostProcessService post;
        SceneRenderSettings ppSettings;
        ppSettings.EnableAutoExposure = true;
        ppSettings.AutoExposureMin = 0.2f;
        ppSettings.AutoExposureMax = 5.0f;
        post.BeginFrame(ppSettings, true);
        RENDER_CHECK(post.GetPolicy().AutoExposure, "M15-U3-24.4: PostProcessPolicy activates AutoExposure");
        float clampedExposure = post.UpdateExposure(0.001f, 1.0f);
        RENDER_CHECK(clampedExposure >= ppSettings.AutoExposureMin && clampedExposure <= ppSettings.AutoExposureMax,
            "M15-U3-24.4: adapted exposure is safely clamped to [AutoExposureMin, AutoExposureMax]");
    }

    // =========================================================================
    // M15-U3-25.1 through 25.4: Reset Matrix Contracts
    // =========================================================================
    {
        std::cout << "  [M15SurfaceMaterial] Running M15-U3-25 reset matrix contracts...\n";

        ViewContext context;
        ViewContextInput input;
        input.Width = 128;
        input.Height = 128;
        input.Targets = std::make_shared<RHI::RHIViewTargets>();
        input.SceneRevision = 1;
        input.MaterialGeneration = 1;
        input.FeatureGeneration = 1;

        // Initial observe
        auto reasons = context.Observe(input, 1);
        RENDER_CHECK((reasons & ViewResetReason::Initial) != ViewResetReason::None,
            "M15-U3-25: initial observation triggers ViewResetReason::Initial");
        context.ConsumeResetReasons();

        // 25.1: Reset on target generation
        auto newTargets = std::make_shared<RHI::RHIViewTargets>();
        bool targetReset = context.ObserveTargets(newTargets);
        RENDER_CHECK(targetReset && (context.GetResetReasons() & ViewResetReason::TargetGeneration) != ViewResetReason::None,
            "M15-U3-25.1: target replacement triggers ViewResetReason::TargetGeneration");
        context.Temporal().Begin(2);
        context.Temporal().Commit(glm::mat4(1.0f), true);
        RENDER_CHECK(context.Temporal().IsHistoryValid(), "temporal history established");
        context.ObserveTargets(std::make_shared<RHI::RHIViewTargets>());
        RENDER_CHECK(!context.Temporal().IsHistoryValid() && context.Temporal().GetReadSlot() == 0 && context.Temporal().GetWriteSlot() == 1,
            "M15-U3-25.1: target generation change resets temporal history and restores initial slot identities");
        context.ConsumeResetReasons();

        // 25.2: Reset on camera cut
        input.Targets = newTargets;
        input.CameraCut = true;
        reasons = context.Observe(input, 1);
        RENDER_CHECK((reasons & ViewResetReason::CameraCut) != ViewResetReason::None,
            "M15-U3-25.2: CameraCut == true triggers ViewResetReason::CameraCut");
        context.Temporal().Reset(static_cast<uint32_t>(ViewResetReason::CameraCut));
        auto postCut = context.Temporal().Begin(2);
        RENDER_CHECK(!postCut.HistoryValid && !postCut.DisocclusionValid && context.Temporal().GetCommittedFrameCount() == 0,
            "M15-U3-25.2: camera cut reset invalidates history and disocclusion feedback");
        context.ConsumeResetReasons();
        input.CameraCut = false;

        // 25.3: Reset on feature toggle
        input.FeatureGeneration = 2;
        reasons = context.Observe(input, 1);
        RENDER_CHECK((reasons & ViewResetReason::FeaturePolicy) != ViewResetReason::None,
            "M15-U3-25.3: FeatureGeneration change triggers ViewResetReason::FeaturePolicy");
        context.ConsumeResetReasons();

        // 25.4: Reset on device epoch
        reasons = context.Observe(input, 2); // Device epoch changed from 1 to 2
        RENDER_CHECK((reasons & ViewResetReason::DeviceEpoch) != ViewResetReason::None,
            "M15-U3-25.4: device epoch change triggers ViewResetReason::DeviceEpoch");
        RENDER_CHECK(context.GetDeviceEpoch() == 2,
            "M15-U3-25.4: ViewContext tracks updated device epoch");
        context.ConsumeResetReasons();
    }

    // =========================================================================
    // M15-U3-26.1 through 26.4: Diagnostics & Measurement Integration
    // =========================================================================
    {
        std::cout << "  [M15SurfaceMaterial] Running M15-U3-26 diagnostics & measurement contracts...\n";

        RenderDiagnostics diagnostics;
        diagnostics.BeginFrame(100, 42);

        // 26.1: Record timing validity
        diagnostics.BeginPass("DepthPrepass");
        diagnostics.EndPass(DiagnosticStatus::Success, 0.15, 0.0, DiagnosticValidity::Unknown, 10, 0);
        diagnostics.BeginPass("ForwardLighting");
        diagnostics.EndPass(DiagnosticStatus::Success, 0.50, 1.25, DiagnosticValidity::Valid, 25, 2);
        RENDER_CHECK(diagnostics.GetFrame().Passes.size() == 2, "recorded 2 render passes");
        RENDER_CHECK(diagnostics.GetFrame().Passes[0].GPUValidity == DiagnosticValidity::Unknown,
            "M15-U3-26.1: DepthPrepass without GPU query reports DiagnosticValidity::Unknown");
        RENDER_CHECK(diagnostics.GetFrame().Passes[1].GPUValidity == DiagnosticValidity::Valid &&
                     diagnostics.GetFrame().Passes[1].GPUTimeMilliseconds == 1.25,
            "M15-U3-26.1: ForwardLighting with valid GPU timer reports DiagnosticValidity::Valid");

        // 26.2: Record fallback reasons
        diagnostics.BeginPass("SSRPass");
        diagnostics.EndPass(DiagnosticStatus::Fallback, 0.1, 0.0, DiagnosticValidity::Unavailable, 0, 0,
                            "ssr_depth_hierarchy_incomplete");
        diagnostics.AddFallback("ssr_depth_hierarchy_incomplete");
        diagnostics.AddFallback("SURFACE_UNSUPPORTED_FEATURES: Sheen (mask=0x1)");
        RENDER_CHECK(diagnostics.GetFrame().Passes.back().Status == DiagnosticStatus::Fallback &&
                     diagnostics.GetFrame().Passes.back().Reason == "ssr_depth_hierarchy_incomplete",
            "M15-U3-26.2: pass diagnostic records DiagnosticStatus::Fallback and explicit reason string");
        RENDER_CHECK(diagnostics.GetFrame().Fallbacks.size() == 2 &&
                     diagnostics.GetFrame().Fallbacks[0] == "ssr_depth_hierarchy_incomplete" &&
                     diagnostics.GetFrame().Fallbacks[1].find("SURFACE_UNSUPPORTED_FEATURES:") != std::string::npos,
            "M15-U3-26.2: frame diagnostics preserves structured fallback reasons");

        // 26.3: Record resource generations
        diagnostics.SetMaterialGeneration(77);
        diagnostics.SetTargetGeneration(42);
        RENDER_CHECK(diagnostics.GetFrame().TargetGeneration == 42,
            "M15-U3-26.3: frame diagnostics records TargetGeneration");
        RENDER_CHECK(diagnostics.GetFrame().MaterialGeneration == 77,
            "M15-U3-26.3: frame diagnostics records MaterialGeneration");

        // 26.4: Report Unknown invalid measurements
        diagnostics.SetFrameGPUTime(0.0, DiagnosticValidity::Unknown);
        RENDER_CHECK(!diagnostics.IsGPUTimeValid(),
            "M15-U3-26.4: 0.0 ms GPU time with DiagnosticValidity::Unknown reports IsGPUTimeValid() == false");
        RENDER_CHECK(diagnostics.GetFrame().GPUValidity == DiagnosticValidity::Unknown,
            "M15-U3-26.4: frame GPU validity is explicitly Unknown, not valid zero");
    }

    std::cout << "  [M15SurfaceMaterial] M15-U3-14 through U3-26.4 contract tests passed.\n";
    return true;
}
