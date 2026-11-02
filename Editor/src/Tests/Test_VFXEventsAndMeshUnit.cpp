#include "VFXTestHarness.hpp"
#include <Axle/VFXCommon.hpp>
#include <Axle/ParticleEffectDefinition.hpp>
#include <Axle/ParticleEffectSerializer.hpp>
#include <Axle/Components.hpp>
#include <Axle/ParticleMeshSampler.hpp>

namespace {
    bool TestBaselineEventsAndMesh() {
        // Baseline test for sub-emitter triggers
        Axle::SubEmitterTrigger birth = Axle::SubEmitterTrigger::Birth;
        Axle::SubEmitterTrigger collision = Axle::SubEmitterTrigger::Collision;
        Axle::SubEmitterTrigger death = Axle::SubEmitterTrigger::Death;
        VFX_TEST_CHECK(static_cast<int>(birth) == 0, "Birth trigger index");
        VFX_TEST_CHECK(static_cast<int>(collision) == 1, "Collision trigger index");
        VFX_TEST_CHECK(static_cast<int>(death) == 2, "Death trigger index");
        return true;
    }

    bool TestSubEmitterGraphValidation() {
        // 1. Valid linear chain A -> B -> C
        Axle::ParticleEffectDefinition def;
        def.SchemaVersion = 2;
        def.Name = "ValidChain";

        Axle::EmitterDefinition eA; eA.Id = "eA"; eA.Name = "EmitterA";
        eA.Config.SubEmitters.emplace_back(Axle::SubEmitterTrigger::Death, "eB", 5, 1.0f);

        Axle::EmitterDefinition eB; eB.Id = "eB"; eB.Name = "EmitterB";
        eB.Config.SubEmitters.emplace_back(Axle::SubEmitterTrigger::Collision, "eC", 10, 1.0f);

        Axle::EmitterDefinition eC; eC.Id = "eC"; eC.Name = "EmitterC";

        def.Emitters = { eA, eB, eC };

        std::vector<std::string> errors;
        VFX_TEST_CHECK(Axle::ValidateSubEmitterGraph(def, errors), "Valid DAG should pass validation");
        VFX_TEST_CHECK(errors.empty(), "No errors for valid DAG");

        // 2. Self-referencing link A -> A
        Axle::ParticleEffectDefinition selfDef = def;
        selfDef.Emitters[0].Config.SubEmitters[0].TargetEmitterName = "eA";
        errors.clear();
        VFX_TEST_CHECK(!Axle::ValidateSubEmitterGraph(selfDef, errors), "Self-referencing emitter must fail");
        VFX_TEST_CHECK(!errors.empty(), "Error reported for self-reference");

        // 3. Non-existent target A -> NonExistent
        Axle::ParticleEffectDefinition missingDef = def;
        missingDef.Emitters[0].Config.SubEmitters[0].TargetEmitterName = "Ghost";
        errors.clear();
        VFX_TEST_CHECK(!Axle::ValidateSubEmitterGraph(missingDef, errors), "Missing target must fail");
        VFX_TEST_CHECK(!errors.empty(), "Error reported for missing target");

        // 4. Direct cycle A -> B -> A
        Axle::ParticleEffectDefinition cycleDef = def;
        cycleDef.Emitters[1].Config.SubEmitters[0].TargetEmitterName = "eA";
        errors.clear();
        VFX_TEST_CHECK(!Axle::ValidateSubEmitterGraph(cycleDef, errors), "Direct cycle A->B->A must fail");
        VFX_TEST_CHECK(!errors.empty(), "Error reported for cycle");

        // 5. Transitive cycle A -> B -> C -> A
        Axle::ParticleEffectDefinition transDef = def;
        transDef.Emitters[2].Config.SubEmitters.emplace_back(Axle::SubEmitterTrigger::Birth, "eA", 2, 1.0f);
        errors.clear();
        VFX_TEST_CHECK(!Axle::ValidateSubEmitterGraph(transDef, errors), "Transitive cycle A->B->C->A must fail");
        VFX_TEST_CHECK(!errors.empty(), "Error reported for transitive cycle");

        return true;
    }

