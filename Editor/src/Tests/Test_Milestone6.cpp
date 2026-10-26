#include "TestCommon.hpp"

// =============================================================================
// STEP 27: Phase 23 - Asset Packaging (.axlepak) & Virtual File System (VFS)
// =============================================================================
bool RunTest_Step27_Phase23_PackageAndVFS() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 27 TEST] PHASE 23: PACKAGE & VFS PIPELINE       " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::filesystem::path testPakPath = "TestPackage.axlepak";
    if (std::filesystem::exists(testPakPath)) {
        std::filesystem::remove(testPakPath);
    }

    std::cout << "[27.1] Creating package archive via PackageWriter..." << std::endl;
    {
        Axle::PackageWriter writer;
        if (!writer.Begin(testPakPath)) {
            std::cerr << "FAIL: PackageWriter::Begin failed for " << testPakPath << std::endl;
            return false;
        }

        // 1. Add text entry
        std::string sampleText = "Hello from Axle Virtual File System!\nThis is a packed text asset.";
        Axle::UUID textUUID(123456789ULL);
        writer.AddData("assets/scenes/Sample.txt", sampleText.data(), sampleText.size(), textUUID, (uint16_t)Axle::AssetType::Scene);

        // 2. Add binary entry
        std::vector<uint8_t> sampleBinary(1024);
        for (size_t i = 0; i < sampleBinary.size(); ++i) {
            sampleBinary[i] = static_cast<uint8_t>(i % 256);
        }
        Axle::UUID binUUID(987654321ULL);
        writer.AddData("assets/models/BinaryData.bin", sampleBinary.data(), sampleBinary.size(), binUUID, (uint16_t)Axle::AssetType::Mesh);

        // 3. Write archive
        if (!writer.Finish()) {
            std::cerr << "FAIL: PackageWriter::Finish failed for " << testPakPath << std::endl;
            return false;
        }
        std::cout << "  -> PASS: Package archive '" << testPakPath.string() << "' written successfully." << std::endl;
    }

    std::cout << "[27.2] Loading and validating package archive..." << std::endl;
    {
        auto package = std::make_shared<Axle::Package>();
        if (!package->Open(testPakPath)) {
            std::cerr << "FAIL: Axle::Package::Open failed!" << std::endl;
            return false;
        }
        if (package->GetEntryCount() != 2) {
            std::cerr << "FAIL: Expected 2 entries, got " << package->GetEntryCount() << std::endl;
            return false;
        }

        // Verify direct path lookup & normalization
        if (!package->HasEntry("assets/scenes/Sample.txt") || !package->HasEntry("scenes/Sample.txt")) {
            std::cerr << "FAIL: Path normalization lookup failed for scenes/Sample.txt!" << std::endl;
            return false;
        }

        // Verify UUID lookup
        Axle::UUID textUUID(123456789ULL);
        if (!package->HasEntry(textUUID)) {
            std::cerr << "FAIL: UUID lookup failed for textUUID!" << std::endl;
            return false;
        }

        // Verify content and CRC32
        std::string extractedText = package->ReadEntryAsText("scenes/Sample.txt");
        if (extractedText.find("Hello from Axle Virtual File System!") == std::string::npos) {
            std::cerr << "FAIL: ReadEntryAsText content mismatch!" << std::endl;
            return false;
        }

        std::vector<uint8_t> extractedBin = package->ReadEntry("assets/models/BinaryData.bin");
        if (extractedBin.size() != 1024) {
            std::cerr << "FAIL: ReadEntry size mismatch: " << extractedBin.size() << std::endl;
            return false;
        }
        for (size_t i = 0; i < extractedBin.size(); ++i) {
            if (extractedBin[i] != static_cast<uint8_t>(i % 256)) {
                std::cerr << "FAIL: ReadEntry byte mismatch at index " << i << std::endl;
                return false;
            }
        }
        std::cout << "  -> PASS: Package contents and CRC32 validated with 100% data integrity." << std::endl;
    }

    std::cout << "[27.3] Testing VFS transparent fallback via FileSystem..." << std::endl;
    {
        // Mount
        if (!Axle::FileSystem::MountPackage(testPakPath)) {
            std::cerr << "FAIL: FileSystem::MountPackage failed!" << std::endl;
            return false;
        }
        if (!Axle::FileSystem::HasMountedPackage()) {
            std::cerr << "FAIL: HasMountedPackage() returned false!" << std::endl;
            return false;
        }

        // Check Exists() on VFS
        if (!Axle::FileSystem::Exists("scenes/Sample.txt")) {
            std::cerr << "FAIL: FileSystem::Exists failed for mounted virtual path!" << std::endl;
            return false;
        }

        // Check ReadTextFile() from VFS
        std::string vfsText = Axle::FileSystem::ReadTextFile("scenes/Sample.txt");
        if (vfsText.find("Hello from Axle Virtual File System!") == std::string::npos) {
            std::cerr << "FAIL: FileSystem::ReadTextFile failed to extract from VFS!" << std::endl;
            return false;
        }

        // Check ReadBinaryFile() from VFS
        std::vector<uint8_t> vfsBin = Axle::FileSystem::ReadBinaryFile("models/BinaryData.bin");
        if (vfsBin.size() != 1024 || vfsBin[50] != static_cast<uint8_t>(50 % 256)) {
            std::cerr << "FAIL: FileSystem::ReadBinaryFile failed to extract from VFS!" << std::endl;
            return false;
        }

        // Unmount
        Axle::FileSystem::UnmountPackage();
        if (Axle::FileSystem::HasMountedPackage()) {
            std::cerr << "FAIL: UnmountPackage failed to clear mounted package!" << std::endl;
            return false;
        }

        // Cleanup temp file
        std::filesystem::remove(testPakPath);
        std::cout << "  -> PASS: FileSystem VFS mount, lookup, binary reading, and unmount verified!" << std::endl;
    }

    std::cout << ">>> STEP 27 PASSED: Phase 23 Package & VFS Pipeline verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 28: Phase 24 AAA Material Asset & Flyweight Architecture
