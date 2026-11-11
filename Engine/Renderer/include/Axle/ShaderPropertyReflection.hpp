#pragma once
#include <Axle/Types.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Axle {

    enum class ShaderTextureColorSpace : uint8_t { Automatic, Linear, SRGB };
    enum class ShaderSamplerPolicy : uint8_t { Default, LinearRepeat, LinearClamp, PointRepeat, PointClamp };
    bool ParseShaderSamplerPolicy(std::string_view name, ShaderSamplerPolicy& policy);

    enum class ShaderPropertyType : uint8_t {
        Float = 0,
        Float2,
        Float3,
        Float4,
        Color,
        Int,
        Bool,
        Texture2D
    };

    const char* ShaderPropertyTypeToString(ShaderPropertyType type);
    ShaderPropertyType StringToShaderPropertyType(const std::string& str);

    struct ShaderPropertyDesc {
        std::string Name;              // e.g. "u_Dissolve" or "Dissolve"
        std::string DisplayName;       // Formatted human-readable name: "Dissolve"
        ShaderPropertyType Type = ShaderPropertyType::Float;
        float MinValue = 0.0f;
        float MaxValue = 1.0f;
        bool HasRange = false;
        std::string Header;            // Category/Header grouping
        std::string Tooltip;

        // Default values
        float DefaultFloat = 0.0f;
        glm::vec4 DefaultVec4{ 0.0f, 0.0f, 0.0f, 1.0f };
        int DefaultInt = 0;
        bool DefaultBool = false;
        std::string DefaultTexturePath;
        ShaderTextureColorSpace TextureColorSpace = ShaderTextureColorSpace::Automatic;
        std::string SamplerName;
        ShaderSamplerPolicy SamplerPolicy = ShaderSamplerPolicy::Default;

        // Reflection metadata from DXC
        uint32_t Offset = 0;
        uint32_t Size = 0;
        std::string ConstantBufferName;
        bool operator==(const ShaderPropertyDesc&) const = default;
    };

    struct ShaderPropertyInstance {
        ShaderPropertyDesc Desc;
        float FloatVal = 0.0f;
        glm::vec4 Vec4Val{ 0.0f, 0.0f, 0.0f, 1.0f };
        int IntVal = 0;
        bool BoolVal = false;
        std::string TexturePath;

        bool operator==(const ShaderPropertyInstance& other) const {
            return Desc.Name == other.Desc.Name;
        }
    };

    class ShaderPropertyParser {
    public:
        // Parse HLSL source annotations and merge with DXC reflection metadata
        static std::vector<ShaderPropertyDesc> ParseFromHLSL(
            const std::string& hlslSource,
            const RHI::RHIShaderReflection* reflection = nullptr
        );

        // Convert identifier to friendly display name (e.g. "u_DissolveAmount" -> "Dissolve Amount")
        static std::string FormatDisplayName(const std::string& identifier);
    };

    // Synchronize material custom properties against a newly parsed or reloaded set of property descriptors.
    // Preserves compatible values when names and types match, initializes new properties with defaults,
    // and filters out engine uniforms and function-local variables.
    std::vector<ShaderPropertyInstance> SynchronizeCustomProperties(
        const std::vector<ShaderPropertyDesc>& newProperties,
        const std::vector<ShaderPropertyInstance>& existingInstances
    );

} // namespace Axle

