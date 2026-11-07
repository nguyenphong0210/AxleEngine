#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <NRI.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Axle::RHI {
    enum class RHIDeviceState { Uninitialized, Ready, SurfaceRebuildRequired, Lost, Stopping };

    struct RHIGPUError {
        nri::Result NativeResult = nri::Result::SUCCESS;
        RHIType Backend = RHIType::None;
        std::string Operation;
        std::string Resource;
        bool SubmissionAmbiguous = false;
        bool TimedOut = false;
    };

    // Operation scopes attach the caller's resource/pass to a native failure.
    class ScopedRHIOperation final {
    public:
        explicit ScopedRHIOperation(std::string_view resource) : m_Previous(std::exchange(s_Resource, resource)) {}
        ~ScopedRHIOperation() { s_Resource = m_Previous; }
        ScopedRHIOperation(const ScopedRHIOperation&) = delete;
        ScopedRHIOperation& operator=(const ScopedRHIOperation&) = delete;
        static std::string_view Resource() { return s_Resource; }
    private:
        inline static thread_local std::string_view s_Resource;
        std::string_view m_Previous;
    };

    void ReportRHIFailure(nri::Result result, std::string_view operation, std::string_view resource);
    bool IsRHIOperationAllowed(std::string_view operation);
}
