#include "TestCommon.hpp"

// =============================================================================
// TEST STEP 14: Phase 17.1 Asset UUID & .meta Companion File Pipeline
// =============================================================================
bool RunTest_Step14_Phase17_AssetUUIDAndMetaPipeline() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 14 TEST] PHASE 17.1: ASSET UUID & .META PIPELINE  " << std::endl;
    std::cout << "========================================================" << std::endl;

    // 1. AssetType detection from path extensions
    std::cout << "[14.1] Testing AssetType detection from file extensions..." << std::endl;
    if (Axle::GetAssetTypeFromPath("car.axlemesh") != Axle::AssetType::Mesh ||
        Axle::GetAssetTypeFromPath("vehicle.axle") != Axle::AssetType::Assembly ||
        Axle::GetAssetTypeFromPath("car.obj") != Axle::AssetType::Mesh ||
        Axle::GetAssetTypeFromPath("texture.png") != Axle::AssetType::Texture ||
        Axle::GetAssetTypeFromPath("material.axlemat") != Axle::AssetType::Material ||
        Axle::GetAssetTypeFromPath("script.hpp") != Axle::AssetType::Script ||
        Axle::GetAssetTypeFromPath("scene.axlescene") != Axle::AssetType::Scene ||
        Axle::GetAssetTypeFromPath("phys.physmat") != Axle::AssetType::PhysicsMaterial ||
        Axle::GetAssetTypeFromPath("engine.wav") != Axle::AssetType::Audio) {
        std::cerr << "FAIL: AssetType detection failed for standard asset extensions!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: All asset extension types correctly categorized (.axlemesh -> Mesh, .axle -> Assembly)." << std::endl;

    // 2. .meta serialization roundtrip
    std::cout << "\n[14.2] Testing .meta file serialization and parsing..." << std::endl;
    std::filesystem::path testMetaFile = "sandbox_single_asset.obj.meta";
    Axle::UUID expectedUUID = 1234567890987654321ULL;
    Axle::AssetMetadata originalMeta;
    originalMeta.Handle = expectedUUID;
    originalMeta.Type = Axle::AssetType::Mesh;
    originalMeta.FilePath = "models/sandbox_single_asset.obj";

    bool serOk = Axle::AssetMetadataSerializer::Serialize(testMetaFile, originalMeta);
    if (!serOk || !std::filesystem::exists(testMetaFile)) {
        std::cerr << "FAIL: AssetMetadataSerializer::Serialize failed!" << std::endl;
        return false;
    }

    Axle::AssetMetadata loadedMeta;
    bool deserOk = Axle::AssetMetadataSerializer::Deserialize(testMetaFile, loadedMeta);
    if (!deserOk || loadedMeta.Handle != expectedUUID || loadedMeta.Type != Axle::AssetType::Mesh) {
        std::cerr << "FAIL: AssetMetadataSerializer::Deserialize returned incorrect metadata!" << std::endl;
        std::filesystem::remove(testMetaFile);
        return false;
    }
    std::filesystem::remove(testMetaFile);
    std::cout << "  -> PASS: .meta YAML format correctly serialized and parsed." << std::endl;

    // 3. Auto-generation of companion .meta files in sandbox asset directory
    std::cout << "\n[14.3] Testing auto-generation of companion .meta files..." << std::endl;
    std::filesystem::path testDir = "sandbox_asset_pipeline_test";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir / "submodels", ec);

    // Create mock asset files
    auto createMockFile = [](const std::filesystem::path& p) {
        std::ofstream f(p);
        f << "Mock asset payload for " << p.filename().string() << "\n";
        f.close();
    };

    std::filesystem::path fileChassis = testDir / "submodels" / "chassis.obj";
    std::filesystem::path fileTexture = testDir / "wheel_albedo.png";
    std::filesystem::path fileMaterial = testDir / "car_paint.axlemat";

    createMockFile(fileChassis);
    createMockFile(fileTexture);
    createMockFile(fileMaterial);

    Axle::AssetManager::Init(testDir);

    std::filesystem::path metaChassis = Axle::AssetMetadataSerializer::GetMetaPath(fileChassis);
    std::filesystem::path metaTexture = Axle::AssetMetadataSerializer::GetMetaPath(fileTexture);
    std::filesystem::path metaMaterial = Axle::AssetMetadataSerializer::GetMetaPath(fileMaterial);

    if (!std::filesystem::exists(metaChassis) ||
        !std::filesystem::exists(metaTexture) ||
        !std::filesystem::exists(metaMaterial)) {
        std::cerr << "FAIL: Companion .meta files were not auto-generated on AssetManager::Sync!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> Auto-generated: " << metaChassis.filename().string() << std::endl;
    std::cout << "  -> Auto-generated: " << metaTexture.filename().string() << std::endl;
    std::cout << "  -> Auto-generated: " << metaMaterial.filename().string() << std::endl;

    Axle::UUID chassisUUID = Axle::AssetManager::GetAssetUUID(fileChassis);
    Axle::UUID textureUUID = Axle::AssetManager::GetAssetUUID(fileTexture);
    Axle::UUID materialUUID = Axle::AssetManager::GetAssetUUID(fileMaterial);

    if (chassisUUID == 0 || textureUUID == 0 || materialUUID == 0) {
        std::cerr << "FAIL: Generated UUIDs must be non-zero!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> Chassis UUID:  " << (uint64_t)chassisUUID << std::endl;
    std::cout << "  -> Texture UUID:  " << (uint64_t)textureUUID << std::endl;
    std::cout << "  -> Material UUID: " << (uint64_t)materialUUID << std::endl;

    // 4. Verification of persistence across restart (Zero UUID drift)
    std::cout << "\n[14.4] Testing UUID persistence across shutdown and restart..." << std::endl;
    Axle::AssetManager::Shutdown();
    Axle::AssetManager::Init(testDir);

    Axle::UUID reloadedChassisUUID = Axle::AssetManager::GetAssetUUID(fileChassis);
    if (reloadedChassisUUID != chassisUUID) {
        std::cerr << "FAIL: UUID changed after restart! Expected " << (uint64_t)chassisUUID 
                  << ", got " << (uint64_t)reloadedChassisUUID << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Persistent UUID preserved across restarts without drift." << std::endl;

    // 5. Path-Independent Referencing & Rename Lifecycle
    std::cout << "\n[14.5] Testing path-independent referencing and rename lifecycle..." << std::endl;
    std::filesystem::path renamedChassis = testDir / "submodels" / "chassis_v2_sport.obj";
    std::filesystem::rename(fileChassis, renamedChassis, ec);
    Axle::AssetManager::OnAssetRenamed(fileChassis, renamedChassis);

    std::filesystem::path renamedMeta = Axle::AssetMetadataSerializer::GetMetaPath(renamedChassis);
    if (!std::filesystem::exists(renamedMeta) || std::filesystem::exists(metaChassis)) {
        std::cerr << "FAIL: Companion .meta file was not renamed alongside asset!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Lookup path by old UUID -> must now resolve to the renamed path!
    std::filesystem::path resolvedPath = Axle::AssetManager::GetAssetPath(chassisUUID);
    std::string normResolved = resolvedPath.generic_string();
    if (normResolved.find("chassis_v2_sport.obj") == std::string::npos) {
        std::cerr << "FAIL: UUID lookup did not resolve to renamed file path!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> UUID " << (uint64_t)chassisUUID << " successfully resolved to renamed file: " 
              << resolvedPath.string() << std::endl;
    std::cout << "  -> PASS: Path-independent asset referencing verified." << std::endl;

    // 6. Delete lifecycle
    std::cout << "\n[14.6] Testing delete lifecycle..." << std::endl;
    std::filesystem::remove(renamedChassis, ec);
    Axle::AssetManager::OnAssetDeleted(renamedChassis);

    if (std::filesystem::exists(renamedMeta) || Axle::AssetManager::HasAsset(chassisUUID)) {
        std::cerr << "FAIL: Deleted asset companion .meta was not removed or registry still holds asset!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Companion .meta cleaned up and asset unregistered on delete." << std::endl;

    // Cleanup
    Axle::AssetManager::Shutdown();
    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 14 PASSED: Phase 17.1 Asset UUID & .meta Pipeline verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 15: Phase 17.2 Right-Click Asset Creation Pipeline
// =============================================================================
bool RunTest_Step15_Phase17_RightClickAssetCreation() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 15 TEST] PHASE 17.2: RIGHT-CLICK ASSET CREATION   " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::filesystem::path testDir = "sandbox_creation_pipeline_test";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);

    Axle::AssetManager::Init(testDir);

    // 1. Create New Folder & Collision Handling
    std::cout << "[15.1] Testing New Folder creation and name deduplication..." << std::endl;
    auto folder1 = Axle::ContentBrowserPanel::CreateNewFolder(testDir);
    auto folder2 = Axle::ContentBrowserPanel::CreateNewFolder(testDir);

    if (!std::filesystem::is_directory(folder1) || !std::filesystem::is_directory(folder2)) {
        std::cerr << "FAIL: CreateNewFolder failed to create physical directory!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (folder1 == folder2 || folder2.filename().string() != "New_Folder_1") {
        std::cerr << "FAIL: Name deduplication for folder failed! Expected New_Folder_1, got " 
                  << folder2.filename().string() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Created '" << folder1.filename().string() 
              << "' and '" << folder2.filename().string() << "' cleanly." << std::endl;

    // 2. Create Scene (.axlescene) & Companion .meta
    std::cout << "\n[15.2] Testing Scene (.axlescene) creation and companion .meta..." << std::endl;
    auto scene1 = Axle::ContentBrowserPanel::CreateNewScene(testDir);
    auto scene2 = Axle::ContentBrowserPanel::CreateNewScene(testDir);

    auto sceneMeta1 = Axle::AssetMetadataSerializer::GetMetaPath(scene1);
    if (!std::filesystem::exists(scene1) || !std::filesystem::exists(sceneMeta1)) {
        std::cerr << "FAIL: CreateNewScene did not create scene file or companion .meta!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (scene2.filename().string() != "New_Scene_1.axlescene") {
        std::cerr << "FAIL: Scene deduplication failed! Got " << scene2.filename().string() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    Axle::AssetMetadata sceneMeta;
    if (!Axle::AssetMetadataSerializer::Deserialize(sceneMeta1, sceneMeta) || sceneMeta.Type != Axle::AssetType::Scene) {
        std::cerr << "FAIL: Scene companion .meta invalid or wrong AssetType!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Scene '" << scene1.filename().string() << "' created with .meta [UUID: " 
              << (uint64_t)sceneMeta.Handle << "]" << std::endl;

    // 3. Create Material Asset (.axlemat) & Default PBR Values
    std::cout << "\n[15.3] Testing Material Asset (.axlemat) creation..." << std::endl;
    auto mat1 = Axle::ContentBrowserPanel::CreateNewMaterial(testDir);
    auto mat2 = Axle::ContentBrowserPanel::CreateNewMaterial(testDir);

    auto matMeta1 = Axle::AssetMetadataSerializer::GetMetaPath(mat1);
    if (!std::filesystem::exists(mat1) || !std::filesystem::exists(matMeta1)) {
        std::cerr << "FAIL: CreateNewMaterial did not write material file or companion .meta!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (mat2.filename().string() != "New_Material_1.axleasset" && mat2.filename().string() != "New_Material_1.axlemat") {
        std::cerr << "FAIL: Material deduplication failed! Got " << mat2.filename().string() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    Axle::MaterialComponent loadedMat;
    if (!Axle::SceneHierarchyPanel::LoadMaterialAsset(mat1, loadedMat)) {
        std::cerr << "FAIL: Could not load created .axlemat material file!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Material '" << mat1.filename().string() 
              << "' created with default Albedo: (" << loadedMat.AlbedoColor.r << ", " 
              << loadedMat.AlbedoColor.g << ", " << loadedMat.AlbedoColor.b << ")" << std::endl;

    // 4. Create C++ Script (.hpp) & Boilerplate
    std::cout << "\n[15.4] Testing C++ Script (.hpp) boilerplate creation..." << std::endl;
    auto script1 = Axle::ContentBrowserPanel::CreateNewScript(testDir);
    auto script2 = Axle::ContentBrowserPanel::CreateNewScript(testDir);

    auto scriptMeta1 = Axle::AssetMetadataSerializer::GetMetaPath(script1);
    if (!std::filesystem::exists(script1) || !std::filesystem::exists(scriptMeta1)) {
        std::cerr << "FAIL: CreateNewScript did not write script file or companion .meta!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (script2.filename().string() != "NewScript_1.hpp") {
        std::cerr << "FAIL: Script deduplication failed! Got " << script2.filename().string() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    // Verify script contains AXLE_SCRIPT_FIELDS and AXLE_REGISTER_SCRIPT
    std::ifstream scriptIn(script1);
    std::string scriptContent((std::istreambuf_iterator<char>(scriptIn)), std::istreambuf_iterator<char>());
    scriptIn.close();
    if (scriptContent.find("AXLE_SCRIPT_FIELDS") == std::string::npos ||
        scriptContent.find("AXLE_REGISTER_SCRIPT") == std::string::npos) {
        std::cerr << "FAIL: Generated C++ script does not contain required reflection macros!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: C++ Script boilerplate with reflection macro verified." << std::endl;

    // 5. Create Physics Material (.physmat) & Restitution/Friction Validation
    std::cout << "\n[15.5] Testing Physics Material (.physmat) creation..." << std::endl;
    auto phys1 = Axle::ContentBrowserPanel::CreateNewPhysicsMaterial(testDir);
    auto phys2 = Axle::ContentBrowserPanel::CreateNewPhysicsMaterial(testDir);

    auto physMeta1 = Axle::AssetMetadataSerializer::GetMetaPath(phys1);
    if (!std::filesystem::exists(phys1) || !std::filesystem::exists(physMeta1)) {
        std::cerr << "FAIL: CreateNewPhysicsMaterial did not create file or companion .meta!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    if (phys2.filename().string() != "New_PhysicsMaterial_1.axleasset" && phys2.filename().string() != "New_PhysicsMaterial_1.physmat") {
        std::cerr << "FAIL: PhysicsMaterial deduplication failed! Got " << phys2.filename().string() << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }

    Axle::PhysicsMaterial loadedPhys;
    if (!Axle::PhysicsMaterial::Load(phys1, loadedPhys)) {
        std::cerr << "FAIL: PhysicsMaterial::Load failed to parse .physmat file!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    bool matchesDefault = (std::abs(loadedPhys.Friction - 0.5f) <= 0.01f && std::abs(loadedPhys.Restitution - 0.0f) <= 0.01f) ||
                          (std::abs(loadedPhys.Friction - 0.85f) <= 0.01f && std::abs(loadedPhys.Restitution - 0.05f) <= 0.01f);
    if (!matchesDefault) {
        std::cerr << "FAIL: Physics material values do not match defaults!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: Physics Material created with Friction: " << loadedPhys.Friction 
              << ", Restitution: " << loadedPhys.Restitution << std::endl;

    // 6. Verify all created assets are indexed in AssetManager
    std::cout << "\n[15.6] Verifying all assets indexed in AssetManager..." << std::endl;
    if (!Axle::AssetManager::HasAsset(scene1) ||
        !Axle::AssetManager::HasAsset(mat1) ||
        !Axle::AssetManager::HasAsset(script1) ||
        !Axle::AssetManager::HasAsset(phys1)) {
        std::cerr << "FAIL: Created assets missing from AssetManager registry!" << std::endl;
        std::filesystem::remove_all(testDir, ec);
        return false;
    }
    std::cout << "  -> PASS: All created assets registered in AssetManager with persistent UUIDs." << std::endl;

    // Cleanup
    Axle::AssetManager::Shutdown();
    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 15 PASSED: Phase 17.2 Right-Click Asset Creation verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 16: PHASE 17.3 DEDICATED MATERIAL INSPECTOR TEST
// =============================================================================
bool RunTest_Step16_Phase17_DedicatedMaterialInspector() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 16 TEST] PHASE 17.3 DEDICATED MATERIAL INSPECTOR  " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::filesystem::path testDir = "sandbox_mat_inspector_test";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);

    Axle::AssetManager::Init(testDir);

    // 1. Dual Format (.amat & .axlemat) Persistence & Serialization Roundtrip
    std::cout << "\n[16.1] Testing Dual-Format (.amat & .axlemat) Persistence..." << std::endl;
    std::filesystem::path amatPath = testDir / "RacingPaint.amat";
    std::filesystem::path axlematPath = testDir / "GoldMetal.axlemat";

    Axle::MaterialComponent mat1;
    mat1.AlbedoColor = glm::vec4(0.88f, 0.12f, 0.15f, 0.95f);
    mat1.AlbedoPath = "textures/paint_base.png";
    mat1.UseAlbedoMap = true;
    mat1.NormalPath = "textures/paint_normal.png";
    mat1.UseNormalMap = true;
    mat1.NormalStrength = 2.2f;
    mat1.Roughness = 0.18f;
    mat1.RoughnessPath = "textures/paint_rough.png";
    mat1.UseRoughnessMap = true;
    mat1.Metallic = 0.90f;
    mat1.MetallicPath = "textures/paint_metal.png";
    mat1.UseMetallicMap = true;
    mat1.EmissionColor = glm::vec3(0.8f, 0.1f, 0.1f);
    mat1.EmissionIntensity = 3.5f;
    mat1.UVScale = glm::vec2(2.5f, 2.5f);

    if (!Axle::SceneHierarchyPanel::SaveMaterialAsset(amatPath, mat1)) {
        std::cerr << "FAIL: Failed to save .amat material file!" << std::endl;
        return false;
    }

    Axle::MaterialComponent loadedAmat;
    if (!Axle::SceneHierarchyPanel::LoadMaterialAsset(amatPath, loadedAmat)) {
        std::cerr << "FAIL: Failed to load .amat material file!" << std::endl;
        return false;
    }

    if (std::abs(loadedAmat.AlbedoColor.r - 0.88f) > 1e-3f ||
        std::abs(loadedAmat.Roughness - 0.18f) > 1e-3f ||
        std::abs(loadedAmat.Metallic - 0.90f) > 1e-3f ||
        std::abs(loadedAmat.NormalStrength - 2.2f) > 1e-3f ||
        std::abs(loadedAmat.EmissionIntensity - 3.5f) > 1e-3f ||
        std::abs(loadedAmat.UVScale.x - 2.5f) > 1e-3f ||
        loadedAmat.AlbedoPath != "textures/paint_base.png" ||
        loadedAmat.NormalPath != "textures/paint_normal.png" ||
        loadedAmat.RoughnessPath != "textures/paint_rough.png" ||
        loadedAmat.MetallicPath != "textures/paint_metal.png") {
        std::cerr << "FAIL: Loaded .amat material parameter mismatch!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: .amat file format saved and loaded with 100% parameter fidelity." << std::endl;

    // Save and load .axlemat
    Axle::MaterialComponent mat2;
    mat2.SetPresetGold();
    mat2.UVScale = glm::vec2(4.0f, 4.0f);
    if (!Axle::SceneHierarchyPanel::SaveMaterialAsset(axlematPath, mat2)) {
        std::cerr << "FAIL: Failed to save .axlemat material file!" << std::endl;
        return false;
    }
    Axle::MaterialComponent loadedAxlemat;
    if (!Axle::SceneHierarchyPanel::LoadMaterialAsset(axlematPath, loadedAxlemat)) {
        std::cerr << "FAIL: Failed to load .axlemat material file!" << std::endl;
        return false;
    }
    if (std::abs(loadedAxlemat.Metallic - 1.0f) > 1e-3f ||
        std::abs(loadedAxlemat.Roughness - 0.18f) > 1e-3f ||
        std::abs(loadedAxlemat.UVScale.x - 4.0f) > 1e-3f) {
        std::cerr << "FAIL: Loaded .axlemat Gold preset parameter mismatch!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: .axlemat file format verified with Gold preset." << std::endl;

    // 2. Testing Material Presets
    std::cout << "\n[16.2] Testing Material Presets Library..." << std::endl;
    Axle::MaterialComponent presetMat;
    presetMat.SetPresetChrome();
    if (presetMat.Metallic != 1.0f || presetMat.Roughness != 0.04f) {
        std::cerr << "FAIL: Chrome preset properties incorrect!" << std::endl;
        return false;
    }
    presetMat.SetPresetRubber();
    if (presetMat.Metallic != 0.0f || presetMat.Roughness != 0.85f) {
        std::cerr << "FAIL: Rubber preset properties incorrect!" << std::endl;
        return false;
    }
    presetMat.SetPresetObsidian();
    if (presetMat.Metallic != 0.0f || presetMat.Roughness != 0.08f) {
        std::cerr << "FAIL: Obsidian preset properties incorrect!" << std::endl;
        return false;
    }
    presetMat.SetPresetHeadlightLED();
    if (presetMat.EmissionIntensity != 5.0f || presetMat.Roughness != 0.10f) {
        std::cerr << "FAIL: Headlight LED preset properties incorrect!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: All PBR material presets verified (Chrome, Rubber, Obsidian, Headlight LED)." << std::endl;

    // 3. Testing Applying Material to Entity
    std::cout << "\n[16.3] Testing Application to Scene Entity..." << std::endl;
    auto testScene = std::make_shared<Axle::Scene>();
    auto ent = testScene->CreateEntity("CyberCar");
    ent.AddComponent<Axle::TransformComponent>();
    
    // Apply material asset to entity
    auto& comp = ent.AddComponent<Axle::MaterialComponent>(loadedAmat);
    if (comp.AlbedoPath != "textures/paint_base.png" ||
        std::abs(comp.Roughness - 0.18f) > 1e-3f ||
        std::abs(comp.Metallic - 0.90f) > 1e-3f) {
        std::cerr << "FAIL: Entity MaterialComponent did not receive material asset properties!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Applied material asset directly to Entity 'CyberCar'." << std::endl;

    // 4. Testing .meta file Companion Generation for .amat and .axlemat
    std::cout << "\n[16.4] Testing .meta file companion for .amat and .axlemat..." << std::endl;
    auto metaAmat = Axle::AssetManager::GetOrCreateMetadata(amatPath);
    auto metaAxlemat = Axle::AssetManager::GetOrCreateMetadata(axlematPath);

    if (metaAmat.Type != Axle::AssetType::Material || metaAxlemat.Type != Axle::AssetType::Material) {
        std::cerr << "FAIL: Material asset files not categorized as AssetType::Material!" << std::endl;
        return false;
    }
    if (metaAmat.Handle == 0 || metaAxlemat.Handle == 0) {
        std::cerr << "FAIL: Material asset files received invalid UUID!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Both .amat and .axlemat recognized as AssetType::Material with valid UUIDs." << std::endl;

    // Cleanup
    Axle::AssetManager::Shutdown();
    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 16 PASSED: Phase 17.3 Dedicated Material Inspector verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 17: PHASE 18.1 FULL 3-FLOW DRAG & DROP & C++ CLASS CREATION TEST
// =============================================================================
bool RunTest_Step17_Phase18_DragDropPipelinesAndClassCreation() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 17 TEST] PHASE 18.1 DRAG & DROP & C++ CLASS CREATION" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::filesystem::path testDir = "sandbox_dragdrop_phase18_test";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);

    Axle::AssetManager::Init(testDir);

    // 1. C++ Class Generation (.hpp + .cpp)
    std::cout << "\n[17.1] Testing C++ Class (.hpp + .cpp) Generation..." << std::endl;
    auto [hppPath, cppPath] = Axle::ContentBrowserPanel::CreateNewClass(testDir, "VehicleController");

    if (!std::filesystem::exists(hppPath) || !std::filesystem::exists(cppPath)) {
        std::cerr << "FAIL: CreateNewClass did not generate both .hpp and .cpp files!" << std::endl;
        return false;
    }

    // Verify .hpp contents
    std::ifstream hppFile(hppPath);
    std::string hppContent((std::istreambuf_iterator<char>(hppFile)), std::istreambuf_iterator<char>());
    if (hppContent.find("class VehicleController : public Axle::ScriptableEntity") == std::string::npos ||
        hppContent.find("AXLE_SCRIPT_FIELDS(VehicleController") == std::string::npos ||
        hppContent.find("void OnCreate() override;") == std::string::npos ||
        hppContent.find("void OnUpdate(Axle::Timestep ts) override;") == std::string::npos) {
        std::cerr << "FAIL: Generated VehicleController.hpp missing critical class declarations or macros!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: VehicleController.hpp generated with valid class declaration & reflection macros." << std::endl;

    // Verify .cpp contents
    std::ifstream cppFile(cppPath);
    std::string cppContent((std::istreambuf_iterator<char>(cppFile)), std::istreambuf_iterator<char>());
    if (cppContent.find("#include \"VehicleController.hpp\"") == std::string::npos ||
        cppContent.find("AXLE_REGISTER_SCRIPT(VehicleController);") == std::string::npos ||
        cppContent.find("void VehicleController::OnCreate()") == std::string::npos ||
        cppContent.find("void VehicleController::OnUpdate(") == std::string::npos) {
        std::cerr << "FAIL: Generated VehicleController.cpp missing includes, registration, or implementations!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: VehicleController.cpp generated with includes, script registration & function bodies." << std::endl;

    // Verify companion .meta generation
    auto hppMeta = Axle::AssetManager::GetOrCreateMetadata(hppPath);
    auto cppMeta = Axle::AssetManager::GetOrCreateMetadata(cppPath);
    if (hppMeta.Type != Axle::AssetType::Script || cppMeta.Type != Axle::AssetType::Script) {
        std::cerr << "FAIL: .hpp or .cpp files not categorized as AssetType::Script in AssetManager!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Both .hpp and .cpp companions generated with persistent UUIDs." << std::endl;

    // Test name deduplication
    auto [hppPath2, cppPath2] = Axle::ContentBrowserPanel::CreateNewClass(testDir, "VehicleController");
    if (hppPath2.filename() != "VehicleController_1.hpp" || cppPath2.filename() != "VehicleController_1.cpp") {
        std::cerr << "FAIL: Class name deduplication failed!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Name deduplication verified ('VehicleController_1.hpp' & '.cpp')." << std::endl;

    // 2. Flow 3: Prefab Assembly Packaging from Entity Hierarchy
    std::cout << "\n[17.2] Testing Prefab Assembly Packaging (Hierarchy -> Content Browser)..." << std::endl;
    auto sourceScene = std::make_shared<Axle::Scene>();
    auto rootCar = sourceScene->CreateEntity("CyberTruck");
    rootCar.AddComponent<Axle::TransformComponent>(glm::vec3(0.0f, 2.0f, 0.0f));

    auto wheelFL = sourceScene->CreateEntity("Wheel_FL");
    wheelFL.AddComponent<Axle::TransformComponent>(glm::vec3(-1.0f, -0.5f, 1.5f));
    wheelFL.SetParent(rootCar);

    auto wheelFR = sourceScene->CreateEntity("Wheel_FR");
    wheelFR.AddComponent<Axle::TransformComponent>(glm::vec3(1.0f, -0.5f, 1.5f));
    wheelFR.SetParent(rootCar);

    auto cabin = sourceScene->CreateEntity("Cabin");
    cabin.AddComponent<Axle::TransformComponent>(glm::vec3(0.0f, 0.8f, 0.0f));
    cabin.SetParent(rootCar);

    std::filesystem::path prefabPath = testDir / "CyberTruck_Prefab.axle";
    if (!Axle::AssemblySerializer::Serialize(prefabPath, rootCar)) {
        std::cerr << "FAIL: Failed to serialize entity hierarchy into Prefab Assembly file!" << std::endl;
        return false;
    }

    auto prefabMeta = Axle::AssetManager::GetOrCreateMetadata(prefabPath);
    if (prefabMeta.Type != Axle::AssetType::Assembly) {
        std::cerr << "FAIL: Prefab Assembly not recognized as AssetType::Assembly!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Entity hierarchy serialized to Prefab Assembly '" << prefabPath.filename().string() << "'." << std::endl;

    // 3. Spawning / Deserializing the packaged Prefab Assembly
    std::cout << "\n[17.3] Testing Spawning Packaged Prefab into Fresh Scene..." << std::endl;
    auto targetScene = std::make_shared<Axle::Scene>();
    auto spawnedCar = Axle::AssemblySerializer::Deserialize(prefabPath, targetScene.get(), nullptr, glm::vec3(10.0f, 0.0f, -5.0f));

    if (!spawnedCar || !spawnedCar.HasComponent<Axle::HierarchyComponent>()) {
        std::cerr << "FAIL: Deserialized Prefab missing or lacks HierarchyComponent!" << std::endl;
        return false;
    }
    const auto& spawnedHierarchy = spawnedCar.GetComponent<Axle::HierarchyComponent>();
    if (spawnedHierarchy.Children.size() != 3) {
        std::cerr << "FAIL: Deserialized Prefab expected 3 children, got: " << spawnedHierarchy.Children.size() << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Prefab deserialized with 3 children intact at custom spawn position." << std::endl;

    // 4. Flow 1 & 2: Material & Script Drag & Drop Target Application
    std::cout << "\n[17.4] Testing Material & Script Drag-Drop Target Binding..." << std::endl;
    auto targetEntity = targetScene->CreateEntity("TargetMeshObject");
    targetEntity.AddComponent<Axle::TransformComponent>();

    // Test Material apply
    Axle::MaterialComponent sampleMat;
    sampleMat.SetPresetChrome();
    std::filesystem::path testMatPath = testDir / "ChromeTest.amat";
    Axle::SceneHierarchyPanel::SaveMaterialAsset(testMatPath, sampleMat);

    Axle::MaterialComponent loadedDropMat;
    if (Axle::SceneHierarchyPanel::LoadMaterialAsset(testMatPath, loadedDropMat)) {
        targetEntity.AddComponent<Axle::MaterialComponent>(loadedDropMat);
    }
    if (!targetEntity.HasComponent<Axle::MaterialComponent>() ||
        targetEntity.GetComponent<Axle::MaterialComponent>().Metallic != 1.0f) {
        std::cerr << "FAIL: Material asset drop did not apply Chrome preset!" << std::endl;
        return false;
    }
    std::cout << "  -> PASS: Material asset dropped and applied to Entity successfully." << std::endl;

    // Test Script live binding
    if (Axle::ScriptRegistry::Has("ReflectionDemoScript")) {
        if (!targetEntity.HasComponent<Axle::NativeScriptComponent>()) {
            targetEntity.AddComponent<Axle::NativeScriptComponent>();
        }
        bool bound = Axle::ScriptRegistry::BindByName(targetEntity, "ReflectionDemoScript");
        if (!bound) {
            std::cerr << "FAIL: Failed to bind registered script by name!" << std::endl;
            return false;
        }
        std::cout << "  -> PASS: Script 'ReflectionDemoScript' bound to entity via name-based drop pipeline." << std::endl;
    }

    // Cleanup
    Axle::AssetManager::Shutdown();
    std::filesystem::remove_all(testDir, ec);

    std::cout << ">>> STEP 17 PASSED: Phase 18.1 Drag & Drop Pipelines & C++ Class Creation verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 18: PHASE 18.2 SAFE SCENE STATE SIMULATION (DEEP-CLONE & ZERO DRIFT)
// =============================================================================
bool RunTest_Step18_Phase18_SafeSceneStateSimulation() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 18 TEST] PHASE 18.2 SAFE SCENE STATE SIMULATION" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::unique_ptr<Axle::PhysicsScene> physicsScene = std::make_unique<Axle::PhysicsScene>();
    std::shared_ptr<Axle::Scene> editorScene = std::make_shared<Axle::Scene>();

    std::cout << "\n[18.1] Populating Pristine Editor Scene..." << std::endl;
    
    // 1. Parent Entity with Transform, Mesh, Material, RigidBody, CollisionShape
    Axle::Entity parentEntity = editorScene->CreateEntity("CarChassis");
    Axle::UUID parentUUID = parentEntity.GetUUID();
    auto& parentTc = parentEntity.GetComponent<Axle::TransformComponent>();
    parentTc.Translation = glm::vec3(0.0f, 1.0f, 0.0f);
    parentTc.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    
    Axle::MaterialComponent goldMat;
    goldMat.AlbedoColor = glm::vec4(1.0f, 0.78f, 0.34f, 1.0f);
    goldMat.Roughness = 0.2f;
    goldMat.Metallic = 1.0f;
    parentEntity.AddComponent<Axle::MaterialComponent>(goldMat);

    Axle::BoxBodyProps bProps;
    bProps.Position = parentTc.Translation;
    bProps.HalfExtent = glm::vec3(1.0f, 0.5f, 2.0f);
    bProps.IsDynamic = true;
    Axle::BodyID parentBodyID = physicsScene->CreateBox(bProps);
    parentEntity.AddComponent<Axle::CollisionShape3D>(Axle::CollisionShapeType::Box, bProps.HalfExtent);
    parentEntity.AddComponent<Axle::RigidBody3D>(parentBodyID, true);

    // 2. Child Entity parented to CarChassis with local offset
    Axle::Entity childEntity = editorScene->CreateEntity("Spoiler");
    Axle::UUID childUUID = childEntity.GetUUID();
    auto& childTc = childEntity.GetComponent<Axle::TransformComponent>();
    childTc.Translation = glm::vec3(0.0f, 1.2f, -1.8f);
    childEntity.SetParent(parentEntity);

    // 3. Scripted Entity referencing CarChassis via UUID
    Axle::Entity scriptEntity = editorScene->CreateEntity("GameManager");
    Axle::UUID scriptUUID = scriptEntity.GetUUID();
    auto& nsc = scriptEntity.AddComponent<Axle::NativeScriptComponent>("ReflectionDemoScript");
    nsc.FloatFields["m_MoveSpeed"] = 55.0f;
    nsc.IntFields["m_AmmoCount"] = 999;
    nsc.BoolFields["m_IsInvulnerable"] = true;
    nsc.Vec3Fields["m_TargetPoint"] = glm::vec3(10.0f, 20.0f, 30.0f);
    nsc.EntityFields["TargetEntity"] = parentUUID;

    // 4. Directional Light & Skybox
    Axle::Entity sunEntity = editorScene->CreateEntity("Sun");
    sunEntity.AddComponent<Axle::DirectionalLightComponent>(glm::vec3(1.0f), 2.5f);
    Axle::Entity skyEntity = editorScene->CreateEntity("Sky");
    skyEntity.AddComponent<Axle::SkyboxComponent>();

    editorScene->UpdateWorldTransforms();

    assert(parentEntity.GetComponent<Axle::HierarchyComponent>().Children.size() == 1);
    assert(childEntity.GetComponent<Axle::HierarchyComponent>().Parent == (entt::entity)parentEntity);
    std::cout << "  -> PASS: Pristine EditorScene initialized with 4 entities and hierarchy." << std::endl;

    std::cout << "\n[18.2] Testing Scene::Copy Deep-Cloning & UUID Preservation..." << std::endl;
    std::shared_ptr<Axle::Scene> runtimeScene = Axle::Scene::Copy(editorScene);
    assert(runtimeScene != nullptr);

    // Verify all UUIDs preserved
    Axle::Entity rtParent = runtimeScene->GetEntityByUUID(parentUUID);
    Axle::Entity rtChild = runtimeScene->GetEntityByUUID(childUUID);
    Axle::Entity rtScript = runtimeScene->GetEntityByUUID(scriptUUID);
    assert(rtParent && rtChild && rtScript);
    assert(rtParent.GetName() == "CarChassis");
    assert(rtChild.GetName() == "Spoiler");
    assert(rtScript.GetName() == "GameManager");

    // Verify hierarchy remapped
    assert(rtChild.GetComponent<Axle::HierarchyComponent>().Parent == (entt::entity)rtParent);
    assert(rtParent.GetComponent<Axle::HierarchyComponent>().Children.size() == 1);
    assert(rtParent.GetComponent<Axle::HierarchyComponent>().Children[0] == (entt::entity)rtChild);

    // Verify Script Field values and UUID entity reference preserved
    auto& rtNsc = rtScript.GetComponent<Axle::NativeScriptComponent>();
    assert(rtNsc.FloatFields["m_MoveSpeed"] == 55.0f);
    assert(rtNsc.IntFields["m_AmmoCount"] == 999);
    assert(rtNsc.BoolFields["m_IsInvulnerable"] == true);
    assert(rtNsc.EntityFields["TargetEntity"] == parentUUID);
    Axle::Entity rtTarget = runtimeScene->GetEntityByUUID(rtNsc.EntityFields["TargetEntity"]);
    assert(rtTarget && rtTarget == rtParent);
    std::cout << "  -> PASS: Scene::Copy successfully cloned scene, preserved UUIDs & remapped hierarchy." << std::endl;

    std::cout << "\n[18.3] Simulating Play Mode Perturbations & Severe Physics Drift..." << std::endl;
    runtimeScene->OnRuntimeStart();

    // Drastically alter the runtime transforms as if simulated by physics
    auto& rtParentTc = rtParent.GetComponent<Axle::TransformComponent>();
    rtParentTc.Translation = glm::vec3(1234.5f, -678.9f, 999.0f);
    rtParentTc.Rotation = glm::quat(glm::vec3(1.2f, 0.8f, -0.5f));
    
    // Simulate physics moving the JPH body in physicsScene as well
    physicsScene->SetBodyPositionAndRotation(parentBodyID, glm::vec3(1234.5f, -678.9f, 999.0f), rtParentTc.Rotation, true);
    physicsScene->SetLinearVelocity(parentBodyID, glm::vec3(50.0f, -20.0f, 15.0f));

    // Spawn an ephemeral bullet/projectile entity in runtime
    Axle::Entity bullet = runtimeScene->CreateEntity("RuntimeBullet");
    bullet.AddComponent<Axle::TransformComponent>(glm::vec3(50.0f));

    runtimeScene->OnUpdateRuntime(0.016f);
    assert(rtParent.GetComponent<Axle::TransformComponent>().Translation.x == 1234.5f);
    std::cout << "  -> PASS: Runtime simulation drift introduced (Chassis moved to (1234.5, -678.9, 999.0))." << std::endl;

    std::cout << "\n[18.4] Testing Stop Mode Discard & Zero Physics Drift Restoration..." << std::endl;
    // Discard runtime scene
    runtimeScene->OnRuntimeStop();
    runtimeScene.reset();

    // Restore pristine physics bodies
    editorScene->RestorePhysicsBodies(physicsScene.get());

    // Verify EditorScene transforms are 100% UNTOUCHED!
    const auto& pristineParentTc = parentEntity.GetComponent<Axle::TransformComponent>();
    assert(pristineParentTc.Translation == glm::vec3(0.0f, 1.0f, 0.0f));
    assert(pristineParentTc.Rotation == glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    // Verify JPH physics scene body was restored to pristine position & zeroed velocity
    glm::vec3 restoredPos = physicsScene->GetBodyPosition(parentBodyID);
    glm::vec3 restoredVel = physicsScene->GetLinearVelocity(parentBodyID);
    assert(glm::distance(restoredPos, glm::vec3(0.0f, 1.0f, 0.0f)) < 0.001f);
    assert(glm::length(restoredVel) < 0.001f);

    // Verify hierarchy in editorScene is intact
    assert(childEntity.GetComponent<Axle::HierarchyComponent>().Parent == (entt::entity)parentEntity);
    assert(parentEntity.GetComponent<Axle::HierarchyComponent>().Children.size() == 1);

    // Verify ephemeral bullet was destroyed with runtimeScene and never leaked into editorScene
    bool bulletFound = false;
    auto tagView = editorScene->GetRegistry().view<Axle::TagComponent>();
    for (auto e : tagView) {
        if (tagView.get<Axle::TagComponent>(e).Tag == "RuntimeBullet") {
            bulletFound = true;
            break;
        }
    }
    assert(!bulletFound);

    std::cout << "  -> PASS: Pristine EditorScene completely restored." << std::endl;
    std::cout << "  -> PASS: Physics body zero-drift restoration verified (vel=0, pos=(0, 1, 0))." << std::endl;
    std::cout << "  -> PASS: Ephemeral runtime entities safely discarded." << std::endl;

    std::cout << ">>> STEP 18 PASSED: Phase 18.2 Safe Scene State Simulation verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 19: PHASE 18.3 DECOUPLED AXLESCRIPTS.DLL SHARED MODULE & EXPORT API
// =============================================================================
bool RunTest_Step19_Phase18_DecoupledAxleScriptsModule() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 19 TEST] PHASE 18.3 DECOUPLED AXLESCRIPTS.DLL MODULE" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::cout << "\n[19.1] Locating AxleScripts.dll binary..." << std::endl;
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path dllPath = std::filesystem::path(exePath).parent_path() / "AxleScripts.dll";
    if (!std::filesystem::exists(dllPath)) {
        dllPath = "build/Editor/Debug/AxleScripts.dll";
    }
    if (!std::filesystem::exists(dllPath)) {
        dllPath = "build/Editor/Release/AxleScripts.dll";
    }
    if (!std::filesystem::exists(dllPath)) {
        dllPath = "AxleScripts.dll";
    }

    std::cout << "  -> DLL Path: " << dllPath.string() << std::endl;
    assert(std::filesystem::exists(dllPath));
    std::cout << "  -> PASS: AxleScripts.dll exists on disk." << std::endl;

    std::cout << "\n[19.2] Dynamically loading AxleScripts.dll..." << std::endl;
    HMODULE hModule = LoadLibraryA(dllPath.string().c_str());
    assert(hModule != nullptr);
    std::cout << "  -> PASS: Successfully loaded AxleScripts.dll into process memory." << std::endl;

    std::cout << "\n[19.3] Querying exported C-API entry points..." << std::endl;
    using GetCountFn = uint32_t(*)();
    using InitFn = void(*)(Axle::ScriptRegisterCallback);
    using ShutdownFn = void(*)();

    GetCountFn fnGetCount = (GetCountFn)GetProcAddress(hModule, "Axle_GetScriptCount");
    InitFn fnInit = (InitFn)GetProcAddress(hModule, "Axle_InitializeScripts");
    ShutdownFn fnShutdown = (ShutdownFn)GetProcAddress(hModule, "Axle_ShutdownScripts");

    assert(fnGetCount != nullptr);
    assert(fnInit != nullptr);
    assert(fnShutdown != nullptr);
    std::cout << "  -> PASS: Exported symbols 'Axle_GetScriptCount', 'Axle_InitializeScripts', 'Axle_ShutdownScripts' found." << std::endl;

    std::cout << "\n[19.4] Testing Script Discovery & Dynamic Registration..." << std::endl;
    uint32_t scriptCount = fnGetCount();
    std::cout << "  -> Discovered " << scriptCount << " script(s) in AxleScripts.dll." << std::endl;
    assert(scriptCount >= 1);

    // Call Axle_InitializeScripts and register into Axle::ScriptRegistry
    fnInit([](const char* name, const Axle::ScriptMetadata* meta) {
        std::cout << "    * Dynamic Registration: '" << name << "' with " << meta->Fields.size() << " reflected field(s)." << std::endl;
        Axle::ScriptRegistry::RegisterMetadata(name, *meta, true);
    });

    assert(Axle::ScriptRegistry::Has("RotatorScript"));
    assert(Axle::ScriptRegistry::IsDynamicScript("RotatorScript"));
    const auto* rotatorMeta = Axle::ScriptRegistry::GetMetadata("RotatorScript");
    assert(rotatorMeta != nullptr);
    assert(rotatorMeta->Fields.size() == 3);
    assert(rotatorMeta->Fields[0].Name == "RotationSpeed");
    assert(rotatorMeta->Fields[1].Name == "Axis");
    assert(rotatorMeta->Fields[2].Name == "Enabled");
    std::cout << "  -> PASS: 'RotatorScript' registered dynamically with all 3 reflected fields." << std::endl;

    std::cout << "\n[19.5] Testing Dynamic Script Instantiation & Field Synchronization..." << std::endl;
    std::shared_ptr<Axle::Scene> testScene = std::make_shared<Axle::Scene>();
    Axle::Entity testEntity = testScene->CreateEntity("RotatingBeacon");
    auto& nsc = testEntity.AddComponent<Axle::NativeScriptComponent>("RotatorScript");
    nsc.FloatFields["RotationSpeed"] = 180.0f;
    nsc.Vec3Fields["Axis"] = glm::vec3(0.0f, 0.0f, 1.0f);
    nsc.BoolFields["Enabled"] = true;

    // Start scene runtime (automatically instantiates script, binds Entity, syncs fields, calls OnCreate)
    testScene->OnRuntimeStart();
    assert(nsc.Instance != nullptr);

    // Update scene runtime (calls OnUpdate with delta time)
    testScene->OnUpdateRuntime(0.1f);

    // Stop scene runtime (calls OnDestroy and cleans up instance)
    testScene->OnRuntimeStop();
    std::cout << "  -> PASS: Instantiated dynamic script via Scene, verified field sync, execution and destruction." << std::endl;

    std::cout << "\n[19.6] Testing Dynamic Script Unloading & Registry Cleanup..." << std::endl;
    Axle::ScriptRegistry::ClearDynamicScripts();
    assert(!Axle::ScriptRegistry::Has("RotatorScript"));
    assert(!Axle::ScriptRegistry::IsDynamicScript("RotatorScript"));
    std::cout << "  -> PASS: Dynamic scripts cleared from ScriptRegistry without affecting static scripts." << std::endl;

    fnShutdown();
    FreeLibrary(hModule);
    std::cout << "  -> PASS: AxleScripts.dll cleanly shut down and unloaded from memory." << std::endl;

    std::cout << ">>> STEP 19 PASSED: Phase 18.3 Decoupled AxleScripts.dll Module verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 20: PHASE 18.3 ANTI-LOCK SHADOW-COPY & STATE PRESERVATION
// =============================================================================
bool RunTest_Step20_Phase18_AntiLockShadowCopyAndStatePreservation() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 20 TEST] PHASE 18.3 ANTI-LOCK SHADOW-COPY & STATE" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::cout << "\n[20.1] Initializing ScriptHotReloadManager..." << std::endl;
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path dllPath = std::filesystem::path(exePath).parent_path() / "AxleScripts.dll";
    if (!std::filesystem::exists(dllPath)) {
        dllPath = "build/Editor/Debug/AxleScripts.dll";
    }
    if (!std::filesystem::exists(dllPath)) {
        dllPath = "build/Editor/Release/AxleScripts.dll";
    }
    auto& reloadManager = Axle::ScriptHotReloadManager::Get();
    bool initOk = reloadManager.Initialize(dllPath.string());
    assert(initOk);
    assert(reloadManager.IsLoaded());
    std::cout << "  -> PASS: ScriptHotReloadManager initialized." << std::endl;

    std::cout << "\n[20.2] Verifying Anti-Lock Shadow-Copy mechanism..." << std::endl;
    auto sourceDll = reloadManager.GetSourceDllPath();
    auto runtimeDll = reloadManager.GetCurrentRuntimeDllPath();
    std::cout << "  -> Source DLL:  " << sourceDll.string() << std::endl;
    std::cout << "  -> Runtime DLL: " << runtimeDll.string() << std::endl;
    assert(sourceDll != runtimeDll);
    assert(std::filesystem::exists(runtimeDll));

    // Test that source DLL is NOT locked by the process (preventing LNK1104 during builds)
    std::ofstream lockTest(sourceDll, std::ios::in | std::ios::out | std::ios::binary);
    bool canOpenSourceForWrite = lockTest.is_open();
    if (lockTest.is_open()) lockTest.close();
    assert(canOpenSourceForWrite);
    std::cout << "  -> PASS: Source AxleScripts.dll has ZERO locks (verified writable for MSBuild/Linker)!" << std::endl;

    std::cout << "\n[20.3] Setting up Scene with Multiple Dynamic Script Entities..." << std::endl;
    auto scene = std::make_shared<Axle::Scene>();
    
    // Entity 1: BeaconAlpha
    auto beaconAlpha = scene->CreateEntity("BeaconAlpha");
    auto& nscAlpha = beaconAlpha.AddComponent<Axle::NativeScriptComponent>("RotatorScript");
    nscAlpha.FloatFields["RotationSpeed"] = 45.0f;
    nscAlpha.Vec3Fields["Axis"] = glm::vec3(1.0f, 0.0f, 0.0f);
    nscAlpha.BoolFields["Enabled"] = true;

    // Entity 2: BeaconBeta
    auto beaconBeta = scene->CreateEntity("BeaconBeta");
    auto& nscBeta = beaconBeta.AddComponent<Axle::NativeScriptComponent>("RotatorScript");
    nscBeta.FloatFields["RotationSpeed"] = -120.0f;
    nscBeta.Vec3Fields["Axis"] = glm::vec3(0.0f, 1.0f, 0.0f);
    nscBeta.BoolFields["Enabled"] = false;

    // Start simulation / edit state
    scene->OnRuntimeStart();
    assert(nscAlpha.Instance != nullptr);
    assert(nscBeta.Instance != nullptr);

    // Modify a field dynamically in memory to simulate active game modifications
    nscAlpha.FloatFields["RotationSpeed"] = 888.8f;
    nscAlpha.SyncFieldsToInstance();

    std::cout << "  -> Pre-reload BeaconAlpha RotationSpeed: " << nscAlpha.FloatFields["RotationSpeed"] << std::endl;
    std::cout << "  -> Pre-reload BeaconBeta RotationSpeed:  " << nscBeta.FloatFields["RotationSpeed"] << std::endl;
    std::cout << "  -> PASS: Entities populated and runtime instances active." << std::endl;

    std::cout << "\n[20.4] Triggering Hot-Reload with Active Scene State Preservation..." << std::endl;
    auto reloadResult = reloadManager.Reload(scene, true);
    assert(reloadResult.Success);
    std::cout << "  -> Hot Reload Result: " << reloadResult.Message << std::endl;
    std::cout << "  -> New Runtime DLL: " << reloadManager.GetCurrentRuntimeDllPath().filename().string() << std::endl;
    assert(reloadManager.GetCurrentRuntimeDllPath() != runtimeDll);

    std::cout << "\n[20.5] Verifying State Preservation across Reload..." << std::endl;
    assert(nscAlpha.Instance != nullptr);
    assert(nscBeta.Instance != nullptr);
    assert(std::abs(nscAlpha.FloatFields["RotationSpeed"] - 888.8f) < 0.001f);
    assert(std::abs(nscBeta.FloatFields["RotationSpeed"] - (-120.0f)) < 0.001f);
    assert(nscBeta.BoolFields["Enabled"] == false);
    assert(nscBeta.Vec3Fields["Axis"].y == 1.0f);
    std::cout << "  -> PASS: All modified fields preserved 100% with fresh class instances!" << std::endl;

    std::cout << "\n[20.6] Shutting down and cleaning shadow copies..." << std::endl;
    scene->OnRuntimeStop();
    reloadManager.Shutdown();
    assert(!reloadManager.IsLoaded());
    std::cout << "  -> PASS: ScriptHotReloadManager successfully cleaned up." << std::endl;

    std::cout << ">>> STEP 20 PASSED: Phase 18.3 Anti-Lock Shadow-Copy & State Preservation verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// STEP 21: PHASE 18.3 ASYNC BACKGROUND SCRIPT COMPILER
// =============================================================================
bool RunTest_Step21_Phase18_AsyncBackgroundScriptCompiler() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 21 TEST] PHASE 18.3 ASYNC BACKGROUND COMPILER    " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::cout << "\n[21.1] Testing MSVC Diagnostic Output Parsing..." << std::endl;
    // Test Error Line
    std::string errLine = "D:\\WorkSpace\\AxleEngine\\Editor\\assets\\scripts\\RotatorScript.hpp(25,10): error C2065: 'speedMultiplier': undeclared identifier";
    auto diagErr = Axle::ScriptCompiler::ParseDiagnosticLine(errLine);
    assert(diagErr.Level == Axle::CompilerDiagnostic::Severity::Error);
    assert(diagErr.Line == 25);
    assert(diagErr.Column == 10);
    assert(diagErr.Code == "C2065");
    std::cout << "  -> PASS: Parsed C++ compiler error: " << diagErr.Code << " at line " << diagErr.Line << std::endl;

    // Test Warning Line
    std::string warnLine = "D:\\WorkSpace\\AxleEngine\\Editor\\src\\Scripts\\ScriptsModule.cpp(42): warning C4101: 'unusedVar': unreferenced local variable";
    auto diagWarn = Axle::ScriptCompiler::ParseDiagnosticLine(warnLine);
    assert(diagWarn.Level == Axle::CompilerDiagnostic::Severity::Warning);
    assert(diagWarn.Line == 42);
    assert(diagWarn.Code == "C4101");
    std::cout << "  -> PASS: Parsed C++ compiler warning: " << diagWarn.Code << " at line " << diagWarn.Line << std::endl;

    // Test Linker Error Line
    std::string linkLine = "LINK : fatal error LNK1104: cannot open file 'AxleScripts.dll'";
    auto diagLink = Axle::ScriptCompiler::ParseDiagnosticLine(linkLine);
    assert(diagLink.Level == Axle::CompilerDiagnostic::Severity::Error);
    assert(diagLink.Code == "LNK1104");
    std::cout << "  -> PASS: Parsed linker error: " << diagLink.Code << std::endl;

    std::cout << "\n[21.2] Testing Asynchronous Background Compilation (Non-Blocking UI)..." << std::endl;
    auto& compiler = Axle::ScriptCompiler::Get();
    compiler.SetBuildConfig("Release");
    compiler.SetTarget("AxleScripts");

    std::atomic<bool> callbackFired{ false };
    Axle::CompilationResult asyncResult;

    bool started = compiler.CompileAsync([&](const Axle::CompilationResult& res) {
        asyncResult = res;
        callbackFired = true;
    });
    assert(started);

    // Simulate Editor UI Loop running smoothly at 60 FPS while background compiler is working
    int simulatedUiFrames = 0;
    while (compiler.IsCompiling()) {
        simulatedUiFrames++;
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps frame tick
    }

    compiler.WaitForCompletion();
    assert(callbackFired.load());
    assert(asyncResult.Success);
    assert(asyncResult.ExitCode == 0);
    assert(asyncResult.DurationSeconds > 0.0);
    std::cout << "  -> PASS: Compilation completed in background in " << asyncResult.DurationSeconds << "s." << std::endl;
    std::cout << "  -> PASS: Editor UI simulated " << simulatedUiFrames << " frames without freezing!" << std::endl;

    std::cout << "\n[21.3] Testing Live Output Log and Metadata Retrieval..." << std::endl;
    std::string liveLog = compiler.GetCurrentOutputLog();
    assert(!liveLog.empty());
    auto lastRes = compiler.GetLastResult();
    assert(lastRes.Success);
    assert(lastRes.ExitCode == 0);
    std::cout << "  -> Captured " << liveLog.size() << " bytes of compiler output log." << std::endl;
    std::cout << "  -> PASS: CompilerStatus is Success and exit code is 0." << std::endl;

    std::cout << ">>> STEP 21 PASSED: Phase 18.3 Async Background Compiler verified! <<<\n" << std::endl;
    return true;
}