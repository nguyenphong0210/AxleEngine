#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDevice.hpp>

#include <NRI.h>

#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>

namespace Axle::RHI {

    enum class RHIShaderStage : uint8_t {
        Vertex,
        Fragment, // alias Pixel
        Compute,
        Geometry,
        Hull,     // Tessellation Control
        Domain,   // Tessellation Evaluation
        Task,     // Amplification
        Mesh,
        RayGen,
        Miss,
        ClosestHit,
        AnyHit,
        Intersection,
        Callable
    };

    enum class RHIShaderFormat : uint8_t {
        DXIL,   // Direct3D 12 standard bytecode (SM 6.0+)
        SPIRV,  // Vulkan 1.3 standard bytecode
        DXBC    // Legacy D3D11 / fallback bytecode (SM 5.0)
    };

    struct RHIShaderMacro {
        std::string Name;
        std::string Definition;
    };

    struct RHIShaderCompileDesc {
        std::string Source;                          // HLSL source code string
        std::string SourcePath;                      // Optional file path (for debug info & relative includes)
        std::string EntryPoint = "main";             // Entry point function name
        RHIShaderStage Stage = RHIShaderStage::Vertex;
        RHIShaderFormat Format = RHIShaderFormat::DXIL;
        std::string ShaderModel = "6_0";             // "6_0", "6_2", "6_5", "6_6"
        std::vector<RHIShaderMacro> Macros;          // Preprocessor macro definitions
        std::vector<std::string> IncludeDirs;        // Additional include directories
        bool Optimization = true;                    // Enable -O3 optimization
        bool GenerateDebugInfo = false;              // Embed debug symbols
        bool EnableVulkanDXLayout = true;            // -fvk-use-dx-layout for SPIR-V
        std::string VulkanTargetEnv = "vulkan1.3";   // -fspv-target-env=vulkan1.3
    };

    struct RHIShaderBytecode {
        std::vector<uint8_t> Data;
        RHIShaderFormat Format = RHIShaderFormat::DXIL;
        RHIShaderStage Stage = RHIShaderStage::Vertex;
        std::string EntryPoint = "main";

        bool IsValid() const { return !Data.empty(); }
        size_t GetSize() const { return Data.size(); }
        const uint8_t* GetData() const { return Data.data(); }

        // Convert to NRI ShaderDesc descriptor ready for pipeline creation
        nri::ShaderDesc ToNRIShaderDesc() const;

        // Persist bytecode to disk
        bool SaveToFile(const std::filesystem::path& filepath) const;

        // Load precompiled bytecode from disk
        static RHIShaderBytecode LoadFromFile(const std::filesystem::path& filepath,
                                              RHIShaderFormat format,
                                              RHIShaderStage stage,
                                              const std::string& entryPoint = "main");
    };

    struct RHIShaderCompileResult {
        bool Success = false;
        RHIShaderBytecode Bytecode;
        std::string ErrorMessage;
        std::string WarningMessage;
        bool IsCacheHit = false;             // True if loaded directly from valid bytecode cache
        std::filesystem::path CachedPath;    // Filepath to the cached .dxil or .spv bytecode
    };

    class RHIShaderCompiler {
    public:
        static RHIShaderCompiler& Get();

        RHIShaderCompiler() = default;
        ~RHIShaderCompiler();

        RHIShaderCompiler(const RHIShaderCompiler&) = delete;
        RHIShaderCompiler& operator=(const RHIShaderCompiler&) = delete;

        // Initialize DXC Compiler and runtime dependencies
        bool Init();

        // Release DXC Compiler resources
        void Shutdown();

        bool IsInitialized() const { return m_Initialized; }

        // Compile HLSL source string to target bytecode (DXIL or SPIR-V)
        RHIShaderCompileResult Compile(const RHIShaderCompileDesc& desc);

        // Compile HLSL shader file from disk
        RHIShaderCompileResult CompileFromFile(const std::filesystem::path& filepath, const RHIShaderCompileDesc& desc);

        // Compile shader targeting the currently active RHI backend (D3D12 -> DXIL, Vulkan -> SPIR-V)
        RHIShaderCompileResult CompileForActiveBackend(const RHIShaderCompileDesc& desc);

        // Default shader cache directory (e.g. "assets/cache/shaders")
        const std::filesystem::path& GetDefaultCacheDirectory() const { return m_DefaultCacheDirectory; }
        void SetDefaultCacheDirectory(const std::filesystem::path& dir) { m_DefaultCacheDirectory = dir; }

        // Compute deterministic cache file path for given shader file and compile parameters
        std::filesystem::path ComputeCachePath(const std::filesystem::path& sourcePath,
                                               const RHIShaderCompileDesc& desc,
                                               const std::filesystem::path& cacheDir = {}) const;

        // Compile HLSL shader file with automated bytecode caching & last_write_time dependency validation
        // Returns cached bytecode if cache file is newer than source file and its include dependencies.
        RHIShaderCompileResult LoadOrCompileFromFile(const std::filesystem::path& filepath,
                                                    const RHIShaderCompileDesc& desc,
                                                    const std::filesystem::path& cacheDir = {},
                                                    bool forceRecompile = false);

        // Compile or load cached shader targeting the active RHI backend (D3D12 -> DXIL, Vulkan -> SPIR-V)
        RHIShaderCompileResult LoadOrCompileForActiveBackend(const std::filesystem::path& filepath,
                                                           const RHIShaderCompileDesc& desc,
                                                           const std::filesystem::path& cacheDir = {},
                                                           bool forceRecompile = false);

        // Invalidate specific cached bytecode
        bool InvalidateCache(const std::filesystem::path& sourcePath,
                             const RHIShaderCompileDesc& desc,
                             const std::filesystem::path& cacheDir = {});

        // Clear all cached shader bytecode files (.dxil and .spv) in cache directory
        size_t ClearCache(const std::filesystem::path& cacheDir = {});

        // Extract recursive #include dependencies for a given shader file
        static std::vector<std::filesystem::path> ExtractIncludeDependencies(const std::filesystem::path& filepath,
                                                                             const std::vector<std::string>& includeDirs);

        // Convert shader stage enum to DXC profile target string (e.g. "vs_6_0", "ps_6_0", "cs_6_0")
        static std::string GetProfileTarget(RHIShaderStage stage, const std::string& shaderModel = "6_0");

        // Convert RHIShaderStage to nri::StageBits
        static nri::StageBits StageToNRI(RHIShaderStage stage);

        // String representation of shader format
        static const char* FormatToString(RHIShaderFormat format);

        // String representation of shader stage
        static const char* StageToString(RHIShaderStage stage);

    private:
        bool LoadDXCLibrary();

    private:
        bool m_Initialized = false;
        std::filesystem::path m_DefaultCacheDirectory = "assets/cache/shaders";
        void* m_DxcDll = nullptr;
        void* m_DxilDll = nullptr;
        void* m_DxcUtils = nullptr;          // IDxcUtils*
        void* m_DxcCompiler = nullptr;       // IDxcCompiler3*
        void* m_DxcIncludeHandler = nullptr; // IDxcIncludeHandler*
    };

}
