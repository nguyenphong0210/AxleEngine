#include <Axle/Path.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/UUID.hpp>
#include <algorithm>
#include <string_view>
#include <Axle/Package.hpp>
#include <Axle/Log.hpp>
#include <fstream>
#include <sstream>
#include <atomic>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Axle {

    static std::filesystem::path s_AssetDirectory;
    static std::unique_ptr<Package> s_MountedPackage = nullptr;
    static uint64_t s_SourceGeneration = 1;
    uint64_t FileSystem::GetSourceGeneration() { return s_SourceGeneration; }

    static std::string PackageKey(const std::filesystem::path& path) {
        auto relative = path;
        if (path.is_absolute()) {
            std::error_code error;
            auto root = std::filesystem::weakly_canonical(FileSystem::GetAssetDirectory(), error);
            if (error) return {};
            relative = path.lexically_normal().lexically_relative(root);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") return {};
        }
        return Package::NormalizePath(PathToUtf8(relative));
    }

    bool FileSystem::MountPackage(const std::filesystem::path& packagePath) {
        auto pkg = std::make_unique<Package>();
        if (pkg->Open(packagePath)) {
            s_MountedPackage = std::move(pkg);
            ++s_SourceGeneration;
            return true;
        }
        return false;
    }

    void FileSystem::UnmountPackage() {
        if (s_MountedPackage) {
            s_MountedPackage->Close();
            s_MountedPackage.reset();
            ++s_SourceGeneration;
        }
    }

    bool FileSystem::HasMountedPackage() {
        return s_MountedPackage != nullptr && s_MountedPackage->IsOpen();
    }

    const Package* FileSystem::GetMountedPackage() {
        return s_MountedPackage.get();
    }

    void FileSystem::SetAssetDirectory(const std::filesystem::path& path) {
        if (s_AssetDirectory != path) ++s_SourceGeneration;
        s_AssetDirectory = path;
    }

    const std::filesystem::path& FileSystem::GetAssetDirectory() {
        // An explicit project root remains authoritative even before its directory exists.
        // Never discover another project's sample assets by walking the engine checkout.
        if (s_AssetDirectory.empty()) s_AssetDirectory = std::filesystem::absolute("assets");
        return s_AssetDirectory;
    }

    std::filesystem::path FileSystem::ResolveAssetPath(const std::filesystem::path& relativePath) {
        // 0. If package is mounted and contains this virtual path, return it directly
        if (s_MountedPackage && s_MountedPackage->IsOpen()) {
            std::string norm = PackageKey(relativePath);
            if (s_MountedPackage->HasEntry(norm)) {
                return relativePath;
            }
        }

        // If already an absolute path and exists, return directly
        if (relativePath.is_absolute() && std::filesystem::exists(relativePath)) {
            return relativePath;
        }

        // 1. Try against active asset directory
        auto resolved = GetAssetDirectory() / relativePath;
        if (std::filesystem::exists(resolved)) {
            return resolved;
        }

        std::string relStr = PathToUtf8(relativePath);
        if (relStr.rfind("assets/", 0) == 0) {
            auto subCandidate = GetAssetDirectory() / PathFromUtf8(relStr.substr(7));
            if (std::filesystem::exists(subCandidate)) {
                return subCandidate;
            }
        }

        // 2. Try against Engine Resources (Engine/Resources or Resources in binary dir)
        static const std::vector<std::filesystem::path> s_ResourceRoots = {
            "Engine/Resources",
            "../../Engine/Resources",
            "../../../Engine/Resources",
            "../Engine/Resources",
            "Resources",
            "../../Resources"
        };

        for (const auto& root : s_ResourceRoots) {
            auto candidate = root / relativePath;
            if (std::filesystem::exists(candidate)) {
                return std::filesystem::canonical(candidate);
            }

            // Also check Shaders/ subfolder if relativePath begins with shaders/
            std::string relStr = PathToUtf8(relativePath);
            if (relStr.rfind("shaders/", 0) == 0) {
                auto candidateShaders = root / "Shaders" / relStr.substr(8);
                if (std::filesystem::exists(candidateShaders)) {
                    return std::filesystem::canonical(candidateShaders);
                }
            }
        }

        // Fallback checks
        if (std::filesystem::exists(relativePath)) {
            return relativePath;
        }

        return resolved;
    }

    std::string FileSystem::ReadTextFile(const std::filesystem::path& filepath) {
        // 1. Check mounted VFS package first
        if (s_MountedPackage && s_MountedPackage->IsOpen()) {
            std::string norm = PackageKey(filepath);
            if (s_MountedPackage->HasEntry(norm)) {
                return s_MountedPackage->ReadEntryAsText(norm);
            }
        }

        // 2. Physical disk read
        std::ifstream file(filepath);
        if (!file.is_open()) {
            auto resolved = ResolveAssetPath(filepath);
            if (resolved != filepath && std::filesystem::exists(resolved)) {
                std::ifstream resFile(resolved);
                if (resFile.is_open()) {
                    std::stringstream buffer;
                    buffer << resFile.rdbuf();
                    return buffer.str();
                }
            }
            AXLE_CORE_WARN("FileSystem: Unable to open file for reading: {0}", PathToUtf8(filepath));
            return "";
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::vector<uint8_t> FileSystem::ReadBinaryFile(const std::filesystem::path& filepath) {
        // 1. Check mounted VFS package first
        if (s_MountedPackage && s_MountedPackage->IsOpen()) {
            std::string norm = PackageKey(filepath);
            if (s_MountedPackage->HasEntry(norm)) {
                return s_MountedPackage->ReadEntry(norm);
            }
        }

        // 2. Physical disk read
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            auto resolved = ResolveAssetPath(filepath);
            if (resolved != filepath && std::filesystem::exists(resolved)) {
                std::ifstream resFile(resolved, std::ios::binary | std::ios::ate);
                if (resFile.is_open()) {
                    std::streamsize size = resFile.tellg();
                    resFile.seekg(0, std::ios::beg);
                    std::vector<uint8_t> buffer(static_cast<size_t>(size));
                    if (resFile.read(reinterpret_cast<char*>(buffer.data()), size)) {
                        return buffer;
                    }
                }
            }
            AXLE_CORE_WARN("FileSystem: Unable to open binary file for reading: {0}", PathToUtf8(filepath));
            return {};
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            AXLE_CORE_WARN("FileSystem: Failed reading binary content from: {0}", PathToUtf8(filepath));
            return {};
        }

        return buffer;
    }

    bool FileSystem::WriteTextFile(const std::filesystem::path& filepath, const std::string& content) {
        return WriteTextFileAtomically(filepath, content);
    }

    bool FileSystem::WriteTextFileAtomically(const std::filesystem::path& filepath, const std::string& text, std::string* outError) {
        return WriteBinaryFileAtomically(filepath, text.data(), text.size(), outError);
    }

    bool FileSystem::WriteBinaryFileAtomically(const std::filesystem::path& filepath, const void* data, size_t size, std::string* outError) {
        if (outError) outError->clear();
        if (size != 0 && data == nullptr) {
            if (outError) *outError = "Nonempty write requires a non-null data buffer";
            return false;
        }
        if (filepath.empty()) {
            if (outError) *outError = "Filepath cannot be empty";
            return false;
        }

        std::filesystem::path target = filepath;
        if (target.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                if (outError) *outError = "Cannot create directory: " + ec.message();
                return false;
            }
        }

        static std::atomic<uint64_t> s_Counter{0};
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path tempPath = target;
        tempPath += ".tmp." + std::to_string(now) + "." + std::to_string(s_Counter++);

        {
            std::ofstream out(tempPath, std::ios::binary);
            if (!out.is_open()) {
                if (outError) *outError = "Failed to open temporary file for write: " + PathToUtf8(tempPath);
                return false;
            }
            if (size > 0 && data != nullptr) {
                out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            }
            out.flush();
            if (!out.good()) {
                out.close();
                std::error_code ec;
                std::filesystem::remove(tempPath, ec);
                if (outError) *outError = "Failed to write complete data to temporary file";
                return false;
            }
            out.close();
            if (out.fail()) {
                std::error_code cleanup;
                std::filesystem::remove(tempPath, cleanup);
                if (outError) *outError = "Failed to close temporary file after writing";
                return false;
            }
        }

#ifdef _WIN32
        if (!MoveFileExW(tempPath.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto failure = GetLastError();
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            if (outError) *outError = "Failed to atomically replace target file: Windows error " + std::to_string(failure);
            return false;
        }
#else
        std::error_code ec;
        std::filesystem::rename(tempPath, target, ec);
        if (ec) {
            const auto failure = ec.message();
            std::filesystem::remove(tempPath, ec);
            if (outError) *outError = "Failed to atomically replace target file: " + failure;
            return false;
        }
#endif

        return true;
    }


    bool FileSystem::MakeAssetReference(const std::filesystem::path& path, std::string& reference) {
        reference.clear();
        if (path.empty()) return true;
        std::error_code error;
        auto relative = path;
        if (path.is_absolute()) {
            if (s_AssetDirectory.empty()) return false;
            relative = std::filesystem::relative(path, s_AssetDirectory, error);
        }
        relative = relative.lexically_normal();
        if (error || relative.empty() || relative.is_absolute() || *relative.begin() == "..") return false;
        reference = PathToUtf8(relative);
        return true;
    }

    bool FileSystem::ReadFileIdentity(const std::filesystem::path& path, uint64_t& id, std::string* error) {
        id = 0;
        std::string first;
        if (HasMountedPackage() && s_MountedPackage->HasEntry(PackageKey(path))) {
            std::istringstream input(ReadTextFile(path));
            std::getline(input, first);
        } else {
            std::ifstream input(path, std::ios::binary);
            std::getline(input, first);
        }
        if (first.empty()) {
            if (error) *error = "Cannot read asset identity: " + PathToUtf8(path);
            return false;
        }
        if (!first.empty() && first.back() == '\r') first.pop_back();
        constexpr std::string_view prefix = "# AxleFileIdentity: ";
        if (first.rfind(prefix, 0) != 0) {
            if (error) *error = "Asset has no embedded identity: " + PathToUtf8(path);
            return false;
        }
        std::istringstream values(first.substr(prefix.size()));
        unsigned version = 0;
        std::string value, extra;
        if (!(values >> version >> value) || version != 1 || (values >> extra) || value.empty() ||
            !std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
            if (error) *error = "Malformed or unsupported asset identity: " + PathToUtf8(path);
            return false;
        }
        try { id = std::stoull(value); } catch (...) { id = 0; }
        if (id == 0) {
            if (error) *error = "Invalid asset UUID: " + PathToUtf8(path);
            return false;
        }
        return true;
    }

    bool FileSystem::WriteIdentifiedTextFile(const std::filesystem::path& path, const std::string& text,
                                              std::string* error, uint64_t requestedID) {
        if (error) error->clear();
        constexpr std::string_view prefix = "# AxleFileIdentity:";
        auto decode = [&](const std::string& line, uint64_t& id) {
            if (line.rfind(prefix, 0) != 0) return false;
            std::istringstream header(line.substr(prefix.size()));
            unsigned version = 0;
            std::string value, extra;
            if (!(header >> version >> value) || version != 1 || (header >> extra) || value.empty() ||
                !std::all_of(value.begin(), value.end(), [](unsigned char c){ return c >= '0' && c <= '9'; })) return false;
            try { id = std::stoull(value); } catch (...) { return false; }
            return id != 0;
        };
        std::string body = text;
        uint64_t incoming = 0, existing = 0;
        if (body.rfind(prefix, 0) == 0) {
            auto end = body.find('\n');
            if (end == std::string::npos || !decode(body.substr(0,end), incoming)) {
                if(error)*error="Invalid or unsupported incoming document identity";
                return false;
            }
            body.erase(0,end+1);
        }
        if (body.empty()) { if(error)*error="Identified document body is empty"; return false; }
        if (std::filesystem::is_regular_file(path)) {
            std::ifstream old(path,std::ios::binary);
            std::string first;
            if (!old || !std::getline(old,first)) { if(error)*error="Cannot read existing document before saving"; return false; }
            if (first.rfind(prefix,0) == 0 && !decode(first,existing)) {
                if(error)*error="Existing document identity is malformed; recovery is required"; return false;
            }
        }
        if (existing && ((requestedID && requestedID != existing) ||
                         (!requestedID && incoming && incoming != existing))) {
            if(error)*error="Save cannot replace an existing document's identity";
            return false;
        }
        uint64_t id = requestedID ? requestedID : existing ? existing : incoming;
        if (!id) id = static_cast<uint64_t>(UUID());
        if (!id) { if(error)*error="Cannot allocate document UUID"; return false; }
        return WriteTextFileAtomically(path, "# AxleFileIdentity: 1 " + std::to_string(id) + "\n" + body, error);
    }


    bool FileSystem::Exists(const std::filesystem::path& filepath) {
        if (s_MountedPackage && s_MountedPackage->IsOpen()) {
            std::string norm = PackageKey(filepath);
            if (s_MountedPackage->HasEntry(norm)) {
                return true;
            }
        }
        return std::filesystem::exists(filepath);
    }

}
