#include "PluginRestartSnapshot.hpp"
#include <Axle/PluginArtifact.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace Axle {
bool CapturePluginRestartSnapshot(const std::filesystem::path& generation, std::vector<std::string> modules,
    PluginRestartSnapshot& snapshot, std::string& error) {
    snapshot = {}; error.clear();
    if (modules.empty()) { snapshot.ReceiptSet = "empty"; return true; }
    if (generation.empty()) { error = "Native restart requires a staged generation"; return false; }
    try {
        snapshot.Generation = std::filesystem::canonical(generation);
        std::sort(modules.begin(),modules.end());
        std::uint64_t hash = 1469598103934665603ull;
        for (const auto& module : modules) {
            const auto path = snapshot.Generation / (module + ".build.receipt");
#ifdef _WIN32
            // Parse and hash one locked file generation. A concurrent replace
            // cannot combine a parsed identity with another receipt's bytes.
            struct ReceiptLock { HANDLE File; ~ReceiptLock() { if (File != INVALID_HANDLE_VALUE) CloseHandle(File); } } lock{
                CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
            if (lock.File == INVALID_HANDLE_VALUE) { error = "Could not lock a restart receipt for capture"; return false; }
#endif
            Plugin::ArtifactIdentity identity;
            if (!Plugin::ReadArtifactReceipt(path,identity,error) || identity.ModuleName != module || identity.BinaryHash.empty()) {
                if (error.empty()) error = "Restart receipt does not identify the selected native module"; return false;
            }
            const auto receiptHash = Plugin::HashArtifactFile(path,error); if (receiptHash.empty()) return false;
            const auto field = std::to_string(module.size()) + ":" + module + ":" + identity.BinaryHash + ":" + receiptHash + ";";
            for (unsigned char byte : field) { hash ^= byte; hash *= 1099511628211ull; }
            if (!snapshot.BinaryHashes.emplace(module,identity.BinaryHash).second) { error = "Duplicate restart module"; return false; }
        }
        std::ostringstream output; output << std::hex << std::setfill('0') << std::setw(16) << hash;
        snapshot.ReceiptSet = output.str(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
}
