#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace Axle {
struct PluginRestartSnapshot {
    std::filesystem::path Generation;
    std::string ReceiptSet;
    std::map<std::string,std::string> BinaryHashes;
};
bool CapturePluginRestartSnapshot(const std::filesystem::path& generation,
    std::vector<std::string> modules, PluginRestartSnapshot& snapshot, std::string& error);
}
