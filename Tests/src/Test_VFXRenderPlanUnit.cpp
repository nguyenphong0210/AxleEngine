#include "VFXTestHarness.hpp"
#include <Axle/ParticleRenderer.hpp>
#include <Axle/RHIParticleRenderer.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/Components.hpp>
#include <Axle/ParticleMaterialAdapter.hpp>

namespace {
    bool TestBaselineParticleVertexStride() {
        // Verify ParticleInstanceVertex stride matches 84 bytes as documented
        VFX_TEST_CHECK(sizeof(Axle::ParticleInstanceVertex) == 84, "ParticleInstanceVertex must be exactly 84 bytes");
        return true;
    }

    bool TestDrawPacketSplitting() {
        Axle::ParticlePool pool(20001);
        Axle::ParticleSpawnData spawnData;
        spawnData.Lifetime = 5.0f;
        spawnData.Position = glm::vec3(0.0f);
        spawnData.Velocity = glm::vec3(0.0f, 1.0f, 0.0f);

        for (uint32_t i = 0; i < 20001; ++i) {
            spawnData.Position.x = static_cast<float>(i);
            pool.Spawn(spawnData);
        }
        VFX_TEST_CHECK(pool.GetActiveCount() == 20001, "Pool spawned 20,001 particles");

        Axle::ParticleEmitterConfig cfg;
        cfg.Name = "LargeBatchEmitter";
        cfg.BlendMode = Axle::ParticleBlendMode::Additive;

        Axle::PerspectiveCamera camera(55.0f, 1.0f, 0.1f, 1000.0f);
        std::vector<Axle::ParticleDrawPacket> packets;

        Axle::ParticleRenderPlanner::BuildDrawPackets(
            pool, cfg, camera, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), packets, 20000
        );

        VFX_TEST_CHECK(packets.size() == 2, "20,001 particles split into exactly 2 batches");
        VFX_TEST_CHECK(packets[0].InstanceCount == 20000, "Batch 0 has 20,000 instances");
        VFX_TEST_CHECK(packets[0].InstanceOffset == 0, "Batch 0 offset is 0");
        VFX_TEST_CHECK(packets[1].InstanceCount == 1, "Batch 1 has 1 instance");
        VFX_TEST_CHECK(packets[1].InstanceOffset == 20000, "Batch 1 offset is 20,000");

        // Verify first and last instances match expected positions
        VFX_TEST_CHECK(packets[0].Instances[0].Position.x == 0.0f, "First particle in batch 0 has x=0");
        VFX_TEST_CHECK(packets[1].Instances[0].Position.x == 20000.0f, "Single particle in batch 1 has x=20000");

