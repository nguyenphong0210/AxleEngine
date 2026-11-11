#pragma once
#include <Axle/RenderWorld.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <array>
#include <string>
#include <vector>
namespace Axle {
    constexpr uint32_t MaxLocalShadowViews = 16;
    struct ShadowDepthView {
        glm::mat4 Matrix{1};
        uint32_t X=0,Y=0,Size=0;
    };
    struct LocalShadowFrame {
        std::vector<ShadowDepthView> Views;
        std::array<int,16> PointViews,SpotViews;
        std::array<glm::vec4,16> Bias{};
        uint32_t AtlasSize=0;
        std::string Diagnostic;
        LocalShadowFrame(){PointViews.fill(-1);SpotViews.fill(-1);}
    };
    // RH + zero-to-one projections, shared by native and GL shadow receivers.
    bool BuildLocalShadowFrame(const RenderWorld&,const glm::vec3& camera,
        const SceneRenderSettings&,LocalShadowFrame&,bool includePoints=true);
    bool ShadowIntersects(const glm::mat4& light,const glm::mat4& model,
        const glm::vec3& minimum,const glm::vec3& maximum);
}
