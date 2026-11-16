#include "RenderTestCommon.hpp"
#include <Axle/Material.hpp>
#include <Axle/Components.hpp>
#include <Axle/ResolvedSurface.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/MaterialBindingPlan.hpp>
#include <Axle/FileSystem.hpp>
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

    std::cout << "  [M15SurfaceMaterial] M15-U3-14 through U3-19 contract tests passed.\n";
    return true;
}