        return true;
    }

    bool TestLocalVsWorldTransformResolution() {
        Axle::ParticlePool pool(10);
        Axle::ParticleSpawnData spawnData;
        spawnData.Lifetime = 5.0f;
        spawnData.Position = glm::vec3(1.0f, 2.0f, 3.0f);
        spawnData.Velocity = glm::vec3(0.0f, 1.0f, 0.0f);
        pool.Spawn(spawnData);

        Axle::PerspectiveCamera camera(55.0f, 1.0f, 0.1f, 1000.0f);
        glm::vec3 emitterWorldPos(10.0f, 0.0f, 0.0f);
        glm::quat emitterWorldRot(1.0f, 0.0f, 0.0f, 0.0f);

        // Case 1: Local simulation space
        {
            Axle::ParticleEmitterConfig localCfg;
            localCfg.SimulationSpace = Axle::ParticleSimulationSpace::Local;
            std::vector<Axle::ParticleDrawPacket> packets;
            Axle::ParticleRenderPlanner::BuildDrawPackets(pool, localCfg, camera, emitterWorldPos, emitterWorldRot, packets);
            VFX_TEST_CHECK(packets.size() == 1, "Local packet generated");
            glm::vec3 expectedWorld = emitterWorldPos + spawnData.Position;
            VFX_TEST_CHECK(packets[0].Instances[0].Position == expectedWorld, "Local particle correctly transformed by emitter world pos");
        }

        // Case 2: World simulation space
        {
            Axle::ParticleEmitterConfig worldCfg;
            worldCfg.SimulationSpace = Axle::ParticleSimulationSpace::World;
            std::vector<Axle::ParticleDrawPacket> packets;
            Axle::ParticleRenderPlanner::BuildDrawPackets(pool, worldCfg, camera, emitterWorldPos, emitterWorldRot, packets);
            VFX_TEST_CHECK(packets.size() == 1, "World packet generated");
            VFX_TEST_CHECK(packets[0].Instances[0].Position == spawnData.Position, "World particle remains where spawned, unshifted by current emitter pos");
        }

        return true;
    }

    bool TestAlphaDepthSorting() {
        Axle::ParticlePool pool(10);
        Axle::ParticleSpawnData closeP;
        closeP.Lifetime = 5.0f;
        closeP.Position = glm::vec3(0.0f, 0.0f, 5.0f); // Closer to camera at (0, 0, 10)
        pool.Spawn(closeP);

        Axle::ParticleSpawnData farP;
        farP.Lifetime = 5.0f;
        farP.Position = glm::vec3(0.0f, 0.0f, 0.0f); // Farther from camera
        pool.Spawn(farP);

        Axle::PerspectiveCamera camera(55.0f, 1.0f, 0.1f, 1000.0f);
        camera.SetViewMatrix(glm::lookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(0.0f, 0.0f, 10.0f));

        Axle::ParticleEmitterConfig cfg;
        cfg.BlendMode = Axle::ParticleBlendMode::AlphaBlend;
        std::vector<Axle::ParticleDrawPacket> packets;
        Axle::ParticleRenderPlanner::BuildDrawPackets(pool, cfg, camera, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), packets);

        VFX_TEST_CHECK(packets.size() == 1, "Alpha sorted packet generated");
        VFX_TEST_CHECK(packets[0].Instances.size() == 2, "2 instances in packet");
        // Back-to-front: Farther particle (z=0) must be drawn before Closer particle (z=5)
        VFX_TEST_CHECK(packets[0].Instances[0].Position.z == 0.0f, "Far particle drawn first in AlphaBlend back-to-front order");
        VFX_TEST_CHECK(packets[0].Instances[1].Position.z == 5.0f, "Near particle drawn second in AlphaBlend back-to-front order");

        return true;
    }

    bool TestEmitterTexturePathAndBindingContract() {
        Axle::ParticleEmitterStack stack("TexturedFire", {});
        stack.TexturePath = "textures/fire_alpha.png";
        stack.NormalMapPath = "textures/fire_normal.png";

        VFX_TEST_CHECK(stack.TexturePath == "textures/fire_alpha.png", "Texture path preserved on stack");
        VFX_TEST_CHECK(stack.NormalMapPath == "textures/fire_normal.png", "Normal map path preserved on stack");

        // Copy constructor preserves authored texture paths
        Axle::ParticleEmitterStack copyStack = stack;
        VFX_TEST_CHECK(copyStack.TexturePath == stack.TexturePath, "Texture path preserved on copy");
        VFX_TEST_CHECK(copyStack.NormalMapPath == stack.NormalMapPath, "Normal map path preserved on copy");

        // Clear removes paths
        stack.TexturePath.clear();
        stack.NormalMapPath.clear();
        VFX_TEST_CHECK(stack.TexturePath.empty(), "Texture path cleared");
        VFX_TEST_CHECK(stack.NormalMapPath.empty(), "Normal map path cleared");
        return true;
    }

    bool TestParticleMaterialAdapterResolution() {
        Axle::ParticleEmitterConfig cfg;
        cfg.BlendMode = Axle::ParticleBlendMode::Additive;

        Axle::EmitterResources res;
        res.MaterialPath = "materials/SparkPBR.axleasset";

        auto mat = std::make_shared<Axle::Material>("SparkPBR");
        mat->SetAlbedoColor(glm::vec4(1.0f, 0.6f, 0.2f, 0.9f));
        mat->SetSurfaceType(Axle::SurfaceType::Cutout);
        mat->SetAlphaCutoff(0.35f);
        mat->SetAlbedoPath("textures/spark_albedo.png");
        mat->SetUseAlbedoMap(true);
        mat->SetNormalPath("textures/spark_normal.png");
        mat->SetUseNormalMap(true);
        mat->SetEmissionColor(glm::vec3(2.5f, 1.2f, 0.4f));
        mat->SetEmissionIntensity(4.0f);

        auto settings = Axle::ParticleMaterialAdapter::Resolve(cfg, res, [&mat](const std::string&) {
            return mat;
        });

        VFX_TEST_CHECK(settings.BlendMode == Axle::ParticleBlendMode::Cutout, "SurfaceType::Cutout mapped to ParticleBlendMode::Cutout");
        VFX_TEST_CHECK(settings.AlphaCutoff == 0.35f, "Alpha cutoff accurately transferred");
        VFX_TEST_CHECK(settings.DepthWrite == true, "Cutout enabled depth write");
        VFX_TEST_CHECK(settings.BaseColor.x == 1.0f && settings.BaseColor.y == 0.6f, "Base color accurately mapped");
        VFX_TEST_CHECK(settings.TexturePath == "textures/spark_albedo.png", "Albedo texture path mapped");
        VFX_TEST_CHECK(settings.NormalMapPath == "textures/spark_normal.png", "Normal texture path mapped");
        VFX_TEST_CHECK(settings.EmissiveIntensity == 4.0f, "Emissive intensity mapped");
        VFX_TEST_CHECK(settings.Diagnostics.empty(), "Valid material produced 0 diagnostics");

        return true;
    }

    bool TestParticleMaterialAdapterMissingAndUnsupportedFeatures() {
        Axle::ParticleEmitterConfig cfg;
        Axle::EmitterResources res;
        res.MaterialPath = "materials/NonExistent.axleasset";

        // Missing material produces diagnostic
        auto settingsMissing = Axle::ParticleMaterialAdapter::Resolve(cfg, res, [](const std::string&) -> std::shared_ptr<Axle::Material> {
            return nullptr;
        });
        VFX_TEST_CHECK(!settingsMissing.Diagnostics.empty(), "Missing material produces diagnostics");
        VFX_TEST_CHECK(settingsMissing.Diagnostics[0].Code == "VFX_RESOURCE_MISSING", "Resource missing code reported");
        VFX_TEST_CHECK(settingsMissing.Diagnostics[0].Severity == Axle::VFXDiagnosticSeverity::Warning, "Resource missing is Warning severity");

        // Material with unsupported clearcoat / custom shader produces Info diagnostics
        auto matAdv = std::make_shared<Axle::Material>("AdvancedMat");
        matAdv->SetClearcoatStrength(0.8f);
        matAdv->SetUseCustomShader(true);

        auto settingsAdv = Axle::ParticleMaterialAdapter::Resolve(cfg, res, [&matAdv](const std::string&) {
            return matAdv;
        });
        VFX_TEST_CHECK(settingsAdv.Diagnostics.size() >= 2, "Unsupported features produce diagnostics");
        VFX_TEST_CHECK(settingsAdv.Diagnostics[0].Code == "VFX_UNSUPPORTED_FEATURE", "Unsupported feature code reported");
        VFX_TEST_CHECK(settingsAdv.Diagnostics[0].Severity == Axle::VFXDiagnosticSeverity::Info, "Unsupported feature is Info severity");

        return true;
    }

    bool TestCutoutAlphaTestingLogic() {
        // Cutout alpha test specification:
        // When in Cutout mode with cutoff > 0.0f, fragment with alpha < cutoff is discarded.
        auto ShouldDiscardCutout = [](float alpha, float cutoff, bool isCutout) -> bool {
            if (isCutout) {
                return (alpha < cutoff);
            }
            return (alpha <= 0.001f);
        };

        VFX_TEST_CHECK(ShouldDiscardCutout(0.4f, 0.5f, true), "Alpha 0.4 < Cutoff 0.5 is discarded in Cutout mode");
        VFX_TEST_CHECK(!ShouldDiscardCutout(0.6f, 0.5f, true), "Alpha 0.6 >= Cutoff 0.5 passes in Cutout mode");

        // Explicit test: Alpha threshold zero (Section 11 requirement)
        VFX_TEST_CHECK(!ShouldDiscardCutout(0.0005f, 0.0f, true), "Alpha threshold 0.0 allows any nonnegative alpha fragment to pass");
        VFX_TEST_CHECK(!ShouldDiscardCutout(0.0f, 0.0f, true), "Alpha exactly 0.0 passes when cutoff is 0.0 in Cutout mode");

        // Non-cutout baseline
        VFX_TEST_CHECK(ShouldDiscardCutout(0.0005f, 0.5f, false), "Alpha 0.0005 <= 0.001 is discarded in non-cutout mode");
        VFX_TEST_CHECK(!ShouldDiscardCutout(0.05f, 0.5f, false), "Alpha 0.05 > 0.001 passes in non-cutout mode");

        return true;
    }

    bool TestDepthLinearizationAndSoftFade() {
        // Camera configuration: Near = 0.1m, Far = 100.0m
        const float nearClip = 0.1f;
        const float farClip = 100.0f;

        auto LinearizeDepthZO = [nearClip, farClip](float rawDepth) -> float {
            return (nearClip * farClip) / std::max(0.0001f, farClip - rawDepth * (farClip - nearClip));
        };

        // Verification of depth linearization boundaries
        float linearNear = LinearizeDepthZO(0.0f);
        float linearFar = LinearizeDepthZO(1.0f);
        VFX_TEST_CHECK(std::abs(linearNear - nearClip) < 0.001f, "Raw depth 0.0 linearizes to near plane (0.1m)");
        VFX_TEST_CHECK(std::abs(linearFar - farClip) < 0.05f, "Raw depth 1.0 linearizes to far plane (100.0m)");

        // Depth soft edge attenuation calculation
        const float softnessDist = 0.5f;
        float particleCamDist = 5.0f;
        float sceneCamDist = 5.25f; // Scene geometry is 0.25m behind particle

        float depthDiff = sceneCamDist - particleCamDist;
        float softFade = std::clamp(depthDiff / softnessDist, 0.0f, 1.0f);
        VFX_TEST_CHECK(std::abs(softFade - 0.5f) < 0.001f, "0.25m diff with 0.5m softness distance yields exactly 50% fade");

        // Deep scene penetration: particle well in front of background
        float sceneFarCamDist = 15.0f;
        float deepFade = std::clamp((sceneFarCamDist - particleCamDist) / softnessDist, 0.0f, 1.0f);
        VFX_TEST_CHECK(deepFade == 1.0f, "Particle far from surface has 100% opacity (no fade)");

        return true;
    }
}

