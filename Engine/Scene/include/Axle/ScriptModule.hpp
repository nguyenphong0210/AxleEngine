#pragma once
#include <Axle/Types.hpp>
#include <Axle/ScriptField.hpp>
#include <string>
#include <vector>
#include <utility>

#ifdef _WIN32
    #ifdef AXLE_SCRIPT_BUILD_DLL
        #define AXLE_SCRIPT_API __declspec(dllexport)
    #else
        #define AXLE_SCRIPT_API __declspec(dllimport)
    #endif
#else
    #define AXLE_SCRIPT_API
#endif

namespace Axle {

    class ScriptableEntity;

    struct ScriptMetadata {
        std::string Name;
        ScriptableEntity* (*Factory)() = nullptr;
        void (*Destroyer)(ScriptableEntity*) = nullptr;
        std::vector<ScriptField> Fields;
    };

    using ScriptRegisterCallback = void (*)(const char* name, const ScriptMetadata* meta);

    /**
     * @brief Manages the collection of scripts declared in an external DLL (AxleScripts.dll).
     * Automatically populated via AXLE_REGISTER_SCRIPT macro when AXLE_SCRIPTS_DLL is defined.
     */
    class ScriptModuleRegistry {
    public:
        static std::vector<std::pair<std::string, ScriptMetadata>>& GetRegisteredScripts() {
            static std::vector<std::pair<std::string, ScriptMetadata>> s_Scripts;
            return s_Scripts;
        }

        template<typename T>
        static void Register(const std::string& name) {
            ScriptMetadata meta;
            meta.Name = name;
            meta.Factory = []() -> ScriptableEntity* {
                return static_cast<ScriptableEntity*>(new T());
            };
            meta.Destroyer = [](ScriptableEntity* instance) {
                delete instance;
            };

            ScriptFieldRegistry fieldReg;
            if constexpr (requires(ScriptFieldRegistry& reg) { T::RegisterFields(reg); }) {
                T::RegisterFields(fieldReg);
            }
            meta.Fields = fieldReg.GetFields();

            GetRegisteredScripts().push_back({ name, meta });
        }
    };

}
