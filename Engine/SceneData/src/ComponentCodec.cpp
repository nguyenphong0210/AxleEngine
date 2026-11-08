#include <Axle/ComponentCodec.hpp>
#include <Axle/DataComponents.hpp>
#include <algorithm>
namespace Axle {
    ComponentCodecRegistry& ComponentCodecRegistry::Get() {
        static ComponentCodecRegistry instance;
        return instance;
    }
    ComponentCodecRegistry::ComponentCodecRegistry() {
        RegisterBuiltinCodecs();
    }
    ComponentCodecRegistry::~ComponentCodecRegistry() {
        m_ShuttingDown = true;
        m_ActiveOwners.clear();
        while (!m_Codecs.empty()) {
            auto detached = std::move(m_Codecs.back());
            m_Codecs.pop_back();
            detached.reset();
        }
    }
    bool ComponentCodecRegistry::RegisterCodec(std::unique_ptr<IComponentCodec> codec) {
        return RegisterCodec(0, std::move(codec));
    }
    bool ComponentCodecRegistry::RegisterCodec(Owner owner, std::unique_ptr<IComponentCodec> codec,
                                               std::shared_ptr<void> moduleLease) {
        Registration registration(owner, std::move(moduleLease), std::move(codec));
        const auto active = [&] { return !m_ShuttingDown && (owner == 0 || m_ActiveOwners.contains(owner)); };
        if (!registration.Codec || !active()) return false;
        const std::string name(registration.Codec->GetComponentName());
        if (!active() || name.empty() || HasCodec(name) || !active()) return false;
        m_Codecs.push_back(std::make_shared<Registration>(std::move(registration)));
        return true;
    }
    void ComponentCodecRegistry::UnregisterOwner(Owner owner) noexcept {
        if (owner == 0) return;
        m_ActiveOwners.erase(owner);
        for (;;) {
            auto it = std::find_if(m_Codecs.begin(), m_Codecs.end(), [owner](const auto& entry) { return entry->OwnerId == owner; });
            if (it == m_Codecs.end()) break;
            auto detached = std::move(*it);
            m_Codecs.erase(it);
            // No vector mutation or iterator remains active across DLL code.
            // Destructors may unregister another owner or add a live owner.
            detached.reset();
        }
    }
    void ComponentCodecRegistry::CopyAllComponents(const ComponentCopyContext& ctx) const {
        const auto snapshot = m_Codecs;
        for (const auto& registration : snapshot) {
            const auto& codec = registration->Codec;
            const auto active = [&] { return !m_ShuttingDown && (registration->OwnerId == 0 || m_ActiveOwners.contains(registration->OwnerId)); };
            if (codec && active() && codec->HasComponent(ctx.SrcRegistry, ctx.SrcEntity) && active()) codec->CopyComponent(ctx);
        }
    }
    bool ComponentCodecRegistry::HasCodec(std::string_view componentName) const {
        const auto snapshot = m_Codecs;
        for (const auto& registration : snapshot) {
            const auto& codec = registration->Codec;
            const auto active = [&] { return !m_ShuttingDown && (registration->OwnerId == 0 || m_ActiveOwners.contains(registration->OwnerId)); };
            if (codec && active() && codec->GetComponentName() == componentName && active()) return true;
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