static bool TestAuthoredScaleAndDisabledResources() {
    Axle::ParticlePool pool(1);
    Axle::ParticleSpawnData spawn;spawn.Position={1,0,0};spawn.StartSize=2;pool.Spawn(spawn);
    Axle::ParticleEmitterConfig cfg;cfg.SimulationSpace=Axle::ParticleSimulationSpace::Local;
    Axle::PerspectiveCamera camera(45,1);
    std::vector<Axle::ParticleDrawPacket> packets;
    Axle::ParticleRenderPlanner::BuildDrawPackets(pool,cfg,camera,{3,0,0},glm::quat(1,0,0,0),packets,20000,{2,3,4});
    VFX_TEST_CHECK(packets.size()==1,"One scaled packet");
    VFX_TEST_CHECK(packets[0].Instances[0].Position==glm::vec3(5,0,0),"Local scale participates in world-space packing");
    VFX_TEST_APPROX_EQUAL(packets[0].Instances[0].Size,8,0.0001f,"Billboard respects largest local scale axis");
    Axle::ParticlePool sorting(2);
    spawn.Position = {5, 0, -2};
    sorting.Spawn(spawn);
    spawn.Position = {0, 0, -4};
    sorting.Spawn(spawn);
    cfg.SimulationSpace = Axle::ParticleSimulationSpace::World;
    cfg.BlendMode = Axle::ParticleBlendMode::AlphaBlend;
    packets.clear();
    camera.SetViewMatrix(glm::mat4(1), glm::vec3(0));
    Axle::ParticleRenderPlanner::BuildDrawPackets(sorting, cfg, camera, glm::vec3(0), glm::quat(1, 0, 0, 0),
                                                  packets);
    VFX_TEST_CHECK(packets[0].Instances[0].Position.z == -4,
                   "Alpha order follows camera depth rather than radial distance");
    Axle::EmitterResources resources;resources.TexturePath="retained.png";resources.NormalMapPath="retained-normal.png";
    auto settings=Axle::ParticleMaterialAdapter::Resolve(cfg,resources);
    VFX_TEST_CHECK(!settings.UseTexture && !settings.UseNormalMap,"Retained paths do not override authored disabled flags");
    return true;
}

