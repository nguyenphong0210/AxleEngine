#include <Axle/PluginRuntime.hpp>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_set>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace Axle::PluginRuntime {

struct RegistrationRegistry::Entry {
    Owner owner;
    std::string id;
    void* resource;
    void (*destroy)(void*);
};

RegistrationRegistry::~RegistrationRegistry() {
    for (auto it = m_Entries.rbegin(); it != m_Entries.rend(); ++it) {
        if ((*it)->destroy) (*it)->destroy((*it)->resource);
    }
}

RegistrationRegistry::Owner RegistrationRegistry::BeginOwner() { return m_NextOwner++; }

bool RegistrationRegistry::Register(Owner owner, const char* resource_id, void* resource,
                                    void (*destroy)(void*)) {
    if (owner == 0 || !resource_id || !*resource_id) return false;
    if (std::any_of(m_Entries.begin(), m_Entries.end(), [resource_id](const auto& entry) {
            return entry->id == resource_id;
        })) return false;
    m_Entries.push_back(std::make_unique<Entry>(Entry{owner, resource_id, resource, destroy}));
    return true;
}

void RegistrationRegistry::Rollback(Owner owner) noexcept {
    for (auto it = m_Entries.begin(); it != m_Entries.end();) {
        if ((*it)->owner != owner) { ++it; continue; }
        if ((*it)->destroy) (*it)->destroy((*it)->resource);
        it = m_Entries.erase(it);
    }
}

bool RegistrationRegistry::Has(const std::string& resource_id) const {
    return std::any_of(m_Entries.begin(), m_Entries.end(), [&resource_id](const auto& entry) {
        return entry->id == resource_id;
    });
}

std::size_t RegistrationRegistry::Size() const noexcept { return m_Entries.size(); }

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
    return "native library load or symbol lookup failed";
#else
    const char* error = dlerror();
    return error ? error : "native library load or symbol lookup failed";
#endif
}
} // namespace

NativePlugin::~NativePlugin() { Stop(); }

bool NativePlugin::Load(const std::filesystem::path& module_path, std::string expected_id,
                        std::string expected_config, RegistrationRegistry& registry,
                        std::string& diagnostic) {
    diagnostic.clear();
    if (m_Handle || m_Active) { diagnostic = "plugin is already loaded"; return false; }
    if (module_path.empty() || !module_path.is_absolute()) {
        diagnostic = "plugin path must be absolute and controlled by the resolver"; return false;
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
    const PluginDescriptor* descriptor = get_descriptor();
    if (!descriptor || descriptor->struct_size < sizeof(PluginDescriptor) || !descriptor->module_id)
        return fail("invalid plugin descriptor");
    if (descriptor->abi_version != kPluginAbiVersion) return fail("plugin ABI mismatch");
    if (expected_id != descriptor->module_id) return fail("plugin module ID mismatch");
    if (!expected_config.empty() && descriptor->build_config && expected_config != descriptor->build_config)
        return fail("plugin build configuration mismatch");

    m_Registry = &registry; m_Owner = registry.BeginOwner(); m_ModuleId = descriptor->module_id;
    PluginHostApi host{kPluginAbiVersion, this,
        [](void* user, const char* id, void* resource, void (*destroy)(void*)) {
            auto* plugin = static_cast<NativePlugin*>(user);
            return plugin->m_Registry && plugin->m_Registry->Register(plugin->m_Owner, id, resource, destroy);
        }};
    if (!start(&host)) {
        registry.Rollback(m_Owner); m_Registry = nullptr; m_Owner = 0; m_Stop = nullptr;
        return fail("plugin start failed; registration rolled back");
    }
    m_Active = true;
    return true;
}

void NativePlugin::Stop() noexcept {
    if (!m_Handle) return;
    if (m_Active && m_Stop) m_Stop();
    if (m_Registry && m_Owner) m_Registry->Rollback(m_Owner);
    m_Registry = nullptr; m_Owner = 0; m_Stop = nullptr; m_Active = false; m_ModuleId.clear();
    CloseLibrary(m_Handle); m_Handle = nullptr;
}

} // namespace Axle::PluginRuntime

