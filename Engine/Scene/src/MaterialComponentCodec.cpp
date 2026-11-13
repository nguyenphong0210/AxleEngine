#include <Axle/MaterialComponentCodec.hpp>
#include <Axle/Components.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/ShaderPropertyPersistence.hpp>
#include <sstream>
#include <cstdio>
namespace Axle {
    static std::string Trim(const std::string& value) {
        const auto first=value.find_first_not_of(" \t\r\n");
        if(first==std::string::npos)return {};
        return value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
    }
    void WriteMaterialComponent(std::ostream& stream,const MaterialComponent& mat) {
        std::ostringstream ss;

        ss << "    MaterialComponent:\n";
        if (mat.MaterialHandle!=0)ss<<"      MaterialHandle: "<<static_cast<uint64_t>(mat.MaterialHandle)<<"\n";
        const auto path=!mat.AssetPath.empty()?mat.AssetPath:mat.MaterialAsset?mat.MaterialAsset->GetFilePath().string():std::string{};
        if(!path.empty())ss<<"      AssetPath: "<<path<<"\n";

        ss << "      AlbedoPath: " << mat.AlbedoPath << "\n";
        ss << "      UseAlbedoMap: " << (mat.UseAlbedoMap ? "true" : "false") << "\n";
        ss << "      AlbedoColor: [ " << mat.AlbedoColor.r << " , " << mat.AlbedoColor.g << " , " << mat.AlbedoColor.b << " , " << mat.AlbedoColor.a << " ]\n";
        ss << "      AOPath: " << mat.AOPath << "\n";
        ss << "      EmissivePath: " << mat.EmissivePath << "\n";
        ss << "      ORMPath: " << mat.ORMPath << "\n";
        ss << "      CustomShaderPath: " << mat.CustomShaderPath << "\n";
        ss << "      TextureMode: " << TextureModeToString(mat.TextureMode) << "\n";
        ss << "      UseAOMap: " << (mat.UseAOMap ? "true" : "false") << "\n";
        ss << "      UseEmissiveMap: " << (mat.UseEmissiveMap ? "true" : "false") << "\n";
        ss << "      UseORMMap: " << (mat.UseORMMap ? "true" : "false") << "\n";
        ss << "      EnableSpecularHighlights: " << (mat.EnableSpecularHighlights ? "true" : "false") << "\n";
        ss << "      EnableEnvironmentReflections: " << (mat.EnableEnvironmentReflections ? "true" : "false") << "\n";
        ss << "      UseCustomShader: " << (mat.UseCustomShader ? "true" : "false") << "\n";
        ss << "      AOStrength: " << mat.AOStrength << "\n";
        ss << "      DetailNormalStrength: " << mat.DetailNormalStrength << "\n";
        ss << "      DetailUVScale: " << "[ " << mat.DetailUVScale.x << " , " << mat.DetailUVScale.y << " ]\n";
        ss << "      DetailUVOffset: " << "[ " << mat.DetailUVOffset.x << " , " << mat.DetailUVOffset.y << " ]\n";
        ss << "      RefractionEnabled: " << (mat.RefractionEnabled?"true":"false") << "\n";
        ss << "      RefractionStrength: " << mat.RefractionStrength << "\n";
        ss << "      RefractionIOR: " << mat.RefractionIOR << "\n";
        ss << "      RefractionThickness: " << mat.RefractionThickness << "\n";
        ss << "      RefractionTint: " << "[ " << mat.RefractionTint.x << " , " << mat.RefractionTint.y << " , " << mat.RefractionTint.z << " ]\n";
        ss << "      ClearcoatStrength: " << mat.ClearcoatStrength << "\n";
        ss << "      ClearcoatRoughness: " << mat.ClearcoatRoughness << "\n";
        ss << "      AnisotropyStrength: " << mat.AnisotropyStrength << "\n";
        ss << "      AnisotropyRotation: " << mat.AnisotropyRotation << "\n";
        ss << "      SheenEnabled: " << (mat.SheenEnabled ? "true" : "false") << "\n";
        ss << "      SheenColor: [ " << mat.SheenColor.r << " , " << mat.SheenColor.g << " , " << mat.SheenColor.b << " ]\n";
        ss << "      SheenRoughness: " << mat.SheenRoughness << "\n";
        ss << "      SubsurfaceEnabled: " << (mat.SubsurfaceEnabled ? "true" : "false") << "\n";
        ss << "      SubsurfaceColor: [ " << mat.SubsurfaceColor.r << " , " << mat.SubsurfaceColor.g << " , " << mat.SubsurfaceColor.b << " ]\n";
        ss << "      SubsurfaceRadius: " << mat.SubsurfaceRadius << "\n";
        ss << "      AbsorptionEnabled: " << (mat.AbsorptionEnabled ? "true" : "false") << "\n";
        ss << "      AbsorptionColor: [ " << mat.AbsorptionColor.r << " , " << mat.AbsorptionColor.g << " , " << mat.AbsorptionColor.b << " ]\n";
        ss << "      Dispersion: " << mat.Dispersion << "\n";
        ss << "      RenderQueue: " << mat.RenderQueue << "\n";
        ss << "      ClearcoatPath: " << mat.ClearcoatPath << "\n";
        ss << "      UseClearcoatMap: " << (mat.UseClearcoatMap?"true":"false") << "\n";
        ss << "      AnisotropyFlowPath: " << mat.AnisotropyFlowPath << "\n";
        ss << "      UseAnisotropyFlowMap: " << (mat.UseAnisotropyFlowMap?"true":"false") << "\n";
        ss << "      DetailNormalPath: " << mat.DetailNormalPath << "\n";
        ss << "      UseDetailNormalMap: " << (mat.UseDetailNormalMap?"true":"false") << "\n";
        ss << "      DetailMaskPath: " << mat.DetailMaskPath << "\n";
        ss << "      UseDetailMaskMap: " << (mat.UseDetailMaskMap?"true":"false") << "\n";
        WriteShaderProperties(ss,mat.CustomProperties,"      ");
        ss << "      NormalPath: " << mat.NormalPath << "\n";
        ss << "      UseNormalMap: " << (mat.UseNormalMap ? "true" : "false") << "\n";
        ss << "      NormalStrength: " << mat.NormalStrength << "\n";
        ss << "      RoughnessPath: " << mat.RoughnessPath << "\n";
        ss << "      UseRoughnessMap: " << (mat.UseRoughnessMap ? "true" : "false") << "\n";
        ss << "      Roughness: " << mat.Roughness << "\n";
        ss << "      MetallicPath: " << mat.MetallicPath << "\n";
        ss << "      UseMetallicMap: " << (mat.UseMetallicMap ? "true" : "false") << "\n";
        ss << "      Metallic: " << mat.Metallic << "\n";
        ss << "      EmissionColor: [ " << mat.EmissionColor.r << " , " << mat.EmissionColor.g << " , " << mat.EmissionColor.b << " ]\n";
        ss << "      EmissionIntensity: " << mat.EmissionIntensity << "\n";
        ss << "      UVScale: [ " << mat.UVScale.x << " , " << mat.UVScale.y << " ]\n";
        ss << "      UVOffset: [ " << mat.UVOffset.x << " , " << mat.UVOffset.y << " ]\n";
        ss << "      SurfaceType: " << SurfaceTypeToString(mat.SurfaceType) << "\n";
        ss << "      CullMode: " << CullModeToString(mat.CullMode) << "\n";
        ss << "      AlphaCutoff: " << mat.AlphaCutoff << "\n";
        ss << "      DepthWrite: " << (mat.DepthWrite ? "true" : "false") << "\n";

        stream<<ss.str();
    }
    bool ReadMaterialComponentLine(MaterialComponent& mat,std::string_view line) {
        const auto trimmed=Trim(std::string(line));
        if (ReadShaderPropertyLine(trimmed,mat.CustomProperties)) {
            // Parsed a typed shader parameter.
        } else if (trimmed.rfind("MaterialHandle:", 0) == 0) {
            mat.MaterialHandle = std::stoull(Trim(trimmed.substr(15)));
            if (mat.MaterialHandle != 0) {
                AssetManager::TrackReferencedAsset(mat.MaterialHandle);
                mat.MaterialAsset = MaterialCache::GetMaterial(mat.MaterialHandle);
                if (!mat.MaterialAsset) {
                    auto resolvedPath = AssetManager::GetAssetPath(mat.MaterialHandle);
                    if (!resolvedPath.empty()) {
                        mat.MaterialAsset = MaterialCache::GetMaterial(resolvedPath);
                    }
                }
                if (mat.MaterialAsset) mat.SyncFromAsset();
            }
        } else if (trimmed.rfind("AssetPath:", 0) == 0) {
            std::string path = Trim(trimmed.substr(10));
            mat.AssetPath = path;
            if (mat.MaterialHandle != 0) {
                AssetManager::TrackReferencedAsset(mat.MaterialHandle, path);
                auto resolved = AssetManager::GetRelativeAssetPath(mat.MaterialHandle);
                if (!resolved.empty()) mat.AssetPath = resolved.generic_string();
                else AssetManager::ReportDiagnostic(AssetDiagnostic::Level::Error, "Material UUID is unresolved", path, mat.MaterialHandle);
            }
            if (!mat.MaterialAsset) {
                if (mat.MaterialHandle != 0) {
                    auto resolvedPath = AssetManager::GetAssetPath(mat.MaterialHandle);
                    if (!resolvedPath.empty()) {
                        mat.MaterialAsset = MaterialCache::GetMaterial(resolvedPath);
                    }
                }
                if (!mat.MaterialAsset && !path.empty()) {
                    mat.MaterialAsset = MaterialCache::GetMaterial(path);
                }
                if (mat.MaterialAsset) {
                    if (mat.MaterialHandle == 0) mat.MaterialHandle = mat.MaterialAsset->GetHandle();
                    mat.SyncFromAsset();
                }
            }
        } else if (trimmed.rfind("AlbedoPath:", 0) == 0) {
            std::string p = Trim(trimmed.substr(11));
            if (!p.empty()) mat.SetAlbedo(p);
        } else if (trimmed.rfind("UseAlbedoMap:", 0) == 0) {
            mat.UseAlbedoMap = (Trim(trimmed.substr(13)) == "true");
        } else if (trimmed.rfind("AlbedoColor:", 0) == 0) {
            float r = 1, g = 1, b = 1, a = 1;
            sscanf(trimmed.c_str(), "AlbedoColor: [ %f , %f , %f , %f ]", &r, &g, &b, &a);
            mat.AlbedoColor = glm::vec4(r, g, b, a);
        } else if (trimmed.rfind("AOPath:", 0) == 0) {
            mat.AOPath = Trim(trimmed.substr(7));
        } else if (trimmed.rfind("EmissivePath:", 0) == 0) {
            mat.EmissivePath = Trim(trimmed.substr(13));
        } else if (trimmed.rfind("ORMPath:", 0) == 0) {
            mat.ORMPath = Trim(trimmed.substr(8));
        } else if (trimmed.rfind("CustomShaderPath:", 0) == 0) {
            mat.CustomShaderPath = Trim(trimmed.substr(17));
        } else if (trimmed.rfind("TextureMode:", 0) == 0) {
            mat.TextureMode = StringToTextureMode(Trim(trimmed.substr(12)));
        } else if (trimmed.rfind("UseAOMap:", 0) == 0) {
            mat.UseAOMap = Trim(trimmed.substr(9)) == "true";
        } else if (trimmed.rfind("UseEmissiveMap:", 0) == 0) {
            mat.UseEmissiveMap = Trim(trimmed.substr(15)) == "true";
        } else if (trimmed.rfind("UseORMMap:", 0) == 0) {
            mat.UseORMMap = Trim(trimmed.substr(10)) == "true";
        } else if (trimmed.rfind("EnableSpecularHighlights:", 0) == 0) {
            mat.EnableSpecularHighlights = Trim(trimmed.substr(25)) == "true";
        } else if (trimmed.rfind("EnableEnvironmentReflections:", 0) == 0) {
            mat.EnableEnvironmentReflections = Trim(trimmed.substr(29)) == "true";
        } else if (trimmed.rfind("UseCustomShader:", 0) == 0) {
            mat.UseCustomShader = Trim(trimmed.substr(16)) == "true";
        } else if (trimmed.rfind("AOStrength:", 0) == 0) {
            mat.AOStrength = std::stof(Trim(trimmed.substr(11)));
        } else if (trimmed.rfind("ClearcoatPath:", 0) == 0) {
            mat.ClearcoatPath=Trim(trimmed.substr(14));
        } else if (trimmed.rfind("UseClearcoatMap:", 0) == 0) {
            mat.UseClearcoatMap=Trim(trimmed.substr(16))=="true";
        } else if (trimmed.rfind("AnisotropyFlowPath:", 0) == 0) {
            mat.AnisotropyFlowPath=Trim(trimmed.substr(19));
        } else if (trimmed.rfind("UseAnisotropyFlowMap:", 0) == 0) {
            mat.UseAnisotropyFlowMap=Trim(trimmed.substr(21))=="true";
        } else if (trimmed.rfind("DetailNormalPath:", 0) == 0) {
            mat.DetailNormalPath=Trim(trimmed.substr(17));
        } else if (trimmed.rfind("UseDetailNormalMap:", 0) == 0) {
            mat.UseDetailNormalMap=Trim(trimmed.substr(19))=="true";
        } else if (trimmed.rfind("DetailMaskPath:", 0) == 0) {
            mat.DetailMaskPath=Trim(trimmed.substr(15));
        } else if (trimmed.rfind("UseDetailMaskMap:", 0) == 0) {
            mat.UseDetailMaskMap=Trim(trimmed.substr(17))=="true";
        } else if (trimmed.rfind("DetailNormalStrength:", 0) == 0) {
            mat.DetailNormalStrength=std::stof(Trim(trimmed.substr(21)));
        } else if (trimmed.rfind("DetailUVScale:", 0) == 0) {
            std::sscanf(trimmed.c_str(), "DetailUVScale: [ %f , %f ]", &mat.DetailUVScale.x, &mat.DetailUVScale.y);
        } else if (trimmed.rfind("DetailUVOffset:", 0) == 0) {
            std::sscanf(trimmed.c_str(), "DetailUVOffset: [ %f , %f ]", &mat.DetailUVOffset.x, &mat.DetailUVOffset.y);
        } else if (trimmed.rfind("RefractionEnabled:", 0) == 0) {
            mat.RefractionEnabled=Trim(trimmed.substr(18))=="true";
        } else if (trimmed.rfind("RefractionStrength:", 0) == 0) {
            mat.RefractionStrength=std::stof(Trim(trimmed.substr(19)));
        } else if (trimmed.rfind("RefractionIOR:", 0) == 0) {
            mat.RefractionIOR=std::stof(Trim(trimmed.substr(14)));
        } else if (trimmed.rfind("RefractionThickness:", 0) == 0) {
            mat.RefractionThickness=std::stof(Trim(trimmed.substr(20)));
        } else if (trimmed.rfind("RefractionTint:", 0) == 0) {
            std::sscanf(trimmed.c_str(), "RefractionTint: [ %f , %f , %f ]", &mat.RefractionTint.x, &mat.RefractionTint.y, &mat.RefractionTint.z);
        } else if (trimmed.rfind("ClearcoatStrength:", 0) == 0) {
            mat.ClearcoatStrength = std::stof(Trim(trimmed.substr(18)));
        } else if (trimmed.rfind("ClearcoatRoughness:", 0) == 0) {
            mat.ClearcoatRoughness = std::stof(Trim(trimmed.substr(19)));
        } else if (trimmed.rfind("AnisotropyStrength:", 0) == 0) {
            mat.AnisotropyStrength = std::stof(Trim(trimmed.substr(19)));
        } else if (trimmed.rfind("AnisotropyRotation:", 0) == 0) {
            mat.AnisotropyRotation = std::stof(Trim(trimmed.substr(19)));
        } else if (trimmed.rfind("SheenEnabled:", 0) == 0) {
            mat.SheenEnabled = Trim(trimmed.substr(13)) == "true";
        } else if (trimmed.rfind("SheenColor:", 0) == 0) {
            float r = 1, g = 1, b = 1;
            std::sscanf(trimmed.c_str(), "SheenColor: [ %f , %f , %f ]", &r, &g, &b);
            mat.SheenColor = glm::vec3(r, g, b);
        } else if (trimmed.rfind("SheenRoughness:", 0) == 0) {
            mat.SheenRoughness = std::stof(Trim(trimmed.substr(15)));
        } else if (trimmed.rfind("SubsurfaceEnabled:", 0) == 0) {
            mat.SubsurfaceEnabled = Trim(trimmed.substr(18)) == "true";
        } else if (trimmed.rfind("SubsurfaceColor:", 0) == 0) {
            float r = 1, g = 0.8f, b = 0.7f;
            std::sscanf(trimmed.c_str(), "SubsurfaceColor: [ %f , %f , %f ]", &r, &g, &b);
            mat.SubsurfaceColor = glm::vec3(r, g, b);
        } else if (trimmed.rfind("SubsurfaceRadius:", 0) == 0) {
            mat.SubsurfaceRadius = std::stof(Trim(trimmed.substr(17)));
        } else if (trimmed.rfind("AbsorptionEnabled:", 0) == 0) {
            mat.AbsorptionEnabled = Trim(trimmed.substr(18)) == "true";
        } else if (trimmed.rfind("AbsorptionColor:", 0) == 0) {
            float r = 0, g = 0, b = 0;
            std::sscanf(trimmed.c_str(), "AbsorptionColor: [ %f , %f , %f ]", &r, &g, &b);
            mat.AbsorptionColor = glm::vec3(r, g, b);
        } else if (trimmed.rfind("Dispersion:", 0) == 0) {
            mat.Dispersion = std::stof(Trim(trimmed.substr(11)));
        } else if (trimmed.rfind("RenderQueue:", 0) == 0) {
            mat.RenderQueue = std::stoi(Trim(trimmed.substr(12)));
        } else if (trimmed.rfind("NormalPath:", 0) == 0) {
            std::string p = Trim(trimmed.substr(11));
            if (!p.empty()) mat.SetNormal(p);
        } else if (trimmed.rfind("UseNormalMap:", 0) == 0) {
            mat.UseNormalMap = (Trim(trimmed.substr(13)) == "true");
        } else if (trimmed.rfind("NormalStrength:", 0) == 0) {
            mat.NormalStrength = std::stof(Trim(trimmed.substr(15)));
        } else if (trimmed.rfind("RoughnessPath:", 0) == 0) {
            std::string p = Trim(trimmed.substr(14));
            if (!p.empty()) mat.SetRoughnessMap(p);
        } else if (trimmed.rfind("UseRoughnessMap:", 0) == 0) {
            mat.UseRoughnessMap = (Trim(trimmed.substr(16)) == "true");
        } else if (trimmed.rfind("Roughness:", 0) == 0) {
            mat.Roughness = std::stof(Trim(trimmed.substr(10)));
        } else if (trimmed.rfind("MetallicPath:", 0) == 0) {
            std::string p = Trim(trimmed.substr(13));
            if (!p.empty()) mat.SetMetallicMap(p);
        } else if (trimmed.rfind("UseMetallicMap:", 0) == 0) {
            mat.UseMetallicMap = (Trim(trimmed.substr(15)) == "true");
        } else if (trimmed.rfind("Metallic:", 0) == 0) {
            mat.Metallic = std::stof(Trim(trimmed.substr(9)));
        } else if (trimmed.rfind("EmissionColor:", 0) == 0) {
            float r = 0, g = 0, b = 0;
            sscanf(trimmed.c_str(), "EmissionColor: [ %f , %f , %f ]", &r, &g, &b);
            mat.EmissionColor = glm::vec3(r, g, b);
        } else if (trimmed.rfind("EmissionIntensity:", 0) == 0) {
            mat.EmissionIntensity = std::stof(Trim(trimmed.substr(18)));
        } else if (trimmed.rfind("UVScale:", 0) == 0 || trimmed.rfind("UVTiling:", 0) == 0) {
            float u = 1, v = 1;
            const char* fmt = (trimmed.rfind("UVTiling:", 0) == 0) ? "UVTiling: [ %f , %f ]" : "UVScale: [ %f , %f ]";
            sscanf(trimmed.c_str(), fmt, &u, &v);
            mat.UVScale = glm::vec2(u, v);
        } else if (trimmed.rfind("UVOffset:", 0) == 0) {
            float u = 0, v = 0;
            sscanf(trimmed.c_str(), "UVOffset: [ %f , %f ]", &u, &v);
            mat.UVOffset = glm::vec2(u, v);
        } else if (trimmed.rfind("SurfaceType:", 0) == 0) {
            mat.SurfaceType = StringToSurfaceType(Trim(trimmed.substr(12)));
        } else if (trimmed.rfind("CullMode:", 0) == 0) {
            mat.CullMode = StringToCullMode(Trim(trimmed.substr(9)));
        } else if (trimmed.rfind("AlphaCutoff:", 0) == 0) {
            try { mat.AlphaCutoff = std::clamp(std::stof(Trim(trimmed.substr(12))), 0.0f, 1.0f); } catch (...) {}
        } else if (trimmed.rfind("DepthWrite:", 0) == 0) {
            std::string val = Trim(trimmed.substr(11));
            mat.DepthWrite = (val == "true" || val == "1");
        } else return false;
        return true;
    }
}
