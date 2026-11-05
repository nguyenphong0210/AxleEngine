#include "TestWorkspace.hpp"
#include <Axle/AssetManager.hpp>
#include <Axle/AssetMetadataSerializer.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
#include <Axle/UI/UIParser.hpp>
#include <Axle/UI/UIDocument.hpp>
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/Material.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <functional>
#include <string_view>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " #value "\n"; return false; } } while(0)

namespace {
    using namespace Axle;
    constexpr uint64_t MaterialID = 100000000000000002ULL;
    std::filesystem::path Workspace() {
        static unsigned counter=0;
        auto path=std::filesystem::current_path()/"asset-identity-review"/
            (std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"_"+std::to_string(++counter));
        std::filesystem::create_directories(path);
        return path;
    }
    std::filesystem::path Fixture() {
        auto path=Workspace();
        std::filesystem::copy(std::filesystem::path(AXLE_TEST_SOURCE_DIR)/"Tests/Fixtures/AssetIdentity/AuthoredProject",path,
                              std::filesystem::copy_options::recursive);
        AssetManager::Init(path/"assets");
        FileSystem::SetAssetDirectory(path/"assets");
        return path/"assets";
    }
    bool NoAuxiliary(const std::filesystem::path& root) {
        for (const auto& entry:std::filesystem::recursive_directory_iterator(root)) {
            auto ext=entry.path().extension().string();
            if(ext==".meta" || ext==".uid" || ext==".import" || ext==".axleregistry") return false;
        }
        return true;
    }
    bool RawPathsAndColdNativeIdentity() {
        auto root=Fixture();
        auto before=FileSystem::ReadBinaryFile(root/"textures/test_texture.png");
        CHECK(AssetManager::GetAssetUUID("textures/test_texture.png")==0);
        auto raw=AssetManager::GetOrCreateMetadata(root/"textures/test_texture.png");
        CHECK(raw.Type==AssetType::Texture && raw.Handle==0 && raw.FilePath=="textures/test_texture.png");
        CHECK(AssetManager::GetAssetUUID("materials/test_material.axleasset")==MaterialID);
        CHECK(FileSystem::ReadBinaryFile(root/"textures/test_texture.png")==before);
        AssetManager::Shutdown(); AssetManager::Init(root);
        CHECK(AssetManager::GetAssetUUID("materials/test_material.axleasset")==MaterialID);
        CHECK(NoAuxiliary(root));
        return true;
    }
    bool RootRelocationAndNativeExternalMove() {
        auto root=Fixture(), moved=Workspace()/"assets";
        std::filesystem::copy(root,moved,std::filesystem::copy_options::recursive);
        AssetManager::Shutdown(); AssetManager::Init(moved);
        CHECK(AssetManager::GetAssetPath(MaterialID)==moved/"materials/test_material.axleasset");
        std::filesystem::rename(moved/"materials/test_material.axleasset",moved/"materials/moved.axleasset");
        CHECK(AssetManager::GetAssetPath(MaterialID)==moved/"materials/moved.axleasset");
        AssetManager::Shutdown(); AssetManager::Init(moved);
        CHECK(AssetManager::GetAssetUUID("materials/moved.axleasset")==MaterialID);
        Scene scene; SceneSerializer reader(&scene);
        CHECK(reader.Deserialize(moved/"scenes/test_scene.axlescene"));
        size_t count=0;
        for(auto entity:scene.GetRegistry().view<MaterialComponent>()) {
            auto& material=scene.GetRegistry().get<MaterialComponent>(entity);
            CHECK(material.MaterialHandle==MaterialID);
            CHECK(material.MaterialAsset && material.MaterialAsset->GetHandle()==MaterialID);
            CHECK(material.AssetPath=="materials/moved.axleasset");
            ++count;
        }
        CHECK(count==1 && NoAuxiliary(moved));
        return true;
    }
    bool SavePreservesIdentityAcrossOwners() {
        auto root=Fixture(); const auto path=root/"materials/test_material.axleasset";
        Material material; CHECK(material.Load(path)); CHECK(material.GetHandle()==MaterialID);
        material.SetRoughness(0.73f); CHECK(material.Save(path));
        uint64_t id=0; CHECK(FileSystem::ReadFileIdentity(path,id) && id==MaterialID);
        AssetDocument document; CHECK(AssetDSL::ParseFile(path,document));
        document.Root->SetFloat("roughness",0.31f); CHECK(AssetDSL::SaveFile(path,document));
        CHECK(FileSystem::ReadFileIdentity(path,id) && id==MaterialID);
        RHI::RHIMaterial rhi; CHECK(rhi.Load(path)); CHECK(rhi.Save(path));
        CHECK(FileSystem::ReadFileIdentity(path,id) && id==MaterialID);
        TextureDescriptor descriptor; descriptor.Source="textures/test_texture.png";
        auto descPath=root/"texture.axleasset"; CHECK(TextureDescriptor::Save(descPath.string(),descriptor));
        CHECK(FileSystem::ReadFileIdentity(descPath,id)); const auto firstID=id;
        descriptor.Filter="Nearest"; CHECK(TextureDescriptor::Save(descPath.string(),descriptor));
        CHECK(FileSystem::ReadFileIdentity(descPath,id) && id==firstID);
        Material paths("Paths"); paths.SetAlbedoTexture(nullptr,(root/"textures/test_texture.png").string());
        const auto materialPath=root/"paths.axleasset"; CHECK(paths.Save(materialPath));
        AssetDocument encoded; CHECK(AssetDSL::ParseFile(materialPath,encoded));
        CHECK(encoded.Root->GetString("albedoMap")=="textures/test_texture.png");
        const auto before=FileSystem::ReadTextFile(descPath);
        descriptor.Source="../escape.png"; CHECK(!TextureDescriptor::Save(descPath.string(),descriptor));
        CHECK(FileSystem::ReadTextFile(descPath)==before);
        CHECK(NoAuxiliary(root)); return true;
    }
    bool RawMoveAndExplicitRelinkUpdateRealReferences() {
        auto root=Fixture(); TextureDescriptor descriptor; descriptor.Source="textures/test_texture.png";
        auto path=root/"texture.axleasset"; CHECK(TextureDescriptor::Save(path.string(),descriptor));
        std::string error;
        CHECK(AssetManager::RelocateAsset("textures/test_texture.png","textures/nested/renamed.png",&error));
        CHECK(std::filesystem::is_regular_file(root/"textures/nested/renamed.png"));
        CHECK(!std::filesystem::exists(root/"textures/test_texture.png"));
        CHECK(TextureDescriptor::Load(path.string(),descriptor) && descriptor.Source=="textures/nested/renamed.png");
        CHECK(AssetManager::GetAssetUUID("textures/nested/renamed.png")==0);
        std::filesystem::rename(root/"textures/nested/renamed.png",root/"textures/external.png");
        CHECK(!std::filesystem::exists(root/descriptor.Source));
        CHECK(AssetManager::RelinkPathReferences("textures/nested/renamed.png","textures/external.png",&error));
        CHECK(TextureDescriptor::Load(path.string(),descriptor) && descriptor.Source=="textures/external.png");
        CHECK(NoAuxiliary(root)); return true;
    }
    bool CopyAndDuplicateIdentity() {
        auto root=Fixture(); std::string error;
        CHECK(AssetManager::CopyAsset("materials/test_material.axleasset","materials/copy.axleasset",&error));
        const auto copyID=AssetManager::GetAssetUUID("materials/copy.axleasset");
        CHECK(copyID!=0 && copyID!=MaterialID);
        CHECK(AssetManager::CopyAsset("textures/test_texture.png","textures/copy.png",&error));
        CHECK(FileSystem::ReadBinaryFile(root/"textures/test_texture.png")==FileSystem::ReadBinaryFile(root/"textures/copy.png"));
        CHECK(AssetManager::GetAssetUUID("textures/copy.png")==0);
        std::filesystem::copy_file(root/"materials/test_material.axleasset",root/"materials/external-copy.axleasset");
        AssetManager::Sync();
        CHECK(!AssetManager::HasAsset(MaterialID) && AssetManager::HasDiagnosticErrors());
        CHECK(!AssetManager::GetMaterial("materials/test_material.axleasset"));
        CHECK(AssetManager::HasAsset(copyID));
        CHECK(NoAuxiliary(root)); return true;
    }
    bool InvalidIdentityIsNotRegenerated() {
        auto root=Fixture(); const auto path=root/"invalid.axleasset";
        const std::string invalid="# AxleFileIdentity: 99 4321\nMaterial { name: \"invalid\" }\n";
        CHECK(FileSystem::WriteTextFileAtomically(path,invalid));
        CHECK(!AssetManager::GetOrCreateMetadata(path));
        CHECK(FileSystem::ReadTextFile(path)==invalid);
        CHECK(!FileSystem::WriteIdentifiedTextFile(path,"Material { name: \"replacement\" }\n"));
        CHECK(FileSystem::ReadTextFile(path)==invalid);
        const auto missing=root/"missing-id.axleasset";
        const std::string body="Material { name: \"new\" }\n";
        CHECK(FileSystem::WriteTextFileAtomically(missing,body));
        AssetManager::Sync();
        CHECK(FileSystem::ReadTextFile(missing)==body); // Discovery never mutates assets.
        AssetManager::TrackReferencedAsset(Axle::UUID(7889),"missing-id.axleasset");
        CHECK(!AssetManager::GetOrCreateMetadata(missing));
        CHECK(FileSystem::ReadTextFile(missing)==body && NoAuxiliary(root));
        return true;
    }
    bool NewNativeImportPublishesAfterPersisting() {
        auto root=Fixture(), path=root/"new.axleasset";
        CHECK(FileSystem::WriteTextFileAtomically(path,"Material { name: \"new\" }\n"));
        const auto metadata=AssetManager::GetOrCreateMetadata(path);
        CHECK(metadata.Handle!=0);
        uint64_t stored=0; CHECK(FileSystem::ReadFileIdentity(path,stored) && stored==uint64_t(metadata.Handle));
        AssetManager::Shutdown(); AssetManager::Init(root);
        CHECK(AssetManager::GetAssetUUID("new.axleasset")==stored);
        CHECK(NoAuxiliary(root)); return true;
    }
    bool PackagedNativeIdentityAndRawPaths() {
        auto root=Fixture(), archive=Workspace()/"Data.axlepak";
        PackageWriter writer;
        CHECK(writer.Begin(archive));
        CHECK(writer.AddFile("materials/test_material.axleasset",root/"materials/test_material.axleasset",Axle::UUID(MaterialID)));
        CHECK(writer.AddFile("textures/test_texture.png",root/"textures/test_texture.png"));
        CHECK(writer.Finish());
        AssetManager::Shutdown();
        // Mount against a root that has no loose files: no disk discovery can satisfy this test.
        const auto packedRoot=Workspace()/"missing-assets";
        FileSystem::SetAssetDirectory(packedRoot);
        CHECK(FileSystem::MountPackage(archive));
        AssetManager::Init(packedRoot);
        const bool valid = AssetManager::GetAssetUUID("materials/test_material.axleasset")==MaterialID &&
            AssetManager::GetAssetUUID("textures/test_texture.png")==0 &&
            !AssetManager::GetAssetPath(Axle::UUID(MaterialID)).empty() &&
            bool(AssetManager::GetMaterial(Axle::UUID(MaterialID))) &&
            FileSystem::ReadBinaryFile(packedRoot/"textures/test_texture.png")==FileSystem::ReadBinaryFile(root/"textures/test_texture.png");
        FileSystem::UnmountPackage();
        CHECK(valid); return true;
    }
    bool MaterialReloadPreservesLiveObject() {
        auto root=Fixture(); auto path=root/"reload.axleasset";
        Material original; original.SetRoughness(0.8f); CHECK(original.Save(path));
        AssetManager::Sync();
        const auto id=AssetManager::GetAssetUUID(path);
        auto live=AssetManager::GetMaterial(id); CHECK(live);
        Material changed; CHECK(changed.Load(path)); changed.SetRoughness(0.2f); CHECK(changed.Save(path));
        AssetManager::ReloadMaterial(id);
        CHECK(AssetManager::GetMaterial(id)==live && std::abs(live->GetRoughness()-0.2f)<0.001f);
        CHECK(live->GetHandle()==id);
        const uint64_t replacementID=uint64_t(id)^0x4000000000000000ULL;
        CHECK(FileSystem::WriteTextFileAtomically(path,"# AxleFileIdentity: 1 "+std::to_string(replacementID)+"\nMaterial { name: \"Different asset\" roughness: 0.9 }\n"));
        auto replacement=AssetManager::GetMaterial(path);
        CHECK(replacement && replacement!=live && replacement->GetHandle()==replacementID);
        CHECK(!AssetManager::GetMaterial(id));
        CHECK(NoAuxiliary(root)); return true;
    }
    bool NativeUIRoundtrip() {
        auto root=Fixture(); UIDocument document;
        CHECK(UIParser::ParseSource("Canvas { name: \"Identity\" }",document));
        auto path=root/"identity.axleui";
        CHECK(UIParser::SaveFile(path.string(),document));
        uint64_t before=0,after=0;
        CHECK(FileSystem::ReadFileIdentity(path,before));
        UIDocument restored; CHECK(UIParser::ParseFile(path.string(),restored));
        CHECK(UIParser::SaveFile(path.string(),restored));
        CHECK(FileSystem::ReadFileIdentity(path,after) && after==before);
        CHECK(NoAuxiliary(root)); return true;
    }
    bool PathAndBuiltinValidation() {
        auto root=Fixture(), outside=Workspace()/"outside.png";
        CHECK(FileSystem::WriteTextFileAtomically(outside,"outside"));
        CHECK(AssetManager::GetAssetType(outside)==AssetType::Texture);
        CHECK(!AssetManager::GetOrCreateMetadata(outside));
        CHECK(AssetManager::GetReferencePath("../outside.png").empty());
        CHECK(!AssetManager::IsBuiltin("builtin://misspelled"));
        CHECK(AssetManager::GetAssetUUID("builtin://misspelled")==0);
        CHECK(AssetManager::GetAssetUUID("builtin://cube")==BuiltinAssets::CubeUUID);
        CHECK(!AssetManager::RelocateAsset("materials/test_material.axleasset","materials/test_material.axleasset/collision"));
        CHECK(!AssetManager::RelocateAsset("missing.png","textures/missing.png"));
        CHECK(!AssetManager::RelinkAsset(Axle::UUID(MaterialID+1),"materials/test_material.axleasset"));
        CHECK(NoAuxiliary(root)); return true;
    }
    bool AtomicInvalidInputAndReplacementFailure() {
        auto root=Fixture(), path=root/"keep.txt";
        CHECK(FileSystem::WriteTextFileAtomically(path,"previous"));
        std::string error;
        CHECK(!FileSystem::WriteBinaryFileAtomically(path,nullptr,5,&error));
        CHECK(!error.empty() && FileSystem::ReadTextFile(path)=="previous");
        const auto directory=root/"directory-target"; std::filesystem::create_directory(directory);
        CHECK(!FileSystem::WriteTextFileAtomically(directory,"cannot replace directory",&error));
        CHECK(std::filesystem::is_directory(directory));
#ifdef _WIN32
        HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        CHECK(lock!=INVALID_HANDLE_VALUE);
        bool saved=FileSystem::WriteTextFileAtomically(path,"replacement",&error);
        CloseHandle(lock);
        CHECK(!saved && FileSystem::ReadTextFile(path)=="previous");
#endif
        return true;
    }
    bool FailedNativeImportDoesNotPublish() {
        auto root=Fixture(), path=root/"locked.axleasset";
        const std::string body="Material { name: \"locked\" }\n";
        CHECK(FileSystem::WriteTextFileAtomically(path,body));
#ifdef _WIN32
        HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        CHECK(lock!=INVALID_HANDLE_VALUE);
        const auto result=AssetManager::GetOrCreateMetadata(path);
        CloseHandle(lock);
        CHECK(!result && !AssetManager::HasAsset(path));
        CHECK(AssetManager::GetAssetUUID(path)==0 && FileSystem::ReadTextFile(path)==body);
#else
        // The deterministic cross-platform replacement failure is exercised above.
        std::filesystem::create_directory(root/"directory-target");
        CHECK(!FileSystem::WriteIdentifiedTextFile(root/"directory-target","payload"));
#endif
        CHECK(NoAuxiliary(root)); return true;
    }
    bool SceneAssemblyAndProjectSavesKeepPreviousBytes() {
        auto root=Fixture(); auto scene=std::make_shared<Scene>(); auto entity=scene->CreateEntity("Ordinary");
        SceneSerializer serializer(scene.get());
        auto scenePath=root/"save.axlescene", assemblyPath=root/"save.axle", projectPath=root/"test.axleproj";
        CHECK(serializer.Serialize(scenePath));
        CHECK(AssemblySerializer::Serialize(assemblyPath,entity));
        auto project=std::make_shared<Project>(); ProjectSerializer writer(project); CHECK(writer.Serialize(projectPath));
        uint64_t sceneID=0; CHECK(FileSystem::ReadFileIdentity(scenePath,sceneID));
        CHECK(serializer.Serialize(scenePath)); uint64_t next=0; CHECK(FileSystem::ReadFileIdentity(scenePath,next) && next==sceneID);
#ifdef _WIN32
        for (const auto& path:{scenePath,assemblyPath,projectPath}) {
            const auto before=FileSystem::ReadBinaryFile(path);
            HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            CHECK(lock!=INVALID_HANDLE_VALUE);
            bool saved=path==scenePath ? serializer.Serialize(path) :
                       path==assemblyPath ? AssemblySerializer::Serialize(path,entity) : writer.Serialize(path);
            CloseHandle(lock);
            CHECK(!saved && FileSystem::ReadBinaryFile(path)==before);
        }
#endif
        CHECK(NoAuxiliary(root)); return true;
    }
}

