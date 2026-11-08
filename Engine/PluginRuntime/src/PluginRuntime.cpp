#include <Axle/PluginRuntime.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace Axle::PluginRuntime {

struct RegistryEntry {
    RegistrationRegistry::Owner owner = 0;
    std::string id;
    void* resource = nullptr;
    void (*destroy)(void*) = nullptr;
};

struct RegistrationRegistry::State {
    std::atomic<std::uint64_t> nextOwner{1};
    std::vector<std::unique_ptr<RegistryEntry>> entries;
    bool alive = true;

    ~State() {
        DestroyAll();
    }

    void DestroyAll() noexcept {
        alive = false;
        std::vector<std::unique_ptr<RegistryEntry>> toDestroy;
        toDestroy.swap(entries);
        for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
            if ((*it)->destroy) {
                try {
                    (*it)->destroy((*it)->resource);
                } catch (...) {}
            }
        }
    }

    Owner BeginOwner() noexcept {
        return nextOwner.fetch_add(1);
    }

    bool Register(Owner owner, const char* resource_id, void* resource,
                  void (*destroy)(void*)) {
        if (!alive || owner == 0 || !resource_id || !*resource_id) return false;
        if (std::any_of(entries.begin(), entries.end(), [resource_id](const auto& entry) {
                return entry->id == resource_id;
            })) return false;
        entries.push_back(std::make_unique<RegistryEntry>(RegistryEntry{owner, resource_id, resource, destroy}));
        return true;
    }

    void Rollback(Owner owner) noexcept {
        if (owner == 0) return;
        for (auto it = entries.begin(); it != entries.end();) {
            if ((*it)->owner != owner) { ++it; continue; }
            if ((*it)->destroy) {
                try {
                    (*it)->destroy((*it)->resource);
                } catch (...) {}
            }
            it = entries.erase(it);
        }
    }

    bool Has(const std::string& resource_id) const {
        return std::any_of(entries.begin(), entries.end(), [&resource_id](const auto& entry) {
            return entry->id == resource_id;
        });
    }

    std::size_t Size() const noexcept { return entries.size(); }
};

RegistrationRegistry::RegistrationRegistry()
    : m_State(std::make_shared<State>()) {}

RegistrationRegistry::~RegistrationRegistry() {
    if (m_State) {
        m_State->DestroyAll();
    }
}

RegistrationRegistry::Owner RegistrationRegistry::BeginOwner() {
    return m_State ? m_State->BeginOwner() : 0;
}

bool RegistrationRegistry::Register(Owner owner, const char* resource_id, void* resource,
                                    void (*destroy)(void*)) {
    return m_State && m_State->Register(owner, resource_id, resource, destroy);
}

void RegistrationRegistry::Rollback(Owner owner) noexcept {
    if (m_State) m_State->Rollback(owner);
}

bool RegistrationRegistry::Has(const std::string& resource_id) const {
    return m_State && m_State->Has(resource_id);
}

std::size_t RegistrationRegistry::Size() const noexcept {
    return m_State ? m_State->Size() : 0;
}

namespace {
void* OpenLibrary(const std::filesystem::path& path) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void CloseLibrary(void* handle) noexcept {
    if (!handle) return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

void* FindSymbol(void* handle, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

std::string LastError() {
#if defined(_WIN32)
    DWORD err = GetLastError();
    if (err == ERROR_BAD_EXE_FORMAT) {
        return "native library binary architecture mismatch (ERROR_BAD_EXE_FORMAT)";
    }
    LPSTR buffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buffer, 0, NULL);
    std::string message(buffer, size);
    LocalFree(buffer);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
        message.pop_back();
    }
    return message.empty() ? "native library load or symbol lookup failed" : message;
#else
    const char* error = dlerror();
    return error ? error : "native library load or symbol lookup failed";
#endif
}

} // namespace

NativePlugin::~NativePlugin() { Stop(); }

