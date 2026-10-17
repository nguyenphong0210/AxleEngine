#include <Axle/ScriptModule.hpp>
#include <Axle/Input.hpp>
#include <Axle/Log.hpp>

#if __has_include("AutoScripts.inc")
    #include "AutoScripts.inc"
#endif

struct GLFWwindow;

extern "C" {

    AXLE_SCRIPT_API void Axle_SetInputContext(void* context) {
        if (context) {
            const auto* inputCtx = static_cast<const Axle::InputContext*>(context);
            if (inputCtx->IsKeyPressedFn != nullptr) {
                Axle::Input::SetInputContext(inputCtx);
                AXLE_INFO("AxleScripts.dll: Input context synchronized (delegated via window: {0})", (void*)inputCtx->Window);
            } else {
                Axle::Input::SetContext(static_cast<GLFWwindow*>(context));
                AXLE_INFO("AxleScripts.dll: Input context synchronized (window: {0})", context);
            }
        }
    }

    AXLE_SCRIPT_API void Axle_InitializeScripts(Axle::ScriptRegisterCallback registerCallback) {
        if (!registerCallback) {
            AXLE_ERROR("Axle_InitializeScripts called with null registerCallback!");
            return;
        }

        const auto& scripts = Axle::ScriptModuleRegistry::GetRegisteredScripts();
        AXLE_INFO("AxleScripts.dll: Initializing and exporting {0} script(s)", scripts.size());

        for (const auto& [name, meta] : scripts) {
            registerCallback(name.c_str(), &meta);
            AXLE_TRACE("  -> Exported Script: {0}", name);
        }
    }

    AXLE_SCRIPT_API void Axle_ShutdownScripts() {
        AXLE_INFO("AxleScripts.dll: Shutting down script module");
    }

    AXLE_SCRIPT_API uint32_t Axle_GetScriptCount() {
        return static_cast<uint32_t>(Axle::ScriptModuleRegistry::GetRegisteredScripts().size());
    }

}