int main(int argc,char** argv) {
    if(argc==2 && std::string_view(argv[1])=="--self-test-failure") {
        std::cout<<"AXLE_TEST_EXPECTED_FAILURE asset-probe\n"; return 1;
    }
    Axle::Tests::InitializeWorkspace();
    const std::pair<const char*,std::function<bool()>> tests[] = {
        {"raw paths and cold native UUID",RawPathsAndColdNativeIdentity},
        {"root relocation and external native move",RootRelocationAndNativeExternalMove},
        {"serializer identity preservation",SavePreservesIdentityAcrossOwners},
        {"raw move and explicit relink",RawMoveAndExplicitRelinkUpdateRealReferences},
        {"copy and duplicate identity",CopyAndDuplicateIdentity},
        {"invalid identity and read-only discovery",InvalidIdentityIsNotRegenerated},
        {"new native import",NewNativeImportPublishesAfterPersisting},
        {"packaged identity and raw paths",PackagedNativeIdentityAndRawPaths},
        {"in-place material reload",MaterialReloadPreservesLiveObject},
        {"native UI roundtrip",NativeUIRoundtrip},
        {"path and builtin validation",PathAndBuiltinValidation},
        {"invalid atomic input and failure",AtomicInvalidInputAndReplacementFailure},
        {"failed identity publication",FailedNativeImportDoesNotPublish},
        {"scene assembly project persistence",SceneAssemblyAndProjectSavesKeepPreviousBytes}
    };
    size_t passed=0;
    try {
        for(const auto& [name,test]:tests) {
            std::cout<<"RUN "<<name<<std::endl;
            if(!test()) {AssetManager::Shutdown(); return 1;}
            AssetManager::Shutdown(); ++passed;
        }
    } catch(const std::exception& error) {std::cerr<<error.what()<<"\n";AssetManager::Shutdown();return 1;}
    std::cout<<"PASS "<<passed<<"/"<<std::size(tests)<<" asset suites\n";
    return 0;
}
