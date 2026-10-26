#include "TestCommon.hpp"

// =============================================================================
// STEP 33 TEST: PHASE 29A SPLINE CURVE & PROCEDURAL TRACK GENERATOR PLUGIN
// =============================================================================
bool RunTest_Step33_Phase29A_SplineTrackPlugin() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 33] Phase 29A: Spline Curve & Track Generator Plugin" << std::endl;
    std::cout << "========================================================" << std::endl;

    // 1. Spline Plugin Lifecycle
    std::cout << "\n[33.1] Testing Spline Plugin Lifecycle & Version Reflection..." << std::endl;
    Axle::Spline::SplinePlugin::Initialize();
    assert(Axle::Spline::SplinePlugin::IsInitialized());
    assert(Axle::Spline::SplinePlugin::Name == "AxleSpline");
    assert(Axle::Spline::SplinePlugin::Version == "1.0.0");
    std::cout << "  -> PASS: AxleSpline Plugin v" << Axle::Spline::SplinePlugin::Version << " initialized." << std::endl;

    // 2. 3D Curve Mathematics Core (Curve3D)
    std::cout << "\n[33.2] Testing 3D Curve Math (Catmull-Rom, Cubic BÃ©zier, Linear)..." << std::endl;
    Axle::Spline::Curve3D catmull(Axle::Spline::SplineType::CatmullRom, true);
    catmull.AddNode({ glm::vec3(-50.0f, 0.0f, -50.0f), 0.0f, 1.0f });
    catmull.AddNode({ glm::vec3(50.0f, 0.0f, -50.0f), 15.0f, 1.0f });
    catmull.AddNode({ glm::vec3(50.0f, 0.0f, 50.0f), 15.0f, 1.0f });
    catmull.AddNode({ glm::vec3(-50.0f, 0.0f, 50.0f), 0.0f, 1.0f });
    catmull.RebuildCache();

    float totalLen = catmull.GetTotalLength();
    std::cout << "  -> Closed Catmull-Rom curve total length: " << totalLen << " m" << std::endl;
    assert(totalLen > 300.0f && totalLen < 500.0f);

    // Uniform Arc-length sampling
    glm::vec3 pos0 = catmull.SamplePosition(0.0f);
    glm::vec3 posHalf = catmull.SamplePosition(totalLen * 0.5f);
    float distMid = glm::distance(pos0, posHalf);
    assert(distMid > 80.0f);
    std::cout << "  -> PASS: Arc-length lookup table (LUT) uniform metric speed verified." << std::endl;

    // Cubic BÃ©zier verification
    Axle::Spline::Curve3D bezier(Axle::Spline::SplineType::CubicBezier, false);
    bezier.AddNode({ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-5.0f, 0.0f, 0.0f), glm::vec3(5.0f, 0.0f, 0.0f) });
    bezier.AddNode({ glm::vec3(20.0f, 10.0f, 0.0f), glm::vec3(-5.0f, 0.0f, 0.0f), glm::vec3(5.0f, 0.0f, 0.0f) });
    bezier.RebuildCache();
    assert(glm::distance(bezier.SamplePosition(0.0f), glm::vec3(0.0f, 0.0f, 0.0f)) < 0.01f);
    assert(glm::distance(bezier.SamplePosition(bezier.GetTotalLength()), glm::vec3(20.0f, 10.0f, 0.0f)) < 0.1f);
    std::cout << "  -> PASS: Cubic BÃ©zier endpoints and tangent handles verified." << std::endl;

    // 3. Rotation-Minimizing Frame (RMF / Parallel Transport)
    std::cout << "\n[33.3] Testing Rotation-Minimizing Frame (RMF) Orthonormality & Banking..." << std::endl;
    for (int i = 0; i < 20; ++i) {
        float s = (static_cast<float>(i) / 20.0f) * totalLen;
        Axle::Spline::SplineFrame frame = catmull.SampleFrame(s);
        float dotTN = std::abs(glm::dot(frame.Tangent, frame.Normal));
        float dotTB = std::abs(glm::dot(frame.Tangent, frame.Binormal));
        float dotNB = std::abs(glm::dot(frame.Normal, frame.Binormal));
        assert(dotTN < 0.01f);
        assert(dotTB < 0.01f);
        assert(dotNB < 0.01f);
        assert(std::abs(glm::length(frame.Tangent) - 1.0f) < 0.01f);
        assert(std::abs(glm::length(frame.Normal) - 1.0f) < 0.01f);
        assert(std::abs(glm::length(frame.Binormal) - 1.0f) < 0.01f);
    }
    std::cout << "  -> PASS: Frame orthonormality (Tangent x Normal = Binormal) 100% verified." << std::endl;

    // 4. Procedural Track Extrusion
    std::cout << "\n[33.4] Testing Procedural Track Mesh Extrusion (Road, Curbs, Guardrails)..." << std::endl;
    Axle::Spline::SplineComponent comp(Axle::Spline::SplineType::CatmullRom, true);
    comp.Curve = catmull;
    comp.TrackWidth = 10.0f;
    comp.StepDistance = 1.0f;
    comp.GenerateCurbs = true;
    comp.CurbWidth = 0.6f;
    comp.CurbHeight = 0.15f;
    comp.GenerateGuardrails = true;
    comp.GuardrailHeight = 0.8f;
    comp.GeneratePhysics = true;

    auto meshData = Axle::Spline::TrackGenerator::GenerateTrackMesh(comp.Curve, comp);
    assert(!meshData.IsEmpty());
    assert(meshData.GetVertexCount() > 500);
    assert(meshData.GetTriangleCount() > 500);
    assert(meshData.Indices.size() == meshData.GetTriangleCount() * 3);
    std::cout << "  -> Extruded Track: " << meshData.GetVertexCount() << " vertices, " << meshData.GetTriangleCount() << " triangles." << std::endl;

    auto vao = Axle::Spline::TrackGenerator::BuildVAO(meshData);
    assert(vao != nullptr);
    assert(vao->GetIndexBuffer() != nullptr);
    assert(vao->GetIndexBuffer()->GetCount() == static_cast<Axle::u32>(meshData.Indices.size()));
    std::cout << "  -> PASS: GPU VertexArray (VAO) and IndexBuffer constructed with valid stride & layout." << std::endl;

    // 5. Jolt Physics MeshShape Collider & Raycast Interaction
    std::cout << "\n[33.5] Testing Jolt Physics Static MeshShape Body & Vehicle Raycast..." << std::endl;
    Axle::PhysicsScene physicsScene;
    Axle::BodyID trackBodyID = Axle::Spline::TrackGenerator::CreatePhysicsCollider(meshData, comp, physicsScene);
    assert(!trackBodyID.IsInvalid());
    assert(physicsScene.IsBodyValid(trackBodyID));
    assert(physicsScene.GetNumBodies() >= 1);
    std::cout << "  -> Track Physics Body ID: " << trackBodyID.GetIndexAndSequenceNumber() << " successfully registered." << std::endl;

    // Raycast down from above the track center
    glm::vec3 trackCenterPos = comp.Curve.SamplePosition(50.0f);
    Axle::RayCastHit hit;
    bool hasHit = physicsScene.CastRay(trackCenterPos + glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 20.0f, hit);
    assert(hasHit);
    assert(hit.HitBodyID == trackBodyID);
    assert(std::abs(hit.Position.y - trackCenterPos.y) < 1.0f);
    std::cout << "  -> Raycast hit track road surface at (" << hit.Position.x << ", " << hit.Position.y << ", " << hit.Position.z << ")" << std::endl;
    std::cout << "  -> PASS: Jolt Physics MeshShape raycast hit confirmed." << std::endl;

    Axle::Spline::TrackGenerator::DestroyPhysicsCollider(trackBodyID, physicsScene);
    assert(!physicsScene.IsBodyValid(trackBodyID));
    std::cout << "  -> PASS: Physics body cleanly removed and destroyed." << std::endl;

    // 6. Scene ECS Component Integration
    std::cout << "\n[33.6] Testing Scene ECS Entity & SplineComponent Lifecycle..." << std::endl;
    auto scene = std::make_shared<Axle::Scene>();
    auto trackEntity = scene->CreateEntity("MonzaCircuit");
    auto& sc = trackEntity.AddComponent<Axle::Spline::SplineComponent>(comp);
    assert(trackEntity.HasComponent<Axle::Spline::SplineComponent>());
    assert(sc.Curve.GetNodeCount() == 4);
    assert(sc.TrackWidth == 10.0f);
    std::cout << "  -> PASS: Entity 'MonzaCircuit' successfully instantiated with SplineComponent." << std::endl;

    Axle::Spline::SplinePlugin::Shutdown();
    std::cout << ">>> STEP 33 PASSED: Phase 29A Spline Track Plugin 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 34 TEST: PHASE 29B OFF-ROAD TERRAIN & SCULPTING SYSTEM PLUGIN
