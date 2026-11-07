#include <Axle/CustomMaterialProgram.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/FileSystem.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

namespace Axle {
    nri::Pipeline* CreateCustomMaterialPipeline(const CustomReadyGeneration& generation,
        nri::Format colorFormat, nri::Format depthFormat, uint8_t sampleCount,
        SurfaceType surfaceType, CullMode cullMode, bool depthWrite) {
        using namespace RHI;
        if (!generation.PipelineLayout || !generation.PipelineLayout->IsValid()) return nullptr;
        RHIGraphicsPipelineDesc gfxDesc;
        gfxDesc.Layout = generation.PipelineLayout.get();
        gfxDesc.Shaders = { generation.VSBytecode, generation.PSBytecode };

        gfxDesc.Rasterizer.FillMode = nri::FillMode::SOLID;
        if (cullMode == Axle::CullMode::Back) {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::BACK;
        } else if (cullMode == Axle::CullMode::Front) {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::FRONT;
        } else {
            gfxDesc.Rasterizer.CullMode = nri::CullMode::NONE;
        }
        gfxDesc.Rasterizer.FrontCounterClockwise = true;

        gfxDesc.DepthStencil.DepthTestEnable = true;
        gfxDesc.DepthStencil.DepthWriteEnable = depthWrite;
        gfxDesc.DepthStencil.DepthCompareOp = nri::CompareOp::LESS;
        gfxDesc.DepthStencil.DepthStencilFormat = depthFormat;

        if (surfaceType == Axle::SurfaceType::Transparent) {
            gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::AlphaBlend() };
        } else if (surfaceType == Axle::SurfaceType::Additive) {
            gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Additive() };
        } else {
            gfxDesc.ColorAttachments = { RHIBlendAttachmentDesc::Opaque() };
        }
        gfxDesc.ColorAttachmentFormats = { colorFormat };
        gfxDesc.SampleCount = sampleCount;

        RHIVertexInputDesc inputDesc;
        uint32_t stride = generation.BindingPlan.VertexInput.Stride > 0 ? generation.BindingPlan.VertexInput.Stride : static_cast<uint32_t>(sizeof(RHIVertex3D));
        inputDesc.Streams.push_back({ 0, static_cast<uint16_t>(stride), nri::VertexStreamStepRate::PER_VERTEX });

        if (!generation.BindingPlan.VertexInput.Attributes.empty()) {
            for (const auto& attr : generation.BindingPlan.VertexInput.Attributes) {
                inputDesc.Attributes.push_back({
                    attr.SemanticName,
                    attr.SemanticIndex,
                    attr.BackendLocation,
                    attr.Offset,
                    attr.Format,
                    static_cast<uint16_t>(attr.InputSlot)
                });
            }
        } else {
            inputDesc = VertexLayoutContract::GetCanonicalVertexInputDesc();
        }
        gfxDesc.VertexInput = inputDesc;

        return RHIPipelineCache::Get().GetOrCreateGraphicsPipeline(gfxDesc);
    }

    CustomProgramCache::CustomProgramCache() {
        m_CandidateBuilder = [this](const CustomProgramKey& key, const std::string& fingerprint) {
            return DefaultBuildCandidate(key, fingerprint);
        };
    }

    std::string CustomProgramCache::ComputeFingerprint(const std::filesystem::path& shaderPath,
        const std::vector<std::filesystem::path>& observedDependencies) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(shaderPath, ec)) {
            return "";
        }

        auto dependencies=RHI::RHIShaderCompiler::ExtractIncludeDependencies(shaderPath);
        dependencies.insert(dependencies.end(),observedDependencies.begin(),observedDependencies.end());
        dependencies.push_back(shaderPath);
        std::sort(dependencies.begin(),dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(),dependencies.end()),dependencies.end());
        // Include identities and contents, so same-size edits and include removal invalidate.
        uint64_t hash = 14695981039346656037ull;
        for (const auto& dependency : dependencies) {
            std::ifstream file(dependency,std::ios::binary);
            const auto identity=std::filesystem::absolute(dependency,ec).lexically_normal().generic_string();
            for (unsigned char c : identity) { hash^=c; hash*=1099511628211ull; }
            if (!file) { hash^=0xfe; hash*=1099511628211ull; continue; }
            char c;
            while(file.get(c)) { hash^=static_cast<unsigned char>(c); hash*=1099511628211ull; }
            hash^=0xff; hash*=1099511628211ull;
        }
        return std::to_string(hash);
    }

    std::string CustomProgramCache::ComputeFingerprint(const CustomProgramKey& key) const {
        const auto* entry=FindEntry(key);
        return ComputeFingerprint(std::filesystem::path(key.ShaderPath),entry?entry->Dependencies:std::vector<std::filesystem::path>{});
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
        if (!candidate.Dependencies.empty()) {
            entry.Dependencies=std::move(candidate.Dependencies);
            entry.LastAttemptFingerprint=ComputeFingerprint(key);
        }

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
        if (!device.IsNativeReady()) {
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
        std::string expanded,preprocessError;
        if (!compiler.Preprocess(vsDesc,expanded,preprocessError)) {
            static const std::regex missingInclude(R"deps(['"]([^'"]+)['"] file not found)deps");
            std::smatch missing;
            if(std::regex_search(preprocessError,missing,missingInclude))
                result.Dependencies.push_back((std::filesystem::path(resolvedPath).parent_path()/missing[1].str()).lexically_normal());
            result.Diagnostic=preprocessError; return result;
        }
        // DXC reports active includes after macro expansion, including nested includes.
        static const std::regex lineDirective(R"deps(^\s*#\s*(?:line\s+)?\d+\s+"([^"]+)")deps");
        std::istringstream preprocessed(expanded);
        std::string line;
        while(std::getline(preprocessed,line)) {
            std::smatch match;
            if(!std::regex_search(line,match,lineDirective))continue;
            std::filesystem::path dependency=match[1].str();
            if(dependency.is_relative())dependency=std::filesystem::path(resolvedPath).parent_path()/dependency;
            dependency=std::filesystem::weakly_canonical(dependency,ec);
            if(!ec && std::filesystem::is_regular_file(dependency,ec))result.Dependencies.push_back(dependency);
        }
        std::sort(result.Dependencies.begin(),result.Dependencies.end());
        result.Dependencies.erase(std::unique(result.Dependencies.begin(),result.Dependencies.end()),result.Dependencies.end());
        vsDesc.Source=CustomShaderInterface::NormalizeLegacyBindings(expanded);

        auto vsResult = compiler.Compile(vsDesc);
        if (!vsResult.Success) {
            result.Success = false;
            result.Diagnostic = "Vertex shader compilation failed: " + vsResult.ErrorMessage;
            return result;
        }

        RHI::RHIShaderCompileDesc psDesc;
        psDesc.Source = vsDesc.Source;
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
        auto validation=CustomShaderInterface::Validate(mergedReflection);
        std::string collision;
        if (!validation.IsValid || CustomShaderInterface::HasVulkanBindingCollision(mergedReflection,collision)) {
            result.Diagnostic=validation.IsValid?collision:validation.Diagnostic; return result;
        }
        ValidatedShaderInterface iface;
        iface.InterfaceRevision = key.InterfaceRevision;

        std::string annotatedSource=source;
        for(const auto& dependency:result.Dependencies)annotatedSource+="\n"+FileSystem::ReadTextFile(dependency);
        auto properties = ShaderPropertyParser::ParseFromHLSL(annotatedSource+"\n"+vsDesc.Source, &mergedReflection);
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
        uint32_t rootSpace=0;
        for (const auto& resource:mergedReflection.Resources) rootSpace=std::max(rootSpace,resource.Space+1);
        generation->LayoutDescription=mergedReflection.BuildPipelineLayoutDesc(rootSpace);
        if (!generation->LayoutDescription.RootConstants.empty()) {
            result.Diagnostic="Custom material cbuffers must be descriptor-bound, not push/root constants.";return result;
        }
        generation->PipelineLayout = std::make_shared<RHI::RHIPipelineLayout>();
        if (!generation->PipelineLayout->Init(generation->LayoutDescription)) {
            result.Success = false;
            result.Diagnostic = "Pipeline layout creation failed.";
            return result;
        }

        if (!CreateCustomMaterialPipeline(*generation,nri::Format::RGBA8_UNORM,nri::Format::D32_SFLOAT,1,SurfaceType::Opaque,CullMode::None,true)) {
            result.Diagnostic="Custom material pipeline validation failed.";return result;
        }
        result.Success = true;
        result.Generation = generation;
        return result;
    }

} // namespace Axle
