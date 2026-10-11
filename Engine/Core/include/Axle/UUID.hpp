#pragma once
#include <Axle/Types.hpp>

namespace Axle {

    class UUID {
    public:
        UUID();
        UUID(u64 uuid);
        UUID(const UUID&) = default;

        operator u64() const { return m_UUID; }

    private:
        u64 m_UUID;
    };

}

namespace std {
    template <typename T> struct hash;

    template<>
    struct hash<Axle::UUID> {
        size_t operator()(const Axle::UUID& uuid) const {
            return (size_t)(uint64_t)uuid;
        }
    };
}
