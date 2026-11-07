#pragma once
#include <NRI.h>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

namespace Axle::RHI {
    // Scoped, owner-thread fault injection at real native operation boundaries.
    // Returning no result executes the native operation normally.
    class ScopedRHIFault final {
    public:
        using Hook = std::function<std::optional<nri::Result>(std::string_view)>;
        explicit ScopedRHIFault(Hook hook) : m_Previous(std::exchange(s_Hook, std::move(hook))) {}
        ~ScopedRHIFault() { s_Hook = std::move(m_Previous); }
        ScopedRHIFault(const ScopedRHIFault&) = delete;
        ScopedRHIFault& operator=(const ScopedRHIFault&) = delete;
        template<class F> static nri::Result Invoke(std::string_view operation, F&& nativeCall) {
            if (s_Hook) {
                if (auto failure = s_Hook(operation); failure && *failure != nri::Result::SUCCESS)
                    return *failure;
            }
            return std::forward<F>(nativeCall)();
        }
    private:
        inline static thread_local Hook s_Hook;
        Hook m_Previous;
    };
}
