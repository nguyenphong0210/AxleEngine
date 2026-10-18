#pragma once
#include <string_view>

namespace Axle::Spline {

    class SplinePlugin {
    public:
        static constexpr std::string_view Name = "AxleSpline";
        static constexpr std::string_view Version = "1.0.0";
        static constexpr std::string_view Description = "Procedural Spline Curve & Race Track Generator Plugin";

        static void Initialize();
        static void Shutdown();
        static bool IsInitialized();

    private:
        static bool s_Initialized;
    };

}
