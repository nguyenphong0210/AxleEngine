#include <Axle/PlatformUtils.hpp>
#include <Axle/Application.hpp>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <filesystem>
#include <thread>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace Axle {

    std::string FileDialogs::OpenFile(const char* filter, const char* defaultPath) {
#ifdef _WIN32
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        CHAR currentDir[260] = { 0 };

        ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
        ofn.lStructSize = sizeof(OPENFILENAMEA);
        ofn.hwndOwner = glfwGetWin32Window(static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow()));
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
        ofn.hwndOwner = glfwGetWin32Window(static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow()));
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
        std::string defaultPathStr = (defaultPath && *defaultPath) ? defaultPath : "";
        std::string resultPath;

        // Run dialog on a dedicated STA thread.
        // This is critical because miniaudio/WASAPI initializes COM as MTA on the main thread,
        // and Windows Shell IFileOpenDialog strictly requires an STA thread with an independent message pump.
        // Calling IFileOpenDialog::Show() on an MTA thread causes an immediate permanent freeze/hang.
        std::thread dialogThread([&]() {
            HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (FAILED(hrCo)) {
                return;
            }

            IFileOpenDialog* pFileOpen = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
            if (SUCCEEDED(hr) && pFileOpen) {
                DWORD dwOptions;
                if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
                    pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
                }

                pFileOpen->SetTitle(L"Select Export Destination Directory");

                if (!defaultPathStr.empty()) {
                    std::error_code ec;
                    std::filesystem::path absPath = std::filesystem::absolute(defaultPathStr, ec);
                    if (!ec && std::filesystem::exists(absPath, ec)) {
                        IShellItem* pDefaultFolder = nullptr;
                        std::wstring wp = absPath.wstring();
                        if (SUCCEEDED(SHCreateItemFromParsingName(wp.c_str(), NULL, IID_PPV_ARGS(&pDefaultFolder)))) {
                            pFileOpen->SetFolder(pDefaultFolder);
                            pDefaultFolder->Release();
                        }
                    }
                }

                // Show with NULL to avoid cross-thread HWND deadlocks
                if (SUCCEEDED(pFileOpen->Show(NULL))) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pFileOpen->GetResult(&pItem)) && pItem) {
                        PWSTR pszFilePath = nullptr;
                        if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)) && pszFilePath) {
                            char pathBuf[MAX_PATH * 2] = { 0 };
                            WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, pathBuf, sizeof(pathBuf), NULL, NULL);
                            resultPath = pathBuf;
                            CoTaskMemFree(pszFilePath);
                        }
                        pItem->Release();
                    }
                }
                pFileOpen->Release();
            } else {
                // Fallback to classic SHBrowseForFolder on STA thread
                BROWSEINFOA bi = { 0 };
                bi.lpszTitle = "Select Destination Folder";
                bi.ulFlags = BIF_RETURNONLYFSDIRS;
                bi.hwndOwner = NULL;
                LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
                if (pidl != 0) {
                    CHAR path[MAX_PATH];
                    if (SHGetPathFromIDListA(pidl, path)) {
                        resultPath = path;
                    }
                    CoTaskMemFree(pidl);
                }
            }

            CoUninitialize();
        });

        if (dialogThread.joinable()) {
            dialogThread.join();
        }

        return resultPath;
#else
        return "";
#endif
    }

}
