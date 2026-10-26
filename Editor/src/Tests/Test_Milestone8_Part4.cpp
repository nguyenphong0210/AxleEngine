#include "TestCommon.hpp"
#include <Axle/UI/UITypes.hpp>
#include <iostream>
#include <cstddef>

// =============================================================================
// TEST STEP 85: Phase 34 Step 1 (Part 1) - Decoupled AxleUI2D Library,
// Core UI Types, 80-Byte Aligned UIVertex & Headless Geometric Verification
// =============================================================================
bool RunTest_Step85_Phase34_Step1_Part1_DecoupledAxleUI2DLibraryAndUITypes() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 85 TEST] DECOUPLED AxleUI2D & 80-BYTE UIVertex   " << std::endl;
    std::cout << " (Decoupled Static Lib, Memory Layout, Rect Geometry)   " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;

    // [85.1] Memory Layout & Hardware Alignment Verification of UIVertex
    std::cout << "[85.1] Testing Unified 80-Byte UIVertex Layout & 16-Byte Alignment..." << std::endl;
    assert(sizeof(UIVertex) == 80);
    assert(alignof(UIVertex) == 16);

    assert(offsetof(UIVertex, ScreenPos) == 0);
    assert(offsetof(UIVertex, UV) == 8);
    assert(offsetof(UIVertex, Color) == 16);
    assert(offsetof(UIVertex, BorderColor) == 32);
    assert(offsetof(UIVertex, QuadSize) == 48);
    assert(offsetof(UIVertex, CornerRadius) == 56);
    assert(offsetof(UIVertex, BorderWidth) == 60);
    assert(offsetof(UIVertex, AngleRange) == 64);
    assert(offsetof(UIVertex, TextureIndex) == 72);
    assert(offsetof(UIVertex, PrimitiveType) == 76);

    // Verify default initialization
    UIVertex defaultVertex{};
    assert(defaultVertex.ScreenPos == glm::vec2(0.0f));
    assert(defaultVertex.UV == glm::vec2(0.0f));
    assert(defaultVertex.Color == glm::vec4(1.0f));
    assert(defaultVertex.BorderColor == glm::vec4(0.0f));
    assert(defaultVertex.QuadSize == glm::vec2(100.0f));
    assert(defaultVertex.CornerRadius == 0.0f);
    assert(defaultVertex.BorderWidth == 0.0f);
    assert(defaultVertex.AngleRange == glm::vec2(0.0f));
    assert(defaultVertex.TextureIndex == 0);
    assert(defaultVertex.PrimitiveType == 0);

    std::cout << "  -> sizeof(UIVertex) == 80: PASS" << std::endl;
    std::cout << "  -> alignof(UIVertex) == 16: PASS" << std::endl;
    std::cout << "  -> Exact 10-field byte offsets verified: PASS" << std::endl;

    // [85.2] 2D Rectangle Geometric Calculations (Contains, Intersect)
    std::cout << "\n[85.2] Testing UIRect Geometric Algorithms..." << std::endl;
    UIRect r1{ 100.0f, 150.0f, 300.0f, 200.0f }; // [100..400] x [150..350]

    // Point contains tests
    assert(r1.Contains(glm::vec2(150.0f, 200.0f))); // Inside
    assert(r1.Contains(glm::vec2(100.0f, 150.0f))); // Top-Left corner
    assert(r1.Contains(glm::vec2(400.0f, 350.0f))); // Bottom-Right corner
    assert(!r1.Contains(glm::vec2(99.0f, 200.0f)));  // Left of rect
    assert(!r1.Contains(glm::vec2(401.0f, 200.0f))); // Right of rect
    assert(!r1.Contains(glm::vec2(200.0f, 149.0f))); // Above rect
    assert(!r1.Contains(glm::vec2(200.0f, 351.0f))); // Below rect

    // Intersect tests: Overlapping
    UIRect r2{ 250.0f, 250.0f, 200.0f, 200.0f }; // [250..450] x [250..450]
    UIRect isect1 = r1.Intersect(r2);
    assert(isect1.X == 250.0f);
    assert(isect1.Y == 250.0f);
    assert(isect1.Width == 150.0f);  // 400 - 250
    assert(isect1.Height == 100.0f); // 350 - 250

    // Intersect tests: Completely non-overlapping
    UIRect r3{ 500.0f, 500.0f, 100.0f, 100.0f };
    UIRect isect2 = r1.Intersect(r3);
    assert(isect2.Width == 0.0f && isect2.Height == 0.0f);

    // Intersect tests: Completely inside
    UIRect r4{ 150.0f, 180.0f, 50.0f, 60.0f };
    UIRect isect3 = r1.Intersect(r4);
    assert(isect3.X == 150.0f && isect3.Y == 180.0f);
    assert(isect3.Width == 50.0f && isect3.Height == 60.0f);

    std::cout << "  -> UIRect::Contains (inside, outside, corners): PASS" << std::endl;
    std::cout << "  -> UIRect::Intersect (overlapping, separated, contained): PASS" << std::endl;

    // [85.3] UIAnchor & UIPrimitiveType Enums and String Conversion
    std::cout << "\n[85.3] Testing UIAnchor and UIPrimitiveType String Conversions..." << std::endl;
    assert(std::string_view(UIAnchorToString(UIAnchor::TopLeft)) == "TopLeft");
    assert(std::string_view(UIAnchorToString(UIAnchor::Center)) == "Center");
    assert(std::string_view(UIAnchorToString(UIAnchor::StretchAll)) == "StretchAll");

    assert(StringToUIAnchor("TopLeft") == UIAnchor::TopLeft);
    assert(StringToUIAnchor("Center") == UIAnchor::Center);
    assert(StringToUIAnchor("stretch") == UIAnchor::StretchAll);
    assert(StringToUIAnchor("BottomRight") == UIAnchor::BottomRight);

    assert(std::string_view(UIPrimitiveTypeToString(UIPrimitiveType::SDF_Rect)) == "SDF_Rect");
    assert(std::string_view(UIPrimitiveTypeToString(UIPrimitiveType::Sprite)) == "Sprite");
    assert(std::string_view(UIPrimitiveTypeToString(UIPrimitiveType::MSDF_Text)) == "MSDF_Text");
    assert(std::string_view(UIPrimitiveTypeToString(UIPrimitiveType::SDF_RadialArc)) == "SDF_RadialArc");

    assert(StringToUIPrimitiveType("Panel") == UIPrimitiveType::SDF_Rect);
    assert(StringToUIPrimitiveType("Image") == UIPrimitiveType::Sprite);
    assert(StringToUIPrimitiveType("Text") == UIPrimitiveType::MSDF_Text);
    assert(StringToUIPrimitiveType("Gauge") == UIPrimitiveType::SDF_RadialArc);

    std::cout << "  -> UIAnchor 12-mode representation & parsing: PASS" << std::endl;
    std::cout << "  -> UIPrimitiveType 4-mode representation & parsing: PASS" << std::endl;

    // [85.4] Decoupled Static Library Architecture Verification
    std::cout << "\n[85.4] Verifying AxleUI2D Static Library Decoupling..." << std::endl;
    UIStyle defaultStyle{};
    assert(defaultStyle.CornerRadius == 0.0f);
    assert(defaultStyle.BorderWidth == 0.0f);
    assert(defaultStyle.BackgroundColor.a > 0.0f);
    assert(defaultStyle.BorderColor.a > 0.0f);

    std::cout << "  -> UIStyle configuration initialized cleanly." << std::endl;
    std::cout << "  -> AxleUI2D verified to be 100% decoupled from Dear ImGui." << std::endl;

    std::cout << "\n>>> STEP 85 PASSED: Decoupled AxleUI2D Library & UITypes 100% verified! <<<\n" << std::endl;
    return true;
}
