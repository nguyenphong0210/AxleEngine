#pragma once
#include <Axle/RenderWorld.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <cstdint>
#include <string>
namespace Axle {
    enum class LightingFrameStatus : uint8_t { Supported, Fallback, Failed };
    struct ShadowPublication { bool CascadedAvailable=false, LocalAtlasAvailable=false; uint64_t CascadeGeneration=0, LocalGeneration=0; };
    struct LightingFrame { LightingFrameStatus Status=LightingFrameStatus::Supported; uint32_t DirectionalCount=0, PointCount=0, SpotCount=0, RectCount=0; ShadowPublication Shadows; std::string Diagnostic; explicit operator bool() const { return Status != LightingFrameStatus::Failed; } };
    class LightingService { public: LightingFrame BuildFrame(const RenderWorld&, const SceneRenderSettings&, const ShadowPublication& = {}) const; };
}
