#pragma once
// Checked construction for the current native ImGui presentation adapter.
// Vendor example helpers continue after allocation failure and cannot establish
// product readiness. Every object below is published into zero-initialized owner
// storage before the next operation; cleanup also accepts partial construction.
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
            return native == VK_SUCCESS ? nri::Result::SUCCESS : nri::Result::FAILURE;
        });
        if (result != nri::Result::SUCCESS)
            throw std::runtime_error(std::string(name) + " failed: " + std::to_string(native));
    }

    static void DestroyWindowVK(VkDevice device, ImGui_ImplVulkanH_Window& window) {
#define DESTROY_VK(type, handle) if (handle) WindowVKProc<PFN_vkDestroy##type>("vkDestroy" #type)(device, handle, nullptr)
        for (auto& frame : window.Frames) {
            DESTROY_VK(Fence, frame.Fence);
            // Destroying the pool also releases any allocated command buffers.
            DESTROY_VK(CommandPool, frame.CommandPool);
            DESTROY_VK(Framebuffer, frame.Framebuffer);
            DESTROY_VK(ImageView, frame.BackbufferView);
        }
        for (auto& semaphores : window.FrameSemaphores) {
            DESTROY_VK(Semaphore, semaphores.ImageAcquiredSemaphore);
            DESTROY_VK(Semaphore, semaphores.RenderCompleteSemaphore);
        }
        DESTROY_VK(RenderPass, window.RenderPass);
        DESTROY_VK(SwapchainKHR, window.Swapchain);
#undef DESTROY_VK
        window.Frames.clear(); window.FrameSemaphores.clear();
        window.RenderPass = VK_NULL_HANDLE; window.Swapchain = VK_NULL_HANDLE;
        window.ImageCount = window.SemaphoreCount = window.FrameIndex = window.SemaphoreIndex = 0;
    }

    static void CreateWindowVK(VkPhysicalDevice physical, VkDevice device,
        ImGui_ImplVulkanH_Window& window, uint32_t family, int width, int height, uint32_t minimum) {
#define VK_OP(name, ...) CheckedWindowVK(#name, [&] { return WindowVKProc<PFN_##name>(#name)(__VA_ARGS__); })
        VkBool32 present = VK_FALSE;
        VK_OP(vkGetPhysicalDeviceSurfaceSupportKHR, physical, family, window.Surface, &present);
        if (!present) throw std::runtime_error("Graphics queue cannot present to this surface");
        VkSurfaceCapabilitiesKHR caps{};
        VK_OP(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, physical, window.Surface, &caps);
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) throw std::runtime_error("Surface does not support color attachments");
        VkSwapchainCreateInfoKHR swap{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        swap.surface = window.Surface;
        swap.minImageCount = std::max(minimum, caps.minImageCount);
        if (caps.maxImageCount) swap.minImageCount = std::min(swap.minImageCount, caps.maxImageCount);
        swap.imageFormat = window.SurfaceFormat.format; swap.imageColorSpace = window.SurfaceFormat.colorSpace;
        swap.imageExtent = caps.currentExtent;
        if (swap.imageExtent.width == std::numeric_limits<uint32_t>::max()) {
            swap.imageExtent.width = std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
            swap.imageExtent.height = std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (!swap.imageExtent.width || !swap.imageExtent.height) throw std::runtime_error("Surface has zero extent");
        window.Width = swap.imageExtent.width; window.Height = swap.imageExtent.height;
        swap.imageArrayLayers = 1; swap.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swap.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; swap.preTransform = caps.currentTransform;
        for (auto mode : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (caps.supportedCompositeAlpha & mode) { swap.compositeAlpha = mode; break; }
        swap.presentMode = window.PresentMode; swap.clipped = VK_TRUE;
        VK_OP(vkCreateSwapchainKHR, device, &swap, nullptr, &window.Swapchain);
        uint32_t count = 0;
        VK_OP(vkGetSwapchainImagesKHR, device, window.Swapchain, &count, nullptr);
        if (count < minimum || count > 32) throw std::runtime_error("Unsupported surface image count");
        std::vector<VkImage> images(count);
        VK_OP(vkGetSwapchainImagesKHR, device, window.Swapchain, &count, images.data());
        window.Frames.resize(count); window.FrameSemaphores.resize(count + 1);
        memset(window.Frames.Data, 0, window.Frames.size_in_bytes());
        memset(window.FrameSemaphores.Data, 0, window.FrameSemaphores.size_in_bytes());
        window.ImageCount = count; window.SemaphoreCount = count + 1;
        auto attachment = window.AttachmentDesc; attachment.format = window.SurfaceFormat.format;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color;
        VkSubpassDependency dependency{}; dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass.attachmentCount = pass.subpassCount = pass.dependencyCount = 1;
        pass.pAttachments = &attachment; pass.pSubpasses = &subpass; pass.pDependencies = &dependency;
        VK_OP(vkCreateRenderPass, device, &pass, nullptr, &window.RenderPass);
        for (uint32_t i = 0; i < count; ++i) {
            auto& frame = window.Frames[i]; frame.Backbuffer = images[i];
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = window.SurfaceFormat.format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_OP(vkCreateImageView, device, &view, nullptr, &frame.BackbufferView);
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb.renderPass = window.RenderPass; fb.attachmentCount = 1; fb.pAttachments = &frame.BackbufferView;
            fb.width = window.Width; fb.height = window.Height; fb.layers = 1;
            VK_OP(vkCreateFramebuffer, device, &fb, nullptr, &frame.Framebuffer);
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex = family;
            VK_OP(vkCreateCommandPool, device, &pool, nullptr, &frame.CommandPool);
            VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            commands.commandPool = frame.CommandPool; commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commands.commandBufferCount = 1;
            VK_OP(vkAllocateCommandBuffers, device, &commands, &frame.CommandBuffer);
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            VK_OP(vkCreateFence, device, &fence, nullptr, &frame.Fence);
        }
        for (auto& semaphores : window.FrameSemaphores) {
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VK_OP(vkCreateSemaphore, device, &semaphore, nullptr, &semaphores.ImageAcquiredSemaphore);
            VK_OP(vkCreateSemaphore, device, &semaphore, nullptr, &semaphores.RenderCompleteSemaphore);
        }
#undef VK_OP
    }
}
#endif
