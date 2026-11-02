#pragma once
#include <Axle/Types.hpp>
#include <Axle/MaterialBindingPlan.hpp>
#include <Axle/CustomShaderInterface.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <filesystem>

namespace Axle {

    struct CustomProgramKey {
        std::string ShaderPath;
        std::string EntryPointVS = "VSMain";
        std::string EntryPointPS = "PSMain";
        RHI::RHIType Backend = RHI::RHIType::Direct3D12;
        uint32_t InterfaceRevision = 1;

        bool operator==(const CustomProgramKey& other) const = default;
    };

    struct CustomProgramKeyHash {
        size_t operator()(const CustomProgramKey& k) const noexcept {
            size_t h = std::hash<std::string>{}(k.ShaderPath);
            h ^= std::hash<std::string>{}(k.EntryPointVS) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::string>{}(k.EntryPointPS) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(static_cast<int>(k.Backend)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(k.InterfaceRevision) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct CustomReadyGeneration {
        uint64_t GenerationID = 0;
        std::string DependencyFingerprint;
        MaterialBindingPlan BindingPlan;
        RHI::RHIShaderBytecode VSBytecode;
        RHI::RHIShaderBytecode PSBytecode;
        RHI::RHIShaderReflection Reflection;
        // Own range storage referenced by the native layout's descriptor descriptions.
        RHI::RHIAutoPipelineLayout LayoutDescription;

        // Shared ownership of pipeline layout if backend device is active
        std::shared_ptr<RHI::RHIPipelineLayout> PipelineLayout;

        // Lifetime tracker for CPU testing observable destruction
        std::shared_ptr<int> LifetimeTracker;
    };

    nri::Pipeline* CreateCustomMaterialPipeline(const CustomReadyGeneration& generation,
        nri::Format colorFormat, nri::Format depthFormat, uint8_t samples,
        SurfaceType surface, CullMode cull, bool depthWrite);

    enum class ProgramSelectionKind : uint8_t {
        BuiltIn = 0,
        Custom
    };

    struct SelectedProgram {
        ProgramSelectionKind Kind = ProgramSelectionKind::BuiltIn;
        std::shared_ptr<const CustomReadyGeneration> Generation;
        std::string Diagnostic;
    };

    struct CandidateBuildResult {
        bool Success = false;
        std::shared_ptr<CustomReadyGeneration> Generation;
        std::string Diagnostic;
        std::vector<std::filesystem::path> Dependencies;
    };

    using CandidateBuilderFunc = std::function<CandidateBuildResult(const CustomProgramKey& key, const std::string& fingerprint)>;

    struct ProgramEntry {
        std::string LastAttemptFingerprint;
        std::shared_ptr<const CustomReadyGeneration> ReadyGeneration;
        std::string Diagnostic;
        std::vector<std::filesystem::path> Dependencies;
    };

    class CustomProgramCache {
    public:
        CustomProgramCache();
        ~CustomProgramCache() = default;

        // Set injected candidate builder (for CPU testing or RHI compilation hook)
        void SetCandidateBuilder(CandidateBuilderFunc builder) { m_CandidateBuilder = std::move(builder); }

        // Compute fingerprint for shader file and its dependencies
        static std::string ComputeFingerprint(const std::filesystem::path& shaderPath,
            const std::vector<std::filesystem::path>& dependencies = {});
        std::string ComputeFingerprint(const CustomProgramKey& key) const;

        // Refresh cache entry for the key
        ProgramEntry& RefreshEntry(const CustomProgramKey& key, const std::string& fingerprint);

        // Program selection decision according to guide matrix
        SelectedProgram SelectProgram(bool useCustomShader, const std::string& shaderPath, const CustomProgramKey& key, const std::string& fingerprint = "");

        const ProgramEntry* FindEntry(const CustomProgramKey& key) const;

        void Clear();
        size_t GetEntryCount() const { return m_Entries.size(); }

        static uint64_t AllocateGenerationID() { return s_NextGenerationID.fetch_add(1); }

    private:
        CandidateBuildResult DefaultBuildCandidate(const CustomProgramKey& key, const std::string& fingerprint);

    private:
        std::unordered_map<CustomProgramKey, ProgramEntry, CustomProgramKeyHash> m_Entries;
        CandidateBuilderFunc m_CandidateBuilder;
        static inline std::atomic<uint64_t> s_NextGenerationID{ 1 };
    };

} // namespace Axle
