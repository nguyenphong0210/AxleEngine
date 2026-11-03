#include "MaterialTestHarness.hpp"
#include "Test_MaterialAcceptance.hpp"
#include <Axle/ShaderDragDropDecoder.hpp>
#include <Axle/ShaderPropertyReflection.hpp>
#include <cstring>

using namespace Axle;
using namespace Axle::MaterialTests;

namespace {

    bool TestEmptyAndNullPayloads() {
        // Null pointer
        auto resNull = DecodeShaderPayload(nullptr, 100);
        MAT_TEST_CHECK(!resNull.Success, "Null data must fail");
        MAT_TEST_CHECK(!resNull.Error.empty(), "Error message required for null data");

        // Zero size
        const char buf[] = "shaders/Valid.hlsl";
        auto resZero = DecodeShaderPayload(buf, 0);
        MAT_TEST_CHECK(!resZero.Success, "Zero size must fail");

        // Blank string with spaces
        const char spaces[] = "   \t \r \n  ";
        auto resSpaces = DecodeShaderPayload(spaces, sizeof(spaces) - 1);
        MAT_TEST_CHECK(!resSpaces.Success, "Whitespace-only payload must fail");

        // Single null byte
        const char nullOnly[] = "\0";
        auto resNullOnly = DecodeShaderPayload(nullOnly, 1);
        MAT_TEST_CHECK(!resNullOnly.Success, "Empty string starting with null byte must fail");

        // Overflow safety check (exceeding 4096 bytes)
        std::vector<char> huge(5000, 'a');
        auto resHuge = DecodeShaderPayload(huge.data(), huge.size());
        MAT_TEST_CHECK(!resHuge.Success, "Payload exceeding maximum path bytes must fail");

        return true;
    }

    bool TestExtensionAndCaseHandling() {
        // Standard lowercase .hlsl
        const char p1[] = "assets/shaders/CustomPBR.hlsl";
        auto r1 = DecodeShaderPayload(p1, sizeof(p1));
        MAT_TEST_CHECK(r1.Success, "Standard .hlsl must succeed");
        MAT_TEST_CHECK(r1.Path == std::filesystem::path("assets/shaders/CustomPBR.hlsl"), "Path mismatch");

        // Uppercase .HLSL
        const char p2[] = "assets/shaders/CAR_PAINT.HLSL";
        auto r2 = DecodeShaderPayload(p2, sizeof(p2));
        MAT_TEST_CHECK(r2.Success, "Uppercase .HLSL must succeed");
        MAT_TEST_CHECK(r2.Path == std::filesystem::path("assets/shaders/CAR_PAINT.HLSL"), "Path mismatch");

        // Mixed case .Hlsl
        const char p3[] = "assets/shaders/WaterSurface.Hlsl";
        auto r3 = DecodeShaderPayload(p3, sizeof(p3));
        MAT_TEST_CHECK(r3.Success, "Mixed case .Hlsl must succeed");

        // Lowercase and uppercase .shader
        const char p4[] = "assets/shaders/UberMaterial.shader";
        auto r4 = DecodeShaderPayload(p4, sizeof(p4));
        MAT_TEST_CHECK(r4.Success, ".shader must succeed");

        const char p5[] = "assets/shaders/UberMaterial.SHADER";
        auto r5 = DecodeShaderPayload(p5, sizeof(p5));
        MAT_TEST_CHECK(r5.Success, "Uppercase .SHADER must succeed");

        // Wrong extensions
        const char p6[] = "assets/textures/Albedo.png";
        auto r6 = DecodeShaderPayload(p6, sizeof(p6));
        MAT_TEST_CHECK(!r6.Success, "Non-shader .png extension must fail");
        MAT_TEST_CHECK(r6.Error.find(".png") != std::string::npos, "Error should report rejected extension");

        const char p7[] = "assets/materials/RedCar.axlemat";
        auto r7 = DecodeShaderPayload(p7, sizeof(p7));
        MAT_TEST_CHECK(!r7.Success, "Non-shader .axlemat extension must fail");

        // No extension
        const char p8[] = "assets/shaders/MyShaderFile";
        auto r8 = DecodeShaderPayload(p8, sizeof(p8));
        MAT_TEST_CHECK(!r8.Success, "File with no extension must fail");

        return true;
    }

