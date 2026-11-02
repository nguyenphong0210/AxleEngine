#include "VFXTestHarness.hpp"
#include <Axle/ParticleRenderer.hpp>
#include <Axle/RHIParticleRenderer.hpp>
#include <Axle/PerspectiveCamera.hpp>

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
}

bool RunVFXRenderPlanUnitTests() {
    std::cout << "  - Running TestBaselineParticleVertexStride ...\n";
    VFX_TEST_CHECK(TestBaselineParticleVertexStride(), "TestBaselineParticleVertexStride failed");

    std::cout << "  - Running TestDrawPacketSplitting ...\n";
    VFX_TEST_CHECK(TestDrawPacketSplitting(), "TestDrawPacketSplitting failed");

    std::cout << "  - Running TestLocalVsWorldTransformResolution ...\n";
    VFX_TEST_CHECK(TestLocalVsWorldTransformResolution(), "TestLocalVsWorldTransformResolution failed");

    std::cout << "  - Running TestAlphaDepthSorting ...\n";
    VFX_TEST_CHECK(TestAlphaDepthSorting(), "TestAlphaDepthSorting failed");

    return true;
}