    bool TestBoundedSubEmitterEventSpawning() {
        // 1. Setup Fire emitter with Death trigger targeting Sparks
        Axle::ParticleEmitterConfig fireConfig;
        fireConfig.Name = "Fire";
        fireConfig.MaxParticles = 50;
        fireConfig.EmissionRate = 0.0f; // controlled bursts
        fireConfig.Lifetime = { 0.1f, 0.1f };
        fireConfig.InitialSpeed = { 5.0f, 5.0f };
        fireConfig.SubEmitters.emplace_back(Axle::SubEmitterTrigger::Death, "Sparks", 5, 1.0f);
        fireConfig.SubEmitters.back().InheritColor = true;
        fireConfig.SubEmitters.back().InheritVelocity = true;

        Axle::ParticleEmitterStack fireStack("Fire", fireConfig);
        fireStack.Initialize();

        // 2. Setup Sparks emitter (Target) with TriggerOnly (no auto emission)
        Axle::ParticleEmitterConfig sparksConfig;
        sparksConfig.Name = "Sparks";
        sparksConfig.MaxParticles = 100;
        sparksConfig.EmissionRate = 0.0f;
        sparksConfig.Lifetime = { 1.0f, 1.0f };
        sparksConfig.InitialSpeed = { 2.0f, 2.0f };

        Axle::ParticleEmitterStack sparksStack("Sparks", sparksConfig);
        sparksStack.Initialize();

        std::vector<Axle::ParticleEmitterStack> stacks;
        stacks.push_back(fireStack);
        stacks.push_back(sparksStack);

        VFX_TEST_CHECK(stacks[1].System->GetActiveParticleCount() == 0, "Sparks starts empty");

        // 3. Emit 2 particles in Fire
        stacks[0].System->EmitBurst(2);
        VFX_TEST_CHECK(stacks[0].System->GetActiveParticleCount() == 2, "Fire has 2 active particles");

        // 4. Update with dt = 0.05s (particles are still alive)
        stacks[0].System->Update(0.05f);
        stacks[1].System->Update(0.05f);
        Axle::DispatchEffectEvents(stacks);
        VFX_TEST_CHECK(stacks[1].System->GetActiveParticleCount() == 0, "Sparks still empty before Fire particles die");

        // 5. Update with dt = 0.08s (total 0.13s > 0.1s lifetime -> Fire particles die and trigger Death event)
        stacks[0].System->Update(0.08f);
        stacks[1].System->Update(0.08f);
        Axle::DispatchEffectEvents(stacks);

        VFX_TEST_CHECK(stacks[0].System->GetActiveParticleCount() == 0, "Fire particles died");
        // 2 Fire particles died * 5 sparks = 10 sparks spawned
        VFX_TEST_CHECK(stacks[1].System->GetActiveParticleCount() == 10, "Sparks received 10 spawned particles from Death event");

        // 6. Test event budgeting: huge burst should clamp to budget
        stacks[0].System->Reset();
        stacks[1].System->Reset();
        stacks[0].Config.SubEmitters[0].EmitCount = 50;
        stacks[0].System->SetConfig(stacks[0].Config);
        stacks[0].System->EmitBurst(20); // 20 * 50 = 1000 requested spawns
        stacks[0].System->Update(0.15f); // all die
        stacks[1].System->Update(0.15f);

        // Dispatch with strict budget = 2 events
        Axle::DispatchEffectEvents(stacks, 2);
        // 2 events * 50 spawns = 100 spawns (pool cap is 100)
        VFX_TEST_CHECK(stacks[1].System->GetActiveParticleCount() <= 100, "Spawns bounded within budget and capacity");

        return true;
    }

