#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace Axle {
inline std::string PathToUtf8(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string(); return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
inline std::filesystem::path PathFromUtf8(std::string_view text) { return std::filesystem::u8path(text.begin(), text.end()); }
}
