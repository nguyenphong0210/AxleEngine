#include <Axle/PlatformUtils.hpp>
#include <Axle/Application.hpp>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

namespace Axle {

    std::string FileDialogs::OpenFile(const char* filter, const char* defaultPath) {
#ifdef _WIN32
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        CHAR currentDir[260] = { 0 };

        ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
        ofn.lStructSize = sizeof(OPENFILENAMEA);
        ofn.hwndOwner = glfwGetWin32Window(Application::Get().GetWindow().GetNativeWindow());
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        if (defaultPath && *defaultPath) {
            ofn.lpstrInitialDir = defaultPath;
        } else if (GetCurrentDirectoryA(260, currentDir)) {
            ofn.lpstrInitialDir = currentDir;
        }

        if (GetOpenFileNameA(&ofn) == TRUE) {
            return ofn.lpstrFile;
        }
#endif
        return "";
    }

    std::string FileDialogs::SaveFile(const char* filter, const char* defaultPath, const char* defaultExt) {
#ifdef _WIN32
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        CHAR currentDir[260] = { 0 };

        ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
        ofn.lStructSize = sizeof(OPENFILENAMEA);
        ofn.hwndOwner = glfwGetWin32Window(Application::Get().GetWindow().GetNativeWindow());
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.lpstrDefExt = defaultExt;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

        if (defaultPath && *defaultPath) {
            ofn.lpstrInitialDir = defaultPath;
        } else if (GetCurrentDirectoryA(260, currentDir)) {
            ofn.lpstrInitialDir = currentDir;
        }

        if (GetSaveFileNameA(&ofn) == TRUE) {
            return ofn.lpstrFile;
        }
#endif
        return "";
    }

    std::string FileDialogs::OpenFolder(const char* defaultPath) {
#ifdef _WIN32
        BROWSEINFOA bi = { 0 };
        bi.lpszTitle = "Select Destination Folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        bi.hwndOwner = glfwGetWin32Window(Application::Get().GetWindow().GetNativeWindow());
        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl != 0) {
            CHAR path[MAX_PATH];
            if (SHGetPathFromIDListA(pidl, path)) {
                CoTaskMemFree(pidl);
                return path;
            }
            CoTaskMemFree(pidl);
        }
#endif
        return "";
    }

}
