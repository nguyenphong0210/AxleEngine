#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Axle {

    struct MaterialComponent;

    // Shared by material asset and entity inspectors. File operations are also
    // exposed independently of ImGui for asset tooling and headless validation.
    class CustomShaderPicker {
    public:
        bool Draw(const char* id, MaterialComponent& material, const std::string& suggestedName,
                  const std::function<void(const std::filesystem::path&)>& openShader);

        static std::vector<std::filesystem::path> FindShaders(const std::filesystem::path& assetRoot);
        static bool GetNewShaderPath(const std::filesystem::path& assetRoot, const std::string& name,
                                     std::filesystem::path& result, std::string& error);
        static bool CreateShader(const std::filesystem::path& assetRoot, const std::string& name,
                                 std::filesystem::path& result, std::string& error);
        static bool AssignShader(MaterialComponent& material, const std::filesystem::path& assetRoot,
                                 const std::filesystem::path& shaderPath, std::string& error);
        static bool DecodeDragDropPayload(const void* data, size_t size,
                                          std::filesystem::path& outPath, std::string& error);

    private:
        char m_Search[128] = {};
        char m_Name[128] = {};
        std::vector<std::filesystem::path> m_Shaders;
        std::string m_Error;
        std::string m_CreateError;
    };
}