    bool TestUnterminatedAndBoundedBuffers() {
        // Unterminated buffer without trailing '\0'
        const char rawPath[] = "assets/shaders/Unterminated.hlsl";
        size_t exactLen = std::strlen(rawPath);
        auto r1 = DecodeShaderPayload(rawPath, exactLen); // Notice: size is exactly strlen, no null byte!
        MAT_TEST_CHECK(r1.Success, "Unterminated buffer bounded by size must succeed");
        MAT_TEST_CHECK(r1.Path == std::filesystem::path("assets/shaders/Unterminated.hlsl"), "Unterminated path content mismatch");

        // Buffer with garbage bytes past the null terminator
        char garbageBuf[128];
        std::memset(garbageBuf, 'X', sizeof(garbageBuf));
        const char validPart[] = "assets/shaders/WithGarbage.hlsl";
        std::memcpy(garbageBuf, validPart, sizeof(validPart)); // copies with trailing '\0'
        auto r2 = DecodeShaderPayload(garbageBuf, sizeof(garbageBuf));
        MAT_TEST_CHECK(r2.Success, "Buffer with garbage after null terminator must terminate at null");
        MAT_TEST_CHECK(r2.Path == std::filesystem::path("assets/shaders/WithGarbage.hlsl"), "Garbage buffer path mismatch");

        // Whitespace and quotes trimming
        const char quoted[] = "  \"assets/shaders/QuotedPath.hlsl\"  \r\n";
        auto r3 = DecodeShaderPayload(quoted, sizeof(quoted));
        MAT_TEST_CHECK(r3.Success, "Quoted and padded string must succeed");
        MAT_TEST_CHECK(r3.Path == std::filesystem::path("assets/shaders/QuotedPath.hlsl"), "Trimmed path mismatch");

        return true;
    }

    bool TestNonAsciiUtf8Payloads() {
        // UTF-8 path with Vietnamese accents and umlauts
        const char utf8Path[] = u8"assets/shaders/VậtLiệu_MàuSơn.hlsl";
        auto r1 = DecodeShaderPayload(utf8Path, sizeof(utf8Path));
        MAT_TEST_CHECK(r1.Success, "Non-ASCII UTF-8 path must succeed");
#if defined(__cpp_lib_char8_t)
        auto expected = std::filesystem::path(u8"assets/shaders/VậtLiệu_MàuSơn.hlsl");
#else
        auto expected = std::filesystem::u8path("assets/shaders/VậtLiệu_MàuSơn.hlsl");
#endif
        MAT_TEST_CHECK(r1.Path == expected, "UTF-8 path representation mismatch");

        return true;
    }

