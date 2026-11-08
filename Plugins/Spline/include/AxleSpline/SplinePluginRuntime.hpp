#pragma once
#include <Axle/PluginABI.hpp>
#include <Axle/PluginSDK.hpp>
#include <AxleSpline/SplineComponent.hpp>
#include <cstdint>

namespace AxleSpline {

inline constexpr const char* kSplinePluginId = "axle.spline";
inline constexpr const char* kSplineTypeId = "axle.spline/Spline";
inline constexpr std::uint32_t kSplineSchemaVersion = 1;

} // namespace AxleSpline