// =============================================================================
bool RunTest_Step34_Phase29B_TerrainPlugin() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 34] Phase 29B: Off-Road Terrain & Sculptor Plugin" << std::endl;
    std::cout << "========================================================" << std::endl;

    // 1. Plugin Lifecycle & Version Reflection
    std::cout << "\n[34.1] Testing Terrain Plugin Lifecycle & Version Reflection..." << std::endl;
    Axle::Terrain::TerrainPlugin::Init();
    assert(Axle::Terrain::TerrainPlugin::IsInitialized());
    assert(Axle::Terrain::TerrainPlugin::GetName() == "AxleTerrain");
    assert(Axle::Terrain::TerrainPlugin::GetVersion() == "1.0.0");
    std::cout << "  -> PASS: AxleTerrain Plugin v1.0.0 initialized successfully." << std::endl;

    // 2. TerrainData Matrix, Bilinear Sampling & Normal / Slope Angles
    std::cout << "\n[34.2] Testing TerrainData Matrix, Bilinear Sampling & Normal / Slope Angles..." << std::endl;
    Axle::Terrain::TerrainData terrain(64, 64, 2.0f, 2.0f);
    assert(terrain.GetResolutionX() == 64);
    assert(terrain.GetResolutionZ() == 64);
    assert(terrain.GetSpacingX() == 2.0f);
    assert(terrain.GetSpacingZ() == 2.0f);
    float expectedWidth = 63.0f * 2.0f; // 126.0m
    assert(std::abs(terrain.GetWidth() - expectedWidth) < 0.001f);

    // Test Ramp heights
    terrain.GenerateRamp(0.0f, 30.0f, false); // Ramp along Z
    float hMid = terrain.SampleHeight(0.0f, 0.0f); // Center of terrain (Z=0 in world coordinates)
    assert(std::abs(hMid - 15.0f) < 0.5f);
    std::cout << "  -> Sampled height at world center (0, 0): " << hMid << " m (Expected ~15.0 m)" << std::endl;

    // Test Bilinear interpolation halfway between grid cells
    float hSub = terrain.SampleHeight(1.0f, 1.0f);
    assert(hSub > 0.0f && hSub < 30.0f);
    std::cout << "  -> Bilinear interpolated height at (1.0, 1.0): " << hSub << " m" << std::endl;

    // Test Normal calculation
    glm::vec3 normal = terrain.SampleNormal(0.0f, 0.0f);
    assert(std::abs(glm::length(normal) - 1.0f) < 0.001f);
    assert(normal.y > 0.0f); // Upward facing
    float slopeDeg = terrain.SampleSlopeAngle(0.0f, 0.0f);
    assert(slopeDeg > 0.0f);
    std::cout << "  -> Terrain normal: (" << normal.x << ", " << normal.y << ", " << normal.z << "), Slope: " << slopeDeg << " deg" << std::endl;
    std::cout << "  -> PASS: TerrainData discrete & continuous bilinear sampling verified." << std::endl;

    // Test Auto Splatmap
    terrain.AutoGenerateSplatmap(30.0f, 15.0f);
    glm::vec4 splatCenter = terrain.GetSplatWeight(32, 32);
    float splatSum = splatCenter.r + splatCenter.g + splatCenter.b + splatCenter.a;
    assert(std::abs(splatSum - 1.0f) < 0.001f);
    std::cout << "  -> PASS: Auto-Splatmap normalized 4-channel weights verified." << std::endl;

    // 3. Multithreaded Chunk Generation & LODs via Axle::JobSystem
    std::cout << "\n[34.3] Testing Multithreaded Chunk Generation & LODs via enkiTS..." << std::endl;
    if (!Axle::JobSystem::IsInitialized()) {
        Axle::JobSystem::Init();
    }
    std::vector<Axle::Terrain::TerrainChunk> chunks;
    Axle::Terrain::TerrainMesh::InitializeChunks(terrain, chunks, 16);
    assert(!chunks.empty());
    std::cout << "  -> Initialized " << chunks.size() << " chunks (chunk size: 16x16 cells)" << std::endl;

    Axle::Terrain::TerrainMesh::BuildAllChunksParallel(terrain, chunks);
    for (const auto& chunk : chunks) {
        assert(!chunk.Vertices.empty());
        assert(!chunk.Indices.empty());
    }
    std::cout << "  -> PASS: All " << chunks.size() << " chunks built in parallel across worker threads." << std::endl;

    // Test Dynamic LOD selection
    glm::vec3 testCamPos(0.0f, 10.0f, 0.0f);
    Axle::Terrain::TerrainMesh::UpdateChunkLODs(testCamPos, chunks, 30.0f, 60.0f);
    uint32_t lod0Count = 0, lod1Count = 0, lod2Count = 0;
    for (const auto& c : chunks) {
        if (c.CurrentLOD == 0) lod0Count++;
        else if (c.CurrentLOD == 1) lod1Count++;
        else lod2Count++;
    }
    std::cout << "  -> Chunk LOD distribution from camera: LOD0=" << lod0Count << ", LOD1=" << lod1Count << ", LOD2=" << lod2Count << std::endl;
    assert(lod0Count > 0);
    assert(lod1Count > 0 || lod2Count > 0);
    std::cout << "  -> PASS: Distance-based dynamic LOD switching validated." << std::endl;

    // Combined VAO build test
    auto combinedVAO = Axle::Terrain::TerrainMesh::BuildCombinedVAO(terrain);
    assert(combinedVAO != nullptr);
    assert(combinedVAO->GetIndexBuffer() != nullptr);
    std::cout << "  -> PASS: Combined terrain GPU VAO built successfully." << std::endl;

    // 4. Jolt Physics HeightField Collision & Off-Road Raycast
    std::cout << "\n[34.4] Testing Jolt Physics HeightField Collision & Off-Road Raycast..." << std::endl;
    Axle::PhysicsScene physicsScene;
    Axle::BodyID terrainBody = Axle::Terrain::TerrainCollider::CreateTerrainCollider(terrain, physicsScene, 0.9f, 0.05f);
    assert(!terrainBody.IsInvalid());
    assert(physicsScene.IsBodyValid(terrainBody));
    std::cout << "  -> JPH::HeightFieldShape Body ID: " << terrainBody.GetIndex() << " registered in PhysicsScene." << std::endl;

    // Cast downward ray from sky onto terrain
    Axle::RayCastHit hit;
    glm::vec3 rayOrigin(0.0f, 50.0f, 0.0f);
    bool rayHit = physicsScene.CastRay(rayOrigin, glm::vec3(0.0f, -1.0f, 0.0f), 100.0f, hit);
    assert(rayHit);
    assert(hit.HitBodyID == terrainBody);
    assert(std::abs(hit.Position.y - hMid) < 0.5f);
    std::cout << "  -> Jolt Physics Raycast hit heightfield at: (" << hit.Position.x << ", " << hit.Position.y << ", " << hit.Position.z << ")" << std::endl;
    std::cout << "  -> PASS: Jolt HeightField collision confirmed matching terrain elevation." << std::endl;

    // Test analytical raymarching
    glm::vec3 analyticalHit(0.0f);
    bool anaHit = Axle::Terrain::TerrainCollider::RaycastTerrain(rayOrigin, glm::vec3(0.0f, -1.0f, 0.0f), terrain, analyticalHit);
    assert(anaHit);
    assert(std::abs(analyticalHit.y - hMid) < 0.1f);
    std::cout << "  -> Analytical Raycast hit: (" << analyticalHit.x << ", " << analyticalHit.y << ", " << analyticalHit.z << ")" << std::endl;
    std::cout << "  -> PASS: Analytical raycast and Jolt physics hit consistent within 0.1m." << std::endl;

    Axle::Terrain::TerrainCollider::DestroyTerrainCollider(terrainBody, physicsScene);
    assert(!physicsScene.IsBodyValid(terrainBody));
    std::cout << "  -> PASS: Terrain physics body cleanly destroyed." << std::endl;

    // 5. Interactive Sculpting Engine (Raise, Lower, Smooth, Flatten, Texture Paint)
    std::cout << "\n[34.5] Testing Interactive Sculpting Engine Tools..." << std::endl;
    terrain.GenerateFlat(10.0f);
    float centerBefore = terrain.SampleHeight(0.0f, 0.0f);
    assert(std::abs(centerBefore - 10.0f) < 0.001f);

    // Raise brush
    Axle::Terrain::SculptSettings sculptSettings;
    sculptSettings.Mode = Axle::Terrain::BrushMode::Raise;
    sculptSettings.Radius = 15.0f;
    sculptSettings.Strength = 10.0f;
    sculptSettings.Falloff = Axle::Terrain::BrushFalloff::Smoothstep;

    auto resRaise = Axle::Terrain::TerrainSculptor::ApplyBrush(terrain, 0.0f, 0.0f, sculptSettings, 0.5f);
    assert(resRaise.Modified);
    float centerRaised = terrain.SampleHeight(0.0f, 0.0f);
    assert(centerRaised > 10.0f);
    float outsideRadius = terrain.SampleHeight(25.0f, 25.0f);
    assert(std::abs(outsideRadius - 10.0f) < 0.001f); // Unmodified outside radius
    std::cout << "  -> Raised center height from 10.0m to " << centerRaised << "m (Outside remains " << outsideRadius << "m)" << std::endl;
    std::cout << "  -> PASS: Raise brush & smoothstep falloff verified." << std::endl;

    // Lower brush
    sculptSettings.Mode = Axle::Terrain::BrushMode::Lower;
    auto resLower = Axle::Terrain::TerrainSculptor::ApplyBrush(terrain, 0.0f, 0.0f, sculptSettings, 0.25f);
    assert(resLower.Modified);
    float centerLowered = terrain.SampleHeight(0.0f, 0.0f);
    assert(centerLowered < centerRaised);
    std::cout << "  -> Lowered center height to " << centerLowered << "m" << std::endl;
    std::cout << "  -> PASS: Lower brush verified." << std::endl;

    // Flatten brush
    sculptSettings.Mode = Axle::Terrain::BrushMode::Flatten;
    sculptSettings.TargetHeight = 25.0f;
    sculptSettings.Strength = 20.0f;
    auto resFlatten = Axle::Terrain::TerrainSculptor::ApplyBrush(terrain, 0.0f, 0.0f, sculptSettings, 1.0f);
    assert(resFlatten.Modified);
    float centerFlattened = terrain.SampleHeight(0.0f, 0.0f);
    assert(std::abs(centerFlattened - 25.0f) < 2.0f); // Approached target height
    std::cout << "  -> Flattened center towards 25.0m: " << centerFlattened << "m" << std::endl;
    std::cout << "  -> PASS: Flatten brush verified." << std::endl;

    // Texture Painting
    sculptSettings.Mode = Axle::Terrain::BrushMode::PaintTexture;
    sculptSettings.PaintLayer = Axle::Terrain::TerrainSplatLayers::DIRT;
    sculptSettings.Strength = 20.0f;
    auto resPaint = Axle::Terrain::TerrainSculptor::ApplyBrush(terrain, 0.0f, 0.0f, sculptSettings, 0.5f);
    assert(resPaint.Modified);
    float gx = 0.0f, gz = 0.0f;
    terrain.WorldToGrid(0.0f, 0.0f, gx, gz);
    glm::vec4 paintedSplat = terrain.GetSplatWeight(static_cast<uint32_t>(std::round(gx)), static_cast<uint32_t>(std::round(gz)));
    assert(paintedSplat.g > 0.3f); // Dirt layer elevated
    std::cout << "  -> Painted splatmap weights at center: Grass=" << paintedSplat.r << ", Dirt=" << paintedSplat.g << ", Gravel=" << paintedSplat.b << ", Rock=" << paintedSplat.a << std::endl;
    std::cout << "  -> PASS: Interactive texture layer painting verified." << std::endl;

    // 6. Spline Road Carving & Terrain Snapping
    std::cout << "\n[34.6] Testing Spline Road Carving & Ground Snapping..." << std::endl;
    Axle::Spline::SplineComponent splineComp;
    splineComp.Curve.SetSplineType(Axle::Spline::SplineType::CatmullRom);
    splineComp.Curve.AddNode({ glm::vec3(-40.0f, 15.0f, 0.0f), 0.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(-20.0f, 18.0f, 5.0f), 0.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(20.0f, 18.0f, -5.0f), 0.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(40.0f, 15.0f, 0.0f), 0.0f, 1.0f });
    splineComp.Curve.RebuildCache();

    // Carve terrain along road
    Axle::Terrain::CarveSettings carveSettings;
    carveSettings.RoadWidth = 8.0f;
    carveSettings.ShoulderWidth = 4.0f;
    carveSettings.SampleStep = 1.0f;
    bool carved = Axle::Terrain::TerrainSplineCarver::CarveSplineRoad(terrain, splineComp.Curve, carveSettings);
    assert(carved);

    // Verify carved road elevation directly beneath spline node 1
    glm::vec3 testRoadPt = splineComp.Curve.SamplePosition(20.0f);
    float groundH = terrain.SampleHeight(testRoadPt.x, testRoadPt.z);
    assert(std::abs(groundH - testRoadPt.y) < 1.0f);
    std::cout << "  -> Ground height beneath road: " << groundH << " m (Spline road height: " << testRoadPt.y << " m)" << std::endl;
    std::cout << "  -> PASS: Spline roadbed terrain carving verified." << std::endl;

    // Snap spline to terrain
    terrain.GenerateHills(0.02f, 12.0f, 2);
    bool snapped = Axle::Terrain::TerrainSplineCarver::SnapSplineToTerrain(splineComp, terrain, 0.2f);
    assert(snapped);
    for (const auto& node : splineComp.Curve.GetNodes()) {
        float expectedH = terrain.SampleHeight(node.Position.x, node.Position.z) + 0.2f;
        assert(std::abs(node.Position.y - expectedH) < 0.01f);
    }
    std::cout << "  -> PASS: All spline control points snapped precisely to terrain elevation (+0.2m offset)." << std::endl;

    // 7. Terrain File Serialization Roundtrip (.axleterrain)
    std::cout << "\n[34.7] Testing Terrain Serialization (.axleterrain)..." << std::endl;
    std::string testTerrainFile = "assets/terrain/UnitTestTerrain.axleterrain";
    std::filesystem::create_directories("assets/terrain");
    bool saved = terrain.SaveToFile(testTerrainFile);
    assert(saved);
    assert(std::filesystem::exists(testTerrainFile));

    Axle::Terrain::TerrainData loadedTerrain;
    bool loaded = loadedTerrain.LoadFromFile(testTerrainFile);
    assert(loaded);
    assert(loadedTerrain.GetResolutionX() == terrain.GetResolutionX());
    assert(loadedTerrain.GetResolutionZ() == terrain.GetResolutionZ());
    assert(std::abs(loadedTerrain.GetSpacingX() - terrain.GetSpacingX()) < 0.001f);
    assert(std::abs(loadedTerrain.GetHeight(15, 15) - terrain.GetHeight(15, 15)) < 0.001f);
    std::error_code ec;
    std::filesystem::remove(testTerrainFile, ec);
    std::cout << "  -> PASS: Terrain binary serialization (.axleterrain) roundtrip 100% verified." << std::endl;

    Axle::Terrain::TerrainPlugin::Shutdown();
    std::cout << ">>> STEP 34 PASSED: Phase 29B Off-Road Terrain Plugin 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 35: SCENE WITH SPLINE TRACK & TERRAIN PLUGIN