    bool TestCustomPropertySynchronization() {
        // Build initial instances representing user-edited properties in inspector
        std::vector<ShaderPropertyInstance> existingInstances;
        {
            ShaderPropertyInstance p1;
            p1.Desc.Name = "u_RoughnessMultiplier";
            p1.Desc.Type = ShaderPropertyType::Float;
            p1.FloatVal = 0.77f;
            existingInstances.push_back(p1);

            ShaderPropertyInstance p2;
            p2.Desc.Name = "u_CustomTint";
            p2.Desc.Type = ShaderPropertyType::Color;
            p2.Vec4Val = glm::vec4(1.0f, 0.5f, 0.2f, 1.0f);
            existingInstances.push_back(p2);

            ShaderPropertyInstance p3;
            p3.Desc.Name = "u_SampleCount";
            p3.Desc.Type = ShaderPropertyType::Int;
            p3.IntVal = 16;
            existingInstances.push_back(p3);

            ShaderPropertyInstance p4;
            p4.Desc.Name = "u_PatternMap";
            p4.Desc.Type = ShaderPropertyType::Texture2D;
            p4.TexturePath = "textures/custom_pattern.png";
            existingInstances.push_back(p4);

            ShaderPropertyInstance p5;
            p5.Desc.Name = "u_ModeChange";
            p5.Desc.Type = ShaderPropertyType::Int;
            p5.IntVal = 42;
            existingInstances.push_back(p5);
        }

        // New shader has:
        // - u_RoughnessMultiplier (same type -> preserve 0.77f)
        // - u_CustomTint (same type -> preserve vec4)
        // - u_SampleCount (same type -> preserve 16)
        // - u_PatternMap (same type -> preserve texture path)
        // - u_ModeChange (changed to Float -> must reset to default!)
        // - u_BrandNewParam (new -> default 0.5f)
        // - u_Model, u_View, u_BaseColor (engine uniforms -> must be ignored!)
        std::vector<ShaderPropertyDesc> newDescs;
        {
            ShaderPropertyDesc d1;
            d1.Name = "u_RoughnessMultiplier";
            d1.Type = ShaderPropertyType::Float;
            d1.DefaultFloat = 0.1f;
            newDescs.push_back(d1);

            ShaderPropertyDesc d2;
            d2.Name = "u_CustomTint";
            d2.Type = ShaderPropertyType::Color;
            d2.DefaultVec4 = glm::vec4(1.0f);
            newDescs.push_back(d2);

            ShaderPropertyDesc d3;
            d3.Name = "u_SampleCount";
            d3.Type = ShaderPropertyType::Int;
            d3.DefaultInt = 4;
            newDescs.push_back(d3);

            ShaderPropertyDesc d4;
            d4.Name = "u_PatternMap";
            d4.Type = ShaderPropertyType::Texture2D;
            newDescs.push_back(d4);

            ShaderPropertyDesc d5;
            d5.Name = "u_ModeChange";
            d5.Type = ShaderPropertyType::Float; // Changed type!
            d5.DefaultFloat = 3.14f;
            newDescs.push_back(d5);

            ShaderPropertyDesc d6;
            d6.Name = "u_BrandNewParam";
            d6.Type = ShaderPropertyType::Float;
            d6.HasRange = true;
            d6.DefaultFloat = 0.5f;
            newDescs.push_back(d6);

            // Engine uniforms that must be filtered out
            ShaderPropertyDesc eng1; eng1.Name = "u_Model"; eng1.Type = ShaderPropertyType::Float4; newDescs.push_back(eng1);
            ShaderPropertyDesc eng2; eng2.Name = "u_View"; eng2.Type = ShaderPropertyType::Float4; newDescs.push_back(eng2);
            ShaderPropertyDesc eng3; eng3.Name = "u_BaseColor"; eng3.Type = ShaderPropertyType::Color; newDescs.push_back(eng3);
        }

        auto syncd = SynchronizeCustomProperties(newDescs, existingInstances);

        // Verify count: 6 user properties (all 3 engine uniforms filtered out)
        MAT_TEST_CHECK(syncd.size() == 6, "Expected 6 user properties (engine uniforms must be excluded)");

        // Map syncd properties by name
        std::unordered_map<std::string, const ShaderPropertyInstance*> syncdMap;
        for (const auto& item : syncd) syncdMap[item.Desc.Name] = &item;

        // 1. Preserved compatible float
        MAT_TEST_CHECK(syncdMap.count("u_RoughnessMultiplier") > 0, "Missing u_RoughnessMultiplier");
        MAT_TEST_APPROX_EQUAL(syncdMap["u_RoughnessMultiplier"]->FloatVal, 0.77f, 1e-5f, "Compatible FloatVal must be preserved");

        // 2. Preserved compatible color
        MAT_TEST_CHECK(syncdMap.count("u_CustomTint") > 0, "Missing u_CustomTint");
        MAT_TEST_APPROX_EQUAL(syncdMap["u_CustomTint"]->Vec4Val.y, 0.5f, 1e-5f, "Compatible Vec4Val must be preserved");

        // 3. Preserved compatible int
        MAT_TEST_CHECK(syncdMap.count("u_SampleCount") > 0, "Missing u_SampleCount");
        MAT_TEST_CHECK(syncdMap["u_SampleCount"]->IntVal == 16, "Compatible IntVal must be preserved");

        // 4. Preserved compatible texture
        MAT_TEST_CHECK(syncdMap.count("u_PatternMap") > 0, "Missing u_PatternMap");
        MAT_TEST_CHECK(syncdMap["u_PatternMap"]->TexturePath == "textures/custom_pattern.png", "Compatible texture path must be preserved");

        // 5. Incompatible type reset to new default
        MAT_TEST_CHECK(syncdMap.count("u_ModeChange") > 0, "Missing u_ModeChange");
        MAT_TEST_CHECK(syncdMap["u_ModeChange"]->Desc.Type == ShaderPropertyType::Float, "u_ModeChange must adopt new type");
        MAT_TEST_APPROX_EQUAL(syncdMap["u_ModeChange"]->FloatVal, 3.14f, 1e-5f, "Incompatible type must adopt default value");

        // 6. Brand new property receives default
        MAT_TEST_CHECK(syncdMap.count("u_BrandNewParam") > 0, "Missing u_BrandNewParam");
        MAT_TEST_APPROX_EQUAL(syncdMap["u_BrandNewParam"]->FloatVal, 0.5f, 1e-5f, "New property must adopt default value");

        // 7. Verify engine uniforms are not present
        MAT_TEST_CHECK(syncdMap.count("u_Model") == 0, "Engine uniform u_Model must not be present");
        MAT_TEST_CHECK(syncdMap.count("u_View") == 0, "Engine uniform u_View must not be present");
        MAT_TEST_CHECK(syncdMap.count("u_BaseColor") == 0, "Engine uniform u_BaseColor must not be present");

        return true;
    }

