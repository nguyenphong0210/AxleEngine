#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <Axle/RHI/RHIDevice.hpp>

namespace Axle {
    class Scene;

    // CPU-testable acceptance helpers
    bool ParseMaterialBackendOption(const char* envVal, RHI::RHIType& outBackend, std::string& outError);
    bool ValidateMaterialCaptureDirectory(const std::string& dirPath, std::filesystem::path& outDir, std::string& outError);
    std::string FormatMaterialTimingReport(const std::string& deviceName, const std::string& backend,
                                          uint32_t width, uint32_t height, uint8_t sampleCount,
                                          const std::vector<double>& frameTimesMs);
    bool VerifyMaterialReferenceSceneStructure(const Scene& scene, std::string& outError);
    bool WritePPMImage(const std::filesystem::path& filePath, uint32_t width, uint32_t height, const uint8_t* rgbaPixels);

    // Full GPU integration acceptance test
    bool RunMaterialAcceptanceTests(const std::filesystem::path& projectRoot);
}
