#include "Test_VFXAcceptance.hpp"
#include <Axle/FileSystem.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/AssetManager.hpp>
#include <iostream>
int main() {
    const std::filesystem::path root = AXLE_VFX_SOURCE_DIR;
    std::filesystem::current_path(root);
    Axle::FileSystem::SetAssetDirectory(root / "Editor" / "assets");
    bool ok = false;
    try {
        ok = Axle::VerifyVFXRendering(root);
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
    }
    auto &device = Axle::RHI::RHIDevice::Get();
    if (device.IsInitialized()) {
        device.WaitIdle();
        Axle::RHIMeshRegistry::Get().Shutdown();
        Axle::RHI::RHIMaterial::ReleaseDefaultTextures();
        device.Shutdown();
    }
    return ok ? 0 : 1;
}
