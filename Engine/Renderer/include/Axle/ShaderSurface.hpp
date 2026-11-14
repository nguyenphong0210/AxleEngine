#pragma once
#include <Axle/ResolvedSurface.hpp>
#include <Axle/MaterialBindingPlan.hpp>

namespace Axle {
    struct ResolvedShaderSurface {
        ResolvedSurface Surface;
        PackedMaterialResultData Constants;
        std::vector<TextureRequest> Textures;
    };
    using ShaderSurfaceResult = std::variant<ResolvedShaderSurface, MaterialBindingError>;

    // Transactional, CPU-only binding resolution. Material-owned engine uniforms
    // always come from base; view-owned matrices/light data come from view.
    // Shader generation identifies immutable code/layout, excluding camera state.
    ShaderSurfaceResult ResolveShaderSurface(const ResolvedSurface& base,
        SurfaceEvaluation evaluation, uint64_t shaderGeneration,
        const MaterialBindingPlan& plan, const EngineMaterialValues& view,
        std::span<const ShaderPropertyInstance> savedValues);
}
