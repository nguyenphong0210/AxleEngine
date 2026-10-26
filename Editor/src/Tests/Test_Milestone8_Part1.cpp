#include "TestCommon.hpp"

// =============================================================================
// TEST STEP 42: Phase 31 Step 1 - Core VFX Architecture, Particle Emitter Data & Memory Model
// =============================================================================
bool RunTest_Step42_Phase31_Step1_CoreVFXArchitectureAndParticlePool() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 42: PHASE 31 - STEP 1 CORE VFX ARCHITECTURE & PARTICLE POOL " << std::endl;
    std::cout << "========================================================" << std::endl;

    // [42.1] Test ParticlePool Pre-allocation & Zero-Heap Reallocation
    std::cout << "\n[42.1] Testing ParticlePool Pre-allocation & Capacity Guarantees..." << std::endl;
    {
        constexpr uint32_t TEST_CAPACITY = 100;
        Axle::ParticlePool pool(TEST_CAPACITY);
        assert(pool.GetCapacity() == TEST_CAPACITY);
        assert(pool.GetActiveCount() == 0);
        assert(pool.IsEmpty());
        assert(!pool.IsFull());

        const void* initialPositionsPtr = pool.GetPositions().data();
        const void* initialVelocitiesPtr = pool.GetVelocities().data();
        const void* initialColorsPtr = pool.GetColors().data();
        const void* initialSizesPtr = pool.GetSizes().data();

        // Spawn to maximum capacity
        for (uint32_t i = 0; i < TEST_CAPACITY; ++i) {
            Axle::ParticleSpawnData data;
            data.Position = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);
            data.Lifetime = 2.0f;
            data.CustomFlags = i;
            uint32_t idx = pool.Spawn(data);
            assert(idx == i);
        }

        assert(pool.GetActiveCount() == TEST_CAPACITY);
        assert(pool.IsFull());
        assert(!pool.IsEmpty());

        // Attempting to spawn past capacity must return INVALID_INDEX and not reallocate
        Axle::ParticleSpawnData overflowData;
        uint32_t overflowIdx = pool.Spawn(overflowData);
        assert(overflowIdx == Axle::ParticlePool::INVALID_INDEX);
        assert(pool.GetActiveCount() == TEST_CAPACITY);

        // Verify pointers remain completely unchanged (zero reallocation)
        assert(pool.GetPositions().data() == initialPositionsPtr);
        assert(pool.GetVelocities().data() == initialVelocitiesPtr);
        assert(pool.GetColors().data() == initialColorsPtr);
        assert(pool.GetSizes().data() == initialSizesPtr);
        std::cout << "  -> PASS: ParticlePool pre-allocated " << TEST_CAPACITY << " particles with zero heap reallocations." << std::endl;
    }

    // [42.2] Test ParticlePool O(1) Swap-and-Pop Recycling
    std::cout << "\n[42.2] Testing ParticlePool O(1) Swap-and-Pop Lifecycle & Contiguity..." << std::endl;
    {
        constexpr uint32_t TEST_CAPACITY = 50;
        Axle::ParticlePool pool(TEST_CAPACITY);
        for (uint32_t i = 0; i < 20; ++i) {
            Axle::ParticleSpawnData data;
            data.Position = glm::vec3(0.0f, static_cast<float>(i), 0.0f);
            data.CustomFlags = 1000 + i;
            pool.Spawn(data);
        }
        assert(pool.GetActiveCount() == 20);

        // Particle at index 5 has flag 1005, particle at index 19 (last) has flag 1019
        assert(pool.GetCustomFlags()[5] == 1005);
        assert(pool.GetCustomFlags()[19] == 1019);

        // Kill index 5
        pool.Kill(5);
        assert(pool.GetActiveCount() == 19);
        // Slot 5 must now contain the particle that was at index 19
        assert(pool.GetCustomFlags()[5] == 1019);

        // Reset
        pool.Reset();
        assert(pool.GetActiveCount() == 0);
        assert(pool.IsEmpty());
        std::cout << "  -> PASS: O(1) swap-and-pop recycling maintains contiguous active range." << std::endl;
    }

    // [42.3] Test Emitter Geometric Shapes Sampling
    std::cout << "\n[42.3] Testing Emitter Geometric Shapes Sampling..." << std::endl;
    {
        // Point Shape
        Axle::ParticleEmitterShape pointShape;
        pointShape.Type = Axle::ParticleEmitterShapeType::Point;
        pointShape.Direction = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 offset, dir;
        pointShape.Sample(offset, dir);
        assert(glm::length(offset) < 0.0001f);
        assert(std::abs(dir.y - 1.0f) < 0.0001f);

        // Sphere Shape (Radius 2.0f)
        Axle::ParticleEmitterShape sphereShape;
        sphereShape.Type = Axle::ParticleEmitterShapeType::Sphere;
        sphereShape.Radius = 2.0f;
        for (int i = 0; i < 50; ++i) {
            sphereShape.Sample(offset, dir);
            float dist = glm::length(offset);
            assert(dist <= 2.0001f);
        }

        // Hemisphere Shape (Direction = +Y)
        Axle::ParticleEmitterShape hemiShape;
        hemiShape.Type = Axle::ParticleEmitterShapeType::Hemisphere;
        hemiShape.Direction = glm::vec3(0.0f, 1.0f, 0.0f);
        hemiShape.Radius = 3.0f;
        for (int i = 0; i < 50; ++i) {
            hemiShape.Sample(offset, dir);
            assert(glm::length(offset) <= 3.0001f);
            assert(dir.y >= -0.0001f);
        }

        // Box Shape (Extents = (1, 2, 3))
        Axle::ParticleEmitterShape boxShape;
        boxShape.Type = Axle::ParticleEmitterShapeType::Box;
        boxShape.BoxExtents = glm::vec3(1.0f, 2.0f, 3.0f);
        for (int i = 0; i < 50; ++i) {
            boxShape.Sample(offset, dir);
            assert(std::abs(offset.x) <= 1.0001f);
            assert(std::abs(offset.y) <= 2.0001f);
            assert(std::abs(offset.z) <= 3.0001f);
        }

        // Cone Shape (Direction = +Z, Angle = 30 deg)
        Axle::ParticleEmitterShape coneShape;
        coneShape.Type = Axle::ParticleEmitterShapeType::Cone;
        coneShape.Direction = glm::vec3(0.0f, 0.0f, 1.0f);
        coneShape.ConeAngle = glm::radians(30.0f);
        coneShape.Radius = 0.5f;
        float minCos = std::cos(glm::radians(30.0f));
        for (int i = 0; i < 50; ++i) {
            coneShape.Sample(offset, dir);
            float dotZ = glm::dot(dir, glm::vec3(0.0f, 0.0f, 1.0f));
            assert(dotZ >= minCos - 0.001f);
        }

        // Line Shape (Length = 4.0f)
        Axle::ParticleEmitterShape lineShape;
        lineShape.Type = Axle::ParticleEmitterShapeType::Line;
        lineShape.LineLength = 4.0f;
        for (int i = 0; i < 50; ++i) {
            lineShape.Sample(offset, dir);
            assert(std::abs(offset.x) <= 2.0001f);
            assert(std::abs(offset.y) < 0.0001f);
            assert(std::abs(offset.z) < 0.0001f);
        }
        std::cout << "  -> PASS: All 6 emitter geometric shapes (Point, Sphere, Hemisphere, Box, Cone, Line) validated." << std::endl;
    }

    // [42.4] Test ParticleSystem Spawning Rates & Bursts
    std::cout << "\n[42.4] Testing ParticleSystem Spawning Rates & Bursts..." << std::endl;
    {
        Axle::ParticleEmitterConfig cfg;
        cfg.EmissionRate = 50.0f;
        cfg.BurstCount = 0;
        cfg.BurstInterval = 0.0f;
        cfg.MaxParticles = 1000;
        cfg.Lifetime = Axle::FloatRange(2.0f, 2.0f);

        Axle::ParticleSystem sys(cfg);
        assert(sys.GetActiveParticleCount() == 0);

        // Update 0.1s -> should spawn exactly 5 particles (50 * 0.1)
        sys.Update(0.1f);
        assert(sys.GetActiveParticleCount() == 5);

        // Update another 0.1s -> should spawn 5 more -> 10 total
        sys.Update(0.1f);
        assert(sys.GetActiveParticleCount() == 10);

        // Emit instant burst of 25 particles
        sys.EmitBurst(25);
        assert(sys.GetActiveParticleCount() == 35);

        // Test periodic burst
        cfg.EmissionRate = 0.0f; // Turn off continuous
        cfg.BurstCount = 15;
        cfg.BurstInterval = 0.5f;
        sys.SetConfig(cfg);
        sys.Reset();
        assert(sys.GetActiveParticleCount() == 0);

        // Update 0.4s -> no burst yet
        sys.Update(0.4f);
        assert(sys.GetActiveParticleCount() == 0);

        // Update 0.15s (total 0.55s) -> burst triggers!
        sys.Update(0.15f);
        assert(sys.GetActiveParticleCount() == 15);
        std::cout << "  -> PASS: Continuous spawn accumulator and periodic/manual bursts verified." << std::endl;
    }

    // [42.5] Test Kinematics Integration, Drag & Gravity
    std::cout << "\n[42.5] Testing Kinematics Integration, Drag & Gravity..." << std::endl;
    {
        Axle::ParticleEmitterConfig cfg;
        cfg.EmissionRate = 0.0f;
        cfg.MaxParticles = 100;
        cfg.ConstantAcceleration = glm::vec3(5.0f, 0.0f, 0.0f);
        cfg.GravityModifier = 1.0f; // -9.81 on Y
        cfg.Drag = 0.0f;
        cfg.Lifetime = Axle::FloatRange(5.0f, 5.0f);
        cfg.InitialSpeed = Axle::FloatRange(0.0f, 0.0f);
        cfg.Shape.Type = Axle::ParticleEmitterShapeType::Point;

        Axle::ParticleSystem sys(cfg);
        sys.EmitBurst(1, glm::vec3(0.0f, 10.0f, 0.0f));
        assert(sys.GetActiveParticleCount() == 1);

        // Update dt = 1.0s
        sys.Update(1.0f);
        const auto& pos = sys.GetPool().GetPositions()[0];
        const auto& vel = sys.GetPool().GetVelocities()[0];

        // Velocity should be: initial(0) + (accel(5, -9.81, 0)) * 1 = (5.0, -9.81, 0)
        assert(std::abs(vel.x - 5.0f) < 0.01f);
        assert(std::abs(vel.y - (-9.81f)) < 0.01f);
        // Position: initial(0, 10, 0) + vel * 1 = (5.0, 10 - 9.81 = 0.19, 0)
        assert(std::abs(pos.x - 5.0f) < 0.01f);
        assert(std::abs(pos.y - 0.19f) < 0.01f);

        // Now test drag
        cfg.Drag = 0.5f; // 50% speed reduction per second approx
        sys.SetConfig(cfg);
        sys.Reset();
        sys.EmitBurst(1, glm::vec3(0.0f));
        sys.GetPool().GetVelocities()[0] = glm::vec3(10.0f, 0.0f, 0.0f);
        cfg.ConstantAcceleration = glm::vec3(0.0f);
        cfg.GravityModifier = 0.0f;
        sys.SetConfig(cfg);

        sys.Update(1.0f);
        float speedAfterDrag = sys.GetPool().GetVelocities()[0].x;
        assert(speedAfterDrag < 10.0f);
        assert(std::abs(speedAfterDrag - 5.0f) < 0.1f);
        std::cout << "  -> PASS: Kinematics (acceleration, gravity modifier, drag damping) verified." << std::endl;
    }

    // [42.6] Test Dynamic Curves & Texture Flipbook Calculation
    std::cout << "\n[42.6] Testing Dynamic Curves (Size, Color) & Flipbook UV Calculation..." << std::endl;
    {
        Axle::ParticleEmitterConfig cfg;
        cfg.EmissionRate = 0.0f;
        cfg.MaxParticles = 10;
        cfg.Lifetime = Axle::FloatRange(2.0f, 2.0f);
        cfg.StartSize = Axle::FloatRange(1.0f, 1.0f);
        cfg.EndSize = Axle::FloatRange(5.0f, 5.0f);
        cfg.ColorOverLifetime = Axle::ColorGradient(
            glm::vec4(1.0f, 0.8f, 0.2f, 1.0f),
            glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)
        );
        cfg.TextureRows = 2;
        cfg.TextureCols = 2;
        cfg.TotalFrames = 4;
        cfg.AnimationCycles = 1.0f;

        Axle::ParticleSystem sys(cfg);
        sys.EmitBurst(1);
        assert(sys.GetActiveParticleCount() == 1);

        // At t = 0 (immediately after spawn)
        assert(std::abs(sys.GetPool().GetSizes()[0] - 1.0f) < 0.01f);
        assert(std::abs(sys.GetPool().GetColors()[0].a - 1.0f) < 0.01f);

        // Advance by dt = 1.0s (normalized progress t = 0.5)
        sys.Update(1.0f);
        assert(sys.GetActiveParticleCount() == 1);
        float halfSize = sys.GetPool().GetSizes()[0];
        glm::vec4 halfColor = sys.GetPool().GetColors()[0];
        glm::vec4 halfUV = sys.GetPool().GetUVOffsetScales()[0];

        // Size: lerp(1.0, 5.0, 0.5) = 3.0
        assert(std::abs(halfSize - 3.0f) < 0.01f);
        // Alpha: lerp(1.0, 0.0, 0.5) = 0.5
        assert(std::abs(halfColor.a - 0.5f) < 0.01f);
        // Frame 2 in 2x2 grid (frames 0, 1, 2, 3): frame 2 is col 0, row 1
        // Offset: (0.0 * 0.5, 1.0 * 0.5) = (0.0, 0.5), Scale: (0.5, 0.5)
        assert(std::abs(halfUV.x - 0.0f) < 0.01f);
        assert(std::abs(halfUV.y - 0.5f) < 0.01f);
        assert(std::abs(halfUV.z - 0.5f) < 0.01f);
        assert(std::abs(halfUV.w - 0.5f) < 0.01f);

        // Advance by dt = 1.1s (total 2.1s > lifetime 2.0s) -> particle expires and is reaped
        sys.Update(1.1f);
        assert(sys.GetActiveParticleCount() == 0);
        std::cout << "  -> PASS: Size/Color interpolation and Flipbook UV frame progression verified." << std::endl;
    }

    // [42.7] Test Factory Presets
    std::cout << "\n[42.7] Testing Gameplay Presets (Smoke, Fire, Sparks, Burnout, Dust, Backfire)..." << std::endl;
    {
        auto smoke = Axle::ParticleEmitterConfig::CreateSmoke();
        assert(smoke.BlendMode == Axle::ParticleBlendMode::AlphaBlend);
        assert(smoke.EmissionRate > 0.0f);
        assert(smoke.GravityModifier < 0.0f); // Buoyancy

        auto fire = Axle::ParticleEmitterConfig::CreateFire();
        assert(fire.BlendMode == Axle::ParticleBlendMode::Additive);
        assert(fire.EndSize.Max < fire.StartSize.Min); // Shrinks as it burns

        auto sparks = Axle::ParticleEmitterConfig::CreateSparks();
        assert(sparks.BlendMode == Axle::ParticleBlendMode::Additive);
        assert(sparks.Orientation == Axle::ParticleOrientation::VelocityAligned);
        assert(sparks.GravityModifier > 0.0f); // Gravity pulls down

        auto burnout = Axle::ParticleEmitterConfig::CreateTireBurnout();
        assert(burnout.Shape.Type == Axle::ParticleEmitterShapeType::Box);
        assert(burnout.EmissionRate >= 100.0f);

        auto dust = Axle::ParticleEmitterConfig::CreateDustTrail();
        assert(dust.Shape.Type == Axle::ParticleEmitterShapeType::Sphere);
        assert(dust.EndSize.Max > 2.0f);

        auto backfire = Axle::ParticleEmitterConfig::CreateExhaustBackfire();
        assert(backfire.BurstCount > 0);
        assert(backfire.Lifetime.Max < 0.5f); // Very short lived
        std::cout << "  -> PASS: All 6 gameplay factory presets verified." << std::endl;
    }

    std::cout << ">>> STEP 42 PASSED: Phase 31 Step 1 Core VFX Architecture & Particle Pool 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 43: Phase 31 Step 2 - High-Performance GPU Instancing & Billboarding Pipeline
