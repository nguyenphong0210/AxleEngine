#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <variant>
#include <glm/glm.hpp>

namespace Axle {

    using UIDataValue = std::variant<int32_t, float, bool, std::string, glm::vec4>;

    class UIDataContext {
    public:
        UIDataContext() = default;
        ~UIDataContext() = default;

        void SetInt(std::string_view key, int32_t value) { m_Values[std::string(key)] = value; }
        void SetFloat(std::string_view key, float value) { m_Values[std::string(key)] = value; }
        void SetBool(std::string_view key, bool value) { m_Values[std::string(key)] = value; }
        void SetString(std::string_view key, std::string_view value) { m_Values[std::string(key)] = std::string(value); }
        void SetColor(std::string_view key, const glm::vec4& value) { m_Values[std::string(key)] = value; }

        bool Has(std::string_view key) const { return m_Values.contains(std::string(key)); }
        const UIDataValue* Get(std::string_view key) const {
            auto it = m_Values.find(std::string(key));
            return (it != m_Values.end()) ? &it->second : nullptr;
        }

        void RegisterAction(std::string_view actionName, std::function<void()> callback) {
            m_Actions[std::string(actionName)] = std::move(callback);
        }

        bool TriggerAction(std::string_view actionName) const {
            auto it = m_Actions.find(std::string(actionName));
            if (it != m_Actions.end() && it->second) {
                it->second();
                return true;
            }
            return false;
        }

        void Clear() {
            m_Values.clear();
            m_Actions.clear();
        }

    private:
        std::unordered_map<std::string, UIDataValue> m_Values;
        std::unordered_map<std::string, std::function<void()>> m_Actions;
    };

} // namespace Axle
