#include "RenderTestCommon.hpp"
#include <Axle/RenderContracts.hpp>
#include <Axle/ResolvedSurface.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
    std::string LoadTextFile(const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) return {};
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
}

bool RunM15AdvancedMaterialContractUnitTests() {
    using namespace Axle;
    std::cout << "[M15U9Contract] Running Milestone 15 Unit 9 Batch A (Charlie Sheen & Energy Conservation) Tests...\n";

    // M15-U9-01: Charlie sheen directional albedo vs numerical quadrature & white-furnace <= 1.0 layering
    {
        RENDER_CHECK(SheenContract::VerifyCharlieAlbedoAndEnergyConservation(),
            "Charlie directional albedo matches numerical quadrature and white-furnace layered reflectance <= 1.0");
        RENDER_CHECK(SheenContract::VerifyDirectVsIBLUniformHemisphereParity(),
            "Direct Charlie integral over uniform hemisphere matches IBL sheen response with base attenuation");
        RENDER_CHECK(SheenContract::VerifyGrazingRimAndRoughnessNarrowing(),
            "Charlie sheen produces stronger grazing rim response and narrows peak as roughness drops");
        RENDER_CHECK(SheenContract::VerifyDisabledAndEdgeBehavior(),
            "Disabled sheen, spec/env toggles, and grazing/below-horizon edges have zero leak and remain finite");

        // Verify CPU surface/RHI packing clamps SheenColor [0,1] and SheenRoughness [0.01,1]
        RHI::RHIMaterialDesc desc;
        desc.SheenEnabled = true;
        desc.SheenColor = glm::vec3(1.8f, -0.2f, 0.6f);
        desc.SheenRoughness = 0.001f;
        RHI::RHIMaterial mat(desc);
        const auto c = mat.BuildConstants();
        const auto resolved = MaterialResolver::Resolve(mat);
        const auto rc = resolved.ToMaterialConstants();
        RENDER_CHECK(std::abs(c.Sheen.x - 1.0f) < 1e-5f && c.Sheen.y == 0.0f &&
                     std::abs(c.Sheen.z - 0.6f) < 1e-5f && std::abs(c.Sheen.w - 0.01f) < 1e-5f,
            "RHIMaterial::BuildConstants clamps SheenColor to [0,1] and SheenRoughness to [0.01,1]");
        RENDER_CHECK(std::abs(rc.Sheen.x - 1.0f) < 1e-5f && rc.Sheen.y == 0.0f &&
                     std::abs(rc.Sheen.z - 0.6f) < 1e-5f && std::abs(rc.Sheen.w - 0.01f) < 1e-5f,
            "ResolvedSurface::ToMaterialConstants matches RHIMaterial Sheen clamping parity");

        const std::string pbrCommon = LoadTextFile(AXLE_PBR_COMMON_SHADER_PATH);
        const std::string forwardPBR = LoadTextFile(AXLE_FORWARD_PBR_SHADER_PATH);
        RENDER_CHECK(!pbrCommon.empty() && !forwardPBR.empty() &&
                     pbrCommon.find("EvaluateCharlieDirectionalAlbedo") != std::string::npos &&
                     pbrCommon.find("ApplyCharlieSheenDirect") != std::string::npos &&
                     pbrCommon.find("ApplyCharlieSheenArea") != std::string::npos &&
                     pbrCommon.find("ApplyCharlieSheenIBL") != std::string::npos &&
                     forwardPBR.find("ApplyCharlieSheenDirect") != std::string::npos &&
                     forwardPBR.find("ApplyCharlieSheenArea") != std::string::npos &&
                     forwardPBR.find("ApplyCharlieSheenIBL") != std::string::npos,
            "PBRCommon.hlsli and ForwardPBR.hlsl share energy-conserving Charlie Sheen direct/area/IBL helpers");
    }

    std::cout << "[M15U9Contract] M15 U9 Batch A contract tests passed.\n";
    return true;
}
