#include <Axle/ComponentCodec.hpp>
#include <Axle/DataComponents.hpp>
namespace Axle {
    ComponentCodecRegistry& ComponentCodecRegistry::Get() {
        static ComponentCodecRegistry instance;
        return instance;
    }
    ComponentCodecRegistry::ComponentCodecRegistry() {
        RegisterBuiltinCodecs();
    }
    void ComponentCodecRegistry::RegisterCodec(std::unique_ptr<IComponentCodec> codec) {
        RegisterCodec(0, std::move(codec));
    }
    void ComponentCodecRegistry::RegisterCodec(Owner owner, std::unique_ptr<IComponentCodec> codec) {
        if (codec) {
            while (m_Owners.size() < m_Codecs.size()) {
                m_Owners.push_back(0);
            }
            m_Codecs.push_back(std::move(codec));
            m_Owners.push_back(owner);
        }
    }
    void ComponentCodecRegistry::UnregisterOwner(Owner owner) noexcept {
        if (owner == 0) return;
        while (m_Owners.size() < m_Codecs.size()) {
            m_Owners.push_back(0);
        }
        for (size_t i = 0; i < m_Codecs.size();) {
            if (i < m_Owners.size() && m_Owners[i] == owner) {
                m_Codecs.erase(m_Codecs.begin() + i);
                m_Owners.erase(m_Owners.begin() + i);
            } else {
                ++i;
            }
        }
    }
    void ComponentCodecRegistry::CopyAllComponents(const ComponentCopyContext& ctx) const {
        for (const auto& codec : m_Codecs) {
            if (codec && codec->HasComponent(ctx.SrcRegistry, ctx.SrcEntity)) {
                codec->CopyComponent(ctx);
            }
        }
    }
    bool ComponentCodecRegistry::HasCodec(std::string_view componentName) const {
        for (const auto& codec : m_Codecs) {
            if (codec && codec->GetComponentName() == componentName) return true;
        }
        return false;
    }
    void ComponentCodecRegistry::RegisterBuiltinCodecs() {
        RegisterCodec(std::make_unique<FunctionalComponentCodec<TransformComponent, void(*)(const ComponentCopyContext&)>>(
            "TransformComponent", [](const ComponentCopyContext& ctx) {
                ctx.DstRegistry.emplace_or_replace<TransformComponent>(ctx.DstEntity, ctx.SrcRegistry.get<TransformComponent>(ctx.SrcEntity));
            }));
        RegisterCodec(std::make_unique<FunctionalComponentCodec<PassiveExtensionComponent, void(*)(const ComponentCopyContext&)>>(
            "PassiveExtensionComponent", [](const ComponentCopyContext& ctx) {
                ctx.DstRegistry.emplace_or_replace<PassiveExtensionComponent>(ctx.DstEntity, ctx.SrcRegistry.get<PassiveExtensionComponent>(ctx.SrcEntity));
            }));
    }
}
