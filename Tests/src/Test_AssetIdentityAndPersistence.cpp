#include "TestWorkspace.hpp"
#include <Axle/AssetManager.hpp>
#include <Axle/AssetRegistrySerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/Components.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

#define ASSET_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "  [FAIL] Assertion failed: (" #cond ") - " << (msg) << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (false)

namespace {

    std::filesystem::path GetSourceFixtureRoot() {
        return std::filesystem::path(AXLE_TEST_SOURCE_DIR) / "Tests" / "Fixtures" / "AssetIdentity";
    }

    std::filesystem::path CreateUniqueWorkspace(const std::string& name) {
        static uint64_t counter = 0;
        std::filesystem::path p = std::filesystem::current_path() / "asset-tests" / (name + "_" + std::to_string(++counter));
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
        std::filesystem::create_directories(p, ec);
        return p;
    }

    void AssertZeroSidecars(const std::filesystem::path& root, const std::string& context) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext == ".meta" || ext == ".uid" || ext == ".import") {
                std::cerr << "  [FAIL] Sidecar file detected: " << entry.path().generic_string() << " in " << context << "\n";
                throw std::runtime_error("Sidecar file detected: " + entry.path().generic_string());
            }
        }
    }

    // -------------------------------------------------------------------------
    // 1. Clean Checkout Reference Resolution
    // -------------------------------------------------------------------------
    bool TestCleanCheckoutReferenceResolution() {
        std::cout << "  [RUN] TestCleanCheckoutReferenceResolution...\n";
        auto workspace = CreateUniqueWorkspace("clean_checkout");
        auto fixtureSource = GetSourceFixtureRoot() / "AuthoredProject";
        ASSET_CHECK(std::filesystem::exists(fixtureSource), "Fixture AuthoredProject must exist");

        // Simulate fresh checkout by copying fixture without any cache or machine paths
        std::error_code ec;
        std::filesystem::copy(fixtureSource, workspace, std::filesystem::copy_options::recursive, ec);
        ASSET_CHECK(!ec, "Copying fixture to clean checkout workspace must succeed");

        auto assetDir = workspace / "assets";
        Axle::AssetManager::Init(assetDir);

        // Verify authored UUIDs from root registry are loaded and preserved exactly
        Axle::UUID textureUUID = Axle::AssetManager::GetAssetUUID("textures/test_texture.png");
        ASSET_CHECK(static_cast<uint64_t>(textureUUID) == 100000000000000001ULL, "Authored texture UUID must match exactly");

        Axle::UUID materialUUID = Axle::AssetManager::GetAssetUUID("materials/test_material.axleasset");
        ASSET_CHECK(static_cast<uint64_t>(materialUUID) == 100000000000000002ULL, "Authored material UUID must match exactly");

        // Verify scene deserialization resolves the exact same material reference
        Axle::Scene scene;
        Axle::SceneSerializer serializer(&scene);
        bool deserialized = serializer.Deserialize((assetDir / "scenes" / "test_scene.axlescene").string());
        ASSET_CHECK(deserialized, "Scene deserialization must succeed");

        bool foundEntity = false;
        auto view = scene.GetRegistry().view<Axle::MaterialComponent>();
        for (auto e : view) {
            const auto& mc = view.get<Axle::MaterialComponent>(e);
            ASSET_CHECK(static_cast<uint64_t>(mc.MaterialHandle) == 100000000000000002ULL,
                        "Serialized scene entity material UUID must resolve to authored material UUID");
            foundEntity = true;
        }
        ASSET_CHECK(foundEntity, "Entity with MaterialComponent must be found in scene");

        // Verify no sidecar files were created
        AssertZeroSidecars(workspace, "TestCleanCheckoutReferenceResolution");

        Axle::AssetManager::Shutdown();
        return true;
    }

    // -------------------------------------------------------------------------
    // 2. Project-Root Relocation
    // -------------------------------------------------------------------------
    bool TestProjectRootRelocation() {
        std::cout << "  [RUN] TestProjectRootRelocation...\n";
        auto origin = CreateUniqueWorkspace("reloc_origin");
        auto destination = CreateUniqueWorkspace("reloc_destination");
        auto fixtureSource = GetSourceFixtureRoot() / "AuthoredProject";

        std::error_code ec;
        std::filesystem::copy(fixtureSource, origin, std::filesystem::copy_options::recursive, ec);
        ASSET_CHECK(!ec, "Staging origin workspace must succeed");

        // Relocate entire project directory to destination
        std::filesystem::copy(origin, destination, std::filesystem::copy_options::recursive, ec);
        ASSET_CHECK(!ec, "Relocating project to destination must succeed");
        std::filesystem::remove_all(origin, ec); // Remove origin completely

        Axle::AssetManager::Init(destination / "assets");

        Axle::UUID texUUID = Axle::AssetManager::GetAssetUUID("textures/test_texture.png");
        ASSET_CHECK(static_cast<uint64_t>(texUUID) == 100000000000000001ULL, "UUID must remain invariant under project-root relocation");

        // Verify resolved absolute path points to new relocated destination
        auto absPath = Axle::AssetManager::GetAssetPath(texUUID);
        ASSET_CHECK(absPath.generic_string().find(destination.generic_string()) != std::string::npos,
                    "Asset path must resolve against new project root without baking old machine root");

        AssertZeroSidecars(destination, "TestProjectRootRelocation");

        Axle::AssetManager::Shutdown();
        return true;
    }

    // -------------------------------------------------------------------------
    // 3. Asset Relocation (Move / Rename) Preserves Identity
    // -------------------------------------------------------------------------
    bool TestAssetRelocationPreservesIdentity() {
        std::cout << "  [RUN] TestAssetRelocationPreservesIdentity...\n";
        auto workspace = CreateUniqueWorkspace("asset_relocate");
        auto fixtureSource = GetSourceFixtureRoot() / "AuthoredProject";

        std::error_code ec;
        std::filesystem::copy(fixtureSource, workspace, std::filesystem::copy_options::recursive, ec);
        auto assetDir = workspace / "assets";
        Axle::AssetManager::Init(assetDir);

        std::filesystem::path oldRelative = "textures/test_texture.png";
        std::filesystem::path newRelative = "textures/subfolder/renamed_texture.png";

        std::filesystem::path oldAbs = assetDir / oldRelative;
        std::filesystem::path newAbs = assetDir / newRelative;

        std::string err;
        bool relocated = Axle::AssetManager::RelocateAsset(oldAbs, newAbs, &err);
        ASSET_CHECK(relocated, "RelocateAsset must succeed: " + err);

        // Verify UUID is preserved exactly
        Axle::UUID newLookupUUID = Axle::AssetManager::GetAssetUUID(newRelative);
        ASSET_CHECK(static_cast<uint64_t>(newLookupUUID) == 100000000000000001ULL, "Relocated asset must retain exact same UUID");

        // Verify old path is no longer mapped
        ASSET_CHECK(!Axle::AssetManager::HasAsset(oldRelative), "Old path must no longer be present in registry");

        // Verify root registry file on disk reflects new relative path
        auto regPath = Axle::AssetManager::GetRegistryPath();
        ASSET_CHECK(std::filesystem::exists(regPath), "AssetRegistry.axleregistry must exist");

        std::unordered_map<Axle::UUID, Axle::AssetMetadata> reloadedEntries;
        std::unordered_map<std::string, Axle::UUID> reloadedPaths;
        std::vector<Axle::AssetDiagnostic> diags;
        ASSET_CHECK(Axle::AssetRegistrySerializer::Deserialize(regPath, reloadedEntries, reloadedPaths, &diags),
                    "Deserializing updated registry must succeed");
        ASSET_CHECK(reloadedEntries.find(newLookupUUID) != reloadedEntries.end(), "Relocated UUID must be present in registry");
        ASSET_CHECK(reloadedEntries[newLookupUUID].FilePath.generic_string() == "textures/subfolder/renamed_texture.png",
                    "Stored path in registry must match new relative path");

        // Verify zero sidecars
        AssertZeroSidecars(workspace, "TestAssetRelocationPreservesIdentity");

        Axle::AssetManager::Shutdown();
        return true;
    }

    // -------------------------------------------------------------------------
    // 4. Asset Copy Creates Brand-New Distinct Identity
    // -------------------------------------------------------------------------
    bool TestAssetCopyIdentity() {
        std::cout << "  [RUN] TestAssetCopyIdentity...\n";
        auto workspace = CreateUniqueWorkspace("asset_copy");
        auto fixtureSource = GetSourceFixtureRoot() / "AuthoredProject";

        std::error_code ec;
        std::filesystem::copy(fixtureSource, workspace, std::filesystem::copy_options::recursive, ec);
        auto assetDir = workspace / "assets";
        Axle::AssetManager::Init(assetDir);

        std::filesystem::path sourceRel = "textures/test_texture.png";
        std::filesystem::path copyRel = "textures/copied_texture.png";
        std::filesystem::path srcAbs = assetDir / sourceRel;
        std::filesystem::path copyAbs = assetDir / copyRel;

        std::string err;
        bool copied = Axle::AssetManager::CopyAsset(srcAbs, copyAbs, &err);
        ASSET_CHECK(copied, "CopyAsset must succeed: " + err);

        Axle::UUID srcUUID = Axle::AssetManager::GetAssetUUID(sourceRel);
        Axle::UUID copyUUID = Axle::AssetManager::GetAssetUUID(copyRel);

        ASSET_CHECK(static_cast<uint64_t>(srcUUID) == 100000000000000001ULL, "Original asset UUID must remain unchanged");
        ASSET_CHECK(copyUUID != 0, "Copied asset must receive a non-zero UUID");
        ASSET_CHECK(copyUUID != srcUUID, "Copied asset MUST NOT share the source UUID (must allocate fresh identity)");

        // Verify registry file has both entries
        auto regPath = Axle::AssetManager::GetRegistryPath();
        std::unordered_map<Axle::UUID, Axle::AssetMetadata> reloadedEntries;
        std::unordered_map<std::string, Axle::UUID> reloadedPaths;
        std::vector<Axle::AssetDiagnostic> diags;
        ASSET_CHECK(Axle::AssetRegistrySerializer::Deserialize(regPath, reloadedEntries, reloadedPaths, &diags),
                    "Deserializing updated registry after copy must succeed");
        ASSET_CHECK(reloadedEntries.find(copyUUID) != reloadedEntries.end(), "Copied UUID must be in registry");
        ASSET_CHECK(reloadedEntries.find(srcUUID) != reloadedEntries.end(), "Source UUID must be in registry");

        // Verify zero sidecars
        AssertZeroSidecars(workspace, "TestAssetCopyIdentity");

        Axle::AssetManager::Shutdown();
        return true;
    }

    // -------------------------------------------------------------------------
    // 5. External Move and Explicit Relink
    // -------------------------------------------------------------------------
    bool TestExternalMoveAndRelink() {
        std::cout << "  [RUN] TestExternalMoveAndRelink...\n";
        auto workspace = CreateUniqueWorkspace("external_relink");
        auto fixtureSource = GetSourceFixtureRoot() / "AuthoredProject";

        std::error_code ec;
        std::filesystem::copy(fixtureSource, workspace, std::filesystem::copy_options::recursive, ec);
        auto assetDir = workspace / "assets";
        Axle::AssetManager::Init(assetDir);

        Axle::UUID targetUUID = Axle::AssetManager::GetAssetUUID("textures/test_texture.png");
        ASSET_CHECK(static_cast<uint64_t>(targetUUID) == 100000000000000001ULL, "Original UUID must match");

        // Simulate external move outside the Editor: move file directly on filesystem
        std::filesystem::path oldAbs = assetDir / "textures" / "test_texture.png";
        std::filesystem::path movedAbs = assetDir / "textures" / "moved_outside.png";
        std::filesystem::rename(oldAbs, movedAbs, ec);
        ASSET_CHECK(!ec, "Filesystem external rename must succeed");

        // Now explicitly relink the UUID to the new path
        std::string err;
        bool relinked = Axle::AssetManager::RelinkAsset(targetUUID, "textures/moved_outside.png", &err);
        ASSET_CHECK(relinked, "RelinkAsset must succeed: " + err);

        // Verify UUID lookup on new path returns the exact original UUID
        Axle::UUID lookup = Axle::AssetManager::GetAssetUUID("textures/moved_outside.png");
        ASSET_CHECK(lookup == targetUUID, "Relinked path must resolve to original UUID");

        // Verify registry file reflects new path
        auto regPath = Axle::AssetManager::GetRegistryPath();
        std::unordered_map<Axle::UUID, Axle::AssetMetadata> reloadedEntries;
        std::unordered_map<std::string, Axle::UUID> reloadedPaths;
        std::vector<Axle::AssetDiagnostic> diags;
        ASSET_CHECK(Axle::AssetRegistrySerializer::Deserialize(regPath, reloadedEntries, reloadedPaths, &diags),
                    "Reading registry after RelinkAsset must succeed");
        ASSET_CHECK(reloadedEntries[targetUUID].FilePath.generic_string() == "textures/moved_outside.png",
                    "Persisted path in registry must be updated");

        AssertZeroSidecars(workspace, "TestExternalMoveAndRelink");

        Axle::AssetManager::Shutdown();
        return true;
    }

    // -------------------------------------------------------------------------
    // 6. Duplicate UUID Diagnostics and Recovery
    // -------------------------------------------------------------------------
    bool TestDuplicateUUIDDiagnostics() {
        std::cout << "  [RUN] TestDuplicateUUIDDiagnostics...\n";
        auto workspace = CreateUniqueWorkspace("duplicate_test");
        auto fixtureSource = GetSourceFixtureRoot() / "DuplicateProject";

        std::error_code ec;
        std::filesystem::copy(fixtureSource, workspace, std::filesystem::copy_options::recursive, ec);
        auto assetDir = workspace / "assets";

        Axle::AssetManager::Init(assetDir);

        // Registry has dup_a.png and dup_b.png sharing UUID 200000000000000001
        ASSET_CHECK(Axle::AssetManager::HasDiagnosticErrors(),
                    "Init on DuplicateProject must report diagnostic errors for duplicate UUID");

        // dup_a.png was registered first and should be valid; dup_b was colliding and rejected
        ASSET_CHECK(Axle::AssetManager::HasAsset("dup_a.png"), "First asset in duplicate registry must be preserved");
        ASSET_CHECK(!Axle::AssetManager::HasAsset("dup_b.png"), "Colliding second asset must be rejected");

        AssertZeroSidecars(workspace, "TestDuplicateUUIDDiagnostics");

        Axle::AssetManager::Shutdown();
        return true;
    }

    // -------------------------------------------------------------------------
    // 7. Corrupt & Unsupported Registry Diagnostics (No Silent Regeneration)
    // -------------------------------------------------------------------------
    bool TestCorruptAndUnsupportedRegistryDiagnostics() {
        std::cout << "  [RUN] TestCorruptAndUnsupportedRegistryDiagnostics...\n";

        // Corrupt registry test
        {
            auto workspace = CreateUniqueWorkspace("corrupt_test");
            auto fixtureSource = GetSourceFixtureRoot() / "CorruptProject";
            std::error_code ec;
            std::filesystem::copy(fixtureSource, workspace, std::filesystem::copy_options::recursive, ec);
            auto assetDir = workspace / "assets";

            Axle::AssetManager::Init(assetDir);
            ASSET_CHECK(Axle::AssetManager::HasDiagnosticErrors(), "Corrupt registry must emit diagnostic errors");
            ASSET_CHECK(!Axle::AssetManager::HasAsset("corrupt.png"), "Corrupt asset must not be registered");

            AssertZeroSidecars(workspace, "CorruptProject");
            Axle::AssetManager::Shutdown();
        }

        // Unsupported version test
        {
            auto workspace = CreateUniqueWorkspace("unsupported_version_test");
            auto fixtureSource = GetSourceFixtureRoot() / "UnsupportedVersion";
            std::error_code ec;
            std::filesystem::copy(fixtureSource, workspace, std::filesystem::copy_options::recursive, ec);
            auto assetDir = workspace / "assets";

            Axle::AssetManager::Init(assetDir);
            ASSET_CHECK(Axle::AssetManager::HasDiagnosticErrors(), "Unsupported version must emit diagnostic errors");
            ASSET_CHECK(!Axle::AssetManager::HasAsset("future.png"), "Unsupported version asset must not be registered");

            AssertZeroSidecars(workspace, "UnsupportedVersion");
            Axle::AssetManager::Shutdown();
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // 8. Missing Registry Entry for Already-Referenced Asset
    // -------------------------------------------------------------------------
    bool TestMissingRegistryEntryReferencedAsset() {
        std::cout << "  [RUN] TestMissingRegistryEntryReferencedAsset...\n";
        auto workspace = CreateUniqueWorkspace("missing_ref_test");
        auto assetDir = workspace / "assets";
        std::filesystem::create_directories(assetDir);
        Axle::AssetManager::Init(assetDir);

        // Create an unindexed asset file on disk
        std::ofstream(assetDir / "referenced_file.png") << "raw png content";

        // Track this asset as referenced in prior scene/material
        Axle::UUID expectedUUID{ 999111222333ULL };
        Axle::AssetManager::TrackReferencedAsset(expectedUUID, "referenced_file.png");

        Axle::AssetManager::ClearDiagnostics();
        auto meta = Axle::AssetManager::GetOrCreateMetadata(assetDir / "referenced_file.png");
        ASSET_CHECK(!meta.IsValid(), "Missing registry entry for referenced asset must NOT silently regenerate fresh UUID");
        ASSET_CHECK(Axle::AssetManager::HasDiagnosticErrors(), "Missing registry entry for referenced asset must report diagnostic error");

        AssertZeroSidecars(workspace, "TestMissingRegistryEntryReferencedAsset");

        Axle::AssetManager::Shutdown();
        return true;
    }

    // -------------------------------------------------------------------------
    // 9. New-Import Checked Atomic Persistence
    // -------------------------------------------------------------------------
    bool TestNewImportCheckedPersistence() {
        std::cout << "  [RUN] TestNewImportCheckedPersistence...\n";
        auto workspace = CreateUniqueWorkspace("new_import_test");
        auto assetDir = workspace / "assets";
        std::filesystem::create_directories(assetDir);
        Axle::AssetManager::Init(assetDir);

        std::filesystem::path newAsset = assetDir / "genuinely_new_texture.png";
        std::ofstream(newAsset) << "pixel data";

        auto meta = Axle::AssetManager::GetOrCreateMetadata(newAsset);
        ASSET_CHECK(meta.IsValid(), "Genuinely new import must succeed");
        ASSET_CHECK(meta.Handle != 0, "New import must be allocated a valid UUID");

        // Verify root registry file was created/updated on disk
        auto regPath = Axle::AssetManager::GetRegistryPath();
        ASSET_CHECK(std::filesystem::exists(regPath), "AssetRegistry.axleregistry must exist on disk");

        std::unordered_map<Axle::UUID, Axle::AssetMetadata> reloadedEntries;
        std::unordered_map<std::string, Axle::UUID> reloadedPaths;
        std::vector<Axle::AssetDiagnostic> diags;
        ASSET_CHECK(Axle::AssetRegistrySerializer::Deserialize(regPath, reloadedEntries, reloadedPaths, &diags),
                    "Reading persisted registry must succeed");
        ASSET_CHECK(reloadedEntries.find(meta.Handle) != reloadedEntries.end(), "Persisted UUID on disk must match published UUID");
        ASSET_CHECK(reloadedEntries[meta.Handle].FilePath.generic_string() == "genuinely_new_texture.png",
                    "Persisted path must match");

        // Assert zero sidecars
        AssertZeroSidecars(workspace, "TestNewImportCheckedPersistence");

        Axle::AssetManager::Shutdown();
        return true;
    }

    // -------------------------------------------------------------------------
    // 10. Deterministic Save Failure Preserves Prior Valid File
    // -------------------------------------------------------------------------
    bool TestDeterministicSaveFailurePreservesPriorFile() {
        std::cout << "  [RUN] TestDeterministicSaveFailurePreservesPriorFile...\n";
        auto workspace = CreateUniqueWorkspace("atomic_failure_test");
        std::filesystem::path targetFile = workspace / "critical_data.txt";

        std::string originalContent = "CRITICAL_ORIGINAL_PAYLOAD_V1\nLine 2: must never be lost.\n";
        bool wroteInitial = Axle::FileSystem::WriteTextFileAtomically(targetFile, originalContent);
        ASSET_CHECK(wroteInitial, "Initial atomic write must succeed");
        ASSET_CHECK(Axle::FileSystem::ReadTextFile(targetFile) == originalContent, "Initial content must match");

        // Attempt write where destination path collision is an existing directory
        // (MoveFileEx/rename into a directory path fails deterministically on all OSes)
        std::filesystem::path dirCollision = workspace / "dir_target";
        std::filesystem::create_directories(dirCollision);

        std::string outErr;
        bool failedWrite = Axle::FileSystem::WriteTextFileAtomically(dirCollision, "new data", &outErr);
        ASSET_CHECK(!failedWrite, "Atomic write targeting a directory must fail");
        ASSET_CHECK(!outErr.empty(), "Error message must be reported");

        // Verify the original file content was preserved completely
        ASSET_CHECK(Axle::FileSystem::ReadTextFile(targetFile) == originalContent,
                    "Prior valid file must be intact after failed write attempt");

        // Test ProjectSerializer deterministic atomic save
        std::filesystem::path projFile = workspace / "TestProj.axleproj";
        auto proj = Axle::Project::New(workspace);
        proj->GetConfig().Name = "PreservedProject";
        Axle::ProjectSerializer serializer(proj);
        ASSET_CHECK(serializer.Serialize(projFile), "Initial project serialize must succeed");

        std::string initialProjContent = Axle::FileSystem::ReadTextFile(projFile);
        ASSET_CHECK(!initialProjContent.empty(), "Project file content must not be empty");

        // Attempt saving project to an invalid target directory collision
        bool failedProjSave = serializer.Serialize(dirCollision);
        ASSET_CHECK(!failedProjSave, "Project serialize to directory target must fail");
        ASSET_CHECK(Axle::FileSystem::ReadTextFile(projFile) == initialProjContent,
                    "Project file content must remain byte-for-byte identical on failed serialize");

        // Test AssetRegistrySerializer deterministic atomic save failure
        std::unordered_map<Axle::UUID, Axle::AssetMetadata> dummyEntries;
        dummyEntries[Axle::UUID{ 12345 }] = Axle::AssetMetadata{ Axle::UUID{ 12345 }, Axle::AssetType::Texture, "a.png", 1 };
        std::filesystem::path validReg = workspace / "ValidReg.axleregistry";
        ASSET_CHECK(Axle::AssetRegistrySerializer::Serialize(validReg, dummyEntries), "Initial registry serialize must succeed");
        std::string regContent = Axle::FileSystem::ReadTextFile(validReg);

        bool failedRegSave = Axle::AssetRegistrySerializer::Serialize(dirCollision, dummyEntries);
        ASSET_CHECK(!failedRegSave, "Registry serialize to directory target must fail");
        ASSET_CHECK(Axle::FileSystem::ReadTextFile(validReg) == regContent,
                    "Registry file content must remain intact on failed serialize");

        return true;
    }

    // -------------------------------------------------------------------------
    // 11. Canonical Builtin Primitives
    // -------------------------------------------------------------------------
    bool TestCanonicalBuiltinPrimitives() {
        std::cout << "  [RUN] TestCanonicalBuiltinPrimitives...\n";
        auto workspace = CreateUniqueWorkspace("builtins_test");
        Axle::AssetManager::Init(workspace / "assets");

        std::vector<std::pair<std::string, uint64_t>> builtins = {
            { "builtin://cube",     Axle::BuiltinAssets::CubeUUID },
            { "builtin://sphere",   Axle::BuiltinAssets::SphereUUID },
            { "builtin://cylinder", Axle::BuiltinAssets::CylinderUUID },
            { "builtin://cone",     Axle::BuiltinAssets::ConeUUID },
            { "builtin://plane",    Axle::BuiltinAssets::PlaneUUID }
        };

        for (const auto& [uri, expectedUUID] : builtins) {
            ASSET_CHECK(Axle::AssetManager::IsBuiltin(uri), "AssetManager::IsBuiltin must recognize " + uri);
            Axle::UUID uuid = Axle::AssetManager::GetAssetUUID(uri);
            ASSET_CHECK(static_cast<uint64_t>(uuid) == expectedUUID, "Builtin UUID must match canonical constant");
            ASSET_CHECK(Axle::AssetManager::IsBuiltin(uuid), "AssetManager::IsBuiltin(UUID) must return true");

            auto path = Axle::AssetManager::GetAssetPath(uuid);
            ASSET_CHECK(path.generic_string() == uri, "AssetManager::GetAssetPath must return canonical URI");
        }

        // Verify NO sidecar files or registry entries were generated for builtins
        AssertZeroSidecars(workspace, "TestCanonicalBuiltinPrimitives");

        Axle::AssetManager::Shutdown();
        return true;
    }

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Axle::Tests::InitializeWorkspace();

    std::cout << "\n========================================================\n"
              << "       AxleEngine Asset Identity & Safe Persistence       \n"
              << "========================================================\n"
              << " [AF-01] Canonical Root Registry & Safe Persistence     \n"
              << "--------------------------------------------------------\n";

    struct TestCase {
        std::string Name;
        bool (*Function)();
    };

    std::vector<TestCase> cases = {
        { "Clean Checkout Reference Resolution",        TestCleanCheckoutReferenceResolution },
        { "Project-Root Relocation",                     TestProjectRootRelocation },
        { "Asset Relocation (Move/Rename) Identity",    TestAssetRelocationPreservesIdentity },
        { "Asset Copy Brand-New Identity",               TestAssetCopyIdentity },
        { "External Move and Explicit Relink",          TestExternalMoveAndRelink },
        { "Duplicate UUID Diagnostics & Recovery",      TestDuplicateUUIDDiagnostics },
        { "Corrupt & Unsupported Registry Diagnostics", TestCorruptAndUnsupportedRegistryDiagnostics },
        { "Missing Registry Entry for Referenced Asset", TestMissingRegistryEntryReferencedAsset },
        { "New-Import Checked Atomic Persistence",       TestNewImportCheckedPersistence },
        { "Deterministic Save Failure Preserves Prior",  TestDeterministicSaveFailurePreservesPriorFile },
        { "Canonical Builtin Primitives",                TestCanonicalBuiltinPrimitives },
    };

    size_t passed = 0;
    for (const auto& test : cases) {
        bool ok = false;
        try {
            ok = test.Function();
        } catch (const std::exception& e) {
            std::cerr << "  [EXCEPTION] in " << test.Name << ": " << e.what() << "\n";
            ok = false;
        } catch (...) {
            std::cerr << "  [EXCEPTION] unknown in " << test.Name << "\n";
            ok = false;
        }

        if (ok) {
            std::cout << "[PASS] " << test.Name << "\n";
            passed++;
        } else {
            std::cerr << "[FAIL] " << test.Name << "\n";
            return 1;
        }
    }

    std::cout << "\n--------------------------------------------------------\n"
              << " Result: " << passed << "/" << cases.size() << " test suites passed.\n"
              << "========================================================\n\n";

    return 0;
}