    bool TestFunctionLocalVariableAndEngineUniformFiltering() {
        // HLSL snippet containing cbuffer parameters, engine uniforms, and function local variables
        const std::string hlslSource = R"(
            cbuffer ObjectConstants : register(b0, space0) {
                float4x4 u_Model;
                float4   u_BaseColor;
            };

            cbuffer MaterialParameters : register(b2, space0) {
                [Range(0.0, 1.0)]
                float u_ExposedRoughness = 0.5f;

                [Color]
                float4 u_ExposedTint;
            };

            Texture2D u_ExposedTexture : register(t0, space1);

            float4 PSMain() : SV_Target0 {
                float localVariableA = 12.34f;
                int localLoopCounter = 0;
                float4 localColor = float4(1, 1, 1, 1);
                return localColor * localVariableA;
            }
        )";

        auto parsed = ShaderPropertyParser::ParseFromHLSL(hlslSource);

        std::unordered_map<std::string, const ShaderPropertyDesc*> parsedMap;
        for (const auto& p : parsed) parsedMap[p.Name] = &p;

        // Check valid properties
        MAT_TEST_CHECK(parsedMap.count("u_ExposedRoughness") > 0, "u_ExposedRoughness must be parsed");
        MAT_TEST_CHECK(parsedMap["u_ExposedRoughness"]->HasRange, "u_ExposedRoughness must have range");
        MAT_TEST_CHECK(parsedMap.count("u_ExposedTint") > 0, "u_ExposedTint must be parsed");
        MAT_TEST_CHECK(parsedMap.count("u_ExposedTexture") > 0, "u_ExposedTexture must be parsed");

        // Check engine uniforms are filtered out
        MAT_TEST_CHECK(parsedMap.count("u_Model") == 0, "Engine uniform u_Model must be filtered out");
        MAT_TEST_CHECK(parsedMap.count("u_BaseColor") == 0, "Engine uniform u_BaseColor must be filtered out");

