#pragma once
#include <Axle/Types.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <Axle/ShaderPropertyReflection.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <variant>
#include <span>
#include <optional>

namespace Axle {

    enum class UniformKind : uint8_t {
        Float32 = 0,
        Float2,
        Float3,
        Float4,
        Int32,
        Bool32,
        Matrix4x4
    };

    uint32_t GetUniformKindSize(UniformKind kind);

    enum class TextureRole : uint8_t {
        Color = 0,
        LinearData,
        TangentNormal
    };

    enum class EngineUniform : uint8_t {
        Model = 0,
        ViewProjection,
        View,
        Projection,
        CameraPosition,
        SunDirection,
        SunColor,
        BaseColor,
        LegacySurfaceParams,
        BaseUVTransform
    };

    struct ShaderAddress {
        uint32_t RegisterSpace = 0;
        uint32_t RegisterIndex = 0;
        bool operator==(const ShaderAddress& other) const = default;
    };

    struct PropertyKey {
        std::string BufferName;
        std::string VariableName;
        bool operator==(const PropertyKey& other) const = default;
    };

    struct UniformBinding {
        size_t BufferIndex = 0;
        uint32_t Offset = 0;
        uint32_t ReflectedSize = 0;
        UniformKind Kind = UniformKind::Float32;
        std::variant<EngineUniform, PropertyKey> Source;
        bool operator==(const UniformBinding& other) const = default;
    };

    struct ConstantBufferPlan {
        std::string Name;
        ShaderAddress Address;
        uint32_t LogicalSize = 0;
        bool operator==(const ConstantBufferPlan& other) const = default;
    };

    struct TextureBinding {
        std::string ResourceName;
        ShaderAddress Address;
        TextureRole Role = TextureRole::LinearData;
        bool operator==(const TextureBinding& other) const = default;
    };

    struct VertexAttributeBinding {
        std::string SemanticName;
        uint32_t SemanticIndex = 0;
        uint32_t InputSlot = 0;
        uint32_t Offset = 0;
        nri::Format Format = nri::Format::UNKNOWN;
        uint32_t BackendLocation = 0;
        bool operator==(const VertexAttributeBinding& other) const = default;
    };

    struct VertexInputPlan {
        uint32_t Stride = 64;
        std::vector<VertexAttributeBinding> Attributes;
        bool operator==(const VertexInputPlan& other) const = default;
    };

    struct MaterialBindingPlan {
        std::vector<ConstantBufferPlan> Buffers;
        std::vector<UniformBinding> Uniforms;
        std::vector<TextureBinding> Textures;
        VertexInputPlan VertexInput;
        uint32_t InterfaceRevision = 1;
        bool IsLegacyTemplate = false;
        bool operator==(const MaterialBindingPlan& other) const = default;
    };

    struct MaterialBindingError {
        std::string Code;
        std::string BufferOrResource;
        std::string Variable;
        std::string Message;
    };

    using BindingPlanResult = std::variant<MaterialBindingPlan, MaterialBindingError>;
    using VertexInputPlanResult = std::variant<VertexInputPlan, MaterialBindingError>;

    struct EngineMaterialValues {
        glm::mat4 Model{ 1.0f };
        glm::mat4 ViewProjection{ 1.0f };
        glm::mat4 View{ 1.0f };
        glm::mat4 Projection{ 1.0f };
        glm::vec4 CameraPosition{ 0.0f };
        glm::vec4 SunDirection{ 0.0f, -1.0f, 0.0f, 1.0f };
        glm::vec4 SunColor{ 1.0f };
        glm::vec4 BaseColor{ 1.0f };
        glm::vec4 BaseUVTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
        glm::vec4 LegacySurfaceParams{ 0.5f, 0.0f, 1.0f, 1.0f };
    };

    struct ResolvedMaterialProperty {
        PropertyKey Key;
        UniformKind Kind = UniformKind::Float32;
        std::variant<float, glm::vec2, glm::vec3, glm::vec4, int32_t, uint32_t, std::string> Value;
    };

    struct ResolvedMaterialProperties {
        std::vector<ResolvedMaterialProperty> Properties;
    };

    using ResolvedPropertyResult = std::variant<ResolvedMaterialProperties, MaterialBindingError>;

    struct PackedBufferData {
        std::string Name;
        ShaderAddress Address;
        std::vector<uint8_t> Bytes;
    };

    struct PackedMaterialResultData {
        std::vector<PackedBufferData> Buffers;
    };

    using PackedMaterialResult = std::variant<PackedMaterialResultData, MaterialBindingError>;

    struct TextureRequest {
        std::string ResourceName;
        ShaderAddress Address;
        TextureRole Role = TextureRole::LinearData;
        std::string ResolvedPath;
    };

    struct TextureRequestResultData {
        std::vector<TextureRequest> Requests;
    };

    using TextureRequestResult = std::variant<TextureRequestResultData, MaterialBindingError>;

    struct ValidatedShaderInterface {
        uint32_t InterfaceRevision = 1;
        bool AllowLegacyTemplate = true;
    };

    struct EffectiveMaterialValues {
        glm::vec4 BaseColor{ 1.0f };
        glm::vec2 UVScale{ 1.0f, 1.0f };
        glm::vec2 UVOffset{ 0.0f, 0.0f };
        std::string BaseMapPath;
        bool UseBaseMap = false;
        std::vector<ShaderPropertyInstance> Properties;
    };

    // Pure functions
    BindingPlanResult BuildMaterialBindingPlan(
        const RHI::RHIShaderReflection& mergedReflection,
        const ValidatedShaderInterface& shaderInterface,
        std::span<const ShaderPropertyDesc> declaredUserProperties);

    ResolvedPropertyResult ResolveMaterialProperties(
        const MaterialBindingPlan& plan,
        std::span<const ShaderPropertyInstance> savedValues,
        std::span<const ShaderPropertyDesc> declaredUserProperties = {});

    PackedMaterialResult PackMaterialUniforms(
        const MaterialBindingPlan& plan,
        const EngineMaterialValues& engineValues,
        const ResolvedMaterialProperties& properties);

    VertexInputPlanResult BuildMaterialVertexInputPlan(
        std::span<const RHI::RHIVertexAttribute> reflectedInputs);

    TextureRequestResult ResolveMaterialTextureRequests(
        const MaterialBindingPlan& plan,
        const EffectiveMaterialValues& materialValues);

} // namespace Axle
