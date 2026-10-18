#include <AxleSpline/SplinePlugin.hpp>
#include <Axle/Log.hpp>

namespace Axle::Spline {

    bool SplinePlugin::s_Initialized = false;

    void SplinePlugin::Initialize() {
        if (s_Initialized) return;
        s_Initialized = true;
        AXLE_INFO("[Plugin: {0} v{1}] Initialized successfully. ({2})", Name, Version, Description);
    }

    void SplinePlugin::Shutdown() {
        if (!s_Initialized) return;
        s_Initialized = false;
        AXLE_INFO("[Plugin: {0}] Shutdown complete.", Name);
    }

    bool SplinePlugin::IsInitialized() {
        return s_Initialized;
    }

}
