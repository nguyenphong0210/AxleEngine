// Keep test assertions active in Release as well as Debug.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <Axle/AssetManager.hpp>
#include <Axle/AssetMetadataSerializer.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/Material.hpp>
#include <Axle/PhysicsMaterial.hpp>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cmath>

namespace {

    std::filesystem::path CreateTempDir(const std::string& name) {
        auto p = std::filesystem::temp_directory_path() / ("Axle_AssetUnit_" + name);
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
        std::filesystem::create_directories(p, ec);
        return p;
    }

    void CleanupTempDir(const std::filesystem::path& p) {
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
    }

    void TestHeadlessAssetManagerRegistry() {
        std::cout << "[RUN] TestHeadlessAssetManagerRegistry..." << std::endl;
        auto root = CreateTempDir("Registry");
        auto assetDir = root / "assets";
        std::filesystem::create_directories(assetDir);

        Axle::AssetManager::Init(assetDir);
        assert(Axle::AssetManager::GetAssetCount() == 0);

        // Write an identified native asset file
        auto matPath = assetDir / "Wood.axleasset";
        uint64_t targetUUID = 0x123456789ABCDEF0ULL;
        std::string content = "# AxleFileIdentity: 1 " + std::to_string(targetUUID) + "\nMaterial {\n    name: \"Wood\"\n    roughness: 0.65\n}\n";
        bool writeOk = Axle::FileSystem::WriteTextFile(matPath, content);
        assert(writeOk);

        Axle::AssetManager::Sync();
        assert(Axle::AssetManager::GetAssetCount() == 1);
        assert(Axle::AssetManager::HasAsset(Axle::UUID(targetUUID)));
        assert(Axle::AssetManager::GetAssetUUID(matPath) == Axle::UUID(targetUUID));

        auto relPath = Axle::AssetManager::GetRelativeAssetPath(Axle::UUID(targetUUID));
        assert(!relPath.empty());

        auto meta = Axle::AssetManager::GetMetadata(Axle::UUID(targetUUID));
        assert(meta != nullptr);
        assert(meta->Handle == Axle::UUID(targetUUID));

        // Test referenced asset tracking & diagnostics
        Axle::AssetManager::TrackReferencedAsset(0x9999999ULL, "missing.axleasset");
        assert(Axle::AssetManager::IsAssetReferenced(0x9999999ULL));
        Axle::AssetManager::ReportDiagnostic(Axle::AssetDiagnostic::Level::Error, "Missing asset", "missing.axleasset", 0x9999999ULL);
        assert(Axle::AssetManager::HasDiagnostics());
        assert(Axle::AssetManager::HasDiagnosticErrors());
        Axle::AssetManager::ClearDiagnostics();
        assert(!Axle::AssetManager::HasDiagnostics());

        Axle::AssetManager::Shutdown();
        CleanupTempDir(root);
        std::cout << "[PASS] TestHeadlessAssetManagerRegistry" << std::endl;
    }

    void TestAssetDocumentAndDSLCodec() {
        std::cout << "[RUN] TestAssetDocumentAndDSLCodec..." << std::endl;

        std::string dslSource =
            "Material {\n"
            "    name: \"BrushedMetal\"\n"
            "    albedoColor: \"#E0E0E0FF\"\n"
            "    roughness: 0.25\n"
            "    metallic: 0.95\n"
            "    bounds: [1.0, 2.0, 3.0]\n"
            "}\n";

        Axle::AssetDocument doc;
        std::string error;
        bool parseOk = Axle::AssetDSL::ParseSource(dslSource, doc, &error);
        assert(parseOk);
        assert(doc.IsValid());
        assert(doc.Root != nullptr);
        assert(doc.Root->Type == "Material");
        assert(doc.Root->GetString("name", "") == "BrushedMetal");
        assert(std::abs(doc.Root->GetFloat("roughness", 0.0f) - 0.25f) < 1e-4f);
        assert(std::abs(doc.Root->GetFloat("metallic", 0.0f) - 0.95f) < 1e-4f);

        auto bounds = doc.Root->GetVec3("bounds", glm::vec3(0.0f));
        assert(std::abs(bounds.x - 1.0f) < 1e-4f);
        assert(std::abs(bounds.y - 2.0f) < 1e-4f);
        assert(std::abs(bounds.z - 3.0f) < 1e-4f);

        std::cout << "[PASS] TestAssetDocumentAndDSLCodec" << std::endl;
    }

