#include <Axle/ScriptRegistry.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <unordered_set>

namespace Axle {

    static std::unordered_map<std::string, ScriptMetadata>& GetRegistryMap() {
        static std::unordered_map<std::string, ScriptMetadata> s_Registry;
        return s_Registry;
    }

    static std::unordered_set<std::string>& GetDynamicScriptsSet() {
        static std::unordered_set<std::string> s_DynamicScripts;
        return s_DynamicScripts;
    }

    void ScriptRegistry::RegisterMetadata(const std::string& name, const ScriptMetadata& meta, bool isDynamic) {
        GetRegistryMap()[name] = meta;
        if (isDynamic) {
            GetDynamicScriptsSet().insert(name);
        }
    }

    void ScriptRegistry::UnregisterMetadata(const std::string& name) {
        GetRegistryMap().erase(name);
        GetDynamicScriptsSet().erase(name);
    }

    void ScriptRegistry::ClearDynamicScripts() {
        auto& map = GetRegistryMap();
        for (const auto& name : GetDynamicScriptsSet()) {
            map.erase(name);
        }
        GetDynamicScriptsSet().clear();
    }

    bool ScriptRegistry::IsDynamicScript(const std::string& name) {
        return GetDynamicScriptsSet().find(name) != GetDynamicScriptsSet().end();
    }

    const std::unordered_map<std::string, ScriptMetadata>& ScriptRegistry::GetScripts() {
        return GetRegistryMap();
    }

    const ScriptMetadata* ScriptRegistry::GetMetadata(const std::string& name) {
        auto& map = GetRegistryMap();
        auto it = map.find(name);
        if (it != map.end()) {
            return &it->second;
        }
        return nullptr;
    }

    bool ScriptRegistry::Has(const std::string& name) {
        return GetMetadata(name) != nullptr;
    }

    bool ScriptRegistry::BindByName(Entity entity, const std::string& scriptName) {
        if (!Has(scriptName) || !entity) return false;
        if (!entity.HasComponent<NativeScriptComponent>()) {
            entity.AddComponent<NativeScriptComponent>(scriptName);
        } else {
            entity.GetComponent<NativeScriptComponent>().Bind(scriptName);
        }
        return true;
    }

    ScriptableEntity* ScriptRegistry::Create(const std::string& name) {
        const auto* meta = GetMetadata(name);
        if (meta && meta->Factory) {
            return meta->Factory();
        }
        return nullptr;
    }

    void ScriptRegistry::Destroy(const std::string& name, ScriptableEntity* instance) {
        if (!instance) return;
        const auto* meta = GetMetadata(name);
        if (meta && meta->Destroyer) {
            meta->Destroyer(instance);
        } else {
            delete instance;
        }
    }

    void NativeScriptComponent::Bind(const std::string& scriptName) {
        ScriptName = scriptName;
        const auto* meta = ScriptRegistry::GetMetadata(scriptName);
        if (meta) {
            InstantiateScript = meta->Factory;
            DestroyScript = [](NativeScriptComponent* nsc) {
                if (nsc->Instance) {
                    delete nsc->Instance;
                    nsc->Instance = nullptr;
                }
            };

            // Populate default values from a clean temporary instance if fields not populated yet
            if (meta->Factory) {
                ScriptableEntity* temp = meta->Factory();
                if (temp) {
                    for (const auto& field : meta->Fields) {
                        switch (field.Type) {
                        case ScriptFieldType::Float:
                            if (FloatFields.find(field.Name) == FloatFields.end())
                                FloatFields[field.Name] = field.GetValue<float>(temp);
                            break;
                        case ScriptFieldType::Int:
                            if (IntFields.find(field.Name) == IntFields.end())
                                IntFields[field.Name] = field.GetValue<int>(temp);
                            break;
                        case ScriptFieldType::Bool:
                            if (BoolFields.find(field.Name) == BoolFields.end())
                                BoolFields[field.Name] = field.GetValue<bool>(temp);
                            break;
                        case ScriptFieldType::Vec3:
                            if (Vec3Fields.find(field.Name) == Vec3Fields.end())
                                Vec3Fields[field.Name] = field.GetValue<glm::vec3>(temp);
                            break;
                        case ScriptFieldType::String:
                            if (StringFields.find(field.Name) == StringFields.end())
                                StringFields[field.Name] = field.GetValue<std::string>(temp);
                            break;
                        case ScriptFieldType::EntityRef:
                            if (EntityFields.find(field.Name) == EntityFields.end())
                                EntityFields[field.Name] = field.GetValue<UUID>(temp);
                            break;
                        default:
                            break;
                        }
                    }
                    delete temp;
                }
            }
        }
    }

    void NativeScriptComponent::SyncFieldsToInstance() {
        if (!Instance) return;
        const auto* meta = ScriptRegistry::GetMetadata(ScriptName);
        if (!meta) return;

        for (const auto& field : meta->Fields) {
            switch (field.Type) {
            case ScriptFieldType::Float: {
                auto it = FloatFields.find(field.Name);
                if (it != FloatFields.end()) field.SetValue<float>(Instance, it->second);
                break;
            }
            case ScriptFieldType::Int: {
                auto it = IntFields.find(field.Name);
                if (it != IntFields.end()) field.SetValue<int>(Instance, it->second);
                break;
            }
            case ScriptFieldType::Bool: {
                auto it = BoolFields.find(field.Name);
                if (it != BoolFields.end()) field.SetValue<bool>(Instance, it->second);
                break;
            }
            case ScriptFieldType::Vec3: {
                auto it = Vec3Fields.find(field.Name);
                if (it != Vec3Fields.end()) field.SetValue<glm::vec3>(Instance, it->second);
                break;
            }
            case ScriptFieldType::String: {
                auto it = StringFields.find(field.Name);
                if (it != StringFields.end()) field.SetValue<std::string>(Instance, it->second);
                break;
            }
            case ScriptFieldType::EntityRef: {
                auto it = EntityFields.find(field.Name);
                if (it != EntityFields.end()) field.SetValue<UUID>(Instance, it->second);
                break;
            }
            default:
                break;
            }
        }
    }

    void NativeScriptComponent::SyncFieldsFromInstance() {
        if (!Instance) return;
        const auto* meta = ScriptRegistry::GetMetadata(ScriptName);
        if (!meta) return;

        for (const auto& field : meta->Fields) {
            switch (field.Type) {
            case ScriptFieldType::Float:
                FloatFields[field.Name] = field.GetValue<float>(Instance);
                break;
            case ScriptFieldType::Int:
                IntFields[field.Name] = field.GetValue<int>(Instance);
                break;
            case ScriptFieldType::Bool:
                BoolFields[field.Name] = field.GetValue<bool>(Instance);
                break;
            case ScriptFieldType::Vec3:
                Vec3Fields[field.Name] = field.GetValue<glm::vec3>(Instance);
                break;
            case ScriptFieldType::String:
                StringFields[field.Name] = field.GetValue<std::string>(Instance);
                break;
            case ScriptFieldType::EntityRef:
                EntityFields[field.Name] = field.GetValue<UUID>(Instance);
                break;
            default:
                break;
            }
        }
    }

}
