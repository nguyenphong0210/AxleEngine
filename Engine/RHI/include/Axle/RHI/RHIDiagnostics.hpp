#pragma once
#include <cstdint>

namespace Axle::RHI {
    // Process-wide, including startup rollback and asynchronous native callbacks.
    uint64_t GetRHIValidationErrorCount() noexcept;
}
