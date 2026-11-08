#pragma once
#include <Axle/PluginABI.hpp>
#include <cstdint>

// Supported plugin SDK: borrowed opaque handles and POD function tables. Plugins
// import the shared hosts and never link Engine, EnTT, Jolt, enkiTS or ImGui code.
// Host services validate owner/scene handles and run on the Engine owner thread;
// work callbacks are the sole exception and may only touch their owned job data.
// Callback implementations contain exceptions. The host also guards each call.
namespace Axle::SDK {
inline constexpr std::uint32_t Version = 1;
inline constexpr std::uint32_t RuntimeService = 1, EditorService = 2;
enum class ServiceAddress : std::uint32_t { Project, Jobs, Physics, Renderer, Components };
enum class Phase : std::uint32_t { Frame, FixedControls, PostPhysics, Animation, Late };
struct EntityRef { std::uint64_t Scene; std::uint64_t Entity; };
struct EntityRemap { std::uint64_t Source; std::uint64_t Destination; };
struct Transform {
    float Position[3];
    float Rotation[4]; // w, x, y, z
    float Scale[3];
};
using TextSink = bool (*)(void*, const char*, std::uint32_t);
struct ComponentType {
    std::uint32_t StructSize;
    const char* TypeId; // plugin-id/local-name, persistent authored identity
    std::uint32_t Schema;
    bool Required;
    bool ContainsEntityReferences;
    void* User;
    // Ownership transfers only when RegisterComponent succeeds. The descriptor
    // destroys this context after its last value/scene pin, before code unload.
    // A transferred context must not also be owned by register_resource.
    void (*DestroyUser)(void*);
    void* (*Create)(void*, EntityRef, const char*, std::uint32_t);
    void (*Destroy)(void*, void*);
    bool (*Serialize)(void*, const void*, TextSink, void* sinkUser);
    void* (*Clone)(void*, const void*, EntityRef destination, const EntityRemap*, std::uint32_t);
    void (*Start)(void*, void*, EntityRef);
    void (*Stop)(void*, void*, EntityRef);
    void (*Update)(void*, void*, EntityRef, Phase, float);
};
struct RuntimeServices {
    std::uint32_t StructSize;
    std::uint32_t Version;
    const char* SdkFingerprint;
    void* User;
    const void* (*Address)(void*, std::uint64_t owner, ServiceAddress, std::uint64_t scene);
    bool (*RegisterComponent)(void*, std::uint64_t owner, const ComponentType*);
    bool (*GetTransform)(void*, std::uint64_t owner, EntityRef, Transform*);
    bool (*SetTransform)(void*, std::uint64_t owner, EntityRef, const Transform*);
    bool (*GetLinearVelocity)(void*, std::uint64_t owner, EntityRef, float* out3);
    bool (*SetLinearVelocity)(void*, std::uint64_t owner, EntityRef, const float* velocity3);
    bool (*SetComponent)(void*, std::uint64_t owner, EntityRef, const char* typeId, const char* payload, std::uint32_t bytes);
    void* (*GetComponent)(void*, std::uint64_t owner, EntityRef, const char* typeId);
    bool (*RemoveComponent)(void*, std::uint64_t owner, EntityRef, const char* typeId);
    bool (*SubmitJob)(void*, std::uint64_t owner, std::uint64_t scene,
                      void (*work)(void*), void (*complete)(void*), void* data, void (*destroy)(void*));
    bool (*DebugLine)(void*, std::uint64_t owner, std::uint64_t scene, const float* from3, const float* to3, const float* color4);
    void (*Log)(void*, std::uint64_t owner, const char*);
};
using InspectorDraw = void (*)(void* user, EntityRef entity, void* component);
struct EditorServices {
    std::uint32_t StructSize;
    std::uint32_t Version;
    void* User;
    const void* (*Context)(void*);
    bool (*RegisterInspector)(void*, std::uint64_t owner, const char* typeId, InspectorDraw, void* user);
    void (*Text)(void*, const char*);
    bool (*DragFloat)(void*, const char* label, float* value, float speed);
    bool (*Button)(void*, const char*);
};
}

#if defined(_WIN32)
#if defined(AXLE_RUNTIME_HOST_EXPORTS)
#define AXLE_RUNTIME_HOST_API __declspec(dllexport)
#else
#define AXLE_RUNTIME_HOST_API __declspec(dllimport)
#endif
#if defined(AXLE_EDITOR_HOST_EXPORTS)
#define AXLE_EDITOR_HOST_API __declspec(dllexport)
#else
#define AXLE_EDITOR_HOST_API __declspec(dllimport)
#endif
#define AXLE_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define AXLE_RUNTIME_HOST_API __attribute__((visibility("default")))
#define AXLE_EDITOR_HOST_API __attribute__((visibility("default")))
#define AXLE_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif
extern "C" {
AXLE_RUNTIME_HOST_API const Axle::SDK::RuntimeServices* AxleRuntimeHost_GetV1() noexcept;
AXLE_RUNTIME_HOST_API const char* AxleRuntimeHost_SdkFingerprint() noexcept;
AXLE_RUNTIME_HOST_API const char* AxleRuntimeHost_ToolchainFingerprint() noexcept;
AXLE_RUNTIME_HOST_API bool AxleRuntimeHost_BindV1(const Axle::SDK::RuntimeServices*) noexcept;
AXLE_RUNTIME_HOST_API bool AxleRuntimeHost_UnbindV1(void* user) noexcept;
AXLE_EDITOR_HOST_API const Axle::SDK::EditorServices* AxleEditorHost_GetV1() noexcept;
AXLE_EDITOR_HOST_API const char* AxleEditorHost_SdkFingerprint() noexcept;
AXLE_EDITOR_HOST_API bool AxleEditorHost_BindV1(const Axle::SDK::EditorServices*) noexcept;
AXLE_EDITOR_HOST_API bool AxleEditorHost_UnbindV1(void* user) noexcept;
}

// Build helpers provide these exact identities; there is no guessed fallback.
#if defined(AXLE_PLUGIN_SDK_FINGERPRINT) && defined(AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT) && defined(AXLE_PLUGIN_BUILD_CONFIGURATION)
#define AXLE_PLUGIN_DESCRIPTOR(pluginId, moduleId) \
    AXLE_PLUGIN_EXPORT const Axle::PluginRuntime::PluginDescriptor* ax_plugin_get_descriptor() noexcept { \
        static const Axle::PluginRuntime::PluginDescriptor descriptor{sizeof(Axle::PluginRuntime::PluginDescriptor), \
            Axle::PluginRuntime::kPluginAbiVersion, pluginId, moduleId, AXLE_PLUGIN_BUILD_CONFIGURATION, \
            AXLE_PLUGIN_SDK_FINGERPRINT, AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT}; return &descriptor; }
#endif
