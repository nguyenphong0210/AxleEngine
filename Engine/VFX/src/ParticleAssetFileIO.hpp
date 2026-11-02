#pragma once
#include <Axle/ParticleEffectSerializer.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Axle::Detail {
    inline bool WriteParticleAssetAtomically(const std::string &filePath, const std::string &text,
                                             std::vector<VFXDiagnostic> *diagnostics = nullptr) {
        std::filesystem::path temporary;
        auto fail = [&](const std::string &message) {
            std::error_code ignored;
            if (!temporary.empty())
                std::filesystem::remove(temporary, ignored);
            if (diagnostics)
                diagnostics->push_back({VFXDiagnosticSeverity::Error, "IO_WRITE_FAILED", "", message});
            return false;
        };
        try {
            if (filePath.empty())
                return fail("An asset destination path is required.");
            const std::filesystem::path target(filePath);
            if (target.has_parent_path())
                std::filesystem::create_directories(target.parent_path());
            static std::atomic<uint64_t> sequence{0};
            temporary = target.string() + ".tmp." +
                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
                        std::to_string(sequence++);
            std::ofstream output(temporary, std::ios::binary);
            if (!output)
                return fail("Cannot open sibling asset temporary file.");
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.flush();
            bool written = output.good();
            output.close();
            if (!written || output.fail())
                return fail("Cannot complete the asset temporary file.");
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), target.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return fail("Cannot replace asset file (Windows error " + std::to_string(GetLastError()) +
                            ").");
#else
            std::error_code error;
            std::filesystem::rename(temporary, target, error);
            if (error)
                return fail("Cannot replace asset file: " + error.message());
#endif
            return true;
        } catch (const std::exception &error) {
            return fail(error.what());
        }
    }
} // namespace Axle::Detail
