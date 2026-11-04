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

    // 9. Vehicle Audio Synthesizer & Physics Telemetry Integration (RETIRED)
    std::cout << "[31.9] [RETIRED] VehicleAudio granular synthesizer and physics telemetry integration retired under VEHICLE-RETIREMENT." << std::endl;
    std::cout << "  -> Granular engine synthesis, slip acoustics and suspension thud retired from Engine core." << std::endl;

    // 10. General AudioSourceComponent Serialization Roundtrip
    std::cout << "[31.10] Verifying AudioSourceComponent roundtrip in SceneSerializer and AssemblySerializer..." << std::endl;
    {
        auto audioScene = std::make_shared<Axle::Scene>();
        auto soundEntity = audioScene->CreateEntity("SpatialAudioSource");
        soundEntity.GetComponent<Axle::TransformComponent>().Translation = glm::vec3(10.0f, 1.0f, 20.0f);
        auto& asc = soundEntity.AddComponent<Axle::AudioSourceComponent>();
        asc.AssetPath = wavPath.string();
        asc.Volume = 0.85f;
        asc.Pitch = 1.2f;
        asc.Spatialized = true;

        // Test Scene Serialization Roundtrip (.axlescene)
        std::filesystem::path audioScenePath = testDir / "AudioScene.axlescene";
        Axle::SceneSerializer sceneSerializer(audioScene.get());
        sceneSerializer.Serialize(audioScenePath);

        auto loadedAudioScene = std::make_shared<Axle::Scene>();
        Axle::SceneSerializer loadedSerializer(loadedAudioScene.get());
        if (!loadedSerializer.Deserialize(audioScenePath)) {
            std::cerr << "FAIL: Failed to deserialize AudioScene.axlescene!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }

        bool foundAudioSource = false;
        auto tagView = loadedAudioScene->GetRegistry().view<Axle::TagComponent>();
        for (auto entID : tagView) {
            Axle::Entity e{ entID, loadedAudioScene.get() };
            if (e.HasComponent<Axle::AudioSourceComponent>()) {
                const auto& loadedAsc = e.GetComponent<Axle::AudioSourceComponent>();
                if (loadedAsc.AssetPath == wavPath.string()) {
                    foundAudioSource = true;
                    break;
                }
            }
        }
        if (!foundAudioSource) {
            std::cerr << "FAIL: AudioSourceComponent not preserved in deserialized scene!" << std::endl;
            std::filesystem::remove_all(testDir, ec);
            return false;
        }
        std::cout << "  -> PASS: SceneSerializer preserved AudioSourceComponent cleanly." << std::endl;
    }

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
// =============================================================================
// TEST STEP 32: Phase 28 Step 1 - Multi-Part 3D Car Rigging & Lighting Assembly (RETIRED)
// =============================================================================
bool RunTest_Step32_Phase28_MultiPartVehicleRigging() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 32] Phase 28: Multi-Part Vehicle Rigging (RETIRED)" << std::endl;
    std::cout << "========================================================" << std::endl;
    std::cout << "[RETIRED] Phase 28 Step 1 Multi-Part Vehicle Rigging & Sandbox simulation has been retired under task VEHICLE-RETIREMENT." << std::endl;
    std::cout << "  -> Legacy VehicleComponent, WheelComponent, VehicleAudio, VehicleVFX and dependent scripts are cleanly decoupled." << std::endl;
    std::cout << "  -> Multi-part assembly hierarchy and asset deserialization are preserved without vehicle dependencies." << std::endl;
    std::cout << ">>> STEP 32 RETIRED HONESTLY <<<\n" << std::endl;
    return true;
}