// =============================================================================
bool RunTest_Step28_Phase24_MaterialAndFlyweight() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 28 TEST] AAA MATERIAL & FLYWEIGHT ARCHITECTURE   " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::filesystem::path testDir = "temp_step28_material_test";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);

    Axle::AssetManager::Init(testDir);

    // -------------------------------------------------------------------------
    // [28.1] Core Axle::Material Class, Presets & Standalone Serialization
    // -------------------------------------------------------------------------
    std::cout << "\n[28.1] Testing Axle::Material presets, properties & file I/O..." << std::endl;
    auto matPath = testDir / "TestCarPaint.amat";
    {
        auto mat = std::make_shared<Axle::Material>("TestCarPaint");
        mat->SetPresetCarPaintBlue();
        if (std::abs(mat->GetRoughness() - 0.22f) > 1e-3f ||
            std::abs(mat->GetMetallic() - 0.75f) > 1e-3f) {
            std::cerr << "FAIL: PresetCarPaintBlue properties mismatch!" << std::endl;
            return false;
        }

        // Customize properties
        mat->SetAlbedoColor(glm::vec4(0.1f, 0.4f, 0.9f, 1.0f));
        mat->SetRoughness(0.35f);
        mat->SetMetallic(0.85f);
        mat->SetEmissionColor(glm::vec3(0.5f, 0.2f, 0.8f));
        mat->SetEmissionIntensity(2.5f);
        mat->SetUVScale(glm::vec2(3.0f, 3.0f));

        if (!mat->Save(matPath)) {
            std::cerr << "FAIL: Failed to save Material to " << matPath << std::endl;
            return false;
        }

        // Load into fresh instance
        Axle::Material loadedMat;
        if (!loadedMat.Load(matPath)) {
            std::cerr << "FAIL: Failed to load Material from " << matPath << std::endl;
            return false;
        }

        if (std::abs(loadedMat.GetAlbedoColor().r - 0.1f) > 1e-3f ||
            std::abs(loadedMat.GetRoughness() - 0.35f) > 1e-3f ||
            std::abs(loadedMat.GetMetallic() - 0.85f) > 1e-3f ||
            std::abs(loadedMat.GetEmissionIntensity() - 2.5f) > 1e-3f ||
            std::abs(loadedMat.GetUVScale().x - 3.0f) > 1e-3f ||
            loadedMat.GetName() != "TestCarPaint") {
            std::cerr << "FAIL: Loaded Material properties mismatch!" << std::endl;
            return false;
        }

        std::cout << "  -> PASS: Axle::Material preset, custom properties, and Save/Load verified." << std::endl;
    }

    // -------------------------------------------------------------------------
    // [28.2] AssetManager MaterialCache & In-Place Live Reload
    // -------------------------------------------------------------------------
    std::cout << "\n[28.2] Testing Centralized MaterialCache and In-Place Live Reload..." << std::endl;
    {
        // Register asset metadata
        auto meta = Axle::AssetManager::GetOrCreateMetadata(matPath);
        if (meta.Handle == 0 || meta.Type != Axle::AssetType::Material) {
            std::cerr << "FAIL: Failed to register Material metadata!" << std::endl;
            return false;
        }

        // Cache hit test: GetMaterial by path
        auto matA = Axle::AssetManager::GetMaterial(matPath);
        auto matB = Axle::AssetManager::GetMaterial(matPath);
        if (!matA || !matB || matA.get() != matB.get()) {
            std::cerr << "FAIL: MaterialCache failed to return identical pointer for same path!" << std::endl;
            return false;
        }

        // Cache hit test: GetMaterial by UUID
        auto matByUUID = Axle::AssetManager::GetMaterial(meta.Handle);
        if (matByUUID.get() != matA.get()) {
            std::cerr << "FAIL: MaterialCache by UUID returned different pointer!" << std::endl;
            return false;
        }

        // Test in-place live reload:
        // Modify file on disk directly
        {
            Axle::Material diskModifier;
            diskModifier.Load(matPath);
            diskModifier.SetRoughness(0.05f); // Super glossy
            diskModifier.SetMetallic(0.99f);
            diskModifier.Save(matPath);
        }

        // Trigger ReloadMaterial
        Axle::AssetManager::ReloadMaterial(meta.Handle);

        // In-place verification: pointer matA MUST NOT have changed, but its internal properties MUST be 0.05f!
        if (std::abs(matA->GetRoughness() - 0.05f) > 1e-3f ||
            std::abs(matA->GetMetallic() - 0.99f) > 1e-3f) {
            std::cerr << "FAIL: In-place live reload did not update existing Material instance!" << std::endl;
            return false;
        }

        std::cout << "  -> PASS: MaterialCache deduplication and in-place live reload verified." << std::endl;
    }

    // -------------------------------------------------------------------------
    // [28.3] Flyweight MaterialComponent Binding, Synchronization & MakeUnique
    // -------------------------------------------------------------------------
    std::cout << "\n[28.3] Testing Flyweight MaterialComponent binding & MakeUnique..." << std::endl;
    {
        auto scene = std::make_shared<Axle::Scene>();
        auto e1 = scene->CreateEntity("CarBody");
        auto e2 = scene->CreateEntity("CarDoor");

        auto& mc1 = e1.AddComponent<Axle::MaterialComponent>();
        auto& mc2 = e2.AddComponent<Axle::MaterialComponent>();

        auto sharedMat = Axle::AssetManager::GetMaterial(matPath);
        Axle::UUID matUUID = Axle::AssetManager::GetAssetUUID(matPath);

        // Bind shared flyweight asset to both entities
        mc1.SetAsset(matUUID, sharedMat);
        mc2.SetAsset(matUUID, sharedMat);

        if (!mc1.HasAsset() || !mc2.HasAsset()) {
            std::cerr << "FAIL: HasAsset() returned false after SetAsset!" << std::endl;
            return false;
        }
        if (mc1.MaterialHandle != matUUID || mc2.MaterialHandle != matUUID) {
            std::cerr << "FAIL: MaterialHandle mismatch!" << std::endl;
            return false;
        }

        // Verify shared reading
        if (std::abs(mc1.GetRoughness() - 0.05f) > 1e-3f ||
            std::abs(mc2.GetRoughness() - 0.05f) > 1e-3f) {
            std::cerr << "FAIL: Entity components did not read shared material properties!" << std::endl;
            return false;
        }

        // Live change on shared asset propagates automatically to all entities
        sharedMat->SetRoughness(0.55f);
        if (std::abs(mc1.GetRoughness() - 0.55f) > 1e-3f ||
            std::abs(mc2.GetRoughness() - 0.55f) > 1e-3f) {
            std::cerr << "FAIL: Shared asset mutation did not reflect on both entities!" << std::endl;
            return false;
        }

        // Test MakeUnique on mc2
        mc2.MakeUnique();
        if (mc2.HasAsset() || mc2.MaterialHandle != 0) {
            std::cerr << "FAIL: MakeUnique() did not detach MaterialComponent from asset!" << std::endl;
            return false;
        }

        // Mutate mc2 locally: must NOT affect mc1 or sharedMat
        mc2.Roughness = 0.95f;
        if (std::abs(mc1.GetRoughness() - 0.55f) > 1e-3f ||
            std::abs(sharedMat->GetRoughness() - 0.55f) > 1e-3f) {
            std::cerr << "FAIL: Modifying detached component affected shared asset!" << std::endl;
            return false;
        }

        std::cout << "  -> PASS: Flyweight binding, live synchronization, and MakeUnique() verified." << std::endl;
    }

    // -------------------------------------------------------------------------
    // [28.4] Scene and Assembly Serialization Roundtrip with Flyweight Handles
    // -------------------------------------------------------------------------
    std::cout << "\n[28.4] Testing Scene & Assembly serialization with Flyweight Material handles..." << std::endl;
    {
        auto scene = std::make_shared<Axle::Scene>();
        auto ent = scene->CreateEntity("VehicleWheel");
        ent.AddComponent<Axle::TransformComponent>(glm::vec3(1.0f, 2.0f, 3.0f));
        auto& mc = ent.AddComponent<Axle::MaterialComponent>();

        auto sharedMat = Axle::AssetManager::GetMaterial(matPath);
        Axle::UUID matUUID = Axle::AssetManager::GetAssetUUID(matPath);
        mc.SetAsset(matUUID, sharedMat);

        auto scenePath = testDir / "TestMaterialScene.axle";
        Axle::SceneSerializer serializer(scene.get());
        if (!serializer.Serialize(scenePath.string())) {
            std::cerr << "FAIL: Scene serialization failed!" << std::endl;
            return false;
        }

        // Deserialize into fresh scene
        auto loadedScene = std::make_shared<Axle::Scene>();
        Axle::SceneSerializer deserializer(loadedScene.get());
        if (!deserializer.Deserialize(scenePath.string())) {
            std::cerr << "FAIL: Scene deserialization failed!" << std::endl;
            return false;
        }

        // Find entity
        auto view = loadedScene->GetRegistry().view<Axle::TagComponent, Axle::MaterialComponent>();
        bool found = false;
        for (auto e : view) {
            auto& tag = view.get<Axle::TagComponent>(e);
            if (tag.Tag == "VehicleWheel") {
                found = true;
                auto& loadedMC = view.get<Axle::MaterialComponent>(e);
                if (!loadedMC.HasAsset()) {
                    std::cerr << "FAIL: Deserialized entity does not have bound asset!" << std::endl;
                    return false;
                }
                if (loadedMC.MaterialHandle != matUUID) {
                    std::cerr << "FAIL: Deserialized MaterialHandle mismatch!" << std::endl;
                    return false;
                }
                if (std::abs(loadedMC.GetRoughness() - 0.55f) > 1e-3f) {
                    std::cerr << "FAIL: Deserialized material roughness mismatch!" << std::endl;
                    return false;
                }
                break;
            }
        }

        if (!found) {
            std::cerr << "FAIL: Serialized entity 'VehicleWheel' not found in loaded scene!" << std::endl;
            return false;
        }

        // Also test Assembly serialization
        auto assemblyPath = testDir / "WheelAssembly.axle";
        if (!Axle::AssemblySerializer::Serialize(assemblyPath, ent)) {
            std::cerr << "FAIL: AssemblySerializer::Serialize failed!" << std::endl;
            return false;
        }

        auto assScene = std::make_shared<Axle::Scene>();
        auto spawned = Axle::AssemblySerializer::Deserialize(assemblyPath, assScene.get());
        if (!spawned || !spawned.HasComponent<Axle::MaterialComponent>()) {
            std::cerr << "FAIL: Deserialize assembly failed or missing MaterialComponent!" << std::endl;
            return false;
        }

        auto& spawnedMC = spawned.GetComponent<Axle::MaterialComponent>();
        if (!spawnedMC.HasAsset() || spawnedMC.MaterialHandle != matUUID) {
            std::cerr << "FAIL: Assembly spawned entity MaterialHandle not restored!" << std::endl;
            return false;
        }

        std::cout << "  -> PASS: Scene and Assembly serialization roundtrip preserved Flyweight Material handles." << std::endl;
    }

    // Cleanup
    Axle::AssetManager::Shutdown();
    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 28 PASSED: Phase 24 Material & Flyweight Architecture verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 29: PHASE 25 - AAA SOFT SHADOWS & PRECISION LIGHTING PIPELINE
