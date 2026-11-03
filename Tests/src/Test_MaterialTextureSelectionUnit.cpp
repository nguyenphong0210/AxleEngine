#include "MaterialTestHarness.hpp"
#include <Axle/Material.hpp>
#include <Axle/Components.hpp>
#include <Axle/RHI/RHIMaterial.hpp>

using namespace Axle;
using namespace Axle::MaterialTests;

namespace {

    bool TestTextureRoleAssignment() {
        // Albedo and Emissive are color (sRGB) roles.
        // All others (Normal, Roughness, Metallic, AO, Clearcoat, Anisotropy, Detail Normal, Detail Mask) are linear data.
        auto isColorSlot = [](RHI::RHITextureSlot slot) -> bool {
            return slot == RHI::RHITextureSlot::Albedo || slot == RHI::RHITextureSlot::Emissive;
        };

        MAT_TEST_CHECK(isColorSlot(RHI::RHITextureSlot::Albedo), "Albedo must be color role");
        MAT_TEST_CHECK(isColorSlot(RHI::RHITextureSlot::Emissive), "Emissive must be color role");
        MAT_TEST_CHECK(!isColorSlot(RHI::RHITextureSlot::Normal), "Normal must be linear data role");
        MAT_TEST_CHECK(!isColorSlot(RHI::RHITextureSlot::Roughness), "Roughness must be linear data role");
        MAT_TEST_CHECK(!isColorSlot(RHI::RHITextureSlot::Metallic), "Metallic must be linear data role");
        MAT_TEST_CHECK(!isColorSlot(RHI::RHITextureSlot::AO), "AO must be linear data role");
        MAT_TEST_CHECK(!isColorSlot(RHI::RHITextureSlot::Clearcoat), "Clearcoat must be linear data role");
        MAT_TEST_CHECK(!isColorSlot(RHI::RHITextureSlot::AnisotropyFlow), "Anisotropy must be linear data role");
        MAT_TEST_CHECK(!isColorSlot(RHI::RHITextureSlot::DetailNormal), "Detail normal must be linear data role");
        MAT_TEST_CHECK(!isColorSlot(RHI::RHITextureSlot::DetailMask), "Detail mask must be linear data role");

        return true;
    }

    bool TestDisabledMapRetainsPath() {
        MaterialComponent comp;
        comp.AlbedoPath = "textures/car_red.png";
        comp.UseAlbedoMap = true;
        comp.NormalPath = "textures/car_normal.png";
        comp.UseNormalMap = false; // Disabled!

        comp.SyncToRHI();
        MAT_TEST_CHECK(comp.AlbedoPath == "textures/car_red.png", "Albedo path retained");
        MAT_TEST_CHECK(comp.NormalPath == "textures/car_normal.png", "Disabled normal path must be retained");
        
        // In RHI desc, disabled normal map path must NOT be bound to GPU
        auto& desc = comp.RHIMaterialAsset->GetDesc();
        MAT_TEST_CHECK(desc.AlbedoPath == "textures/car_red.png", "RHI desc albedo bound");
        MAT_TEST_CHECK(desc.NormalPath.empty(), "RHI desc normal must be empty when disabled");

        // Re-enabling normal map
        comp.UseNormalMap = true;
        comp.SyncToRHI();
        auto& desc2 = comp.RHIMaterialAsset->GetDesc();
        MAT_TEST_CHECK(desc2.NormalPath == "textures/car_normal.png", "RHI desc normal restored when re-enabled");

        // Clearing removes path entirely
        comp.NormalPath.clear();
        comp.UseNormalMap = false;
        comp.SyncToRHI();
        MAT_TEST_CHECK(comp.NormalPath.empty(), "Normal path cleared");
        MAT_TEST_CHECK(comp.RHIMaterialAsset->GetDesc().NormalPath.empty(), "RHI normal path cleared");

        return true;
    }

    bool TestAutomotiveMapsDisableBehavior() {
        MaterialComponent comp;
        comp.ClearcoatPath = "textures/coat.png";
        comp.UseClearcoatMap = false;
        comp.AnisotropyFlowPath = "textures/flow.png";
        comp.UseAnisotropyFlowMap = false;
        comp.DetailNormalPath = "textures/detail_n.png";
        comp.UseDetailNormalMap = false;
        comp.DetailMaskPath = "textures/detail_m.png";
        comp.UseDetailMaskMap = false;

        comp.SyncToRHI();
        auto& desc = comp.RHIMaterialAsset->GetDesc();
        MAT_TEST_CHECK(desc.ClearcoatPath.empty(), "Disabled coat path must not be bound");
        MAT_TEST_CHECK(desc.AnisotropyFlowPath.empty(), "Disabled flow path must not be bound");
        MAT_TEST_CHECK(desc.DetailNormalPath.empty(), "Disabled detail normal must not be bound");
        MAT_TEST_CHECK(desc.DetailMaskPath.empty(), "Disabled detail mask must not be bound");

        // Authored paths must remain preserved
        MAT_TEST_CHECK(comp.ClearcoatPath == "textures/coat.png", "Authored coat path preserved");
        MAT_TEST_CHECK(comp.AnisotropyFlowPath == "textures/flow.png", "Authored flow path preserved");
        MAT_TEST_CHECK(comp.DetailNormalPath == "textures/detail_n.png", "Authored detail normal preserved");
        MAT_TEST_CHECK(comp.DetailMaskPath == "textures/detail_m.png", "Authored detail mask preserved");

        return true;
    }

} // namespace

bool RunMaterialTextureSelectionUnitTests() {
    std::cout << "  - Running TestTextureRoleAssignment ...\n";
    if (!TestTextureRoleAssignment()) return false;

    std::cout << "  - Running TestDisabledMapRetainsPath ...\n";
    if (!TestDisabledMapRetainsPath()) return false;

    std::cout << "  - Running TestAutomotiveMapsDisableBehavior ...\n";
    if (!TestAutomotiveMapsDisableBehavior()) return false;

    return true;
}