    void TestAssetSerializerCPUBoundary() {
        std::cout << "[RUN] TestAssetSerializerCPUBoundary..." << std::endl;
        auto root = CreateTempDir("MeshCPU");
        auto meshPath = root / "Cube.axle";
        auto binPath = root / ".cache" / "Cube.axlebin";

        Axle::AxleMeshSource src;
        src.Name = "TestCube";
        src.Vertices = {
            { -0.5f, -0.5f, 0.0f },
            {  0.5f, -0.5f, 0.0f },
            {  0.5f,  0.5f, 0.0f },
            { -0.5f,  0.5f, 0.0f }
        };
        src.Normals = {
            { 0.0f, 0.0f, 1.0f },
            { 0.0f, 0.0f, 1.0f },
            { 0.0f, 0.0f, 1.0f },
            { 0.0f, 0.0f, 1.0f }
        };
        src.UVs = {
            { 0.0f, 0.0f },
            { 1.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.0f, 1.0f }
        };
        src.Indices = { 0, 1, 2, 2, 3, 0 };
        src.BoundingSize = glm::vec3(1.0f, 1.0f, 0.0f);

        // Save to text
        bool saveTextOk = Axle::AssetSerializer::SaveToAxleText(meshPath, src);
        assert(saveTextOk);

        // Load from text
        Axle::AxleMeshSource loadedText;
        bool loadTextOk = Axle::AssetSerializer::LoadFromAxleText(meshPath, loadedText);
        assert(loadTextOk);
        assert(loadedText.Vertices.size() == 4);
        assert(loadedText.Indices.size() == 6);

        // Save to binary
        bool saveBinOk = Axle::AssetSerializer::SaveToAxleBinary(binPath, src);
        assert(saveBinOk);

        // Load from binary
        Axle::AxleMeshSource loadedBin;
        bool loadBinOk = Axle::AssetSerializer::LoadFromAxleBinary(binPath, loadedBin);
        assert(loadBinOk);
        assert(loadedBin.Vertices.size() == 4);
        assert(loadedBin.Indices.size() == 6);

        // LoadMesh pure CPU without GPU context
        auto loaded = Axle::AssetSerializer::LoadMesh(meshPath);
        assert(loaded.Success);
        assert(loaded.SourceData.Vertices.size() == 4);
        assert(loaded.SourceData.Indices.size() == 6);

        CleanupTempDir(root);
        std::cout << "[PASS] TestAssetSerializerCPUBoundary" << std::endl;
    }

