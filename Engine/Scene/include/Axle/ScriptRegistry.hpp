#pragma once
#include <Axle/Types.hpp>
#include <Axle/ScriptField.hpp>
#include <Axle/ScriptModule.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Axle {

    class ScriptableEntity;
    class Entity;

    class ScriptRegistry {
    public:
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

            RegisterMetadata(name, meta, false);
        }

        static const std::unordered_map<std::string, ScriptMetadata>& GetScripts();
        static const ScriptMetadata* GetMetadata(const std::string& name);
        static bool Has(const std::string& name);
        static bool BindByName(Entity entity, const std::string& scriptName);
        static ScriptableEntity* Create(const std::string& name);
        static void Destroy(const std::string& name, ScriptableEntity* instance);

        // Dynamic DLL Metadata Registration & Lifecycle
        static void RegisterMetadata(const std::string& name, const ScriptMetadata& meta, bool isDynamic = false);
        static void UnregisterMetadata(const std::string& name);
        static void ClearDynamicScripts();
        static bool IsDynamicScript(const std::string& name);
    };

}

#ifdef AXLE_SCRIPTS_DLL
#define AXLE_REGISTER_SCRIPT(Type) \
    namespace { \
        struct Type##_Registrar { \
            Type##_Registrar() { ::Axle::ScriptModuleRegistry::Register<Type>(#Type); } \
        }; \
        inline static Type##_Registrar s_##Type##_Registrar; \
    }
#else
#define AXLE_REGISTER_SCRIPT(Type) \
    namespace { \
        struct Type##_Registrar { \
            Type##_Registrar() { ::Axle::ScriptRegistry::Register<Type>(#Type); } \
        }; \
        inline static Type##_Registrar s_##Type##_Registrar; \
    }
#endif