        // Check function-local variables are filtered out
        MAT_TEST_CHECK(parsedMap.count("localVariableA") == 0, "Function local variable localVariableA must not be parsed");
        MAT_TEST_CHECK(parsedMap.count("localLoopCounter") == 0, "Function local variable localLoopCounter must not be parsed");
        MAT_TEST_CHECK(parsedMap.count("localColor") == 0, "Function local variable localColor must not be parsed");

        auto defaults=ShaderPropertyParser::ParseFromHLSL(R"(
            // float Commented; {
            /* } float AlsoCommented; */
            cbuffer MaterialParameters {
                // [Default(1)]
                float Gain;
                float2 Offset = float2(-2, 3e-1);
                bool Active = true;
                // [Range(0.2, 0.8)]
                float Ranged;
            };
            float4 PSMain() { /* } */ float Hidden; return 1; }
        )");
        auto instances=SynchronizeCustomProperties(defaults,{});
        MAT_TEST_CHECK(instances.size()==4,"Comments, braces and one-line function locals never become properties");
        MAT_TEST_CHECK(instances[0].FloatVal==1 && instances[1].Vec4Val.x==-2 && std::abs(instances[1].Vec4Val.y-0.3f)<0.0001f && instances[2].BoolVal && instances[3].FloatVal==0.2f,"Scalar, vector, bool and range defaults initialize correctly");

