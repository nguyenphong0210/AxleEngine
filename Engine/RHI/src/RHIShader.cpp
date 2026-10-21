#include <Axle/RHI/RHIShader.hpp>
#include <Axle/Log.hpp>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <combaseapi.h>
#include <wrl/client.h>
#endif

#ifdef GetProfileString
#undef GetProfileString
#endif

#include <dxcapi.h>

#include <fstream>
#include <sstream>

namespace Axle::RHI {

    static std::wstring ToWide(const std::string& str) {
        if (str.empty()) return std::wstring();
        int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0);
        std::wstring wstr(sizeNeeded, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], sizeNeeded);
        return wstr;
    }

    // =========================================================================
    // RHIShaderBytecode Implementation
    // =========================================================================
    nri::ShaderDesc RHIShaderBytecode::ToNRIShaderDesc() const {
        nri::ShaderDesc desc = {};
        desc.stage = RHIShaderCompiler::StageToNRI(Stage);
        desc.bytecode = Data.data();
        desc.size = Data.size();
        desc.entryPointName = EntryPoint.c_str();
        return desc;
    }

    bool RHIShaderBytecode::SaveToFile(const std::filesystem::path& filepath) const {
        if (Data.empty()) return false;
        try {
            if (filepath.has_parent_path()) {
                std::filesystem::create_directories(filepath.parent_path());
            }
            std::ofstream out(filepath, std::ios::binary);
            if (!out.is_open()) return false;
            out.write(reinterpret_cast<const char*>(Data.data()), Data.size());
            return out.good();
        } catch (const std::exception& e) {
            AXLE_CORE_ERROR("Failed to save shader bytecode to '{0}': {1}", filepath.string(), e.what());
            return false;
        }
    }

    RHIShaderBytecode RHIShaderBytecode::LoadFromFile(const std::filesystem::path& filepath,
                                                      RHIShaderFormat format,
                                                      RHIShaderStage stage,
                                                      const std::string& entryPoint) {
        RHIShaderBytecode bc;
        bc.Format = format;
        bc.Stage = stage;
        bc.EntryPoint = entryPoint;

        if (!std::filesystem::exists(filepath)) return bc;

        try {
            std::ifstream in(filepath, std::ios::binary | std::ios::ate);
            if (!in.is_open()) return bc;
            std::streamsize size = in.tellg();
            in.seekg(0, std::ios::beg);
            if (size > 0) {
                bc.Data.resize(static_cast<size_t>(size));
                in.read(reinterpret_cast<char*>(bc.Data.data()), size);
            }
        } catch (const std::exception& e) {
            AXLE_CORE_ERROR("Failed to load shader bytecode from '{0}': {1}", filepath.string(), e.what());
            bc.Data.clear();
        }

        return bc;
    }

    // =========================================================================
    // RHIShaderCompiler Implementation
    // =========================================================================
    RHIShaderCompiler& RHIShaderCompiler::Get() {
        static RHIShaderCompiler s_Instance;
        return s_Instance;
    }

    RHIShaderCompiler::~RHIShaderCompiler() {
        Shutdown();
    }

    bool RHIShaderCompiler::Init() {
        if (m_Initialized) return true;
        return LoadDXCLibrary();
    }

    void RHIShaderCompiler::Shutdown() {
        if (!m_Initialized) return;

        if (m_DxcIncludeHandler) {
            static_cast<IDxcIncludeHandler*>(m_DxcIncludeHandler)->Release();
            m_DxcIncludeHandler = nullptr;
        }

        if (m_DxcCompiler) {
            static_cast<IDxcCompiler3*>(m_DxcCompiler)->Release();
            m_DxcCompiler = nullptr;
        }

        if (m_DxcUtils) {
            static_cast<IDxcUtils*>(m_DxcUtils)->Release();
            m_DxcUtils = nullptr;
        }

        if (m_DxcDll) {
            FreeLibrary(static_cast<HMODULE>(m_DxcDll));
            m_DxcDll = nullptr;
        }

        if (m_DxilDll) {
            FreeLibrary(static_cast<HMODULE>(m_DxilDll));
            m_DxilDll = nullptr;
        }

        m_Initialized = false;
        AXLE_CORE_INFO("RHIShaderCompiler successfully shut down.");
    }

    bool RHIShaderCompiler::LoadDXCLibrary() {
        if (m_DxcDll) return true;

        std::vector<std::filesystem::path> searchPaths;

        char exePathBuf[MAX_PATH];
        if (GetModuleFileNameA(NULL, exePathBuf, MAX_PATH)) {
            std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();
            searchPaths.push_back(exeDir / "dxcompiler.dll");
        }

        searchPaths.push_back("ThirdParty/dxc/bin/x64/dxcompiler.dll");
        searchPaths.push_back("../ThirdParty/dxc/bin/x64/dxcompiler.dll");
        searchPaths.push_back("../../ThirdParty/dxc/bin/x64/dxcompiler.dll");
        searchPaths.push_back("dxcompiler.dll");

        HMODULE hDxc = nullptr;
        for (const auto& path : searchPaths) {
            if (std::filesystem::exists(path)) {
                std::filesystem::path dxilPath = path.parent_path() / "dxil.dll";
                if (std::filesystem::exists(dxilPath) && !m_DxilDll) {
                    m_DxilDll = LoadLibraryA(dxilPath.string().c_str());
                }

                hDxc = LoadLibraryA(path.string().c_str());
                if (hDxc) {
                    AXLE_CORE_INFO("RHIShaderCompiler: Loaded DXC library from '{0}'", path.string());
                    break;
                }
            }
        }

        if (!hDxc) {
            hDxc = LoadLibraryA("dxcompiler.dll");
        }

        if (!hDxc) {
            AXLE_CORE_ERROR("RHIShaderCompiler: Failed to locate or load dxcompiler.dll!");
            return false;
        }

        m_DxcDll = hDxc;
        auto fnDxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(hDxc, "DxcCreateInstance"));
        if (!fnDxcCreateInstance) {
            AXLE_CORE_ERROR("RHIShaderCompiler: Failed to resolve DxcCreateInstance symbol from dxcompiler.dll!");
            Shutdown();
            return false;
        }

        IDxcUtils* pUtils = nullptr;
        HRESULT hr = fnDxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pUtils));
        if (FAILED(hr) || !pUtils) {
            AXLE_CORE_ERROR("RHIShaderCompiler: Failed to create IDxcUtils instance (hr = 0x{0:x})!", static_cast<uint32_t>(hr));
            Shutdown();
            return false;
        }
        m_DxcUtils = pUtils;

        IDxcCompiler3* pCompiler = nullptr;
        hr = fnDxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler));
        if (FAILED(hr) || !pCompiler) {
            AXLE_CORE_ERROR("RHIShaderCompiler: Failed to create IDxcCompiler3 instance (hr = 0x{0:x})!", static_cast<uint32_t>(hr));
            Shutdown();
            return false;
        }
        m_DxcCompiler = pCompiler;

        IDxcIncludeHandler* pIncludeHandler = nullptr;
        hr = pUtils->CreateDefaultIncludeHandler(&pIncludeHandler);
        if (SUCCEEDED(hr)) {
            m_DxcIncludeHandler = pIncludeHandler;
        }

        m_Initialized = true;
        AXLE_CORE_INFO("RHIShaderCompiler: Successfully initialized (Direct3D 12 DXIL & Vulkan 1.3 SPIR-V toolchain ready)");
        return true;
    }

    RHIShaderCompileResult RHIShaderCompiler::Compile(const RHIShaderCompileDesc& desc) {
        RHIShaderCompileResult result;

        if (!m_Initialized) {
            if (!Init()) {
                result.ErrorMessage = "RHIShaderCompiler is not initialized and dxcompiler.dll could not be loaded.";
                return result;
            }
        }

        if (desc.Source.empty()) {
            result.ErrorMessage = "Shader compilation failed: source code is empty.";
            return result;
        }

        auto* pCompiler = static_cast<IDxcCompiler3*>(m_DxcCompiler);
        auto* pIncludeHandler = static_cast<IDxcIncludeHandler*>(m_DxcIncludeHandler);

        DxcBuffer sourceBuffer = {};
        sourceBuffer.Ptr = desc.Source.data();
        sourceBuffer.Size = desc.Source.size();
        sourceBuffer.Encoding = DXC_CP_UTF8;

        std::vector<std::wstring> wargs;

        // Entry point: -E <entryPoint>
        wargs.push_back(L"-E");
        wargs.push_back(ToWide(desc.EntryPoint));

        // Target profile: -T <profile>
        std::string profile = GetProfileTarget(desc.Stage, desc.ShaderModel);
        wargs.push_back(L"-T");
        wargs.push_back(ToWide(profile));

        // Optimization level
        if (desc.Optimization) {
            wargs.push_back(L"-O3");
        } else {
            wargs.push_back(L"-O0");
        }

        // Debug information
        if (desc.GenerateDebugInfo) {
            wargs.push_back(L"-Zi");
            wargs.push_back(L"-Qembed_debug");
        }

        // Column major packing by default
        wargs.push_back(L"-Zpc");

        // Target format configuration
        if (desc.Format == RHIShaderFormat::SPIRV) {
            wargs.push_back(L"-spirv");
            if (desc.EnableVulkanDXLayout) {
                wargs.push_back(L"-fvk-use-dx-layout");
            }
            std::wstring vkEnv = L"-fspv-target-env=" + ToWide(desc.VulkanTargetEnv);
            wargs.push_back(vkEnv);

            // Preprocessor defines for Vulkan/SPIR-V
            wargs.push_back(L"-D");
            wargs.push_back(L"__spirv__=1");
        } else {
            // Direct3D 12 DXIL defines
            wargs.push_back(L"-D");
            wargs.push_back(L"__directx__=1");
            wargs.push_back(L"-D");
            wargs.push_back(L"_DX12=1");
        }

        // Custom user preprocessor macros
        for (const auto& macro : desc.Macros) {
            std::wstring def = ToWide(macro.Name);
            if (!macro.Definition.empty()) {
                def += L"=" + ToWide(macro.Definition);
            }
            wargs.push_back(L"-D");
            wargs.push_back(def);
        }

        // Include directories
        for (const auto& incDir : desc.IncludeDirs) {
            wargs.push_back(L"-I");
            wargs.push_back(ToWide(incDir));
        }

        if (!desc.SourcePath.empty()) {
            std::filesystem::path p(desc.SourcePath);
            if (p.has_parent_path()) {
                wargs.push_back(L"-I");
                wargs.push_back(ToWide(p.parent_path().string()));
            }
        }

        // Add standard NRI HLSL include directories
        std::vector<std::string> nriIncludes = {
            "ThirdParty/NRI/Include",
            "../ThirdParty/NRI/Include",
            "../../ThirdParty/NRI/Include"
        };
        for (const auto& nriInc : nriIncludes) {
            if (std::filesystem::exists(nriInc)) {
                wargs.push_back(L"-I");
                wargs.push_back(ToWide(nriInc));
                break;
            }
        }

        std::vector<LPCWSTR> pszArgs;
        pszArgs.reserve(wargs.size());
        for (const auto& w : wargs) {
            pszArgs.push_back(w.c_str());
        }

        IDxcResult* pCompileResult = nullptr;
        HRESULT hr = pCompiler->Compile(
            &sourceBuffer,
            pszArgs.data(),
            static_cast<UINT32>(pszArgs.size()),
            pIncludeHandler,
            IID_PPV_ARGS(&pCompileResult)
        );

        if (FAILED(hr) || !pCompileResult) {
            result.ErrorMessage = "IDxcCompiler3::Compile failed with HRESULT 0x" + std::to_string(hr);
            return result;
        }

        HRESULT hrStatus = S_OK;
        pCompileResult->GetStatus(&hrStatus);

        // Retrieve error / warning messages
        IDxcBlobUtf8* pErrors = nullptr;
        if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr)) && pErrors) {
            if (pErrors->GetStringLength() > 0) {
                if (FAILED(hrStatus)) {
                    result.ErrorMessage = pErrors->GetStringPointer();
                } else {
                    result.WarningMessage = pErrors->GetStringPointer();
                }
            }
            pErrors->Release();
        }

        // Retrieve compiled bytecode blob
        if (SUCCEEDED(hrStatus)) {
            IDxcBlob* pBytecodeBlob = nullptr;
            if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pBytecodeBlob), nullptr)) && pBytecodeBlob) {
                const uint8_t* pData = static_cast<const uint8_t*>(pBytecodeBlob->GetBufferPointer());
                size_t dataSize = pBytecodeBlob->GetBufferSize();

                result.Bytecode.Data.assign(pData, pData + dataSize);
                result.Bytecode.Format = desc.Format;
                result.Bytecode.Stage = desc.Stage;
                result.Bytecode.EntryPoint = desc.EntryPoint;
                result.Success = true;

                pBytecodeBlob->Release();

                AXLE_CORE_INFO("RHIShaderCompiler: Compiled {0} shader '{1}' to {2} ({3} bytes)",
                    StageToString(desc.Stage), desc.EntryPoint, FormatToString(desc.Format), dataSize);
            }
        } else {
            AXLE_CORE_ERROR("RHIShaderCompiler compilation failed for {0} shader '{1}':\n{2}",
                StageToString(desc.Stage), desc.EntryPoint, result.ErrorMessage);
        }

        pCompileResult->Release();
        return result;
    }

    RHIShaderCompileResult RHIShaderCompiler::CompileFromFile(const std::filesystem::path& filepath, const RHIShaderCompileDesc& desc) {
        RHIShaderCompileResult result;
        if (!std::filesystem::exists(filepath)) {
            result.ErrorMessage = "Shader file does not exist: " + filepath.string();
            return result;
        }

        std::ifstream file(filepath);
        if (!file.is_open()) {
            result.ErrorMessage = "Failed to open shader file: " + filepath.string();
            return result;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        RHIShaderCompileDesc fullDesc = desc;
        fullDesc.Source = buffer.str();
        fullDesc.SourcePath = filepath.string();

        return Compile(fullDesc);
    }

    RHIShaderCompileResult RHIShaderCompiler::CompileForActiveBackend(const RHIShaderCompileDesc& desc) {
        RHIShaderCompileDesc activeDesc = desc;
        auto backend = RHIDevice::Get().GetBackend();
        if (backend == RHIType::Vulkan) {
            activeDesc.Format = RHIShaderFormat::SPIRV;
        } else {
            activeDesc.Format = RHIShaderFormat::DXIL;
        }
        return Compile(activeDesc);
    }

    std::string RHIShaderCompiler::GetProfileTarget(RHIShaderStage stage, const std::string& shaderModel) {
        std::string sm = shaderModel.empty() ? "6_0" : shaderModel;
        switch (stage) {
            case RHIShaderStage::Vertex:       return "vs_" + sm;
            case RHIShaderStage::Fragment:     return "ps_" + sm;
            case RHIShaderStage::Compute:      return "cs_" + sm;
            case RHIShaderStage::Geometry:     return "gs_" + sm;
            case RHIShaderStage::Hull:         return "hs_" + sm;
            case RHIShaderStage::Domain:       return "ds_" + sm;
            case RHIShaderStage::Mesh:         return "ms_" + (sm == "6_0" ? "6_5" : sm);
            case RHIShaderStage::Task:         return "as_" + (sm == "6_0" ? "6_5" : sm);
            case RHIShaderStage::RayGen:
            case RHIShaderStage::Miss:
            case RHIShaderStage::ClosestHit:
            case RHIShaderStage::AnyHit:
            case RHIShaderStage::Intersection:
            case RHIShaderStage::Callable:     return "lib_" + (sm == "6_0" ? "6_3" : sm);
            default:                           return "vs_" + sm;
        }
    }

    nri::StageBits RHIShaderCompiler::StageToNRI(RHIShaderStage stage) {
        switch (stage) {
            case RHIShaderStage::Vertex:       return nri::StageBits::VERTEX_SHADER;
            case RHIShaderStage::Fragment:     return nri::StageBits::FRAGMENT_SHADER;
            case RHIShaderStage::Compute:      return nri::StageBits::COMPUTE_SHADER;
            case RHIShaderStage::Geometry:     return nri::StageBits::GEOMETRY_SHADER;
            case RHIShaderStage::Hull:         return nri::StageBits::TESS_CONTROL_SHADER;
            case RHIShaderStage::Domain:       return nri::StageBits::TESS_EVALUATION_SHADER;
            case RHIShaderStage::Task:         return nri::StageBits::TASK_SHADER;
            case RHIShaderStage::Mesh:         return nri::StageBits::MESH_SHADER;
            case RHIShaderStage::RayGen:       return nri::StageBits::RAYGEN_SHADER;
            case RHIShaderStage::Miss:         return nri::StageBits::MISS_SHADER;
            case RHIShaderStage::ClosestHit:   return nri::StageBits::CLOSEST_HIT_SHADER;
            case RHIShaderStage::AnyHit:       return nri::StageBits::ANY_HIT_SHADER;
            case RHIShaderStage::Intersection: return nri::StageBits::INTERSECTION_SHADER;
            case RHIShaderStage::Callable:     return nri::StageBits::CALLABLE_SHADER;
            default:                           return nri::StageBits::ALL;
        }
    }

    const char* RHIShaderCompiler::FormatToString(RHIShaderFormat format) {
        switch (format) {
            case RHIShaderFormat::DXIL:  return "Direct3D 12 DXIL";
            case RHIShaderFormat::SPIRV: return "Vulkan 1.3 SPIR-V";
            case RHIShaderFormat::DXBC:  return "Legacy DXBC";
            default:                     return "Unknown";
        }
    }

    const char* RHIShaderCompiler::StageToString(RHIShaderStage stage) {
        switch (stage) {
            case RHIShaderStage::Vertex:       return "Vertex";
            case RHIShaderStage::Fragment:     return "Fragment";
            case RHIShaderStage::Compute:      return "Compute";
            case RHIShaderStage::Geometry:     return "Geometry";
            case RHIShaderStage::Hull:         return "Hull";
            case RHIShaderStage::Domain:       return "Domain";
            case RHIShaderStage::Task:         return "Task";
            case RHIShaderStage::Mesh:         return "Mesh";
            case RHIShaderStage::RayGen:       return "RayGen";
            case RHIShaderStage::Miss:         return "Miss";
            case RHIShaderStage::ClosestHit:   return "ClosestHit";
            case RHIShaderStage::AnyHit:       return "AnyHit";
            case RHIShaderStage::Intersection: return "Intersection";
            case RHIShaderStage::Callable:     return "Callable";
            default:                           return "Unknown";
        }
    }

}
