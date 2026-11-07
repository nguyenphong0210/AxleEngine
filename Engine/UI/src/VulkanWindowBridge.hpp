#pragma once
// Checked native operations for the ImGui renderer bridge. Window presentation
// belongs to RHIPresentationContext and the pinned NRI swapchain.
#if defined(AXLE_HAS_VULKAN)
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <algorithm>
#include <limits>
#include <vector>

namespace Axle {
    template<class T> static T WindowVKProc(const char* name) {
        auto& rhi = RHI::RHIDevice::Get();
        const auto& vk = rhi.GetNRIWrapperVK();
        auto loader = reinterpret_cast<PFN_vkGetInstanceProcAddr>(vk.GetInstanceProcAddrVK(*rhi.GetNRIDevice()));
        auto function = reinterpret_cast<T>(loader(static_cast<VkInstance>(vk.GetInstanceVK(*rhi.GetNRIDevice())), name));
        if (!function) throw std::runtime_error(std::string("Missing Vulkan window operation: ") + name);
        return function;
    }
    template<class F> static void CheckedWindowVK(const char* name, F&& call) {
        VkResult native = VK_SUCCESS;
        auto result = RHI::ScopedRHIFault::Invoke(name, [&] {
            native = call();
            if (native == VK_ERROR_DEVICE_LOST) return nri::Result::DEVICE_LOST;
            return native == VK_SUCCESS ? nri::Result::SUCCESS : nri::Result::FAILURE;
        });
        if (result != nri::Result::SUCCESS)
            throw std::runtime_error(std::string(name) + " failed: " + std::to_string(native));
    }

}
#endif
