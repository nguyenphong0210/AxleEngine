#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <variant>
#include <type_traits>
#include <glm/glm.hpp>

namespace Axle {

    using UIDataValue = std::variant<int32_t, float, bool, std::string, glm::vec4>;

    class UIDataContext {
    public:
        UIDataContext() = default;
        explicit UIDataContext(const UIDataContext* parent) : m_ParentContext(parent) {}
        ~UIDataContext() = default;

        void SetParentContext(const UIDataContext* parent) { m_ParentContext = parent; }
        const UIDataContext* GetParentContext() const { return m_ParentContext; }

        void SetInt(std::string_view key, int32_t value) { m_Values[std::string(key)] = value; }
        void SetFloat(std::string_view key, float value) { m_Values[std::string(key)] = value; }
        void SetBool(std::string_view key, bool value) { m_Values[std::string(key)] = value; }
        void SetString(std::string_view key, std::string_view value) { m_Values[std::string(key)] = std::string(value); }
        void SetColor(std::string_view key, const glm::vec4& value) { m_Values[std::string(key)] = value; }

        template<typename T>
        void Set(std::string_view key, T&& value) {
            using Decayed = std::decay_t<T>;
            if constexpr (std::is_same_v<Decayed, int32_t> || std::is_same_v<Decayed, int>) {
                SetInt(key, static_cast<int32_t>(value));
            } else if constexpr (std::is_same_v<Decayed, float> || std::is_same_v<Decayed, double>) {
                SetFloat(key, static_cast<float>(value));
            } else if constexpr (std::is_same_v<Decayed, bool>) {
                SetBool(key, static_cast<bool>(value));
            } else if constexpr (std::is_convertible_v<T, std::string_view>) {
                SetString(key, std::string_view(value));
            } else if constexpr (std::is_same_v<Decayed, glm::vec4>) {
                SetColor(key, value);
            } else {
                m_Values[std::string(key)] = std::forward<T>(value);
            }
        }

        bool Has(std::string_view key) const {
            if (m_Values.contains(std::string(key))) return true;
            return m_ParentContext ? m_ParentContext->Has(key) : false;
        }

        const UIDataValue* Get(std::string_view key) const {
            auto it = m_Values.find(std::string(key));
            if (it != m_Values.end()) return &it->second;
            return m_ParentContext ? m_ParentContext->Get(key) : nullptr;
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
            return m_ParentContext ? m_ParentContext->TriggerAction(actionName) : false;
        }

        void Clear() {
            m_Values.clear();
            m_Actions.clear();
        }

    private:
        const UIDataContext* m_ParentContext = nullptr;
        std::unordered_map<std::string, UIDataValue> m_Values;
        std::unordered_map<std::string, std::function<void()>> m_Actions;
    };

} // namespace Axle