    bool TestMeshSurfaceAreaWeightedSampling() {
        // 1. Two unequal-area non-spherical triangles
        // Triangle A (Area = 1.0): (0,0,0), (2,0,0), (0,1,0)
        // Triangle B (Area = 9.0): (10,0,0), (10,6,0), (13,0,0)
        std::vector<glm::vec3> positions = {
            // Triangle A: indices 0, 1, 2
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(2.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            // Triangle B: indices 3, 4, 5
            glm::vec3(10.0f, 0.0f, 0.0f),
            glm::vec3(10.0f, 6.0f, 0.0f),
            glm::vec3(13.0f, 0.0f, 0.0f),
            // Degenerate triangle: collinear points indices 6, 7, 8
            glm::vec3(5.0f, 5.0f, 5.0f),
            glm::vec3(6.0f, 6.0f, 6.0f),
            glm::vec3(7.0f, 7.0f, 7.0f)
        };
        std::vector<uint32_t> indices = {
            0, 1, 2, // Tri A
            3, 4, 5, // Tri B
            6, 7, 8  // Degenerate
        };

        Axle::ParticleMeshSampler sampler;
        VFX_TEST_CHECK(sampler.Build(positions, indices), "Sampler build succeeded with valid + degenerate triangles");
        VFX_TEST_CHECK(sampler.GetTriangleCount() == 2, "Degenerate triangle filtered out, leaving exactly 2 triangles");
        VFX_TEST_CHECK(std::abs(sampler.GetTotalArea() - 10.0f) < 1e-4f, "Total area equals 1.0 + 9.0 = 10.0");

        // Sample 2000 points
        std::mt19937 rng(42);
        uint32_t countA = 0;
        uint32_t countB = 0;
        for (int i = 0; i < 2000; ++i) {
            glm::vec3 pos;
            glm::vec3 normal;
            bool ok = sampler.Sample(pos, normal, rng);
            VFX_TEST_CHECK(ok, "Sampling point on valid mesh succeeds");
            VFX_TEST_CHECK(std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z), "Sampled position is finite");
            VFX_TEST_CHECK(std::abs(normal.z) > 0.9f, "Normal is perpendicular to XY plane");

            if (pos.x <= 5.0f) {
                countA++;
                // Check within Triangle A bounding box
                VFX_TEST_CHECK(pos.x >= -1e-4f && pos.x <= 2.0f + 1e-4f, "Sample on Tri A within X bounds");
                VFX_TEST_CHECK(pos.y >= -1e-4f && pos.y <= 1.0f + 1e-4f, "Sample on Tri A within Y bounds");
            } else {
                countB++;
                // Check within Triangle B bounding box
                VFX_TEST_CHECK(pos.x >= 10.0f - 1e-4f && pos.x <= 13.0f + 1e-4f, "Sample on Tri B within X bounds");
                VFX_TEST_CHECK(pos.y >= -1e-4f && pos.y <= 6.0f + 1e-4f, "Sample on Tri B within Y bounds");
            }
        }

        // Expected ~10% on A (200), ~90% on B (1800)
        VFX_TEST_CHECK(countA >= 140 && countA <= 260, "Triangle A received ~10% of samples (area-weighted)");
        VFX_TEST_CHECK(countB >= 1740 && countB <= 1860, "Triangle B received ~90% of samples (area-weighted)");
        VFX_TEST_CHECK(countA + countB == 2000, "All samples partitioned cleanly");

        // 2. Pure degenerate mesh fallback
        std::vector<glm::vec3> degenPositions = {
            glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(2.0f)
        };
        std::vector<uint32_t> degenIndices = { 0, 1, 2 };
        Axle::ParticleMeshSampler degenSampler;
        VFX_TEST_CHECK(!degenSampler.Build(degenPositions, degenIndices), "Pure degenerate mesh build fails gracefully");
        VFX_TEST_CHECK(!degenSampler.IsValid(), "Degenerate sampler is invalid");

        glm::vec3 fallbackPos, fallbackNormal;
        bool sampleDegen = degenSampler.Sample(fallbackPos, fallbackNormal, rng);
        VFX_TEST_CHECK(!sampleDegen, "Sample degenerate mesh returns false");
        VFX_TEST_CHECK(fallbackPos == glm::vec3(0.0f), "Fallback position is origin");
        VFX_TEST_CHECK(fallbackNormal == glm::vec3(0.0f, 1.0f, 0.0f), "Fallback normal is (0,1,0)");
        VFX_TEST_CHECK(!degenSampler.GetLastError().empty(), "Diagnostic error reported");

        // 3. Emitter shape integration test
        Axle::ParticleEmitterConfig config;
        config.Shape.Type = Axle::ParticleEmitterShapeType::MeshSurface;
        config.Shape.MeshSampler = std::make_shared<Axle::ParticleMeshSampler>(sampler);

        glm::vec3 shapePos, shapeDir;
        config.Shape.Sample(shapePos, shapeDir, rng);
        VFX_TEST_CHECK((shapePos.x <= 2.0f + 1e-4f) || (shapePos.x >= 10.0f - 1e-4f), "Emitter shape samples mesh surface");

        // Empty sampler fallback
        config.Shape.MeshSampler = nullptr;
        config.Shape.Sample(shapePos, shapeDir, rng);
        VFX_TEST_CHECK(shapePos == glm::vec3(0.0f), "Emitter shape with null sampler falls back to origin");

        return true;
    }

