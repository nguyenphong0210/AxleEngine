#include <Axle/RHI/RHIDevice.hpp>
#include <cstring>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_3.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#endif

namespace Axle::RHI {
    nri::FormatSupportBits RHIDevice::GetTextureFormatSupport(nri::Format format) const {
        if (!IsNativeReady()) return nri::FormatSupportBits::UNSUPPORTED;
        auto support = m_NRI.Core.GetFormatSupport(*m_Device, format);
#if defined(_WIN32)
        // Pinned NRI queries the typed DSV format, while DescriptorD3D12 patches
        // depth SRVs to the corresponding sampled format. Query that view too.
        if (GetBackend() == RHIType::Direct3D12 &&
            (format == nri::Format::D32_SFLOAT || format == nri::Format::D24_UNORM_S8_UINT)) {
            auto* native = static_cast<ID3D12Device*>(m_NRI.Core.GetDeviceNativeObject(m_Device));
            D3D12_FEATURE_DATA_FORMAT_SUPPORT sampled{};
            sampled.Format = format == nri::Format::D32_SFLOAT ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            if (native && SUCCEEDED(native->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &sampled, sizeof(sampled))) &&
                (sampled.Support1 & (D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_SHADER_LOAD)))
                support |= nri::FormatSupportBits::TEXTURE;
        }
#endif
        return support;
    }

    bool RHIDevice::IsGraphicsAPIValidationAvailable(RHIType backend) {
#if defined(_WIN32)
        if (backend == RHIType::Vulkan) {
            const auto library = LoadLibraryW(L"vulkan-1.dll");
            if (!library) return false;
            const auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(GetProcAddress(library, "vkEnumerateInstanceLayerProperties"));
            uint32_t count = 0;
            bool found = false;
            if (enumerate && enumerate(&count, nullptr) == VK_SUCCESS) {
                std::vector<VkLayerProperties> layers(count);
                if (enumerate(&count, layers.data()) == VK_SUCCESS)
                    for (const auto& layer : layers)
                        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) found = true;
            }
            FreeLibrary(library);
            return found;
        }
        if (backend == RHIType::Direct3D12) {
            const auto d3d = LoadLibraryW(L"d3d12.dll");
            const auto dxgi = LoadLibraryW(L"dxgi.dll");
            auto debugInterface = d3d ? reinterpret_cast<decltype(&D3D12GetDebugInterface)>(GetProcAddress(d3d, "D3D12GetDebugInterface")) : nullptr;
            auto createFactory = dxgi ? reinterpret_cast<decltype(&CreateDXGIFactory2)>(GetProcAddress(dxgi, "CreateDXGIFactory2")) : nullptr;
            ID3D12Debug* debug = nullptr;
            IDXGIFactory2* factory = nullptr;
            const bool available = debugInterface && createFactory &&
                SUCCEEDED(debugInterface(IID_PPV_ARGS(&debug))) &&
                SUCCEEDED(createFactory(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)));
            if (factory) factory->Release();
            if (debug) debug->Release();
            if (dxgi) FreeLibrary(dxgi);
            if (d3d) FreeLibrary(d3d);
            return available;
        }
#endif
        return false;
    }
}