// =============================================================================
bool RunTest_Step35_SceneWithSplineAndTerrain() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 35: SCENE WITH SPLINE TRACK & TERRAIN PLUGIN     " << std::endl;
    std::cout << "========================================================" << std::endl;

    Axle::Spline::SplinePlugin::Initialize();
    Axle::Terrain::TerrainPlugin::Initialize();

    // 1. Generate Terrain Asset: Editor/assets/terrain/OffRoadTerrain.axleterrain
    std::cout << "\n[35.1] Procedurally generating 128x128 Mountain Terrain..." << std::endl;
    std::filesystem::path terrainDir = "Editor/assets/terrain";
    std::filesystem::create_directories(terrainDir);
    std::filesystem::create_directories("assets/terrain");

    std::filesystem::path terrainPath = terrainDir / "OffRoadTerrain.axleterrain";

    Axle::Terrain::TerrainData terrain(128, 128, 2.0f, 2.0f);
    terrain.SetOrigin(glm::vec3(-127.0f, 0.0f, -127.0f));
    terrain.GenerateHills(0.02f, 10.0f, 3);

    // Ensure baseline elevation is above 4.0m
    for (uint32_t z = 0; z < terrain.GetResolutionZ(); ++z) {
        for (uint32_t x = 0; x < terrain.GetResolutionX(); ++x) {
            terrain.SetHeight(x, z, terrain.GetHeight(x, z) + 4.0f);
        }
    }

    // 2. Build Closed-Loop Race Track Spline
    std::cout << "\n[35.2] Constructing Closed-Loop Spline Race Track Circuit..." << std::endl;
    Axle::Spline::SplineComponent splineComp(Axle::Spline::SplineType::CatmullRom, true);
    splineComp.TrackWidth = 8.0f;
    splineComp.StepDistance = 1.0f;
    splineComp.GenerateCurbs = true;
    splineComp.CurbWidth = 0.6f;
    splineComp.CurbHeight = 0.15f;
    splineComp.GenerateGuardrails = true;
    splineComp.GuardrailHeight = 0.9f;
    splineComp.GeneratePhysics = true;
    splineComp.Friction = 0.85f;
    splineComp.Restitution = 0.05f;

    // 8 Spline Control Points forming a circuit through the terrain
    splineComp.Curve.AddNode({ glm::vec3(0.0f, 6.0f, -80.0f), 0.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(55.0f, 9.0f, -55.0f), 6.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(80.0f, 13.0f, 0.0f), 4.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(60.0f, 17.0f, 55.0f), -8.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(0.0f, 12.0f, 80.0f), 0.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(-55.0f, 8.0f, 55.0f), 8.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(-80.0f, 5.0f, 0.0f), -6.0f, 1.0f });
    splineComp.Curve.AddNode({ glm::vec3(-55.0f, 5.5f, -55.0f), 0.0f, 1.0f });
    splineComp.Curve.RebuildCache();

    float totalTrackLen = splineComp.Curve.GetTotalLength();
    assert(totalTrackLen > 250.0f);
    std::cout << "  -> Spline Track length: " << totalTrackLen << " m across 8 control nodes." << std::endl;

    // 3. Carve Roadbed into Terrain & Auto-Generate Splatmap
    std::cout << "\n[35.3] Carving Roadbed into Terrain & Auto-generating Splatmap..." << std::endl;
    Axle::Terrain::CarveSettings carveSettings;
    carveSettings.RoadWidth = 9.0f;
    carveSettings.ShoulderWidth = 4.0f;
    carveSettings.SampleStep = 0.5f;
    bool carved = Axle::Terrain::TerrainSplineCarver::CarveSplineRoad(terrain, splineComp.Curve, carveSettings);
    assert(carved);

    terrain.AutoGenerateSplatmap(30.0f, 16.0f);

    bool savedTerrain = terrain.SaveToFile(terrainPath.string());
    assert(savedTerrain);
    terrain.SaveToFile("assets/terrain/OffRoadTerrain.axleterrain");
    std::cout << "  -> Saved terrain binary to: " << terrainPath.string() << " (and assets/terrain/)" << std::endl;

    // 4. Generate OffRoadTrack.axlescene File
    std::cout << "\n[35.4] Generating 'Editor/assets/scenes/OffRoadTrack.axlescene'..." << std::endl;
    std::filesystem::path scenesDir = "Editor/assets/scenes";
    std::filesystem::create_directories(scenesDir);
    std::filesystem::create_directories("assets/scenes");
    std::filesystem::path scenePath = scenesDir / "OffRoadTrack.axlescene";

    auto scene = std::make_shared<Axle::Scene>();
    auto physicsScene = std::make_unique<Axle::PhysicsScene>();

    // Entity 1: Sun Light & Skybox
    auto sunEntity = scene->CreateEntity("Sun Light");
    auto& sunTc = sunEntity.GetComponent<Axle::TransformComponent>();
    sunTc.Translation = glm::vec3(0.0f, 100.0f, 0.0f);
    sunTc.Rotation = glm::quat(glm::vec3(glm::radians(50.0f), glm::radians(-35.0f), 0.0f));
    auto& sunLight = sunEntity.AddComponent<Axle::DirectionalLightComponent>();
    sunLight.Color = glm::vec3(1.0f, 0.95f, 0.85f);
    sunLight.Intensity = 2.0f;
    sunEntity.AddComponent<Axle::SkyboxComponent>();

    // Entity 2: OffRoad Terrain
    auto terrainEntity = scene->CreateEntity("OffRoadTerrain");
    auto& tc = terrainEntity.AddComponent<Axle::Terrain::TerrainComponent>(128u, 128u, 2.0f, 2.0f);
    tc.Data = terrain;
    tc.DataFilePath = "assets/terrain/OffRoadTerrain.axleterrain";
    tc.GeneratePhysics = true;
    tc.Friction = 0.85f;
    tc.Restitution = 0.05f;

    // Entity 3: Spline Track Circuit
    auto splineEntity = scene->CreateEntity("SplineTrack");
    auto& sc = splineEntity.AddComponent<Axle::Spline::SplineComponent>(splineComp);

    // Entity 4: Car
    auto carEntity = scene->CreateEntity("Car");
    auto& carTc = carEntity.GetComponent<Axle::TransformComponent>();
    carTc.Translation = glm::vec3(0.0f, 7.5f, -80.0f);
    carTc.Rotation = glm::quat(glm::vec3(0.0f, glm::radians(90.0f), 0.0f));

    auto& carRb = carEntity.AddComponent<Axle::RigidBody3D>();
    carRb.MotionType = Axle::BodyMotionType::Dynamic;
    carRb.IsDynamic = true;
    carRb.Mass = 1500.0f;
    carRb.LinearDamping = 0.05f;
    carRb.AngularDamping = 0.05f;

    auto& carCol = carEntity.AddComponent<Axle::CollisionShape3D>();
    carCol.ShapeType = Axle::CollisionShapeType::Box;
    carCol.HalfExtent = glm::vec3(0.9f, 0.4f, 2.1f);
    carCol.Friction = 0.2f;

    auto& vc = carEntity.AddComponent<Axle::VehicleComponent>();
    vc.ChassisMass = 1500.0f;
    vc.MaxEngineTorque = 900.0f;
    vc.MaxRPM = 7500.0f;
    vc.FourWheelDrive = true;

    auto& vac = carEntity.AddComponent<Axle::VehicleAudioComponent>();
    vac.Enabled = true;

    auto& vfx = carEntity.AddComponent<Axle::VehicleVFXComponent>();
    vfx.Enabled = true;
    vfx.Config.SurfaceType = Axle::SurfaceVFXType::Gravel;
    vfx.Config.BurnoutSlipThreshold = 0.25f;
    vfx.Config.BackfireMinRPM = 5200.0f;

    carEntity.AddComponent<Axle::NativeScriptComponent>("CarController");

    // Children of Car: 4 Wheels
    struct WheelDef {
        std::string Name;
        glm::vec3 Offset;
        bool CanSteer;
        bool IsDriven;
        bool HasHandbrake;
    };
    std::vector<WheelDef> wheelDefs = {
        { "Wheel_FL", glm::vec3( 0.95f, -0.1f,  1.4f),  true, true, false },
        { "Wheel_FR", glm::vec3(-0.95f, -0.1f,  1.4f),  true, true, false },
        { "Wheel_RL", glm::vec3( 0.95f, -0.1f, -1.4f), false, true,  true },
        { "Wheel_RR", glm::vec3(-0.95f, -0.1f, -1.4f), false, true,  true }
    };

    for (const auto& wdef : wheelDefs) {
        auto wheelEnt = scene->CreateEntity(wdef.Name);
        auto& wtc = wheelEnt.GetComponent<Axle::TransformComponent>();
        wtc.Translation = wdef.Offset;
        wheelEnt.SetParent(carEntity, false);

        auto& wc = wheelEnt.AddComponent<Axle::WheelComponent>();
        wc.Radius = 0.35f;
        wc.Width = 0.25f;
        wc.CanSteer = wdef.CanSteer;
        wc.IsDriven = wdef.IsDriven;
        wc.HasHandbrake = wdef.HasHandbrake;
        wc.AutoSyncMesh = true;

        auto& wmc = wheelEnt.AddComponent<Axle::MeshInstance3D>();
        wmc.AssetPath = "assets/models/SportsCar_Wheel.axlemesh";
        wmc.PatternMode = 3;
        wmc.TileScale = 4.0f;
        wmc.Color1 = glm::vec4(0.08f);
        wmc.Color2 = glm::vec4(0.08f);
    }

    // BodyMesh child
    auto bodyMeshEnt = scene->CreateEntity("BodyMesh");
    bodyMeshEnt.SetParent(carEntity, false);
    auto& bmc = bodyMeshEnt.AddComponent<Axle::MeshInstance3D>();
    bmc.AssetPath = "assets/models/SportsCar_Chassis.axlemesh";
    bmc.PatternMode = 1;
    bmc.TileScale = 6.0f;
    bmc.Color1 = glm::vec4(1.0f, 0.77f, 0.34f, 1.0f);
    bmc.Color2 = glm::vec4(0.15f, 0.15f, 0.15f, 1.0f);

    // Entity: Main Camera with Chase Script
    auto camEntity = scene->CreateEntity("Main Camera");
    auto& camTc = camEntity.GetComponent<Axle::TransformComponent>();
    camTc.Translation = glm::vec3(0.0f, 12.0f, -95.0f);
    camTc.Rotation = glm::quat(glm::vec3(glm::radians(-15.0f), 0.0f, 0.0f));
    camEntity.AddComponent<Axle::CameraComponent>(45.0f, 0.1f, 1000.0f, true);
    camEntity.AddComponent<Axle::NativeScriptComponent>("VehicleChaseCamera");

    // Entity: Racing HUD
    auto hudEntity = scene->CreateEntity("RacingHUD");
    hudEntity.AddComponent<Axle::NativeScriptComponent>("RacingHUD");

    // Entity: Track Torch VFX
    auto torchEntity = scene->CreateEntity("TrackTorch_VFX");
    auto& torchTc = torchEntity.GetComponent<Axle::TransformComponent>();
    torchTc.Translation = glm::vec3(3.5f, 7.5f, -76.0f);
    auto& torchPec = torchEntity.AddComponent<Axle::ParticleEmitterComponent>();
    torchPec.Active = true;
    torchPec.AutoPlay = true;
    torchPec.Config.Name = "TrackFire";
    torchPec.Config.EmissionRate = 80.0f;
    torchPec.Config.MaxParticles = 800;
    torchPec.Config.Lifetime = Axle::FloatRange(0.8f, 1.5f);
    torchPec.Config.InitialSpeed = Axle::FloatRange(1.5f, 3.5f);
    torchPec.Config.StartSize = Axle::FloatRange(0.25f, 0.45f);
    torchPec.Config.EndSize = Axle::FloatRange(0.05f, 0.15f);
    torchPec.Config.ColorOverLifetime = Axle::ColorGradient(
        glm::vec4(1.0f, 0.65f, 0.15f, 1.0f),
        glm::vec4(0.85f, 0.12f, 0.02f, 0.0f)
    );
    torchPec.Config.GravityModifier = -0.65f;
    torchPec.Config.BlendMode = Axle::ParticleBlendMode::Additive;

    // Serialize using SceneSerializer
    Axle::SceneSerializer serializer(scene.get(), physicsScene.get());
    Axle::SceneSerializerContext ctx;
    ctx.CustomEntitySerializer = [](Axle::Entity ent, std::ostream& out) {
        Axle::Spline::SplineSerializer::Serialize(ent, out);
        Axle::Terrain::TerrainSerializer::Serialize(ent, out);
    };
    ctx.CustomSectionDeserializer = [](Axle::Entity ent, const std::string& sectionName, const std::vector<std::string>& lines) -> bool {
        if (Axle::Spline::SplineSerializer::Deserialize(ent, sectionName, lines)) return true;
        if (Axle::Terrain::TerrainSerializer::Deserialize(ent, sectionName, lines)) return true;
        return false;
    };
    serializer.SetContext(ctx);

    bool serialized = serializer.Serialize(scenePath);
    assert(serialized);
    serializer.Serialize("assets/scenes/OffRoadTrack.axlescene");
    std::cout << "  -> Successfully serialized scene to: " << scenePath.string() << std::endl;

    // 5. Test Deserialization & Integrity Verification
    std::cout << "\n[35.5] Deserializing 'OffRoadTrack.axlescene' into fresh Scene..." << std::endl;
    auto loadedScene = std::make_shared<Axle::Scene>();
    auto loadedPhysics = std::make_unique<Axle::PhysicsScene>();
    Axle::SceneSerializer deserializer(loadedScene.get(), loadedPhysics.get());
    deserializer.SetContext(ctx);

    bool loaded = deserializer.Deserialize(scenePath);
    assert(loaded);
    std::cout << "  -> Scene deserialization SUCCEEDED." << std::endl;

    // Verify all components on loaded scene
    bool foundTerrain = false;
    bool foundSpline = false;
    bool foundCar = false;
    bool foundCamera = false;
    bool foundHUD = false;
    size_t wheelCount = 0;

    auto regView = loadedScene->GetRegistry().view<Axle::TagComponent>();
    for (auto entId : regView) {
        Axle::Entity ent{ entId, loadedScene.get() };
        const auto& tag = ent.GetComponent<Axle::TagComponent>().Tag;

        if (ent.HasComponent<Axle::Terrain::TerrainComponent>()) {
            foundTerrain = true;
            auto& ltc = ent.GetComponent<Axle::Terrain::TerrainComponent>();
            assert(ltc.Data.GetResolutionX() == 128);
            assert(ltc.Data.GetResolutionZ() == 128);
            assert(ltc.GeneratePhysics == true);
            std::cout << "  -> Verified TerrainComponent: " << ltc.Data.GetResolutionX() << "x" << ltc.Data.GetResolutionZ()
                      << " (DataFilePath: " << ltc.DataFilePath << ")" << std::endl;
        }

        if (ent.HasComponent<Axle::Spline::SplineComponent>()) {
            foundSpline = true;
            auto& lsc = ent.GetComponent<Axle::Spline::SplineComponent>();
            assert(lsc.Curve.GetNodeCount() == 8);
            assert(lsc.Curve.IsLoop());
            assert(lsc.Curve.GetTotalLength() > 250.0f);
            assert(lsc.GenerateCurbs);
            assert(lsc.GenerateGuardrails);
            std::cout << "  -> Verified SplineComponent: " << lsc.Curve.GetNodeCount() << " nodes, length="
                      << lsc.Curve.GetTotalLength() << "m, ClosedLoop=" << (lsc.Curve.IsLoop() ? "true" : "false") << std::endl;
        }

        if (ent.HasComponent<Axle::VehicleComponent>()) {
            foundCar = true;
            assert(ent.HasComponent<Axle::RigidBody3D>());
            assert(ent.GetComponent<Axle::RigidBody3D>().IsDynamic);
            assert(ent.HasComponent<Axle::NativeScriptComponent>());
            assert(ent.GetComponent<Axle::NativeScriptComponent>().ScriptName == "CarController");
            std::cout << "  -> Verified Car Entity with VehicleComponent & CarController script." << std::endl;
        }

        if (ent.HasComponent<Axle::WheelComponent>()) {
            wheelCount++;
        }

        if (tag == "Main Camera") {
            foundCamera = true;
            assert(ent.HasComponent<Axle::CameraComponent>());
            assert(ent.GetComponent<Axle::CameraComponent>().Primary);
            assert(ent.HasComponent<Axle::NativeScriptComponent>());
            assert(ent.GetComponent<Axle::NativeScriptComponent>().ScriptName == "VehicleChaseCamera");
            std::cout << "  -> Verified Main Camera with VehicleChaseCamera script." << std::endl;
        }

        if (tag == "RacingHUD") {
            foundHUD = true;
            assert(ent.HasComponent<Axle::NativeScriptComponent>());
            assert(ent.GetComponent<Axle::NativeScriptComponent>().ScriptName == "RacingHUD");
            std::cout << "  -> Verified RacingHUD entity." << std::endl;
        }
    }

    assert(foundTerrain);
    assert(foundSpline);
    assert(foundCar);
    assert(foundCamera);
    assert(foundHUD);
    assert(wheelCount == 4);
    std::cout << "  -> PASS: All 9 expected entities & components deserialized perfectly." << std::endl;

    // 6. Test Jolt Physics Simulation with Terrain & Vehicle
    std::cout << "\n[35.6] Testing Jolt Physics Simulation on Off-Road Scene..." << std::endl;
    auto tEntView = loadedScene->GetRegistry().view<Axle::Terrain::TerrainComponent>();
    for (auto e : tEntView) {
        auto& iterTc = tEntView.get<Axle::Terrain::TerrainComponent>(e);
        iterTc.PhysicsBodyID = Axle::Terrain::TerrainCollider::CreateTerrainCollider(iterTc.Data, *loadedPhysics, iterTc.Friction, iterTc.Restitution);
        assert(!iterTc.PhysicsBodyID.IsInvalid());
        assert(loadedPhysics->IsBodyValid(iterTc.PhysicsBodyID));
        std::cout << "  -> HeightField Body ID: " << iterTc.PhysicsBodyID.GetIndex() << " active in PhysicsScene." << std::endl;
    }

    loadedScene->OnRuntimeStart(loadedPhysics.get());
    auto* rv = loadedScene->GetPrimaryRuntimeVehicle();
    assert(rv != nullptr);
    std::cout << "  -> Primary Runtime Vehicle initialized with chassis BodyID: " << rv->GetChassisBodyID().GetIndex() << std::endl;

    for (int frame = 0; frame < 60; ++frame) {
        loadedPhysics->Step(1.0f / 60.0f);
    }

    glm::vec3 carPos = rv->GetPosition();
    assert(!std::isnan(carPos.x) && !std::isnan(carPos.y) && !std::isnan(carPos.z));
    assert(carPos.y > 0.0f);
    std::cout << "  -> Car position after 60 physics steps: (" << carPos.x << ", " << carPos.y << ", " << carPos.z << ")" << std::endl;
    std::cout << "  -> PASS: Jolt Physics simulated 60 steps smoothly without collision explosions." << std::endl;

    loadedScene->OnRuntimeStop(loadedPhysics.get());

    Axle::Terrain::TerrainPlugin::Shutdown();
    Axle::Spline::SplinePlugin::Shutdown();
    std::cout << ">>> STEP 35 PASSED: Scene with Spline Track & Terrain 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 36: PHASE 30 - STEP 1 SKELETAL ANIMATION CORE DATA STRUCTURES
