#pragma once

#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <filesystem>

namespace Axle {

    enum class AssetKind : uint32_t {
        Unknown = 0,
        Material,
        Canvas,
        ParticleEmitter,
        Mesh,
        Texture,
        Terrain,
        PhysicsMaterial
    };

    struct AssetProperty {
        std::string Key;
        std::string RawValue;

        std::string AsString() const;
        float AsFloat(float defaultVal = 0.0f) const;
        int32_t AsInt(int32_t defaultVal = 0) const;
        uint32_t AsUInt(uint32_t defaultVal = 0) const;
        bool AsBool(bool defaultVal = false) const;
        glm::vec2 AsVec2(const glm::vec2& defaultVal = glm::vec2(0.0f)) const;
        glm::vec3 AsVec3(const glm::vec3& defaultVal = glm::vec3(0.0f)) const;
        glm::vec4 AsVec4(const glm::vec4& defaultVal = glm::vec4(0.0f)) const;
        std::vector<float> AsFloatArray() const;
        std::vector<std::string> AsStringArray() const;
        std::vector<glm::vec3> AsVec3Array() const;
        std::vector<glm::ivec3> AsIVec3Array() const;
    };

    struct AssetBlock {
        std::string Type;
        std::vector<AssetProperty> Properties;
        std::vector<std::shared_ptr<AssetBlock>> Children;

        bool HasProperty(const std::string& key) const;
        const AssetProperty* GetProperty(const std::string& key) const;

        std::string GetString(const std::string& key, const std::string& defaultVal = "") const;
        float GetFloat(const std::string& key, float defaultVal = 0.0f) const;
        int32_t GetInt(const std::string& key, int32_t defaultVal = 0) const;
        uint32_t GetUInt(const std::string& key, uint32_t defaultVal = 0) const;
        bool GetBool(const std::string& key, bool defaultVal = false) const;
        glm::vec2 GetVec2(const std::string& key, const glm::vec2& defaultVal = glm::vec2(0.0f)) const;
        glm::vec3 GetVec3(const std::string& key, const glm::vec3& defaultVal = glm::vec3(0.0f)) const;
        glm::vec4 GetVec4(const std::string& key, const glm::vec4& defaultVal = glm::vec4(0.0f)) const;

        void SetString(const std::string& key, const std::string& val);
        void SetFloat(const std::string& key, float val);
        void SetInt(const std::string& key, int32_t val);
        void SetUInt(const std::string& key, uint32_t val);
        void SetBool(const std::string& key, bool val);
        void SetVec2(const std::string& key, const glm::vec2& val);
        void SetVec3(const std::string& key, const glm::vec3& val);
        void SetVec4(const std::string& key, const glm::vec4& val);
        void SetHexColor(const std::string& key, const glm::vec4& color, bool includeAlpha = true);
        void SetFloatArray(const std::string& key, const std::vector<float>& values);
        void SetStringArray(const std::string& key, const std::vector<std::string>& values);
    };

    struct AssetDocument {
        AssetKind Kind = AssetKind::Unknown;
        std::shared_ptr<AssetBlock> Root = nullptr;

        bool IsValid() const { return Root != nullptr && Kind != AssetKind::Unknown; }
    };

    class AssetDSL {
    public:
        static AssetKind StringToAssetKind(std::string_view str);
        static std::string_view AssetKindToString(AssetKind kind);

        // Fast peek utility to identify asset type for Content Browser icons with zero full-parse cost
        static AssetKind PeekAssetKind(std::string_view source);
        static AssetKind PeekAssetKindFromFile(const std::string& filepath);
        static AssetKind PeekAssetKindFromFile(const std::filesystem::path& filepath) { return PeekAssetKindFromFile(filepath.string()); }

        // Parsing
        static bool ParseSource(std::string_view source, AssetDocument& outDoc, std::string* outError = nullptr);
        static bool ParseFile(const std::string& filepath, AssetDocument& outDoc, std::string* outError = nullptr);
        static bool ParseFile(const std::filesystem::path& filepath, AssetDocument& outDoc, std::string* outError = nullptr) { return ParseFile(filepath.string(), outDoc, outError); }

        // Serializing
        static std::string SerializeDocument(const AssetDocument& doc);
        static std::string SerializeBlock(const AssetBlock& block, int indentLevel = 0);
        static bool SaveFile(const std::string& filepath, const AssetDocument& doc, std::string* outError = nullptr);
        static bool SaveFile(const std::filesystem::path& filepath, const AssetDocument& doc, std::string* outError = nullptr) { return SaveFile(filepath.string(), doc, outError); }

        // Utilities
        static std::string StripComments(std::string_view source);
        static glm::vec4 ParseHexColor(std::string_view hex);
        static std::string FormatHexColor(const glm::vec4& color, bool includeAlpha = true);

    private:
        static bool ParseBlockInternal(std::string_view& src, std::shared_ptr<AssetBlock> block, std::string* outError);
    };

} // namespace Axle

