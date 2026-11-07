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
        if (codec) m_Codecs.push_back(std::move(codec));
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
