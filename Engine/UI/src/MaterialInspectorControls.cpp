#include <Axle/MaterialInspectorControls.hpp>
#include <Axle/Components.hpp>
#include <Axle/Material.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/AssetThumbnailManager.hpp>
#include <Axle/TextureDescriptor.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Window.hpp>
#include <Axle/ShaderDragDropDecoder.hpp>
#include <Axle/RenderBackendPolicy.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIMaterialConstants.hpp>
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

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
            image=value.empty()?nullptr:MaterialCache::GetTexture(value);
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
        void Tooltip(const char* text) {
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
        }
        void ApplyMaterialPreset(MaterialComponent& m, const std::function<void(Material&)>& applyFn) {
            Material temp("Preset");
            applyFn(temp);
            const UUID handle = m.MaterialHandle;
            const auto asset = m.MaterialAsset;
            const auto path = m.AssetPath;
            const bool useCustom = m.UseCustomShader;
            const auto customPath = m.CustomShaderPath;
            const auto customProps = m.CustomProperties;
            m = MaterialComponent{};
            m.MaterialHandle = handle;
            m.MaterialAsset = asset;
            m.AssetPath = path;
            m.UseCustomShader = useCustom;
            m.CustomShaderPath = customPath;
            m.CustomProperties = customProps;
            m.SurfaceType = temp.GetSurfaceType();
            m.CullMode = temp.GetCullMode();
            m.AlphaCutoff = temp.GetAlphaCutoff();
            m.DepthWrite = temp.GetDepthWrite();
            m.EnableSpecularHighlights = temp.GetEnableSpecularHighlights();
            m.EnableEnvironmentReflections = temp.GetEnableEnvironmentReflections();
            m.RenderQueue = temp.GetRenderQueue();
            m.AlbedoColor = temp.GetAlbedoColor();
            m.Roughness = temp.GetRoughness();
            m.Metallic = temp.GetMetallic();
            m.NormalStrength = temp.GetNormalStrength();
            m.AOStrength = temp.GetAO();
            m.ClearcoatStrength = temp.GetClearcoatStrength();
            m.ClearcoatRoughness = temp.GetClearcoatRoughness();
            m.AnisotropyStrength = temp.GetAnisotropyStrength();
            m.AnisotropyRotation = temp.GetAnisotropyRotation();
            m.RefractionEnabled = temp.GetRefractionEnabled();
            m.RefractionStrength = temp.GetRefractionStrength();
            m.RefractionIOR = temp.GetRefractionIOR();
            m.RefractionThickness = temp.GetRefractionThickness();
            m.RefractionTint = temp.GetRefractionTint();
            m.SheenEnabled = temp.GetSheenEnabled();
            m.SheenColor = temp.GetSheenColor();
            m.SheenRoughness = temp.GetSheenRoughness();
            m.SubsurfaceEnabled = temp.GetSubsurfaceEnabled();
            m.SubsurfaceColor = temp.GetSubsurfaceColor();
            m.SubsurfaceRadius = temp.GetSubsurfaceRadius();
            m.AbsorptionEnabled = temp.GetAbsorptionEnabled();
            m.AbsorptionColor = temp.GetAbsorptionColor();
            m.Dispersion = temp.GetDispersion();
            m.EmissionColor = temp.GetEmissionColor();
            m.EmissionIntensity = temp.GetEmissionIntensity();
            m.UVScale = temp.GetUVScale();
            m.UVOffset = temp.GetUVOffset();
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
                                               const std::function<void(const std::string&)>& assign, bool showEnabled) {
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
        if (showEnabled) {
        ImGui::BeginDisabled(path.empty());
        changed |= ImGui::Checkbox("##Enabled",&enabled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable this texture without losing its assignment");
        ImGui::EndDisabled();
        ImGui::SameLine();
        }
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
            if (const auto* payload=ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const auto decoded=DecodeAssetPayload(payload->Data,payload->DataSize);
                if(decoded.Success)changed|=choose(decoded.Path);
                else m_Error=decoded.Error;
            }
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

    bool MaterialInspectorControls::Draw(MaterialComponent& m, const SceneRenderSettings* activeViewSettings) {
        if (m.UseCustomShader && m.MaterialAsset && m.MaterialAsset->GetCustomShaderPath()==m.CustomShaderPath) {
            std::vector<ShaderPropertyDesc> schema;
            for (const auto& property:m.MaterialAsset->GetCustomProperties()) schema.push_back(property.Desc);
            m.CustomProperties=SynchronizeCustomProperties(schema,m.CustomProperties);
        }
        ImGui::PushID(&m);
        bool changed=false;
        {
            const auto policy = RenderBackendPolicy::FromDevice(RHI::RHIDevice::Get().GetCapabilities());
            const SceneRenderSettings resolvedView = policy.ResolveSettings(
                activeViewSettings ? *activeViewSettings : SceneRenderSettings{});
            const auto diags = policy.CollectMaterialFeatureDiagnostics(
                resolvedView,
                m.UseCustomShader,
                m.RefractionEnabled,
                m.SubsurfaceEnabled && m.SubsurfaceRadius > 0.0f,
                m.AbsorptionEnabled || m.Dispersion > 0.0f);
            for (const auto& msg : diags) {
                ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f), "%s", msg.c_str());
            }
            if (!diags.empty()) ImGui::Spacing();
        }
        if (BeginGroup("Presets & Defaults")) {
            Row("Quick Presets");
            if (ImGui::SmallButton("Reset Default")) { m.ResetToDefault(); changed = true; }
            Tooltip("Restore standardized Axle PBR material defaults.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Car Paint Red")) { ApplyMaterialPreset(m, [](Material& mat){ mat.SetPresetCarPaintRed(); }); changed = true; }
            Tooltip("Glossy red clearcoat metallic paint.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Chrome")) { ApplyMaterialPreset(m, [](Material& mat){ mat.SetPresetChrome(); }); changed = true; }
            Tooltip("Polished metallic chrome (Metallic=1, Roughness=0.04).");
            Row("Advanced Presets");
            if (ImGui::SmallButton("Glass")) {
                ApplyMaterialPreset(m, [](Material& mat){
                    mat.SetPresetGlass();
                    mat.SetRefractionEnabled(true);
                    mat.SetRefractionIOR(1.52f);
                    mat.SetRefractionThickness(0.25f);
                });
                changed = true;
            }
            Tooltip("Transparent refractive glass preset with IOR 1.52.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Foliage")) { ApplyMaterialPreset(m, [](Material& mat){ mat.SetPresetFoliage(); }); changed = true; }
            Tooltip("Two-sided alpha-cutout foliage material.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Gold")) { ApplyMaterialPreset(m, [](Material& mat){ mat.SetPresetGold(); }); changed = true; }
            Tooltip("Brushed metallic gold (Metallic=1, Roughness=0.18).");
            ImGui::SameLine();
            if (ImGui::SmallButton("Rubber")) { ApplyMaterialPreset(m, [](Material& mat){ mat.SetPresetRubber(); }); changed = true; }
            Tooltip("Matte dielectric rubber (Roughness=0.85).");
            EndGroup();
        }
        if (BeginGroup("Surface / Transparency")) {
            const char* surfaces[]={"Opaque","Cutout","Transparent","Additive"};
            int surface=static_cast<int>(m.SurfaceType);
            Row("Surface Type");
            if (ImGui::Combo("##Surface",&surface,surfaces,4)) {
                m.SurfaceType=static_cast<SurfaceType>(surface);
                m.DepthWrite=m.SurfaceType!=SurfaceType::Transparent && m.SurfaceType!=SurfaceType::Additive;
                changed=true;
            }
            Tooltip("Selects render queue and blending: Opaque, Cutout (alpha-tested), Transparent, or Additive.");
            if (m.SurfaceType==SurfaceType::Cutout) {
                Row("Alpha Cutoff");
                if (ImGui::SliderFloat("##Cutoff",&m.AlphaCutoff,MaterialParameterBounds::MinAlphaCutoff,MaterialParameterBounds::MaxAlphaCutoff,"%.3f")) {
                    m.AlphaCutoff = MaterialParameterBounds::ClampFinite(
                        m.AlphaCutoff, MaterialParameterBounds::MinAlphaCutoff, MaterialParameterBounds::MaxAlphaCutoff,
                        MaterialParameterBounds::DefaultAlphaCutoff);
                    changed=true;
                }
                Tooltip("Alpha threshold [0..1] below which Cutout fragments are discarded.");
            }
            const char* faces[]={"Back","Front","Disabled (Two-sided)"};
            int face=m.CullMode==CullMode::Back?0:(m.CullMode==CullMode::Front?1:2);
            Row("Cull Mode");
            if (ImGui::Combo("##Cull",&face,faces,3)) { m.CullMode=face==0?CullMode::Back:(face==1?CullMode::Front:CullMode::None); changed=true; }
            Tooltip("Triangle face culling mode (Back, Front, or Disabled for thin two-sided geometry).");
            Row("Depth Write"); changed|=ImGui::Checkbox("##Depth",&m.DepthWrite);
            Tooltip("Controls whether this material writes depth buffer values.");
            EndGroup();
        }
        if (BeginGroup("Shading")) {
            Row("Specular Highlights"); changed|=ImGui::Checkbox("##Specular",&m.EnableSpecularHighlights);
            Tooltip("Enable direct GGX specular highlights from scene lights.");
            Row("Environment Reflections"); changed|=ImGui::Checkbox("##Environment",&m.EnableEnvironmentReflections);
            Tooltip("Enable indirect specular reflections from skybox, probes, planar reflectors, and SSR.");
            int mode=static_cast<int>(m.TextureMode);
            const char* modes[]={"Separate Maps","Packed ORM"};
            Row("Texture Packing");
            if (ImGui::Combo("##Packing",&mode,modes,2)) { m.TextureMode=static_cast<TextureMode>(mode); changed=true; }
            Tooltip("Choose separate AO/Roughness/Metallic maps or channel-packed ORM texture (R=AO, G=Roughness, B=Metallic).");
            if (m.TextureMode==TextureMode::PackedORM) {
                Row("ORM Texture"); changed|=TextureSlot("ORM",m.ORMPath,m.UseORMMap,[&](const auto& p){AssignTexture(m.ORMPath,m.ORMMap,m.UseORMMap,p);});
                Row("Channels"); ImGui::TextDisabled("R: AO  G: Roughness  B: Metallic");
            }
            EndGroup();
        }
        if (BeginGroup("Albedo",true)) {
            Row("Color");
            if (ImGui::ColorEdit4("##Color",glm::value_ptr(m.AlbedoColor),ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_AlphaPreview)) {
                m.AlbedoColor = MaterialParameterBounds::SanitizeAlbedo(m.AlbedoColor);
                changed=true;
            }
            Tooltip("Base surface reflectance color and alpha opacity (linear HDR RGB >= 0, Alpha [0..1]).");
            Row("Texture"); changed|=TextureSlot("Albedo",m.AlbedoPath,m.UseAlbedoMap,[&](const auto& p){AssignTexture(m.AlbedoPath,m.AlbedoMap,m.UseAlbedoMap,p);});
            EndGroup();
        }
        if (BeginGroup("Metallic")) {
            Row("Amount");
            if (ImGui::SliderFloat("##Amount",&m.Metallic,MaterialParameterBounds::MinMetallic,MaterialParameterBounds::MaxMetallic,"%.3f")) {
                m.Metallic = MaterialParameterBounds::ClampFinite(
                    m.Metallic, MaterialParameterBounds::MinMetallic, MaterialParameterBounds::MaxMetallic,
                    MaterialParameterBounds::DefaultMetallic);
                changed=true;
            }
            Tooltip("Conductor blend factor [0=dielectric, 1=metallic].");
            if (m.TextureMode==TextureMode::SeparateMaps) { Row("Texture"); changed|=TextureSlot("Metallic",m.MetallicPath,m.UseMetallicMap,[&](const auto& p){AssignTexture(m.MetallicPath,m.MetallicMap,m.UseMetallicMap,p);}); }
            EndGroup();
        }
        if (BeginGroup("Roughness")) {
            Row("Amount");
            const float minRough = m.UseRoughnessMap ? 0.0f : MaterialParameterBounds::MinScalarRoughness;
            if (ImGui::SliderFloat("##Amount",&m.Roughness,minRough,MaterialParameterBounds::MaxRoughness,"%.3f")) {
                m.Roughness = MaterialParameterBounds::ClampFinite(
                    m.Roughness, minRough, MaterialParameterBounds::MaxRoughness,
                    MaterialParameterBounds::DefaultRoughness);
                changed=true;
            }
            Tooltip("Microfacet surface roughness [0.04=mirror-like, 1.0=matte]. Multiplies Roughness/ORM green channel when mapped.");
            if (m.TextureMode==TextureMode::SeparateMaps) { Row("Texture"); changed|=TextureSlot("Roughness",m.RoughnessPath,m.UseRoughnessMap,[&](const auto& p){AssignTexture(m.RoughnessPath,m.RoughnessMap,m.UseRoughnessMap,p);}); }
            EndGroup();
        }
        if (BeginGroup("Emission")) {
            Row("Color");
            if (ImGui::ColorEdit3("##Color",glm::value_ptr(m.EmissionColor),ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float)) {
                m.EmissionColor = MaterialParameterBounds::ClampMinVec3(m.EmissionColor, 0.0f);
                changed=true;
            }
            Tooltip("Self-illumination color in linear HDR space (>= 0).");
            Row("Intensity");
            if (ImGui::DragFloat("##Intensity",&m.EmissionIntensity,0.05f,MaterialParameterBounds::MinEmissionIntensity,MaterialParameterBounds::MaxEmissionIntensity,"%.2f")) {
                m.EmissionIntensity = MaterialParameterBounds::ClampFinite(
                    m.EmissionIntensity, MaterialParameterBounds::MinEmissionIntensity, MaterialParameterBounds::MaxEmissionIntensity, 0.0f);
                changed=true;
            }
            Tooltip("Emissive radiance multiplier [0..10000].");
            Row("Texture"); changed|=TextureSlot("Emission",m.EmissivePath,m.UseEmissiveMap,[&](const auto& p){AssignTexture(m.EmissivePath,m.EmissiveMap,m.UseEmissiveMap,p);});
            EndGroup();
        }
        if (BeginGroup("Normal Map")) {
            Row("Strength");
            if (ImGui::SliderFloat("##Strength",&m.NormalStrength,MaterialParameterBounds::MinNormalStrength,MaterialParameterBounds::MaxNormalStrength,"%.2f")) {
                m.NormalStrength = MaterialParameterBounds::ClampFinite(
                    m.NormalStrength, MaterialParameterBounds::MinNormalStrength, MaterialParameterBounds::MaxNormalStrength,
                    MaterialParameterBounds::DefaultNormalStrength);
                changed=true;
            }
            Tooltip("Tangent-space normal map perturbation multiplier [0..5].");
            Row("Texture"); changed|=TextureSlot("Normal",m.NormalPath,m.UseNormalMap,[&](const auto& p){AssignTexture(m.NormalPath,m.NormalMap,m.UseNormalMap,p);});
            EndGroup();
        }
        if (BeginGroup("Ambient Occlusion")) {
            Row("Strength");
            if (ImGui::SliderFloat("##Strength",&m.AOStrength,MaterialParameterBounds::MinAO,MaterialParameterBounds::MaxAO,"%.2f")) {
                m.AOStrength = MaterialParameterBounds::ClampFinite(
                    m.AOStrength, MaterialParameterBounds::MinAO, MaterialParameterBounds::MaxAO,
                    MaterialParameterBounds::DefaultAO);
                changed=true;
            }
            Tooltip("Diffuse indirect ambient occlusion strength [0..1].");
            if (m.TextureMode==TextureMode::SeparateMaps) { Row("Texture"); changed|=TextureSlot("AO",m.AOPath,m.UseAOMap,[&](const auto& p){AssignTexture(m.AOPath,m.AOMap,m.UseAOMap,p);}); }
            EndGroup();
        }
        if (BeginGroup("Clearcoat")) {
            Row("Strength");
            if (ImGui::SliderFloat("##Strength",&m.ClearcoatStrength,MaterialParameterBounds::MinClearcoatStrength,MaterialParameterBounds::MaxClearcoatStrength,"%.2f")) {
                m.ClearcoatStrength = MaterialParameterBounds::ClampFinite(
                    m.ClearcoatStrength, MaterialParameterBounds::MinClearcoatStrength, MaterialParameterBounds::MaxClearcoatStrength, 0.0f);
                changed=true;
            }
            Tooltip("Dielectric top-coat weight [0..1] for car paint and lacquered surfaces.");
            Row("Roughness");
            if (ImGui::SliderFloat("##Roughness",&m.ClearcoatRoughness,MaterialParameterBounds::MinClearcoatRoughnessUI,MaterialParameterBounds::MaxClearcoatRoughness,"%.3f")) {
                m.ClearcoatRoughness = MaterialParameterBounds::ClampFinite(
                    m.ClearcoatRoughness, MaterialParameterBounds::MinClearcoatRoughnessUI, MaterialParameterBounds::MaxClearcoatRoughness,
                    MaterialParameterBounds::DefaultClearcoatRoughness);
                changed=true;
            }
            Tooltip("Microfacet roughness [0.04..1] of the clearcoat specular lobe.");
            Row("Texture"); changed|=TextureSlot("Clearcoat",m.ClearcoatPath,m.UseClearcoatMap,[&](const auto& p){AssignTexture(m.ClearcoatPath,m.ClearcoatMap,m.UseClearcoatMap,p);});
            Row("Channels"); ImGui::TextDisabled("R: Strength  G: Glossiness");
            EndGroup();
        }
        if (BeginGroup("Anisotropy")) {
            Row("Strength");
            if (ImGui::SliderFloat("##Strength",&m.AnisotropyStrength,MaterialParameterBounds::MinAnisotropyStrength,MaterialParameterBounds::MaxAnisotropyStrength,"%.2f")) {
                m.AnisotropyStrength = MaterialParameterBounds::ClampFinite(
                    m.AnisotropyStrength, MaterialParameterBounds::MinAnisotropyStrength, MaterialParameterBounds::MaxAnisotropyStrength, 0.0f);
                changed=true;
            }
            Tooltip("Directional brushed specular eccentricity [-1..1].");
            Row("Rotation");
            if (ImGui::SliderFloat("##Rotation",&m.AnisotropyRotation,MaterialParameterBounds::MinAnisotropyRotation,MaterialParameterBounds::MaxAnisotropyRotation,"%.3f turns")) {
                m.AnisotropyRotation = MaterialParameterBounds::ClampFinite(
                    m.AnisotropyRotation, MaterialParameterBounds::MinAnisotropyRotation, MaterialParameterBounds::MaxAnisotropyRotation, 0.0f);
                changed=true;
            }
            Tooltip("Tangent-space anisotropy axis rotation in full turns [0..1].");
            Row("Texture"); changed|=TextureSlot("AnisotropyFlow",m.AnisotropyFlowPath,m.UseAnisotropyFlowMap,[&](const auto& p){AssignTexture(m.AnisotropyFlowPath,m.AnisotropyFlowMap,m.UseAnisotropyFlowMap,p);});
            Row("Channels"); ImGui::TextDisabled("RG: Direction  A: Strength");
            EndGroup();
        }
        if (BeginGroup("Refraction")) {
            Row("Enabled"); changed|=ImGui::Checkbox("##Enabled",&m.RefractionEnabled);
            Tooltip("Enable screen-space refraction for Transparent materials on D3D12/Vulkan.");
            if (m.RefractionEnabled) {
                Row("Strength");
                if (ImGui::SliderFloat("##Strength",&m.RefractionStrength,MaterialParameterBounds::MinRefractionStrength,MaterialParameterBounds::MaxRefractionStrength,"%.2f")) {
                    m.RefractionStrength = MaterialParameterBounds::ClampFinite(
                        m.RefractionStrength, MaterialParameterBounds::MinRefractionStrength, MaterialParameterBounds::MaxRefractionStrength,
                        MaterialParameterBounds::DefaultRefractionStrength);
                    changed=true;
                }
                Tooltip("Screen-space distortion weight [0..1].");
                Row("IOR");
                if (ImGui::SliderFloat("##IOR",&m.RefractionIOR,MaterialParameterBounds::MinRefractionIOR,MaterialParameterBounds::MaxRefractionIOR,"%.3f")) {
                    m.RefractionIOR = MaterialParameterBounds::ClampFinite(
                        m.RefractionIOR, MaterialParameterBounds::MinRefractionIOR, MaterialParameterBounds::MaxRefractionIOR,
                        MaterialParameterBounds::DefaultRefractionIOR);
                    changed=true;
                }
                Tooltip("Index of refraction [1.0=air, 1.33=water, 1.5=glass, 2.4=diamond, max 3.0].");
                Row("Thickness");
                if (ImGui::DragFloat("##Thickness",&m.RefractionThickness,0.01f,MaterialParameterBounds::MinRefractionThickness,MaterialParameterBounds::MaxRefractionThickness,"%.3f")) {
                    m.RefractionThickness = MaterialParameterBounds::ClampFinite(
                        m.RefractionThickness, MaterialParameterBounds::MinRefractionThickness, MaterialParameterBounds::MaxRefractionThickness,
                        MaterialParameterBounds::DefaultRefractionThickness);
                    changed=true;
                }
                Tooltip("Effective optical thickness in meters [0..100] used for refraction offset and Beer-Lambert absorption.");
                Row("Tint");
                if (ImGui::ColorEdit3("##Tint",glm::value_ptr(m.RefractionTint))) {
                    m.RefractionTint = MaterialParameterBounds::ClampVec3(m.RefractionTint, 0.0f, 1.0f, glm::vec3(1.0f));
                    changed=true;
                }
                Tooltip("Transmitted refraction multiplier tint [0..1].");
                Row("Scene input"); ImGui::TextDisabled("Opaque color + depth");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Screen-space opaque-only refraction. Requires Transparent surface. Falls back to alpha when MSAA, scene buffers, or opaque preview background are unavailable.");
                if (m.SurfaceType!=SurfaceType::Transparent) { Row("Surface"); ImGui::TextDisabled("Select Transparent to refract"); }
            }
            EndGroup();
        }
        if (m.SheenEnabled) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (BeginGroup("Sheen")) {
            Row("Enabled"); changed|=ImGui::Checkbox("##Enabled",&m.SheenEnabled);
            Tooltip("Enable grazing-angle microfiber Charlie sheen lobe (velvet/cloth).");
            if (m.SheenEnabled) {
                Row("Color");
                if (ImGui::ColorEdit3("##Color",glm::value_ptr(m.SheenColor))) {
                    m.SheenColor=MaterialParameterBounds::ClampVec3(m.SheenColor,0.0f,1.0f,glm::vec3(1.0f));
                    changed=true;
                }
                Tooltip("Retro-reflective cloth/velvet sheen tint [0..1].");
                Row("Roughness");
                if (ImGui::SliderFloat("##Roughness",&m.SheenRoughness,MaterialParameterBounds::MinSheenRoughness,MaterialParameterBounds::MaxSheenRoughness,"%.2f")) {
                    m.SheenRoughness=MaterialParameterBounds::ClampFinite(
                        m.SheenRoughness, MaterialParameterBounds::MinSheenRoughness, MaterialParameterBounds::MaxSheenRoughness,
                        MaterialParameterBounds::DefaultSheenRoughness);
                    changed=true;
                }
                Tooltip("Microfiber sheen roughness [0.01..1]. Higher values create softer wide grazing halos.");
            }
            EndGroup();
        }
        if (m.SubsurfaceEnabled) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (BeginGroup("Subsurface Scattering")) {
            Row("Enabled"); changed|=ImGui::Checkbox("##Enabled",&m.SubsurfaceEnabled);
            Tooltip("Enable separable screen-space subsurface diffusion (D3D12/Vulkan, Opaque/Cutout, MSAA 1x).");
            if (m.SubsurfaceEnabled) {
                Row("Color");
                if (ImGui::ColorEdit3("##Color",glm::value_ptr(m.SubsurfaceColor))) {
                    m.SubsurfaceColor=MaterialParameterBounds::ClampVec3(m.SubsurfaceColor,0.0f,1.0f,glm::vec3(1.0f,0.8f,0.7f));
                    changed=true;
                }
                Tooltip("Subsurface scatter bleed tint [0..1] (for skin, wax, marble, foliage).");
                Row("Radius");
                if (ImGui::SliderFloat("##Radius",&m.SubsurfaceRadius,MaterialParameterBounds::MinSubsurfaceRadius,MaterialParameterBounds::MaxSubsurfaceRadius,"%.2f")) {
                    m.SubsurfaceRadius=MaterialParameterBounds::ClampFinite(
                        m.SubsurfaceRadius, MaterialParameterBounds::MinSubsurfaceRadius, MaterialParameterBounds::MaxSubsurfaceRadius,
                        MaterialParameterBounds::DefaultSubsurfaceRadius);
                    changed=true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Screen-space separable subsurface scattering radius (0-16). Single-sample RHI pass; disabled when MSAA > 1.");
                if (m.SurfaceType>=SurfaceType::Transparent) { Row("Surface"); ImGui::TextDisabled("Select Opaque or Cutout to scatter"); }
            }
            EndGroup();
        }
        if (m.AbsorptionEnabled || m.Dispersion > 0.0f) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (BeginGroup("Absorption & Dispersion")) {
            Row("Absorption"); changed|=ImGui::Checkbox("##AbsorptionEnabled",&m.AbsorptionEnabled);
            Tooltip("Enable volumetric Beer-Lambert color absorption along refracted rays.");
            if (m.AbsorptionEnabled) {
                Row("Sigma Color");
                if (ImGui::ColorEdit3("##AbsorptionColor",glm::value_ptr(m.AbsorptionColor),ImGuiColorEditFlags_Float|ImGuiColorEditFlags_HDR)) {
                    m.AbsorptionColor=MaterialParameterBounds::ClampMinVec3(m.AbsorptionColor,0.0f);
                    changed=true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Beer-Lambert per-channel absorption coefficient (sigma >= 0) over refraction thickness.");
            }
            if (m.AbsorptionEnabled || m.RefractionEnabled) {
                Row("Dispersion");
                if (ImGui::SliderFloat("##Dispersion",&m.Dispersion,MaterialParameterBounds::MinDispersion,MaterialParameterBounds::MaxDispersion,"%.3f")) {
                    m.Dispersion=MaterialParameterBounds::ClampFinite(
                        m.Dispersion, MaterialParameterBounds::MinDispersion, MaterialParameterBounds::MaxDispersion, 0.0f);
                    changed=true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cauchy RGB chromatic dispersion across IOR on refractive transparent surfaces.");
                if (!m.RefractionEnabled || m.SurfaceType!=SurfaceType::Transparent) {
                    Row("Surface"); ImGui::TextDisabled("Enable Refraction on Transparent to transmit");
                }
            }
            EndGroup();
        }
        if (BeginGroup("Detail Normal")) {
            Row("Texture"); changed|=TextureSlot("DetailNormal",m.DetailNormalPath,m.UseDetailNormalMap,[&](const auto& p){AssignTexture(m.DetailNormalPath,m.DetailNormalMap,m.UseDetailNormalMap,p);});
            Row("Strength");
            if (ImGui::SliderFloat("##Strength",&m.DetailNormalStrength,MaterialParameterBounds::MinDetailNormalStrength,MaterialParameterBounds::MaxDetailNormalStrength,"%.2f")) {
                m.DetailNormalStrength = MaterialParameterBounds::ClampFinite(
                    m.DetailNormalStrength, MaterialParameterBounds::MinDetailNormalStrength, MaterialParameterBounds::MaxDetailNormalStrength,
                    MaterialParameterBounds::DefaultDetailNormalStrength);
                changed=true;
            }
            Tooltip("Secondary micro-normal perturbation blend weight [0..4].");
            Row("Mask (R)"); changed|=TextureSlot("DetailMask",m.DetailMaskPath,m.UseDetailMaskMap,[&](const auto& p){AssignTexture(m.DetailMaskPath,m.DetailMaskMap,m.UseDetailMaskMap,p);});
            Row("Tiling");
            if (ImGui::DragFloat2("##Tiling",glm::value_ptr(m.DetailUVScale),0.05f,-1000,1000)) {
                m.DetailUVScale = MaterialParameterBounds::SanitizeVec2(m.DetailUVScale, glm::vec2(1.0f));
                changed=true;
            }
            Tooltip("Detail normal texture UV repeat scale.");
            Row("Offset");
            if (ImGui::DragFloat2("##Offset",glm::value_ptr(m.DetailUVOffset),0.02f,-1000,1000)) {
                m.DetailUVOffset = MaterialParameterBounds::SanitizeVec2(m.DetailUVOffset, glm::vec2(0.0f));
                changed=true;
            }
            Tooltip("Detail normal texture UV translation offset.");
            EndGroup();
        }
        if (BeginGroup("UV")) {
            Row("Tiling");
            if (ImGui::DragFloat2("##Tiling",glm::value_ptr(m.UVScale),0.05f,-1000,1000)) {
                m.UVScale = MaterialParameterBounds::SanitizeVec2(m.UVScale, glm::vec2(1.0f));
                changed=true;
            }
            Tooltip("Primary surface UV repeat scale.");
            Row("Offset");
            if (ImGui::DragFloat2("##Offset",glm::value_ptr(m.UVOffset),0.02f,-1000,1000)) {
                m.UVOffset = MaterialParameterBounds::SanitizeVec2(m.UVOffset, glm::vec2(0.0f));
                changed=true;
            }
            Tooltip("Primary surface UV translation offset.");
            EndGroup();
        }
        if (BeginGroup("Advanced Options")) {
            Row("Render Queue");
            if (ImGui::DragInt("##Queue",&m.RenderQueue,1,MaterialParameterBounds::MinRenderQueue,MaterialParameterBounds::MaxRenderQueue)) {
                m.RenderQueue = std::clamp(m.RenderQueue, MaterialParameterBounds::MinRenderQueue, MaterialParameterBounds::MaxRenderQueue);
                changed=true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Orders materials within their render pass [1000..5000]. Transparent ties draw back-to-front.");
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
                } else if (property.Desc.Type==ShaderPropertyType::Texture2D) {
                    bool enabled=!property.TexturePath.empty();
                    changed|=TextureSlot("CustomTexture",property.TexturePath,enabled,
                        [&](const std::string& path){property.TexturePath=path;},false);
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
                AssignTexture(m.DetailNormalPath,m.DetailNormalMap,m.UseDetailNormalMap,"");
                AssignTexture(m.DetailMaskPath,m.DetailMaskMap,m.UseDetailMaskMap,"");
                AssignTexture(m.ClearcoatPath,m.ClearcoatMap,m.UseClearcoatMap,"");
                AssignTexture(m.AnisotropyFlowPath,m.AnisotropyFlowMap,m.UseAnisotropyFlowMap,"");
                changed=true;
            }
            EndGroup();
        }
        if (!m_Error.empty()) ImGui::TextWrapped("%s",m_Error.c_str());
        ImGui::PopID();
        return changed;
    }
}
