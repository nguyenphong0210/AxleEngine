#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <Axle/RHI/RHIDevice.hpp>

namespace Axle {

    // CPU-testable verification helpers for VFX acceptance harness
    bool ParseVFXBackendOption(const char* envVal, RHI::RHIType& outBackend, std::string& outError);
    bool ValidateVFXCaptureDirectory(const std::string& dirPath, std::filesystem::path& outDir, std::string& outError);
    std::string FormatVFXTimingReport(const std::string& deviceName, const std::string& backend,
                                     uint32_t width, uint32_t height, uint8_t sampleCount,
                                     const std::vector<double>& frameTimesMs);

    struct VFXReadbackLayout {
        uint32_t BytesPerPixel = 4;
        uint32_t RowPitch = 0;
        uint64_t SlicePitch = 0;
    };
    VFXReadbackLayout GetVFXReadbackLayout(uint32_t width, uint32_t height, bool hdr);

    // Full real GPU integration test entry point (executed when physical GPU is available)
    bool VerifyVFXRendering(const std::filesystem::path& root);

} // namespace Axle

