#pragma once
#include <string>
#include <string_view>

namespace Axle::RHI {
    struct RHIShaderResourceBinding;
    struct RHIShaderConstantBuffer;
    struct RHIShaderReflection;
}

namespace Axle {
    // Embeds the same PBR BRDF source used by ForwardPBR. No disk includes are
    // required at runtime; graph declarations/evaluation contain only surface logic.
    std::string AssembleMaterialSurfaceShader(std::string_view declarations, std::string_view surfaceFunction);
    // Reserved lighting/visibility inputs are supplied by the renderer, never
    // exposed as editable material properties. Keep physical reflection intact.
    bool IsMaterialSurfaceEngineResource(const RHI::RHIShaderResourceBinding& resource);
    bool IsMaterialSurfaceEngineBuffer(const RHI::RHIShaderConstantBuffer& buffer);
    bool ValidateMaterialSurfaceEngineBuffers(const RHI::RHIShaderReflection& reflection, std::string& diagnostic);
    void StripMaterialSurfaceEngineBindings(RHI::RHIShaderReflection& editableReflection);
}