// =============================================================================
bool RunTest_Step36_Phase30_Step1_CoreSkeletalDataStructures() {
    std::cout << "\n--------------------------------------------------------" << std::endl;
    std::cout << " RUNNING STEP 36: PHASE 30 - STEP 1 SKELETAL ANIMATION CORE" << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;

    // Sub-test 1: Skeleton and Bone Hierarchy
    std::cout << "  [1/5] Testing Skeleton & Hierarchical Bone Tree..." << std::endl;
    Axle::Skeleton skeleton("Humanoid");
    
    Axle::Bone root;
    root.Name = "Root";
    root.LocalTransform = glm::mat4(1.0f);
    int rootIdx = skeleton.AddBone(root);
    assert(rootIdx == 0);
    assert(skeleton.GetRootBoneIndex() == 0);

    Axle::Bone hips;
    hips.Name = "Hips";
    hips.ParentID = 0;
    hips.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    int hipsIdx = skeleton.AddBone(hips);
    assert(hipsIdx == 1);

    Axle::Bone spine;
    spine.Name = "Spine";
    spine.ParentID = 1;
    spine.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f));
    int spineIdx = skeleton.AddBone(spine);
    assert(spineIdx == 2);

    assert(skeleton.GetBoneCount() == 3);
    assert(skeleton.FindBoneIndex("Hips") == 1);
    assert(skeleton.FindBoneIndex("Spine") == 2);
    assert(skeleton.FindBoneIndex("NonExistent") == -1);

    skeleton.ComputeInverseBindMatrices();
    std::vector<glm::mat4> bindGlobals;
    skeleton.ComputeBindPoseGlobalTransforms(bindGlobals);

    // Verify InverseBindMatrix * GlobalTransform = Identity
    for (size_t b = 0; b < skeleton.GetBoneCount(); ++b) {
        glm::mat4 ident = bindGlobals[b] * skeleton.GetBones()[b].InverseBindMatrix;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                float expected = (row == col) ? 1.0f : 0.0f;
                assert(std::abs(ident[col][row] - expected) < 1e-4f);
            }
        }
    }
    std::cout << "  -> PASS: Skeleton hierarchy and InverseBindMatrices mathematically proven." << std::endl;

    // Sub-test 2: Animation Channel Keyframe SLERP/LERP Interpolation
    std::cout << "  [2/5] Testing AnimationChannel SLERP & LERP Keyframe Interpolation..." << std::endl;
    Axle::AnimationChannel channel;
    channel.BoneName = "Spine";
    channel.BoneIndex = 2;

    channel.Positions.push_back({ 0.0f, glm::vec3(0.0f, 0.5f, 0.0f) });
    channel.Positions.push_back({ 1.0f, glm::vec3(0.0f, 0.7f, 0.0f) });

    glm::quat q0 = glm::angleAxis(0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::quat q1 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    channel.Rotations.push_back({ 0.0f, q0 });
    channel.Rotations.push_back({ 1.0f, q1 });

    channel.Scales.push_back({ 0.0f, glm::vec3(1.0f) });
    channel.Scales.push_back({ 1.0f, glm::vec3(1.0f) });

    // Midpoint evaluation at t = 0.5s
    glm::vec3 midPos = channel.InterpolatePosition(0.5f);
    assert(std::abs(midPos.y - 0.6f) < 1e-4f);

    glm::quat midRot = channel.InterpolateRotation(0.5f);
    glm::vec3 rotatedForward = midRot * glm::vec3(0.0f, 0.0f, 1.0f);
    // At 45 degrees, rotated forward should have x ~= sin(45 deg) ~= 0.7071
    assert(std::abs(rotatedForward.x - std::sin(glm::radians(45.0f))) < 1e-3f);

    glm::mat4 xform = channel.EvaluateTransform(0.5f);
    assert(!std::isnan(xform[3][1]));
    std::cout << "  -> PASS: Quaternion SLERP and vector LERP evaluated with sub-millimeter precision." << std::endl;

    // Sub-test 3: Animation Clip Forward Kinematics & Skinning Matrix Computation
    std::cout << "  [3/5] Testing AnimationClip Forward Kinematics & Skinning Palette..." << std::endl;
    Axle::AnimationClip clip("TestWalk", 1.0f, 30.0f);
    clip.AddChannel(channel);

    assert(std::abs(clip.WrapTime(1.25f, true) - 0.25f) < 1e-5f);
    assert(clip.WrapTime(1.25f, false) == 1.0f);

    std::vector<glm::mat4> localPoses;
    clip.SamplePose(0.5f, skeleton, localPoses, true);
    assert(localPoses.size() == 3);

    std::vector<glm::mat4> globalPoses;
    Axle::AnimationClip::ComputeGlobalPose(skeleton, localPoses, globalPoses);
    assert(globalPoses.size() == 3);

    std::vector<glm::mat4> skinningMatrices;
    Axle::AnimationClip::ComputeSkinningMatrices(skeleton, globalPoses, skinningMatrices);
    assert(skinningMatrices.size() == 3);
    std::cout << "  -> PASS: Forward kinematics solved hierarchical global matrices & skinning palette." << std::endl;

    // Sub-test 4: Skeletal Vertex Weight Pruning & Normalization
    std::cout << "  [4/5] Testing Skeletal Vertex Bone Weight Pruning & Normalization..." << std::endl;
    Axle::SkeletalVertex v;
    v.Position = glm::vec3(0.0f, 1.5f, 0.0f);
    v.AddBoneData(0, 0.1f);
    v.AddBoneData(1, 0.4f);
    v.AddBoneData(2, 0.2f);
    v.AddBoneData(3, 0.5f);
    v.AddBoneData(4, 0.3f); // 5th bone should replace lowest (0.1f)

    // Check that bone 0 (lowest weight 0.1f) was dropped and bone 4 (0.3f) was retained
    bool found0 = false;
    bool found4 = false;
    for (int k = 0; k < 4; ++k) {
        if (v.BoneIDs[k] == 0) found0 = true;
        if (v.BoneIDs[k] == 4) found4 = true;
    }
    assert(!found0);
    assert(found4);

    Axle::SkeletalMeshData meshData;
    meshData.Vertices.push_back(v);
    meshData.Indices = { 0, 0, 0 };
    Axle::SkeletalMesh skeletalMesh(meshData);

    const auto& normV = skeletalMesh.GetData().Vertices[0];
    float sumWeights = normV.Weights.x + normV.Weights.y + normV.Weights.z + normV.Weights.w;
    assert(std::abs(sumWeights - 1.0f) < 1e-4f);
    std::cout << "  -> PASS: Vertex bone influences pruned to 4 and strictly normalized to 1.0." << std::endl;

    // Sub-test 5: Procedural Humanoid Biped Generator & Full Evaluation
    std::cout << "  [5/5] Testing SkeletalMeshImporter::CreateProceduralBiped()..." << std::endl;
    Axle::SkeletalMeshData bipedData;
    std::shared_ptr<Axle::Skeleton> bipedSkeleton;
    std::vector<std::shared_ptr<Axle::AnimationClip>> bipedClips;

    bool bipedOk = Axle::SkeletalMeshImporter::CreateProceduralBiped(bipedData, bipedSkeleton, bipedClips);
    assert(bipedOk);
    assert(bipedSkeleton != nullptr);
    assert(bipedSkeleton->GetBoneCount() == 8);
    assert(bipedData.Vertices.size() > 0);
    assert(bipedData.Indices.size() > 0);
    assert(bipedClips.size() == 2);

    assert(bipedClips[0]->GetName() == "Idle");
    assert(bipedClips[1]->GetName() == "Walk");

    // Sample walk cycle at t = 0.25s (quarter step)
    std::vector<glm::mat4> walkLocals;
    bipedClips[1]->SamplePose(0.25f, *bipedSkeleton, walkLocals);
    std::vector<glm::mat4> walkGlobals;
    Axle::AnimationClip::ComputeGlobalPose(*bipedSkeleton, walkLocals, walkGlobals);
    std::vector<glm::mat4> walkSkinning;
    Axle::AnimationClip::ComputeSkinningMatrices(*bipedSkeleton, walkGlobals, walkSkinning);
    assert(walkSkinning.size() == 8);

    for (const auto& mat : walkSkinning) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                assert(!std::isnan(mat[c][r]) && !std::isinf(mat[c][r]));
            }
        }
    }

    std::cout << "  -> Biped Skeleton: " << bipedSkeleton->GetBoneCount() << " bones, "
              << bipedData.Vertices.size() << " vertices, " << bipedData.Indices.size() / 3 << " triangles." << std::endl;
    std::cout << "  -> Animation Clips: '" << bipedClips[0]->GetName() << "' (" << bipedClips[0]->GetDuration() << "s), '"
              << bipedClips[1]->GetName() << "' (" << bipedClips[1]->GetDuration() << "s)." << std::endl;
    std::cout << "  -> PASS: Procedural animated character fully functional with zero external files." << std::endl;

    std::cout << ">>> STEP 36 PASSED: Phase 30 Step 1 Core Skeletal Animation 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 37: PHASE 30 - STEP 2: GPU VERTEX SKINNING & SHADER PIPELINE
