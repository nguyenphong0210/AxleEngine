#pragma once
#include <Axle/Types.hpp>

namespace Axle {

    class PhysicsEngine {
    public:
        static void Init();
        static void Shutdown();

        static bool IsInitialized() { return s_Initialized; }

    private:
        inline static bool s_Initialized = false;
    };

}
