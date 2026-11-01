#include <Axle/HLSLStudioPanel.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <imgui.h>
#include <fstream>
#include <sstream>
#include <chrono>

namespace Axle {

    HLSLStudioPanel::HLSLStudioPanel() {
        m_SourceCode = GetDefaultPBRTemplate("CustomForwardPBR");
        m_SavedSourceCode = m_SourceCode;
        m_CurrentFilePath = "assets/shaders/CustomForwardPBR.hlsl";
    }

    HLSLStudioPanel::~HLSLStudioPanel() = default;

    void HLSLStudioPanel::OpenFile(const std::filesystem::path& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            // Try resolving via FileSystem if relative
            std::filesystem::path resolved = FileSystem::ResolveAssetPath(filepath.string());
            file.open(resolved);
            if (file.is_open()) {
                m_CurrentFilePath = resolved;
            } else {
                AXLE_ERROR("HLSLStudioPanel: Failed to open HLSL file '{}'", filepath.string());
                return;
            }
        } else {
            m_CurrentFilePath = filepath;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        m_SourceCode = buffer.str();
        m_SavedSourceCode = m_SourceCode;
        m_IsDirty = false;
        m_HasCompiled = false;
        m_CompileMessage.clear();
        m_WarningMessage.clear();

        AXLE_INFO("HLSLStudioPanel: Successfully loaded shader '{}'", m_CurrentFilePath.string());
    }

    bool HLSLStudioPanel::SaveFile() {
        if (m_CurrentFilePath.empty()) {
            m_CurrentFilePath = "assets/shaders/CustomForwardPBR.hlsl";
        }

        std::filesystem::create_directories(m_CurrentFilePath.parent_path());
        std::ofstream file(m_CurrentFilePath, std::ios::trunc);
        if (!file.is_open()) {
            AXLE_ERROR("HLSLStudioPanel: Failed to save file to '{}'", m_CurrentFilePath.string());
            return false;
        }

        file << m_SourceCode;
        file.close();

        m_SavedSourceCode = m_SourceCode;
        m_IsDirty = false;
        AXLE_INFO("HLSLStudioPanel: Saved '{}' ({} bytes)", m_CurrentFilePath.string(), m_SourceCode.size());
        return true;
    }

    bool HLSLStudioPanel::CompileShader() {
        auto& compiler = RHI::RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            compiler.Init();
        }

        auto startTime = std::chrono::high_resolution_clock::now();
        m_HasCompiled = true;
        m_CompiledBytecodeSize = 0;
        m_WarningMessage.clear();
        m_CompileMessage.clear();

        std::vector<std::string> includeDirs = {
            "Engine/Resources/Shaders",
            "assets/shaders"
        };
        if (!m_CurrentFilePath.empty()) {
            includeDirs.push_back(m_CurrentFilePath.parent_path().string());
        }

        bool overallSuccess = true;
        std::string accumulatedErrors;
        std::string accumulatedWarnings;

        auto compileStage = [&](RHI::RHIShaderStage stage, const std::string& entryPoint, const std::string& stageName) -> bool {
            RHI::RHIShaderCompileDesc desc;
            desc.Source = m_SourceCode;
            desc.SourcePath = m_CurrentFilePath.empty() ? "ShaderStudio.hlsl" : m_CurrentFilePath.string();
            desc.EntryPoint = entryPoint;
            desc.Stage = stage;
            desc.ShaderModel = "6_0";
            desc.HLSLVersion = "2021";
            desc.Optimization = true;
            desc.IncludeDirs = includeDirs;

            RHI::RHIShaderCompileResult res = compiler.CompileForActiveBackend(desc);
            if (!res.WarningMessage.empty()) {
                accumulatedWarnings += "[" + stageName + " Warnings]\n" + res.WarningMessage + "\n";
            }

            if (!res.Success) {
                accumulatedErrors += "[" + stageName + " Error (" + entryPoint + ")]:\n" + res.ErrorMessage + "\n";
                return false;
            }

            m_CompiledBytecodeSize += res.Bytecode.GetSize();
            return true;
        };