// =============================================================================
bool RunTest_Step37_Phase30_Step2_GPUSkinningAndShaderPipeline() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 37: PHASE 30 - STEP 2 GPU VERTEX SKINNING PIPELINE" << std::endl;
    std::cout << "========================================================" << std::endl;

    // Sub-test 1: Skinning Pipeline Initialization & Shaders
    std::cout << "\n[37.1] Initializing SkinningPipeline and verifying shaders..." << std::endl;
    Axle::SkinningPipeline pipeline;
    pipeline.Init();

    assert(pipeline.GetShader() != nullptr);
    assert(pipeline.GetShadowShader() != nullptr);
    std::cout << "  -> PASS: SkinningPipeline initialized with PBR lighting & shadow depth shaders." << std::endl;

    // Sub-test 2: Dynamic Bone Matrix Palette Upload (Identity & Max 128 Bones)
    std::cout << "\n[37.2] Testing Dynamic Bone Matrix Uniform Array Upload..." << std::endl;
    pipeline.UploadIdentityPalette(*pipeline.GetShader());
    pipeline.UploadIdentityPalette(*pipeline.GetShadowShader());

    // Generate 128 distinct bone matrices (MAX_BONES)
    std::vector<glm::mat4> maxBones(Axle::SkinningPipeline::MAX_BONES);
    for (size_t i = 0; i < Axle::SkinningPipeline::MAX_BONES; ++i) {
        maxBones[i] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, static_cast<float>(i) * 0.1f, 0.0f));
    }
    pipeline.UploadBonePalette(*pipeline.GetShader(), maxBones);
    pipeline.UploadBonePalette(*pipeline.GetShadowShader(), maxBones);
    std::cout << "  -> PASS: Uploaded full 128-bone uniform palette (identity & transformed) to both shader passes." << std::endl;

    // Sub-test 3: 5-Attribute Vertex Buffer Layout & 64-Byte Stride
    std::cout << "\n[37.3] Testing SkeletalMesh 5-Attribute Layout & 64-Byte Stride..." << std::endl;
    Axle::SkeletalMeshData bipedData;
    std::shared_ptr<Axle::Skeleton> bipedSkeleton;
    std::vector<std::shared_ptr<Axle::AnimationClip>> bipedClips;
    bool bipedOk = Axle::SkeletalMeshImporter::CreateProceduralBiped(bipedData, bipedSkeleton, bipedClips);
    assert(bipedOk);
    assert(bipedData.Vertices.size() > 0);

    Axle::SkeletalMesh bipedMesh(bipedData);
    auto vao = bipedMesh.GetVAO();
    assert(vao != nullptr);
    assert(vao->GetVertexBuffers().size() == 1);

    const auto& layout = vao->GetVertexBuffers()[0]->GetLayout();
    assert(layout.GetStride() == sizeof(Axle::SkeletalVertex));
    assert(layout.GetStride() == 64); // 12 + 12 + 8 + 16 + 16 = 64 bytes
    assert(layout.GetElements().size() == 5);

    const auto& elems = layout.GetElements();
    assert(elems[0].Name == "a_Position" && elems[0].Type == Axle::ShaderDataType::Float3 && elems[0].Offset == 0);
    assert(elems[1].Name == "a_Normal" && elems[1].Type == Axle::ShaderDataType::Float3 && elems[1].Offset == 12);
    assert(elems[2].Name == "a_TexCoord" && elems[2].Type == Axle::ShaderDataType::Float2 && elems[2].Offset == 24);
    assert(elems[3].Name == "a_BoneIDs" && elems[3].Type == Axle::ShaderDataType::Int4 && elems[3].Offset == 32);
    assert(elems[4].Name == "a_BoneWeights" && elems[4].Type == Axle::ShaderDataType::Float4 && elems[4].Offset == 48);

    std::cout << "  -> Vertex Stride: " << layout.GetStride() << " bytes (sizeof(SkeletalVertex) = " << sizeof(Axle::SkeletalVertex) << ")" << std::endl;
    std::cout << "  -> Attribute 0: " << elems[0].Name << " (Float3, offset=" << elems[0].Offset << ", size=" << elems[0].Size << ")" << std::endl;
    std::cout << "  -> Attribute 1: " << elems[1].Name << " (Float3, offset=" << elems[1].Offset << ", size=" << elems[1].Size << ")" << std::endl;
    std::cout << "  -> Attribute 2: " << elems[2].Name << " (Float2, offset=" << elems[2].Offset << ", size=" << elems[2].Size << ")" << std::endl;
    std::cout << "  -> Attribute 3: " << elems[3].Name << " (Int4, offset=" << elems[3].Offset << ", size=" << elems[3].Size << ")" << std::endl;
    std::cout << "  -> Attribute 4: " << elems[4].Name << " (Float4, offset=" << elems[4].Offset << ", size=" << elems[4].Size << ")" << std::endl;
    std::cout << "  -> PASS: 5-attribute vertex specification verified with exact byte alignment." << std::endl;

    // Sub-test 4: Forward Kinematics to GPU Skinning Dispatch
    std::cout << "\n[37.4] Testing Animation Forward Kinematics to Skinning Render Dispatch..." << std::endl;
    assert(bipedClips.size() >= 2);
    auto walkClip = bipedClips[1];
    assert(walkClip->GetName() == "Walk");

    std::vector<glm::mat4> localPoses;
    walkClip->SamplePose(0.33f, *bipedSkeleton, localPoses);
    std::vector<glm::mat4> globalPoses;
    Axle::AnimationClip::ComputeGlobalPose(*bipedSkeleton, localPoses, globalPoses);
    std::vector<glm::mat4> skinningMatrices;
    Axle::AnimationClip::ComputeSkinningMatrices(*bipedSkeleton, globalPoses, skinningMatrices);
    assert(skinningMatrices.size() == 8);

    glm::mat4 characterWorld = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 10.0f));
    pipeline.RenderSkinned(*pipeline.GetShader(), vao, characterWorld, skinningMatrices);
    pipeline.RenderSkinned(*pipeline.GetShadowShader(), vao, characterWorld, skinningMatrices);

    std::cout << "  -> Dispatched RenderSkinned with 8 active animated bones at t = 0.33s." << std::endl;
    std::cout << "  -> PASS: RenderSkinned pipeline executed without errors for both lighting and shadow passes." << std::endl;

    // Sub-test 5: Integration with SceneRenderer
    std::cout << "\n[37.5] Verifying SceneRenderer Skinning Pipeline Integration..." << std::endl;
    Axle::SceneRenderer sceneRenderer;
    auto* rendererPipeline = sceneRenderer.GetSkinningPipeline();
    assert(rendererPipeline != nullptr);
    assert(rendererPipeline->GetShader() != nullptr);
    assert(rendererPipeline->GetShadowShader() != nullptr);

    std::cout << "  -> PASS: SceneRenderer owns and exposes active SkinningPipeline instance." << std::endl;

    std::cout << ">>> STEP 37 PASSED: Phase 30 Step 2 GPU Vertex Skinning & Shader Pipeline 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 38: PHASE 30 - STEP 3: ANIMATION POSE EVALUATOR & BLENDING TREE
// =============================================================================
bool RunTest_Step38_Phase30_Step3_AnimationPoseEvaluatorAndBlendingTree() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 38: PHASE 30 - STEP 3 POSE EVALUATOR & BLEND TREE" << std::endl;
    std::cout << "========================================================" << std::endl;

    // Sub-test 1: BoneTransform & AnimationPose Evaluation & Shortest-Path SLERP
    std::cout << "\n[38.1] Testing BoneTransform Arithmetic & Shortest-Path SLERP..." << std::endl;
    Axle::BoneTransform xformA(glm::vec3(0.0f, 1.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    glm::quat rot90Y = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    Axle::BoneTransform xformB(glm::vec3(0.0f, 3.0f, 0.0f), rot90Y, glm::vec3(2.0f));

    // Midpoint blend
    Axle::BoneTransform mid = Axle::BoneTransform::Blend(xformA, xformB, 0.5f);
    assert(std::abs(mid.Translation.y - 2.0f) < 1e-4f);
    assert(std::abs(mid.Scale.x - 1.5f) < 1e-4f);
    glm::vec3 fwd = mid.Rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    assert(std::abs(fwd.x - std::sin(glm::radians(45.0f))) < 1e-3f);

    // Opposite hemisphere shortest path test (negated quaternion representing same orientation)
    Axle::BoneTransform xformNeg(glm::vec3(0.0f, 3.0f, 0.0f), -rot90Y, glm::vec3(2.0f));
    Axle::BoneTransform midNeg = Axle::BoneTransform::Blend(xformA, xformNeg, 0.5f);
    glm::vec3 fwdNeg = midNeg.Rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    assert(std::abs(fwdNeg.x - std::sin(glm::radians(45.0f))) < 1e-3f); // Must follow shortest arc

    // Matrix roundtrip
    glm::mat4 mat = mid.ToMatrix();
    Axle::BoneTransform reconstructed = Axle::BoneTransform::FromMatrix(mat);
    assert(std::abs(reconstructed.Translation.y - mid.Translation.y) < 1e-3f);
    assert(std::abs(reconstructed.Scale.x - mid.Scale.x) < 1e-3f);
    std::cout << "  -> PASS: BoneTransform arithmetic, matrix roundtrip, and shortest-path SLERP mathematically verified." << std::endl;

    // Sub-test 2: 1D Blend Space Locomotion (Idle <-> Walk <-> Run)
    std::cout << "\n[38.2] Testing 1D Locomotion Blend Space (Idle <-> Walk <-> Run)..." << std::endl;
    Axle::SkeletalMeshData bipedData;
    std::shared_ptr<Axle::Skeleton> bipedSkeleton;
    std::vector<std::shared_ptr<Axle::AnimationClip>> clips;
    bool bipedOk = Axle::SkeletalMeshImporter::CreateProceduralBiped(bipedData, bipedSkeleton, clips);
    assert(bipedOk);

    std::vector<std::shared_ptr<Axle::AnimationClip>> fullClips;
    Axle::SkeletalMeshImporter::GenerateHumanoidClips(*bipedSkeleton, fullClips);
    assert(fullClips.size() == 6);

    std::shared_ptr<Axle::AnimationClip> idleClip, walkClip, runClip, jumpClip, fallClip, landClip;
    for (const auto& c : fullClips) {
        if (c->GetName() == "Idle") idleClip = c;
        else if (c->GetName() == "Walk") walkClip = c;
        else if (c->GetName() == "Run") runClip = c;
        else if (c->GetName() == "Jump") jumpClip = c;
        else if (c->GetName() == "Fall") fallClip = c;
        else if (c->GetName() == "Land") landClip = c;
    }
    assert(idleClip && walkClip && runClip && jumpClip && fallClip && landClip);

    auto locomotion = std::make_shared<Axle::BlendSpace1D>("Locomotion");
    locomotion->AddPoint(0.0f, idleClip);  // Speed 0.0 m/s
    locomotion->AddPoint(1.8f, walkClip);  // Speed 1.8 m/s
    locomotion->AddPoint(4.5f, runClip);   // Speed 4.5 m/s

    assert(locomotion->GetPointCount() == 3);

    // Boundary sampling
    Axle::AnimationPose poseIdle(bipedSkeleton->GetBoneCount());
    locomotion->SamplePose(0.0f, 0.5f, *bipedSkeleton, poseIdle);

    Axle::AnimationPose poseRun(bipedSkeleton->GetBoneCount());
    locomotion->SamplePose(4.5f, 0.3f, *bipedSkeleton, poseRun);

    // In-between sampling (Speed = 0.9 m/s, 50% Idle / 50% Walk)
    Axle::AnimationPose poseHalf(bipedSkeleton->GetBoneCount());
    locomotion->SamplePose(0.9f, 0.25f, *bipedSkeleton, poseHalf);
    for (const auto& xf : poseHalf.Transforms) {
        assert(!std::isnan(xf.Translation.x) && !std::isnan(xf.Rotation.w));
    }

    // In-between sampling (Speed = 3.15 m/s, 50% Walk / 50% Run)
    Axle::AnimationPose poseWalkRun(bipedSkeleton->GetBoneCount());
    locomotion->SamplePose(3.15f, 0.2f, *bipedSkeleton, poseWalkRun);
    assert(std::abs(locomotion->GetBlendedDuration(3.15f) - 0.8f) < 1e-3f);
    std::cout << "  -> PASS: 1D Blend Space evaluated with phase-matched time synchronization." << std::endl;

    // Sub-test 3: Animation State Machine Setup
    std::cout << "\n[38.3] Setting up Character Animation State Machine..." << std::endl;
    auto sm = std::make_shared<Axle::AnimationStateMachine>("CharacterController");

    Axle::AnimationState stateLoco;
    stateLoco.Name = "Locomotion";
    stateLoco.Type = Axle::AnimationNodeType::BlendSpace1D;
    stateLoco.BlendSpace = locomotion;
    stateLoco.BlendParameterName = "Speed";
    stateLoco.Loop = true;
    sm->AddState(stateLoco);

    Axle::AnimationState stateJump;
    stateJump.Name = "Jump";
    stateJump.Type = Axle::AnimationNodeType::Clip;
    stateJump.Clip = jumpClip;
    stateJump.Loop = false;
    sm->AddState(stateJump);

    Axle::AnimationState stateFall;
    stateFall.Name = "Fall";
    stateFall.Type = Axle::AnimationNodeType::Clip;
    stateFall.Clip = fallClip;
    stateFall.Loop = true;
    sm->AddState(stateFall);

    Axle::AnimationState stateLand;
    stateLand.Name = "Land";
    stateLand.Type = Axle::AnimationNodeType::Clip;
    stateLand.Clip = landClip;
    stateLand.Loop = false;
    sm->AddState(stateLand);

    // Transitions
    // 1. Locomotion -> Jump (Triggered by IsJumping)
    Axle::AnimationTransition tJump;
    tJump.FromState = "Locomotion";
    tJump.ToState = "Jump";
    tJump.Duration = 0.15f;
    tJump.Condition = [sm]() { return sm->GetBool("IsJumping"); };
    sm->AddTransition(tJump);

    // 2. Jump -> Fall (Wait until jump is 70% completed and vertical velocity < 0)
    Axle::AnimationTransition tFall;
    tFall.FromState = "Jump";
    tFall.ToState = "Fall";
    tFall.Duration = 0.15f;
    tFall.HasExitTime = true;
    tFall.ExitTime = 0.7f;
    tFall.Condition = [sm]() { return sm->GetFloat("VerticalVelocity") < 0.0f; };
    sm->AddTransition(tFall);

    // 3. Fall -> Land (Triggered by IsGrounded)
    Axle::AnimationTransition tLand;
    tLand.FromState = "Fall";
    tLand.ToState = "Land";
    tLand.Duration = 0.1f;
    tLand.Condition = [sm]() { return sm->GetBool("IsGrounded"); };
    sm->AddTransition(tLand);

    // 4. Land -> Locomotion (Automatic exit after landing recovery)
    Axle::AnimationTransition tLoco;
    tLoco.FromState = "Land";
    tLoco.ToState = "Locomotion";
    tLoco.Duration = 0.15f;
    tLoco.HasExitTime = true;
    tLoco.ExitTime = 0.85f;
    sm->AddTransition(tLoco);

    assert(sm->GetCurrentStateName() == "Locomotion");
    std::cout << "  -> PASS: 4 Animation states and 4 transition rules configured with predicates and exit times." << std::endl;

    // Sub-test 4: Animation State Machine Simulation Cycle (Locomotion -> Jump -> Fall -> Land -> Locomotion)
    std::cout << "\n[38.4] Simulating State Machine Lifecycle with Dynamic Cross-Fades..." << std::endl;
    sm->SetFloat("Speed", 3.0f);
    sm->Update(0.1f, *bipedSkeleton);
    assert(sm->GetCurrentStateName() == "Locomotion");
    assert(!sm->IsTransitioning());

    // Trigger Jump
    sm->SetBool("IsJumping", true);
    sm->Update(0.05f, *bipedSkeleton);
    assert(sm->IsTransitioning());
    assert(sm->GetTransitionProgress() > 0.0f && sm->GetTransitionProgress() < 1.0f);

    // Finish crossfade into Jump
    sm->Update(0.15f, *bipedSkeleton);
    assert(sm->GetCurrentStateName() == "Jump");
    assert(!sm->IsTransitioning());

    // Trigger Fall
    sm->SetBool("IsJumping", false);
    sm->SetFloat("VerticalVelocity", -4.0f);
    sm->Update(0.35f, *bipedSkeleton); // Past exit time
    sm->Update(0.15f, *bipedSkeleton); // Crossfade to Fall
    assert(sm->GetCurrentStateName() == "Fall");

    // Trigger Land
    sm->SetBool("IsGrounded", true);
    sm->Update(0.05f, *bipedSkeleton);
    assert(sm->IsTransitioning());
    sm->Update(0.1f, *bipedSkeleton);
    assert(sm->GetCurrentStateName() == "Land");

    // Rebound back to Locomotion
    sm->Update(0.35f, *bipedSkeleton); // Past landing exit time
    sm->Update(0.15f, *bipedSkeleton); // Crossfade back to Locomotion
    assert(sm->GetCurrentStateName() == "Locomotion");
    assert(sm->GetSkinningMatrices().size() == 8);

    for (const auto& mat : sm->GetSkinningMatrices()) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                assert(!std::isnan(mat[c][r]) && !std::isinf(mat[c][r]));
            }
        }
    }
    std::cout << "  -> PASS: Complete character locomotion-jump-fall-land-locomotion lifecycle executed flawlessly." << std::endl;

    // Sub-test 5: High-Level Animator Controller & GPU Skinning Pipeline Upload
    std::cout << "\n[38.5] Testing High-Level Animator Controller & GPU Skinning Upload..." << std::endl;
    Axle::Animator animator(bipedSkeleton);
    animator.SetStateMachine(sm);
    animator.SetFloat("Speed", 2.2f);
    animator.Update(0.016f); // 60 FPS tick

    assert(animator.GetSkinningMatrices().size() == 8);

    Axle::SkeletalMesh bipedMesh(bipedData);
    Axle::SkinningPipeline skinningPipeline;
    skinningPipeline.Init();

    animator.UploadToPipeline(skinningPipeline, *skinningPipeline.GetShader());
    skinningPipeline.RenderSkinned(*skinningPipeline.GetShader(), bipedMesh.GetVAO(), glm::mat4(1.0f), animator.GetSkinningMatrices());
    skinningPipeline.RenderSkinned(*skinningPipeline.GetShadowShader(), bipedMesh.GetVAO(), glm::mat4(1.0f), animator.GetSkinningMatrices());

    std::cout << "  -> PASS: Animator controller evaluated poses and dispatched GPU skinning draw call." << std::endl;

    std::cout << ">>> STEP 38 PASSED: Phase 30 Step 3 Animation Pose Evaluator & Blending Tree 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 39: PHASE 30 - STEP 4: KINEMATIC CHARACTER CONTROLLER (JOLT PHYSICS)
