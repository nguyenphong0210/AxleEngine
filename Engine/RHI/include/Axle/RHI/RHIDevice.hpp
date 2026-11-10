#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIStatus.hpp>
#include <Axle/RHI/RHIResource.hpp>

#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRIStreamer.h>
#include <Extensions/NRISwapChain.h>
#include <Extensions/NRIWrapperVK.h>

#include <memory>
#include <vector>
#include <functional>

namespace Axle::RHI {

    struct NRIInterfaces {
        nri::CoreInterface Core = {};
        nri::HelperInterface Helper = {};
        nri::StreamerInterface Streamer = {};
        nri::SwapChainInterface SwapChain = {};
        nri::WrapperVKInterface WrapperVK = {};
    };

    struct RHICompletionToken {
        uint64_t Epoch = 0;
        uint64_t Value = 0;
        explicit operator bool() const { return Epoch && Value; }
        bool operator==(const RHICompletionToken&) const = default;
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
        bool Shutdown();

        // Wait for all GPU queue operations to complete
        bool WaitIdle(uint32_t timeoutMilliseconds = 2000);
        bool WaitForFence(nri::Fence* fence, uint64_t value, uint32_t timeoutMilliseconds = 2000);
        bool Submit(const nri::QueueSubmitDesc& desc, nri::Queue* queue = nullptr, RHICompletionToken* completion = nullptr,
                    RHIRecordingLease externalUses = {});
        bool RetainExternalSubmission(RHIRecordingLease uses, RHICompletionToken* completion = nullptr);
        nri::Result UploadData(const nri::TextureUploadDesc* textures, uint32_t textureNum,
                        const nri::BufferUploadDesc* buffers, uint32_t bufferNum);
        bool WaitForCompletion(RHICompletionToken token, uint32_t timeoutMilliseconds = 2000);
        RHICompletionToken GetLastSubmittedToken() const { return {GetEpoch(), m_SubmittedValue}; }
        bool ObserveResult(nri::Result result, std::string_view operation, std::string_view resource = {}, bool timedOut = false);
        RHIStateSnapshot GetStateSnapshot() const;
        RHIDeviceState GetState() const { return GetStateSnapshot().State; }
        const std::optional<RHIGPUError>& GetFirstError() const { return m_FirstError; }
        bool CanSubmit() const { return IsNativeReady() && m_Resources.IsOwnerThread() && GetState() == RHIDeviceState::Ready; }
        bool CanCreateResources() const { return IsNativeReady() && m_Resources.IsOwnerThread() && (GetState() == RHIDeviceState::Ready || GetState() == RHIDeviceState::SurfaceRebuildRequired); }
        RHIResourceService& GetResources() { return m_Resources; }
        const RHIResourceService& GetResources() const { return m_Resources; }
        uint64_t GetEpoch() const { return m_Resources.GetEpoch(); }
        void SurfaceRebuilt() { if (GetState() == RHIDeviceState::SurfaceRebuildRequired) SetState(RHIDeviceState::Ready); }
        void ReleaseNative(std::function<void()> release);

        bool IsInitialized() const { return m_Initialized; }
        bool IsBackendSelected() const { return m_Initialized; }
        bool IsNativeReady() const { return m_Initialized && m_Device && m_GraphicsQueue; }
        bool IsOwnerThread() const { return m_Resources.IsOwnerThread(); }
        void MarkRenderWorkerOwnershipSupported() { if (IsOwnerThread()) m_Capabilities.SupportsRenderWorkerOwnership = true; }
        bool IsContextReady() const { return m_ContextReady; }
        // Set by product composition only after window and entry points are ready.
        void SetContextReady(bool ready) { m_ContextReady = m_Initialized && ready; }
        RHIType GetBackend() const { return m_Capabilities.ActiveBackend; }
        const RHIDeviceCapabilities& GetCapabilities() const { return m_Capabilities; }
        const std::vector<RHIAdapterInfo>& GetAdapters() const { return m_Adapters; }
        bool IsSoftwareAdapter() const { return m_Capabilities.IsSoftwareAdapter; }
        bool IsHeadless() const { return m_Capabilities.IsHeadless; }

        // Query physical adapters on the machine
        static std::vector<RHIAdapterInfo> EnumerateAdapters();
        static int32_t FindSoftwareAdapterIndex(RHIType backend = RHIType::None);
        static int32_t FindDiscreteAdapterIndex(RHIType backend = RHIType::None);
        static bool IsGraphicsAPIValidationAvailable(RHIType backend);
        nri::FormatSupportBits GetTextureFormatSupport(nri::Format format) const;

        // Access underlying NRI device and interfaces
        nri::Device* GetNRIDevice() const { return m_Device; }
        nri::Queue* GetGraphicsQueue() const { return m_GraphicsQueue; }
        const nri::CoreInterface& GetNRICore() const { return m_NRI.Core; }
        const nri::HelperInterface& GetNRIHelper() const { return m_NRI.Helper; }
        const nri::StreamerInterface& GetNRIStreamer() const { return m_NRI.Streamer; }
        const nri::SwapChainInterface& GetNRISwapChain() const { return m_NRI.SwapChain; }
        const nri::WrapperVKInterface& GetNRIWrapperVK() const { return m_NRI.WrapperVK; }

    private:
        friend class RHITransientResources;
        static constexpr uint64_t PackState(RHIDeviceState state, uint64_t epoch) {
            return (epoch << 8u) | (static_cast<uint64_t>(state) & 0xffu);
        }
        static constexpr RHIStateSnapshot UnpackState(uint64_t packed) {
            return {static_cast<RHIDeviceState>(packed & 0xffu), packed >> 8u};
        }
        void SetState(RHIDeviceState state);
        bool TryInitNRI(RHIType backend, const RHIDeviceDesc& desc, const nri::AdapterDesc* adapterDesc);
        void InitOpenGLFallback();
        void PopulateCapabilities();

    private:
        bool m_Initialized = false;
        bool m_ContextReady = false;
        std::atomic<uint64_t> m_StateSnapshot{PackState(RHIDeviceState::Uninitialized, 1)};
        std::optional<RHIGPUError> m_FirstError;
        nri::Fence* m_IdleFence = nullptr;
        uint64_t m_IdleValue = 0;
        bool m_UnresolvedWork = false;
        std::vector<std::function<void()>> m_DeferredCleanup;
        RHIDeviceDesc m_Desc;
        RHIDeviceCapabilities m_Capabilities;
        std::vector<RHIAdapterInfo> m_Adapters;

        nri::Device* m_Device = nullptr;
        nri::Queue* m_GraphicsQueue = nullptr;
        NRIInterfaces m_NRI;
        RHIResourceService m_Resources;
        struct PendingSubmission { RHICompletionToken Token; RHIRecordingLease Uses; };
        nri::Fence* m_SubmissionFence = nullptr;
        uint64_t m_SubmittedValue = 0;
        std::vector<PendingSubmission> m_PendingSubmissions;
        std::vector<RHIRecordingLease> m_AmbiguousSubmissions;
    };

}
