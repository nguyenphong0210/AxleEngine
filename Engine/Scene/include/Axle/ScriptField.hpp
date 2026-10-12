#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstddef>
#include <type_traits>

namespace Axle {

    enum class ScriptFieldType {
        None = 0,
        Float,
        Int,
        Bool,
        Vec3,
        String
    };

    struct ScriptField {
        std::string Name;
        ScriptFieldType Type = ScriptFieldType::None;
        size_t Offset = 0;
        float Min = 0.0f;
        float Max = 0.0f;
        std::string Tooltip;

        template<typename T>
        const T& GetValue(const void* instance) const {
            return *reinterpret_cast<const T*>(reinterpret_cast<const char*>(instance) + Offset);
        }

        template<typename T>
        void SetValue(void* instance, const T& value) const {
            *reinterpret_cast<T*>(reinterpret_cast<char*>(instance) + Offset) = value;
        }
    };

    class ScriptFieldRegistry {
    public:
        template<typename ClassType, typename MemberType>
        static size_t CalculateOffset(MemberType ClassType::* member) {
            alignas(ClassType) char buffer[sizeof(ClassType)];
            ClassType* dummy = reinterpret_cast<ClassType*>(buffer);
            return reinterpret_cast<size_t>(&(dummy->*member)) - reinterpret_cast<size_t>(dummy);
        }

        template<typename ClassType>
        void AddField(const std::string& name, float ClassType::* member, float min = 0.0f, float max = 0.0f, const std::string& tooltip = "") {
            ScriptField field;
            field.Name = name;
            field.Type = ScriptFieldType::Float;
            field.Offset = CalculateOffset(member);
            field.Min = min;
            field.Max = max;
            field.Tooltip = tooltip;
            m_Fields.push_back(field);
        }

        template<typename ClassType>
        void AddField(const std::string& name, int ClassType::* member, int min = 0, int max = 0, const std::string& tooltip = "") {
            ScriptField field;
            field.Name = name;
            field.Type = ScriptFieldType::Int;
            field.Offset = CalculateOffset(member);
            field.Min = static_cast<float>(min);
            field.Max = static_cast<float>(max);
            field.Tooltip = tooltip;
            m_Fields.push_back(field);
        }

        template<typename ClassType>
        void AddField(const std::string& name, bool ClassType::* member, const std::string& tooltip = "") {
            ScriptField field;
            field.Name = name;
            field.Type = ScriptFieldType::Bool;
            field.Offset = CalculateOffset(member);
            field.Tooltip = tooltip;
            m_Fields.push_back(field);
        }

        template<typename ClassType>
        void AddField(const std::string& name, glm::vec3 ClassType::* member, const std::string& tooltip = "") {
            ScriptField field;
            field.Name = name;
            field.Type = ScriptFieldType::Vec3;
            field.Offset = CalculateOffset(member);
            field.Tooltip = tooltip;
            m_Fields.push_back(field);
        }

        template<typename ClassType>
        void AddField(const std::string& name, std::string ClassType::* member, const std::string& tooltip = "") {
            ScriptField field;
            field.Name = name;
            field.Type = ScriptFieldType::String;
            field.Offset = CalculateOffset(member);
            field.Tooltip = tooltip;
            m_Fields.push_back(field);
        }

        const std::vector<ScriptField>& GetFields() const { return m_Fields; }

    private:
        std::vector<ScriptField> m_Fields;
    };

}