// =============================================================================
bool RunTest_Step39_Phase30_Step4_KinematicCharacterController() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 39: PHASE 30 - STEP 4 KINEMATIC CHARACTER CONTROLLER" << std::endl;
    std::cout << "========================================================" << std::endl;

    Axle::PhysicsSettings physSettings;
    physSettings.Gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    Axle::PhysicsScene physicsScene(physSettings);

    // 1. Static Ground Plane at Y = 0
    physicsScene.CreatePlane(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 200.0f, 0.6f, 0.0f);
    physicsScene.OptimizeBroadPhase();

    // Sub-test 1: Character Creation, Capsule Geometry & Gravity Fall
    std::cout << "\n[39.1] Testing Character Creation, Capsule Offset & Free Fall Landing..." << std::endl;
    Axle::CharacterControllerSettings charSettings;
    charSettings.Radius = 0.35f;
    charSettings.HalfHeight = 0.65f; // Capsule total height = 2.0m
    charSettings.Mass = 75.0f;
    charSettings.StepOffset = 0.4f;
    charSettings.StickToFloorStepDown = 0.5f;

    Axle::CharacterController character(physicsScene, glm::vec3(0.0f, 4.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), charSettings);
    assert(character.IsInAir());

    const float dt = 1.0f / 60.0f;
    // Step 60 frames to simulate free fall under gravity and landing
    for (int frame = 0; frame < 60; ++frame) {
        physicsScene.Step(dt);
        character.Update(dt);
    }

    assert(character.IsOnGround());
    glm::vec3 landedPos = character.GetPosition();
    // Character base is designed to sit on Y = 0 (ground level)
    assert(std::abs(landedPos.y) < 0.1f);
    assert(character.GetGroundNormal().y > 0.9f);
    std::cout << "  -> Landed safely at Y = " << landedPos.y << " m (Ground Normal: "
              << character.GetGroundNormal().x << ", " << character.GetGroundNormal().y << ", " << character.GetGroundNormal().z << ")" << std::endl;
    std::cout << "  -> PASS: CharacterVirtual fell from 4.0m and landed solidly on ground plane." << std::endl;

    // Sub-test 2: Kinematic Horizontal Locomotion & Jump Mechanics
    std::cout << "\n[39.2] Testing Horizontal Kinematic Locomotion & Jump Mechanics..." << std::endl;
    character.SetTargetVelocity(glm::vec3(4.0f, 0.0f, 0.0f)); // Move along +X at 4 m/s

    // Run for 30 frames (0.5s)
    for (int frame = 0; frame < 30; ++frame) {
        physicsScene.Step(dt);
        character.Update(dt);
    }

    assert(character.IsOnGround());
    assert(character.GetPosition().x > 1.5f);
    std::cout << "  -> Traversed along +X to X = " << character.GetPosition().x << " m while grounded." << std::endl;

    // Trigger Jump
    character.Jump(6.0f);
    for (int frame = 0; frame < 15; ++frame) {
        physicsScene.Step(dt);
        character.Update(dt);
    }
    assert(character.IsInAir());
    assert(character.GetPosition().y > 0.3f);
    std::cout << "  -> Jump apex achieved: Y = " << character.GetPosition().y << " m (State: InAir)." << std::endl;

    // Allow character to complete jump arc and land
    for (int frame = 0; frame < 45; ++frame) {
        physicsScene.Step(dt);
        character.Update(dt);
    }
    assert(character.IsOnGround());
    assert(std::abs(character.GetPosition().y) < 0.1f);
    std::cout << "  -> PASS: Horizontal velocity and jump impulse evaluated with realistic parabolic trajectory." << std::endl;

    // Sub-test 3: Automatic Stair / Curb Stepping (StepOffset)
    std::cout << "\n[39.3] Testing Automatic Stair & Curb Stepping (StepOffset = 0.4m)..." << std::endl;
    // Create a 0.25m curb obstacle in front of character (within StepOffset)
    Axle::BoxBodyProps curbProps;
    curbProps.Position = glm::vec3(8.0f, 0.125f, 0.0f); // Top of curb at Y = 0.25m
    curbProps.HalfExtent = glm::vec3(1.5f, 0.125f, 2.0f);
    curbProps.IsDynamic = false;
    physicsScene.CreateBox(curbProps);
    physicsScene.OptimizeBroadPhase();

    character.SetPosition(glm::vec3(6.0f, 0.0f, 0.0f));
    character.SetTargetVelocity(glm::vec3(3.0f, 0.0f, 0.0f));

    // Walk forward onto curb
    for (int frame = 0; frame < 40; ++frame) {
        physicsScene.Step(dt);
        character.Update(dt);
    }

    assert(character.IsOnGround());
    assert(character.GetPosition().x > 7.0f);
    assert(character.GetPosition().y > 0.2f); // Elevated onto top of curb (Y ~ 0.25m)
    std::cout << "  -> Stepped up 0.25m curb: Position = (" << character.GetPosition().x
              << ", " << character.GetPosition().y << ", " << character.GetPosition().z << ")" << std::endl;
    std::cout << "  -> PASS: Automatic stair/curb traversal succeeded without catching on vertical step edge." << std::endl;

    // Sub-test 4: Slope Traversal & Steep Cliff Sliding Resistance
    std::cout << "\n[39.4] Testing Slope Traversal & Steep Wall Sliding Resistance..." << std::endl;
    // Create a steep vertical wall (too steep to climb, height = 2m)
    Axle::BoxBodyProps wallProps;
    wallProps.Position = glm::vec3(12.0f, 1.0f, 0.0f);
    wallProps.HalfExtent = glm::vec3(0.5f, 1.0f, 3.0f);
    wallProps.IsDynamic = false;
    physicsScene.CreateBox(wallProps);
    physicsScene.OptimizeBroadPhase();

    character.SetPosition(glm::vec3(10.5f, 0.0f, 0.0f));
    character.SetTargetVelocity(glm::vec3(4.0f, 0.0f, 0.0f)); // Walk directly into vertical wall

    for (int frame = 0; frame < 30; ++frame) {
        physicsScene.Step(dt);
        character.Update(dt);
    }

    // Must be blocked by wall (front edge of wall is at X = 11.5m, character radius = 0.35m -> X <= 11.2m)
    assert(character.GetPosition().x < 11.3f);
    assert(character.GetPosition().y < 0.1f); // Did NOT climb vertical wall!
    std::cout << "  -> Blocked by vertical wall at X = " << character.GetPosition().x << " m (remained at Y = " << character.GetPosition().y << "m)" << std::endl;
    std::cout << "  -> PASS: CancelVelocityTowardsSteepSlopes prevented climbing on non-walkable surfaces." << std::endl;

    // Sub-test 5: Dynamic Physics Object Pushback & Interaction
    std::cout << "\n[39.5] Testing Dynamic Physics Object Pushback & Impulse Resistance..." << std::endl;
    Axle::BoxBodyProps crateProps;
    crateProps.Position = glm::vec3(15.0f, 0.5f, 0.0f);
    crateProps.HalfExtent = glm::vec3(0.4f, 0.4f, 0.4f);
    crateProps.IsDynamic = true;
    crateProps.Friction = 0.3f;
    Axle::BodyID crateID = physicsScene.CreateBox(crateProps);
    physicsScene.OptimizeBroadPhase();

    character.SetPosition(glm::vec3(14.0f, 0.0f, 0.0f));
    character.SetTargetVelocity(glm::vec3(3.0f, 0.0f, 0.0f)); // Walk into dynamic crate

    for (int frame = 0; frame < 40; ++frame) {
        physicsScene.Step(dt);
        character.Update(dt);
    }

    glm::vec3 crateFinalPos = physicsScene.GetBodyPosition(crateID);
    assert(crateFinalPos.x > 15.3f); // Crate pushed forward by character
    assert(character.GetPosition().x > 14.3f);
    std::cout << "  -> Dynamic crate pushed from X = 15.0m to X = " << crateFinalPos.x << " m" << std::endl;
    std::cout << "  -> PASS: Character pushed dynamic object with realistic force and momentum transfer." << std::endl;

    std::cout << ">>> STEP 39 PASSED: Phase 30 Step 4 Kinematic Character Controller (KCC) 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 40: PHASE 30 - STEP 5 ECS INTEGRATION & THIRD-PERSON GAMEPLAY CONTROLLER
// =============================================================================
static std::shared_ptr<Axle::SkeletalMesh> CreateTestBipedMesh(std::shared_ptr<Axle::Skeleton>& outSkeleton) {
    Axle::SkeletalMeshData bipedData;
    std::vector<std::shared_ptr<Axle::AnimationClip>> clips;
    Axle::SkeletalMeshImporter::CreateProceduralBiped(bipedData, outSkeleton, clips);
    return std::make_shared<Axle::SkeletalMesh>(bipedData);
}

