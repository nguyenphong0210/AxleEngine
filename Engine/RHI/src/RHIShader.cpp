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
#ifdef FindResource
#undef FindResource
#endif
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include <dxcapi.h>
#include <d3d12shader.h>

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_set>
#include <map>

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

            // Extract reflection metadata
            if (desc.Format == RHIShaderFormat::SPIRV) {
                // When compiling to SPIR-V, DXC doesn't output DXIL reflection blob.
                // We reflect using HLSL source directly to get unified spaces, registers, variables and thread groups!
                result.Reflection = ReflectFromSource(desc);
            } else {
                IDxcBlob* pReflBlob = nullptr;
                if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&pReflBlob), nullptr)) && pReflBlob) {
                    result.Reflection = Reflect(pReflBlob->GetBufferPointer(), pReflBlob->GetBufferSize(), desc.Stage, desc.Format, desc.EntryPoint);
                    pReflBlob->Release();
                } else if (!result.Bytecode.Data.empty()) {
                    result.Reflection = Reflect(result.Bytecode);
                }
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

    static uint32_t FNV1aHash(const std::string& str, uint32_t seed = 2166136261u) {
        uint32_t hash = seed;
        for (char c : str) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    std::filesystem::path RHIShaderCompiler::ComputeCachePath(
        const std::filesystem::path& sourcePath,
        const RHIShaderCompileDesc& desc,
        const std::filesystem::path& cacheDir) const
    {
        std::filesystem::path targetDir = cacheDir.empty() ? m_DefaultCacheDirectory : cacheDir;
        std::string ext = (desc.Format == RHIShaderFormat::SPIRV) ? ".spv" : ".dxil";
        std::string formatTag = (desc.Format == RHIShaderFormat::SPIRV) ? "spv" : "dxil";

        // Build deterministic configuration hash
        uint32_t hash = 2166136261u;
        hash = FNV1aHash(desc.EntryPoint, hash);
        hash = FNV1aHash(GetProfileTarget(desc.Stage, desc.ShaderModel), hash);
        hash = FNV1aHash(desc.ShaderModel, hash);
        hash = FNV1aHash(desc.Optimization ? "O3" : "O0", hash);
        hash = FNV1aHash(desc.EnableVulkanDXLayout ? "dxlayout" : "stdlayout", hash);
        hash = FNV1aHash(desc.VulkanTargetEnv, hash);

        // Sort macros for deterministic hash
        auto sortedMacros = desc.Macros;
        std::sort(sortedMacros.begin(), sortedMacros.end(), [](const RHIShaderMacro& a, const RHIShaderMacro& b) {
            return a.Name < b.Name;
        });
        for (const auto& macro : sortedMacros) {
            hash = FNV1aHash(macro.Name + "=" + macro.Definition + ";", hash);
        }

        std::stringstream ss;
        ss << std::hex << std::setfill('0') << std::setw(8) << hash;
        std::string hashStr = ss.str();

        std::string stem = sourcePath.stem().string();
        std::string stageTag = StageToString(desc.Stage);
        std::string filename = stem + "_" + desc.EntryPoint + "_" + stageTag + "_" + formatTag + "_" + hashStr + ext;

        return targetDir / filename;
    }

    static void ExtractDependenciesInternal(
        const std::filesystem::path& filepath,
        const std::vector<std::string>& includeDirs,
        std::unordered_set<std::string>& visited,
        std::vector<std::filesystem::path>& result)
    {
        std::string canonicalKey = filepath.lexically_normal().string();
        if (visited.count(canonicalKey)) return;
        visited.insert(canonicalKey);

        if (!std::filesystem::exists(filepath)) return;

        std::ifstream file(filepath);
        if (!file.is_open()) return;

        std::vector<std::filesystem::path> searchDirs;
        if (filepath.has_parent_path()) {
            searchDirs.push_back(filepath.parent_path());
        }
        for (const auto& inc : includeDirs) {
            searchDirs.push_back(inc);
        }
        searchDirs.push_back("ThirdParty/NRI/Include");
        searchDirs.push_back("../ThirdParty/NRI/Include");
        searchDirs.push_back("../../ThirdParty/NRI/Include");

        std::string line;
        while (std::getline(file, line)) {
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) continue;

            if (line.compare(start, 8, "#include") == 0) {
                size_t quoteStart = line.find_first_of("\"<", start + 8);
                if (quoteStart == std::string::npos) continue;
                char closeQuote = (line[quoteStart] == '\"') ? '\"' : '>';
                size_t quoteEnd = line.find_first_of(closeQuote, quoteStart + 1);
                if (quoteEnd == std::string::npos) continue;

                std::string incName = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                for (const auto& searchDir : searchDirs) {
                    std::filesystem::path candidate = searchDir / incName;
                    if (std::filesystem::exists(candidate)) {
                        std::filesystem::path normCandidate = candidate.lexically_normal();
                        result.push_back(normCandidate);
                        ExtractDependenciesInternal(normCandidate, includeDirs, visited, result);
                        break;
                    }
                }
            }
        }
    }

    std::vector<std::filesystem::path> RHIShaderCompiler::ExtractIncludeDependencies(
        const std::filesystem::path& filepath,
        const std::vector<std::string>& includeDirs)
    {
        std::vector<std::filesystem::path> dependencies;
        std::unordered_set<std::string> visited;
        ExtractDependenciesInternal(filepath, includeDirs, visited, dependencies);
        return dependencies;
    }

    RHIShaderCompileResult RHIShaderCompiler::LoadOrCompileFromFile(
        const std::filesystem::path& filepath,
        const RHIShaderCompileDesc& desc,
        const std::filesystem::path& cacheDir,
        bool forceRecompile)
    {
        RHIShaderCompileResult result;
        if (!std::filesystem::exists(filepath)) {
            result.ErrorMessage = "Shader source file does not exist: " + filepath.string();
            return result;
        }

        std::filesystem::path cachePath = ComputeCachePath(filepath, desc, cacheDir);
        result.CachedPath = cachePath;

        bool needsCompile = forceRecompile;

        if (!needsCompile && std::filesystem::exists(cachePath)) {
            std::error_code ec;
            auto cacheTime = std::filesystem::last_write_time(cachePath, ec);
            auto sourceTime = std::filesystem::last_write_time(filepath, ec);

            if (!ec && sourceTime <= cacheTime) {
                // Validate all include dependencies
                auto deps = ExtractIncludeDependencies(filepath, desc.IncludeDirs);
                bool depStale = false;
                for (const auto& dep : deps) {
                    if (std::filesystem::exists(dep)) {
                        auto depTime = std::filesystem::last_write_time(dep, ec);
                        if (!ec && depTime > cacheTime) {
                            depStale = true;
                            AXLE_CORE_INFO("RHIShaderCompiler: Dependency '{0}' is newer than cache for '{1}'. Triggering recompile.",
                                dep.filename().string(), filepath.filename().string());
                            break;
                        }
                    }
                }

                if (!depStale) {
                    // CACHE HIT!
                    auto cachedBytecode = RHIShaderBytecode::LoadFromFile(cachePath, desc.Format, desc.Stage, desc.EntryPoint);
                    if (cachedBytecode.IsValid()) {
                        result.Success = true;
                        result.Bytecode = std::move(cachedBytecode);
                        result.IsCacheHit = true;

                        // Populate reflection metadata
                        if (desc.Format == RHIShaderFormat::SPIRV) {
                            std::ifstream sf(filepath);
                            if (sf.is_open()) {
                                std::stringstream sbuf;
                                sbuf << sf.rdbuf();
                                RHIShaderCompileDesc reflDesc = desc;
                                reflDesc.Source = sbuf.str();
                                reflDesc.SourcePath = filepath.string();
                                result.Reflection = ReflectFromSource(reflDesc);
                            }
                        } else {
                            result.Reflection = Reflect(result.Bytecode);
                        }

                        AXLE_CORE_INFO("RHIShaderCompiler: Cache hit for {0} shader '{1}' ({2} bytes) from '{3}'",
                            StageToString(desc.Stage), desc.EntryPoint, result.Bytecode.GetSize(), cachePath.string());
                        return result;
                    }
                }
            }
        }

        // Cache Miss, Stale, or Forced Recompile
        AXLE_CORE_INFO("RHIShaderCompiler: Cache miss for {0} shader '{1}'. Compiling from source '{2}'...",
            StageToString(desc.Stage), desc.EntryPoint, filepath.string());

        result = CompileFromFile(filepath, desc);
        result.CachedPath = cachePath;
        result.IsCacheHit = false;

        if (result.Success) {
            if (result.Bytecode.SaveToFile(cachePath)) {
                AXLE_CORE_INFO("RHIShaderCompiler: Successfully cached bytecode to '{0}'", cachePath.string());
            } else {
                AXLE_CORE_WARN("RHIShaderCompiler: Failed to write bytecode cache to '{0}'", cachePath.string());
            }
        }

        return result;
    }

    RHIShaderCompileResult RHIShaderCompiler::LoadOrCompileForActiveBackend(
        const std::filesystem::path& filepath,
        const RHIShaderCompileDesc& desc,
        const std::filesystem::path& cacheDir,
        bool forceRecompile)
    {
        RHIShaderCompileDesc activeDesc = desc;
        auto backend = RHIDevice::Get().GetBackend();
        if (backend == RHIType::Vulkan) {
            activeDesc.Format = RHIShaderFormat::SPIRV;
        } else {
            activeDesc.Format = RHIShaderFormat::DXIL;
        }
        return LoadOrCompileFromFile(filepath, activeDesc, cacheDir, forceRecompile);
    }

    bool RHIShaderCompiler::InvalidateCache(
        const std::filesystem::path& sourcePath,
        const RHIShaderCompileDesc& desc,
        const std::filesystem::path& cacheDir)
    {
        std::filesystem::path cachePath = ComputeCachePath(sourcePath, desc, cacheDir);
        if (std::filesystem::exists(cachePath)) {
            std::error_code ec;
            return std::filesystem::remove(cachePath, ec);
        }
        return false;
    }

    size_t RHIShaderCompiler::ClearCache(const std::filesystem::path& cacheDir) {
        std::filesystem::path targetDir = cacheDir.empty() ? m_DefaultCacheDirectory : cacheDir;
        if (!std::filesystem::exists(targetDir)) return 0;

        size_t count = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(targetDir, ec)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".dxil" || ext == ".spv" || ext == ".bin") {
                    if (std::filesystem::remove(entry.path(), ec)) {
                        count++;
                    }
                }
            }
        }
        return count;
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

    // =========================================================================
    // Shader Reflection & Pipeline Layout Implementation
    // =========================================================================

    nri::PipelineLayoutDesc RHIAutoPipelineLayout::GetDesc() const {
        NRIDescriptorSets.clear();
        NRIDescriptorSets.reserve(DescriptorSets.size());
        for (const auto& setStorage : DescriptorSets) {
            nri::DescriptorSetDesc setDesc = {};
            setDesc.registerSpace = setStorage.RegisterSpace;
            setDesc.ranges = setStorage.Ranges.data();
            setDesc.rangeNum = static_cast<uint32_t>(setStorage.Ranges.size());
            setDesc.flags = nri::DescriptorSetBits::NONE;
            NRIDescriptorSets.push_back(setDesc);
        }

        nri::PipelineLayoutDesc desc = {};
        desc.rootRegisterSpace = RootRegisterSpace;
        desc.rootConstants = RootConstants.empty() ? nullptr : RootConstants.data();
        desc.rootConstantNum = static_cast<uint32_t>(RootConstants.size());
        desc.rootDescriptors = RootDescriptors.empty() ? nullptr : RootDescriptors.data();
        desc.rootDescriptorNum = static_cast<uint32_t>(RootDescriptors.size());
        desc.rootSamplers = RootSamplers.empty() ? nullptr : RootSamplers.data();
        desc.rootSamplerNum = static_cast<uint32_t>(RootSamplers.size());
        desc.descriptorSets = NRIDescriptorSets.empty() ? nullptr : NRIDescriptorSets.data();
        desc.descriptorSetNum = static_cast<uint32_t>(NRIDescriptorSets.size());
        desc.shaderStages = ShaderStages;
        desc.flags = Flags;
        return desc;
    }

    const RHIShaderResourceBinding* RHIShaderReflection::FindResource(const std::string& name) const {
        for (const auto& res : Resources) {
            if (res.Name == name) return &res;
        }
        return nullptr;
    }

    const RHIShaderConstantBuffer* RHIShaderReflection::FindConstantBuffer(const std::string& name) const {
        for (const auto& cb : ConstantBuffers) {
            if (cb.Name == name) return &cb;
        }
        return nullptr;
    }

    RHIAutoPipelineLayout RHIShaderReflection::BuildPipelineLayoutDesc(uint32_t rootRegisterSpace) const {
        RHIAutoPipelineLayout layout;
        layout.ShaderStages = nri::StageBits::ALL;

        auto isPushConstantName = [](const std::string& name) {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return (lower.find("pushconstant") != std::string::npos || lower.find("rootconstant") != std::string::npos ||
                    lower.find("push_constant") != std::string::npos || lower.find("root_constant") != std::string::npos);
        };

        std::map<uint32_t, std::vector<nri::DescriptorRangeDesc>> setMap;
        uint32_t maxSpace = 0;

        for (const auto& res : Resources) {
            if (res.Space > maxSpace) {
                maxSpace = res.Space;
            }

            // Check if this resource is a Root/Push Constant
            if (res.Type == RHIResourceType::ConstantBuffer && isPushConstantName(res.Name)) {
                const auto* cb = FindConstantBuffer(res.Name);
                uint32_t size = cb ? cb->Size : 128;
                if (size == 0) size = 128;
                size = (size + 3) & ~3; // multiple of 4

                bool found = false;
                for (auto& existingRc : layout.RootConstants) {
                    if (existingRc.registerIndex == res.BindPoint) {
                        existingRc.shaderStages |= RHIShaderCompiler::StageToNRI(res.Stage);
                        existingRc.size = std::max(existingRc.size, size);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    nri::RootConstantDesc rc = {};
                    rc.registerIndex = res.BindPoint;
                    rc.size = size;
                    rc.shaderStages = RHIShaderCompiler::StageToNRI(res.Stage);
                    layout.RootConstants.push_back(rc);
                }
                continue;
            }

            // Map resource type to NRI DescriptorType
            nri::DescriptorType nriType = nri::DescriptorType::CONSTANT_BUFFER;
            switch (res.Type) {
                case RHIResourceType::ConstantBuffer:
                    nriType = nri::DescriptorType::CONSTANT_BUFFER;
                    break;
                case RHIResourceType::Texture:
                    nriType = nri::DescriptorType::TEXTURE;
                    break;
                case RHIResourceType::Sampler:
                    nriType = nri::DescriptorType::SAMPLER;
                    break;
                case RHIResourceType::StorageTexture:
                    nriType = nri::DescriptorType::STORAGE_TEXTURE;
                    break;
                case RHIResourceType::StructuredBuffer:
                    nriType = nri::DescriptorType::STRUCTURED_BUFFER;
                    break;
                case RHIResourceType::StorageBuffer:
                    nriType = nri::DescriptorType::STORAGE_STRUCTURED_BUFFER;
                    break;
                case RHIResourceType::AccelerationStructure:
                    nriType = nri::DescriptorType::ACCELERATION_STRUCTURE;
                    break;
                default:
                    nriType = nri::DescriptorType::CONSTANT_BUFFER;
                    break;
            }

            auto& ranges = setMap[res.Space];
            bool mergedRange = false;
            for (auto& existingRange : ranges) {
                if (existingRange.baseRegisterIndex == res.BindPoint && existingRange.descriptorType == nriType) {
                    existingRange.shaderStages |= RHIShaderCompiler::StageToNRI(res.Stage);
                    existingRange.descriptorNum = std::max(existingRange.descriptorNum, res.BindCount);
                    mergedRange = true;
                    break;
                }
            }

            if (!mergedRange) {
                nri::DescriptorRangeDesc range = {};
                range.baseRegisterIndex = res.BindPoint;
                range.descriptorNum = res.BindCount;
                range.descriptorType = nriType;
                range.shaderStages = RHIShaderCompiler::StageToNRI(res.Stage);
                range.flags = nri::DescriptorRangeBits::NONE;
                ranges.push_back(range);
            }
        }

        // NRI requires rootRegisterSpace to avoid collisions with descriptor set spaces
        if (rootRegisterSpace == 0 && !layout.RootConstants.empty() && setMap.find(0) != setMap.end()) {
            layout.RootRegisterSpace = maxSpace + 1;
        } else {
            layout.RootRegisterSpace = rootRegisterSpace;
        }

        for (auto& [space, ranges] : setMap) {
            RHIAutoPipelineLayout::DescriptorSetStorage setStorage;
            setStorage.RegisterSpace = space;
            setStorage.Ranges = std::move(ranges);
            layout.DescriptorSets.push_back(std::move(setStorage));
        }

        return layout;
    }

    RHIShaderReflection RHIShaderReflection::Merge(const std::vector<RHIShaderReflection>& reflections) {
        RHIShaderReflection merged;
        if (reflections.empty()) return merged;

        merged.Format = reflections.front().Format;
        merged.Stage = reflections.front().Stage;
        merged.EntryPoint = reflections.front().EntryPoint;

        for (const auto& refl : reflections) {
            // Merge Resources
            for (const auto& res : refl.Resources) {
                bool found = false;
                for (auto& existing : merged.Resources) {
                    if (existing.Name == res.Name && existing.Space == res.Space && existing.BindPoint == res.BindPoint) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    merged.Resources.push_back(res);
                }
            }

            // Merge Constant Buffers
            for (const auto& cb : refl.ConstantBuffers) {
                bool found = false;
                for (auto& existingCb : merged.ConstantBuffers) {
                    if (existingCb.Name == cb.Name) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    merged.ConstantBuffers.push_back(cb);
                }
            }

            // Merge Vertex Inputs (from Vertex Shader)
            if (refl.Stage == RHIShaderStage::Vertex && !refl.VertexInputs.empty()) {
                merged.VertexInputs = refl.VertexInputs;
            }

            // Merge Thread Group Dimensions (from Compute Shader)
            if (refl.Stage == RHIShaderStage::Compute) {
                merged.ThreadGroupSizeX = refl.ThreadGroupSizeX;
                merged.ThreadGroupSizeY = refl.ThreadGroupSizeY;
                merged.ThreadGroupSizeZ = refl.ThreadGroupSizeZ;
            }
        }

        return merged;
    }

    RHIShaderReflection RHIShaderCompiler::Reflect(
        const void* reflectionData,
        size_t reflectionSize,
        RHIShaderStage stage,
        RHIShaderFormat format,
        const std::string& entryPoint)
    {
        RHIShaderReflection reflection;
        reflection.Stage = stage;
        reflection.Format = format;
        reflection.EntryPoint = entryPoint;

        if (!m_Initialized) {
            if (!Init()) return reflection;
        }
        if (!reflectionData || reflectionSize == 0) return reflection;

        auto* pUtils = static_cast<IDxcUtils*>(m_DxcUtils);
        if (!pUtils) return reflection;

        DxcBuffer buffer = {};
        buffer.Ptr = reflectionData;
        buffer.Size = reflectionSize;
        buffer.Encoding = 0;

        static const GUID IID_ID3D12ShaderReflection_Val = {
            0x5A58797D, 0xA72C, 0x478D, { 0x8B, 0xA2, 0xEF, 0xC6, 0xB0, 0xEF, 0xE8, 0x8E }
        };

        ID3D12ShaderReflection* pShaderReflection = nullptr;
        HRESULT hr = pUtils->CreateReflection(&buffer, IID_ID3D12ShaderReflection_Val, reinterpret_cast<void**>(&pShaderReflection));
        if (FAILED(hr) || !pShaderReflection) {
            AXLE_CORE_WARN("RHIShaderCompiler::Reflect: CreateReflection failed with HRESULT 0x{0:x}", static_cast<uint32_t>(hr));
            return reflection;
        }

        D3D12_SHADER_DESC shaderDesc = {};
        if (SUCCEEDED(pShaderReflection->GetDesc(&shaderDesc))) {
            // 1. Thread group size (for compute shaders)
            pShaderReflection->GetThreadGroupSize(
                &reflection.ThreadGroupSizeX,
                &reflection.ThreadGroupSizeY,
                &reflection.ThreadGroupSizeZ
            );

            // 2. Bound resources (SRV, UAV, CBV, Sampler)
            for (UINT i = 0; i < shaderDesc.BoundResources; ++i) {
                D3D12_SHADER_INPUT_BIND_DESC bindDesc = {};
                if (FAILED(pShaderReflection->GetResourceBindingDesc(i, &bindDesc))) continue;

                RHIShaderResourceBinding binding;
                binding.Name = bindDesc.Name ? bindDesc.Name : "";
                binding.BindPoint = bindDesc.BindPoint;
                binding.BindCount = bindDesc.BindCount;
                binding.Space = bindDesc.Space;
                binding.Stage = stage;

                switch (bindDesc.Type) {
                    case D3D_SIT_CBUFFER:
                    case D3D_SIT_TBUFFER:
                        binding.Type = RHIResourceType::ConstantBuffer;
                        break;
                    case D3D_SIT_TEXTURE:
                        binding.Type = RHIResourceType::Texture;
                        break;
                    case D3D_SIT_SAMPLER:
                        binding.Type = RHIResourceType::Sampler;
                        break;
                    case D3D_SIT_UAV_RWTYPED:
                        binding.Type = RHIResourceType::StorageTexture;
                        break;
                    case D3D_SIT_STRUCTURED:
                    case D3D_SIT_BYTEADDRESS:
                        binding.Type = RHIResourceType::StructuredBuffer;
                        break;
                    case D3D_SIT_UAV_RWSTRUCTURED:
                    case D3D_SIT_UAV_RWBYTEADDRESS:
                    case D3D_SIT_UAV_APPEND_STRUCTURED:
                    case D3D_SIT_UAV_CONSUME_STRUCTURED:
                    case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                        binding.Type = RHIResourceType::StorageBuffer;
                        break;
                    case D3D_SIT_RTACCELERATIONSTRUCTURE:
                        binding.Type = RHIResourceType::AccelerationStructure;
                        break;
                    default:
                        binding.Type = RHIResourceType::Unknown;
                        break;
                }

                reflection.Resources.push_back(std::move(binding));
            }

            // 3. Constant buffers and their variables
            for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i) {
                ID3D12ShaderReflectionConstantBuffer* pCb = pShaderReflection->GetConstantBufferByIndex(i);
                if (!pCb) continue;

                D3D12_SHADER_BUFFER_DESC cbDesc = {};
                if (FAILED(pCb->GetDesc(&cbDesc))) continue;

                RHIShaderConstantBuffer cb;
                cb.Name = cbDesc.Name ? cbDesc.Name : "";
                cb.Size = cbDesc.Size;

                // Match with bound resources to extract bind point & space
                for (const auto& res : reflection.Resources) {
                    if (res.Type == RHIResourceType::ConstantBuffer && res.Name == cb.Name) {
                        cb.BindPoint = res.BindPoint;
                        cb.Space = res.Space;
                        break;
                    }
                }

                for (UINT v = 0; v < cbDesc.Variables; ++v) {
                    ID3D12ShaderReflectionVariable* pVar = pCb->GetVariableByIndex(v);
                    if (!pVar) continue;

                    D3D12_SHADER_VARIABLE_DESC varDesc = {};
                    if (FAILED(pVar->GetDesc(&varDesc))) continue;

                    RHIShaderVariable var;
                    var.Name = varDesc.Name ? varDesc.Name : "";
                    var.Offset = varDesc.StartOffset;
                    var.Size = varDesc.Size;
                    cb.Variables.push_back(std::move(var));
                }

                reflection.ConstantBuffers.push_back(std::move(cb));
            }

            // 4. Vertex inputs (Input Parameters)
            for (UINT i = 0; i < shaderDesc.InputParameters; ++i) {
                D3D12_SIGNATURE_PARAMETER_DESC paramDesc = {};
                if (FAILED(pShaderReflection->GetInputParameterDesc(i, &paramDesc))) continue;

                RHIVertexAttribute attr;
                attr.SemanticName = paramDesc.SemanticName ? paramDesc.SemanticName : "";
                attr.SemanticIndex = paramDesc.SemanticIndex;
                attr.Register = paramDesc.Register;

                uint32_t count = 0;
                BYTE mask = paramDesc.Mask;
                while (mask > 0) {
                    if (mask & 1) count++;
                    mask >>= 1;
                }
                attr.ComponentCount = count > 0 ? count : 4;

                reflection.VertexInputs.push_back(std::move(attr));
            }
        }

        pShaderReflection->Release();
        return reflection;
    }

    RHIShaderReflection RHIShaderCompiler::Reflect(const RHIShaderBytecode& bytecode) {
        if (!bytecode.IsValid()) return RHIShaderReflection{};
        return Reflect(bytecode.GetData(), bytecode.GetSize(), bytecode.Stage, bytecode.Format, bytecode.EntryPoint);
    }

    RHIShaderReflection RHIShaderCompiler::ReflectFromSource(const RHIShaderCompileDesc& desc) {
        if (!m_Initialized) {
            if (!Init()) return RHIShaderReflection{};
        }

        auto* pCompiler = static_cast<IDxcCompiler3*>(m_DxcCompiler);
        auto* pIncludeHandler = static_cast<IDxcIncludeHandler*>(m_DxcIncludeHandler);
        if (!pCompiler) return RHIShaderReflection{};

        DxcBuffer sourceBuffer = {};
        sourceBuffer.Ptr = desc.Source.data();
        sourceBuffer.Size = desc.Source.size();
        sourceBuffer.Encoding = DXC_CP_UTF8;

        std::vector<std::wstring> wargs;
        wargs.push_back(L"-E");
        wargs.push_back(ToWide(desc.EntryPoint));
        wargs.push_back(L"-T");
        wargs.push_back(ToWide(GetProfileTarget(desc.Stage, desc.ShaderModel)));
        wargs.push_back(L"-O0");
        wargs.push_back(L"-Zpc");

        for (const auto& macro : desc.Macros) {
            std::wstring def = ToWide(macro.Name);
            if (!macro.Definition.empty()) {
                def += L"=" + ToWide(macro.Definition);
            }
            wargs.push_back(L"-D");
            wargs.push_back(def);
        }
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

        RHIShaderReflection reflection;
        reflection.Stage = desc.Stage;
        reflection.Format = desc.Format;
        reflection.EntryPoint = desc.EntryPoint;

        if (SUCCEEDED(hr) && pCompileResult) {
            IDxcBlob* pReflBlob = nullptr;
            if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&pReflBlob), nullptr)) && pReflBlob) {
                reflection = Reflect(pReflBlob->GetBufferPointer(), pReflBlob->GetBufferSize(), desc.Stage, desc.Format, desc.EntryPoint);
                pReflBlob->Release();
            } else {
                IDxcBlob* pObjBlob = nullptr;
                if (SUCCEEDED(pCompileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pObjBlob), nullptr)) && pObjBlob) {
                    reflection = Reflect(pObjBlob->GetBufferPointer(), pObjBlob->GetBufferSize(), desc.Stage, desc.Format, desc.EntryPoint);
                    pObjBlob->Release();
                }
            }
            pCompileResult->Release();
        }

        return reflection;
    }

}

