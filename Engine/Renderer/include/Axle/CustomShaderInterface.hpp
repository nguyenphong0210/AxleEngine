#pragma once
#include <Axle/Types.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <string>
#include <vector>

namespace Axle {

    enum class CustomInterfaceType : uint8_t {
        Unknown = 0,
        Canonical,
        LegacyTemplate
    };

    struct InterfaceValidationResult {
        bool IsValid = false;
        CustomInterfaceType Type = CustomInterfaceType::Unknown;
        std::string Diagnostic;
    };

    class CustomShaderInterface {
    public:
        // Returns the canonical custom material HLSL source code template (b0/b1/b2 in space0, t0/s0 in space1)
        static std::string GetCanonicalTemplate(const std::string& shaderName = "CustomMaterial");

        // Returns the legacy HLSL Studio template for backward compatibility
        static std::string GetLegacyTemplate(const std::string& shaderName = "CustomMaterial");
        // Compile-view compatibility adapter. Input must have includes expanded.
        static std::string NormalizeLegacyBindings(const std::string& source);

        // Detect interface type from reflected constant buffers and resources
        static CustomInterfaceType DetectInterface(const RHI::RHIShaderReflection& reflection);

        // Validate interface compliance and detect unsupported layouts
        static InterfaceValidationResult Validate(const RHI::RHIShaderReflection& reflection);

        // Check if there are binding collisions (e.g. cbuffer b0 and texture t0 in same space0)
        static bool HasVulkanBindingCollision(const RHI::RHIShaderReflection& reflection, std::string& outDetails);
    };

} // namespace Axle
