#include <Axle/MaterialSurfaceShader.hpp>
#include "MaterialSurfaceSource.hpp"
#include <Axle/RHI/RHIShader.hpp>
#include <algorithm>
#include <stdexcept>

namespace Axle {
    bool IsMaterialSurfaceEngineBuffer(const RHI::RHIShaderConstantBuffer& buffer) {
        return (buffer.Name == "MultiLightBuffer" && buffer.Space == 3 && buffer.BindPoint == 0) ||
               (buffer.Name == "CascadeReceiver" && buffer.Space == 0 && buffer.BindPoint == 4) ||
               (buffer.Name == "LocalShadowReceiver" && buffer.Space == 0 && buffer.BindPoint == 5);
    }

    bool IsMaterialSurfaceEngineResource(const RHI::RHIShaderResourceBinding& resource) {
        using RHI::RHIResourceType;
        if (resource.Type == RHIResourceType::ConstantBuffer)
            return (resource.Name == "MultiLightBuffer" && resource.Space == 3 && resource.BindPoint == 0) ||
                   (resource.Name == "CascadeReceiver" && resource.Space == 0 && resource.BindPoint == 4) ||
                   (resource.Name == "LocalShadowReceiver" && resource.Space == 0 && resource.BindPoint == 5);
        if (resource.Type == RHIResourceType::Texture && resource.Space == 4)
            return (resource.Name == "u_ForwardOcclusion" && resource.BindPoint == 0) ||
                   (resource.Name == "u_ForwardOcclusionDepth" && resource.BindPoint == 1);
        if (resource.Space != 2) return false;
        if (resource.Type == RHIResourceType::Texture) {
            static constexpr std::string_view names[]{"u_IrradianceMap", "u_PrefilterMap", "u_BRDFLUT", "u_CascadeDepth", "u_LocalShadowDepth"};
            return resource.BindPoint < std::size(names) && resource.Name == names[resource.BindPoint];
        }
        return resource.Type == RHIResourceType::Sampler &&
               ((resource.BindPoint == 0 && resource.Name == "u_EnvironmentSampler") ||
                (resource.BindPoint == 1 && resource.Name == "u_ShadowSampler"));
    }

    void StripMaterialSurfaceEngineBindings(RHI::RHIShaderReflection& reflection) {
        std::erase_if(reflection.Resources, IsMaterialSurfaceEngineResource);
        std::erase_if(reflection.ConstantBuffers, IsMaterialSurfaceEngineBuffer);
    }

    bool ValidateMaterialSurfaceEngineBuffers(const RHI::RHIShaderReflection& reflection, std::string& diagnostic) {
        for (const auto& buffer : reflection.ConstantBuffers) {
            if (!IsMaterialSurfaceEngineBuffer(buffer)) continue;
            // Logical shader bytes; native CBV allocations may include padding.
            const uint32_t expected = buffer.Name == "MultiLightBuffer" ? 2304u : buffer.Name == "CascadeReceiver" ? 304u : 1680u;
            if (buffer.Size != expected) {
                diagnostic = "Reserved material surface buffer has incompatible layout: " + buffer.Name;
                return false;
            }
        }
        return true;
    }

    std::string AssembleMaterialSurfaceShader(std::string_view declarations, std::string_view surfaceFunction) {
        std::string result(MaterialSurfaceSource::Common);
        result += "\n#line 1 \"mg_surface_template\"\n";
        std::string body(MaterialSurfaceSource::Template);
        auto replace = [&](std::string_view marker, std::string_view value) {
            const auto position = body.find(marker);
            if (position == std::string::npos) throw std::logic_error("Missing material surface template marker");
            body.replace(position, marker.size(), value);
        };
        replace("// @AXLE_GRAPH_FUNCTION@", surfaceFunction);
        replace("// @AXLE_GRAPH_DECLARATIONS@", declarations);
        return result + body;
    }
}
