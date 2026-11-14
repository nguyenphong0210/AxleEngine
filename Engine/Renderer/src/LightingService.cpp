#include <Axle/LightingService.hpp>
namespace Axle {
    LightingFrame LightingService::BuildFrame(const RenderWorld& world, const SceneRenderSettings& settings, const ShadowPublication& shadows) const {
        LightingFrame frame; frame.DirectionalCount=world.DirectionalLight?1u:0u; frame.DirectionalCount+=static_cast<uint32_t>(world.AdditionalDirectionalLights.size());
        frame.PointCount=static_cast<uint32_t>(world.PointLights.size()); frame.SpotCount=static_cast<uint32_t>(world.SpotLights.size()); frame.RectCount=static_cast<uint32_t>(world.RectLights.size()); frame.Shadows=shadows;
        if (!settings.EnableShadows) frame.Shadows={}; else if (!frame.Shadows.CascadeGeneration && !frame.Shadows.LocalGeneration) { frame.Status=LightingFrameStatus::Fallback; frame.Diagnostic="shadow_publication_unavailable: direct lighting remains valid without receiver atlases"; }
        return frame;
    }
}