        return true;
    }

    bool TestMaterialAcceptanceHelpers() {
        // 1. Backend option parsing
        RHI::RHIType backend;
        std::string err;

        // Unset / empty -> D3D12
        MAT_TEST_CHECK(ParseMaterialBackendOption(nullptr, backend, err), "Null backend must succeed");
        MAT_TEST_CHECK(backend == RHI::RHIType::Direct3D12, "Null backend must default to Direct3D12");

        MAT_TEST_CHECK(ParseMaterialBackendOption("", backend, err), "Empty backend must succeed");
        MAT_TEST_CHECK(backend == RHI::RHIType::Direct3D12, "Empty backend must default to Direct3D12");

        // "dx12" / "d3d12" -> D3D12
        MAT_TEST_CHECK(ParseMaterialBackendOption("dx12", backend, err), "'dx12' must parse");
        MAT_TEST_CHECK(backend == RHI::RHIType::Direct3D12, "Expected Direct3D12");

        MAT_TEST_CHECK(ParseMaterialBackendOption("D3D12", backend, err), "'D3D12' uppercase must parse");
        MAT_TEST_CHECK(backend == RHI::RHIType::Direct3D12, "Expected Direct3D12");

        // "vulkan" / "vk" -> Vulkan
        MAT_TEST_CHECK(ParseMaterialBackendOption("vulkan", backend, err), "'vulkan' must parse");
        MAT_TEST_CHECK(backend == RHI::RHIType::Vulkan, "Expected Vulkan");

        MAT_TEST_CHECK(ParseMaterialBackendOption("VK", backend, err), "'VK' must parse");
        MAT_TEST_CHECK(backend == RHI::RHIType::Vulkan, "Expected Vulkan");

        // Unknown backend -> reject with error
        MAT_TEST_CHECK(!ParseMaterialBackendOption("opengl", backend, err), "Unknown backend must fail");
        MAT_TEST_CHECK(err.find("Unknown explicit backend") != std::string::npos, "Error must describe invalid backend");

        MAT_TEST_CHECK(!ParseMaterialBackendOption("metal", backend, err), "Metal must fail on Windows");

        // 2. Capture directory validation
        std::filesystem::path outDir;
        MAT_TEST_CHECK(!ValidateMaterialCaptureDirectory("", outDir, err), "Empty capture dir must fail");

        // Forbidden write into project assets or engine source
        const auto repo=std::filesystem::path(AXLE_TEST_SOURCE_DIR);
        MAT_TEST_CHECK(!ValidateMaterialCaptureDirectory((repo/"Tests/Fixtures/Assets/captures").string(), outDir, err), "Writing into assets must be rejected");
        MAT_TEST_CHECK(!ValidateMaterialCaptureDirectory((repo/"Engine/captures").string(), outDir, err), "Writing into Engine source must be rejected");
        MAT_TEST_CHECK(!ValidateMaterialCaptureDirectory((repo/"build/../eNgInE/captures").string(), outDir, err), "Traversal and Windows path case cannot bypass protected directories");
        auto sdr=GetMaterialReadbackLayout(3,2,false);
        auto hdr=GetMaterialReadbackLayout(1024,640,true);
        MAT_TEST_CHECK(sdr.BytesPerPixel==4 && sdr.RowPitch==256 && sdr.SlicePitch==512,"SDR readback aligns short rows");
        MAT_TEST_CHECK(hdr.BytesPerPixel==8 && hdr.RowPitch==8192 && hdr.SlicePitch==5242880,"HDR readback allocates eight bytes per pixel");

        // Valid temp capture directory
        ScopedTempDir tempCap("axle_test_cap");
        MAT_TEST_CHECK(ValidateMaterialCaptureDirectory(tempCap.Path.string(), outDir, err), "Valid temp capture dir must succeed");

        // 3. Timing report formatting
        std::vector<double> emptyTimes;
        auto emptyRep = FormatMaterialTimingReport("Adapter", "Direct3D12", 1024, 640, 1, emptyTimes);
        MAT_TEST_CHECK(emptyRep.find("No timing data") != std::string::npos, "Empty times should report no data");

        std::vector<double> samples = { 16.6, 16.7, 16.5, 16.8, 16.4 };
        auto rep = FormatMaterialTimingReport("NVIDIA RTX", "Direct3D12", 1024, 640, 1, samples);
        MAT_TEST_CHECK(rep.find("Average:") != std::string::npos, "Report must include Average");
        MAT_TEST_CHECK(rep.find("Min Frame:") != std::string::npos, "Report must include Min Frame");
        MAT_TEST_CHECK(rep.find("Max Frame:") != std::string::npos, "Report must include Max Frame");
        MAT_TEST_CHECK(rep.find("queue idle time") != std::string::npos, "Report must note that timings include submission/queue idle");

        // 4. PPM image writing
        uint8_t dummyPixels[4 * 4] = {
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   255, 255, 255, 255
        };
        auto ppmPath = tempCap.Path / "test_image.ppm";
        MAT_TEST_CHECK(WritePPMImage(ppmPath, 2, 2, dummyPixels), "Write PPM image must succeed");
        MAT_TEST_CHECK(std::filesystem::exists(ppmPath), "PPM image file must exist");
        MAT_TEST_CHECK(std::filesystem::file_size(ppmPath) > 12, "PPM image size must include header and 12 RGB bytes");

        MAT_TEST_CHECK(!WritePPMImage(ppmPath, 0, 2, dummyPixels), "Zero width must fail");
        MAT_TEST_CHECK(!WritePPMImage(ppmPath, 2, 2, nullptr), "Null pixels must fail");

        return true;
    }

} // namespace

bool RunShaderDragDropUnitTests() {
    std::cout << "  - Running TestEmptyAndNullPayloads ...\n";
    if (!TestEmptyAndNullPayloads()) return false;

    std::cout << "  - Running TestExtensionAndCaseHandling ...\n";
    if (!TestExtensionAndCaseHandling()) return false;

    std::cout << "  - Running TestUnterminatedAndBoundedBuffers ...\n";
    if (!TestUnterminatedAndBoundedBuffers()) return false;

    std::cout << "  - Running TestNonAsciiUtf8Payloads ...\n";
    if (!TestNonAsciiUtf8Payloads()) return false;

    std::cout << "  - Running TestCustomPropertySynchronization ...\n";
    if (!TestCustomPropertySynchronization()) return false;

    std::cout << "  - Running TestFunctionLocalVariableAndEngineUniformFiltering ...\n";
    if (!TestFunctionLocalVariableAndEngineUniformFiltering()) return false;

    std::cout << "  - Running TestMaterialAcceptanceHelpers ...\n";
    if (!TestMaterialAcceptanceHelpers()) return false;

    return true;
}
