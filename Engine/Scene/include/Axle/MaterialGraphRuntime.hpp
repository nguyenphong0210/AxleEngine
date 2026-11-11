#pragma once
#include <Axle/MaterialGraphProgram.hpp>
#include <Axle/CustomMaterialProgram.hpp>
#include <deque>
#include <map>
#include <span>

namespace Axle {
    // Overrides retain authored, case-sensitive parameter names, never mg_pN symbols.
    struct MaterialGraphInstance {
        std::map<std::string, MaterialValue, std::less<>> Overrides;
        bool Resolve(const CompiledMaterialGraphProgram& program, std::vector<ShaderPropertyInstance>& properties,
                     MaterialGraphDiagnostic* error = nullptr) const;
    };

    // Atomic in-memory authoring transaction. All supported registry parameter
    // references and every supplied instance are updated together, or none are.
    // Disk persistence and discovering instances in projects are caller responsibilities.
    bool RenameMaterialGraphParameter(MaterialGraphDocument& document, std::string_view oldName, std::string_view newName,
                                     std::span<MaterialGraphInstance> instances, MaterialGraphDiagnostic* error = nullptr,
                                     const MaterialNodeRegistry& definitions = MaterialNodeRegistry::Builtins());

    struct MaterialGraphTarget {
        RHI::RHIShaderFormat Format = RHI::RHIShaderFormat::DXIL;
        // Native targets require a physical/explicitly selected active device.
        // CPU compilation is device-independent when Native is false.
        bool Native = false;
        nri::Format Color = nri::Format::RGBA8_UNORM;
        nri::Format Depth = nri::Format::D32_SFLOAT;
        uint8_t Samples = 1;
    };

    struct MaterialGraphReadyGeneration {
        uint64_t ID = 0;
        uint64_t DeviceEpoch = 0;
        CompiledMaterialGraphProgram Program;
        MaterialGraphTarget Target;
        std::string PipelineKey;
        std::shared_ptr<const CustomReadyGeneration> Native;
    };

    struct MaterialGraphRuntimeStats {
        uint64_t ShaderCompiles = 0; // One VS+PS attempt, not a DXC disk-cache count.
        uint64_t ShaderCacheHits = 0;
        uint64_t LayoutBuilds = 0;
    };

    // Explicitly owned, synchronous, single-thread owner. No file watcher/global
    // registration. Hold shared immutable generations while recording/submitting.
    // RHI leases protect submitted native resources; stale device epochs cannot draw.
    class MaterialGraphRuntime {
    public:
        MaterialGraphRuntime(uint64_t assetIdentity, MaterialGraphTarget target, size_t shaderCapacity = 16);
        bool Reload(std::string_view source, const MaterialNodeRegistry& definitions = MaterialNodeRegistry::Builtins(),
                    const MaterialHLSLRegistry& lowerings = MaterialHLSLRegistry::Builtins());
        bool Reload(const MaterialGraphDocument& document, const MaterialNodeRegistry& definitions = MaterialNodeRegistry::Builtins(),
                    const MaterialHLSLRegistry& lowerings = MaterialHLSLRegistry::Builtins());
        const std::shared_ptr<const MaterialGraphReadyGeneration>& Ready() const { return m_Ready; }
        const MaterialGraphDiagnostic& Diagnostic() const { return m_Diagnostic; }
        const MaterialGraphRuntimeStats& Stats() const { return m_Stats; }
        size_t ShaderCacheSize() const { return m_Shaders.size(); }
        void Clear();
        // Returns an RHI-cache pipeline for immediate recording; never retain the raw
        // pointer across cache eviction/device reset. Commands retain native leases.
        static nri::Pipeline* GetPipeline(const MaterialGraphReadyGeneration& generation);
    private:
        struct ShaderCode {
            RHI::RHIShaderBytecode Vertex, Fragment;
            RHI::RHIShaderReflection Reflection;
        };
        uint64_t m_Identity;
        MaterialGraphTarget m_Target;
        size_t m_ShaderCapacity;
        std::map<std::string, ShaderCode> m_Shaders;
        std::deque<std::string> m_ShaderOrder;
        std::shared_ptr<const MaterialGraphReadyGeneration> m_Ready;
        MaterialGraphDiagnostic m_Diagnostic;
        MaterialGraphRuntimeStats m_Stats;
    };
}