    bool TestParticleCollisionSimulation() {
        // 1. Floor Plane Collision & Bounce
        {
            Axle::ParticleEmitterConfig config;
            config.MaxParticles = 10;
            config.EmissionRate = 0.0f;
            config.Lifetime = { 2.0f, 2.0f };
            config.InitialSpeed = { 0.0f, 0.0f };
            config.ConstantAcceleration = glm::vec3(0.0f);
            config.GravityModifier = 0.0f;
            config.Drag = 0.0f;
            config.Collision.Enabled = true;
            config.Collision.CollisionMode = Axle::ParticleCollisionMode::Floor;
            config.Collision.FloorHeight = 0.0f;
            config.Collision.Restitution = 0.6f;
            config.Collision.Friction = 0.0f;
            config.Collision.ParticleRadius = 0.1f;
            config.Collision.MinBounceSpeed = 0.01f;

            Axle::ParticleSystem system(config);
            system.SetSeed(12345);

            Axle::ParticleSpawnData spawnData;
            spawnData.Position = glm::vec3(0.0f, 1.0f, 0.0f);
            spawnData.Velocity = glm::vec3(0.0f, -10.0f, 0.0f);
            spawnData.StartSize = 0.2f;
            spawnData.EndSize = 0.2f;
            spawnData.Lifetime = 2.0f;
            spawnData.StartColor = glm::vec4(1.0f);
            spawnData.EndColor = glm::vec4(1.0f);
            system.GetPool().Spawn(spawnData);

            system.Update(0.15f);

            VFX_TEST_CHECK(system.GetActiveParticleCount() == 1, "Particle is still alive after floor bounce");
            VFX_TEST_CHECK(system.GetCollisionCount() == 1, "Collision count incremented on floor bounce");
            const auto& pos = system.GetPool().GetPositions()[0];
            const auto& vel = system.GetPool().GetVelocities()[0];
            VFX_TEST_CHECK(pos.y >= 0.1f - 1e-4f, "Particle kept above floor plane");
            VFX_TEST_CHECK(vel.y > 5.0f && vel.y < 7.0f, "Particle velocity reflected upwards with restitution damping");
        }

        // 2. Per-Collider KillOnCollision vs Global
        {
            Axle::ParticleEmitterConfig fireConfig;
            fireConfig.Name = "Fire";
            fireConfig.MaxParticles = 10;
            fireConfig.EmissionRate = 0.0f;
            fireConfig.Lifetime = { 2.0f, 2.0f };
            fireConfig.Collision.Enabled = true;
            fireConfig.Collision.CollisionMode = Axle::ParticleCollisionMode::PlanesAndBoxes;
            fireConfig.Collision.KillOnCollision = false; // Global kill is FALSE

            // Add plane with per-plane KillOnCollision = TRUE
            Axle::ParticleCollisionPlane killPlane(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.5f, 0.0f, true);
            fireConfig.Collision.Planes.push_back(killPlane);

            // Sub-emitter triggered on Collision targeting Sparks
            fireConfig.SubEmitters.emplace_back(Axle::SubEmitterTrigger::Collision, "Sparks", 5, 1.0f);

            Axle::ParticleEmitterConfig sparkConfig;
            sparkConfig.Name = "Sparks";
            sparkConfig.MaxParticles = 20;
            sparkConfig.EmissionRate = 0.0f;
            sparkConfig.Lifetime = { 1.0f, 1.0f };

            std::vector<Axle::ParticleEmitterStack> stacks;
            stacks.emplace_back("Fire", fireConfig);
            stacks.emplace_back("Sparks", sparkConfig);
            stacks[0].Initialize(100);
            stacks[1].Initialize(101);

            Axle::ParticleSpawnData spawnData;
            spawnData.Position = glm::vec3(0.0f, 0.5f, 0.0f);
            spawnData.Velocity = glm::vec3(0.0f, -10.0f, 0.0f);
            spawnData.Lifetime = 2.0f;
            stacks[0].System->GetPool().Spawn(spawnData);

            stacks[0].System->Update(0.1f);
            stacks[1].System->Update(0.1f);
            Axle::DispatchEffectEvents(stacks);

            VFX_TEST_CHECK(stacks[0].System->GetActiveParticleCount() == 0, "Particle killed by per-plane KillOnCollision");
            VFX_TEST_CHECK(stacks[0].System->GetCollisionCount() == 1, "Collision occurred");
            VFX_TEST_CHECK(stacks[1].System->GetActiveParticleCount() == 5, "Collision triggered 5 sparks to spawn");
        }

        // 3. Scene Physics Raycast Service Binding
        {
            Axle::ParticleEmitterConfig config;
            config.MaxParticles = 10;
            config.EmissionRate = 0.0f;
            config.Lifetime = { 2.0f, 2.0f };
            config.Collision.Enabled = true;
            config.Collision.CollisionMode = Axle::ParticleCollisionMode::ScenePhysics;
            config.Collision.Restitution = 0.8f;
            config.Collision.Friction = 0.1f;
            config.Collision.ParticleRadius = 0.05f;

            bool raycastInvoked = false;
            config.Collision.WorldRaycast = [&raycastInvoked](const glm::vec3& origin, const glm::vec3& dir, float maxDist, glm::vec3& outHitPoint, glm::vec3& outHitNormal) -> bool {
                raycastInvoked = true;
                if (dir.y < 0.0f) {
                    float t = (origin.y - (-1.0f)) / (-dir.y);
                    if (t >= 0.0f && t <= maxDist) {
                        outHitPoint = origin + dir * t;
                        outHitNormal = glm::vec3(0.0f, 1.0f, 0.0f);
                        return true;
                    }
                }
                return false;
            };

            Axle::ParticleSystem system(config);
            system.SetSeed(777);

            Axle::ParticleSpawnData spawnData;
            spawnData.Position = glm::vec3(0.0f, -0.8f, 0.0f);
            spawnData.Velocity = glm::vec3(0.0f, -5.0f, 0.0f);
            spawnData.Lifetime = 2.0f;
            system.GetPool().Spawn(spawnData);

            system.Update(0.1f);

            VFX_TEST_CHECK(raycastInvoked, "Physics raycast service callback was invoked");
            VFX_TEST_CHECK(system.GetCollisionCount() == 1, "Collision registered from raycast hit");
            VFX_TEST_CHECK(system.GetPool().GetVelocities()[0].y > 0.0f, "Velocity reflected upwards from raycast normal");
        }

        // 4. Persistence Round-Trip for Collision Module
        {
            Axle::ParticleEffectDefinition effectDef;
            effectDef.SchemaVersion = 2;
            effectDef.Name = "CollisionEffect";

            Axle::EmitterDefinition emitter;
            emitter.Id = "e1";
            emitter.Name = "CollisionEmitter";
            emitter.Config.Collision.Enabled = true;
            emitter.Config.Collision.CollisionMode = Axle::ParticleCollisionMode::Floor;
            emitter.Config.Collision.FloorHeight = -2.5f;
            emitter.Config.Collision.FloorKill = true;
            emitter.Config.Collision.Restitution = 0.75f;
            emitter.Config.Collision.Friction = 0.35f;
            emitter.Config.Collision.MinBounceSpeed = 0.15f;
            emitter.Config.Collision.ParticleRadius = 0.12f;
            emitter.Config.Collision.Planes.emplace_back(glm::vec3(1.0f, 0.0f, 0.0f), 5.0f, 0.9f, 0.1f, false);
            emitter.Config.Collision.Boxes.emplace_back(glm::vec3(-5.0f), glm::vec3(5.0f), true, 0.8f, 0.2f, false);

            effectDef.Emitters.push_back(emitter);

            std::string serialized = Axle::ParticleEffectSerializer::Serialize(effectDef);
            Axle::ParticleEffectDefinition loadedDef;
            VFX_TEST_CHECK(Axle::ParticleEffectSerializer::Deserialize(serialized, loadedDef), "Deserialize collision effect succeeds");
            VFX_TEST_CHECK(loadedDef.Emitters.size() == 1, "1 emitter loaded");

            const auto& col = loadedDef.Emitters[0].Config.Collision;
            VFX_TEST_CHECK(col.Enabled == true, "Collision enabled round-trips");
            VFX_TEST_CHECK(col.CollisionMode == Axle::ParticleCollisionMode::Floor, "CollisionMode Floor round-trips");
            VFX_TEST_CHECK(std::abs(col.FloorHeight - (-2.5f)) < 1e-4f, "FloorHeight round-trips");
            VFX_TEST_CHECK(col.FloorKill == true, "FloorKill round-trips");
            VFX_TEST_CHECK(std::abs(col.Restitution - 0.75f) < 1e-4f, "Restitution round-trips");
            VFX_TEST_CHECK(std::abs(col.Friction - 0.35f) < 1e-4f, "Friction round-trips");
            VFX_TEST_CHECK(std::abs(col.ParticleRadius - 0.12f) < 1e-4f, "ParticleRadius round-trips");
            VFX_TEST_CHECK(col.Planes.size() == 1, "Planes collection round-trips");
            VFX_TEST_CHECK(std::abs(col.Planes[0].Distance - 5.0f) < 1e-4f, "Plane distance round-trips");
            VFX_TEST_CHECK(col.Boxes.size() == 1, "Boxes collection round-trips");
            VFX_TEST_CHECK(col.Boxes[0].CollideInside == true, "Box CollideInside round-trips");
        }

        return true;
    }
}

bool RunVFXEventsAndMeshUnitTests() {
    std::cout << "  - Running TestBaselineEventsAndMesh ...\n";
    VFX_TEST_CHECK(TestBaselineEventsAndMesh(), "TestBaselineEventsAndMesh failed");
    std::cout << "  - Running TestSubEmitterGraphValidation ...\n";
    VFX_TEST_CHECK(TestSubEmitterGraphValidation(), "TestSubEmitterGraphValidation failed");
    std::cout << "  - Running TestBoundedSubEmitterEventSpawning ...\n";
    VFX_TEST_CHECK(TestBoundedSubEmitterEventSpawning(), "TestBoundedSubEmitterEventSpawning failed");
    std::cout << "  - Running TestMeshSurfaceAreaWeightedSampling ...\n";
    VFX_TEST_CHECK(TestMeshSurfaceAreaWeightedSampling(), "TestMeshSurfaceAreaWeightedSampling failed");
    std::cout << "  - Running TestParticleCollisionSimulation ...\n";
    VFX_TEST_CHECK(TestParticleCollisionSimulation(), "TestParticleCollisionSimulation failed");
    return true;
}
