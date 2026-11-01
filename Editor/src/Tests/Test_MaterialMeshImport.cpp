#include <Axle/AssetSerializer.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <fstream>
#include <iostream>
#include <cstring>

bool VerifyMaterialMeshImport(const std::filesystem::path& root) {
    using namespace Axle;
    auto check=[](bool passed,const char* message) {
        if (!passed) std::cerr << "[106 Mesh] FAIL: " << message << std::endl;
        return passed;
    };
    auto obj=root/"uv_seams.obj";
    {
        std::ofstream file(obj);
        file << "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\n"
             << "vt 0 0\nvt 1 0\nvt 1 1\nvt 1 0\nvt 0 1\nvt 1 1\n"
             << "vn 0 0 1\nf 1/1/1 2/2/1 3/3/1\nf -4/4/1 -2/5/1 -1/6/1\n";
    }
    auto converted=root/"uv_seams.axleasset";
    if (!check(AssetSerializer::ConvertOBJToAxle(obj,converted,false),"OBJ import")) return false;
    AxleMeshSource text,binary;
    if (!check(AssetSerializer::LoadFromAxleText(converted,text) && text.Vertices.size()==6 && text.UVs.size()==6 && text.Normals.size()==6,"OBJ corners retain UV seams and authored normals")) return false;
    if (!check(text.UVs[0]==glm::vec2(0,0) && text.UVs[3]==glm::vec2(1,0) && text.Normals[0]==glm::vec3(0,0,1),"OBJ negative indices preserve corner attributes")) return false;
    auto cache=AssetSerializer::GetCachePathFor(converted);
    if (!check(AssetSerializer::LoadFromAxleBinary(cache,binary) && binary.UVs==text.UVs && binary.Normals==text.Normals,"Mesh v2 cache round trip")) return false;
    auto legacyText=root/"uv_seams.axle";
    AxleMeshSource legacy;
    if (!check(AssetSerializer::SaveToAxleText(legacyText,text) && AssetSerializer::LoadFromAxleText(legacyText,legacy) && legacy.UVs==text.UVs && legacy.Normals==text.Normals,"Legacy text extension round trip")) return false;
    // A genuine version-1 payload has no attribute counts or trailing arrays.
    std::ifstream input(cache,std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
    bytes.resize(bytes.size()-8-text.Normals.size()*sizeof(glm::vec3)-text.UVs.size()*sizeof(glm::vec2));
    uint32_t version=1;
    std::memcpy(bytes.data()+8,&version,sizeof(version));
    auto oldCache=root/"legacy_v1.axlebin";
    { std::ofstream file(oldCache,std::ios::binary); file.write(bytes.data(),bytes.size()); }
    if (!check(AssetSerializer::LoadFromAxleBinary(oldCache,binary) && binary.UVs.empty() && binary.Normals.empty() && binary.Vertices==text.Vertices,"Version-1 caches remain readable and clear stale attributes")) return false;
    auto truncated=root/"truncated.axlebin";
    { std::ofstream file(truncated,std::ios::binary); file.write(bytes.data(),bytes.size()-1); }
    if (!check(!AssetSerializer::LoadFromAxleBinary(truncated,binary),"Truncated mesh cache is rejected")) return false;
    auto imported=RHIMeshRegistry::Get().GetOrCreateMesh(converted.string());
    if (!check(imported && imported->GetVertexCount()==6,"RHI import retains split corners")) return false;

    // Separate glTF accessors exercise transformed normals and mirrored winding.
    auto buffer=root/"attributes.bin";
    std::vector<glm::vec3> positions={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
    std::vector<glm::vec3> normals(4,glm::vec3(0,0.6f,0.8f));
    std::vector<glm::vec2> uvs={{0,0},{1,0},{1,1},{0,1}};
    uint16_t indices[]={0,1,2,0,2,3};
    {
        std::ofstream file(buffer,std::ios::binary);
        file.write(reinterpret_cast<const char*>(positions.data()),48);
        file.write(reinterpret_cast<const char*>(normals.data()),48);
        file.write(reinterpret_cast<const char*>(uvs.data()),32);
        file.write(reinterpret_cast<const char*>(indices),12);
    }
    auto gltf=root/"attributes.gltf";
    {
        std::ofstream file(gltf);
        file << R"({"asset":{"version":"2.0"},"buffers":[{"uri":"attributes.bin","byteLength":140}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":48},{"buffer":0,"byteOffset":96,"byteLength":32},{"buffer":0,"byteOffset":128,"byteLength":12}],
"accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[-1,-1,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":4,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":4,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":6,"type":"SCALAR"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}],"nodes":[{"mesh":0,"scale":[-2,1,0.5]}],"scenes":[{"nodes":[0]}],"scene":0})";
    }
    auto gltfAsset=root/"gltf_attributes.axleasset";
    AxleMeshSource gltfSource;
    if (!check(AssetSerializer::ConvertGLTFToAxle(gltf,gltfAsset,false) && AssetSerializer::LoadFromAxleText(gltfAsset,gltfSource),"glTF attribute import")) return false;
    auto expected=glm::normalize(glm::vec3(0,0.6f,1.6f));
    if (!check(gltfSource.UVs==uvs && gltfSource.Normals.size()==4 && glm::length(gltfSource.Normals[0]-expected)<1e-5f && gltfSource.Indices[1]==2,"glTF retains UVs and inverse-transpose normals through mirrored node transforms")) return false;
    auto fbx=root/"attributes.fbx";
    {
        std::ofstream file(fbx);
        file << R"(; FBX 7.4.0 project file
FBXHeaderExtension: {
 FBXHeaderVersion: 1003
 FBXVersion: 7400
}
GlobalSettings: {
 Version: 1000
 Properties70: {
  P: "UpAxis", "int", "Integer", "", 1
  P: "UpAxisSign", "int", "Integer", "", 1
  P: "FrontAxis", "int", "Integer", "", 2
  P: "FrontAxisSign", "int", "Integer", "", -1
  P: "CoordAxis", "int", "Integer", "", 0
  P: "CoordAxisSign", "int", "Integer", "", 1
  P: "UnitScaleFactor", "double", "Number", "", 100
 }
}
Objects: {
 Geometry: 1, "Geometry::Quad", "Mesh" {
  Vertices: *12 { a: -1,-1,0,1,-1,0,1,1,0,-1,1,0 }
  PolygonVertexIndex: *6 { a: 0,1,-3,0,2,-4 }
  LayerElementNormal: 0 {
   Version: 101
   Name: ""
   MappingInformationType: "ByPolygonVertex"
   ReferenceInformationType: "Direct"
   Normals: *18 { a: 0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1 }
  }
  LayerElementUV: 0 {
   Version: 101
   Name: "UVMap"
   MappingInformationType: "ByPolygonVertex"
   ReferenceInformationType: "Direct"
   UV: *12 { a: 0,0,1,0,1,1,1,0,0,1,1,1 }
  }
  Layer: 0 {
   Version: 100
   LayerElement: { Type: "LayerElementNormal"
    TypedIndex: 0
   }
   LayerElement: { Type: "LayerElementUV"
    TypedIndex: 0
   }
  }
 }
 Model: 2, "Model::Quad", "Mesh" {
  Version: 232
 }
}
Connections: {
 C: "OO",1,2
 C: "OO",2,0
}
)";
    }
    auto fbxAsset=root/"fbx_attributes.axleasset";
    AxleMeshSource fbxSource;
    if (!check(AssetSerializer::ConvertFBXToAxle(fbx,fbxAsset,false) && AssetSerializer::LoadFromAxleText(fbxAsset,fbxSource),"FBX attribute import")) return false;
    if (!check(fbxSource.Vertices.size()==6 && fbxSource.UVs==text.UVs && fbxSource.Normals==text.Normals,"FBX polygon corners retain normals and UV seams")) return false;
    std::cout << "[106 Mesh] PASS: OBJ/FBX seams, glTF attributes, text and v1/v2 caches." << std::endl;
    return true;
}
