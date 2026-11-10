#pragma once

#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <memory>
#include <variant>

namespace Axle::RHI {
    using RHITransientDesc = std::variant<RHITextureDesc, RHIBufferDesc>;

    struct RHITransientRequirements {
        uint64_t SizeBytes = 0, Alignment = 1;
        uint32_t MemoryType = 0;
        bool MustBeDedicated = false;
    };

    // A snapshot owns its exact generation. No memory handles escape the factory.
    struct RHITransientView {
        RHITransientDesc Description;
        RHIResourceLease Lease;
        nri::Texture* Texture = nullptr;
        nri::Buffer* Buffer = nullptr;
        nri::Descriptor* SRV = nullptr;
        nri::Descriptor* RTV = nullptr;
        nri::Descriptor* DSV = nullptr;
        nri::Descriptor* CBV = nullptr;
        bool IsLive() const { return Lease.IsLive(); }
    };
    RHITransientView MakeTransientView(const RHITexture& texture);
    RHITransientView MakeTransientView(const RHIBuffer& buffer);
    bool SameTransientDescription(const RHITransientDesc& a, const RHITransientDesc& b);
    bool ValidateTransientView(const RHITransientView& view);

    // Render-owner factory. Replacement is transactional; all native requirements
    // are queried from the created objects, not trusted from a CPU graph estimate.
    class RHITransientResources {
    public:
        RHITransientResources();
        ~RHITransientResources();
        RHITransientResources(RHITransientResources&&) noexcept;
        RHITransientResources& operator=(RHITransientResources&&) noexcept;
        RHITransientResources(const RHITransientResources&) = delete;
        RHITransientResources& operator=(const RHITransientResources&) = delete;
        bool InitCommitted(const std::vector<RHITransientDesc>& descriptions);
        static bool QueryRequirements(const RHITransientDesc& description, RHITransientRequirements& requirements);
        // Optional block IDs allow explicit overlap. The caller owns the lifetime
        // and alias-barrier proof. Shared blocks require identical descriptions
        // and native requirements, no dedicated allocations, and device support.
        bool InitPlaced(const std::vector<RHITransientDesc>& descriptions, const std::vector<uint32_t>& blocks = {});
        const RHITransientView* Get(uint32_t index) const;
        const RHITransientRequirements* GetRequirements(uint32_t index) const;
        uint64_t GetAllocatedBytes() const;
    private:
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };
}