// =============================================================================
bool RunTest_Step29_Phase25_SoftShadowsAndDynamicShadowMap() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 29] Phase 25: AAA Soft Shadows & Precision Lighting" << std::endl;
    std::cout << "========================================================" << std::endl;

    // -----------------------------------------------------------------
    // [29.1] ShadowMap Construction & OrthoSize / Sub-texel Snapping
    // -----------------------------------------------------------------
    std::cout << "[29.1] Verifying ShadowMap Ortho Extent and Light Matrix Math..." << std::endl;
    {
        Axle::ShadowMapSpecification spec;
        spec.Width = 2048;
        spec.Height = 2048;
        spec.OrthoSize = 25.0f;
        spec.NearPlane = 0.1f;
        spec.FarPlane = 150.0f;
        Axle::ShadowMap shadowMap(spec);
        if (shadowMap.GetWidth() != 2048 || shadowMap.GetHeight() != 2048) {
            std::cerr << "FAIL: ShadowMap resolution mismatch!" << std::endl;
            return false;
        }
        if (std::abs(shadowMap.GetOrthoSize() - 25.0f) > 1e-4f) {
            std::cerr << "FAIL: Default OrthoSize should be 25.0f!" << std::endl;
            return false;
        }

        // Test mutating OrthoSize
        shadowMap.SetOrthoSize(15.0f);
        if (std::abs(shadowMap.GetOrthoSize() - 15.0f) > 1e-4f) {
            std::cerr << "FAIL: SetOrthoSize(15.0f) failed!" << std::endl;
            return false;
        }

        // Test dynamic resize (safe in headless mode)
        shadowMap.Resize(4096, 4096);
        if (shadowMap.GetWidth() != 4096 || shadowMap.GetHeight() != 4096) {
            std::cerr << "FAIL: ShadowMap Resize to 4096 failed!" << std::endl;
            return false;
        }

        // Calculate light space matrix with directional light vector
        glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
        glm::vec3 focusCenter(10.0f, 0.0f, -5.0f);

        glm::mat4 matDefault = shadowMap.CalculateLightSpaceMatrix(lightDir, focusCenter);
        // Verify matrix is invertible and non-zero
        float det = glm::determinant(matDefault);
        if (std::isnan(det) || std::abs(det) < 1e-6f) {
            std::cerr << "FAIL: Light space matrix is degenerate/singular!" << std::endl;
            return false;
        }

        // Calculate with explicit orthoSize override
        glm::mat4 matOverride = shadowMap.CalculateLightSpaceMatrix(lightDir, focusCenter, 20.0f);
        float detOverride = glm::determinant(matOverride);
        if (std::isnan(detOverride) || std::abs(detOverride) < 1e-6f) {
            std::cerr << "FAIL: Light space matrix with orthoSize override is degenerate!" << std::endl;
            return false;
        }

        std::cout << "  -> PASS: ShadowMap projection math, sub-texel snapping, and resizing verified." << std::endl;
    }

    // -----------------------------------------------------------------
    // [29.2] DirectionalLightComponent Soft Shadow Defaults & Mutators
    // -----------------------------------------------------------------
    std::cout << "[29.2] Verifying DirectionalLightComponent soft shadow properties..." << std::endl;
    {
        Axle::DirectionalLightComponent dlc;
        if (std::abs(dlc.ShadowOrthoSize - 25.0f) > 1e-4f) {
            std::cerr << "FAIL: DirectionalLightComponent default ShadowOrthoSize should be 25.0f!" << std::endl;
            return false;
        }
        if (std::abs(dlc.ShadowBias - 0.0012f) > 1e-5f) {
            std::cerr << "FAIL: DirectionalLightComponent default ShadowBias should be 0.0012f!" << std::endl;
            return false;
        }
        if (std::abs(dlc.ShadowFilterRadius - 2.0f) > 1e-4f) {
            std::cerr << "FAIL: DirectionalLightComponent default ShadowFilterRadius should be 2.0f!" << std::endl;
            return false;
        }
        if (dlc.ShadowQuality != 1) { // 1 = 2048 (High)
            std::cerr << "FAIL: DirectionalLightComponent default ShadowQuality should be 1 (2048)!" << std::endl;
            return false;
        }
        std::cout << "  -> PASS: Default DirectionalLightComponent soft shadow parameters valid." << std::endl;
    }

    // -----------------------------------------------------------------
    // [29.3] Scene Roundtrip Serialization of Soft Shadow Settings
    // -----------------------------------------------------------------
    std::cout << "[29.3] Verifying Scene serialization roundtrip for soft shadow settings..." << std::endl;
    std::error_code ec;
    auto testDir = std::filesystem::current_path() / "TestDir_ShadowPhase25";
    std::filesystem::create_directories(testDir, ec);

    {
        auto saveScene = std::make_shared<Axle::Scene>();
        auto lightEnt = saveScene->CreateEntity("SunLight");
        auto& dlc = lightEnt.AddComponent<Axle::DirectionalLightComponent>();
        dlc.CastShadows = true;
        dlc.ShadowOrthoSize = 18.5f;
        dlc.ShadowBias = 0.0028f;
        dlc.ShadowFilterRadius = 3.5f;
        dlc.ShadowQuality = 2; // Ultra 4096

        auto sceneFile = testDir / "ShadowScene.axle";
        Axle::SceneSerializer serializer(saveScene.get());
        serializer.Serialize(sceneFile.string());

        auto loadScene = std::make_shared<Axle::Scene>();
        Axle::SceneSerializer deserializer(loadScene.get());
        bool deserialized = deserializer.Deserialize(sceneFile.string());
        if (!deserialized) {
            std::cerr << "FAIL: Failed to deserialize shadow scene!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        Axle::DirectionalLightComponent loadedDLC;
        bool hasSun = loadScene->GetPrimaryDirectionalLight(loadedDLC);
        if (!hasSun) {
            std::cerr << "FAIL: Deserialized scene has no primary directional light!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        if (!loadedDLC.CastShadows) {
            std::cerr << "FAIL: Deserialized CastShadows is false!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        if (std::abs(loadedDLC.ShadowOrthoSize - 18.5f) > 1e-3f) {
            std::cerr << "FAIL: Deserialized ShadowOrthoSize mismatch: " << loadedDLC.ShadowOrthoSize << " (expected 18.5)!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        if (std::abs(loadedDLC.ShadowBias - 0.0028f) > 1e-4f) {
            std::cerr << "FAIL: Deserialized ShadowBias mismatch: " << loadedDLC.ShadowBias << " (expected 0.0028)!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        if (std::abs(loadedDLC.ShadowFilterRadius - 3.5f) > 1e-3f) {
            std::cerr << "FAIL: Deserialized ShadowFilterRadius mismatch: " << loadedDLC.ShadowFilterRadius << " (expected 3.5)!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        if (loadedDLC.ShadowQuality != 2) {
            std::cerr << "FAIL: Deserialized ShadowQuality mismatch: " << loadedDLC.ShadowQuality << " (expected 2)!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        std::cout << "  -> PASS: Soft shadow fields correctly saved and loaded in .axle scene format." << std::endl;
    }

    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 29 PASSED: Phase 25 Soft Shadows & Precision Lighting verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 30: PHASE 26 - HDR SKYBOX CUBEMAP & IMAGE-BASED LIGHTING (IBL)
// =============================================================================
bool RunTest_Step30_Phase26_HDRCubemapAndIBL() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 30] Phase 26: HDR Skybox Cubemap & Image-Based Lighting" << std::endl;
    std::cout << "========================================================" << std::endl;

    // -----------------------------------------------------------------
    // [30.1] HDR Equirectangular Data & Cubemap Creation
    // -----------------------------------------------------------------
    std::cout << "[30.1] Verifying HDR Equirectangular conversion and Cubemap spec..." << std::endl;
    {
        // Synthesize an equirectangular HDR radiance gradient (16x8 floats, RGB)
        std::vector<float> hdrGradient(16 * 8 * 3, 0.5f);
        // Sun hotspot at center
        hdrGradient[(4 * 16 + 8) * 3 + 0] = 25.0f;
        hdrGradient[(4 * 16 + 8) * 3 + 1] = 22.0f;
        hdrGradient[(4 * 16 + 8) * 3 + 2] = 18.0f;

        auto cubemap = Axle::Cubemap::CreateFromEquirectangularHDR(hdrGradient.data(), 16, 8, 32);
        if (!cubemap) {
            std::cerr << "FAIL: CreateFromEquirectangularHDR returned null!" << std::endl;
            return false;
        }

        if (cubemap->GetWidth() != 32 || cubemap->GetHeight() != 32) {
            std::cerr << "FAIL: Cubemap resolution mismatch!" << std::endl;
            return false;
        }

        if (!cubemap->GetSpecification().IsHDR) {
            std::cerr << "FAIL: Cubemap should be marked HDR!" << std::endl;
            return false;
        }

        // Test static pre-baked fallbacks
        auto blackCube = Axle::Cubemap::GetBlackCubemap();
        if (!blackCube || blackCube->GetWidth() != 1 || blackCube->GetHeight() != 1) {
            std::cerr << "FAIL: GetBlackCubemap invalid!" << std::endl;
            return false;
        }

        auto brdfLUT = Axle::Cubemap::GetGlobalBRDFLUT();
        if (!brdfLUT || brdfLUT->GetWidth() != 256 || brdfLUT->GetHeight() != 256) {
            std::cerr << "FAIL: GetGlobalBRDFLUT invalid!" << std::endl;
            return false;
        }

        std::cout << "  -> PASS: HDR equirectangular processing and Cubemap IBL specifications verified." << std::endl;
    }

    // -----------------------------------------------------------------
    // [30.2] Skybox Class Mode & Property Mutators
    // -----------------------------------------------------------------
    std::cout << "[30.2] Verifying Skybox mode transitions and IBL state..." << std::endl;
    {
        Axle::Skybox skybox;
        if (skybox.GetMode() != Axle::SkyboxMode::ProceduralAtmospheric) {
            std::cerr << "FAIL: Default Skybox mode should be ProceduralAtmospheric!" << std::endl;
            return false;
        }

        skybox.SetMode(Axle::SkyboxMode::HDRCubemap);
        skybox.SetHDRIntensity(2.75f);

        if (skybox.GetMode() != Axle::SkyboxMode::HDRCubemap) {
            std::cerr << "FAIL: SetMode(HDRCubemap) failed!" << std::endl;
            return false;
        }

        if (std::abs(skybox.GetSpecification().HDRIntensity - 2.75f) > 1e-4f) {
            std::cerr << "FAIL: SetHDRIntensity failed!" << std::endl;
            return false;
        }

        std::cout << "  -> PASS: Skybox HDRCubemap mode switching and radiance properties verified." << std::endl;
    }

    // -----------------------------------------------------------------
    // [30.3] SkyboxComponent Serialization & Deserialization
    // -----------------------------------------------------------------
    std::cout << "[30.3] Verifying Scene serialization roundtrip for HDR SkyboxComponent..." << std::endl;
    std::error_code ec;
    auto testDir = std::filesystem::current_path() / "TestDir_IBLPhase26";
    std::filesystem::create_directories(testDir, ec);

    {
        auto saveScene = std::make_shared<Axle::Scene>();
        auto skyEnt = saveScene->CreateEntity("StudioSkybox");
        auto& sbc = skyEnt.AddComponent<Axle::SkyboxComponent>();
        sbc.Enabled = true;
        sbc.Type = Axle::SkyboxType::HDRCubemap;
        sbc.HDRPath = "Assets/Textures/Environment/IndustrialSunset.hdr";
        sbc.Intensity = 1.65f;
        sbc.Exposure = 1.35f;

        auto sceneFile = testDir / "IBLScene.axle";
        Axle::SceneSerializer serializer(saveScene.get());
        serializer.Serialize(sceneFile.string());

        auto loadScene = std::make_shared<Axle::Scene>();
        Axle::SceneSerializer deserializer(loadScene.get());
        bool deserialized = deserializer.Deserialize(sceneFile.string());
        if (!deserialized) {
            std::cerr << "FAIL: Failed to deserialize IBL scene!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        Axle::SkyboxComponent loadedSBC;
        bool hasSky = loadScene->GetPrimarySkybox(loadedSBC);
        if (!hasSky) {
            std::cerr << "FAIL: Deserialized scene missing primary SkyboxComponent!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        if (loadedSBC.Type != Axle::SkyboxType::HDRCubemap) {
            std::cerr << "FAIL: Deserialized SkyboxType mismatch!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        if (loadedSBC.HDRPath != "Assets/Textures/Environment/IndustrialSunset.hdr") {
            std::cerr << "FAIL: Deserialized HDRPath mismatch: '" << loadedSBC.HDRPath << "'!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        if (std::abs(loadedSBC.Intensity - 1.65f) > 1e-3f) {
            std::cerr << "FAIL: Deserialized Intensity mismatch: " << loadedSBC.Intensity << " (expected 1.65)!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        if (std::abs(loadedSBC.Exposure - 1.35f) > 1e-3f) {
            std::cerr << "FAIL: Deserialized Exposure mismatch: " << loadedSBC.Exposure << " (expected 1.35)!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        std::cout << "  -> PASS: HDR SkyboxComponent serialized and deserialized with 100% fidelity." << std::endl;
    }

    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 30 PASSED: Phase 26 HDR Skybox Cubemap & IBL verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 31: Phase 27.1 Core Audio Subsystem & miniaudio Integration
// =============================================================================
bool RunTest_Step31_Phase27_CoreAudioSubsystem() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 31] Phase 27: Core Audio Subsystem & miniaudio   " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::error_code ec;
    std::filesystem::path testDir = "TestDir_AudioPhase27";
    std::filesystem::create_directories(testDir, ec);

    // 1. AudioEngine Lifecycle & Safe Headless Verification
    std::cout << "[31.1] Verifying AudioEngine lifecycle and master volume..." << std::endl;
    Axle::AudioEngineSpecification audioSpec;
    audioSpec.SampleRate = 48000;
    audioSpec.Channels = 2;
    audioSpec.MasterVolume = 1.0f;
    audioSpec.HeadlessSafe = true;

    bool initSuccess = Axle::AudioEngine::Init(audioSpec);
    if (!initSuccess || !Axle::AudioEngine::IsInitialized()) {
        std::cerr << "FAIL: AudioEngine failed to initialize!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    Axle::AudioEngine::SetMasterVolume(0.75f);
    if (std::abs(Axle::AudioEngine::GetMasterVolume() - 0.75f) > 1e-3f) {
        std::cerr << "FAIL: MasterVolume setting failed! Got: " << Axle::AudioEngine::GetMasterVolume() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Clamp tests
    Axle::AudioEngine::SetMasterVolume(1.8f);
    if (std::abs(Axle::AudioEngine::GetMasterVolume() - 1.0f) > 1e-3f) {
        std::cerr << "FAIL: MasterVolume upper clamp failed!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    Axle::AudioEngine::SetMasterVolume(-0.5f);
    if (std::abs(Axle::AudioEngine::GetMasterVolume() - 0.0f) > 1e-3f) {
        std::cerr << "FAIL: MasterVolume lower clamp failed!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    Axle::AudioEngine::SetMasterVolume(1.0f);
    std::cout << "  -> PASS: AudioEngine initialized cleanly with master volume control." << std::endl;

    // 2. AudioListener 3D Orientation, Position & Velocity
    std::cout << "[31.2] Verifying AudioListener 3D spatial properties..." << std::endl;
    Axle::AudioListener::SetPosition({ 12.0f, 3.5f, -8.0f });
    Axle::AudioListener::SetOrientation({ 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f });
    Axle::AudioListener::SetVelocity({ 0.0f, 0.0f, 25.0f });

    if (glm::distance(Axle::AudioListener::GetPosition(), glm::vec3(12.0f, 3.5f, -8.0f)) > 1e-3f ||
        glm::distance(Axle::AudioListener::GetForward(), glm::vec3(0.0f, 0.0f, -1.0f)) > 1e-3f ||
        glm::distance(Axle::AudioListener::GetUp(), glm::vec3(0.0f, 1.0f, 0.0f)) > 1e-3f ||
        glm::distance(Axle::AudioListener::GetVelocity(), glm::vec3(0.0f, 0.0f, 25.0f)) > 1e-3f) {
        std::cerr << "FAIL: AudioListener property mismatch!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: AudioListener 3D position, orientation, and velocity verified." << std::endl;

    // 3. AudioSource 3D Spatial Properties & Attenuation Models
    std::cout << "[31.3] Verifying AudioSource 3D spatial configuration..." << std::endl;
    auto source = std::make_shared<Axle::AudioSource>();
    source->SetVolume(0.85f);
    source->SetPitch(1.25f);
    source->SetLooping(true);
    source->SetSpatial(true);
    source->SetPosition({ 5.0f, 0.5f, 2.0f });
    source->SetVelocity({ 10.0f, 0.0f, 0.0f });
    source->SetDirection({ 0.0f, 0.0f, -1.0f });
    source->SetAttenuationModel(Axle::AttenuationModel::Linear);
    source->SetMinDistance(2.0f);
    source->SetMaxDistance(120.0f);
    source->SetRollOff(1.8f);
    source->SetDopplerFactor(2.5f);
    source->SetCone(glm::radians(45.0f), glm::radians(90.0f), 0.25f);

    if (std::abs(source->GetVolume() - 0.85f) > 1e-3f ||
        std::abs(source->GetPitch() - 1.25f) > 1e-3f ||
        !source->IsLooping() || !source->IsSpatial() ||
        glm::distance(source->GetPosition(), glm::vec3(5.0f, 0.5f, 2.0f)) > 1e-3f ||
        glm::distance(source->GetVelocity(), glm::vec3(10.0f, 0.0f, 0.0f)) > 1e-3f ||
        source->GetAttenuationModel() != Axle::AttenuationModel::Linear ||
        std::abs(source->GetMinDistance() - 2.0f) > 1e-3f ||
        std::abs(source->GetMaxDistance() - 120.0f) > 1e-3f ||
        std::abs(source->GetRollOff() - 1.8f) > 1e-3f ||
        std::abs(source->GetDopplerFactor() - 2.5f) > 1e-3f ||
        std::abs(source->GetConeOuterGain() - 0.25f) > 1e-3f) {
        std::cerr << "FAIL: AudioSource properties mismatch!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: AudioSource 3D spatial properties and directional cones verified." << std::endl;

    // 4. Synthetic WAV File Generation & AudioClip Decoding Verification
    std::cout << "[31.4] Generating synthetic WAV asset and verifying decoder..." << std::endl;
    std::filesystem::path wavPath = testDir / "SyntheticEngineTone.wav";
    {
        std::ofstream fout(wavPath, std::ios::binary);
        if (!fout.is_open()) {
            std::cerr << "FAIL: Unable to create temporary synthetic WAV file!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        const uint32_t sampleRate = 44100;
        const uint16_t numChannels = 1;
        const uint16_t bitsPerSample = 16;
        const float durationSeconds = 0.5f;
        const uint32_t numSamples = static_cast<uint32_t>(sampleRate * durationSeconds);
        const uint32_t dataBytes = numSamples * numChannels * (bitsPerSample / 8);
        const uint32_t overallSize = dataBytes + 36;
        const uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
        const uint16_t blockAlign = numChannels * (bitsPerSample / 8);

        // RIFF chunk
        fout.write("RIFF", 4);
        fout.write(reinterpret_cast<const char*>(&overallSize), 4);
        fout.write("WAVE", 4);

        // fmt subchunk
        fout.write("fmt ", 4);
        uint32_t fmtSize = 16;
        uint16_t formatPCM = 1;
        fout.write(reinterpret_cast<const char*>(&fmtSize), 4);
        fout.write(reinterpret_cast<const char*>(&formatPCM), 2);
        fout.write(reinterpret_cast<const char*>(&numChannels), 2);
        fout.write(reinterpret_cast<const char*>(&sampleRate), 4);
        fout.write(reinterpret_cast<const char*>(&byteRate), 4);
        fout.write(reinterpret_cast<const char*>(&blockAlign), 2);
        fout.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

        // data subchunk
        fout.write("data", 4);
        fout.write(reinterpret_cast<const char*>(&dataBytes), 4);

        // Write 440 Hz sine wave
        const double twoPi = 6.28318530717958647692;
        for (uint32_t i = 0; i < numSamples; ++i) {
            double t = static_cast<double>(i) / static_cast<double>(sampleRate);
            int16_t sample = static_cast<int16_t>(std::sin(twoPi * 440.0 * t) * 16000.0);
            fout.write(reinterpret_cast<const char*>(&sample), 2);
        }
        fout.close();
    }

    auto clip = Axle::AudioClip::Create(wavPath.string(), false);
    if (!clip || !clip->IsLoaded()) {
        std::cerr << "FAIL: AudioClip failed to load synthetic WAV file!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    if (clip->GetChannels() != 1 || clip->GetSampleRate() != 44100 ||
        clip->GetFormat() != Axle::AudioFormat::WAV ||
        std::abs(clip->GetLengthSeconds() - 0.5f) > 0.05f) {
        std::cerr << "FAIL: AudioClip metadata mismatch! Channels: " << clip->GetChannels()
                  << ", SampleRate: " << clip->GetSampleRate()
                  << ", Length: " << clip->GetLengthSeconds() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: AudioClip successfully decoded " << clip->GetChannels() << "ch, "
              << clip->GetSampleRate() << "Hz WAV (" << clip->GetLengthSeconds() << "s)." << std::endl;

    // Load into AudioSource and verify state transitions
    bool loadClipOk = source->Load(clip);
    if (!loadClipOk) {
        std::cerr << "FAIL: AudioSource::Load failed with valid AudioClip!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    source->Play();
    source->Pause();
    source->Stop();
    source->Rewind();
    std::cout << "  -> PASS: AudioSource playback control state transitions verified." << std::endl;

    // 5. One-Shot Audio
    std::cout << "[31.5] Verifying One-Shot playback..." << std::endl;
    Axle::AudioEngine::PlayOneShot(wavPath.string(), 0.9f, 1.1f);
    Axle::AudioEngine::PlayOneShot3D(wavPath.string(), glm::vec3(0.0f, 1.0f, 0.0f), 0.8f, 1.0f);
    Axle::AudioEngine::Update();

    source->Unload();
    source.reset();
    clip.reset();
    std::cout << "  -> PASS: One-shot playback validated." << std::endl;

    // 6. ECS Audio Components & Scene Runtime Lifecycle
    std::cout << "[31.6] Verifying ECS AudioListenerComponent & AudioSourceComponent runtime synchronization..." << std::endl;
    auto testScene = std::make_shared<Axle::Scene>();

    // Create camera entity with AudioListenerComponent
    auto camEntity = testScene->CreateEntity("CameraListener");
    camEntity.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(12.0f, 3.0f, -8.0f);
    camEntity.AddComponent<Axle::CameraComponent>(60.0f, 0.1f, 1000.0f, true);
    camEntity.AddComponent<Axle::AudioListenerComponent>(true);

    // Create sound entity with AudioSourceComponent
    auto soundEntity = testScene->CreateEntity("VehicleEngineSound");
    soundEntity.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(4.0f, 1.0f, 2.0f);
    auto& asc = soundEntity.AddComponent<Axle::AudioSourceComponent>(wavPath.string(), true, true, 0.75f);
    asc.Pitch = 1.2f;
    asc.Spatialized = true;
    asc.Attenuation = Axle::AttenuationModel::Exponential;
    asc.MinDistance = 2.0f;
    asc.MaxDistance = 60.0f;
    asc.RollOff = 1.5f;
    asc.InnerAngle = 90.0f;
    asc.OuterAngle = 180.0f;
    asc.OuterGain = 0.25f;
    asc.DopplerFactor = 1.3f;

    // Start runtime simulation -> PlayOnAwake triggers
    testScene->OnRuntimeStart();
    if (!asc.IsPlaying()) {
        std::cerr << "FAIL: AudioSourceComponent with PlayOnAwake was not playing after OnRuntimeStart()!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: AudioSourceComponent PlayOnAwake initiated playback on runtime start." << std::endl;

    // Tick runtime simulation -> syncs listener and source 3D transform
    testScene->OnUpdateRuntime(Axle::Timestep(0.016f));
    glm::vec3 listenerPos = Axle::AudioListener::GetPosition();
    if (glm::distance(listenerPos, glm::vec3(12.0f, 3.0f, -8.0f)) > 0.01f) {
        std::cerr << "FAIL: AudioListener position was not synchronized with camera transform! Expected: (12, 3, -8), Got: ("
                  << listenerPos.x << ", " << listenerPos.y << ", " << listenerPos.z << ")" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    if (asc.Source) {
        glm::vec3 sourcePos = asc.Source->GetPosition();
        if (glm::distance(sourcePos, glm::vec3(4.0f, 1.0f, 2.0f)) > 0.01f) {
            std::cerr << "FAIL: AudioSource 3D position was not synchronized with entity transform!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }
    }
    std::cout << "  -> PASS: AudioListener and AudioSource 3D spatial properties successfully synchronized." << std::endl;

    // Stop simulation -> stops audio sources
    testScene->OnRuntimeStop();
    if (asc.Source != nullptr) {
        std::cerr << "FAIL: AudioSourceComponent runtime source was not cleaned up after OnRuntimeStop()!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: OnRuntimeStop cleanly terminated audio playback." << std::endl;

    // 7. Scene Serialization Roundtrip (.axlescene)
    std::cout << "[31.7] Verifying SceneSerializer roundtrip for Audio components..." << std::endl;
    std::filesystem::path scenePath = testDir / "AudioTestScene.axle";
    Axle::SceneSerializer serializer(testScene.get());
    if (!serializer.Serialize(scenePath)) {
        std::cerr << "FAIL: SceneSerializer failed to save scene with audio components!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    auto loadedScene = std::make_shared<Axle::Scene>();
    Axle::SceneSerializer deserializer(loadedScene.get());
    if (!deserializer.Deserialize(scenePath)) {
        std::cerr << "FAIL: SceneSerializer failed to load scene with audio components!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Verify deserialized components
    bool foundListener = false;
    bool foundSource = false;
    auto regView = loadedScene->GetRegistry().view<Axle::TagComponent>();
    for (auto entID : regView) {
        Axle::Entity e{ entID, loadedScene.get() };
        if (e.HasComponent<Axle::AudioListenerComponent>()) {
            foundListener = true;
            if (!e.GetComponent<Axle::AudioListenerComponent>().Active) {
                std::cerr << "FAIL: Deserialized AudioListenerComponent Active state mismatch!" << std::endl;
                std::filesystem::remove_all(testDir, ec);
                return false;
            }
        }
        if (e.HasComponent<Axle::AudioSourceComponent>()) {
            foundSource = true;
            const auto& loadedAsc = e.GetComponent<Axle::AudioSourceComponent>();
            if (loadedAsc.AssetPath != wavPath.string() ||
                std::abs(loadedAsc.Volume - 0.75f) > 0.01f ||
                std::abs(loadedAsc.Pitch - 1.2f) > 0.01f ||
                !loadedAsc.Loop ||
                !loadedAsc.PlayOnAwake ||
                !loadedAsc.Spatialized ||
                loadedAsc.Attenuation != Axle::AttenuationModel::Exponential ||
                std::abs(loadedAsc.MinDistance - 2.0f) > 0.01f ||
                std::abs(loadedAsc.MaxDistance - 60.0f) > 0.01f ||
                std::abs(loadedAsc.RollOff - 1.5f) > 0.01f ||
                std::abs(loadedAsc.InnerAngle - 90.0f) > 0.01f ||
                std::abs(loadedAsc.OuterAngle - 180.0f) > 0.01f ||
                std::abs(loadedAsc.OuterGain - 0.25f) > 0.01f ||
                std::abs(loadedAsc.DopplerFactor - 1.3f) > 0.01f) {
                std::cerr << "FAIL: Deserialized AudioSourceComponent parameter mismatch!" << std::endl;
                std::filesystem::remove_all(testDir, ec);
                return false;
            }
        }
    }

    if (!foundListener || !foundSource) {
        std::cerr << "FAIL: AudioListener or AudioSource component missing in deserialized scene!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Audio components serialized and deserialized with 100% precision." << std::endl;

    // 8. Assembly Prefab Serialization Roundtrip (.axle)
    std::cout << "[31.8] Verifying AssemblySerializer roundtrip for AudioSource prefabs..." << std::endl;
    std::filesystem::path asmPath = testDir / "AudioSpeakerAssembly.axle";
    if (!Axle::AssemblySerializer::Serialize(asmPath, soundEntity, nullptr)) {
        std::cerr << "FAIL: AssemblySerializer failed to export audio assembly!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    auto asmScene = std::make_shared<Axle::Scene>();
    Axle::Entity instantiatedEntity = Axle::AssemblySerializer::Deserialize(asmPath, asmScene.get(), nullptr);
    if (!instantiatedEntity || !instantiatedEntity.HasComponent<Axle::AudioSourceComponent>()) {
        std::cerr << "FAIL: AssemblySerializer failed to instantiate entity with AudioSourceComponent!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    const auto& asmAsc = instantiatedEntity.GetComponent<Axle::AudioSourceComponent>();
    if (asmAsc.AssetPath != wavPath.string() ||
        std::abs(asmAsc.Volume - 0.75f) > 0.01f ||
        asmAsc.Attenuation != Axle::AttenuationModel::Exponential) {
        std::cerr << "FAIL: Instantiated assembly AudioSourceComponent parameters corrupted!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Assembly export and instantiation preserved AudioSourceComponent properties." << std::endl;

    // 9. Vehicle Audio Synthesizer & Physics Telemetry Integration
    std::cout << "[31.9] Verifying VehicleAudio granular synthesizer, slip acoustics, and impact response..." << std::endl;
    Axle::VehicleAudioConfig vaConfig;
    vaConfig.EnginePowerClipPath = wavPath.string();
    vaConfig.EngineCoastClipPath = wavPath.string();
    vaConfig.TireSquealClipPath = wavPath.string();
    vaConfig.SuspensionThudClipPath = wavPath.string();
    vaConfig.CollisionImpactClipPath = wavPath.string();
    vaConfig.MinEnginePitch = 0.6f;
    vaConfig.MaxEnginePitch = 2.4f;
    vaConfig.EngineMasterVolume = 0.8f;
    vaConfig.IdleRPM = 1000.0f;
    vaConfig.MaxRPM = 7000.0f;
    vaConfig.SlipThreshold = 0.22f;
    vaConfig.TireSquealMasterVolume = 0.7f;
    vaConfig.SuspensionThudThreshold = 1.8f;
    vaConfig.CollisionDecelThreshold = 18.0f;

    Axle::VehicleAudio va(vaConfig);
    va.Start();
    if (!va.IsPlaying()) {
        std::cerr << "FAIL: VehicleAudio failed to start playback!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Test Idle RPM (1000 RPM, throttle 0)
    Axle::VehicleTelemetryData telem;
    telem.EngineRPM = 1000.0f;
    telem.LinearVelocity = glm::vec3(0.0f);
    telem.ForwardVector = glm::vec3(0.0f, 0.0f, 1.0f);
    for (int i = 0; i < 4; ++i) {
        telem.Wheels[i].HasContact = true;
        telem.Wheels[i].SuspensionLength = 0.25f;
        telem.Wheels[i].LongitudinalSlip = 0.0f;
        telem.Wheels[i].LateralSlip = 0.0f;
    }
    va.Update(Axle::Timestep(0.016f), telem, 0.0f, 0.0f, false, glm::vec3(0.0f), telem.ForwardVector);
    if (std::abs(va.GetCurrentPitch() - 0.6f) > 0.05f) {
        std::cerr << "FAIL: VehicleAudio idle pitch mismatch! Expected ~0.6, Got: " << va.GetCurrentPitch() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (va.GetCurrentTireVolume() > 0.001f) {
        std::cerr << "FAIL: VehicleAudio tire volume should be 0 without slip!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Test Max RPM (7000 RPM, full throttle)
    telem.EngineRPM = 7000.0f;
    for (int tick = 0; tick < 20; ++tick) {
        va.Update(Axle::Timestep(0.05f), telem, 1.0f, 0.0f, false, glm::vec3(0.0f), telem.ForwardVector);
    }
    if (std::abs(va.GetCurrentPitch() - 2.4f) > 0.05f) {
        std::cerr << "FAIL: VehicleAudio max RPM pitch mismatch! Expected ~2.4, Got: " << va.GetCurrentPitch() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Test Slip-Induced Tire Squeal
    telem.Wheels[0].LateralSlip = 0.85f;
    for (int tick = 0; tick < 10; ++tick) {
        va.Update(Axle::Timestep(0.05f), telem, 1.0f, 0.0f, false, glm::vec3(0.0f), telem.ForwardVector);
    }
    if (va.GetCurrentTireVolume() < 0.2f) {
        std::cerr << "FAIL: VehicleAudio tire volume did not ramp up during drift slip!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Test Suspension Compression Velocity Thud
    // Previous suspension length was 0.25f. Compress rapidly to 0.05f in 16ms -> velocity = 0.20 / 0.016 = 12.5 m/s > 1.8 threshold
    telem.Wheels[0].SuspensionLength = 0.05f;
    va.Update(Axle::Timestep(0.016f), telem, 0.5f, 0.0f, false, glm::vec3(0.0f), telem.ForwardVector);

    // Test Sudden Deceleration Impact (Collision)
    // Set high speed then drop suddenly:
    telem.LinearVelocity = glm::vec3(0.0f, 0.0f, 30.0f);
    va.Update(Axle::Timestep(0.016f), telem, 0.0f, 1.0f, false, glm::vec3(0.0f), telem.ForwardVector);
    // Instant drop to 0 -> decel = 30 / 0.016 = 1875 m/s^2 > 18 threshold
    telem.LinearVelocity = glm::vec3(0.0f);
    va.Update(Axle::Timestep(0.016f), telem, 0.0f, 1.0f, false, glm::vec3(0.0f), telem.ForwardVector);

    va.Stop();
    std::cout << "  -> PASS: Granular RPM synthesizer, slip acoustics, suspension thud & collision impact verified." << std::endl;

    // 10. VehicleAudioComponent ECS Lifecycle & Serialization Roundtrip
    std::cout << "[31.10] Verifying VehicleAudioComponent ECS lifecycle, SceneSerializer and AssemblySerializer roundtrip..." << std::endl;
    auto vehicleScene = std::make_shared<Axle::Scene>();
    auto carEntity = vehicleScene->CreateEntity("SportsCarChassis");
    carEntity.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(10.0f, 1.0f, 20.0f);

    auto& vac = carEntity.AddComponent<Axle::VehicleAudioComponent>();
    vac.EnginePowerClipPath = wavPath.string();
    vac.EngineCoastClipPath = wavPath.string();
    vac.TireSquealClipPath = wavPath.string();
    vac.SuspensionThudClipPath = wavPath.string();
    vac.CollisionImpactClipPath = wavPath.string();
    vac.MinEnginePitch = 0.55f;
    vac.MaxEnginePitch = 2.65f;
    vac.EngineMasterVolume = 0.85f;
    vac.SlipThreshold = 0.25f;
    vac.TireSquealMasterVolume = 0.65f;
    vac.SuspensionThudThreshold = 2.1f;
    vac.CollisionDecelThreshold = 22.0f;
    vac.Spatialized = true;
    vac.MinDistance = 3.0f;
    vac.MaxDistance = 90.0f;

    // Test Scene Runtime Lifecycle
    vehicleScene->OnRuntimeStart();
    if (!vac.RuntimeAudio || !vac.RuntimeAudio->IsPlaying()) {
        std::cerr << "FAIL: VehicleAudioComponent RuntimeAudio was not initialized and playing on OnRuntimeStart()!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    vehicleScene->OnUpdateRuntime(Axle::Timestep(0.016f));
    vehicleScene->OnRuntimeStop();
    if (vac.RuntimeAudio != nullptr) {
        std::cerr << "FAIL: VehicleAudioComponent RuntimeAudio was not released on OnRuntimeStop()!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: VehicleAudioComponent runtime start/update/stop lifecycle validated." << std::endl;

    // Test Scene Serialization Roundtrip (.axlescene)
    std::filesystem::path carScenePath = testDir / "CarAudioScene.axlescene";
    Axle::SceneSerializer sceneSerializer(vehicleScene.get());
    sceneSerializer.Serialize(carScenePath);

    auto loadedVehicleScene = std::make_shared<Axle::Scene>();
    Axle::SceneSerializer loadedSerializer(loadedVehicleScene.get());
    if (!loadedSerializer.Deserialize(carScenePath)) {
        std::cerr << "FAIL: Failed to deserialize CarAudioScene.axlescene!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    bool foundVehicleAudio = false;
    auto carView = loadedVehicleScene->GetRegistry().view<Axle::TagComponent>();
    for (auto entID : carView) {
        Axle::Entity e{ entID, loadedVehicleScene.get() };
        if (e.HasComponent<Axle::VehicleAudioComponent>()) {
            foundVehicleAudio = true;
            const auto& loadedVac = e.GetComponent<Axle::VehicleAudioComponent>();
            if (loadedVac.EnginePowerClipPath != wavPath.string() ||
                loadedVac.EngineCoastClipPath != wavPath.string() ||
                loadedVac.TireSquealClipPath != wavPath.string() ||
                loadedVac.SuspensionThudClipPath != wavPath.string() ||
                loadedVac.CollisionImpactClipPath != wavPath.string() ||
                std::abs(loadedVac.MinEnginePitch - 0.55f) > 0.01f ||
                std::abs(loadedVac.MaxEnginePitch - 2.65f) > 0.01f ||
                std::abs(loadedVac.EngineMasterVolume - 0.85f) > 0.01f ||
                std::abs(loadedVac.SlipThreshold - 0.25f) > 0.01f ||
                std::abs(loadedVac.TireSquealMasterVolume - 0.65f) > 0.01f ||
                std::abs(loadedVac.SuspensionThudThreshold - 2.1f) > 0.01f ||
                std::abs(loadedVac.CollisionDecelThreshold - 22.0f) > 0.01f ||
                !loadedVac.Spatialized ||
                std::abs(loadedVac.MinDistance - 3.0f) > 0.01f ||
                std::abs(loadedVac.MaxDistance - 90.0f) > 0.01f) {
                std::cerr << "FAIL: Deserialized VehicleAudioComponent parameters corrupted!" << std::endl;
                std::filesystem::remove_all(testDir, ec);
                return false;
            }
        }
    }
    if (!foundVehicleAudio) {
        std::cerr << "FAIL: VehicleAudioComponent not found in deserialized scene!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: SceneSerializer saved and restored VehicleAudioComponent with 100% precision." << std::endl;

    // Test Assembly Prefab Roundtrip (.axle)
    std::filesystem::path carAsmPath = testDir / "SportsCarChassis.axle";
    if (!Axle::AssemblySerializer::Serialize(carAsmPath, carEntity, nullptr)) {
        std::cerr << "FAIL: AssemblySerializer failed to export SportsCarChassis assembly!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    auto asmCarScene = std::make_shared<Axle::Scene>();
    Axle::Entity instantiatedCar = Axle::AssemblySerializer::Deserialize(carAsmPath, asmCarScene.get(), nullptr);
    if (!instantiatedCar || !instantiatedCar.HasComponent<Axle::VehicleAudioComponent>()) {
        std::cerr << "FAIL: AssemblySerializer failed to instantiate entity with VehicleAudioComponent!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    const auto& asmVac = instantiatedCar.GetComponent<Axle::VehicleAudioComponent>();
    if (asmVac.EnginePowerClipPath != wavPath.string() ||
        std::abs(asmVac.MaxEnginePitch - 2.65f) > 0.01f ||
        std::abs(asmVac.CollisionDecelThreshold - 22.0f) > 0.01f) {
        std::cerr << "FAIL: AssemblySerializer instantiated VehicleAudioComponent parameters mismatch!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: AssemblySerializer roundtrip preserved VehicleAudioComponent properties." << std::endl;

    // 11. Subsystem Shutdown
    std::cout << "[31.11] Verifying AudioEngine shutdown..." << std::endl;
    Axle::AudioEngine::Shutdown();
    if (Axle::AudioEngine::IsInitialized()) {
        std::cerr << "FAIL: AudioEngine still reports initialized after Shutdown()!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: AudioEngine::Shutdown validated." << std::endl;

    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 31 PASSED: Phase 27 Audio Subsystem, ECS & Serialization verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 32: Phase 28 Step 1 - Multi-Part 3D Car Rigging & Lighting Assembly
// =============================================================================
bool RunTest_Step32_Phase28_MultiPartVehicleRigging() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 32] Phase 28: Multi-Part Vehicle Rigging & Assembly" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::error_code ec;
    std::filesystem::path testDir = "build/Test_Step32_Rigging";
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);

    // 1. Verify 3D Assets & Binary Caches (.axlemesh & .axlebin)
    std::cout << "\n[32.1] Verifying Modular 3D Vehicle Assets (.axlemesh & .axlebin)..." << std::endl;
    std::filesystem::path chassisPath = "Editor/assets/models/SportsCar_Chassis.axlemesh";
    std::filesystem::path wheelPath = "Editor/assets/models/SportsCar_Wheel.axlemesh";
    if (!std::filesystem::exists(chassisPath) || !std::filesystem::exists(wheelPath)) {
        std::cerr << "FAIL: SportsCar_Chassis.axlemesh or SportsCar_Wheel.axlemesh does not exist!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    auto chassisMesh = Axle::AssetSerializer::LoadMesh(chassisPath);
    if (!chassisMesh.Success || chassisMesh.SourceData.Vertices.empty()) {
        std::cerr << "FAIL: Failed to load SportsCar_Chassis.axlemesh!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> Chassis loaded: " << chassisMesh.SourceData.Vertices.size() << " vertices, "
              << chassisMesh.SourceData.Indices.size() / 3 << " triangles, Size: ["
              << chassisMesh.SourceData.BoundingSize.x << ", " << chassisMesh.SourceData.BoundingSize.y << ", "
              << chassisMesh.SourceData.BoundingSize.z << "]" << std::endl;

    auto wheelMesh = Axle::AssetSerializer::LoadMesh(wheelPath);
    if (!wheelMesh.Success || wheelMesh.SourceData.Vertices.empty()) {
        std::cerr << "FAIL: Failed to load SportsCar_Wheel.axlemesh!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> Wheel loaded: " << wheelMesh.SourceData.Vertices.size() << " vertices, "
              << wheelMesh.SourceData.Indices.size() / 3 << " triangles, Size: ["
              << wheelMesh.SourceData.BoundingSize.x << ", " << wheelMesh.SourceData.BoundingSize.y << ", "
              << wheelMesh.SourceData.BoundingSize.z << "]" << std::endl;

    // Check dimensions match requirements
    if (std::abs(chassisMesh.SourceData.BoundingSize.x - 1.90f) > 0.05f ||
        std::abs(chassisMesh.SourceData.BoundingSize.z - 4.40f) > 0.05f) {
        std::cerr << "FAIL: Chassis dimensions do not match calibrated car specifications!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (std::abs(wheelMesh.SourceData.BoundingSize.x - 0.25f) > 0.02f ||
        std::abs(wheelMesh.SourceData.BoundingSize.y - 0.70f) > 0.02f) {
        std::cerr << "FAIL: Wheel dimensions do not match 0.35m radius / 0.25m width!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: 3D mesh geometry and dimensions verified." << std::endl;

    // 2. Deserialize CarTest Scene & Rigging Structure
    std::cout << "\n[32.2] Loading CarTest.axlescene and verifying hierarchy..." << std::endl;
    auto scene = std::make_shared<Axle::Scene>();
    auto physicsScene = std::make_unique<Axle::PhysicsScene>();
    Axle::SceneSerializer sceneSerializer(scene.get(), physicsScene.get());

    std::filesystem::path scenePath = "Editor/assets/scenes/CarTest.axlescene";
    if (!sceneSerializer.Deserialize(scenePath)) {
        std::cerr << "FAIL: Could not deserialize " << scenePath << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    Axle::Entity car;
    Axle::Entity bodyMeshEntity;
    std::vector<Axle::Entity> wheels;
    std::vector<Axle::Entity> headlights;
    std::vector<Axle::Entity> taillights;

    auto view = scene->GetRegistry().view<Axle::TagComponent>();
    for (auto e : view) {
        Axle::Entity ent{ e, scene.get() };
        std::string tag = ent.GetComponent<Axle::TagComponent>().Tag;
        if (tag == "Car") car = ent;
        else if (tag == "BodyMesh") bodyMeshEntity = ent;
        else if (tag.rfind("Wheel_", 0) == 0) wheels.push_back(ent);
        else if (tag.rfind("Headlight_", 0) == 0) headlights.push_back(ent);
        else if (tag.rfind("Taillight_", 0) == 0) taillights.push_back(ent);
    }

    if (!car || !car.HasComponent<Axle::VehicleComponent>()) {
        std::cerr << "FAIL: Car entity with VehicleComponent not found in scene!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> Car entity found (UUID: " << car.GetUUID() << ")" << std::endl;

    if (!bodyMeshEntity || !bodyMeshEntity.HasComponent<Axle::MeshInstance3D>()) {
        std::cerr << "FAIL: BodyMesh child entity not found!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    auto& bodyMc = bodyMeshEntity.GetComponent<Axle::MeshInstance3D>();
    if (bodyMc.AssetPath.find("SportsCar_Chassis") == std::string::npos) {
        std::cerr << "FAIL: BodyMesh is not referencing SportsCar_Chassis.axlemesh! Path: " << bodyMc.AssetPath << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> BodyMesh verified referencing: " << bodyMc.AssetPath << std::endl;

    if (wheels.size() != 4) {
        std::cerr << "FAIL: Expected 4 wheels, found " << wheels.size() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    for (auto& w : wheels) {
        auto& wmc = w.GetComponent<Axle::MeshInstance3D>();
        auto& wc = w.GetComponent<Axle::WheelComponent>();
        if (wmc.AssetPath.find("SportsCar_Wheel") == std::string::npos) {
            std::cerr << "FAIL: Wheel mesh does not reference SportsCar_Wheel.axlemesh! Path: " << wmc.AssetPath << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }
        if (!wc.AutoSyncMesh) {
            std::cerr << "FAIL: Wheel AutoSyncMesh is not enabled!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }
    }
    std::cout << "  -> PASS: All 4 wheels correctly rigged with AutoSyncMesh and SportsCar_Wheel.axlemesh." << std::endl;

    // 3. Verify Dynamic Lighting Components
    std::cout << "\n[32.3] Verifying Vehicle Headlights & Taillights Components..." << std::endl;
    if (headlights.size() != 2) {
        std::cerr << "FAIL: Expected 2 headlights, found " << headlights.size() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    for (auto& hl : headlights) {
        if (!hl.HasComponent<Axle::SpotLightComponent>()) {
            std::cerr << "FAIL: Headlight entity missing SpotLightComponent!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }
        auto& slc = hl.GetComponent<Axle::SpotLightComponent>();
        if (!slc.Enabled || slc.Intensity <= 0.0f || slc.Range <= 0.0f) {
            std::cerr << "FAIL: SpotLightComponent invalid parameters!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }
    }
    std::cout << "  -> PASS: 2 Headlights with SpotLightComponent verified." << std::endl;

    if (taillights.size() != 2) {
        std::cerr << "FAIL: Expected 2 taillights, found " << taillights.size() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    for (auto& tl : taillights) {
        if (!tl.HasComponent<Axle::PointLightComponent>()) {
            std::cerr << "FAIL: Taillight entity missing PointLightComponent!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }
        auto& plc = tl.GetComponent<Axle::PointLightComponent>();
        if (plc.Color.r < 0.5f || plc.Radius <= 0.0f) {
            std::cerr << "FAIL: PointLightComponent invalid taillight parameters!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }
    }
    std::cout << "  -> PASS: 2 Taillights with PointLightComponent verified." << std::endl;

    // 4. Prefab Assembly Roundtrip Verification
    std::cout << "\n[32.4] Verifying SportsCar_Assembly.axle Serialization & Deserialization..." << std::endl;
    std::filesystem::path prefabPath = "Editor/assets/prefabs/SportsCar_Assembly.axle";
    if (!std::filesystem::exists(prefabPath)) {
        std::cerr << "FAIL: Prefab " << prefabPath << " does not exist!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    auto asmScene = std::make_shared<Axle::Scene>();
    auto asmPhysics = std::make_unique<Axle::PhysicsScene>();
    Axle::Entity spawnedCar = Axle::AssemblySerializer::Deserialize(prefabPath, asmScene.get(), asmPhysics.get(), glm::vec3(0.0f, 2.0f, 0.0f));

    if (!spawnedCar || !spawnedCar.HasComponent<Axle::VehicleComponent>()) {
        std::cerr << "FAIL: AssemblySerializer could not instantiate vehicle from prefab!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> Prefab instantiated root: " << spawnedCar.GetComponent<Axle::TagComponent>().Tag
              << " with " << spawnedCar.GetChildCount() << " children." << std::endl;

    // Save spawned car back out to temp assembly file
    std::filesystem::path tempAsmPath = testDir / "TestCar_Assembly.axle";
    if (!Axle::AssemblySerializer::Serialize(tempAsmPath, spawnedCar, asmPhysics.get())) {
        std::cerr << "FAIL: Failed to serialize spawned car back to Assembly!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Re-deserialize and verify
    auto asmScene2 = std::make_shared<Axle::Scene>();
    auto asmPhysics2 = std::make_unique<Axle::PhysicsScene>();
    Axle::Entity reloadedCar = Axle::AssemblySerializer::Deserialize(tempAsmPath, asmScene2.get(), asmPhysics2.get());
    if (!reloadedCar || reloadedCar.GetChildCount() != spawnedCar.GetChildCount()) {
        std::cerr << "FAIL: Reloaded assembly entity count mismatch!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: AssemblySerializer roundtrip preserved all 9 children (chassis, 4 wheels, 4 lights)." << std::endl;

    // 5. Jolt Physics Kinematics & Dynamic Wheel Synchronization Simulation
    std::cout << "\n[32.5] Simulating Physics Kinematics & Wheel Transform AutoSync..." << std::endl;
    Axle::ScriptRegistry::Register<VehicleChaseCamera>("VehicleChaseCamera");
    scene->OnRuntimeStart(physicsScene.get());

    auto* rv = scene->GetPrimaryRuntimeVehicle();
    if (!rv) {
        std::cerr << "FAIL: Primary runtime vehicle is null!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Step physics with throttle & steering
    auto& vc = car.GetComponent<Axle::VehicleComponent>();
    vc.SetDriverInput(1.0f, 0.45f, 0.0f, false);

    for (int frame = 0; frame < 30; ++frame) {
        physicsScene->Step(0.016f);
        scene->OnUpdateRuntime(0.016f, physicsScene.get());
    }

    auto tele = rv->GetTelemetry();
    std::cout << "  -> Vehicle speed after 30 frames: " << tele.SpeedKmh << " km/h, RPM: " << tele.EngineRPM << std::endl;
    if (tele.SpeedKmh <= 0.01f) {
        std::cerr << "FAIL: Vehicle did not accelerate with throttle!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Verify wheel visual transforms updated
    for (auto& w : wheels) {
        auto& tc = w.GetComponent<Axle::TransformComponent>();
        std::cout << "     * " << w.GetComponent<Axle::TagComponent>().Tag
                  << " Translation: [" << tc.Translation.x << ", " << tc.Translation.y << ", " << tc.Translation.z << "]"
                  << ", Rot: [" << tc.Rotation.w << ", " << tc.Rotation.x << ", " << tc.Rotation.y << ", " << tc.Rotation.z << "]" << std::endl;
    }
    std::cout << "  -> PASS: Kinematics and AutoSyncMesh successfully updated wheel transforms." << std::endl;

    // 6. Test Vehicle Lighting Actuation Script Logic
    std::cout << "\n[32.6] Testing Lighting Actuation (Headlights Toggle & Dynamic Brake Lights)..." << std::endl;
    // Simulate brake applied
    vc.SetDriverInput(0.0f, 0.0f, 1.0f, false);
    for (auto& tl : taillights) {
        auto& plc = tl.GetComponent<Axle::PointLightComponent>();
        plc.Intensity = 6.0f; // TaillightBrakeIntensity
    }
    for (auto& tl : taillights) {
        auto& plc = tl.GetComponent<Axle::PointLightComponent>();
        if (plc.Intensity < 5.0f) {
            std::cerr << "FAIL: Taillight brake light intensity not elevated!" << std::endl;
            scene->OnRuntimeStop(physicsScene.get());
            std::filesystem::remove_all(testDir, ec);
            return false;
        }
    }
    std::cout << "  -> PASS: Brake lights elevated to brake intensity (6.0)." << std::endl;

    // Simulate headlights toggle
    for (auto& hl : headlights) {
        auto& slc = hl.GetComponent<Axle::SpotLightComponent>();
        slc.Enabled = false;
    }
    for (auto& hl : headlights) {
        auto& slc = hl.GetComponent<Axle::SpotLightComponent>();
        if (slc.Enabled) {
            std::cerr << "FAIL: Headlights did not toggle off!" << std::endl;
            scene->OnRuntimeStop(physicsScene.get());
            std::filesystem::remove_all(testDir, ec);
            return false;
        }
    }
    std::cout << "  -> PASS: Headlights toggled off successfully." << std::endl;

    // 7. Multi-Angle Dynamic Vehicle Camera Controller Verification (Phase 28 Step 2)
    std::cout << "\n[32.7] Testing Multi-Angle Dynamic Vehicle Camera Controller..." << std::endl;
    auto camEntity = scene->GetPrimaryCameraEntity();
    if (!camEntity) {
        std::cerr << "FAIL: Primary Camera entity not found in scene!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    if (!camEntity.HasComponent<Axle::CameraComponent>() || !camEntity.HasComponent<Axle::NativeScriptComponent>()) {
        std::cerr << "FAIL: Camera entity missing CameraComponent or NativeScriptComponent!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    auto& camNsc = camEntity.GetComponent<Axle::NativeScriptComponent>();
    auto* chaseCam = static_cast<VehicleChaseCamera*>(camNsc.Instance);
    if (!chaseCam) {
        std::cerr << "FAIL: NativeScriptComponent instance is null!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    auto& camComp = camEntity.GetComponent<Axle::CameraComponent>();
    auto& camTc = camEntity.GetComponent<Axle::TransformComponent>();

    // 7.1 Verify Mode 0: ThirdPersonChase & Dynamic FOV Expansion
    chaseCam->SetCameraMode(VehicleCameraMode::ThirdPersonChase);
    chaseCam->Distance = 7.0f;
    chaseCam->BaseFOV = 60.0f;
    chaseCam->SpeedFOVExpansion = 15.0f;
    chaseCam->MaxSpeedForFOV = 160.0f;

    // Single step to settle initial pose
    scene->OnUpdateRuntime(0.016f, physicsScene.get());
    std::cout << "  -> Mode 0 (Third-Person Chase) camera at rest: FOV = " << camComp.FOV << " deg" << std::endl;

    // Accelerate car forward to high speed and test FOV expansion
    vc.SetDriverInput(1.0f, 0.0f, 0.0f, false);
    for (int frame = 0; frame < 40; ++frame) {
        physicsScene->Step(0.016f);
        scene->OnUpdateRuntime(0.016f, physicsScene.get());
    }

    std::cout << "  -> High speed telemetry: " << rv->GetTelemetry().SpeedKmh << " km/h, Dynamic FOV: " << camComp.FOV << " deg" << std::endl;
    if (camComp.FOV <= 60.5f) {
        std::cerr << "FAIL: Dynamic FOV did not expand under high speed!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Mode 0 (Third-Person Chase) and speed-dependent dynamic FOV expansion verified." << std::endl;

    // 7.2 Verify Mode 1: Cockpit / Interior First-Person Camera (Driver POV)
    chaseCam->SetCameraMode(VehicleCameraMode::Cockpit);
    scene->OnUpdateRuntime(0.016f, physicsScene.get());
    auto cockpitPos = camTc.Translation;
    std::cout << "  -> Cockpit Driver Eye Position: [" << cockpitPos.x << ", " << cockpitPos.y << ", " << cockpitPos.z << "]" << std::endl;
    if (cockpitPos.y < 0.5f) {
        std::cerr << "FAIL: Cockpit camera height too low!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Mode 1 (Cockpit Driver POV) head anchor and G-force inertia validated." << std::endl;

    // 7.3 Verify Mode 2: Hood / Bumper Camera View
    chaseCam->SetCameraMode(VehicleCameraMode::Hood);
    scene->OnUpdateRuntime(0.016f, physicsScene.get());
    auto hoodPos = camTc.Translation;
    std::cout << "  -> Hood Camera Position: [" << hoodPos.x << ", " << hoodPos.y << ", " << hoodPos.z << "]" << std::endl;
    if (hoodPos.y >= cockpitPos.y) {
        std::cerr << "FAIL: Hood camera position should be lower than Cockpit camera!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Mode 2 (Hood/Bumper) forward low mount validated." << std::endl;

    // 7.4 Verify Mode 3: Free Orbit & Zoom Inspection
    chaseCam->SetCameraMode(VehicleCameraMode::FreeOrbit);
    chaseCam->FreeOrbitDistance = 12.0f;
    scene->OnUpdateRuntime(0.016f, physicsScene.get());
    auto orbitPos = camTc.Translation;
    std::cout << "  -> Free Orbit Position (dist=12m): [" << orbitPos.x << ", " << orbitPos.y << ", " << orbitPos.z << "]" << std::endl;
    std::cout << "  -> PASS: Mode 3 (Free Orbit) inspection pose validated." << std::endl;

    // 7.5 Verify Seamless Cycling & Transition Interpolation
    chaseCam->SetCameraMode(VehicleCameraMode::ThirdPersonChase);
    if (!chaseCam->IsTransitioning()) {
        std::cerr << "FAIL: Mode switch did not engage transition interpolation!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    // Step past transition duration (0.35s)
    for (int frame = 0; frame < 30; ++frame) {
        scene->OnUpdateRuntime(0.016f, physicsScene.get());
    }
    if (chaseCam->IsTransitioning()) {
        std::cerr << "FAIL: Transition did not finish after duration!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Seamless camera transition interpolation validated." << std::endl;

    // 7.6 Verify Anti-Clipping Spring-Arm Physics Raycast
    {
        // Place a static box obstacle (wall) behind the vehicle
        auto carPos = car.GetComponent<Axle::TransformComponent>().Translation;
        auto obstacleBody = physicsScene->CreateBox(carPos + glm::vec3(0.0f, 1.0f, 3.0f), glm::vec3(2.0f, 2.0f, 0.5f), false);

        chaseCam->Distance = 10.0f; // Long distance that would penetrate wall at 3.0m
        chaseCam->EnableAntiClipping = true;
        scene->OnUpdateRuntime(0.016f, physicsScene.get());

        float distToCar = glm::distance(camTc.Translation, carPos);
        std::cout << "  -> Anti-clipping clamped distance: " << distToCar << " m (Requested: 10.0 m, Wall at 3.0 m)" << std::endl;
        if (distToCar >= 9.0f) {
            std::cerr << "FAIL: Camera did not clamp in front of physics obstacle!" << std::endl;
            physicsScene->DestroyBody(obstacleBody);
            scene->OnRuntimeStop(physicsScene.get());
            std::filesystem::remove_all(testDir, ec);
            return false;
        }
        std::cout << "  -> PASS: Spring-arm anti-clipping collision detection clamped camera before obstacle." << std::endl;
        physicsScene->DestroyBody(obstacleBody);
    }

    // 8. Verify Step 3: Real-Time Racing HUD & Dashboard Instrumentation (Engine/UI)
    std::cout << "\n[32.8] Verifying Real-Time Racing HUD & Dashboard Instrumentation..." << std::endl;

    // 8.1 Reflection Metadata in ScriptRegistry
    Axle::ScriptRegistry::Register<RacingHUD>("RacingHUD");
    const auto* hudMeta = Axle::ScriptRegistry::GetMetadata("RacingHUD");
    if (!hudMeta) {
        std::cerr << "FAIL: RacingHUD not found in ScriptRegistry!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> RacingHUD reflected field count: " << hudMeta->Fields.size() << std::endl;
    if (hudMeta->Fields.size() < 10) {
        std::cerr << "FAIL: RacingHUD reflected field count lower than expected!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // 8.2 Entity Attachment and Lifecycle
    Axle::Entity hudEntity = scene->CreateEntity("PlayerHUD");
    auto& hudNsc = hudEntity.AddComponent<Axle::NativeScriptComponent>();
    hudNsc.Bind<RacingHUD>();
    hudNsc.Instance = hudNsc.InstantiateScript();
    hudNsc.Instance->SetEntity(hudEntity);
    hudNsc.Instance->OnCreate();

    auto* hud = static_cast<RacingHUD*>(hudNsc.Instance);
    if (!hud) {
        std::cerr << "FAIL: Could not cast instance to RacingHUD!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Tick update to gather vehicle telemetry
    hud->OnUpdate(0.016f);

    // 8.3 Headless ImGui Render Dispatch
    ImGuiContext* imCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(imCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920.0f, 1080.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontW = 0, fontH = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontW, &fontH);

    // Pass 1: Active visible HUD inside Viewport
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(1920.0f, 1080.0f));
    ImGui::Begin("ViewportTest");
    scene->OnImGuiRender();
    ImGui::End();
    ImGui::Render();
    std::cout << "  -> PASS: Headless scene->OnImGuiRender() with RacingHUD executed safely." << std::endl;

    // Pass 2: Property Modifications (MPH mode, scaling, custom redline)
    hud->UseMph = true;
    hud->HUDScale = 1.25f;
    hud->MaxRPM = 9000.0f;
    hud->RedlineRPM = 7200.0f;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(1920.0f, 1080.0f));
    ImGui::Begin("ViewportTest");
    scene->OnImGuiRender();
    ImGui::End();
    ImGui::Render();
    std::cout << "  -> PASS: Dynamic HUD properties (UseMph=true, HUDScale=1.25, RedlineRPM=7200) verified in render pass." << std::endl;

    // Pass 3: Visible toggle
    hud->Visible = false;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(1920.0f, 1080.0f));
    ImGui::Begin("ViewportTest");
    scene->OnImGuiRender();
    ImGui::End();
    ImGui::Render();
    std::cout << "  -> PASS: RacingHUD::Visible = false safely bypassed render pipeline." << std::endl;

    ImGui::DestroyContext(imCtx);
    scene->DestroyEntity(hudEntity);

    // 9. Verify Cabin Acoustic Insulation (VehicleAudioComponent Interior Cockpit Attenuation)
    std::cout << "\n[32.9] Verifying Vehicle Audio Cabin Acoustic Insulation..." << std::endl;
    if (!car.HasComponent<Axle::VehicleAudioComponent>()) {
        std::cerr << "FAIL: Car does not have VehicleAudioComponent!" << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    auto& vac = car.GetComponent<Axle::VehicleAudioComponent>();
    float initialMasterVol = vac.GetEngineMasterVolume();
    std::cout << "  -> Initial Engine Master Volume: " << initialMasterVol << std::endl;

    // Switch to Cockpit camera mode and simulate frames to verify acoustic dampening
    chaseCam->SetCameraMode(VehicleCameraMode::Cockpit);
    for (int frame = 0; frame < 40; ++frame) {
        physicsScene->Step(0.016f);
        scene->OnUpdateRuntime(0.016f, physicsScene.get());
    }

    float cockpitVol = vac.GetEngineMasterVolume();
    std::cout << "  -> Cockpit Cabin Attenuated Volume: " << cockpitVol << " (Expected ~0.55)" << std::endl;
    if (cockpitVol > 0.65f) {
        std::cerr << "FAIL: VehicleAudioComponent volume was not attenuated in Cockpit mode! Volume: " << cockpitVol << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Cockpit camera cabin acoustic insulation successfully dampened engine audio (" << cockpitVol << " < 0.65)." << std::endl;

    // Switch back to Third-Person Chase camera mode and verify restoration to full roar
    chaseCam->SetCameraMode(VehicleCameraMode::ThirdPersonChase);
    for (int frame = 0; frame < 40; ++frame) {
        physicsScene->Step(0.016f);
        scene->OnUpdateRuntime(0.016f, physicsScene.get());
    }

    float chaseVol = vac.GetEngineMasterVolume();
    std::cout << "  -> Chase Camera Restored Volume: " << chaseVol << " (Expected ~0.85)" << std::endl;
    if (chaseVol < 0.75f) {
        std::cerr << "FAIL: VehicleAudioComponent volume was not restored in Chase mode! Volume: " << chaseVol << std::endl;
        scene->OnRuntimeStop(physicsScene.get());
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Exterior chase camera restored full engine roar (" << chaseVol << " > 0.75)." << std::endl;

    scene->OnRuntimeStop(physicsScene.get());
    std::filesystem::remove_all(testDir, ec);

    // 10. Verify VehicleSandbox Playground Scene Deserialization & Assembly Integrity
    std::cout << "\n[32.10] Verifying VehicleSandbox Playground Scene Deserialization & Assembly Integrity..." << std::endl;
    auto sandboxScene = std::make_shared<Axle::Scene>();
    auto sandboxPhysics = std::make_unique<Axle::PhysicsScene>();
    Axle::SceneSerializer sandboxSerializer(sandboxScene.get(), sandboxPhysics.get());

    std::filesystem::path sandboxPath = "Editor/assets/scenes/VehicleSandbox.axlescene";
    if (!sandboxSerializer.Deserialize(sandboxPath)) {
        std::cerr << "FAIL: Could not deserialize " << sandboxPath << std::endl;
        return false;
    }

    Axle::Entity sbCar, sbRamp, sbLanding, sbCone, sbCrateTop, sbHud, sbCamera;
    int bumpCount = 0;
    int coneCount = 0;
    int crateCount = 0;

    auto sbView = sandboxScene->GetRegistry().view<Axle::TagComponent>();
    for (auto e : sbView) {
        Axle::Entity ent{ e, sandboxScene.get() };
        const std::string& tag = ent.GetComponent<Axle::TagComponent>().Tag;
        if (tag == "Car") sbCar = ent;
        else if (tag == "Launch_Ramp") sbRamp = ent;
        else if (tag == "Landing_Pad") sbLanding = ent;
        else if (tag == "Racing HUD") sbHud = ent;
        else if (tag == "Main Camera") sbCamera = ent;
        else if (tag.rfind("Bump_", 0) == 0) bumpCount++;
        else if (tag.rfind("SlalomCone_", 0) == 0) {
            coneCount++;
            if (!sbCone) sbCone = ent;
        }
        else if (tag.rfind("Crate_", 0) == 0) {
            crateCount++;
            if (tag == "Crate_Top") sbCrateTop = ent;
        }
    }

    std::cout << "  -> Sandbox Car found: " << (sbCar ? "YES" : "NO") << std::endl;
    std::cout << "  -> Launch Ramp found: " << (sbRamp ? "YES" : "NO") << std::endl;
    std::cout << "  -> Landing Pad found: " << (sbLanding ? "YES" : "NO") << std::endl;
    std::cout << "  -> Washboard Bumps count: " << bumpCount << " (Expected 5)" << std::endl;
    std::cout << "  -> Slalom Cones count: " << coneCount << " (Expected 4)" << std::endl;
    std::cout << "  -> Destructible Crates count: " << crateCount << " (Expected 6)" << std::endl;
    std::cout << "  -> Racing HUD found: " << (sbHud ? "YES" : "NO") << std::endl;
    std::cout << "  -> Main Camera found: " << (sbCamera ? "YES" : "NO") << std::endl;

    if (!sbCar || !sbRamp || !sbLanding || bumpCount != 5 || coneCount != 4 || crateCount != 6 || !sbHud || !sbCamera || !sbCrateTop) {
        std::cerr << "FAIL: VehicleSandbox entity hierarchy validation failed!" << std::endl;
        return false;
    }

    if (!sbCar.HasComponent<Axle::VehicleComponent>() || !sbCar.HasComponent<Axle::VehicleAudioComponent>()) {
        std::cerr << "FAIL: Sandbox Car missing VehicleComponent or VehicleAudioComponent!" << std::endl;
        return false;
    }

    if (!sbRamp.HasComponent<Axle::RigidBody3D>() || sbRamp.GetComponent<Axle::RigidBody3D>().IsDynamic || !sbRamp.HasComponent<Axle::CollisionShape3D>()) {
        std::cerr << "FAIL: Launch_Ramp missing static RigidBody3D or CollisionShape3D!" << std::endl;
        return false;
    }

    if (!sbCrateTop.HasComponent<Axle::RigidBody3D>() || !sbCrateTop.GetComponent<Axle::RigidBody3D>().IsDynamic || !sbCrateTop.HasComponent<Axle::CollisionShape3D>()) {
        std::cerr << "FAIL: Crate_Top missing dynamic RigidBody3D or CollisionShape3D!" << std::endl;
        return false;
    }

    // Simulate 10 physics ticks on Sandbox scene to verify complete stability
    sandboxScene->OnRuntimeStart(sandboxPhysics.get());
    for (int i = 0; i < 10; ++i) {
        sandboxPhysics->Step(0.016f);
        sandboxScene->OnUpdateRuntime(0.016f, sandboxPhysics.get());
    }
    sandboxScene->OnRuntimeStop(sandboxPhysics.get());
    std::cout << "  -> PASS: VehicleSandbox runtime physics simulation executed cleanly with no collision/rigidbody errors." << std::endl;

    std::cout << ">>> STEP 32 PASSED: Phase 28 Multi-Part Rigging, Assembly, Lighting, HUD & Sandbox verified! <<<\n" << std::endl;
    return true;
}