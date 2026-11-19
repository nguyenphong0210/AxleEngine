#include "EditorPreferences.hpp"

#include <Axle/FileSystem.hpp>
#include <Axle/Window.hpp>
#include <imgui.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace Axle {
    namespace {
        std::filesystem::path PreferencesPath() {
#ifdef _WIN32
            if (const auto* directory = _wgetenv(L"LOCALAPPDATA"); directory && *directory)
                return std::filesystem::path(directory) / "AxleEngine" / "EditorPreferences.ini";
#else
            if (const auto* directory = std::getenv("XDG_CONFIG_HOME"); directory && *directory)
                return std::filesystem::path(directory) / "AxleEngine" / "EditorPreferences.ini";
            if (const auto* directory = std::getenv("HOME"); directory && *directory)
                return std::filesystem::path(directory) / ".config" / "AxleEngine" / "EditorPreferences.ini";
#endif
            return {};
        }
    }

    bool EditorPreferences::LoadVSync() {
        const auto path = PreferencesPath();
        if (path.empty()) return true;

        std::ifstream file(path);
        std::string line;
        while (std::getline(file, line)) {
            if (line == "VSync=false") return false;
            if (line == "VSync=true") return true;
        }
        return true;
    }

    void EditorPreferences::Draw(Window& window) {
        if (!m_Open) return;

        ImGui::SetNextWindowSize(ImVec2(400.0f, 150.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Editor Preferences", &m_Open, ImGuiWindowFlags_NoCollapse)) {
            ImGui::SeparatorText("Performance");
            bool vsync = window.IsVSync();
            if (ImGui::Checkbox("Enable VSync", &vsync)) {
                window.SetVSync(vsync);
                FileSystem::WriteTextFileAtomically(PreferencesPath(), vsync ? "VSync=true\n" : "VSync=false\n", &m_SaveError);
            }
            if (!m_SaveError.empty())
                ImGui::TextWrapped("VSync changed for this session. Saving the preference failed. %s", m_SaveError.c_str());
        }
        ImGui::End();
    }
}
