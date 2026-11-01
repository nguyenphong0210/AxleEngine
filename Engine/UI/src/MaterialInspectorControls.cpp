#include <Axle/MaterialInspectorControls.hpp>
#include <Axle/Components.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/AssetThumbnailManager.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Window.hpp>
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cctype>

namespace Axle {
    namespace {
        std::string Lower(std::string value) {
            std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            return value;
        }
        bool IsTexture(const std::filesystem::path& path) {
            auto ext = Lower(path.extension().string());
            if (ext == ".axleasset") {
                TextureDescriptor descriptor;
                return TextureDescriptor::Load(path.string(),descriptor);
            }
            return ext==".png" || ext==".jpg" || ext==".jpeg" || ext==".tga" || ext==".bmp" || ext==".hdr" || ext==".psd" || ext==".pnm";
        }
        std::string RelativePath(const std::filesystem::path& path) {
            std::error_code ec;
            auto relative = std::filesystem::relative(path,FileSystem::GetAssetDirectory(),ec);
            return !ec && !relative.empty() && *relative.begin()!=".." ? relative.generic_string() : path.generic_string();
        }
        void AssignTexture(std::string& path,std::shared_ptr<Texture2D>& image,bool& enabled,const std::string& value) {
            path=value;
            image=value.empty()?nullptr:AssetManager::GetTexture(value);
            enabled=!value.empty();
        }
        bool BeginGroup(const char* title, bool open=false) {
            if (!ImGui::CollapsingHeader(title,open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None)) return false;
            ImGui::PushID(title);
            if (ImGui::BeginTable("Properties",2,ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Property",ImGuiTableColumnFlags_WidthStretch,0.35f);
                ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch,0.65f);
                return true;
            }
            ImGui::PopID();
            return false;
        }
        void EndGroup() { ImGui::EndTable(); ImGui::PopID(); ImGui::Spacing(); }
        void Row(const char* label) {
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
        }
    }

