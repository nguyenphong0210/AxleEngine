#pragma once
#include <Axle/RHI/RHICommon.hpp>

#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRIStreamer.h>

#include <memory>
#include <vector>

namespace Axle::RHI {

    struct NRIInterfaces {
        nri::CoreInterface Core = {};
        nri::HelperInterface Helper = {};
        nri::StreamerInterface Streamer = {};
    };

    class RHIDevice {
    public:
        static RHIDevice& Get();

        RHIDevice() = default;
        ~RHIDevice();

        // Prevent copying
        RHIDevice(const RHIDevice&) = delete;
        RHIDevice& operator=(const RHIDevice&) = delete;

        // Initialize RHI Device with preferred backend and fallback chain
        bool Init(const RHIDeviceDesc& desc = RHIDeviceDesc());

        // Graceful shutdown
        void Shutdown();

        bool IsInitialized() const { return m_Initialized; }
        RHIType GetBackend() const { return m_Capabilities.ActiveBackend; }
        const RHIDeviceCapabilities& GetCapabilities() const { return m_Capabilities; }
        const std::vector<RHIAdapterInfo>& GetAdapters() const { return m_Adapters; }

        // Query physical adapters on the machine
        static std::vector<RHIAdapterInfo> EnumerateAdapters();

        // Access underlying NRI device and interfaces
        nri::Device* GetNRIDevice() const { return m_Device; }
        const nri::CoreInterface& GetNRICore() const { return m_NRI.Core; }
        const nri::HelperInterface& GetNRIHelper() const { return m_NRI.Helper; }
        const nri::StreamerInterface& GetNRIStreamer() const { return m_NRI.Streamer; }

    private:
        bool TryInitNRI(RHIType backend, const RHIDeviceDesc& desc, const nri::AdapterDesc* adapterDesc);
        void InitOpenGLFallback();
        void PopulateCapabilities();

    private:
        bool m_Initialized = false;
        RHIDeviceDesc m_Desc;
        RHIDeviceCapabilities m_Capabilities;
        std::vector<RHIAdapterInfo> m_Adapters;

        nri::Device* m_Device = nullptr;
        NRIInterfaces m_NRI;
    };

}
