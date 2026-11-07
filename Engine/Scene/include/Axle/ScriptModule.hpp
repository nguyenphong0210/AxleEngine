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

    constexpr uint32_t AXLE_SCRIPT_ABI_VERSION = 4; // Entity lifetimes, SceneData layout and tick input edges
    constexpr uint64_t AXLE_SCRIPT_BUILD_ID = (uint64_t(AXLE_SCRIPT_ABI_VERSION) << 32)
#ifdef _MSC_FULL_VER
        | uint64_t(_MSC_FULL_VER)
#endif
#ifdef _DEBUG
        | (uint64_t(1) << 63)
#endif
#ifdef _HAS_EXCEPTIONS
        | (uint64_t(_HAS_EXCEPTIONS != 0) << 48)
#endif
#ifdef _ITERATOR_DEBUG_LEVEL
        | (uint64_t(_ITERATOR_DEBUG_LEVEL) << 49)
#endif
#ifdef _DLL
        | (uint64_t(1) << 56)
#endif
        ;

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
