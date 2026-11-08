#include <Axle/PlatformUtils.hpp>
#include <Axle/Application.hpp>
#include <Axle/Path.hpp>
#include <cstring>
#include <iterator>

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

#ifdef _WIN32
    namespace {
        std::wstring WideFilter(const char* filter) {
            std::wstring result;
            for (const char* part = filter; part && *part; part += std::strlen(part) + 1) {
                result += PathFromUtf8(part).wstring(); result.push_back(L'\0');
            }
            result.push_back(L'\0'); return result;
        }
        std::string FileDialog(bool save, const char* filter, const char* defaultPath, const char* defaultExt) {
            OPENFILENAMEW request{};
            wchar_t selected[32768]{};
            const auto wideFilter = WideFilter(filter);
            const auto initial = defaultPath && *defaultPath ? PathFromUtf8(defaultPath) : std::filesystem::current_path();
            const auto directory = initial.wstring();
            const auto extension = defaultExt ? PathFromUtf8(defaultExt).wstring() : std::wstring{};
            request.lStructSize = sizeof(request);
            request.hwndOwner = glfwGetWin32Window(static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow()));
            request.lpstrFile = selected; request.nMaxFile = static_cast<DWORD>(std::size(selected));
            request.lpstrFilter = wideFilter.c_str(); request.nFilterIndex = 1;
            request.lpstrInitialDir = directory.c_str(); request.lpstrDefExt = extension.empty() ? nullptr : extension.c_str();
            request.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
            if ((save ? GetSaveFileNameW(&request) : GetOpenFileNameW(&request)) == TRUE) return PathToUtf8(std::filesystem::path(selected));
            return {};
        }
    }
#endif
    std::string FileDialogs::OpenFile(const char* filter, const char* defaultPath) {
#ifdef _WIN32
        return FileDialog(false, filter, defaultPath, nullptr);
#else
        return {};
#endif
    }
    std::string FileDialogs::SaveFile(const char* filter, const char* defaultPath, const char* defaultExt) {
#ifdef _WIN32
        return FileDialog(true, filter, defaultPath, defaultExt);
#else
        return {};
#endif
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
                    std::filesystem::path absPath = std::filesystem::absolute(PathFromUtf8(defaultPathStr), ec);
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
                            resultPath = PathToUtf8(std::filesystem::path(pszFilePath));
                            CoTaskMemFree(pszFilePath);
                        }
                        pItem->Release();
                    }
                }
                pFileOpen->Release();
            } else {
                // Fallback to classic SHBrowseForFolder on STA thread
                BROWSEINFOW bi = { 0 };
                bi.lpszTitle = L"Select Destination Folder";
                bi.ulFlags = BIF_RETURNONLYFSDIRS;
                bi.hwndOwner = NULL;
                LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
                if (pidl != 0) {
                    wchar_t path[MAX_PATH];
                    if (SHGetPathFromIDListW(pidl, path)) {
                        resultPath = PathToUtf8(std::filesystem::path(path));
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
