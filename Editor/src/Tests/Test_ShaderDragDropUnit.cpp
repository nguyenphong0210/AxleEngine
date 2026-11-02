#include "MaterialTestHarness.hpp"
#include <Axle/ShaderDragDropDecoder.hpp>
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

    return true;
}