    std::vector<std::filesystem::path> MaterialInspectorControls::FindTextures(const std::filesystem::path& root) {
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec);
        for (; !ec && it!=std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            std::error_code entryError;
            if (it->is_regular_file(entryError) && IsTexture(it->path())) files.push_back(it->path());
        }
        std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return Lower(a.generic_string())<Lower(b.generic_string());});
        return files;
    }

    bool MaterialInspectorControls::TextureSlot(const char* id, std::string& path, bool& enabled,
                                               const std::function<void(const std::string&)>& assign) {
        ImGui::PushID(id);
        bool changed=false;
        auto choose = [&](const std::filesystem::path& chosen) {
            auto resolved = FileSystem::ResolveAssetPath(chosen);
            if (!FileSystem::Exists(resolved) || !IsTexture(resolved)) {
                m_Error="Choose an image or a texture descriptor from the project.";
                return false;
            }
            assign(RelativePath(resolved));
            enabled=true;
            m_Error.clear();
            return true;
        };
        ImGui::BeginDisabled(path.empty());
        changed |= ImGui::Checkbox("##Enabled",&enabled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable this texture without losing its assignment");
        ImGui::EndDisabled();
        ImGui::SameLine();
        bool clicked = ImGui::Button("##ChooseTexture",ImVec2(ImGui::GetContentRegionAvail().x,32));
        auto min=ImGui::GetItemRectMin(), max=ImGui::GetItemRectMax();
        auto texture = path.empty() ? 0 : AssetThumbnailManager::GetPreview(FileSystem::ResolveAssetPath(path));
        auto* draw=ImGui::GetWindowDrawList();
        draw->PushClipRect(min,max,true);
        if (texture) {
            bool flip=Window::HasActiveContext();
            draw->AddImage(static_cast<ImTextureID>(texture),ImVec2(min.x+3,min.y+3),ImVec2(min.x+29,min.y+29),ImVec2(0,flip?1.0f:0.0f),ImVec2(1,flip?0.0f:1.0f));
        }
        std::string label=path.empty()?"Select texture...":std::filesystem::path(path).filename().string();
        draw->AddText(ImVec2(min.x+34,min.y+(32-ImGui::GetFontSize())*0.5f),ImGui::GetColorU32(ImGuiCol_Text),label.c_str());
        draw->PopClipRect();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nClick to search, or drag an image here. Right-click for texture actions.",path.empty()?"No texture assigned":path.c_str());
        if (clicked) ImGui::OpenPopup("TexturePicker");
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload=ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                changed |= choose(std::filesystem::path(static_cast<const char*>(payload->Data)));
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("TextureActions")) {
            if (ImGui::MenuItem("Clear Texture",nullptr,false,!path.empty())) { assign(""); enabled=false; changed=true; }
            if (ImGui::MenuItem("Copy Path",nullptr,false,!path.empty())) ImGui::SetClipboardText(path.c_str());
            ImGui::EndPopup();
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(340,0),ImVec2(700,420));
        if (ImGui::BeginPopup("TexturePicker")) {
            if (ImGui::IsWindowAppearing()) {
                m_Textures=FindTextures(FileSystem::GetAssetDirectory()); m_Search[0]=0;
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-75);
            ImGui::InputTextWithHint("##Search","Search textures...",m_Search,sizeof(m_Search));
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) m_Textures=FindTextures(FileSystem::GetAssetDirectory());
            ImGui::Separator();
            if (ImGui::Selectable("None",path.empty())) { assign(""); enabled=false; changed=true; }
            size_t matches=0;
            auto query=Lower(m_Search);
            for (const auto& file:m_Textures) {
                auto name=RelativePath(file);
                if (Lower(name).find(query)==std::string::npos) continue;
                ++matches;
                if (ImGui::Selectable(name.c_str(),name==path)) changed |= choose(file);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",file.string().c_str());
            }
            if (!matches) ImGui::TextDisabled("No matching textures. Import an image into the project.");
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }

    bool MaterialInspectorControls::Draw(MaterialComponent& m) {
        ImGui::PushID(&m);
        bool changed=false;
        if (BeginGroup("Surface / Transparency")) {
            const char* surfaces[]={"Opaque","Cutout","Transparent","Additive"};
            int surface=static_cast<int>(m.SurfaceType);
            Row("Surface Type");
            if (ImGui::Combo("##Surface",&surface,surfaces,4)) {
                m.SurfaceType=static_cast<SurfaceType>(surface);
                m.DepthWrite=m.SurfaceType!=SurfaceType::Transparent && m.SurfaceType!=SurfaceType::Additive;
                changed=true;
            }
            if (m.SurfaceType==SurfaceType::Cutout) { Row("Alpha Cutoff"); changed|=ImGui::SliderFloat("##Cutoff",&m.AlphaCutoff,0,1); }
            const char* faces[]={"Back","Front","Disabled (Two-sided)"};
            int face=m.CullMode==CullMode::Back?0:(m.CullMode==CullMode::Front?1:2);
            Row("Cull Mode");
            if (ImGui::Combo("##Cull",&face,faces,3)) { m.CullMode=face==0?CullMode::Back:(face==1?CullMode::Front:CullMode::None); changed=true; }
            Row("Depth Write"); changed|=ImGui::Checkbox("##Depth",&m.DepthWrite);
            EndGroup();
        }
        if (BeginGroup("Shading")) {
            Row("Specular Highlights"); changed|=ImGui::Checkbox("##Specular",&m.EnableSpecularHighlights);
            Row("Environment Reflections"); changed|=ImGui::Checkbox("##Environment",&m.EnableEnvironmentReflections);
            int mode=static_cast<int>(m.TextureMode);
            const char* modes[]={"Separate Maps","Packed ORM"};
            Row("Texture Packing");
            if (ImGui::Combo("##Packing",&mode,modes,2)) { m.TextureMode=static_cast<TextureMode>(mode); changed=true; }
            if (m.TextureMode==TextureMode::PackedORM) {
                Row("ORM Texture"); changed|=TextureSlot("ORM",m.ORMPath,m.UseORMMap,[&](const auto& p){AssignTexture(m.ORMPath,m.ORMMap,m.UseORMMap,p);});
                Row("Channels"); ImGui::TextDisabled("R: AO  G: Roughness  B: Metallic");
            }
            EndGroup();
        }
        if (BeginGroup("Albedo",true)) {
            Row("Color"); changed|=ImGui::ColorEdit4("##Color",glm::value_ptr(m.AlbedoColor),ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_AlphaPreview);
            Row("Texture"); changed|=TextureSlot("Albedo",m.AlbedoPath,m.UseAlbedoMap,[&](const auto& p){AssignTexture(m.AlbedoPath,m.AlbedoMap,m.UseAlbedoMap,p);});
            EndGroup();
        }
        if (BeginGroup("Metallic")) {
            Row("Amount"); changed|=ImGui::SliderFloat("##Amount",&m.Metallic,0,1);
            if (m.TextureMode==TextureMode::SeparateMaps) { Row("Texture"); changed|=TextureSlot("Metallic",m.MetallicPath,m.UseMetallicMap,[&](const auto& p){AssignTexture(m.MetallicPath,m.MetallicMap,m.UseMetallicMap,p);}); }
            EndGroup();
        }
        if (BeginGroup("Roughness")) {
            Row("Amount"); changed|=ImGui::SliderFloat("##Amount",&m.Roughness,0,1);
            if (m.TextureMode==TextureMode::SeparateMaps) { Row("Texture"); changed|=TextureSlot("Roughness",m.RoughnessPath,m.UseRoughnessMap,[&](const auto& p){AssignTexture(m.RoughnessPath,m.RoughnessMap,m.UseRoughnessMap,p);}); }
            EndGroup();
        }
        if (BeginGroup("Emission")) {
            Row("Color"); changed|=ImGui::ColorEdit3("##Color",glm::value_ptr(m.EmissionColor),ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float);
            Row("Intensity"); changed|=ImGui::DragFloat("##Intensity",&m.EmissionIntensity,0.05f,0,10000);
            Row("Texture"); changed|=TextureSlot("Emission",m.EmissivePath,m.UseEmissiveMap,[&](const auto& p){AssignTexture(m.EmissivePath,m.EmissiveMap,m.UseEmissiveMap,p);});
            EndGroup();
        }
        if (BeginGroup("Normal Map")) {
            Row("Strength"); changed|=ImGui::SliderFloat("##Strength",&m.NormalStrength,0,5);
            Row("Texture"); changed|=TextureSlot("Normal",m.NormalPath,m.UseNormalMap,[&](const auto& p){AssignTexture(m.NormalPath,m.NormalMap,m.UseNormalMap,p);});
            EndGroup();
        }
        if (BeginGroup("Ambient Occlusion")) {
            Row("Strength"); changed|=ImGui::SliderFloat("##Strength",&m.AOStrength,0,1);
            if (m.TextureMode==TextureMode::SeparateMaps) { Row("Texture"); changed|=TextureSlot("AO",m.AOPath,m.UseAOMap,[&](const auto& p){AssignTexture(m.AOPath,m.AOMap,m.UseAOMap,p);}); }
            EndGroup();
        }
        if (BeginGroup("Clearcoat")) {
            Row("Strength"); changed|=ImGui::SliderFloat("##Strength",&m.ClearcoatStrength,0,1);
            Row("Roughness"); changed|=ImGui::SliderFloat("##Roughness",&m.ClearcoatRoughness,0.04f,1);
            Row("Texture"); changed|=TextureSlot("Clearcoat",m.ClearcoatPath,m.UseClearcoatMap,[&](const auto& p){AssignTexture(m.ClearcoatPath,m.ClearcoatMap,m.UseClearcoatMap,p);});
            Row("Channels"); ImGui::TextDisabled("R: Strength  G: Glossiness");
            EndGroup();
        }
        if (BeginGroup("Anisotropy")) {
            Row("Strength"); changed|=ImGui::SliderFloat("##Strength",&m.AnisotropyStrength,-1,1);
            Row("Rotation"); changed|=ImGui::SliderFloat("##Rotation",&m.AnisotropyRotation,0,1,"%.3f turns");
            EndGroup();
        }
        if (BeginGroup("UV")) {
            Row("Tiling"); changed|=ImGui::DragFloat2("##Tiling",glm::value_ptr(m.UVScale),0.05f,-1000,1000);
            Row("Offset"); changed|=ImGui::DragFloat2("##Offset",glm::value_ptr(m.UVOffset),0.02f,-1000,1000);
            EndGroup();
        }
        if (BeginGroup("Advanced Options")) {
            Row("Render Queue"); changed|=ImGui::DragInt("##Queue",&m.RenderQueue,1,1000,5000);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Orders materials within their render pass. Transparent ties draw back-to-front.");
            EndGroup();
        }
        if (m.UseCustomShader && !m.CustomProperties.empty() && BeginGroup("Shader Parameters",true)) {
            for (auto& property:m.CustomProperties) {
                ImGui::PushID(property.Desc.Name.c_str());
                Row(property.Desc.DisplayName.empty()?property.Desc.Name.c_str():property.Desc.DisplayName.c_str());
                if (property.Desc.Type==ShaderPropertyType::Float) {
                    if (property.Desc.HasRange) changed|=ImGui::SliderFloat("##Value",&property.FloatVal,property.Desc.MinValue,property.Desc.MaxValue);
                    else changed|=ImGui::DragFloat("##Value",&property.FloatVal,0.01f);
                } else if (property.Desc.Type==ShaderPropertyType::Color) {
                    changed|=ImGui::ColorEdit4("##Value",glm::value_ptr(property.Vec4Val),ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_HDR);
                } else if (property.Desc.Type==ShaderPropertyType::Float2) {
                    changed|=ImGui::DragFloat2("##Value",glm::value_ptr(property.Vec4Val),0.01f);
                } else if (property.Desc.Type==ShaderPropertyType::Float3) {
                    changed|=ImGui::DragFloat3("##Value",glm::value_ptr(property.Vec4Val),0.01f);
                } else if (property.Desc.Type==ShaderPropertyType::Float4) {
                    changed|=ImGui::DragFloat4("##Value",glm::value_ptr(property.Vec4Val),0.01f);
                } else if (property.Desc.Type==ShaderPropertyType::Int) {
                    changed|=ImGui::DragInt("##Value",&property.IntVal);
                } else if (property.Desc.Type==ShaderPropertyType::Bool) {
                    changed|=ImGui::Checkbox("##Value",&property.BoolVal);
                } else {
                    ImGui::TextUnformatted(property.TexturePath.empty()?"No texture assigned":property.TexturePath.c_str());
                }
                ImGui::PopID();
            }
            EndGroup();
        }
        if (BeginGroup("Texture Tools")) {
            Row("Quick Setup");
            if (ImGui::Button("Assign Prototype Textures")) {
                m.SetAlbedo("textures/Prototype_Grid.png");
                m.SetNormal("textures/Flat_Normal.png");
                m.SetRoughnessMap("textures/Roughness_Gradient.png");
                changed=true;
            }
            Row("Assignments");
            if (ImGui::Button("Clear All Textures")) {
                m.SetAlbedo(""); m.SetNormal(""); m.SetRoughnessMap(""); m.SetMetallicMap("");
                m.SetAOMap(""); m.SetEmissiveMap(""); m.SetORMMap("");
                AssignTexture(m.ClearcoatPath,m.ClearcoatMap,m.UseClearcoatMap,"");
                changed=true;
            }
            EndGroup();
        }
        if (!m_Error.empty()) ImGui::TextWrapped("%s",m_Error.c_str());
        ImGui::PopID();
        return changed;
    }
}