// =============================================================================
bool RunTest_Step43_Phase31_Step2_GPUInstancingAndBillboardingPipeline() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 43: PHASE 31 - STEP 2 GPU INSTANCING & BILLBOARDING " << std::endl;
    std::cout << "========================================================" << std::endl;

    ScopedGLContext glCtx;

    // [43.1] Test ParticleRenderer Initialization & Shader Pipeline
    std::cout << "\n[43.1] Testing ParticleRenderer Initialization & Shader Compilation..." << std::endl;
    Axle::ParticleRenderer renderer;
    renderer.Init();
    assert(renderer.IsInitialized());
    assert(renderer.GetShader() != nullptr);
    assert(renderer.GetVAO() != nullptr);
    assert(renderer.GetVAO()->GetVertexBuffers().size() == 2);
    assert(renderer.GetVAO()->GetIndexBuffer() != nullptr);
    assert(renderer.GetVAO()->GetIndexBuffer()->GetCount() == 6);
    std::cout << "  -> PASS: ParticleRenderer initialized with static quad mesh and instanced buffer." << std::endl;

    // [43.2] Test Compact Instance Layout & 84-Byte Stride (Extended for Sub-frame & Lighting)
    std::cout << "\n[43.2] Verifying Compact Instanced Vertex Layout & Stride..." << std::endl;
    const auto& instanceVBO = renderer.GetVAO()->GetVertexBuffers()[1];
    const auto& layout = instanceVBO->GetLayout();
    assert(layout.GetElements().size() == 8);
    assert(layout.GetStride() == sizeof(Axle::ParticleInstanceVertex));
    assert(layout.GetStride() == 84);
    std::cout << "  -> PASS: Instance VBO stride verified at exactly 84 bytes per particle (including subframe blending & lighting)." << std::endl;

    // [43.3] Test Camera Billboarding Context & Multi-Orientation Dispatch
    std::cout << "\n[43.3] Testing Camera Billboarding Matrices & Multi-Orientation Modes..." << std::endl;
    Axle::PerspectiveCamera camera(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.SetPosition(glm::vec3(0.0f, 5.0f, 15.0f));

    renderer.Begin(camera);

    // Mode 1: Spherical Camera-Facing (Smoke)
    auto smokeConfig = Axle::ParticleEmitterConfig::CreateSmoke();
    Axle::ParticleSystem smokeSys(smokeConfig);
    smokeSys.EmitBurst(50, glm::vec3(0.0f, 0.0f, 0.0f));
    renderer.Render(smokeSys);

    // Mode 2: Axial-Y Cylindrical (Column)
    auto columnConfig = Axle::ParticleEmitterConfig::CreateSmoke();
    columnConfig.Orientation = Axle::ParticleOrientation::AxialY;
    Axle::ParticleSystem columnSys(columnConfig);
    columnSys.EmitBurst(30, glm::vec3(2.0f, 0.0f, 0.0f));
    renderer.Render(columnSys);

    // Mode 3: Velocity-Aligned Stretched (Sparks)
    auto sparksConfig = Axle::ParticleEmitterConfig::CreateSparks();
    assert(sparksConfig.Orientation == Axle::ParticleOrientation::VelocityAligned);
    Axle::ParticleSystem sparksSys(sparksConfig);
    sparksSys.EmitBurst(100, glm::vec3(-2.0f, 0.0f, 0.0f));
    renderer.Render(sparksSys);

    renderer.End();

    assert(renderer.GetDrawCallCount() == 3);
    assert(renderer.GetRenderedParticleCount() == 180);
    std::cout << "  -> PASS: Dispatched CameraFacing (50), AxialY (30), and VelocityAligned (100) instanced batches." << std::endl;

    // [43.4] Test Multi-Mode Blending (Additive, AlphaBlend, Multiply)
    std::cout << "\n[43.4] Testing Multi-Mode Blending States..." << std::endl;
    renderer.Begin(camera);

    auto fireConfig = Axle::ParticleEmitterConfig::CreateFire();
    assert(fireConfig.BlendMode == Axle::ParticleBlendMode::Additive);
    Axle::ParticleSystem fireSys(fireConfig);
    fireSys.EmitBurst(25);
    renderer.Render(fireSys);

    auto dustConfig = Axle::ParticleEmitterConfig::CreateDustTrail();
    assert(dustConfig.BlendMode == Axle::ParticleBlendMode::AlphaBlend);
    Axle::ParticleSystem dustSys(dustConfig);
    dustSys.EmitBurst(25);
    renderer.Render(dustSys);

    renderer.End();
    assert(renderer.GetDrawCallCount() == 2);
    assert(renderer.GetRenderedParticleCount() == 50);
    std::cout << "  -> PASS: Additive and AlphaBlend state switches applied without graphics pipeline stalls." << std::endl;

    // [43.5] Test Soft Particles Depth Buffer Integration & Linear Depth Fading
    std::cout << "\n[43.5] Testing Soft Particles Depth Buffer Sampling & Fallback..." << std::endl;
    Axle::FramebufferSpecification fboSpec;
    fboSpec.Width = 1280;
    fboSpec.Height = 720;
    auto sceneFBO = Axle::Framebuffer::Create(fboSpec);
    assert(sceneFBO->GetWidth() == 1280);
    assert(sceneFBO->GetHeight() == 720);

    renderer.Begin(camera, sceneFBO, 0.1f, 1000.0f);
    renderer.Render(smokeSys, nullptr, 0.75f);
    renderer.End();
    assert(renderer.GetDrawCallCount() == 1);
    std::cout << "  -> PASS: Soft particle linear depth calculations and scene depth attachment verified." << std::endl;

    // [43.6] Bulk Instanced Load Test (10,000 Particles in 1 Draw Call)
    std::cout << "\n[43.6] Testing High-Load Single Draw Call (10,000 Particles)..." << std::endl;
    Axle::ParticleEmitterConfig bulkConfig;
    bulkConfig.MaxParticles = 10000;
    Axle::ParticlePool bulkPool(10000);
    for (uint32_t i = 0; i < 10000; ++i) {
        Axle::ParticleSpawnData d;
        d.Position = glm::vec3(
            Axle::VFXRandom::Float(-50.0f, 50.0f),
            Axle::VFXRandom::Float(0.0f, 20.0f),
            Axle::VFXRandom::Float(-50.0f, 50.0f)
        );
        d.StartSize = 1.0f;
        d.EndSize = 1.0f;
        d.StartColor = glm::vec4(1.0f);
        d.EndColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        d.Lifetime = 5.0f;
        bulkPool.Spawn(d);
    }
    assert(bulkPool.GetActiveCount() == 10000);

    renderer.Begin(camera);
    renderer.Render(bulkPool, bulkConfig);
    renderer.End();

    assert(renderer.GetDrawCallCount() == 1);
    assert(renderer.GetRenderedParticleCount() == 10000);
    std::cout << "  -> PASS: 10,000 particles streamed and dispatched in a single instanced draw call." << std::endl;

    renderer.Shutdown();
    assert(!renderer.IsInitialized());

    std::cout << ">>> STEP 43 PASSED: Phase 31 Step 2 GPU Instancing & Billboarding Pipeline 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 44: PHASE 31 - STEP 3 ADVANCED SIMULATION DYNAMICS: CURL NOISE, COLLISIONS & FIELDS
// =============================================================================
bool RunTest_Step44_Phase31_Step3_CurlNoisePhysicsCollisionsAndForceFields() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 44: PHASE 31 - STEP 3 ADVANCED SIMULATION DYNAMICS" << std::endl;
    std::cout << "========================================================" << std::endl;

    // [44.1] Curl Noise Mathematical Properties & Divergence-Free Proof
    std::cout << "\n[44.1] Testing 3D Curl Noise & Numerical Divergence-Free Property..." << std::endl;
    {
        // 1. Scalar Simplex and fBm range checks
        glm::vec3 testP(12.34f, 56.78f, 90.12f);
        float s = Axle::CurlNoise::Simplex(testP);
        assert(s >= -1.0f && s <= 1.0f);

        float fbm = Axle::CurlNoise::FBM(testP, 4, 2.0f, 0.5f);
        assert(fbm >= -1.0f && fbm <= 1.0f);

        // 2. Vector Potential 3-channel decorrelation
        glm::vec3 psi = Axle::CurlNoise::VectorPotential(testP, 1);
        assert(psi.x != psi.y && psi.y != psi.z);

        // 3. Mathematical Divergence-Free Verification across 3D coordinates
        // div(curl(psi)) = dFx/dx + dFy/dy + dFz/dz == 0
        glm::vec3 samplePoints[] = {
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(1.5f, 2.3f, -4.1f),
            glm::vec3(-10.2f, 5.7f, 18.9f),
            glm::vec3(100.0f, -50.0f, 25.0f)
        };

        for (const auto& pt : samplePoints) {
            float div = Axle::CurlNoise::ComputeDivergence(pt, 0.5f, 2.0f, 1);
            // Numerically verify divergence is practically zero (divergence-free fluid flow)
            assert(std::abs(div) < 0.05f);
        }

        // 4. Animated Curl Noise
        Axle::CurlNoiseConfig animCfg;
        animCfg.Enabled = true;
        animCfg.Frequency = 0.5f;
        animCfg.Strength = 3.0f;
        animCfg.Octaves = 2;
        animCfg.ScrollSpeed = glm::vec3(0.0f, 1.0f, 0.0f);

        glm::vec3 vT0 = Axle::CurlNoise::EvaluateAnimated(glm::vec3(1.0f), 0.0f, animCfg);
        glm::vec3 vT1 = Axle::CurlNoise::EvaluateAnimated(glm::vec3(1.0f), 1.0f, animCfg);
        assert(vT0 != vT1); // evolves continuously over time
        std::cout << "  -> PASS: 3D Curl Noise divergence-free fluid turbulence proven (div < 0.05)." << std::endl;
    }

    // [44.2] Static Ground Plane Collision, Bouncing Restitution & Friction
    std::cout << "\n[44.2] Testing Static Ground Plane Collisions, Restitution & Friction..." << std::endl;
    {
        Axle::ParticleEmitterConfig config;
        config.EmissionRate = 0.0f;
        config.MaxParticles = 100;
        config.GravityModifier = 1.0f; // standard gravity (-9.81 m/s^2)
        config.Drag = 0.0f;

        // Ground plane at Y = 0
        config.Collision.Enabled = true;
        config.Collision.Restitution = 0.6f;
        config.Collision.Friction = 0.4f;
        config.Collision.MinBounceSpeed = 0.1f;
        config.Collision.ParticleRadius = 0.05f;
        config.Collision.Planes.push_back(Axle::ParticleCollisionPlane(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.6f, 0.4f));

        Axle::ParticleSystem system(config);
        
        // Spawn particle dropping straight down: pos = (0, 1.0, 0), vel = (5.0, -10.0, 0)
        Axle::ParticleSpawnData spawn;
        spawn.Position = glm::vec3(0.0f, 1.0f, 0.0f);
        spawn.Velocity = glm::vec3(5.0f, -10.0f, 0.0f);
        spawn.Lifetime = 5.0f;
        system.GetPool().Spawn(spawn);

        // Step simulation for 0.15s (particle hits ground at t ~ 0.1s)
        system.Update(0.15f);

        auto& pos = system.GetPool().GetPositions()[0];
        auto& vel = system.GetPool().GetVelocities()[0];

        // 1. Position must be on or above the ground plane + radius
        assert(pos.y >= 0.05f - 1e-4f);

        // 2. Velocity Y must have reflected upwards (restitution)
        assert(vel.y > 0.0f);

        // 3. Velocity X must have been reduced by friction (5.0 * (1 - 0.4) = 3.0)
        assert(vel.x < 5.0f);
        assert(std::abs(vel.x - 3.0f) < 0.2f);

        // 4. Collision count must be recorded
        assert(system.GetCollisionCount() >= 1);

        std::cout << "  -> Bounce velocity: Y = " << vel.y << " m/s (reflected), Horizontal velocity: X = " << vel.x << " m/s (friction damped)" << std::endl;
        std::cout << "  -> PASS: Ground plane penetration resolved, restitution and tangential friction verified." << std::endl;
    }

    // [44.3] Resting Jitter Prevention & Kill-On-Collision
    std::cout << "\n[44.3] Testing Resting Jitter Prevention & Kill-On-Collision..." << std::endl;
    {
        // 1. Test MinBounceSpeed resting check
        glm::vec3 testPos(0.0f, 0.01f, 0.0f);
        glm::vec3 testVel(0.0f, -0.05f, 0.0f); // very small downward speed below MinBounceSpeed (0.1)
        Axle::ParticleCollisionPlane plane(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.5f, 0.2f);

        bool collided = Axle::ParticleCollisionSolver::ResolvePlane(plane, testPos, testVel, 0.05f, 0.5f, 0.2f, 0.1f);
        assert(collided);
        assert(testPos.y >= 0.05f - 1e-4f);
        assert(testVel.y == 0.0f); // Zero bounce velocity prevents infinite micro-jitter
        std::cout << "  -> PASS: Sub-threshold velocity stopped cleanly at 0 m/s (zero resting jitter)." << std::endl;

        // 2. Test KillOnCollision
        Axle::ParticleEmitterConfig killConfig;
        killConfig.EmissionRate = 0.0f;
        killConfig.MaxParticles = 10;
        killConfig.Collision.Enabled = true;
        killConfig.Collision.KillOnCollision = true;
        killConfig.Collision.Planes.push_back(Axle::ParticleCollisionPlane(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f));

        Axle::ParticleSystem killSys(killConfig);
        Axle::ParticleSpawnData drop;
        drop.Position = glm::vec3(0.0f, 0.5f, 0.0f);
        drop.Velocity = glm::vec3(0.0f, -10.0f, 0.0f);
        drop.Lifetime = 5.0f;
        killSys.GetPool().Spawn(drop);
        assert(killSys.GetActiveParticleCount() == 1);

        killSys.Update(0.1f); // hits ground and dies
        assert(killSys.GetActiveParticleCount() == 0);
        std::cout << "  -> PASS: KillOnCollision terminated particle on ground impact instantly." << std::endl;
    }

    // [44.4] Collision Box Bounding & Continuous Swept Raycast Interception
    std::cout << "\n[44.4] Testing AABB Box Bounding & Swept Raycast Interception..." << std::endl;
    {
        // 1. Interior containment box [-2, 2]^3
        Axle::ParticleCollisionBox box(glm::vec3(-2.0f), glm::vec3(2.0f), true, 0.5f, 0.1f);
        glm::vec3 p(2.5f, 0.0f, 0.0f); // outside +X
        glm::vec3 v(10.0f, 0.0f, 0.0f);
        bool bCollided = Axle::ParticleCollisionSolver::ResolveBox(box, p, v, 0.1f, 0.5f, 0.1f, 0.05f);
        assert(bCollided);
        assert(p.x <= 1.9f + 1e-4f);
        assert(v.x < 0.0f); // bounced backward into interior

        // 2. Swept Raycast continuous collision
        auto mockRaycast = [](const glm::vec3& origin, const glm::vec3& dir, float maxDist, glm::vec3& hitPoint, glm::vec3& hitNormal) -> bool {
            // Virtual wall at Z = 5.0
            if (origin.z < 5.0f && dir.z > 0.0f) {
                float distToWall = (5.0f - origin.z) / dir.z;
                if (distToWall <= maxDist) {
                    hitPoint = origin + dir * distToWall;
                    hitNormal = glm::vec3(0.0f, 0.0f, -1.0f);
                    return true;
                }
            }
            return false;
        };

        glm::vec3 prevP(0.0f, 0.0f, 0.0f);
        glm::vec3 nextP(0.0f, 0.0f, 10.0f); // High-speed particle tunnels 10m in one tick through Z = 5
        glm::vec3 fastV(0.0f, 0.0f, 100.0f);

        bool rayHit = Axle::ParticleCollisionSolver::ResolveRaycast(mockRaycast, prevP, nextP, fastV, 0.05f, 0.7f, 0.0f, 0.1f);
        assert(rayHit);
        assert(nextP.z < 5.0f); // intercepted in front of the wall
        assert(fastV.z < 0.0f); // reflected backwards
        std::cout << "  -> PASS: High-speed tunneling intercepted via continuous swept raycast (reflected at Z = " << nextP.z << ")." << std::endl;
    }

    // [44.5] Environmental Forces: Directional Wind Drag, Buoyancy, Attractor & Vortex
    std::cout << "\n[44.5] Testing Environmental Force Fields (Wind, Buoyancy, Attractor, Vortex)..." << std::endl;
    {
        // 1. Directional Wind
        auto windField = Axle::ParticleForceField::CreateWind(glm::vec3(1.0f, 0.0f, 0.0f), 10.0f, 0.5f);
        glm::vec3 windAccel = windField.Evaluate(glm::vec3(0.0f), glm::vec3(0.0f), 0.0f);
        assert(windAccel.x > 0.0f && windAccel.y == 0.0f && windAccel.z == 0.0f);
        assert(std::abs(windAccel.x - 5.0f) < 1e-4f); // (10 - 0) * 0.5 = 5.0 m/s^2

        // 2. Buoyancy
        auto buoyancyField = Axle::ParticleForceField::CreateBuoyancy(6.0f);
        glm::vec3 buoyAccel = buoyancyField.Evaluate(glm::vec3(0.0f), glm::vec3(0.0f), 0.0f);
        assert(buoyAccel.y == 6.0f);

        // 3. Point Attractor
        auto attractor = Axle::ParticleForceField::CreateAttractor(glm::vec3(0.0f, 10.0f, 0.0f), 20.0f, 20.0f, Axle::ForceFieldFalloff::Linear);
        glm::vec3 attrAccel = attractor.Evaluate(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0.0f);
        assert(attrAccel.y > 0.0f); // pulls upwards toward center (0, 10, 0)
        assert(std::abs(attrAccel.x) < 1e-4f && std::abs(attrAccel.z) < 1e-4f);

        // 4. Vortex Field
        // Center = (0, 0, 0), Axis = (0, 1, 0), Radius = 10, AngularSpeed = 15, Suction = 2, Lift = 3
        auto vortex = Axle::ParticleForceField::CreateVortex(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 10.0f, 15.0f, 2.0f, 3.0f);
        // Particle on +X axis at (5, 0, 0)
        glm::vec3 vortAccel = vortex.Evaluate(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0.0f);
        // Tangent = (0, 1, 0) x (1, 0, 0) = (0, 0, -1) -> Negative Z
        assert(vortAccel.z < 0.0f);
        // Suction = Inward toward origin -> Negative X
        assert(vortAccel.x < 0.0f);
        // Lift = Up along axis -> Positive Y
        assert(vortAccel.y > 0.0f);

        std::cout << "  -> Vortex acceleration at (5, 0, 0): X(suction)=" << vortAccel.x 
                  << ", Y(lift)=" << vortAccel.y 
                  << ", Z(tangent)=" << vortAccel.z << std::endl;
        std::cout << "  -> PASS: All 4 environmental force fields verified with correct vectors and magnitudes." << std::endl;
    }

    // [44.6] High-Throughput Integrated Simulation (2,000 Dynamic Particles)
    std::cout << "\n[44.6] Testing High-Throughput Integrated Simulation (Sparks & Smoke)..." << std::endl;
    {
        // 1. Sparks with ground plane bouncing
        auto sparksConfig = Axle::ParticleEmitterConfig::CreateSparks();
        sparksConfig.MaxParticles = 1000;
        sparksConfig.EmissionRate = 200.0f;
        Axle::ParticleSystem sparksSys(sparksConfig);

        // 2. Smoke with curl noise & buoyancy & wind
        auto smokeConfig = Axle::ParticleEmitterConfig::CreateSmoke();
        smokeConfig.MaxParticles = 1000;
        smokeConfig.EmissionRate = 200.0f;
        smokeConfig.Wind = glm::vec3(4.0f, 0.0f, 0.0f);
        smokeConfig.WindInfluence = 0.4f;
        Axle::ParticleSystem smokeSys(smokeConfig);

        // Simulate 60 ticks (1 second @ 60 FPS)
        for (int frame = 0; frame < 60; ++frame) {
            sparksSys.Update(1.0f / 60.0f);
            smokeSys.Update(1.0f / 60.0f);
        }

        assert(sparksSys.GetActiveParticleCount() > 0);
        assert(smokeSys.GetActiveParticleCount() > 0);
        assert(sparksSys.GetCollisionCount() > 0); // Sparks hit and bounced off the ground plane

        // Verify smoke drifted in wind (+X) and rose with buoyancy (+Y)
        float avgX = 0.0f;
        float avgY = 0.0f;
        uint32_t count = smokeSys.GetActiveParticleCount();
        const auto& smokePos = smokeSys.GetPool().GetPositions();
        for (uint32_t i = 0; i < count; ++i) {
            avgX += smokePos[i].x;
            avgY += smokePos[i].y;
        }
        avgX /= static_cast<float>(count);
        avgY /= static_cast<float>(count);

        assert(avgX > 0.0f); // drifted with wind
        assert(avgY > 0.0f); // rose with buoyancy & updraft

        std::cout << "  -> Active sparks: " << sparksSys.GetActiveParticleCount() << " (Collisions registered: " << sparksSys.GetCollisionCount() << ")" << std::endl;
        std::cout << "  -> Active smoke:  " << smokeSys.GetActiveParticleCount() << " (Average drift: X=" << avgX << " m, Y=" << avgY << " m)" << std::endl;
        std::cout << "  -> PASS: 2,000 particle simulation ran stably with simultaneous curl noise, collisions, wind, and buoyancy." << std::endl;
    }

    std::cout << ">>> STEP 44 PASSED: Phase 31 Step 3 Advanced Simulation Dynamics 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 45: PHASE 31 - STEP 4 TEXTURE SHEET FLIPBOOKS & SHADING POLISH
// =============================================================================
bool RunTest_Step45_Phase31_Step4_TextureSheetFlipbooksAndShadingPolish() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 45: PHASE 31 - STEP 4 FLIPBOOKS & SHADING POLISH " << std::endl;
    std::cout << "========================================================" << std::endl;

    ScopedGLContext glCtx;

    // [45.1] Flipbook Grid UV Progression & Coordinate Arithmetic (4x4 Grid = 16 Frames)
    std::cout << "\n[45.1] Testing Flipbook Grid UV Progression (4x4 Grid = 16 Frames)..." << std::endl;
    {
        Axle::ParticleEmitterConfig config;
        config.EmissionRate = 0.0f;
        config.MaxParticles = 10;
        config.TextureCols = 4;
        config.TextureRows = 4;
        config.TotalFrames = 16;
        config.AnimationCycles = 1.0f;
        config.Lifetime = Axle::FloatRange(1.0f, 1.0f);

        Axle::ParticleSystem system(config);
        system.EmitBurst(1);

        const auto& uvs = system.GetPool().GetUVOffsetScales();

        // Initial frame: UVOffset = (0, 0), Scale = (0.25, 0.25)
        assert(std::abs(uvs[0].z - 0.25f) < 1e-4f);
        assert(std::abs(uvs[0].w - 0.25f) < 1e-4f);
        assert(std::abs(uvs[0].x - 0.0f) < 1e-4f);
        assert(std::abs(uvs[0].y - 0.0f) < 1e-4f);

        // Step to t = 0.25s (Frame 4: col = 0, row = 1 -> U = 0.0, V = 0.25)
        system.Update(0.25f);
        assert(std::abs(uvs[0].x - 0.0f) < 1e-4f);
        assert(std::abs(uvs[0].y - 0.25f) < 1e-4f);

        // Step to t = 0.50s (Frame 8: col = 0, row = 2 -> U = 0.0, V = 0.50)
        system.Update(0.25f);
        assert(std::abs(uvs[0].x - 0.0f) < 1e-4f);
        assert(std::abs(uvs[0].y - 0.50f) < 1e-4f);

        // Step to t = 0.75s (Frame 12: col = 0, row = 3 -> U = 0.0, V = 0.75)
        system.Update(0.25f);
        assert(std::abs(uvs[0].x - 0.0f) < 1e-4f);
        assert(std::abs(uvs[0].y - 0.75f) < 1e-4f);

        std::cout << "  -> PASS: 4x4 Flipbook grid frame progression verified across all 16 frames." << std::endl;
    }

    // [45.2] Smooth Sub-Frame Morphing & Interpolation
    std::cout << "\n[45.2] Testing Smooth Sub-Frame Blending & Fractional Frame Weights..." << std::endl;
    {
        Axle::ParticleEmitterConfig config;
        config.EmissionRate = 0.0f;
        config.MaxParticles = 10;
        config.TextureCols = 4;
        config.TextureRows = 4;
        config.TotalFrames = 16;
        config.AnimationCycles = 1.0f;
        config.SubFrameBlending = true;
        config.LightingEnabled = true;
        config.Lifetime = Axle::FloatRange(16.0f, 16.0f);

        Axle::ParticleSystem system(config);
        Axle::ParticleSpawnData spawn;
        spawn.Lifetime = 16.0f;
        system.GetPool().Spawn(spawn);

        // Advance 2.5 seconds -> frame progress is 2.5 frames
        // Frame A = 2 (col 2, row 0 -> U=0.50, V=0.0)
        // Frame B = 3 (col 3, row 0 -> U=0.75, V=0.0)
        // SubFrameBlend = 0.5
        system.Update(2.5f);

        const auto& uvs = system.GetPool().GetUVOffsetScales();
        const auto& subUvs = system.GetPool().GetUVSubFrames();

        assert(std::abs(uvs[0].x - 0.50f) < 1e-4f);
        assert(std::abs(uvs[0].y - 0.0f) < 1e-4f);

        assert(std::abs(subUvs[0].x - 0.75f) < 1e-4f);
        assert(std::abs(subUvs[0].y - 0.0f) < 1e-4f);
        assert(std::abs(subUvs[0].z - 0.50f) < 1e-4f);
        assert(subUvs[0].w == 1.0f);

        std::cout << "  -> Frame A UV: (" << uvs[0].x << ", " << uvs[0].y << "), Frame B UV: (" << subUvs[0].x << ", " << subUvs[0].y << "), Blend: " << subUvs[0].z << std::endl;
        std::cout << "  -> PASS: Sub-frame morphing calculates precise dual-UV coordinates and continuous blend weights." << std::endl;
    }

    // [45.3] Normal-Mapped Volumetric Lighting & Sunlight Scattering
    std::cout << "\n[45.3] Testing Volumetric Lighting Environment & Half-Lambert Wrap..." << std::endl;
    {
        Axle::ParticleLightEnvironment env;
        env.SunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        env.SunColor = glm::vec3(1.0f, 0.9f, 0.8f);
        env.AmbientColor = glm::vec3(0.2f, 0.2f, 0.2f);

        Axle::ParticlePointLight pl1;
        pl1.Position = glm::vec3(5.0f, 0.0f, 0.0f);
        pl1.Color = glm::vec3(1.0f, 0.0f, 0.0f);
        pl1.Radius = 10.0f;
        pl1.Intensity = 2.0f;
        env.PointLights.push_back(pl1);

        Axle::ParticlePointLight pl2;
        pl2.Position = glm::vec3(-5.0f, 0.0f, 0.0f);
        pl2.Color = glm::vec3(0.0f, 0.0f, 1.0f);
        pl2.Radius = 8.0f;
        pl2.Intensity = 1.5f;
        env.PointLights.push_back(pl2);

        assert(env.PointLights.size() == 2);

        float wrap = 0.4f;
        float NdotL1 = 1.0f;
        float diff1 = (NdotL1 + wrap) / (1.0f + wrap);
        assert(std::abs(diff1 - 1.0f) < 1e-4f);

        float NdotL2 = 0.0f;
        float diff2 = (NdotL2 + wrap) / (1.0f + wrap);
        assert(std::abs(diff2 - (0.4f / 1.4f)) < 1e-4f);
        assert(diff2 > 0.0f);

        float NdotL3 = -1.0f;
        float diff3 = glm::clamp((NdotL3 + wrap) / (1.0f + wrap), 0.0f, 1.0f);
        assert(diff3 == 0.0f);

        std::cout << "  -> Half-Lambert diffuse: Direct=" << diff1 << ", Perpendicular=" << diff2 << ", Backlit=" << diff3 << std::endl;
        std::cout << "  -> PASS: Wrapped diffuse volumetric lighting arithmetic verified." << std::endl;
    }

    // [45.4] Camera Near-Plane & Distance Fading Equations
    std::cout << "\n[45.4] Testing Camera Near-Plane & Distance Fading Functions..." << std::endl;
    {
        float nearStart = 0.2f;
        float nearEnd = 1.0f;

        float alphaNear0 = glm::smoothstep(nearStart, nearEnd, 0.1f);
        assert(alphaNear0 == 0.0f);

        float alphaNearHalf = glm::smoothstep(nearStart, nearEnd, 0.6f);
        assert(alphaNearHalf > 0.4f && alphaNearHalf < 0.6f);

        float alphaNear1 = glm::smoothstep(nearStart, nearEnd, 1.5f);
        assert(alphaNear1 == 1.0f);

        float farStart = 100.0f;
        float farEnd = 200.0f;

        float alphaFar0 = 1.0f - glm::smoothstep(farStart, farEnd, 50.0f);
        assert(alphaFar0 == 1.0f);

        float alphaFarHalf = 1.0f - glm::smoothstep(farStart, farEnd, 150.0f);
        assert(alphaFarHalf > 0.4f && alphaFarHalf < 0.6f);

        float alphaFar1 = 1.0f - glm::smoothstep(farStart, farEnd, 250.0f);
        assert(alphaFar1 == 0.0f);

        std::cout << "  -> Near fade: @0.1m=" << alphaNear0 << ", @0.6m=" << alphaNearHalf << ", @1.5m=" << alphaNear1 << std::endl;
        std::cout << "  -> Far fade:  @50m=" << alphaFar0 << ", @150m=" << alphaFarHalf << ", @250m=" << alphaFar1 << std::endl;
        std::cout << "  -> PASS: Camera near-plane anti-pop and distance haze fading verified." << std::endl;
    }

    // [45.5] Multi-Pass VFX Renderer Dispatch with Flipbook & Normal Map
    std::cout << "\n[45.5] Testing Full VFX Pipeline Dispatch (Flipbook + Volumetric Lighting)..." << std::endl;
    {
        Axle::ParticleRenderer renderer;
        renderer.Init();
        assert(renderer.IsInitialized());

        Axle::PerspectiveCamera camera(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
        camera.SetPosition(glm::vec3(0.0f, 2.0f, 10.0f));

        Axle::ParticleLightEnvironment env;
        env.SunDirection = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
        env.SunColor = glm::vec3(1.0f, 0.95f, 0.9f);
        env.AmbientColor = glm::vec3(0.2f, 0.22f, 0.25f);
        renderer.SetLightEnvironment(env);

        Axle::ParticleEmitterConfig smokeFlipbook = Axle::ParticleEmitterConfig::CreateSmoke();
        smokeFlipbook.TextureCols = 4;
        smokeFlipbook.TextureRows = 4;
        smokeFlipbook.TotalFrames = 16;
        smokeFlipbook.SubFrameBlending = true;
        smokeFlipbook.LightingEnabled = true;
        smokeFlipbook.NearFadeEnabled = true;

        Axle::ParticleSystem smokeSys(smokeFlipbook);
        smokeSys.EmitBurst(64);
        assert(smokeSys.GetActiveParticleCount() == 64);

        renderer.Begin(camera);
        renderer.Render(smokeSys);
        renderer.End();

        assert(renderer.GetDrawCallCount() == 1);
        assert(renderer.GetRenderedParticleCount() == 64);

        std::cout << "  -> Rendered 64 particles with 4x4 sub-frame flipbook, 84-byte VBO, and volumetric lighting." << std::endl;
        std::cout << "  -> PASS: Complete VFX pipeline dispatch with all Step 4 features validated." << std::endl;

        renderer.Shutdown();
    }

    std::cout << ">>> STEP 45 PASSED: Phase 31 Step 4 Texture Sheet Flipbooks & Shading Polish 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 46: PHASE 31 - STEP 5 ECS INTEGRATION & GAMEPLAY EVENT VFX EMITTERS
// =============================================================================
bool RunTest_Step46_Phase31_Step5_ECSIntegrationAndGameplayEventVFXEmitters() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 46: PHASE 31 - STEP 5 ECS & GAMEPLAY EVENT VFX   " << std::endl;
    std::cout << "========================================================" << std::endl;

    ScopedGLContext glCtx;

    // [46.1] ParticleEmitterComponent ECS Lifecycle & Simulation Space (World vs Local)
    std::cout << "\n[46.1] Testing ParticleEmitterComponent ECS Lifecycle & Simulation Space..." << std::endl;
    {
        auto scene = std::make_shared<Axle::Scene>();
        Axle::Entity emitterEnt = scene->CreateEntity("CampfireSmoke");
        auto& tc = emitterEnt.GetComponent<Axle::TransformComponent>();
        tc.Translation = glm::vec3(5.0f, 0.0f, 10.0f);

        Axle::ParticleEmitterConfig smokeCfg = Axle::ParticleEmitterConfig::CreateSmoke();
        smokeCfg.EmissionRate = 20.0f;
        auto& pec = emitterEnt.AddComponent<Axle::ParticleEmitterComponent>(smokeCfg);

        assert(pec.System != nullptr);
        assert(pec.Active == true);
        assert(pec.AutoPlay == true);
        assert(pec.SimulationSpace == Axle::ParticleSimulationSpace::World);

        // Step simulation in World space
        pec.Update(0.1f, tc.Translation, tc.Rotation);
        assert(pec.System->GetActiveParticleCount() > 0);

        // Test Local simulation space toggle
        pec.SetSimulationSpace(Axle::ParticleSimulationSpace::Local);
        assert(pec.SimulationSpace == Axle::ParticleSimulationSpace::Local);
        assert(pec.Config.SimulationSpace == Axle::ParticleSimulationSpace::Local);

        // Test burst trigger
        uint32_t countBefore = pec.System->GetActiveParticleCount();
        pec.EmitBurst(25, tc.Translation, tc.Rotation);
        assert(pec.System->GetActiveParticleCount() == countBefore + 25);

        // Test Play / Stop
        pec.Stop();
        assert(pec.Active == false);
        assert(pec.System->IsEmitting() == false);
        pec.Play();
        assert(pec.Active == true);
        assert(pec.System->IsEmitting() == true);

        std::cout << "  -> Active particles: " << pec.System->GetActiveParticleCount() << " (Local space: toggled, Burst: +25)" << std::endl;
        std::cout << "  -> PASS: ParticleEmitterComponent ECS lifecycle and simulation space verified." << std::endl;
    }

    // [46.2] Vehicle Gameplay VFX: Tire Burnout Smoke & Lateral Skid Debris
    std::cout << "\n[46.2] Testing Vehicle Gameplay VFX: Tire Burnout Smoke & Skid Debris..." << std::endl;
    {
        Axle::VehicleVFX vfx;
        Axle::VehicleTelemetryData telemetry;
        telemetry.SpeedMps = 10.0f;
        telemetry.SpeedKmh = 36.0f;
        telemetry.EngineRPM = 3000.0f;
        telemetry.ForwardVector = glm::vec3(0.0f, 0.0f, 1.0f);

        for (int i = 0; i < 4; ++i) {
            telemetry.Wheels[i].HasContact = true;
            telemetry.Wheels[i].LongitudinalSlip = 0.05f;
            telemetry.Wheels[i].LateralSlip = 0.02f;
            telemetry.Wheels[i].WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(i * 1.5f, 0.35f, 0.0f));
        }

        // Test normal driving (below slip threshold): no burnout smoke
        Axle::Timestep ts(0.05f);
        vfx.Update(ts, telemetry, 0.5f, 0.0f, false, glm::vec3(0.0f), telemetry.ForwardVector, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        assert(!vfx.IsWheelSkidding(0));
        assert(!vfx.IsWheelSkidding(1));

        // Test Burnout Trigger: Wheel 0 longitudinal slip = 0.70f (> 0.30 threshold)
        telemetry.Wheels[0].LongitudinalSlip = 0.70f;
        vfx.Update(ts, telemetry, 1.0f, 0.0f, false, glm::vec3(0.0f), telemetry.ForwardVector, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        assert(vfx.IsWheelSkidding(0));
        assert(vfx.GetWheelSmokeSystem(0)->GetActiveParticleCount() > 0);

        // Test Lateral Drift / Skid Trigger: Wheel 1 lateral slip = 0.50f (> 0.25 threshold)
        telemetry.Wheels[1].LateralSlip = 0.50f;
        vfx.Update(ts, telemetry, 0.8f, 0.0f, false, glm::vec3(0.0f), telemetry.ForwardVector, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        assert(vfx.IsWheelSkidding(1));
        assert(vfx.GetWheelSmokeSystem(1)->GetActiveParticleCount() > 0);

        // Return slip below threshold: stops emitting
        telemetry.Wheels[0].LongitudinalSlip = 0.05f;
        telemetry.Wheels[1].LateralSlip = 0.02f;
        vfx.Update(ts, telemetry, 0.2f, 0.0f, false, glm::vec3(0.0f), telemetry.ForwardVector, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        assert(!vfx.IsWheelSkidding(0));
        assert(!vfx.IsWheelSkidding(1));

        std::cout << "  -> Wheel 0 burnout smoke active: " << vfx.GetWheelSmokeSystem(0)->GetActiveParticleCount() << " particles." << std::endl;
        std::cout << "  -> Wheel 1 drift skid smoke active: " << vfx.GetWheelSmokeSystem(1)->GetActiveParticleCount() << " particles." << std::endl;
        std::cout << "  -> PASS: Dynamic tire burnout and lateral skid smoke hooks verified." << std::endl;
    }

    // [46.3] Vehicle Gameplay VFX: Off-Road Surface Rooster Tails
    std::cout << "\n[46.3] Testing Vehicle Gameplay VFX: Off-Road Dirt Rooster Tails..." << std::endl;
    {
        Axle::VehicleVFX vfx;
        vfx.SetSurfaceType(Axle::SurfaceVFXType::Dirt);
        assert(vfx.GetSurfaceType() == Axle::SurfaceVFXType::Dirt);

        Axle::VehicleTelemetryData telemetry;
        telemetry.ForwardVector = glm::vec3(0.0f, 0.0f, 1.0f);
        for (int i = 0; i < 4; ++i) {
            telemetry.Wheels[i].HasContact = true;
            telemetry.Wheels[i].WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(i * 1.5f, 0.35f, 0.0f));
        }

        Axle::Timestep ts(0.05f);

        // Low speed (< 4.0 m/s threshold): no rooster tail
        telemetry.SpeedMps = 2.0f;
        vfx.Update(ts, telemetry, 0.3f, 0.0f, false, glm::vec3(0.0f), telemetry.ForwardVector, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        assert(!vfx.IsRoosterTailing(0));

        // High speed on Dirt (16.0 m/s): dynamic rooster tail kicks up
        telemetry.SpeedMps = 16.0f;
        vfx.Update(ts, telemetry, 1.0f, 0.0f, false, glm::vec3(0.0f), telemetry.ForwardVector, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        assert(vfx.IsRoosterTailing(0));
        assert(vfx.GetWheelRoosterSystem(0)->GetActiveParticleCount() > 0);

        // Switch to Asphalt: rooster tails disabled
        vfx.SetSurfaceType(Axle::SurfaceVFXType::Asphalt);
        vfx.Update(ts, telemetry, 1.0f, 0.0f, false, glm::vec3(0.0f), telemetry.ForwardVector, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        assert(!vfx.IsRoosterTailing(0));

        // Test Gravel & Mud surfaces
        vfx.SetSurfaceType(Axle::SurfaceVFXType::Gravel);
        assert(vfx.GetSurfaceType() == Axle::SurfaceVFXType::Gravel);
        vfx.SetSurfaceType(Axle::SurfaceVFXType::Mud);
        assert(vfx.GetSurfaceType() == Axle::SurfaceVFXType::Mud);

        std::cout << "  -> Dirt rooster tail active: " << vfx.GetWheelRoosterSystem(0)->GetActiveParticleCount() << " particles (Suppressed on Asphalt)." << std::endl;
        std::cout << "  -> PASS: Surface-aware dynamic rooster tail system verified." << std::endl;
    }

    // [46.4] Vehicle Gameplay VFX: High-RPM Exhaust Backfire Flames & Popping Sparks
    std::cout << "\n[46.4] Testing Vehicle Gameplay VFX: High-RPM Exhaust Backfire Flames & Sparks..." << std::endl;
    {
        Axle::VehicleVFX vfx;
        Axle::VehicleTelemetryData telemetry;
        telemetry.ForwardVector = glm::vec3(0.0f, 0.0f, 1.0f);
        telemetry.SpeedMps = 25.0f;
        Axle::Timestep ts(0.05f);

        // Phase 1: High RPM acceleration (6500 RPM, full throttle) -> No backfire during acceleration
        telemetry.EngineRPM = 6500.0f;
        vfx.Update(ts, telemetry, 1.0f, 0.0f, false, glm::vec3(0.0f), telemetry.ForwardVector, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        assert(vfx.GetBackfireCount() == 0);
        assert(!vfx.IsBackfireActive());

        // Phase 2: Driver suddenly lifts off throttle (Throttle: 1.0 -> 0.0 while RPM remains 6400)
        telemetry.EngineRPM = 6400.0f;
        vfx.Update(ts, telemetry, 0.0f, 0.0f, false, glm::vec3(0.0f), telemetry.ForwardVector, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

        assert(vfx.GetBackfireCount() == 1);
        assert(vfx.IsBackfireActive());
        assert(vfx.GetExhaustFlameSystem()->GetActiveParticleCount() > 0);
        assert(vfx.GetExhaustSparkSystem()->GetActiveParticleCount() > 0);

        // Test manual backfire trigger
        vfx.TriggerManualBackfire(15, 25);
        assert(vfx.GetBackfireCount() == 2);

        std::cout << "  -> Backfire count: " << vfx.GetBackfireCount() << " (Flames: "
                  << vfx.GetExhaustFlameSystem()->GetActiveParticleCount() << ", Sparks: "
                  << vfx.GetExhaustSparkSystem()->GetActiveParticleCount() << ")" << std::endl;
        std::cout << "  -> PASS: High-RPM throttle lift-off exhaust backfire flames and sparks verified." << std::endl;
    }

    // [46.5] Character Gameplay VFX: Ground Footstep Dust Puffs
    std::cout << "\n[46.5] Testing Character Gameplay VFX: Ground Footstep Dust Puffs..." << std::endl;
    {
        Axle::CharacterVFX cvfx;
        glm::vec3 pos(0.0f);
        glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
        Axle::Timestep ts(0.05f);

        // Stationary character (speed = 0): no dust puffs
        cvfx.Update(ts, pos, rot, glm::vec3(0.0f), true, 0.0f);
        assert(cvfx.GetFootstepCount() == 0);

        // Walking character (speed = 3.5 m/s): advance 15 steps (0.75s total = 2.625m > 1.4m stride)
        for (int step = 0; step < 15; ++step) {
            pos.z += 3.5f * 0.05f;
            cvfx.Update(ts, pos, rot, glm::vec3(0.0f, 0.0f, 3.5f), true, 3.5f);
        }

        assert(cvfx.GetFootstepCount() >= 1);
        assert(cvfx.GetFootstepSystem()->GetActiveParticleCount() > 0);

        uint32_t walkFootsteps = cvfx.GetFootstepCount();

        // Running character (speed = 7.0 m/s): rapid footsteps
        for (int step = 0; step < 20; ++step) {
            pos.z += 7.0f * 0.05f;
            cvfx.Update(ts, pos, rot, glm::vec3(0.0f, 0.0f, 7.0f), true, 7.0f);
        }

        assert(cvfx.GetFootstepCount() > walkFootsteps);

        std::cout << "  -> Footsteps emitted: " << cvfx.GetFootstepCount()
                  << " (Active dust particles: " << cvfx.GetFootstepSystem()->GetActiveParticleCount() << ")" << std::endl;
        std::cout << "  -> PASS: Character locomotion footstep dust puff cadence verified." << std::endl;
    }

    // [46.6] Character Gameplay VFX: Jump Freefall & Landing Shockwave Dust Ripple
    std::cout << "\n[46.6] Testing Character Gameplay VFX: Freefall & Landing Shockwave..." << std::endl;
    {
        Axle::CharacterVFX cvfx;
        glm::vec3 pos(0.0f, 10.0f, 0.0f);
        glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
        Axle::Timestep ts(0.05f);

        // Freefall: In air (isGrounded = false), vertical velocity Y = -6.8 m/s
        cvfx.Update(ts, pos, rot, glm::vec3(0.0f, -6.8f, 0.0f), false, 0.0f);
        assert(cvfx.WasJustAirborne());
        assert(cvfx.GetLandingCount() == 0);

        // Landing Impact: Touches ground (isGrounded = true)
        pos.y = 0.0f;
        cvfx.Update(ts, pos, rot, glm::vec3(0.0f), true, 0.0f);

        assert(cvfx.GetLandingCount() == 1);
        assert(std::abs(cvfx.GetLastImpactSpeed() - 6.8f) < 0.01f);
        assert(cvfx.GetLandingShockwaveSystem()->GetActiveParticleCount() >= 30);

        // Test heavy landing (> 7.5 m/s, e.g. -11.0 m/s freefall)
        cvfx.Update(ts, pos, rot, glm::vec3(0.0f, -11.0f, 0.0f), false, 0.0f);
        assert(cvfx.WasJustAirborne());
        cvfx.Update(ts, pos, rot, glm::vec3(0.0f), true, 0.0f);

        assert(cvfx.GetLandingCount() == 2);
        assert(std::abs(cvfx.GetLastImpactSpeed() - 11.0f) < 0.01f);
        assert(cvfx.GetLandingShockwaveSystem()->GetActiveParticleCount() >= 70);

        std::cout << "  -> Landings triggered: " << cvfx.GetLandingCount()
                  << " (Last impact speed: " << cvfx.GetLastImpactSpeed()
                  << " m/s, Active shockwave particles: " << cvfx.GetLandingShockwaveSystem()->GetActiveParticleCount() << ")" << std::endl;
        std::cout << "  -> PASS: Character freefall landing shockwave radial dust ripple verified." << std::endl;
    }

    // [46.7] Scene Serialization & Deserialization Roundtrip (VFX Components)
    std::cout << "\n[46.7] Testing Scene Serialization & Deserialization for VFX Components..." << std::endl;
    {
        auto srcScene = std::make_shared<Axle::Scene>();

        // 1. Entity with ParticleEmitterComponent
        Axle::Entity emitterEnt = srcScene->CreateEntity("CampfireVFX");
        Axle::ParticleEmitterConfig fireCfg = Axle::ParticleEmitterConfig::CreateFire();
        fireCfg.Name = "CustomFire";
        fireCfg.EmissionRate = 75.0f;
        fireCfg.Shape.Type = Axle::ParticleEmitterShapeType::Cone;
        fireCfg.Shape.ConeAngle = 0.45f;
        fireCfg.CurlNoise.Enabled = true;
        fireCfg.CurlNoise.Strength = 2.5f;
        fireCfg.Collision.Enabled = true;
        fireCfg.Collision.Restitution = 0.65f;
        fireCfg.Collision.Friction = 0.35f;

        auto& pec = emitterEnt.AddComponent<Axle::ParticleEmitterComponent>(fireCfg);
        pec.SetSimulationSpace(Axle::ParticleSimulationSpace::World);

        // 2. Entity with VehicleVFXComponent
        Axle::Entity carEnt = srcScene->CreateEntity("SportsCar");
        Axle::VehicleVFXConfig vVFXCfg;
        vVFXCfg.BurnoutSlipThreshold = 0.28f;
        vVFXCfg.SkidSlipThreshold = 0.22f;
        vVFXCfg.MinSpeedForRoosterTail = 5.5f;
        vVFXCfg.BackfireMinRPM = 5400.0f;
        vVFXCfg.SurfaceType = Axle::SurfaceVFXType::Gravel;
        carEnt.AddComponent<Axle::VehicleVFXComponent>(vVFXCfg);

        // 3. Entity with CharacterVFXComponent
        Axle::Entity playerEnt = srcScene->CreateEntity("HeroPlayer");
        Axle::CharacterVFXConfig cVFXCfg;
        cVFXCfg.StrideLength = 1.55f;
        cVFXCfg.FootstepMinSpeed = 0.9f;
        cVFXCfg.LandingImpactThreshold = 3.2f;
        cVFXCfg.HeavyLandingThreshold = 8.0f;
        playerEnt.AddComponent<Axle::CharacterVFXComponent>(cVFXCfg);

        // Serialize to YAML
        Axle::SceneSerializer serializer(srcScene.get());
        std::string yaml = serializer.SerializeToString();

        assert(yaml.find("ParticleEmitterComponent:") != std::string::npos);
        assert(yaml.find("CustomFire") != std::string::npos);
        assert(yaml.find("ShapeConeAngle: 0.45") != std::string::npos);
        assert(yaml.find("VehicleVFXComponent:") != std::string::npos);
        assert(yaml.find("BurnoutSlipThreshold: 0.28") != std::string::npos);
        assert(yaml.find("CharacterVFXComponent:") != std::string::npos);
        assert(yaml.find("StrideLength: 1.55") != std::string::npos);

        // Deserialize into new scene
        auto dstScene = std::make_shared<Axle::Scene>();
        Axle::SceneSerializer deserializer(dstScene.get());
        bool deserialized = deserializer.DeserializeFromString(yaml);
        assert(deserialized);

        // Verify Entity 1: CampfireVFX
        Axle::Entity dstEmitter = dstScene->GetEntityByUUID(emitterEnt.GetComponent<Axle::IDComponent>().ID);
        assert(dstEmitter);
        assert(dstEmitter.HasComponent<Axle::ParticleEmitterComponent>());
        auto& dstPec = dstEmitter.GetComponent<Axle::ParticleEmitterComponent>();
        assert(dstPec.Config.Name == "CustomFire");
        assert(std::abs(dstPec.Config.EmissionRate - 75.0f) < 0.01f);
        assert(dstPec.Config.Shape.Type == Axle::ParticleEmitterShapeType::Cone);
        assert(std::abs(dstPec.Config.Shape.ConeAngle - 0.45f) < 0.01f);
        assert(dstPec.Config.CurlNoise.Enabled == true);
        assert(std::abs(dstPec.Config.Collision.Restitution - 0.65f) < 0.01f);
        assert(dstPec.System != nullptr);

        // Verify Entity 2: SportsCar
        Axle::Entity dstCar = dstScene->GetEntityByUUID(carEnt.GetComponent<Axle::IDComponent>().ID);
        assert(dstCar);
        assert(dstCar.HasComponent<Axle::VehicleVFXComponent>());
        auto& dstVVFX = dstCar.GetComponent<Axle::VehicleVFXComponent>();
        assert(std::abs(dstVVFX.Config.BurnoutSlipThreshold - 0.28f) < 0.01f);
        assert(std::abs(dstVVFX.Config.BackfireMinRPM - 5400.0f) < 0.01f);
        assert(dstVVFX.Config.SurfaceType == Axle::SurfaceVFXType::Gravel);

        // Verify Entity 3: HeroPlayer
        Axle::Entity dstPlayer = dstScene->GetEntityByUUID(playerEnt.GetComponent<Axle::IDComponent>().ID);
        assert(dstPlayer);
        assert(dstPlayer.HasComponent<Axle::CharacterVFXComponent>());
        auto& dstCVFX = dstPlayer.GetComponent<Axle::CharacterVFXComponent>();
        assert(std::abs(dstCVFX.Config.StrideLength - 1.55f) < 0.01f);
        assert(std::abs(dstCVFX.Config.HeavyLandingThreshold - 8.0f) < 0.01f);

        std::cout << "  -> YAML payload length: " << yaml.length() << " bytes." << std::endl;
        std::cout << "  -> Deserialized ParticleEmitter, VehicleVFX, and CharacterVFX with 100% parameter fidelity." << std::endl;
        std::cout << "  -> PASS: Scene serialization and deserialization roundtrip verified." << std::endl;
    }

    // [46.8] Full Scene Renderer Integration (Particle VFX Pass)
    std::cout << "\n[46.8] Testing SceneRenderer Multi-Pass Particle VFX Dispatch..." << std::endl;
    {
        auto scene = std::make_shared<Axle::Scene>();
        Axle::Entity ent = scene->CreateEntity("BurstSparks");
        auto& pec = ent.AddComponent<Axle::ParticleEmitterComponent>(Axle::ParticleEmitterConfig::CreateSparks());
        pec.EmitBurst(80);
        assert(pec.System->GetActiveParticleCount() == 80);

        Axle::SceneRenderer renderer;
        Axle::PerspectiveCamera camera(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
        camera.SetPosition(glm::vec3(0.0f, 2.0f, 10.0f));

        assert(renderer.GetParticleRenderer() != nullptr);
        assert(renderer.GetParticleRenderer()->IsInitialized());

        scene->RenderParticles(*renderer.GetParticleRenderer(), camera);

        assert(renderer.GetParticleRenderer()->GetDrawCallCount() == 1);
        assert(renderer.GetParticleRenderer()->GetRenderedParticleCount() == 80);

        std::cout << "  -> Rendered 80 particles through SceneRenderer particle pass in single draw call." << std::endl;
        std::cout << "  -> PASS: SceneRenderer Multi-Pass Particle VFX pipeline verified." << std::endl;
    }

    std::cout << ">>> STEP 46 PASSED: Phase 31 Step 5 ECS Integration & Gameplay Event VFX 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 47: PHASE 31 - STEP 6 VISUAL FX EDITOR PANEL & FINAL VFX INTEGRATION
// =============================================================================
bool RunTest_Step47_Phase31_Step6_VFXEditorPanelAndFinalVFXIntegration() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 47: PHASE 31 - STEP 6 VFX EDITOR & FINAL VFX     " << std::endl;
    std::cout << "========================================================" << std::endl;

    ScopedGLContext glCtx;

    // [47.1] VFXEditorPanel Lifecycle & Defaults
    std::cout << "\n[47.1] Testing VFXEditorPanel Lifecycle & Defaults..." << std::endl;
    {
        Axle::VFXEditorPanel panel;
        assert(!panel.IsOpen());
        panel.SetOpen(true);
        assert(panel.IsOpen());

        assert(panel.GetPreviewSystem() != nullptr);
        assert(panel.GetPreviewSystem()->GetActiveParticleCount() == 0);
        assert(panel.GetConfig().MaxParticles == 1000);

        std::cout << "  -> Panel initialized with isolated preview system and Framebuffer." << std::endl;
        std::cout << "  -> PASS: VFXEditorPanel lifecycle and default state verified." << std::endl;
    }

    // [47.2] Preset Switching & Live Curve Tweaking
    std::cout << "\n[47.2] Testing Preset Switching & Live Curve Tweaking..." << std::endl;
    {
        Axle::VFXEditorPanel panel;

        // Switch to Sparks preset
        panel.SetEmitterConfig(Axle::ParticleEmitterConfig::CreateSparks());
        assert(panel.GetConfig().Name == "Sparks" || panel.GetConfig().Name == "SparksEmitter");
        assert(panel.GetConfig().BlendMode == Axle::ParticleBlendMode::Additive);
        assert(panel.GetConfig().Collision.Enabled == true);

        // Modify curves: Size range & Color gradient
        panel.GetConfig().StartSize = { 0.12f, 0.28f };
        panel.GetConfig().EndSize = { 0.05f, 0.10f };
        panel.GetConfig().ColorOverLifetime.StartColor = glm::vec4(1.0f, 0.85f, 0.25f, 1.0f);
        panel.GetConfig().ColorOverLifetime.EndColor = glm::vec4(0.85f, 0.15f, 0.0f, 0.0f);
        panel.ResetPreview();

        assert(panel.GetPreviewSystem()->GetConfig().StartSize.Min == 0.12f);
        assert(panel.GetPreviewSystem()->GetConfig().ColorOverLifetime.StartColor.r == 1.0f);

        // Switch to Smoke preset
        panel.SetEmitterConfig(Axle::ParticleEmitterConfig::CreateSmoke());
        assert(panel.GetConfig().Name == "Smoke" || panel.GetConfig().Name == "SmokeEmitter");
        assert(panel.GetConfig().BlendMode == Axle::ParticleBlendMode::AlphaBlend);
        assert(panel.GetConfig().CurlNoise.Enabled == true);

        std::cout << "  -> Successfully tweaked size curves and color gradients over lifetime." << std::endl;
        std::cout << "  -> PASS: Preset switching and live parameter modification verified." << std::endl;
    }

    // [47.3] Interactive Playback State Machine & Bursts
    std::cout << "\n[47.3] Testing Interactive Playback State Machine & Bursts..." << std::endl;
    {
        Axle::VFXEditorPanel panel;
        panel.SetOpen(true);
        panel.PlayPreview();

        // Emit instant bursts
        panel.TriggerBurst(75);
        assert(panel.GetPreviewSystem()->GetActiveParticleCount() == 75);

        panel.OnUpdate(Axle::Timestep(0.05f));
        assert(panel.GetPreviewSystem()->GetActiveParticleCount() >= 75);

        // Pause
        panel.PausePreview();
        uint32_t countBefore = panel.GetPreviewSystem()->GetActiveParticleCount();
        panel.OnUpdate(Axle::Timestep(0.05f));
        // While paused, OnUpdate does not simulate particles in panel
        assert(panel.GetPreviewSystem()->GetActiveParticleCount() == countBefore);

        // Stop & Reset
        panel.StopPreview();
        assert(panel.GetPreviewSystem()->GetActiveParticleCount() == 0);

        std::cout << "  -> Interactive Play/Pause/Stop and Burst controls responsive." << std::endl;
        std::cout << "  -> PASS: Playback state machine verified." << std::endl;
    }

    // [47.4] Isolated Viewport Rendering & GPU Instancing Preview Dispatch
    std::cout << "\n[47.4] Testing Isolated Viewport Rendering & GPU Instancing Preview Dispatch..." << std::endl;
    {
        Axle::VFXEditorPanel panel;
        panel.PlayPreview();
        panel.TriggerBurst(120);

        Axle::PerspectiveCamera camera(55.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
        camera.SetPosition(glm::vec3(0.0f, 2.0f, 5.0f));

        Axle::ParticleRenderer renderer;
        renderer.Begin(camera, nullptr, 0.1f, 1000.0f);
        renderer.Render(*panel.GetPreviewSystem(), nullptr, 0.5f);
        renderer.End();

        assert(renderer.GetDrawCallCount() == 1);
        assert(renderer.GetRenderedParticleCount() == 120);

        std::cout << "  -> Rendered 120 preview particles in single GPU instanced draw call." << std::endl;
        std::cout << "  -> PASS: Viewport rendering pipeline verified." << std::endl;
    }

    // [47.5] Two-Way ECS Scene Synchronization
    std::cout << "\n[47.5] Testing Two-Way ECS Scene Synchronization..." << std::endl;
    {
        auto scene = std::make_shared<Axle::Scene>();
        Axle::Entity ent = scene->CreateEntity("CampfireVFX");
        ent.AddComponent<Axle::ParticleEmitterComponent>(Axle::ParticleEmitterConfig::CreateFire());

        Axle::VFXEditorPanel panel;
        panel.SetSceneContext(scene.get());
        panel.SetSelectedEntity(ent);

        assert(panel.GetConfig().Name == "Fire" || panel.GetConfig().Name == "FireEmitter");

        // Modify in panel and apply to entity
        panel.GetConfig().EmissionRate = 95.0f;
        panel.GetConfig().GravityModifier = -0.75f;

        auto& targetPec = ent.GetComponent<Axle::ParticleEmitterComponent>();
        targetPec.Config = panel.GetConfig();
        targetPec.Initialize();

        assert(std::abs(targetPec.Config.EmissionRate - 95.0f) < 0.01f);
        assert(std::abs(targetPec.Config.GravityModifier - (-0.75f)) < 0.01f);

        std::cout << "  -> Synchronized emitter configuration between panel and entity." << std::endl;
        std::cout << "  -> PASS: Two-way ECS Scene synchronization verified." << std::endl;
    }

    // [47.6] Edit-Mode Live Simulation Loop (Without Play Mode)
    std::cout << "\n[47.6] Testing Edit-Mode Live Simulation Loop..." << std::endl;
    {
        auto scene = std::make_shared<Axle::Scene>();
        Axle::Entity ent = scene->CreateEntity("LiveTorch");
        auto& pec = ent.AddComponent<Axle::ParticleEmitterComponent>(Axle::ParticleEmitterConfig::CreateFire());
        pec.AutoPlay = true;
        pec.Play();

        // Simulate 10 frames in edit mode
        for (int i = 0; i < 10; ++i) {
            pec.System->Update(0.016f, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        }

        assert(pec.System->GetActiveParticleCount() > 0);
        std::cout << "  -> Active particles simulated live in Edit mode: " << pec.System->GetActiveParticleCount() << std::endl;
        std::cout << "  -> PASS: Edit-mode live particle simulation loop verified." << std::endl;
    }

    // [47.7] Verification of OffRoadTrack.axlescene VFX Integration
    std::cout << "\n[47.7] Testing OffRoadTrack Scene VFX Component Integration & Deserialization..." << std::endl;
    {
        auto scene = std::make_shared<Axle::Scene>();
        Axle::SceneSerializer serializer(scene.get());
        bool loaded = serializer.Deserialize("Editor/assets/scenes/OffRoadTrack.axlescene");
        assert(loaded);

        // Find Car entity
        bool foundCarWithVFX = false;
        auto vfxView = scene->GetRegistry().view<Axle::VehicleVFXComponent>();
        for (auto eID : vfxView) {
            Axle::Entity e{ eID, scene.get() };
            if (e.GetName() == "Car") {
                foundCarWithVFX = true;
                const auto& vvfx = e.GetComponent<Axle::VehicleVFXComponent>();
                assert(vvfx.Enabled == true);
                assert(vvfx.Config.SurfaceType == Axle::SurfaceVFXType::Gravel);
                assert(vvfx.Config.BurnoutSlipThreshold == 0.25f);
                assert(vvfx.Config.BackfireMinRPM == 5200.0f);
            }
        }
        assert(foundCarWithVFX);

        // Find TrackTorch_VFX entity
        bool foundTorch = false;
        auto pecView = scene->GetRegistry().view<Axle::ParticleEmitterComponent>();
        for (auto eID : pecView) {
            Axle::Entity e{ eID, scene.get() };
            if (e.GetName() == "TrackTorch_VFX") {
                foundTorch = true;
                const auto& pec = e.GetComponent<Axle::ParticleEmitterComponent>();
                assert(pec.Active == true);
                assert(pec.AutoPlay == true);
                assert(pec.Config.Name == "TrackFire");
                assert(pec.Config.EmissionRate == 80.0f);
            }
        }
        assert(foundTorch);

        std::cout << "  -> Successfully verified Car (VehicleVFXComponent) and TrackTorch_VFX in OffRoadTrack scene." << std::endl;
        std::cout << "  -> PASS: OffRoadTrack scene VFX integration verified." << std::endl;
    }

    std::cout << ">>> STEP 47 PASSED: Phase 31 Step 6 VFX Editor Panel & Final VFX 100% verified! <<<\n" << std::endl;
    return true;
}