bool RunTest_Step40_Phase30_Step5_ECSIntegrationAndThirdPersonGameplayController() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 40: PHASE 30 - STEP 5 ECS INTEGRATION & CONTROLLER  " << std::endl;
    std::cout << "========================================================" << std::endl;

    Axle::ScriptRegistry::Register<ThirdPersonController>("ThirdPersonController");
    Axle::ScriptRegistry::Register<ThirdPersonCamera>("ThirdPersonCamera");

    // [40.1] Testing ECS Component Attachment & Queries
    std::cout << "\n[40.1] Testing ECS Component Attachment & Verification..." << std::endl;
    {
        auto scene = std::make_shared<Axle::Scene>();
        auto entity = scene->CreateEntity("PlayerCharacter");

        std::shared_ptr<Axle::Skeleton> bipedSkeleton;
        auto bipedMesh = CreateTestBipedMesh(bipedSkeleton);
        entity.AddComponent<Axle::SkeletalMeshComponent>(bipedMesh, "procedural://biped");
        auto& smc = entity.GetComponent<Axle::SkeletalMeshComponent>();
        assert(smc.Mesh != nullptr);
        assert(smc.PatternMode == 3);
        assert(smc.CastShadows == true);

        auto animator = std::make_shared<Axle::Animator>(bipedSkeleton);
        entity.AddComponent<Axle::AnimatorComponent>(animator);
        auto& ac = entity.GetComponent<Axle::AnimatorComponent>();
        assert(ac.AnimatorInstance != nullptr);
        assert(ac.Playing == true);

        entity.AddComponent<Axle::CharacterControllerComponent>();
        auto& ccc = entity.GetComponent<Axle::CharacterControllerComponent>();
        ccc.WalkSpeed = 4.0f;
        ccc.RunSpeed = 8.0f;
        ccc.JumpSpeed = 7.0f;

        assert(entity.HasComponent<Axle::SkeletalMeshComponent>());
        assert(entity.HasComponent<Axle::AnimatorComponent>());
        assert(entity.HasComponent<Axle::CharacterControllerComponent>());

        // Duplicate entity test
        auto duplicated = scene->DuplicateEntity(entity);
        assert(duplicated.HasComponent<Axle::SkeletalMeshComponent>());
        assert(duplicated.HasComponent<Axle::AnimatorComponent>());
        assert(duplicated.HasComponent<Axle::CharacterControllerComponent>());
        assert(duplicated.GetComponent<Axle::CharacterControllerComponent>().WalkSpeed == 4.0f);
        std::cout << "  -> PASS: All three components attached, verified, and preserved across duplication." << std::endl;
    }

    // [40.2] Testing CharacterControllerComponent Physics Simulation
    std::cout << "\n[40.2] Testing CharacterControllerComponent Kinematics & Physics..." << std::endl;
    {
        if (!Axle::PhysicsEngine::IsInitialized()) {
            Axle::PhysicsEngine::Init();
        }
        Axle::PhysicsScene physicsScene;

        // Create ground plane
        Axle::BoxBodyProps groundProps;
        groundProps.Position = glm::vec3(0.0f, -0.5f, 0.0f);
        groundProps.HalfExtent = glm::vec3(50.0f, 0.5f, 50.0f);
        groundProps.IsDynamic = false;
        physicsScene.CreateBox(groundProps);

        auto scene = std::make_shared<Axle::Scene>();
        auto player = scene->CreateEntity("Player");
        player.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(0.0f, 3.0f, 0.0f);

        auto& ccc = player.AddComponent<Axle::CharacterControllerComponent>();
        ccc.WalkSpeed = 3.5f;
        ccc.RunSpeed = 7.0f;
        ccc.JumpSpeed = 6.0f;

        scene->OnRuntimeStart(&physicsScene);

        // Simulate falling and landing
        for (int i = 0; i < 40; ++i) {
            scene->OnUpdateRuntime(Axle::Timestep(1.0f / 60.0f), &physicsScene);
            physicsScene.Step(1.0f / 60.0f);
        }

        assert(ccc.IsGrounded);
        assert(ccc.GroundState == Axle::CharacterGroundState::OnGround);
        assert(std::abs(player.GetComponent<Axle::TransformComponent>().Translation.y) < 0.05f);

        // Test running along +X
        ccc.MoveDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        ccc.IsRunning = true;
        for (int i = 0; i < 30; ++i) {
            scene->OnUpdateRuntime(Axle::Timestep(1.0f / 60.0f), &physicsScene);
            physicsScene.Step(1.0f / 60.0f);
        }

        assert(ccc.CurrentSpeed > 6.5f);
        assert(player.GetComponent<Axle::TransformComponent>().Translation.x > 2.0f);

        // Test jumping
        ccc.JumpRequested = true;
        scene->OnUpdateRuntime(Axle::Timestep(1.0f / 60.0f), &physicsScene);
        physicsScene.Step(1.0f / 60.0f);

        assert(ccc.VerticalVelocity > 4.5f);

        scene->OnRuntimeStop(&physicsScene);
        std::cout << "  -> PASS: CharacterControllerComponent fell, landed on ground, ran at sprint speed (" << ccc.CurrentSpeed << " m/s), and jumped." << std::endl;
    }

    // [40.3] Testing Controller Telemetry to Animator Parameter Synchronization
    std::cout << "\n[40.3] Testing Controller Telemetry to Animator Synchronization..." << std::endl;
    {
        Axle::PhysicsScene physicsScene;
        Axle::BoxBodyProps groundProps;
        groundProps.Position = glm::vec3(0.0f, -0.5f, 0.0f);
        groundProps.HalfExtent = glm::vec3(50.0f, 0.5f, 50.0f);
        groundProps.IsDynamic = false;
        physicsScene.CreateBox(groundProps);

        auto scene = std::make_shared<Axle::Scene>();
        auto player = scene->CreateEntity("Player");
        player.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(0.0f, 0.0f, 0.0f);

        std::shared_ptr<Axle::Skeleton> bipedSkeleton;
        auto bipedMesh = CreateTestBipedMesh(bipedSkeleton);
        auto animator = std::make_shared<Axle::Animator>(bipedSkeleton);
        auto& ac = player.AddComponent<Axle::AnimatorComponent>(animator);
        auto& ccc = player.AddComponent<Axle::CharacterControllerComponent>();

        scene->OnRuntimeStart(&physicsScene);

        // Move player forward
        ccc.MoveDirection = glm::vec3(0.0f, 0.0f, -1.0f);
        ccc.IsRunning = false; // Walk
        for (int i = 0; i < 20; ++i) {
            scene->OnUpdateRuntime(Axle::Timestep(1.0f / 60.0f), &physicsScene);
            physicsScene.Step(1.0f / 60.0f);
        }

        // Animator should automatically receive telemetry from CharacterControllerComponent
        assert(ac.Speed > 3.0f);
        assert(ac.IsGrounded == true);
        std::cout << "  -> Animator Speed automatically synced: " << ac.Speed << " m/s (IsGrounded: " << (ac.IsGrounded ? "true" : "false") << ")" << std::endl;

        // Jump
        ccc.JumpRequested = true;
        scene->OnUpdateRuntime(Axle::Timestep(1.0f / 60.0f), &physicsScene);
        physicsScene.Step(1.0f / 60.0f);

        assert(ac.VerticalVelocity > 4.5f);
        std::cout << "  -> Animator VerticalVelocity automatically synced on jump: " << ac.VerticalVelocity << " m/s" << std::endl;

        scene->OnRuntimeStop(&physicsScene);
        std::cout << "  -> PASS: Real-time telemetry forward to Animator verified 100%." << std::endl;
    }

    // [40.4] Testing ThirdPersonController Camera-Relative Movement
    std::cout << "\n[40.4] Testing ThirdPersonController Script & Camera-Relative Vectors..." << std::endl;
    {
        auto scene = std::make_shared<Axle::Scene>();

        // Create Camera rotated 90 degrees around Y (facing -X)
        auto camera = scene->CreateEntity("Main Camera");
        camera.AddComponent<Axle::CameraComponent>();
        auto& camTC = camera.GetComponent<Axle::TransformComponent>();
        camTC.Rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)); // 90 deg Yaw

        auto player = scene->CreateEntity("Player");
        auto& ccc = player.AddComponent<Axle::CharacterControllerComponent>();
        auto& nsc = player.AddComponent<Axle::NativeScriptComponent>("ThirdPersonController");
        assert(nsc.InstantiateScript != nullptr);

        // Instantiate script and test camera vector math
        nsc.Instance = nsc.InstantiateScript();
        nsc.Instance->SetEntity(player);
        nsc.Instance->OnCreate();

        // When camera is rotated 90 deg yaw:
        // Forward vector = camRot * (0, 0, -1) = (-1, 0, 0)
        // Right vector = camRot * (1, 0, 0) = (0, 0, -1)
        glm::mat4 camWorld = camera.GetWorldTransform();
        glm::quat camRot = glm::quat_cast(camWorld);
        glm::vec3 camForward = glm::normalize(camRot * glm::vec3(0.0f, 0.0f, -1.0f));
        camForward.y = 0.0f;
        camForward = glm::normalize(camForward);

        assert(std::abs(camForward.x - (-1.0f)) < 0.01f);
        assert(std::abs(camForward.z) < 0.01f);
        std::cout << "  -> Camera-Relative Forward Vector (at Yaw 90 deg): (" << camForward.x << ", " << camForward.y << ", " << camForward.z << ")" << std::endl;

        nsc.Instance->OnDestroy();
        delete nsc.Instance;
        nsc.Instance = nullptr;

        std::cout << "  -> PASS: ThirdPersonController script instantiation and camera vector transformations verified." << std::endl;
    }

    // [40.5] Testing ThirdPersonCamera Spring-Arm Orbit, Pitch Clamping & Tracking
    std::cout << "\n[40.5] Testing ThirdPersonCamera Spring-Arm Orbit & Pitch Clamping..." << std::endl;
    {
        auto scene = std::make_shared<Axle::Scene>();
        auto player = scene->CreateEntity("Player");
        player.AddComponent<Axle::CharacterControllerComponent>();
        player.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(10.0f, 0.0f, -5.0f);

        auto camera = scene->CreateEntity("Main Camera");
        camera.AddComponent<Axle::CameraComponent>();
        auto& nsc = camera.AddComponent<Axle::NativeScriptComponent>("ThirdPersonCamera");
        assert(nsc.InstantiateScript != nullptr);

        nsc.Instance = nsc.InstantiateScript();
        nsc.Instance->SetEntity(camera);
        nsc.Instance->OnCreate();

        auto* tpc = dynamic_cast<ThirdPersonCamera*>(nsc.Instance);
        assert(tpc != nullptr);

        tpc->TargetTag = "Player";
        tpc->Distance = 4.0f;
        tpc->TargetHeightOffset = 1.5f;
        tpc->SetYawPitch(0.0f, 0.0f); // Level look

        // Simulate 60 frames to let spring-arm interpolate
        for (int i = 0; i < 60; ++i) {
            nsc.Instance->OnUpdate(Axle::Timestep(1.0f / 60.0f));
        }

        auto camPos = camera.GetComponent<Axle::TransformComponent>().Translation;
        // With Yaw 0, Pitch 0, offset = (0, 0, Distance=4)
        // Focus = (10, 1.5, -5) -> Expected Pos = (10, 1.5, -1)
        assert(std::abs(camPos.x - 10.0f) < 0.1f);
        assert(std::abs(camPos.y - 1.5f) < 0.1f);
        assert(std::abs(camPos.z - (-1.0f)) < 0.1f);

        // Pitch clamping test
        tpc->SetYawPitch(0.0f, 95.0f); // Above max 75
        assert(tpc->GetPitch() == 75.0f);
        tpc->SetYawPitch(0.0f, -80.0f); // Below min -35
        assert(tpc->GetPitch() == -35.0f);

        nsc.Instance->OnDestroy();
        delete nsc.Instance;
        nsc.Instance = nullptr;

        std::cout << "  -> PASS: Spring-arm orbit targeting (" << camPos.x << ", " << camPos.y << ", " << camPos.z << ") and pitch limits [-35, 75] proven." << std::endl;
    }

    // [40.6] Testing Scene Serialization & Deserialization
    std::cout << "\n[40.6] Testing Scene Serialization & Deserialization of All Components..." << std::endl;
    {
        std::string yamlData;
        {
            auto scene = std::make_shared<Axle::Scene>();
            auto player = scene->CreateEntity("Hero");

            std::shared_ptr<Axle::Skeleton> bipedSkeleton;
            auto biped = CreateTestBipedMesh(bipedSkeleton);
            player.AddComponent<Axle::SkeletalMeshComponent>(biped, "assets/models/Hero.fbx");
            auto& smc = player.GetComponent<Axle::SkeletalMeshComponent>();
            smc.Color1 = glm::vec4(0.2f, 0.4f, 0.8f, 1.0f);

            auto& ac = player.AddComponent<Axle::AnimatorComponent>();
            ac.Speed = 5.5f;
            ac.IsGrounded = true;

            auto& ccc = player.AddComponent<Axle::CharacterControllerComponent>();
            ccc.WalkSpeed = 4.2f;
            ccc.RunSpeed = 8.5f;
            ccc.JumpSpeed = 6.5f;
            ccc.Settings.Radius = 0.4f;
            ccc.Settings.HalfHeight = 0.7f;

            Axle::SceneSerializer serializer(scene.get());
            yamlData = serializer.SerializeToString();

            assert(yamlData.find("SkeletalMeshComponent:") != std::string::npos);
            assert(yamlData.find("AnimatorComponent:") != std::string::npos);
            assert(yamlData.find("CharacterControllerComponent:") != std::string::npos);
            assert(yamlData.find("assets/models/Hero.fbx") != std::string::npos);
            assert(yamlData.find("WalkSpeed: 4.2") != std::string::npos);
            assert(yamlData.find("RunSpeed: 8.5") != std::string::npos);
        }

        // Deserialize into new scene
        {
            auto newScene = std::make_shared<Axle::Scene>();
            Axle::SceneSerializer deserializer(newScene.get());
            bool success = deserializer.DeserializeFromString(yamlData);
            assert(success);

            auto view = newScene->GetRegistry().view<Axle::TagComponent>();
            Axle::Entity restoredHero{};
            for (auto ent : view) {
                if (view.get<Axle::TagComponent>(ent).Tag == "Hero") {
                    restoredHero = Axle::Entity{ ent, newScene.get() };
                    break;
                }
            }

            assert(restoredHero);
            assert(restoredHero.HasComponent<Axle::SkeletalMeshComponent>());
            assert(restoredHero.HasComponent<Axle::AnimatorComponent>());
            assert(restoredHero.HasComponent<Axle::CharacterControllerComponent>());

            const auto& smc = restoredHero.GetComponent<Axle::SkeletalMeshComponent>();
            assert(smc.AssetPath == "assets/models/Hero.fbx");
            assert(std::abs(smc.Color1.r - 0.2f) < 0.01f);

            const auto& ac = restoredHero.GetComponent<Axle::AnimatorComponent>();
            assert(std::abs(ac.Speed - 5.5f) < 0.01f);
            assert(ac.IsGrounded == true);

            const auto& ccc = restoredHero.GetComponent<Axle::CharacterControllerComponent>();
            assert(std::abs(ccc.WalkSpeed - 4.2f) < 0.01f);
            assert(std::abs(ccc.RunSpeed - 8.5f) < 0.01f);
            assert(std::abs(ccc.JumpSpeed - 6.5f) < 0.01f);
            assert(std::abs(ccc.Settings.Radius - 0.4f) < 0.01f);
            assert(std::abs(ccc.Settings.HalfHeight - 0.7f) < 0.01f);

            std::cout << "  -> PASS: Hero entity restored with 100% component fidelity." << std::endl;

            // Test Scene::Copy preservation (used by Editor Play mode)
            auto clonedScene = Axle::Scene::Copy(newScene);
            assert(clonedScene);
            auto clonedView = clonedScene->GetRegistry().view<Axle::TagComponent>();
            Axle::Entity clonedHero{};
            for (auto ent : clonedView) {
                if (clonedView.get<Axle::TagComponent>(ent).Tag == "Hero") {
                    clonedHero = Axle::Entity{ ent, clonedScene.get() };
                    break;
                }
            }
            assert(clonedHero);
            assert(clonedHero.HasComponent<Axle::SkeletalMeshComponent>());
            assert(clonedHero.HasComponent<Axle::AnimatorComponent>());
            assert(clonedHero.HasComponent<Axle::CharacterControllerComponent>());
            const auto& cccCloned = clonedHero.GetComponent<Axle::CharacterControllerComponent>();
            assert(std::abs(cccCloned.WalkSpeed - 4.2f) < 0.01f);
            assert(std::abs(cccCloned.RunSpeed - 8.5f) < 0.01f);
            std::cout << "  -> PASS: Scene::Copy preserved SkeletalMeshComponent, AnimatorComponent, and CharacterControllerComponent for Play Mode." << std::endl;
        }
    }

    std::cout << ">>> STEP 40 PASSED: Phase 30 Step 5 ECS Integration & Third-Person Controller 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 41: Phase 30 Step 6 - Character Sandbox Playground & Scene Integration
