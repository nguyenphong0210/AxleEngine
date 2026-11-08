#include <AxleTerrain/TerrainPlugin.hpp>
#include <Axle/Log.hpp>

namespace Axle::Terrain {

    void TerrainPlugin::Init() {
        if (s_Initialized) {
            return;
        }

        s_Initialized = true;
        AXLE_INFO("[Plugin: AxleTerrain v{0}] Initialized successfully. (Off-Road Terrain & Sculpting System Plugin)", GetVersion());
    }

    void TerrainPlugin::Shutdown() {
        if (!s_Initialized) {
            return;
        }

        s_Initialized = false;
        AXLE_INFO("[Plugin: AxleTerrain] Shutdown complete.");
    }

}
