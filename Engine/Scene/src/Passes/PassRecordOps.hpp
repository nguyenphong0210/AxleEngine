#pragma once

#include <Axle/RHI/RHIBuffer.hpp>
#include <functional>
#include <string_view>

namespace Axle::Passes {
    // Borrowed frame operations. The renderer owns graph order, diagnostics and
    // command lifetime; an extracted pass may use these only during Record.
    struct PassRecordOps {
        std::function<bool(std::string_view)> BeginPass;
        std::function<RHI::RHIBuffer&()> Uniform;
        std::function<bool(uint32_t, nri::DescriptorSet*, nri::BindPoint)> BindDescriptor;
        std::function<void(const nri::RenderingDesc&)> BeginRendering;
        std::function<void()> EndRendering;
    };
}