        if (m_TargetStage == HLSLTargetStage::ForwardPipeline) {
            bool vsOk = compileStage(RHI::RHIShaderStage::Vertex, m_VSEntryPoint, "Vertex Stage");
            bool psOk = compileStage(RHI::RHIShaderStage::Fragment, m_PSEntryPoint, "Pixel Stage");
            overallSuccess = vsOk && psOk;
        } else if (m_TargetStage == HLSLTargetStage::VertexOnly) {
            overallSuccess = compileStage(RHI::RHIShaderStage::Vertex, m_VSEntryPoint, "Vertex Stage");
        } else if (m_TargetStage == HLSLTargetStage::PixelOnly) {
            overallSuccess = compileStage(RHI::RHIShaderStage::Fragment, m_PSEntryPoint, "Pixel Stage");
        } else if (m_TargetStage == HLSLTargetStage::ComputeOnly) {
            overallSuccess = compileStage(RHI::RHIShaderStage::Compute, m_CSEntryPoint, "Compute Stage");
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        m_LastCompileTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
        m_LastCompileSuccess = overallSuccess;

        if (overallSuccess) {
            std::ostringstream oss;
            oss << "Direct3D 12 (DXIL) & Vulkan (SPIR-V) target compilation succeeded in "
                << static_cast<int>(m_LastCompileTimeMs) << " ms.\n"
                << "Total Generated Bytecode: " << m_CompiledBytecodeSize << " bytes across active stages.\n"
                << "Shader Model 6.0 | HLSL 2021 Compliant.";
            m_CompileMessage = oss.str();
            m_WarningMessage = accumulatedWarnings;
            AXLE_INFO("HLSLStudioPanel: Live compilation passed for '{}' in {:.1f} ms",
                      m_CurrentFilePath.filename().string(), m_LastCompileTimeMs);
        } else {
            m_CompileMessage = accumulatedErrors;
            m_WarningMessage = accumulatedWarnings;
            AXLE_ERROR("HLSLStudioPanel: Compilation failed for '{}':\n{}",
                       m_CurrentFilePath.filename().string(), m_CompileMessage);
        }

        return overallSuccess;
    }

    void HLSLStudioPanel::SetSourceCode(const std::string& code) {
        m_SourceCode = code;
        m_IsDirty = (m_SourceCode != m_SavedSourceCode);
    }

    void HLSLStudioPanel::CreateNewShaderTemplate(const std::string& shaderName) {
        m_SourceCode = GetDefaultPBRTemplate(shaderName);
        m_SavedSourceCode.clear();
        m_CurrentFilePath = "assets/shaders/" + shaderName + ".hlsl";
        m_IsDirty = true;
        m_HasCompiled = false;
        m_CompileMessage.clear();
        m_WarningMessage.clear();
    }

    std::string HLSLStudioPanel::GetDefaultPBRTemplate(const std::string& shaderName) {
        std::ostringstream oss;
        oss << "// =============================================================================\n"
            << "// " << shaderName << ".hlsl - AxleEngine Standard PBR Forward Shader\n"
            << "// Direct3D 12 (DXIL) & Vulkan 1.3 (SPIR-V) Standard HLSL 2021 Codebase\n"
            << "// =============================================================================\n\n"
            << "// Per-Object Constants (Register b0, Space 0)\n"
            << "cbuffer ObjectConstants : register(b0, space0) {\n"
            << "    float4x4 u_Model;\n"
            << "    float4   u_BaseColor;\n"
            << "    float4   u_Params; // x: Roughness, y: Metallic, z: AO, w: Tiling\n"
            << "};\n\n"
            << "// Global Scene Constants (Register b1, Space 0)\n"
            << "cbuffer SceneConstants : register(b1, space0) {\n"
            << "    float4x4 u_ViewProjection;\n"
            << "    float4   u_CameraPosition;\n"
            << "    float4   u_SunDirection;\n"
            << "    float4   u_SunColor;\n"
            << "};\n\n"
            << "// Texture & Sampler Bindings\n"
            << "Texture2D    u_BaseMap : register(t0, space0);\n"
            << "SamplerState u_Sampler : register(s0, space0);\n\n"
            << "// Vertex Input & Pixel Input Structs\n"
            << "struct VSInput {\n"
            << "    float3 Position : POSITION;\n"
            << "    float3 Normal   : NORMAL;\n"
            << "    float2 TexCoord : TEXCOORD0;\n"
            << "};\n\n"
            << "struct PSInput {\n"
            << "    float4 Position : SV_Position;\n"
            << "    float3 WorldPos : POSITION0;\n"
            << "    float3 Normal   : NORMAL0;\n"
            << "    float2 TexCoord : TEXCOORD0;\n"
            << "};\n\n"
            << "// -----------------------------------------------------------------------------\n"
            << "// Vertex Shader Entry Point\n"
            << "// -----------------------------------------------------------------------------\n"
            << "PSInput VSMain(VSInput input) {\n"
            << "    PSInput output;\n"
            << "    float4 worldPos = mul(u_Model, float4(input.Position, 1.0));\n"
            << "    output.Position = mul(u_ViewProjection, worldPos);\n"
            << "    output.WorldPos = worldPos.xyz;\n"
            << "    output.Normal   = normalize(mul((float3x3)u_Model, input.Normal));\n"
            << "    output.TexCoord = input.TexCoord * u_Params.w;\n"
            << "    return output;\n"
            << "}\n\n"
            << "// -----------------------------------------------------------------------------\n"
            << "// Pixel Shader Entry Point\n"
            << "// -----------------------------------------------------------------------------\n"
            << "float4 PSMain(PSInput input) : SV_Target0 {\n"
            << "    float4 texColor = u_BaseMap.Sample(u_Sampler, input.TexCoord);\n"
            << "    float3 albedo   = u_BaseColor.rgb * texColor.rgb;\n\n"
            << "    // Lambertian Diffuse + Directional Sun Lighting\n"
            << "    float3 N = normalize(input.Normal);\n"
            << "    float3 L = normalize(-u_SunDirection.xyz);\n"
            << "    float NdotL = max(dot(N, L), 0.0);\n\n"
            << "    float3 diffuse  = albedo * u_SunColor.rgb * NdotL;\n"
            << "    float3 ambient  = albedo * 0.15;\n"
            << "    float3 finalRgb = diffuse + ambient;\n\n"
            << "    return float4(finalRgb, u_BaseColor.a * texColor.a);\n"
            << "}\n";
        return oss.str();
    }

