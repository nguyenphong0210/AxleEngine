#include "MaterialTestHarness.hpp"
#include <Axle/Material.hpp>
#include <Axle/Components.hpp>
#include <Axle/ShaderPropertyPersistence.hpp>
#include <sstream>
#include <fstream>

using namespace Axle;
using namespace Axle::MaterialTests;

namespace {

    bool TestStandardMaterialPersistence() {
        ScopedTempDir temp;
        auto assetPath = temp.Path / "StandardMaterial.axleasset";

        auto mat = std::make_shared<Material>("TestStandard");
        mat->SetAlbedoColor(glm::vec4(1.25f, 0.8f, 0.5f, 0.75f)); // HDR + alpha
        mat->SetMetallic(0.85f);
        mat->SetRoughness(0.35f);
        mat->SetAO(0.92f);
        mat->SetNormalStrength(1.4f);
        mat->SetEmissionColor(glm::vec3(4.5f, 2.0f, 0.5f)); // HDR emission
        mat->SetEmissionIntensity(3.0f);
        mat->SetUVScale(glm::vec2(2.5f, 5.0f));  // Asymmetric UV scale
        mat->SetUVOffset(glm::vec2(-0.25f, 0.75f));

        mat->SetAlbedoTexture(nullptr, "textures/albedo.png");
        mat->SetNormalTexture(nullptr, "textures/normal.png");
        mat->SetRoughnessTexture(nullptr, "textures/roughness.png");
        mat->SetMetallicTexture(nullptr, "textures/metallic.png");
        mat->SetAOTexture(nullptr, "textures/ao.png");
        mat->SetEmissiveTexture(nullptr, "textures/emissive.png");

        mat->SetSurfaceType(SurfaceType::Cutout);
        mat->SetCullMode(CullMode::None);
        mat->SetAlphaCutoff(0.33f);
        mat->SetDepthWrite(true);
        mat->SetEnableSpecularHighlights(false);
        mat->SetEnableEnvironmentReflections(true);
        mat->SetRenderQueue(2450);

        MAT_TEST_CHECK(mat->Save(assetPath), "Failed to save standard material asset");

        auto loaded = std::make_shared<Material>("LoadedStandard");
        MAT_TEST_CHECK(loaded->Load(assetPath), "Failed to load standard material asset");

        MAT_TEST_APPROX_EQUAL(loaded->GetAlbedoColor().r, 1.25f, 1e-4f, "Albedo red HDR mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetAlbedoColor().g, 0.80f, 1e-4f, "Albedo green mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetAlbedoColor().b, 0.50f, 1e-4f, "Albedo blue mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetAlbedoColor().a, 0.75f, 1e-4f, "Albedo alpha mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetMetallic(), 0.85f, 1e-4f, "Metallic mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetRoughness(), 0.35f, 1e-4f, "Roughness mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetAO(), 0.92f, 1e-4f, "AO mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetNormalStrength(), 1.4f, 1e-4f, "Normal strength mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetEmissionColor().r, 4.5f, 1e-4f, "Emission red HDR mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetEmissionIntensity(), 3.0f, 1e-4f, "Emission intensity mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetUVScale().x, 2.5f, 1e-4f, "UV scale X mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetUVScale().y, 5.0f, 1e-4f, "UV scale Y mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetUVOffset().x, -0.25f, 1e-4f, "UV offset X mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetUVOffset().y, 0.75f, 1e-4f, "UV offset Y mismatch");

        MAT_TEST_CHECK(loaded->GetAlbedoPath() == "textures/albedo.png", "Albedo path mismatch");
        MAT_TEST_CHECK(loaded->GetNormalPath() == "textures/normal.png", "Normal path mismatch");
        MAT_TEST_CHECK(loaded->GetRoughnessPath() == "textures/roughness.png", "Roughness path mismatch");
        MAT_TEST_CHECK(loaded->GetMetallicPath() == "textures/metallic.png", "Metallic path mismatch");
        MAT_TEST_CHECK(loaded->GetAOPath() == "textures/ao.png", "AO path mismatch");
        MAT_TEST_CHECK(loaded->GetEmissivePath() == "textures/emissive.png", "Emissive path mismatch");

        MAT_TEST_CHECK(loaded->GetSurfaceType() == SurfaceType::Cutout, "Surface type mismatch");
        MAT_TEST_CHECK(loaded->GetCullMode() == CullMode::None, "Cull mode mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetAlphaCutoff(), 0.33f, 1e-4f, "Alpha cutoff mismatch");
        MAT_TEST_CHECK(loaded->GetDepthWrite() == true, "Depth write mismatch");
        MAT_TEST_CHECK(loaded->GetEnableSpecularHighlights() == false, "Specular highlights mismatch");
        MAT_TEST_CHECK(loaded->GetEnableEnvironmentReflections() == true, "Environment reflections mismatch");
        MAT_TEST_CHECK(loaded->GetRenderQueue() == 2450, "Render queue mismatch");

        return true;
    }

    bool TestAutomotiveMaterialPersistence() {
        ScopedTempDir temp;
        auto assetPath = temp.Path / "AutomotiveMaterial.axleasset";

        auto mat = std::make_shared<Material>("TestAutomotive");
        mat->SetClearcoatStrength(0.85f);
        mat->SetClearcoatRoughness(0.08f);
        mat->SetClearcoatTexture(nullptr, "textures/car_coat.png");

        mat->SetAnisotropyStrength(0.72f);
        mat->SetAnisotropyRotation(0.39f);
        mat->SetAnisotropyFlowTexture(nullptr, "textures/brush_flow.png");

        mat->SetDetailNormalStrength(1.5f);
        mat->SetDetailUVScale(glm::vec2(10.0f, 12.0f));
        mat->SetDetailUVOffset(glm::vec2(0.1f, 0.2f));
        mat->SetDetailNormalTexture(nullptr, "textures/leather_detail.png");
        mat->SetDetailMaskTexture(nullptr, "textures/leather_mask.png");

        mat->SetRefractionEnabled(true);
        mat->SetRefractionStrength(0.95f);
        mat->SetRefractionIOR(1.52f);
        mat->SetRefractionThickness(0.25f);
        mat->SetRefractionTint(glm::vec3(0.8f, 0.95f, 1.0f));

        MAT_TEST_CHECK(mat->Save(assetPath), "Failed to save automotive material asset");

        auto loaded = std::make_shared<Material>("LoadedAutomotive");
        MAT_TEST_CHECK(loaded->Load(assetPath), "Failed to load automotive material asset");

        MAT_TEST_APPROX_EQUAL(loaded->GetClearcoatStrength(), 0.85f, 1e-4f, "Clearcoat strength mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetClearcoatRoughness(), 0.08f, 1e-4f, "Clearcoat roughness mismatch");
        MAT_TEST_CHECK(loaded->GetClearcoatPath() == "textures/car_coat.png", "Clearcoat path mismatch");
        MAT_TEST_CHECK(loaded->IsClearcoatMapEnabled() == true, "Clearcoat enable mismatch");

        MAT_TEST_APPROX_EQUAL(loaded->GetAnisotropyStrength(), 0.72f, 1e-4f, "Anisotropy strength mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetAnisotropyRotation(), 0.39f, 1e-4f, "Anisotropy rotation mismatch");
        MAT_TEST_CHECK(loaded->GetAnisotropyFlowPath() == "textures/brush_flow.png", "Anisotropy flow path mismatch");
        MAT_TEST_CHECK(loaded->IsAnisotropyFlowMapEnabled() == true, "Anisotropy enable mismatch");

        MAT_TEST_APPROX_EQUAL(loaded->GetDetailNormalStrength(), 1.5f, 1e-4f, "Detail normal strength mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetDetailUVScale().x, 10.0f, 1e-4f, "Detail UV scale X mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetDetailUVScale().y, 12.0f, 1e-4f, "Detail UV scale Y mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetDetailUVOffset().x, 0.1f, 1e-4f, "Detail UV offset X mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetDetailUVOffset().y, 0.2f, 1e-4f, "Detail UV offset Y mismatch");
        MAT_TEST_CHECK(loaded->GetDetailNormalPath() == "textures/leather_detail.png", "Detail normal path mismatch");
        MAT_TEST_CHECK(loaded->GetDetailMaskPath() == "textures/leather_mask.png", "Detail mask path mismatch");
        MAT_TEST_CHECK(loaded->IsDetailNormalMapEnabled() == true, "Detail normal enable mismatch");
        MAT_TEST_CHECK(loaded->IsDetailMaskMapEnabled() == true, "Detail mask enable mismatch");

        MAT_TEST_CHECK(loaded->GetRefractionEnabled() == true, "Refraction enable mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetRefractionStrength(), 0.95f, 1e-4f, "Refraction strength mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetRefractionIOR(), 1.52f, 1e-4f, "Refraction IOR mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetRefractionThickness(), 0.25f, 1e-4f, "Refraction thickness mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetRefractionTint().r, 0.8f, 1e-4f, "Refraction tint R mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetRefractionTint().g, 0.95f, 1e-4f, "Refraction tint G mismatch");
        MAT_TEST_APPROX_EQUAL(loaded->GetRefractionTint().b, 1.0f, 1e-4f, "Refraction tint B mismatch");

        return true;
    }

    bool TestOldDefaultsOnReload() {
        ScopedTempDir temp;
        auto assetPath = temp.Path / "MinimalOld.axleasset";

        // Create a minimal asset missing new fields
        {
            std::ofstream f(assetPath);
            f << "Name: MinimalOld\n"
              << "AlbedoColor: [1, 1, 1, 1]\n"
              << "Roughness: 0.5\n"
              << "Metallic: 0.0\n";
        }

        auto loaded = std::make_shared<Material>("MinimalOld");
        MAT_TEST_CHECK(loaded->Load(assetPath), "Failed to load minimal old material");

        // Verify sensible defaults for fields omitted in old assets
        MAT_TEST_APPROX_EQUAL(loaded->GetClearcoatStrength(), 0.0f, 1e-4f, "Default clearcoat strength must be 0");
        MAT_TEST_APPROX_EQUAL(loaded->GetAnisotropyStrength(), 0.0f, 1e-4f, "Default anisotropy strength must be 0");
        MAT_TEST_CHECK(loaded->GetRefractionEnabled() == false, "Default refraction must be false");
        MAT_TEST_CHECK(loaded->GetDetailNormalPath().empty(), "Default detail normal path must be empty");
        MAT_TEST_CHECK(loaded->IsClearcoatMapEnabled() == false, "Default clearcoat map must be disabled");

        return true;
    }

    bool TestTypedCustomPersistence() {
        std::vector<ShaderPropertyInstance> properties;

        ShaderPropertyInstance pFloat;
        pFloat.Desc.Name = "u_Dissolve";
        pFloat.Desc.Type = ShaderPropertyType::Float;
        pFloat.FloatVal = 0.65f;
        pFloat.Desc.HasRange = true;
        pFloat.Desc.MinValue = 0.0f;
        pFloat.Desc.MaxValue = 1.0f;
        properties.push_back(pFloat);

        ShaderPropertyInstance pInt;
        pInt.Desc.Name = "u_LayerCount";
        pInt.Desc.Type = ShaderPropertyType::Int;
        pInt.IntVal = 5;
        properties.push_back(pInt);

        ShaderPropertyInstance pBool;
        pBool.Desc.Name = "u_EnableStripe";
        pBool.Desc.Type = ShaderPropertyType::Bool;
        pBool.BoolVal = true;
        properties.push_back(pBool);

        ShaderPropertyInstance pVec3;
        pVec3.Desc.Name = "u_LightOffset";
        pVec3.Desc.Type = ShaderPropertyType::Float3;
        pVec3.Vec4Val = glm::vec4(1.5f, -2.0f, 3.5f, 0.0f);
        properties.push_back(pVec3);

        ShaderPropertyInstance pColor;
        pColor.Desc.Name = "u_Tint";
        pColor.Desc.Type = ShaderPropertyType::Color;
        pColor.Vec4Val = glm::vec4(2.5f, 1.0f, 0.5f, 0.9f); // HDR color + alpha
        properties.push_back(pColor);

        ShaderPropertyInstance pTex;
        pTex.Desc.Name = "u_NoiseMap";
        pTex.Desc.Type = ShaderPropertyType::Texture2D;
        pTex.TexturePath = "textures/noise_perlin.png";
        properties.push_back(pTex);

        std::ostringstream ss;
        WriteShaderProperties(ss, properties);
        std::string serialized = ss.str();

        std::vector<ShaderPropertyInstance> reloaded;
        std::istringstream iss(serialized);
        std::string line;
        while (std::getline(iss, line)) {
            ReadShaderPropertyLine(line, reloaded);
        }

        MAT_TEST_CHECK(reloaded.size() == 6, "Expected 6 reloaded custom properties");

        auto findProp = [&](const std::string& name) -> const ShaderPropertyInstance* {
            for (const auto& p : reloaded) {
                if (p.Desc.Name == name) return &p;
            }
            return nullptr;
        };

        const auto* rFloat = findProp("u_Dissolve");
        MAT_TEST_CHECK(rFloat != nullptr, "Missing u_Dissolve");
        MAT_TEST_APPROX_EQUAL(rFloat->FloatVal, 0.65f, 1e-4f, "u_Dissolve float value mismatch");
        MAT_TEST_CHECK(rFloat->Desc.HasRange, "u_Dissolve range flag missing");
        MAT_TEST_APPROX_EQUAL(rFloat->Desc.MinValue, 0.0f, 1e-4f, "u_Dissolve min mismatch");
        MAT_TEST_APPROX_EQUAL(rFloat->Desc.MaxValue, 1.0f, 1e-4f, "u_Dissolve max mismatch");

        const auto* rInt = findProp("u_LayerCount");
        MAT_TEST_CHECK(rInt != nullptr, "Missing u_LayerCount");
        MAT_TEST_CHECK(rInt->IntVal == 5, "u_LayerCount int value mismatch");

        const auto* rBool = findProp("u_EnableStripe");
        MAT_TEST_CHECK(rBool != nullptr, "Missing u_EnableStripe");
        MAT_TEST_CHECK(rBool->BoolVal == true, "u_EnableStripe bool value mismatch");

        const auto* rVec3 = findProp("u_LightOffset");
        MAT_TEST_CHECK(rVec3 != nullptr, "Missing u_LightOffset");
        MAT_TEST_APPROX_EQUAL(rVec3->Vec4Val.x, 1.5f, 1e-4f, "u_LightOffset X mismatch");
        MAT_TEST_APPROX_EQUAL(rVec3->Vec4Val.y, -2.0f, 1e-4f, "u_LightOffset Y mismatch");
        MAT_TEST_APPROX_EQUAL(rVec3->Vec4Val.z, 3.5f, 1e-4f, "u_LightOffset Z mismatch");

        const auto* rColor = findProp("u_Tint");
        MAT_TEST_CHECK(rColor != nullptr, "Missing u_Tint");
        MAT_TEST_APPROX_EQUAL(rColor->Vec4Val.r, 2.5f, 1e-4f, "u_Tint R HDR mismatch");
        MAT_TEST_APPROX_EQUAL(rColor->Vec4Val.a, 0.9f, 1e-4f, "u_Tint alpha mismatch");

        const auto* rTex = findProp("u_NoiseMap");
        MAT_TEST_CHECK(rTex != nullptr, "Missing u_NoiseMap");
        MAT_TEST_CHECK(rTex->TexturePath == "textures/noise_perlin.png", "u_NoiseMap path mismatch");

        return true;
    }

    bool TestMaterialSynchronization() {
        auto asset = std::make_shared<Material>("SharedAsset");
        asset->SetAlbedoColor(glm::vec4(0.2f, 0.4f, 0.6f, 1.0f));
        asset->SetRoughness(0.45f);
        asset->SetMetallic(0.9f);

        MaterialComponent compA(1001, asset);
        MaterialComponent compB(1001, asset);

        MAT_TEST_APPROX_EQUAL(compA.GetRoughness(), 0.45f, 1e-4f, "compA roughness mismatch");
        MAT_TEST_APPROX_EQUAL(compB.GetRoughness(), 0.45f, 1e-4f, "compB roughness mismatch");

        // Mutate shared asset: both should sync
        asset->SetRoughness(0.12f);
        compA.SyncFromAsset();
        compB.SyncFromAsset();
        MAT_TEST_APPROX_EQUAL(compA.GetRoughness(), 0.12f, 1e-4f, "compA sync after asset change");
        MAT_TEST_APPROX_EQUAL(compB.GetRoughness(), 0.12f, 1e-4f, "compB sync after asset change");

        // MakeUnique on compA detaches it
        compA.MakeUnique();
        MAT_TEST_CHECK(compA.MaterialHandle == 0, "MakeUnique must clear MaterialHandle");
        MAT_TEST_CHECK(!compA.HasAsset(), "MakeUnique must clear HasAsset");

        // Modifying compA must not affect asset or compB
        compA.Roughness = 0.88f;
        MAT_TEST_APPROX_EQUAL(compA.GetRoughness(), 0.88f, 1e-4f, "compA local roughness");
        MAT_TEST_APPROX_EQUAL(compB.GetRoughness(), 0.12f, 1e-4f, "compB must remain linked to asset");
        MAT_TEST_APPROX_EQUAL(asset->GetRoughness(), 0.12f, 1e-4f, "asset must be unmodified by compA");

        return true;
    }

} // namespace

bool RunMaterialSerializationUnitTests() {
    std::cout << "  - Running TestStandardMaterialPersistence ...\n";
    if (!TestStandardMaterialPersistence()) return false;

    std::cout << "  - Running TestAutomotiveMaterialPersistence ...\n";
    if (!TestAutomotiveMaterialPersistence()) return false;

    std::cout << "  - Running TestOldDefaultsOnReload ...\n";
    if (!TestOldDefaultsOnReload()) return false;

    std::cout << "  - Running TestTypedCustomPersistence ...\n";
    if (!TestTypedCustomPersistence()) return false;

    std::cout << "  - Running TestMaterialSynchronization ...\n";
    if (!TestMaterialSynchronization()) return false;

    return true;
}