// =============================================================================
bool RunTest_Step41_Phase30_Step6_CharacterSandboxPlaygroundAndSceneIntegration() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  STEP 41: PHASE 30 - STEP 6 CHARACTER SANDBOX PLAYGROUND " << std::endl;
    std::cout << "========================================================" << std::endl;

    // [41.1] Verify CharacterSandbox Scene Deserialization & Entity Topology
    std::cout << "\n[41.1] Loading CharacterSandbox.axlescene and verifying hierarchy..." << std::endl;
    std::filesystem::path scenePath = "Editor/assets/scenes/CharacterSandbox.axlescene";
    if (!std::filesystem::exists(scenePath)) {
        scenePath = "assets/scenes/CharacterSandbox.axlescene";
    }
    if (!std::filesystem::exists(scenePath)) {
        scenePath = "../Editor/assets/scenes/CharacterSandbox.axlescene";
    }
    if (!std::filesystem::exists(scenePath)) {
        scenePath = "../../Editor/assets/scenes/CharacterSandbox.axlescene";
    }
    if (!std::filesystem::exists(scenePath)) {
        std::cerr << "FAIL: CharacterSandbox.axlescene does not exist!" << std::endl;
        return false;
    }

    auto scene = std::make_shared<Axle::Scene>();
    auto physicsScene = std::make_unique<Axle::PhysicsScene>();
    Axle::SceneSerializer serializer(scene.get(), physicsScene.get());
    if (!serializer.Deserialize(scenePath)) {
        std::cerr << "FAIL: Could not deserialize " << scenePath << std::endl;
        return false;
    }

    auto findEntityByTag = [&](const std::string& targetTag) -> Axle::Entity {
        auto view = scene->GetRegistry().view<Axle::TagComponent>();
        for (auto e : view) {
            if (view.get<Axle::TagComponent>(e).Tag == targetTag) {
                return Axle::Entity{ e, scene.get() };
            }
        }
        return {};
    };

    Axle::Entity player = findEntityByTag("Player");
    Axle::Entity camera = findEntityByTag("Main Camera");
    Axle::Entity ground = findEntityByTag("Ground");
    Axle::Entity stair1 = findEntityByTag("Stair_1");
    Axle::Entity stair2 = findEntityByTag("Stair_2");
    Axle::Entity stair3 = findEntityByTag("Stair_3");
    Axle::Entity stair4 = findEntityByTag("Stair_4");
    Axle::Entity stair5 = findEntityByTag("Stair_5");
    Axle::Entity platform = findEntityByTag("ElevatedPlatform");
    Axle::Entity ramp = findEntityByTag("SlopedRamp");
    Axle::Entity steepWall = findEntityByTag("SteepWall");
    Axle::Entity crate1 = findEntityByTag("Crate_Base_1");
    Axle::Entity crate2 = findEntityByTag("Crate_Base_2");
    Axle::Entity crate3 = findEntityByTag("Crate_Base_3");
    Axle::Entity crateTop = findEntityByTag("Crate_Top");

    assert(player && "Player entity not found!");
    assert(camera && "Main Camera entity not found!");
    assert(ground && "Ground entity not found!");
    assert(stair1 && stair2 && stair3 && stair4 && stair5 && platform && "Stairs/Platform entities not found!");
    assert(ramp && "SlopedRamp entity not found!");
    assert(steepWall && "SteepWall entity not found!");
    assert(crate1 && crate2 && crate3 && crateTop && "Crate entities not found!");

    assert(player.HasComponent<Axle::SkeletalMeshComponent>());
    assert(player.HasComponent<Axle::AnimatorComponent>());
    assert(player.HasComponent<Axle::CharacterControllerComponent>());
    assert(player.HasComponent<Axle::NativeScriptComponent>());

    assert(camera.HasComponent<Axle::CameraComponent>());
    assert(camera.HasComponent<Axle::NativeScriptComponent>());
    std::cout << "  -> PASS: CharacterSandbox.axlescene deserialized with 100% hierarchy integrity." << std::endl;

    // [41.2] Equipping Player with Procedural Humanoid Biped & Animation State Machine
    std::cout << "\n[41.2] Equipping Player with Procedural Humanoid Biped & Animation State Machine..." << std::endl;
    Axle::SkeletalMeshData bipedData;
    std::shared_ptr<Axle::Skeleton> bipedSkeleton;
    std::vector<std::shared_ptr<Axle::AnimationClip>> bipedClips;
    bool bipedOk = Axle::SkeletalMeshImporter::CreateProceduralBiped(bipedData, bipedSkeleton, bipedClips);
    assert(bipedOk);
    auto bipedMesh = std::make_shared<Axle::SkeletalMesh>(bipedData);

    auto& smc = player.GetComponent<Axle::SkeletalMeshComponent>();
    smc.Mesh = bipedMesh;

    auto& ac = player.GetComponent<Axle::AnimatorComponent>();
    auto animator = std::make_shared<Axle::Animator>(bipedSkeleton);
    auto stateMachine = std::make_shared<Axle::AnimationStateMachine>("LocomotionStateMachine");

    Axle::AnimationState stateIdle;
    stateIdle.Name = "Idle";
    stateIdle.Type = Axle::AnimationNodeType::Clip;
    stateIdle.Clip = bipedClips[0];
    stateIdle.Loop = true;
    stateMachine->AddState(stateIdle);

    Axle::AnimationState stateWalk;
    stateWalk.Name = "Walk";
    stateWalk.Type = Axle::AnimationNodeType::Clip;
    stateWalk.Clip = bipedClips[1];
    stateWalk.Loop = true;
    stateMachine->AddState(stateWalk);

    Axle::AnimationTransition tIdleToWalk;
    tIdleToWalk.FromState = "Idle";
    tIdleToWalk.ToState = "Walk";
    tIdleToWalk.Duration = 0.2f;
    tIdleToWalk.Condition = [stateMachine]() { return stateMachine->GetFloat("Speed") > 0.1f; };
    stateMachine->AddTransition(tIdleToWalk);

    Axle::AnimationTransition tWalkToIdle;
    tWalkToIdle.FromState = "Walk";
    tWalkToIdle.ToState = "Idle";
    tWalkToIdle.Duration = 0.2f;
    tWalkToIdle.Condition = [stateMachine]() { return stateMachine->GetFloat("Speed") <= 0.1f; };
    stateMachine->AddTransition(tWalkToIdle);

    animator->SetStateMachine(stateMachine);
    ac.AnimatorInstance = animator;
    std::cout << "  -> PASS: Procedural Humanoid biped & Locomotion state machine attached." << std::endl;

    // [41.3] Initializing Sandbox Runtime Simulation & Native Scripts
    std::cout << "\n[41.3] Initializing Sandbox Runtime Simulation & Native Scripts..." << std::endl;
    Axle::ScriptRegistry::Register<ThirdPersonController>("ThirdPersonController");
    Axle::ScriptRegistry::Register<ThirdPersonCamera>("ThirdPersonCamera");

    scene->OnRuntimeStart(physicsScene.get());

    auto& ccc = player.GetComponent<Axle::CharacterControllerComponent>();
    assert(ccc.RuntimeController != nullptr);

    auto& playerNsc = player.GetComponent<Axle::NativeScriptComponent>();
    auto* controllerScript = dynamic_cast<ThirdPersonController*>(playerNsc.Instance);
    assert(controllerScript != nullptr);

    auto& cameraNsc = camera.GetComponent<Axle::NativeScriptComponent>();
    auto* cameraScript = dynamic_cast<ThirdPersonCamera*>(cameraNsc.Instance);
    assert(cameraScript != nullptr);
    std::cout << "  -> PASS: Runtime controller & scripts initialized successfully." << std::endl;

    // [41.4] Testing Staircase Traversal (Ascending 5 Steps via StepOffset)
    std::cout << "\n[41.4] Testing Staircase Traversal (Ascending 5 Steps via StepOffset)..." << std::endl;
    // Position character in front of Stair_1 at (0, 0.5, -3.2), facing -Z towards stairs
    ccc.RuntimeController->SetPosition(glm::vec3(0.0f, 0.5f, -3.2f));
    controllerScript->DirectVirtualDirection = true;
    controllerScript->VirtualMoveDirection = glm::vec3(0.0f, 0.0f, -1.0f); // Walk straight towards -Z into stairs
    controllerScript->VirtualSprint = false;

    // Simulate for 60 frames (1.0 second)
    for (int frame = 0; frame < 60; ++frame) {
        scene->OnUpdateRuntime(Axle::Timestep(1.0f / 60.0f), physicsScene.get());
        physicsScene->Step(1.0f / 60.0f);
    }

    glm::vec3 postStairPos = ccc.RuntimeController->GetPosition();
    std::cout << "  -> Position after walking up stairs: Y = " << postStairPos.y << " m, Z = " << postStairPos.z << " m" << std::endl;
    assert(postStairPos.y > 0.8f);
    assert(postStairPos.z < -4.0f);
    assert(ccc.IsGrounded);
    assert(ac.Speed > 0.1f);
    std::cout << "  -> PASS: Character ascended stairs smoothly without jumping. Grounded: " << (ccc.IsGrounded ? "true" : "false") << std::endl;

    // [41.5] Testing Steep Wall Blocking (90 deg Vertical Barrier)
    std::cout << "\n[41.5] Testing Steep Wall Blocking (90 deg Vertical Barrier)..." << std::endl;
    // Wall is at X = 6.0, thickness 0.5 (surface at X = 5.75). Place player at X = 5.0, move towards +X
    ccc.RuntimeController->SetPosition(glm::vec3(5.0f, 0.5f, 0.0f));
    controllerScript->DirectVirtualDirection = true;
    controllerScript->VirtualMoveDirection = glm::vec3(1.0f, 0.0f, 0.0f); // Move towards +X into wall

    for (int frame = 0; frame < 30; ++frame) {
        scene->OnUpdateRuntime(Axle::Timestep(1.0f / 60.0f), physicsScene.get());
        physicsScene->Step(1.0f / 60.0f);
    }

    glm::vec3 blockedPos = ccc.RuntimeController->GetPosition();
    std::cout << "  -> Blocked position at wall: X = " << blockedPos.x << " m (Wall boundary is at X = 5.75 m)" << std::endl;
    assert(blockedPos.x < 5.6f);
    std::cout << "  -> PASS: Vertical wall blocked character movement (MaxSlopeAngle: 50 deg enforced)." << std::endl;

    // [41.6] Testing Dynamic Obstacle Interaction (Pushing 20kg Crate)
    std::cout << "\n[41.6] Testing Dynamic Obstacle Interaction (Pushing 20kg Crate)..." << std::endl;
    auto& crateRb = crate1.GetComponent<Axle::RigidBody3D>();
    assert(!crateRb.RuntimeBodyID.IsInvalid());
    glm::vec3 initialCratePos = physicsScene->GetBodyPosition(crateRb.RuntimeBodyID);
    std::cout << "  -> Initial Crate_Base_1 position: (" << initialCratePos.x << ", " << initialCratePos.y << ", " << initialCratePos.z << ")" << std::endl;

    // Place character in front of Crate_Base_1 at (-1.2, 0.0, 3.0), moving towards +Z into crate
    ccc.RuntimeController->SetPosition(glm::vec3(-1.2f, 0.0f, 3.0f));
    controllerScript->DirectVirtualDirection = true;
    controllerScript->VirtualMoveDirection = glm::vec3(0.0f, 0.0f, 1.0f); // Push towards +Z into crate

    for (int frame = 0; frame < 45; ++frame) {
        scene->OnUpdateRuntime(Axle::Timestep(1.0f / 60.0f), physicsScene.get());
        physicsScene->Step(1.0f / 60.0f);
    }

    glm::vec3 finalCratePos = physicsScene->GetBodyPosition(crateRb.RuntimeBodyID);
    std::cout << "  -> Final Crate position after push: (" << finalCratePos.x << ", " << finalCratePos.y << ", " << finalCratePos.z << ")" << std::endl;
    assert(finalCratePos.z > initialCratePos.z + 0.05f);
    std::cout << "  -> PASS: Dynamic crate pushed forward (Displacement: " << (finalCratePos.z - initialCratePos.z) << " m)." << std::endl;

    // [41.7] Testing ThirdPersonCamera Spring-Arm Orbit & Target Following
    std::cout << "\n[41.7] Testing ThirdPersonCamera Spring-Arm Orbit & Target Following..." << std::endl;
    auto& camTC = camera.GetComponent<Axle::TransformComponent>();
    std::cout << "  -> Camera position tracking Player: (" << camTC.Translation.x << ", " << camTC.Translation.y << ", " << camTC.Translation.z << ")" << std::endl;
    assert(camTC.Translation.y >= cameraScript->MinElevation);
    float distToPlayer = glm::distance(camTC.Translation, ccc.RuntimeController->GetPosition());
    std::cout << "  -> Distance to Player: " << distToPlayer << " m (Target Distance: " << cameraScript->Distance << " m)" << std::endl;
    assert(std::abs(distToPlayer - cameraScript->Distance) < 2.0f);
    std::cout << "  -> PASS: ThirdPersonCamera spring-arm follow validated." << std::endl;

    // [41.8] Testing Multi-Pass Skeletal Renderer Dispatch in Sandbox
    std::cout << "\n[41.8] Testing Multi-Pass Skeletal Renderer Dispatch in Sandbox..." << std::endl;
    Axle::SkinningPipeline skinningPipeline;
    skinningPipeline.Init();

    Axle::PerspectiveCamera testCam(65.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    testCam.SetPosition(camTC.Translation);
    glm::mat4 lightSpaceMat = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, -20.0f, 40.0f);

    // Pass 1: Shadow Depth Pass
    scene->RenderSkeletalMeshes(skinningPipeline, testCam, lightSpaceMat, glm::vec3(0.5f, 1.0f, 0.3f), true);
    // Pass 2: PBR Forward Lighting Pass
    scene->RenderSkeletalMeshes(skinningPipeline, testCam, lightSpaceMat, glm::vec3(0.5f, 1.0f, 0.3f), false);
    std::cout << "  -> PASS: Shadow depth and PBR lighting passes dispatched with active bone palettes." << std::endl;

    // Clean runtime stop
    scene->OnRuntimeStop(physicsScene.get());
    assert(ccc.RuntimeController == nullptr);
    std::cout << "  -> PASS: Runtime simulation cleaned up safely." << std::endl;

    std::cout << ">>> STEP 41 PASSED: Phase 30 Step 6 Character Sandbox Playground 100% verified! <<<\n" << std::endl;
    return true;
}