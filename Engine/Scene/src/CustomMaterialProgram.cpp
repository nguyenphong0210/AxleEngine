#include <Axle/CustomMaterialProgram.hpp>
#include <Axle/FileSystem.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace Axle {

    CustomProgramCache::CustomProgramCache() {
        m_CandidateBuilder = [this](const CustomProgramKey& key, const std::string& fingerprint) {
            return DefaultBuildCandidate(key, fingerprint);
        };
    }

    std::string CustomProgramCache::ComputeFingerprint(const std::filesystem::path& shaderPath) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(shaderPath, ec)) {
            return "";
        }

        auto dependencies=RHI::RHIShaderCompiler::ExtractIncludeDependencies(shaderPath);
        dependencies.push_back(shaderPath);
        std::sort(dependencies.begin(),dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(),dependencies.end()),dependencies.end());
        // Include identities and contents, so same-size edits and include removal invalidate.
        uint64_t hash = 14695981039346656037ull;
        for (const auto& dependency : dependencies) {
            std::ifstream file(dependency,std::ios::binary);
            if (!file) return "";
            const auto identity=std::filesystem::absolute(dependency,ec).lexically_normal().generic_string();
            for (unsigned char c : identity) { hash^=c; hash*=1099511628211ull; }
            char c;
            while(file.get(c)) { hash^=static_cast<unsigned char>(c); hash*=1099511628211ull; }
            hash^=0xff; hash*=1099511628211ull;
        }
        return std::to_string(hash);
    }

    ProgramEntry& CustomProgramCache::RefreshEntry(const CustomProgramKey& key, const std::string& fingerprint) {
        auto& entry = m_Entries[key];

        // Unchanged fingerprint suppresses duplicate compilation and duplicate diagnostic logging
        if (!fingerprint.empty() && fingerprint == entry.LastAttemptFingerprint) {
            return entry;
        }

        if (!m_CandidateBuilder) {
            entry.Diagnostic = "No candidate builder configured.";
            return entry;
        }

        CandidateBuildResult candidate = m_CandidateBuilder(key, fingerprint);
        entry.LastAttemptFingerprint = fingerprint;

        if (candidate.Success && candidate.Generation) {
            if (candidate.Generation->GenerationID == 0) {
                candidate.Generation->GenerationID = AllocateGenerationID();
            }
            candidate.Generation->DependencyFingerprint = fingerprint;
            entry.ReadyGeneration = candidate.Generation;
            entry.Diagnostic.clear();
        } else {
            entry.Diagnostic = candidate.Diagnostic.empty() ? "Shader compilation or layout creation failed." : candidate.Diagnostic;
            // Retain last known good readyGeneration!
        }

        return entry;
    }

    SelectedProgram CustomProgramCache::SelectProgram(
        bool useCustomShader,
        const std::string& shaderPath,
        const CustomProgramKey& key,
        const std::string& fingerprint)
    {
        if (!useCustomShader) {
            return SelectedProgram{ ProgramSelectionKind::BuiltIn, nullptr, "" };
        }

        if (shaderPath.empty()) {
            return SelectedProgram{ ProgramSelectionKind::BuiltIn, nullptr, "Custom shader enabled but no shader path specified." };
        }

        if (fingerprint.empty())
            return {ProgramSelectionKind::BuiltIn,nullptr,"Custom shader is missing or unreadable: "+shaderPath};
        RefreshEntry(key, fingerprint);

        auto it = m_Entries.find(key);
        if (it != m_Entries.end() && it->second.ReadyGeneration) {
            return SelectedProgram{ ProgramSelectionKind::Custom, it->second.ReadyGeneration, it->second.Diagnostic };
        }

        std::string diag = (it != m_Entries.end()) ? it->second.Diagnostic : "Shader not compiled.";
        return SelectedProgram{ ProgramSelectionKind::BuiltIn, nullptr, diag };
    }

    const ProgramEntry* CustomProgramCache::FindEntry(const CustomProgramKey& key) const {
        auto it = m_Entries.find(key);
        if (it != m_Entries.end()) return &it->second;
        return nullptr;
    }

    void CustomProgramCache::Clear() {
        m_Entries.clear();
    }

    CandidateBuildResult CustomProgramCache::DefaultBuildCandidate(const CustomProgramKey& key, const std::string& fingerprint) {
        CandidateBuildResult result;
        (void)fingerprint;

        std::string resolvedPath = FileSystem::ResolveAssetPath(key.ShaderPath).string();
        std::error_code ec;
        if (!std::filesystem::exists(resolvedPath, ec)) {
            result.Success = false;
            result.Diagnostic = "Shader file does not exist: " + key.ShaderPath;
            return result;
        }

        // Real DXC compilation hook when RHI device is initialized
        auto& device = RHI::RHIDevice::Get();
        if (!device.IsInitialized()) {
            result.Success = false;
            result.Diagnostic = "RHI device not initialized for shader compilation.";
            return result;
        }

        auto& compiler = RHI::RHIShaderCompiler::Get();
        if (!compiler.IsInitialized()) {
            if (!compiler.Init()) {
                result.Success = false;
                result.Diagnostic = "Failed to initialize DXC shader compiler.";
                return result;
            }
        }

        std::string source = FileSystem::ReadTextFile(resolvedPath);
        if (source.empty()) {
            result.Success = false;
            result.Diagnostic = "Shader file is empty: " + key.ShaderPath;
            return result;
        }

        RHI::RHIShaderCompileDesc vsDesc;
        vsDesc.Source = source;
        vsDesc.SourcePath = resolvedPath;
        vsDesc.EntryPoint = key.EntryPointVS;
        vsDesc.Stage = RHI::RHIShaderStage::Vertex;
        vsDesc.Format = (key.Backend == RHI::RHIType::Vulkan) ? RHI::RHIShaderFormat::SPIRV : RHI::RHIShaderFormat::DXIL;

        auto vsResult = compiler.Compile(vsDesc);
        if (!vsResult.Success) {
            result.Success = false;
            result.Diagnostic = "Vertex shader compilation failed: " + vsResult.ErrorMessage;
            return result;
        }

        RHI::RHIShaderCompileDesc psDesc;
        psDesc.Source = source;
        psDesc.SourcePath = resolvedPath;
        psDesc.EntryPoint = key.EntryPointPS;
        psDesc.Stage = RHI::RHIShaderStage::Fragment;
        psDesc.Format = vsDesc.Format;

        auto psResult = compiler.Compile(psDesc);
        if (!psResult.Success) {
            result.Success = false;
            result.Diagnostic = "Pixel shader compilation failed: " + psResult.ErrorMessage;
            return result;
        }

        auto mergedReflection = RHI::RHIShaderReflection::Merge({ vsResult.Reflection, psResult.Reflection });
        ValidatedShaderInterface iface;
        iface.InterfaceRevision = key.InterfaceRevision;

        auto properties = ShaderPropertyParser::ParseFromHLSL(source, &mergedReflection);
        auto planResult = BuildMaterialBindingPlan(mergedReflection, iface, properties);
        if (std::holds_alternative<MaterialBindingError>(planResult)) {
            const auto& err = std::get<MaterialBindingError>(planResult);
            result.Success = false;
            result.Diagnostic = "Binding plan creation failed [" + err.Code + "]: " + err.Message;
            return result;
        }

        auto generation = std::make_shared<CustomReadyGeneration>();
        generation->VSBytecode = vsResult.Bytecode;
        generation->PSBytecode = psResult.Bytecode;
        generation->Reflection = mergedReflection;
        generation->BindingPlan = std::get<MaterialBindingPlan>(planResult);

        // Build pipeline layout from merged reflection
        auto autoLayout = mergedReflection.BuildPipelineLayoutDesc(1);
        generation->PipelineLayout = std::make_shared<RHI::RHIPipelineLayout>();
        if (!generation->PipelineLayout->Init(autoLayout)) {
            result.Success = false;
            result.Diagnostic = "Pipeline layout creation failed.";
            return result;
        }

        result.Success = true;
        result.Generation = generation;
        return result;
    }

} // namespace Axle
