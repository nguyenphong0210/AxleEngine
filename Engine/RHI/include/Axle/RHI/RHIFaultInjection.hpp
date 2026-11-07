#pragma once
#include <NRI.h>
#include <Axle/RHI/RHIStatus.hpp>
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
            nri::Result result = nri::Result::SUCCESS;
            bool injected = false;
            if (!IsRHIOperationAllowed(operation)) {
                result = nri::Result::FAILURE;
                injected = true;
            }
            if (s_Hook) {
                if (auto failure = s_Hook(operation); failure && *failure != nri::Result::SUCCESS) {
                    result = *failure;
                    injected = true;
                }
            }
            if (!injected) result = std::forward<F>(nativeCall)();
            ReportRHIFailure(result, operation, ScopedRHIOperation::Resource());
            return result;
        }
    private:
        inline static thread_local Hook s_Hook;
        Hook m_Previous;
    };
}
