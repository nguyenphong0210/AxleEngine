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

bool ValidateReceipt(const std::filesystem::path& receipt_path, const std::string& expected_id,
                     const std::string& expected_config, std::string& error) {
    if (receipt_path.empty()) return true;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(receipt_path, ec)) {
        error = "build receipt does not exist or is not a regular file: " + receipt_path.generic_string();
        return false;
    }
    std::ifstream stream(receipt_path);
    if (!stream) {
        error = "unable to open build receipt: " + receipt_path.generic_string();
        return false;
    }
    std::string line;
    std::unordered_map<std::string, std::string> fields;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            fields[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    auto schemaIt = fields.find("schema");
    if (schemaIt == fields.end() || schemaIt->second != "1") {
        error = "unsupported or missing build receipt schema version";
        return false;
    }
    auto pluginIt = fields.find("plugin_id");
    if (pluginIt != fields.end() && !expected_id.empty() && pluginIt->second != expected_id) {
        error = "build receipt plugin ID mismatch: expected '" + expected_id + "', got '" + pluginIt->second + "'";
        return false;
    }
    auto configIt = fields.find("configuration");
    if (configIt != fields.end() && !expected_config.empty() && configIt->second != expected_config) {
        error = "build receipt configuration mismatch: expected '" + expected_config + "', got '" + configIt->second + "'";
        return false;
    }
    return true;
}

} // namespace

NativePlugin::~NativePlugin() { Stop(); }

bool NativePlugin::Load(const std::filesystem::path& module_path, std::string expected_id,
                        std::string expected_config, RegistrationRegistry& registry,
                        std::string& diagnostic, const std::filesystem::path& receipt_path) {
    diagnostic.clear();
    if (m_Handle || m_Active) { diagnostic = "plugin is already loaded"; return false; }
    if (module_path.empty() || !module_path.is_absolute()) {
        diagnostic = "plugin path must be absolute and controlled by the resolver"; return false;
    }

    if (!receipt_path.empty()) {
        if (!ValidateReceipt(receipt_path, expected_id, expected_config, diagnostic)) {
            return false;
        }
    }

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
    if (!expected_config.empty() && (!descriptor->build_config || expected_config != descriptor->build_config))
        return fail("plugin build configuration mismatch");

    m_State = registry.m_State;
    m_Owner = registry.BeginOwner();
    m_ModuleId = descriptor->module_id;
    PluginHostApi host{kPluginAbiVersion, this,
        [](void* user, const char* id, void* resource, void (*destroy)(void*)) noexcept -> bool {
            try {
                auto* plugin = static_cast<NativePlugin*>(user);
                if (!plugin || !plugin->m_State) return false;
                return plugin->m_State->Register(plugin->m_Owner, id, resource, destroy);
            } catch (...) {
                return false;
            }
        }};

    bool startSuccess = false;
    try {
        startSuccess = start(&host);
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