    void TestDecoupledPhysicsMaterial() {
        std::cout << "[RUN] TestDecoupledPhysicsMaterial..." << std::endl;
        auto root = CreateTempDir("PhysMat");

        // Declarative .axleasset format
        Axle::PhysicsMaterial mat;
        mat.Name = "Rubber";
        mat.Friction = 0.95f;
        mat.Restitution = 0.85f;
        mat.Density = 1100.0f;

        std::string serialized = Axle::PhysicsMaterial::Serialize(mat);
        assert(serialized.find("PhysicsMaterial {") != std::string::npos);

        Axle::PhysicsMaterial deserialized;
        bool desOk = Axle::PhysicsMaterial::Deserialize(serialized, deserialized);
        assert(desOk);
        assert(deserialized.Name == "Rubber");
        assert(std::abs(deserialized.Friction - 0.95f) < 1e-4f);
        assert(std::abs(deserialized.Restitution - 0.85f) < 1e-4f);
        assert(std::abs(deserialized.Density - 1100.0f) < 1e-4f);

        // File Save/Load roundtrip
        auto path = root / "Rubber.axleasset";
        bool saveOk = Axle::PhysicsMaterial::Save(path, mat);
        assert(saveOk);

        Axle::PhysicsMaterial loaded;
        bool loadOk = Axle::PhysicsMaterial::Load(path, loaded);
        assert(loadOk);
        assert(std::abs(loaded.Friction - 0.95f) < 1e-4f);

        // Legacy format parsing
        std::string legacySource =
            "# Legacy PhysMat\n"
            "PhysicsMaterial:\n"
            "  Friction: 0.42\n"
            "  Restitution: 0.15\n"
            "  Density: 750.0\n";
        Axle::PhysicsMaterial legacyMat;
        bool legOk = Axle::PhysicsMaterial::Deserialize(legacySource, legacyMat);
        assert(legOk);
        assert(std::abs(legacyMat.Friction - 0.42f) < 1e-4f);
        assert(std::abs(legacyMat.Restitution - 0.15f) < 1e-4f);
        assert(std::abs(legacyMat.Density - 750.0f) < 1e-4f);

        CleanupTempDir(root);
        std::cout << "[PASS] TestDecoupledPhysicsMaterial" << std::endl;
    }

    void TestMaterialCacheDeduplicationAndReload() {
        std::cout << "[RUN] TestMaterialCacheDeduplicationAndReload..." << std::endl;
        auto root = CreateTempDir("MatCache");
        auto matPath = root / "TestMat.axleasset";

        uint64_t targetUUID = 0xABCD0001ULL;
        std::string matContent =
            "# AxleFileIdentity: 1 " + std::to_string(targetUUID) + "\n"
            "Material {\n"
            "    name: \"TestMat\"\n"
            "    roughness: 0.75\n"
            "    metallic: 0.20\n"
            "}\n";
        Axle::FileSystem::WriteTextFile(matPath, matContent);

        // Initial fetch
        auto mat1 = Axle::MaterialCache::GetMaterial(matPath);
        assert(mat1 != nullptr);
        assert(mat1->GetHandle() == Axle::UUID(targetUUID));
        assert(std::abs(mat1->GetRoughness() - 0.75f) < 1e-3f);

        // Deduplication test
        auto mat2 = Axle::MaterialCache::GetMaterial(matPath);
        assert(mat1.get() == mat2.get());

        // By UUID lookup
        auto matUUID = Axle::MaterialCache::GetMaterial(Axle::UUID(targetUUID));
        assert(matUUID.get() == mat1.get());

        // In-place live reload
        std::string modifiedContent =
            "# AxleFileIdentity: 1 " + std::to_string(targetUUID) + "\n"
            "Material {\n"
            "    name: \"TestMat\"\n"
            "    roughness: 0.10\n"
            "    metallic: 0.90\n"
            "}\n";
        Axle::FileSystem::WriteTextFile(matPath, modifiedContent);

        Axle::MaterialCache::ReloadMaterial(matPath);

        // Pointer must remain identical, but properties are updated
        assert(mat1.get() == mat2.get());
        assert(std::abs(mat1->GetRoughness() - 0.10f) < 1e-3f);
        assert(std::abs(mat1->GetMetallic() - 0.90f) < 1e-3f);

        Axle::MaterialCache::ClearAll();
        CleanupTempDir(root);
        std::cout << "[PASS] TestMaterialCacheDeduplicationAndReload" << std::endl;
    }

}

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "  Axle Asset Registry & Format Unit Tests " << std::endl;
    std::cout << "=========================================" << std::endl;

    TestHeadlessAssetManagerRegistry();
    TestAssetDocumentAndDSLCodec();
    TestAssetSerializerCPUBoundary();
    TestDecoupledPhysicsMaterial();
    TestMaterialCacheDeduplicationAndReload();

    std::cout << "=========================================" << std::endl;
    std::cout << "  ALL ASSET UNIT TESTS PASSED (100%)     " << std::endl;
    std::cout << "=========================================" << std::endl;
    return 0;
}
