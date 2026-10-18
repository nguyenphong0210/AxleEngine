#pragma once

#include <Axle/Types.hpp>
#include <string>

namespace Axle::Terrain {

    class TerrainPlugin {
    public:
        static void Init();
        static void Initialize() { Init(); }
        static void Shutdown();

        static std::string GetName() { return "AxleTerrain"; }
        static std::string GetVersion() { return "1.0.0"; }
        static bool IsInitialized() { return s_Initialized; }

    private:
        static inline bool s_Initialized = false;
    };

}
