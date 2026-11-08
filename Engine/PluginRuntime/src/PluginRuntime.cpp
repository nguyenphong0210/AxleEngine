#include <Axle/PluginRuntime.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>

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
    std::vector<std::unique_ptr<RegistryEntry>> entries;
    bool alive = true;
    const std::thread::id ownerThread = std::this_thread::get_id();
    std::unordered_set<Owner> owners;
    std::unordered_map<Owner, std::weak_ptr<void>> leases;
    std::unordered_map<Owner, std::string> pluginIds;

    ~State() {
        DestroyAll();
    }

    void DestroyAll() noexcept {
        alive = false;
        owners.clear(); leases.clear(); pluginIds.clear();
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

    Owner BeginOwner() {
        if (!alive || std::this_thread::get_id() != ownerThread) return 0;
        static std::atomic<std::uint64_t> nextOwner{1};
        const auto owner = nextOwner.fetch_add(1);
        if (!owner) return 0;
        owners.insert(owner);
        return owner;
    }

    bool Register(Owner owner, const char* resource_id, void* resource,
                  void (*destroy)(void*)) {
        if (std::this_thread::get_id() != ownerThread || !alive || !owners.contains(owner) || !resource_id || !*resource_id) return false;
        if (std::any_of(entries.begin(), entries.end(), [resource_id](const auto& entry) {
                return entry->id == resource_id;
            })) return false;
        entries.push_back(std::make_unique<RegistryEntry>(RegistryEntry{owner, resource_id, resource, destroy}));
        return true;
    }

    void Rollback(Owner owner) noexcept {
        if (owner == 0 || std::this_thread::get_id() != ownerThread) return;
        owners.erase(owner); leases.erase(owner); pluginIds.erase(owner);
        for (;;) {
            auto it = std::find_if(entries.rbegin(), entries.rend(), [owner](const auto& entry) { return entry->owner == owner; });
            if (it == entries.rend()) break;
            const auto index = static_cast<std::size_t>(std::distance(it, entries.rend()) - 1);
            auto detached = std::move(entries[index]);
            entries.erase(entries.begin() + index);
            if (detached->destroy) { try { detached->destroy(detached->resource); } catch (...) {} }
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
    : m_State(std::make_shared<State>()) { NativePlugin::CollectRetiredModules(); }

RegistrationRegistry::~RegistrationRegistry() {
    NativePlugin::CollectRetiredModules();
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

std::shared_ptr<void> RegistrationRegistry::GetOwnerLease(Owner owner) const {
    return LeaseFor(m_State, owner);
}
bool RegistrationRegistry::IsOwnerActive(Owner owner) const {
    return OwnerIsActive(m_State, owner);
}
bool RegistrationRegistry::OwnerIsActive(const std::shared_ptr<State>& state, Owner owner) {
    return state && std::this_thread::get_id() == state->ownerThread && state->alive && state->owners.contains(owner);
}
std::shared_ptr<void> RegistrationRegistry::LeaseFor(const std::shared_ptr<State>& state, Owner owner) {
    if (!OwnerIsActive(state, owner)) return {};
    const auto it = state->leases.find(owner);
    return it == state->leases.end() ? nullptr : it->second.lock();
}
bool RegistrationRegistry::RegisterFor(const std::shared_ptr<State>& state, Owner owner, const char* id, void* resource, void (*destroy)(void*)) {
    return state && state->Register(owner, id, resource, destroy);
}
std::string RegistrationRegistry::PluginIdFor(const std::shared_ptr<State>& state, Owner owner) {
    if (!OwnerIsActive(state, owner)) return {};
    const auto found = state->pluginIds.find(owner);
    return found == state->pluginIds.end() ? "" : found->second;
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

struct NativePlugin::ModuleState {
    // Intrusive retirement queues cannot fail allocation during noexcept Stop.
    // Queue storage intentionally survives process static destruction: the host
    // explicitly drains its owner thread before unloading service providers.
    struct RetirementQueue {
        std::mutex Mutex;
        ModuleState* Stops = nullptr;
        ModuleState* Deletes = nullptr;
    };
    static RetirementQueue& Queue() { static auto* queue = new RetirementQueue; return *queue; }
    ModuleState* StopNext = nullptr;
    ModuleState* DeleteNext = nullptr;
    std::shared_ptr<ModuleState> PendingStop;
    void* Handle = nullptr;
#if defined(_WIN32)
    HANDLE ArtifactFile = INVALID_HANDLE_VALUE;
#endif
    StopFn Stop = nullptr;
    bool StartEntered = false;
    std::atomic<bool> Active{false};
    const std::thread::id OwnerThread = std::this_thread::get_id();
    std::shared_ptr<RegistrationRegistry::State> Registry;
    RegistrationRegistry::Owner Owner = 0;
    PluginHostApi Host{};
    std::string Sdk;
    void* ServiceUser = nullptr;
    std::shared_ptr<void> ServiceLifetime;
    void (*OwnerInitialized)(void*, std::uint64_t, bool) = nullptr;
    const void* (*GetService)(void*, std::uint64_t, std::uint32_t, std::uint32_t) = nullptr;
    void Revoke() noexcept {
        Active = false;
        if (Registry && Owner) Registry->Rollback(Owner);
        Registry.reset();
    }
    static void Delete(ModuleState* module) noexcept {
        if (std::this_thread::get_id() == module->OwnerThread) { delete module; return; }
        auto& queue = Queue();
        std::lock_guard lock(queue.Mutex);
        module->DeleteNext = queue.Deletes; queue.Deletes = module;
    }
    ~ModuleState() {
        Active = false;
        if (Registry && Owner) Registry->Rollback(Owner);
        if (StartEntered && Stop) { try { Stop(); } catch (...) {} }
        CloseLibrary(Handle);
#if defined(_WIN32)
        if (ArtifactFile != INVALID_HANDLE_VALUE) CloseHandle(ArtifactFile);
#endif
    }
};

NativePlugin::~NativePlugin() { Stop(); }
bool NativePlugin::IsActive() const noexcept {
    return m_Active && m_Module && std::this_thread::get_id() == m_Module->OwnerThread && m_Module->Active && m_Module->Registry &&
        m_Module->Registry->alive && m_Module->Registry->owners.contains(m_Module->Owner);
}
const PluginHostApi* NativePlugin::GetHostApi() const noexcept { return m_Module ? &m_Module->Host : nullptr; }

bool NativePlugin::Load(const LoadRequest& request, RegistrationRegistry& registry, std::string& diagnostic) {
    diagnostic.clear();
    if (m_Module || m_Active) { diagnostic = "plugin is already loaded"; return false; }
    if (request.ModulePath.empty() || !request.ModulePath.is_absolute()) {
        diagnostic = "plugin path must be absolute and controlled by the resolver"; return false;
    }
    if (request.Expected.Product == Plugin::ProductKind::Runtime) {
        diagnostic = "shipping Runtime native DLL loading is unsupported"; return false;
    }
    CollectRetiredModules();
    auto code = std::shared_ptr<ModuleState>(new ModuleState, ModuleState::Delete);
#if defined(_WIN32)
    // Pin the validated file before hashing it. Neither a concurrent writer nor
    // a path replacement may change the bytes between validation and loading,
    // including while a component/job retains code after logical Stop.
    code->ArtifactFile = CreateFileW(request.ModulePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (code->ArtifactFile == INVALID_HANDLE_VALUE) { diagnostic = LastError(); return false; }
#endif
    if (!Plugin::ValidateArtifact(request.Expected, request.ModulePath, request.ReceiptPath, diagnostic)) return false;
    code->Handle = OpenLibrary(request.ModulePath);
    if (!code->Handle) { diagnostic = LastError(); return false; }
    auto fail = [&](const char* reason) {
        diagnostic = reason;
        code->Active = false;
        if (code->Owner && request.OwnerInitialized) { try { request.OwnerInitialized(request.ServiceUser, code->Owner, false); } catch (...) {} }
        if (code->Registry) code->Registry->Rollback(code->Owner);
        code->Registry.reset();
        return false;
    };
    auto query = reinterpret_cast<GetDescriptorFn>(FindSymbol(code->Handle, "ax_plugin_get_descriptor"));
    auto start = reinterpret_cast<StartFn>(FindSymbol(code->Handle, "ax_plugin_start"));
    code->Stop = reinterpret_cast<StopFn>(FindSymbol(code->Handle, "ax_plugin_stop"));
    if (!query || !start || !code->Stop) return fail("required plugin entrypoint is missing");
    const PluginDescriptor* descriptor = nullptr;
    try { descriptor = query(); } catch (...) { return fail("plugin descriptor callback threw"); }
    if (!descriptor || descriptor->struct_size < sizeof(PluginDescriptor) || !descriptor->module_id) return fail("invalid plugin descriptor");
    if (descriptor->abi_version != kPluginAbiVersion) return fail("plugin ABI mismatch");
    const auto& expected = request.Expected;
    if (expected.ModuleName != descriptor->module_id) return fail("plugin module ID mismatch");
    if (!descriptor->plugin_id || expected.PluginId != descriptor->plugin_id) return fail("plugin ID mismatch");
    if (!descriptor->sdk_fingerprint || expected.SdkFingerprint != descriptor->sdk_fingerprint) return fail("plugin SDK mismatch");
    if (!descriptor->toolchain_fingerprint || expected.ToolchainFingerprint != descriptor->toolchain_fingerprint) return fail("plugin toolchain mismatch");
    if (!descriptor->build_config || expected.Configuration != descriptor->build_config) return fail("plugin build configuration mismatch");
    code->Registry = registry.m_State;
    code->Owner = registry.BeginOwner();
    if (!code->Owner) return fail("plugin registration requires a live owner-thread registry");
    code->Registry->leases[code->Owner] = code;
    code->Registry->pluginIds[code->Owner] = expected.PluginId;
    code->Sdk = expected.SdkFingerprint;
    code->ServiceUser = request.ServiceUser;
    code->ServiceLifetime = request.ServiceLifetime;
    code->OwnerInitialized = request.OwnerInitialized;
    code->GetService = request.GetService;
    code->Active = true;
    code->Host = {sizeof(PluginHostApi), kPluginAbiVersion, code.get(), code->Owner, code->Sdk.c_str(),
        [](void* user, const char* id, void* resource, void (*destroy)(void*)) noexcept -> bool {
            try {
                const auto* module = static_cast<ModuleState*>(user);
                return module && std::this_thread::get_id() == module->OwnerThread && module->Active && module->Registry && module->Registry->Register(module->Owner, id, resource, destroy);
            } catch (...) { return false; }
        },
        [](void* user, std::uint32_t service, std::uint32_t minimumSize) noexcept -> const void* {
            try {
                const auto* module = static_cast<ModuleState*>(user);
                return module && std::this_thread::get_id() == module->OwnerThread && module->Active && module->Registry &&
                    module->Registry->alive && module->Registry->owners.contains(module->Owner) && module->GetService
                    ? module->GetService(module->ServiceUser, module->Owner, service, minimumSize) : nullptr;
            } catch (...) { return nullptr; }
        }};
    code->StartEntered = true;
    bool started = false;
    try { started = start(&code->Host); } catch (...) {}
    if (!started) return fail("plugin start failed; registration rolled back");
    if (request.OwnerInitialized) {
        try { request.OwnerInitialized(request.ServiceUser, code->Owner, true); }
        catch (...) { return fail("host could not commit plugin initialization"); }
    }
    m_ModuleId = expected.ModuleName;
    m_Module = std::move(code);
    m_Active = true;
    return true;
}

void NativePlugin::Stop() noexcept {
    auto code = std::move(m_Module);
    m_Active = false;
    m_ModuleId.clear();
    if (!code) return;
    code->Active = false;
    if (std::this_thread::get_id() == code->OwnerThread) code->Revoke();
    else {
        auto& queue = ModuleState::Queue();
        std::lock_guard lock(queue.Mutex);
        code->PendingStop = code;
        code->StopNext = queue.Stops; queue.Stops = code.get();
    }
    // Stop and FreeLibrary run only after every retained payload/destructor/job
    // releases its lease. The host table remains valid through that boundary.
}
bool NativePlugin::AbortStartup() noexcept {
    if (m_Module && std::this_thread::get_id() != m_Module->OwnerThread) return false;
    if (m_Module && m_Module->OwnerInitialized) {
        try { m_Module->OwnerInitialized(m_Module->ServiceUser, m_Module->Owner, false); } catch (...) {}
    }
    Stop(); return true;
}

void NativePlugin::CollectRetiredModules() noexcept {
    auto& queue = ModuleState::Queue();
    for (;;) {
        ModuleState* retired = nullptr;
        bool stop = false;
        {
            std::lock_guard lock(queue.Mutex);
            auto** next = &queue.Stops;
            while (*next && (*next)->OwnerThread != std::this_thread::get_id()) next = &(*next)->StopNext;
            if (*next) { retired = *next; *next = retired->StopNext; stop = true; }
            else {
                next = &queue.Deletes;
                while (*next && (*next)->OwnerThread != std::this_thread::get_id()) next = &(*next)->DeleteNext;
                if (*next) { retired = *next; *next = retired->DeleteNext; }
            }
        }
        if (!retired) break;
        if (stop) {
            auto keepAlive = std::move(retired->PendingStop);
            retired->Revoke();
        } else delete retired;
    }
}

} // namespace Axle::PluginRuntime
