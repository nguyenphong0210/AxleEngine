#pragma once
#include <Axle/RHI/RHIShader.hpp>
#include <memory>

namespace Axle::RHI {
    // Optional, synchronous graph-profile compiler. No Slang headers or mandatory
    // DLL dependency escape this API. Instances/compilation require one owner thread.
    // SDK modules are process-resident after first use; this is not plugin unloading.
    class RHISlangCompiler {
    public:
        // An explicit path can probe unavailable deployments; a usable compiler
        // must match the configured SDK (multiple SDK ABIs cannot share imports).
        explicit RHISlangCompiler(std::filesystem::path library = {});
        ~RHISlangCompiler();
        RHISlangCompiler(const RHISlangCompiler&) = delete;
        RHISlangCompiler& operator=(const RHISlangCompiler&) = delete;
        static RHISlangCompiler& Get();
        static bool IsEnabled();
        bool IsAvailable(std::string* error = nullptr);
        std::string Identity();
        // Bounded HLSL graph profile: VS/PS, SM6_0, DXIL/SPIR-V, column-major
        // matrices and DX buffer packing. Unsupported options fail explicitly.
        RHIShaderCompileResult Compile(const RHIShaderCompileDesc& description);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