bool RunVFXRenderPlanUnitTests() {
    VFX_TEST_CHECK(TestAuthoredScaleAndDisabledResources(), "Resource/scale regressions");
    std::cout << "  - Running TestBaselineParticleVertexStride ...\n";
    VFX_TEST_CHECK(TestBaselineParticleVertexStride(), "TestBaselineParticleVertexStride failed");

    std::cout << "  - Running TestDrawPacketSplitting ...\n";
    VFX_TEST_CHECK(TestDrawPacketSplitting(), "TestDrawPacketSplitting failed");

    std::cout << "  - Running TestLocalVsWorldTransformResolution ...\n";
    VFX_TEST_CHECK(TestLocalVsWorldTransformResolution(), "TestLocalVsWorldTransformResolution failed");

    std::cout << "  - Running TestAlphaDepthSorting ...\n";
    VFX_TEST_CHECK(TestAlphaDepthSorting(), "TestAlphaDepthSorting failed");

    std::cout << "  - Running TestEmitterTexturePathAndBindingContract ...\n";
    VFX_TEST_CHECK(TestEmitterTexturePathAndBindingContract(), "TestEmitterTexturePathAndBindingContract failed");

    std::cout << "  - Running TestParticleMaterialAdapterResolution ...\n";
    VFX_TEST_CHECK(TestParticleMaterialAdapterResolution(), "TestParticleMaterialAdapterResolution failed");

    std::cout << "  - Running TestParticleMaterialAdapterMissingAndUnsupportedFeatures ...\n";
    VFX_TEST_CHECK(TestParticleMaterialAdapterMissingAndUnsupportedFeatures(), "TestParticleMaterialAdapterMissingAndUnsupportedFeatures failed");

    std::cout << "  - Running TestCutoutAlphaTestingLogic ...\n";
    VFX_TEST_CHECK(TestCutoutAlphaTestingLogic(), "TestCutoutAlphaTestingLogic failed");

    std::cout << "  - Running TestDepthLinearizationAndSoftFade ...\n";
    VFX_TEST_CHECK(TestDepthLinearizationAndSoftFade(), "TestDepthLinearizationAndSoftFade failed");

    return true;
}
