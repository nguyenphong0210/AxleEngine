#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <entt/entt.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Axle {

    struct UnknownComponentSection {
        std::string SectionName;
        std::vector<std::string> Lines;
    };

    struct PassiveExtensionComponent {
        std::vector<UnknownComponentSection> Sections;
    };

    struct ComponentCopyContext {
        entt::registry& DstRegistry;
        entt::entity DstEntity;
        const entt::registry& SrcRegistry;
        entt::entity SrcEntity;
        const std::unordered_map<UUID, entt::entity>& UUIDMap;
    };

    class IComponentCodec {
    public:
        virtual ~IComponentCodec() = default;
        virtual std::string_view GetComponentName() const = 0;
        virtual bool HasComponent(const entt::registry& registry, entt::entity entity) const = 0;
        virtual void CopyComponent(const ComponentCopyContext& ctx) const = 0;
    };

    template<typename T, typename CopyFn>
    class FunctionalComponentCodec : public IComponentCodec {
    public:
        FunctionalComponentCodec(std::string_view name, CopyFn copyFn)
            : m_Name(name), m_CopyFn(std::move(copyFn)) {}

        std::string_view GetComponentName() const override { return m_Name; }

        bool HasComponent(const entt::registry& registry, entt::entity entity) const override {
            return registry.all_of<T>(entity);
        }

        void CopyComponent(const ComponentCopyContext& ctx) const override {
            m_CopyFn(ctx);
        }

    private:
        std::string m_Name;
        CopyFn m_CopyFn;
    };

    class ComponentCodecRegistry {
    public:
        static ComponentCodecRegistry& Get();

        void RegisterCodec(std::unique_ptr<IComponentCodec> codec);
        const std::vector<std::unique_ptr<IComponentCodec>>& GetCodecs() const { return m_Codecs; }

        void CopyAllComponents(const ComponentCopyContext& ctx) const {
            for (const auto& codec : m_Codecs) {
                if (codec->HasComponent(ctx.SrcRegistry, ctx.SrcEntity)) {
                    codec->CopyComponent(ctx);
                }
            }
        }

    private:
        ComponentCodecRegistry();
        void RegisterBuiltinCodecs();

        std::vector<std::unique_ptr<IComponentCodec>> m_Codecs;
    };

}