    void HLSLStudioPanel::OnImGuiRender(bool* p_open) {
        if (!m_IsOpen && (!p_open || !*p_open)) {
            return;
        }

        std::string title = "HLSL Shader Studio: " +
            (m_CurrentFilePath.empty() ? "Untitled" : m_CurrentFilePath.filename().string()) +
            (m_IsDirty ? " *###HLSLStudioPanel" : "###HLSLStudioPanel");

        if (m_RequestFocus) {
            ImGui::SetNextWindowFocus();
            m_RequestFocus = false;
        }

        ImGui::SetNextWindowSize(ImVec2(750, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title.c_str(), p_open ? p_open : &m_IsOpen, ImGuiWindowFlags_MenuBar)) {
            ImGui::End();
            return;
        }

        // Global Window Keyboard Shortcuts
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            bool ctrlPressed = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
            if (ctrlPressed && ImGui::IsKeyPressed(ImGuiKey_S)) {
                SaveFile();
                CompileShader();
            } else if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
                SaveFile();
                CompileShader();
            }
        }

        DrawToolbar();
        DrawEditorArea();
        DrawDiagnosticsPanel();
        DrawStatusBar();

        ImGui::End();
    }

    void HLSLStudioPanel::DrawToolbar() {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New PBR Template", "Ctrl+N")) {
                    CreateNewShaderTemplate("CustomShader");
                }
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    SaveFile();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Recompile Shader", "F5")) {
                    SaveFile();
                    CompileShader();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Action Toolbar Buttons
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));

        if (ImGui::Button(" Save (Ctrl+S) ")) {
            SaveFile();
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.58f, 0.36f, 1.0f));
        if (ImGui::Button(" Recompile (F5) ")) {
            SaveFile();
            CompileShader();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();

        // Target Pipeline Stage Combo
        ImGui::SetNextItemWidth(170.0f);
        const char* stageOptions[] = { "Forward (VS + PS)", "Vertex Only (VS)", "Pixel Only (PS)", "Compute Only (CS)" };
        int currentStageIdx = static_cast<int>(m_TargetStage);
        if (ImGui::Combo("##TargetStage", &currentStageIdx, stageOptions, IM_ARRAYSIZE(stageOptions))) {
            m_TargetStage = static_cast<HLSLTargetStage>(currentStageIdx);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Select target compilation stage: dual forward pipeline or isolated stage");
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputTextWithHint("##VSMain", "VS Entry", m_VSEntryPoint, sizeof(m_VSEntryPoint));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vertex Shader Entry Point");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputTextWithHint("##PSMain", "PS Entry", m_PSEntryPoint, sizeof(m_PSEntryPoint));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pixel Shader Entry Point");

        ImGui::PopStyleVar();
        ImGui::Separator();
    }

    void HLSLStudioPanel::DrawEditorArea() {
        float availableHeight = ImGui::GetContentRegionAvail().y;
        float editorHeight = availableHeight - m_DiagnosticsHeight - 28.0f;
        if (editorHeight < 100.0f) editorHeight = 100.0f;

        // Calculate line count
        int lineCount = 1;
        for (char c : m_SourceCode) {
            if (c == '\n') lineCount++;
        }

        char maxLineStr[32];
        snprintf(maxLineStr, sizeof(maxLineStr), "%d", lineCount);
        float gutterWidth = ImGui::CalcTextSize(maxLineStr).x + 16.0f;
        if (gutterWidth < 38.0f) gutterWidth = 38.0f;

        // Line Numbers Gutter
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.11f, 1.0f));
        ImGui::BeginChild("LineGutter", ImVec2(gutterWidth, editorHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetScrollY(m_ScrollY);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.44f, 0.48f, 1.0f));
        std::string gutterText;
        gutterText.reserve(lineCount * 6);
        for (int i = 1; i <= lineCount; ++i) {
            gutterText += std::to_string(i) + "\n";
        }
        ImGui::TextUnformatted(gutterText.c_str());
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::SameLine();

        // Main Code Editor Area
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.13f, 0.14f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 6.0f));

        struct TextEditCallbackData {
            std::string* Str;
            bool* IsDirty;
        };
        TextEditCallbackData cbData{ &m_SourceCode, &m_IsDirty };

        if (m_SourceCode.capacity() < m_SourceCode.size() + 512) {
            m_SourceCode.reserve(m_SourceCode.size() + 2048);
        }

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize;
        if (ImGui::InputTextMultiline("##HLSLSourceInput",
                                      m_SourceCode.data(),
                                      m_SourceCode.capacity() + 1,
                                      ImVec2(-1.0f, editorHeight),
                                      flags,
                                      [](ImGuiInputTextCallbackData* data) -> int {
                                          auto* cb = static_cast<TextEditCallbackData*>(data->UserData);
                                          if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                                              cb->Str->resize(data->BufTextLen);
                                              data->Buf = cb->Str->data();
                                          }
                                          *cb->IsDirty = true;
                                          return 0;
                                      }, &cbData))
        {
            m_IsDirty = (m_SourceCode != m_SavedSourceCode);
        }

        m_ScrollY = ImGui::GetScrollY();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void HLSLStudioPanel::DrawDiagnosticsPanel() {
        ImGui::Separator();

        // Header with status indicator badge
        ImGui::TextUnformatted("DXC Compiler Diagnostics:");
        ImGui::SameLine();
        if (!m_HasCompiled) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[READY]");
        } else if (m_LastCompileSuccess) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "[PASS - 0 ERRORS]");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[FAIL - COMPILATION ERROR]");
        }

        // Diagnostics Log Window
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.09f, 1.0f));
        ImGui::BeginChild("DiagnosticsLog", ImVec2(-1.0f, m_DiagnosticsHeight - 25.0f), true);

        if (!m_CompileMessage.empty()) {
            if (m_LastCompileSuccess) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s", m_CompileMessage.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_CompileMessage.c_str());
            }
        }
        if (!m_WarningMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", m_WarningMessage.c_str());
        }
        if (m_CompileMessage.empty() && m_WarningMessage.empty()) {
            ImGui::TextDisabled("Press 'Recompile (F5)' or 'Ctrl + S' to invoke DirectX Shader Compiler (DXC).");
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void HLSLStudioPanel::DrawStatusBar() {
        int lineCount = 1;
        for (char c : m_SourceCode) {
            if (c == '\n') lineCount++;
        }

        ImGui::TextDisabled("Lines: %d  |  Size: %zu bytes  |  Target: SM 6.0 (DXIL / SPIR-V)  |  Encoding: UTF-8",
                            lineCount, m_SourceCode.size());
        ImGui::SameLine();

        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > 100.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - 90.0f);
            if (m_IsDirty) {
                ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1.0f), "* Modified");
            } else {
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "Saved");
            }
        }
    }

} // namespace Axle

