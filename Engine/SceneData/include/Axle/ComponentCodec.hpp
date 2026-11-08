#pragma once
#include <Axle/Types.hpp>
#include <Axle/UUID.hpp>
#include <entt/entt.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>

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
        using Owner = std::uint64_t;

        static ComponentCodecRegistry& Get();

        Owner BeginOwner() { const auto owner = m_NextOwner++; m_ActiveOwners.insert(owner); return owner; }
        bool RegisterCodec(std::unique_ptr<IComponentCodec> codec);
        bool RegisterCodec(Owner owner, std::unique_ptr<IComponentCodec> codec,
                           std::shared_ptr<void> moduleLease = {});
        void UnregisterOwner(Owner owner) noexcept;

        struct Registration {
            Owner OwnerId;
            // Destroy the codec before releasing its executable code.
            std::shared_ptr<void> ModuleLease;
            std::unique_ptr<IComponentCodec> Codec;
            Registration(Owner owner, std::shared_ptr<void> lease, std::unique_ptr<IComponentCodec> codec)
                : OwnerId(owner), ModuleLease(std::move(lease)), Codec(std::move(codec)) {}
            Registration(Registration&&) noexcept = default;
            Registration& operator=(Registration&& other) noexcept {
                if (this != &other) {
                    Codec.reset();
                    ModuleLease = std::move(other.ModuleLease);
                    Codec = std::move(other.Codec);
                    OwnerId = other.OwnerId;
                }
                return *this;
            }
        };

        void CopyAllComponents(const ComponentCopyContext& ctx) const;
        bool HasCodec(std::string_view componentName) const;
        std::size_t Size() const noexcept { return m_Codecs.size(); }

    private:
        ComponentCodecRegistry();
        ~ComponentCodecRegistry();
        void RegisterBuiltinCodecs();

        std::uint64_t m_NextOwner = 1;
        std::vector<std::shared_ptr<Registration>> m_Codecs;
        std::unordered_set<Owner> m_ActiveOwners;
        bool m_ShuttingDown = false;
    };

    void RegisterRuntimeComponentCodecs();

}
