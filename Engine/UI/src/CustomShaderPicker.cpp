#include <Axle/CustomShaderPicker.hpp>
#include <Axle/Components.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/HLSLStudioPanel.hpp>
#include <Axle/ShaderPropertyReflection.hpp>
#include <Axle/ShaderDragDropDecoder.hpp>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <fstream>

namespace Axle {
    namespace {
        std::string Lower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return value;
        }

        bool IsShader(const std::filesystem::path& path) {
            auto extension = Lower(path.extension().string());
            return extension == ".hlsl" || extension == ".shader";
        }

        std::filesystem::path ResolveShader(const std::filesystem::path& root, const std::filesystem::path& path) {
            std::error_code ec;
            if (path.is_absolute()) return path;
            if (std::filesystem::is_regular_file(root / path, ec)) return root / path;
            return FileSystem::ResolveAssetPath(path);
        }

        std::string ShaderLabel(const std::filesystem::path& root, const std::filesystem::path& path) {
            std::error_code ec;
            auto relative = std::filesystem::relative(path, root, ec);
            return ec || relative.empty() || *relative.begin() == ".." ? path.generic_string() : relative.generic_string();
        }
    }

    std::vector<std::filesystem::path> CustomShaderPicker::FindShaders(const std::filesystem::path& assetRoot) {
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(assetRoot, std::filesystem::directory_options::skip_permission_denied, ec);
        for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec) && IsShader(it->path())) files.push_back(it->path());
        }
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
            return Lower(a.generic_string()) < Lower(b.generic_string());
        });
        return files;
    }

    bool CustomShaderPicker::GetNewShaderPath(const std::filesystem::path& assetRoot, const std::string& name,
                                              std::filesystem::path& result, std::string& error) {
        result.clear();
        error.clear();
        auto first = name.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) { error = "Enter a shader name."; return false; }
        std::string stem = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
        if (Lower(std::filesystem::path(stem).extension().string()) == ".hlsl") stem.resize(stem.size() - 5);
        if (stem.empty() || stem == "." || stem == ".." || stem.back() == '.' || stem.back() == ' ' ||
            stem.find_first_of("<>:\"/\\|?*") != std::string::npos ||
            std::any_of(stem.begin(), stem.end(), [](unsigned char c) { return c < 32; })) {
            error = "Use a file name without folders or special characters: < > : \" / \\ | ? *";
            return false;
        }
        std::string base = Lower(stem.substr(0, stem.find('.')));
        if (base == "con" || base == "prn" || base == "aux" || base == "nul" ||
            (base.size() == 4 && (base.substr(0, 3) == "com" || base.substr(0, 3) == "lpt") && base[3] >= '1' && base[3] <= '9')) {
            error = "That name is reserved. Choose another shader name.";
            return false;
        }
        result = assetRoot / "shaders" / (stem + ".hlsl");
        std::error_code ec;
        bool exists = std::filesystem::exists(result, ec);
        if (ec) { error = "Could not check the shader folder: " + ec.message(); return false; }
        if (exists) { error = "A shader with that name already exists. Choose another name."; return false; }
        return true;
    }

    bool CustomShaderPicker::CreateShader(const std::filesystem::path& assetRoot, const std::string& name,
                                          std::filesystem::path& result, std::string& error) {
        std::filesystem::path destination;
        result.clear();
        if (!GetNewShaderPath(assetRoot, name, destination, error)) return false;
        std::error_code ec;
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec) { error = "Could not create the shader folder: " + ec.message(); return false; }
        std::ofstream file(destination, std::ios::binary);
        if (!file) { error = "Could not create the shader file. Check that the folder is writable."; return false; }
        file << HLSLStudioPanel::GetDefaultPBRTemplate(destination.stem().string());
        file.close();
        if (!file) { error = "Could not finish writing the shader file."; return false; }
        result = destination;
        return true;
    }

    bool CustomShaderPicker::AssignShader(MaterialComponent& material, const std::filesystem::path& assetRoot,
                                          const std::filesystem::path& shaderPath, std::string& error) {
        error.clear();
        auto resolved = ResolveShader(assetRoot, shaderPath);
        std::error_code ec;
        if (!IsShader(resolved) || !std::filesystem::is_regular_file(resolved, ec)) {
            error = "Choose an existing .hlsl or .shader file.";
            return false;
        }
        auto source = FileSystem::ReadTextFile(resolved);
        if (source.empty()) { error = "The shader is empty or could not be read."; return false; }
        auto properties = ShaderPropertyParser::ParseFromHLSL(source);
        std::vector<ShaderPropertyInstance> instances;
        for (const auto& desc : properties) {
            ShaderPropertyInstance instance;
            instance.Desc = desc;
            if (desc.Type == ShaderPropertyType::Float) instance.FloatVal = desc.MinValue;
            else if (desc.Type == ShaderPropertyType::Color) instance.Vec4Val = glm::vec4(1.0f);
            else if (desc.Type == ShaderPropertyType::Int) instance.IntVal = (int)desc.MinValue;
            instances.push_back(instance);
        }
        material.CustomShaderPath = ShaderLabel(assetRoot, resolved);
        material.CustomProperties = std::move(instances);
        return true;
    }

    bool CustomShaderPicker::DecodeDragDropPayload(const void* data, size_t size,
                                                  std::filesystem::path& outPath, std::string& error) {
        auto res = DecodeShaderPayload(data, size);
        outPath = res.Path;
        error = res.Error;
        return res.Success;
    }

    bool CustomShaderPicker::Draw(const char* id, MaterialComponent& material, const std::string& suggestedName,
                                  const std::function<void(const std::filesystem::path&)>& openShader) {
        ImGui::PushID(id);
        bool changed = ImGui::Checkbox("Use Custom Shader", &material.UseCustomShader);
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Shader:");
        ImGui::SameLine();
        if (!material.UseCustomShader) {
            ImGui::BeginDisabled();
            char label[] = "Standard PBR (Built-in)";
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##BuiltIn", label, sizeof(label), ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            ImGui::Spacing();
            ImGui::PopID();
            return changed;
        }

        auto assetRoot = FileSystem::GetAssetDirectory();
        const float createWidth = ImGui::CalcTextSize("+ Create HLSL").x + ImGui::GetStyle().FramePadding.x * 2;
        const float editWidth = ImGui::CalcTextSize("</> Edit").x + ImGui::GetStyle().FramePadding.x * 2;
        const float available = ImGui::GetContentRegionAvail().x;
        const bool stacked = available < createWidth + editWidth + 140.0f;
        float slotWidth = stacked ? available : available - createWidth - editWidth - ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::SetNextItemWidth(slotWidth);
        ImGui::SetNextWindowSizeConstraints(ImVec2((std::max)(slotWidth, 320.0f), 0.0f), ImVec2(FLT_MAX, 400.0f));
        const char* preview = material.CustomShaderPath.empty() ? "Select or search shader..." : material.CustomShaderPath.c_str();
        if (ImGui::BeginCombo("##Shader", preview, ImGuiComboFlags_HeightLarge)) {
            if (ImGui::IsWindowAppearing()) {
                m_Shaders = FindShaders(assetRoot);
                m_Search[0] = '\0';
                m_Error.clear();
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-70.0f);
            ImGui::InputTextWithHint("##Search", "Search shaders...", m_Search, sizeof(m_Search));
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) m_Shaders = FindShaders(assetRoot);
            ImGui::Separator();
            if (ImGui::Selectable("None", material.CustomShaderPath.empty())) {
                material.CustomShaderPath.clear();
                material.CustomProperties.clear();
                m_Error.clear();
                changed = true;
            }
            std::string query = Lower(m_Search);
            size_t matches = 0;
            for (const auto& shader : m_Shaders) {
                std::string label = ShaderLabel(assetRoot, shader);
                if (Lower(label).find(query) == std::string::npos) continue;
                ++matches;
                if (ImGui::Selectable(label.c_str(), label == material.CustomShaderPath)) {
                    if (label != material.CustomShaderPath && AssignShader(material, assetRoot, shader, m_Error)) changed = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", shader.generic_string().c_str());
            }
            if (!matches) ImGui::TextDisabled("No matching shaders. Use + Create HLSL to add one.");
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to search project shaders, or drop a .hlsl file here.");
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                std::filesystem::path droppedPath;
                std::string decodeError;
                if (DecodeDragDropPayload(payload->Data, payload->DataSize, droppedPath, decodeError)) {
                    if (AssignShader(material, assetRoot, droppedPath, m_Error)) changed = true;
                } else {
                    m_Error = decodeError;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (!stacked) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.50f, 0.35f, 1.0f));
        if (ImGui::Button("+ Create HLSL", ImVec2(createWidth, 0))) {
            std::snprintf(m_Name, sizeof(m_Name), "%s", suggestedName.empty() ? "CustomShader" : suggestedName.c_str());
            m_CreateError.clear();
            ImGui::OpenPopup("Create HLSL Shader");
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::BeginDisabled(material.CustomShaderPath.empty());
        if (ImGui::Button("</> Edit", ImVec2(editWidth, 0)) && openShader)
            openShader(ResolveShader(assetRoot, material.CustomShaderPath));
        ImGui::EndDisabled();

        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Create HLSL Shader", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Shader name");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(360.0f);
            bool submit = ImGui::InputTextWithHint("##Name", "MyShader", m_Name, sizeof(m_Name), ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemEdited()) m_CreateError.clear();
            std::filesystem::path destination;
            std::string validationError;
            bool valid = GetNewShaderPath(assetRoot, m_Name, destination, validationError);
            ImGui::TextDisabled("Folder: shaders/   |   Extension: .hlsl");
            if (valid) ImGui::TextDisabled("File: %s", destination.filename().string().c_str());
            if (!valid) ImGui::TextWrapped("%s", validationError.c_str());
            if (!m_CreateError.empty()) ImGui::TextWrapped("%s", m_CreateError.c_str());
            ImGui::Spacing();
            ImGui::BeginDisabled(!valid);
            bool create = ImGui::Button("Create", ImVec2(110.0f, 0));
            ImGui::EndDisabled();
            if (valid && (create || submit)) {
                std::filesystem::path created;
                if (CreateShader(assetRoot, m_Name, created, m_CreateError) && AssignShader(material, assetRoot, created, m_CreateError)) {
                    m_Error.clear();
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110.0f, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (!m_Error.empty()) ImGui::TextWrapped("%s", m_Error.c_str());
        ImGui::Spacing();
        ImGui::PopID();
        return changed;
    }
}
