#pragma once
#include <Axle/Types.hpp>
#include <Axle/ScriptField.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Axle {

    class ScriptableEntity;
    class Entity;

    struct ScriptMetadata {
        std::string Name;
        ScriptableEntity* (*Factory)() = nullptr;
        void (*Destroyer)(ScriptableEntity*) = nullptr;
        std::vector<ScriptField> Fields;
    };

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

            RegisterMetadata(name, meta);
        }

        static const std::unordered_map<std::string, ScriptMetadata>& GetScripts();
        static const ScriptMetadata* GetMetadata(const std::string& name);
        static bool Has(const std::string& name);
        static bool BindByName(Entity entity, const std::string& scriptName);
        static ScriptableEntity* Create(const std::string& name);
        static void Destroy(const std::string& name, ScriptableEntity* instance);

    private:
        static void RegisterMetadata(const std::string& name, const ScriptMetadata& meta);
    };

}

#define AXLE_REGISTER_SCRIPT(Type) \
    namespace { \
        struct Type##_Registrar { \
            Type##_Registrar() { ::Axle::ScriptRegistry::Register<Type>(#Type); } \
        }; \
        inline static Type##_Registrar s_##Type##_Registrar; \
    }
