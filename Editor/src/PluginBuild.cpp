#include "PluginBuild.hpp"

#include <chrono>
#include <fstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Axle {

namespace {

static std::atomic<bool> s_BuildInProgress{false};

struct ScopedBuildLock {
    bool Locked = false;
    ScopedBuildLock() {
        Locked = !s_BuildInProgress.exchange(true);
    }
    ~ScopedBuildLock() {
        if (Locked) {
            s_BuildInProgress.store(false);
        }
    }
    bool IsLocked() const { return Locked; }
};

#ifdef _WIN32
struct Handle {
    HANDLE Value = nullptr;
    ~Handle() { if (Value && Value != INVALID_HANDLE_VALUE) CloseHandle(Value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit Handle(HANDLE value = nullptr) : Value(value) {}
};

std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#endif

bool IsItemUpToDate(const PluginBuildItem& item, const std::filesystem::path& binaryDir,
                    const std::string& config, const std::string& planFingerprint,
                    const std::string& planSdk) {
    std::error_code ec;
    const auto dllPath = binaryDir / (item.ModuleName + ".dll");
    const auto receiptPath = binaryDir / (item.ModuleName + ".build.receipt");

    if (!std::filesystem::is_regular_file(dllPath, ec) ||
        !std::filesystem::is_regular_file(receiptPath, ec)) {
        return false;
    }

    std::ifstream receipt(receiptPath);
    if (!receipt.is_open()) return false;

    std::string line;
    bool sawSchema = false;
    std::string recPluginId, recModule, recConfig, recFingerprint, recSdk;
    while (std::getline(receipt, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind("schema=", 0) == 0) {
            if (line.substr(7) == "1") sawSchema = true;
        } else if (line.rfind("plugin_id=", 0) == 0) {
            recPluginId = line.substr(10);
        } else if (line.rfind("module_name=", 0) == 0) {
            recModule = line.substr(12);
        } else if (line.rfind("configuration=", 0) == 0) {
            recConfig = line.substr(14);
        } else if (line.rfind("fingerprint=", 0) == 0) {
            recFingerprint = line.substr(12);
        } else if (line.rfind("sdk_fingerprint=", 0) == 0) {
            recSdk = line.substr(16);
        }
    }

    if (!sawSchema) return false;
    if (!item.PluginId.empty() && recPluginId != item.PluginId) return false;
    if (!item.ModuleName.empty() && recModule != item.ModuleName) return false;
    if (!config.empty() && !recConfig.empty() && recConfig != config) return false;

    const std::string expectedFingerprint = !item.Fingerprint.empty() ? item.Fingerprint : planFingerprint;
    if (!expectedFingerprint.empty() && recFingerprint != expectedFingerprint) return false;

    const std::string expectedSdk = !item.SdkFingerprint.empty() ? item.SdkFingerprint : planSdk;
    if (!expectedSdk.empty() && recSdk != expectedSdk) return false;

    return true;
}

} // namespace

bool IsPluginBuildInProgress() {
    return s_BuildInProgress.load();
}

PluginBuildResult BuildPlugins(const PluginBuildRequest& request, const std::atomic_bool& cancel) {
    PluginBuildResult result;

    // 1. Simultaneous writer rejection (build lock)
    ScopedBuildLock lock;
    if (!lock.IsLocked()) {
        result.Error = "Simultaneous plugin build rejected: another build is in progress.";
        return result;
    }

    // 2. Cancellation check
    if (cancel.load()) {
        result.Error = "Plugin build cancelled.";
        return result;
    }

    // 3. Resolve items to build
    std::vector<PluginBuildItem> items = request.Items;
    if (items.empty() && request.Plan.IsValid()) {
        for (const auto& artifact : request.Plan.Artifacts) {
            PluginBuildItem item;
            item.PluginId = artifact.PluginId;
            item.ModuleName = artifact.ModuleName;
            item.Kind = artifact.Kind;
            item.SourceRoot = artifact.SourceRoot;
            item.Fingerprint = request.Plan.Fingerprint;
            item.SdkFingerprint = request.Plan.SdkFingerprint;
            items.push_back(std::move(item));
        }
    }

    if (items.empty()) {
        result.Success = true;
        result.UpToDate = true;
        result.ExitCode = 0;
        return result;
    }

    // 4. Prebuilt no-op check
    bool allUpToDate = true;
    for (const auto& item : items) {
        if (!IsItemUpToDate(item, request.BinaryDirectory, request.Configuration,
                            request.Plan.Fingerprint, request.Plan.SdkFingerprint)) {
            allUpToDate = false;
            break;
        }
    }

    if (allUpToDate) {
        result.Success = true;
        result.UpToDate = true;
        result.ExitCode = 0;
        return result;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(request.CMake, ec) ||
        !std::filesystem::is_directory(request.BuildDirectory, ec) ||
        request.BinaryDirectory.empty()) {
        result.Error = "Plugin build tools or configured build directory are unavailable.";
        return result;
    }

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    result.LogPath = request.BuildDirectory / ("plugin-build-" + std::to_string(stamp) + ".log");

#ifdef _WIN32
    SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    Handle output(CreateFileW(result.LogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (output.Value == INVALID_HANDLE_VALUE || input.Value == INVALID_HANDLE_VALUE || !job.Value) {
        result.Error = "Could not create plugin build log/process handles.";
        return result;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.Value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        result.Error = "Could not configure plugin build process ownership.";
        return result;
    }

    auto command = Quote(request.CMake.wstring()) + L" --build " + Quote(request.BuildDirectory.wstring());
    if (!request.Configuration.empty()) {
        command += L" --config " + Quote(std::filesystem::path(request.Configuration).wstring());
    }
    if (!request.TargetName.empty()) {
        command += L" --target " + Quote(std::filesystem::path(request.TargetName).wstring());
    } else {
        for (const auto& item : items) {
            command += L" --target " + Quote(std::filesystem::path(item.ModuleName).wstring());
        }
    }

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = output.Value;
    startup.hStdInput = input.Value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(request.CMake.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, request.BuildDirectory.c_str(),
                        &startup, &process)) {
        result.Error = "Could not start CMake plugin build (Windows error " + std::to_string(GetLastError()) + ").";
        return result;
    }
    Handle processHandle(process.hProcess), thread(process.hThread);
    if (!AssignProcessToJobObject(job.Value, processHandle.Value)) {
        TerminateProcess(processHandle.Value, 1);
        result.Error = "Could not attach plugin build to its process owner.";
        return result;
    }
    if (ResumeThread(thread.Value) == static_cast<DWORD>(-1)) {
        result.Error = "Could not resume plugin build.";
        return result;
    }

    for (;;) {
        const DWORD wait = WaitForSingleObject(processHandle.Value, 100);
        if (cancel.load()) {
            TerminateJobObject(job.Value, 1);
            WaitForSingleObject(processHandle.Value, 5000);
            result.Error = "Plugin build cancelled.";
            return result;
        }
        if (wait == WAIT_OBJECT_0) break;
        if (wait != WAIT_TIMEOUT) {
            result.Error = "Plugin build process wait failed.";
            return result;
        }
    }

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(processHandle.Value, &exitCode)) {
        result.Error = "Could not read plugin build exit code.";
        return result;
    }
    result.ExitCode = static_cast<int>(exitCode);
    if (exitCode != 0) {
        result.Error = "Plugin build failed (exit " + std::to_string(exitCode) + "). See " + result.LogPath.string();
        return result;
    }

    // Verify DLL output and atomically write build receipts
    for (const auto& item : items) {
        const auto dllPath = request.BinaryDirectory / (item.ModuleName + ".dll");
        if (!std::filesystem::is_regular_file(dllPath, ec)) {
            result.Error = "Plugin build finished without expected module: " + item.ModuleName;
            return result;
        }

        const auto receiptPath = request.BinaryDirectory / (item.ModuleName + ".build.receipt");
        const auto receiptTemp = receiptPath.string() + ".tmp";
        {
            std::ofstream receipt(receiptTemp, std::ios::trunc);
            if (!receipt) {
                result.Error = "Plugin build succeeded but receipt could not be written for: " + item.ModuleName;
                return result;
            }
            receipt << "schema=1\n"
                    << "plugin_id=" << item.PluginId << "\n"
                    << "module_name=" << item.ModuleName << "\n"
                    << "kind=" << Plugin::ToString(item.Kind) << "\n"
                    << "configuration=" << request.Configuration << "\n"
                    << "product=" << Plugin::ToString(request.Plan.Product) << "\n"
                    << "sdk_fingerprint=" << (!item.SdkFingerprint.empty() ? item.SdkFingerprint : request.Plan.SdkFingerprint) << "\n"
                    << "fingerprint=" << (!item.Fingerprint.empty() ? item.Fingerprint : request.Plan.Fingerprint) << "\n"
                    << "source_root=" << item.SourceRoot.generic_string() << "\n";
            receipt.flush();
            if (!receipt) {
                std::error_code removeEc;
                std::filesystem::remove(receiptTemp, removeEc);
                result.Error = "Plugin build succeeded but receipt could not be completed for: " + item.ModuleName;
                return result;
            }
        }

        std::error_code renameEc;
        if (!MoveFileExW(std::filesystem::path(receiptTemp).wstring().c_str(),
                         receiptPath.wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            renameEc = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        }

        if (renameEc) {
            std::error_code removeEc;
            std::filesystem::remove(receiptTemp, removeEc);
            result.Error = "Plugin build succeeded but receipt could not be published for: " + item.ModuleName;
            return result;
        }
        result.PublishedReceipts.push_back(receiptPath);
    }

    result.Success = true;
#else
    result.Error = "Plugin building is currently supported only on Windows.";
#endif
    return result;
}

} // namespace Axle