bool NativePlugin::Load(const LoadRequest& request, RegistrationRegistry& registry, std::string& diagnostic) {
    const auto& module_path = request.ModulePath;
    const auto& expected_id = request.Expected.ModuleName;
    const auto& expected_config = request.Expected.Configuration;
    diagnostic.clear();
    if (m_Handle || m_Active) { diagnostic = "plugin is already loaded"; return false; }
    if (module_path.empty() || !module_path.is_absolute()) {
        diagnostic = "plugin path must be absolute and controlled by the resolver"; return false;
    }

    if (request.Expected.Product == Plugin::ProductKind::Runtime) {
        diagnostic = "shipping Runtime native DLL loading is unsupported"; return false;
    }
    if (!Plugin::ValidateArtifact(request.Expected, module_path, request.ReceiptPath, diagnostic)) return false;

    m_Handle = OpenLibrary(module_path);
    if (!m_Handle) { diagnostic = LastError(); return false; }
    auto fail = [&](const char* reason) {
        diagnostic = reason;
        CloseLibrary(m_Handle); m_Handle = nullptr; return false;
    };
    auto get_descriptor = reinterpret_cast<GetDescriptorFn>(FindSymbol(m_Handle, "ax_plugin_get_descriptor"));
    auto start = reinterpret_cast<StartFn>(FindSymbol(m_Handle, "ax_plugin_start"));
    m_Stop = reinterpret_cast<StopFn>(FindSymbol(m_Handle, "ax_plugin_stop"));
    if (!get_descriptor || !start || !m_Stop) return fail("required plugin entrypoint is missing");
    const PluginDescriptor* descriptor = nullptr;
    try {
        descriptor = get_descriptor();
    } catch (...) {
        return fail("plugin get_descriptor threw an unhandled exception across C ABI");
    }
    if (!descriptor || descriptor->struct_size < sizeof(PluginDescriptor) || !descriptor->module_id)
        return fail("invalid plugin descriptor");
    if (descriptor->abi_version != kPluginAbiVersion) return fail("plugin ABI mismatch");
    if (expected_id != descriptor->module_id) return fail("plugin module ID mismatch");
    if (!descriptor->plugin_id || request.Expected.PluginId != descriptor->plugin_id) return fail("plugin ID mismatch");
    if (!descriptor->sdk_fingerprint || request.Expected.SdkFingerprint != descriptor->sdk_fingerprint) return fail("plugin SDK mismatch");
    if (!descriptor->toolchain_fingerprint || request.Expected.ToolchainFingerprint != descriptor->toolchain_fingerprint) return fail("plugin toolchain mismatch");
    if (!expected_config.empty() && (!descriptor->build_config || expected_config != descriptor->build_config))
        return fail("plugin build configuration mismatch");

    m_State = registry.m_State;
    m_Owner = registry.BeginOwner();
    m_ModuleId = descriptor->module_id;
    m_SdkFingerprint = request.Expected.SdkFingerprint;
    m_Host = {sizeof(PluginHostApi), kPluginAbiVersion, this, m_SdkFingerprint.c_str(),
        [](void* user, const char* id, void* resource, void (*destroy)(void*)) noexcept -> bool {
            try {
                auto* plugin = static_cast<NativePlugin*>(user);
                if (!plugin || !plugin->m_State) return false;
                return plugin->m_State->Register(plugin->m_Owner, id, resource, destroy);
            } catch (...) {
                return false;
            }
        }, nullptr};

    bool startSuccess = false;
    try {
        startSuccess = start(&m_Host);
    } catch (...) {
        startSuccess = false;
    }

    if (!startSuccess) {
        if (m_State) m_State->Rollback(m_Owner);
        m_State.reset();
        m_Owner = 0;
        m_Stop = nullptr;
        return fail("plugin start failed; registration rolled back");
    }
    m_Active = true;
    return true;
}

void NativePlugin::Stop() noexcept {
    if (!m_Handle) return;
    if (m_Active && m_Stop) {
        try {
            m_Stop();
        } catch (...) {}
    }
    if (m_State && m_Owner) {
        try {
            m_State->Rollback(m_Owner);
        } catch (...) {}
    }
    m_State.reset();
    m_Owner = 0;
    m_Stop = nullptr;
    m_Active = false;
    m_ModuleId.clear();
    CloseLibrary(m_Handle);
    m_Handle = nullptr;
}

} // namespace Axle::PluginRuntime
