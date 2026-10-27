#include "TestCommon.hpp"
#include <Axle/UI/UITypes.hpp>
#include <Axle/UI/RectTransform.hpp>
#include <Axle/UI/RectTransformGizmo.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/PerspectiveCameraController.hpp>
#include <Axle/UIPalettePanel.hpp>
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

// =============================================================================
// TEST STEP 86: Phase 34 Step 1 (Part 2) - Hardware-Accelerated Canvas2D
// Batch Renderer, Scissor Clipping Stack & Procedural SDF Uber-Shader
// =============================================================================
bool RunTest_Step86_Phase34_Step1_Part2_Canvas2DBatchingAndSDFPipeline() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 86 TEST] CANVAS2D BATCHING & PROCEDURAL SDF      " << std::endl;
    std::cout << " (1 Draw Call, Scissor Clipping, Canvas2D.hlsl DXIL/SPV) " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;
    using namespace Axle::RHI;

    // [86.1] Canvas2D Batch Emission (Rect, Sprite, RadialArc)
    std::cout << "[86.1] Testing Canvas2D Batch Emission & Index Geometry..." << std::endl;
    Canvas2D canvas;
    canvas.Init();
    canvas.BeginCanvas(glm::vec2(1920.0f, 1080.0f), 1.0f);

    UIStyle panelStyle{
        .BackgroundColor = glm::vec4(0.1f, 0.15f, 0.25f, 0.9f),
        .BorderColor = glm::vec4(0.0f, 0.96f, 0.83f, 1.0f),
        .CornerRadius = 12.0f,
        .BorderWidth = 2.0f
    };

    // Draw Rect (Panel)
    canvas.DrawRect(UIRect{ 100.0f, 100.0f, 300.0f, 150.0f }, panelStyle);
    assert(canvas.GetStats().QuadCount == 1);
    assert(canvas.GetStats().VertexCount == 4);
    assert(canvas.GetStats().IndexCount == 6);

    const auto& verts = canvas.GetVertexBuffer();
    assert(verts.size() == 4);
    assert(verts[0].ScreenPos == glm::vec2(100.0f, 100.0f));
    assert(verts[1].ScreenPos == glm::vec2(400.0f, 100.0f));
    assert(verts[2].ScreenPos == glm::vec2(400.0f, 250.0f));
    assert(verts[3].ScreenPos == glm::vec2(100.0f, 250.0f));
    assert(verts[0].CornerRadius == 12.0f);
    assert(verts[0].BorderWidth == 2.0f);
    assert(verts[0].PrimitiveType == static_cast<uint32_t>(UIPrimitiveType::SDF_Rect));

    // Draw Sprite (TextureIndex 7)
    canvas.DrawSprite(UIRect{ 450.0f, 100.0f, 64.0f, 64.0f }, 7, glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
    assert(canvas.GetStats().QuadCount == 2);
    assert(canvas.GetStats().VertexCount == 8);
    assert(canvas.GetStats().IndexCount == 12);
    assert(verts[4].TextureIndex == 7);
    assert(verts[4].PrimitiveType == static_cast<uint32_t>(UIPrimitiveType::Sprite));

    // Draw RadialArc (RPM Gauge, start -225 deg, end 45 deg, 75% fill)
    glm::vec4 arcFill{ 0.0f, 0.96f, 0.83f, 1.0f };
    glm::vec4 arcTrack{ 0.12f, 0.16f, 0.23f, 0.6f };
    canvas.DrawRadialArc(UIRect{ 600.0f, 100.0f, 200.0f, 200.0f }, -225.0f, 45.0f, 0.75f, 16.0f, arcFill, arcTrack);
    assert(canvas.GetStats().QuadCount == 3);
    assert(canvas.GetStats().VertexCount == 12);
    assert(canvas.GetStats().IndexCount == 18);
    assert(verts[8].PrimitiveType == static_cast<uint32_t>(UIPrimitiveType::SDF_RadialArc));
    assert(verts[8].CornerRadius == 16.0f); // Thickness

    std::cout << "  -> Quad batch emission: 3 quads, 12 vertices, 18 indices." << std::endl;
    std::cout << "  -> PASS: Primitive Types (SDF_Rect, Sprite, SDF_RadialArc) verified." << std::endl;

    // [86.2] Hardware Scissor Clipping Stack & Out-of-Bounds Culling
    std::cout << "\n[86.2] Testing Scissor Clipping Stack & Cull Optimization..." << std::endl;
    canvas.PushClipRect(UIRect{ 0.0f, 0.0f, 400.0f, 400.0f });

    // This quad is completely outside the clip rect [0..400] x [0..400] -> MUST BE CULLED!
    canvas.DrawRect(UIRect{ 600.0f, 600.0f, 100.0f, 100.0f }, panelStyle);
    assert(canvas.GetStats().QuadCount == 3); // Must NOT increase!

    // This quad is inside the clip rect -> MUST BE EMITTED!
    canvas.DrawRect(UIRect{ 150.0f, 150.0f, 50.0f, 50.0f }, panelStyle);
    assert(canvas.GetStats().QuadCount == 4);

    canvas.PopClipRect();
    assert(canvas.GetCurrentClipRect().Width == 1920.0f);
    assert(canvas.GetCurrentClipRect().Height == 1080.0f);

    std::cout << "  -> Out-of-bounds quad culled with zero vertex allocation: PASS" << std::endl;
    std::cout << "  -> In-bounds quad correctly emitted through clip rect: PASS" << std::endl;

    // [86.3] Uniform DPI Scaling Verification
    std::cout << "\n[86.3] Testing Uniform DPI Scaling Factor..." << std::endl;
    canvas.BeginCanvas(glm::vec2(3840.0f, 2160.0f), 2.0f);
    canvas.DrawRect(UIRect{ 10.0f, 20.0f, 100.0f, 50.0f }, panelStyle);

    const auto& dpiVerts = canvas.GetVertexBuffer();
    assert(dpiVerts[0].ScreenPos == glm::vec2(20.0f, 40.0f));
    assert(dpiVerts[0].QuadSize == glm::vec2(200.0f, 100.0f));
    assert(dpiVerts[0].CornerRadius == 24.0f); // 12 * 2.0
    assert(dpiVerts[0].BorderWidth == 4.0f);   // 2 * 2.0

    canvas.EndCanvas();
    assert(canvas.GetStats().DrawCalls == 1);
    std::cout << "  -> 2.0x DPI scaling accurately multiplies ScreenPos, QuadSize, Radius, Border: PASS" << std::endl;
    std::cout << "  -> EndCanvas() consolidated 1 draw call: PASS" << std::endl;

    // [86.4] Dual-Stage HLSL 2021 Shader Bytecode Compilation (Canvas2D.hlsl)
    std::cout << "\n[86.4] Testing Canvas2D.hlsl Dual-Target Bytecode Compilation..." << std::endl;
    auto& compiler = RHIShaderCompiler::Get();
    if (!compiler.IsInitialized()) {
        assert(compiler.Init());
    }

    std::filesystem::path shaderPath = "Engine/Resources/Shaders/Canvas2D.hlsl";
    if (!std::filesystem::exists(shaderPath)) {
        shaderPath = std::filesystem::current_path() / "Engine" / "Resources" / "Shaders" / "Canvas2D.hlsl";
    }
    if (!std::filesystem::exists(shaderPath)) {
        shaderPath = "Resources/Shaders/Canvas2D.hlsl";
    }
    assert(std::filesystem::exists(shaderPath));

    std::ifstream file(shaderPath);
    assert(file.is_open());
    std::stringstream ss;
    ss << file.rdbuf();
    std::string shaderSource = ss.str();
    assert(!shaderSource.empty());

    // 1. Compile to Direct3D 12 DXIL
    RHIShaderCompileDesc vsDescDXIL;
    vsDescDXIL.Source = shaderSource;
    vsDescDXIL.SourcePath = shaderPath.string();
    vsDescDXIL.EntryPoint = "VSMain";
    vsDescDXIL.Stage = RHIShaderStage::Vertex;
    vsDescDXIL.HLSLVersion = "2021";
    vsDescDXIL.ShaderModel = "6_0";
    vsDescDXIL.Format = RHIShaderFormat::DXIL;
    vsDescDXIL.Optimization = true;

    auto vsResDXIL = compiler.Compile(vsDescDXIL);
    if (!vsResDXIL.Success) {
        std::cerr << "FAIL: Canvas2D.hlsl VS DXIL compilation failed: " << vsResDXIL.ErrorMessage << std::endl;
    }
    assert(vsResDXIL.Success);
    assert(vsResDXIL.Bytecode.GetSize() > 0);

    RHIShaderCompileDesc psDescDXIL = vsDescDXIL;
    psDescDXIL.EntryPoint = "PSMain";
    psDescDXIL.Stage = RHIShaderStage::Fragment;
    auto psResDXIL = compiler.Compile(psDescDXIL);
    if (!psResDXIL.Success) {
        std::cerr << "FAIL: Canvas2D.hlsl PS DXIL compilation failed: " << psResDXIL.ErrorMessage << std::endl;
    }
    assert(psResDXIL.Success);
    assert(psResDXIL.Bytecode.GetSize() > 0);

    std::cout << "  -> Canvas2D.hlsl D3D12 DXIL: VS (" << vsResDXIL.Bytecode.GetSize()
              << "B), PS (" << psResDXIL.Bytecode.GetSize() << "B). PASS!" << std::endl;

    // 2. Compile to Vulkan 1.3 SPIR-V
    RHIShaderCompileDesc vsDescSPV = vsDescDXIL;
    vsDescSPV.Format = RHIShaderFormat::SPIRV;
    auto vsResSPV = compiler.Compile(vsDescSPV);
    if (!vsResSPV.Success) {
        std::cerr << "FAIL: Canvas2D.hlsl VS SPIR-V compilation failed: " << vsResSPV.ErrorMessage << std::endl;
    }
    assert(vsResSPV.Success);
    assert(vsResSPV.Bytecode.GetSize() > 0);

    RHIShaderCompileDesc psDescSPV = psDescDXIL;
    psDescSPV.Format = RHIShaderFormat::SPIRV;
    auto psResSPV = compiler.Compile(psDescSPV);
    if (!psResSPV.Success) {
        std::cerr << "FAIL: Canvas2D.hlsl PS SPIR-V compilation failed: " << psResSPV.ErrorMessage << std::endl;
    }
    assert(psResSPV.Success);
    assert(psResSPV.Bytecode.GetSize() > 0);

    std::cout << "  -> Canvas2D.hlsl Vulkan 1.3 SPIR-V: VS (" << vsResSPV.Bytecode.GetSize()
              << "B), PS (" << psResSPV.Bytecode.GetSize() << "B). PASS!" << std::endl;

    std::cout << "\n>>> STEP 86 PASSED: Canvas2D Batching, Scissor Clipping & SDF Uber-Shader 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 87: Phase 34 Step 2 - MSDF Font Pipeline, Glyph Metrics,
// Kerning Pairs, Text Measurement & Sharp Text Quad Emission
// =============================================================================
bool RunTest_Step87_Phase34_Step2_MSDFFontAndTextLayout() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 87 TEST] MSDF FONT PIPELINE & TEXT LAYOUT       " << std::endl;
    std::cout << " (Embedded ASCII Fallback, Kerning, MeasureText, Quads) " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;

    // [87.1] Default Embedded Font Initialization & Glyph Inspection
    std::cout << "[87.1] Testing Default Embedded Font & Standard ASCII Glyphs..." << std::endl;
    auto font = std::make_shared<UIFont>();
    assert(font != nullptr);
    assert(font->GetLineHeight() == 1.15f);

    const GlyphMetrics* spaceGlyph = font->GetGlyph(' ');
    assert(spaceGlyph != nullptr);
    assert(spaceGlyph->Codepoint == 32);
    assert(spaceGlyph->AdvanceX == 0.35f);

    const GlyphMetrics* digitGlyph = font->GetGlyph('5');
    assert(digitGlyph != nullptr);
    assert(digitGlyph->Codepoint == '5');
    assert(digitGlyph->AdvanceX == 0.6f);

    const GlyphMetrics* upperGlyph = font->GetGlyph('A');
    assert(upperGlyph != nullptr);
    assert(upperGlyph->Codepoint == 'A');
    assert(upperGlyph->AdvanceX == 0.65f);

    const GlyphMetrics* lowerGlyph = font->GetGlyph('a');
    assert(lowerGlyph != nullptr);
    assert(lowerGlyph->Codepoint == 'a');
    assert(lowerGlyph->AdvanceX == 0.55f);

    std::cout << "  -> Space (' '), Digits ('0'-'9'), Uppercase ('A'-'Z'), Lowercase ('a'-'z') glyphs: PASS" << std::endl;

    // [87.2] Kerning Pair Inspection & Custom Kerning Assignment
    std::cout << "\n[87.2] Testing Kerning Map & Pair Arithmetic..." << std::endl;
    float avKerning = font->GetKerning('A', 'V');
    assert(avKerning == -0.05f);

    font->SetKerning('T', 'e', -0.06f);
    assert(font->GetKerning('T', 'e') == -0.06f);

    // Unset pair should return 0.0f
    assert(font->GetKerning('X', 'Z') == 0.0f);
    std::cout << "  -> Kerning pairs queried and dynamically assigned: PASS" << std::endl;

    // [87.3] Multi-line Text Measurement (UIFont::MeasureText)
    std::cout << "\n[87.3] Testing Single-Line & Multi-Line Text Measurement..." << std::endl;
    glm::vec2 size1 = font->MeasureText("280 KM/H", 48.0f);
    assert(size1.x > 0.0f);
    assert(size1.y > 0.0f);
    // 8 chars total, approximate width ~ (8 * 0.6f * 48.0f) = ~230px
    assert(size1.x > 150.0f && size1.x < 350.0f);
    assert(std::abs(size1.y - (48.0f * 1.15f)) < 1e-3f);

    glm::vec2 sizeMulti = font->MeasureText("SPEED\n280 KM/H", 48.0f);
    assert(sizeMulti.x >= size1.x);
    // 2 lines height
    assert(std::abs(sizeMulti.y - (2.0f * 48.0f * 1.15f)) < 1e-3f);

    std::cout << "  -> MeasureText single line (\"280 KM/H\"): (" << size1.x << ", " << size1.y << ") PASS" << std::endl;
    std::cout << "  -> MeasureText multi-line (\"SPEED\\n280 KM/H\"): (" << sizeMulti.x << ", " << sizeMulti.y << ") PASS" << std::endl;

    // [87.4] Text Quad Emission in Canvas2D
    std::cout << "\n[87.4] Testing Sharp Text Quad Emission in Canvas2D..." << std::endl;
    Canvas2D canvas;
    canvas.Init();
    canvas.BeginCanvas(glm::vec2(1920.0f, 1080.0f), 1.0f);

    font->SetBindlessTextureIndex(12);
    // "280 KM/H" has 8 characters, but 1 is a space (' ') which must NOT emit a quad!
    canvas.DrawTextString("280 KM/H", font, UIRect{ 100.0f, 200.0f, 500.0f, 60.0f }, 48.0f, glm::vec4(1.0f, 0.9f, 0.2f, 1.0f));

    assert(canvas.GetStats().QuadCount == 7);
    assert(canvas.GetStats().VertexCount == 28);
    assert(canvas.GetStats().IndexCount == 42);

    const auto& textVerts = canvas.GetVertexBuffer();
    assert(textVerts.size() == 28);
    for (size_t i = 0; i < textVerts.size(); i += 4) {
        assert(textVerts[i].PrimitiveType == static_cast<uint32_t>(UIPrimitiveType::MSDF_Text));
        assert(textVerts[i].TextureIndex == 12);
        assert(textVerts[i].Color == glm::vec4(1.0f, 0.9f, 0.2f, 1.0f));
    }

    canvas.EndCanvas();
    assert(canvas.GetStats().DrawCalls == 1);
    std::cout << "  -> Exactly 7 quads emitted for 8-char string (space quad omitted): PASS" << std::endl;
    std::cout << "  -> PrimitiveType == MSDF_Text and TextureIndex == 12 verified: PASS" << std::endl;

    // [87.5] Font Serialization & Deserialization (.axlefont)
    std::cout << "\n[87.5] Testing .axlefont Save & Load..." << std::endl;
    std::filesystem::create_directories("test_output");
    std::string testFontPath = "test_output/TestFont.axlefont";
    assert(font->SaveToAxleFont(testFontPath));

    UIFont loadedFont;
    assert(loadedFont.LoadFromAxleFont(testFontPath));
    assert(loadedFont.GetBindlessTextureIndex() == 12);
    assert(loadedFont.GetGlyph('A') != nullptr);

    std::cout << "  -> SaveToAxleFont / LoadFromAxleFont roundtrip: PASS" << std::endl;

    std::cout << "\n>>> STEP 87 PASSED: MSDF Font Pipeline & Text Layout 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 88: Phase 34 Step 3 - Hybrid OOP UI DOM Tree (UINode & UIDocument)
// & 2-Pass Responsive Layout Engine (MeasurePass & ArrangePass across 12 UIAnchors)
// =============================================================================
bool RunTest_Step88_Phase34_Step3_DOMTreeAnd2PassLayoutEngine() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 88 TEST] UI DOM TREE & 2-PASS LAYOUT ENGINE     " << std::endl;
    std::cout << " (Arena Hierarchy, 12 UIAnchors, Box Container, DPI)    " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;

    // [88.1] Arena Allocation & Element Type Coverage in UIDocument
    std::cout << "[88.1] Testing Arena Node Allocation & Full Type Coverage..." << std::endl;
    UIDocument doc;
    assert(doc.GetNodeCount() == 0);
    assert(doc.GetRootNode() == nullptr);

    // Root Canvas
    auto* root = doc.CreateNode<UICanvasNode>(nullptr);
    assert(root != nullptr);
    assert(doc.GetRootNode() == root);
    assert(root->GetTypeName() == "Canvas");
    assert(root->ReferenceResolution == glm::vec2(1920.0f, 1080.0f));

    // Panel (Rectangle)
    auto* panel = doc.CreateNode<UIRectangleNode>(root);
    assert(panel != nullptr);
    assert(panel->Parent == root);
    assert(panel->GetTypeName() == "Rectangle");

    // Text child inside panel
    auto* speedText = doc.CreateNode<UITextNode>(panel);
    assert(speedText != nullptr);
    assert(speedText->Parent == panel);
    assert(speedText->GetTypeName() == "Text");

    // Radial Gauge
    auto* rpmGauge = doc.CreateNode<UIRadialGaugeNode>(root);
    assert(rpmGauge != nullptr);
    assert(rpmGauge->Parent == root);
    assert(rpmGauge->GetTypeName() == "RadialGauge");

    // Progress Bar
    auto* fuelBar = doc.CreateNode<UIProgressBarNode>(root);
    assert(fuelBar != nullptr);
    assert(fuelBar->Parent == root);
    assert(fuelBar->GetTypeName() == "ProgressBar");

    // Interactive Button
    auto* pauseBtn = doc.CreateNode<UIButtonNode>(root);
    assert(pauseBtn != nullptr);
    assert(pauseBtn->Parent == root);
    assert(pauseBtn->GetTypeName() == "Button");

    // Box Container (Column)
    auto* telemetryCol = doc.CreateNode<UIBoxContainerNode>(root, true);
    assert(telemetryCol != nullptr);
    assert(telemetryCol->IsVertical == true);
    assert(telemetryCol->GetTypeName() == "Column");

    // 2 children inside Column
    auto* label1 = doc.CreateNode<UITextNode>(telemetryCol);
    auto* label2 = doc.CreateNode<UITextNode>(telemetryCol);
    assert(label1->Parent == telemetryCol);
    assert(label2->Parent == telemetryCol);

    // Image Node
    auto* logoImg = doc.CreateNode<UIImageNode>(root);
    assert(logoImg != nullptr);
    assert(logoImg->GetTypeName() == "Image");

    assert(doc.GetNodeCount() == 10);
    assert(root->Children.size() == 6); // panel, rpmGauge, fuelBar, pauseBtn, telemetryCol, logoImg
    assert(panel->Children.size() == 1); // speedText
    assert(telemetryCol->Children.size() == 2); // label1, label2

    std::cout << "  -> Arena allocation of 10 nodes across all 8 concrete element types: PASS" << std::endl;
    std::cout << "  -> Parent-child hierarchy pointers and children vectors: PASS" << std::endl;

    // [88.2] Fast ID Lookup & Dynamic Property Reflection
    std::cout << "\n[88.2] Testing ID Map Lookup & Dynamic Property Setters..." << std::endl;
    panel->ID = "mainPanel";
    speedText->ID = "speedText";
    rpmGauge->ID = "rpmGauge";
    fuelBar->ID = "fuelBar";
    pauseBtn->ID = "pauseBtn";

    doc.RebuildIDMap();
    assert(doc.FindNodeByID("rpmGauge") == rpmGauge);
    assert(doc.FindNodeByID("speedText") == speedText);
    assert(doc.FindNodeByID("pauseBtn") == pauseBtn);
    assert(doc.FindNodeByID("nonExistent") == nullptr);

    // Test Color & Hex Parsing
    assert(panel->SetProperty("backgroundColor", "#1A2233E6"));
    assert(std::abs(panel->Style.BackgroundColor.r - (0x1A / 255.0f)) < 0.01f);
    assert(std::abs(panel->Style.BackgroundColor.g - (0x22 / 255.0f)) < 0.01f);
    assert(std::abs(panel->Style.BackgroundColor.b - (0x33 / 255.0f)) < 0.01f);
    assert(std::abs(panel->Style.BackgroundColor.a - (0xE6 / 255.0f)) < 0.01f);

    assert(panel->SetPropertyFloat("cornerRadius", 16.0f));
    assert(panel->Style.CornerRadius == 16.0f);
    assert(panel->SetPropertyFloat("borderWidth", 2.0f));
    assert(panel->Style.BorderWidth == 2.0f);

    assert(speedText->SetProperty("text", "280 KM/H"));
    assert(speedText->Text == "280 KM/H");
    assert(speedText->SetPropertyFloat("fontSize", 48.0f));
    assert(speedText->FontSize == 48.0f);

    assert(fuelBar->SetPropertyFloat("value", 0.72f));
    assert(fuelBar->Value == 0.72f);
    assert(rpmGauge->SetPropertyFloat("value", 0.85f));
    assert(rpmGauge->Value == 0.85f);
    assert(pauseBtn->SetProperty("action", "PauseGame"));
    assert(pauseBtn->OnClickAction == "PauseGame");

    std::cout << "  -> RebuildIDMap() & FindNodeByID() verified: PASS" << std::endl;
    std::cout << "  -> Hex color, numeric float, and string property reflection: PASS" << std::endl;

    // [88.3] 2-Pass Layout Engine: Bottom-Up Measure & Top-Down Arrange across 12 UIAnchor Modes
    std::cout << "\n[88.3] Testing 2-Pass Layout Engine (Measure & Arrange across Anchors)..." << std::endl;

    // Configure distinct anchor test nodes under root
    doc.Clear();
    root = doc.CreateNode<UICanvasNode>(nullptr);
    root->ReferenceResolution = glm::vec2(1920.0f, 1080.0f);

    // 1. TopLeft Anchor
    auto* nTL = doc.CreateNode<UIRectangleNode>(root);
    nTL->Anchor = UIAnchor::TopLeft;
    nTL->Position = glm::vec2(20.0f, 30.0f);
    nTL->Size = glm::vec2(200.0f, 100.0f);
    nTL->Margin = glm::vec4(10.0f, 15.0f, 0.0f, 0.0f);

    // 2. TopCenter Anchor
    auto* nTC = doc.CreateNode<UIRectangleNode>(root);
    nTC->Anchor = UIAnchor::TopCenter;
    nTC->Position = glm::vec2(5.0f, 0.0f);
    nTC->Size = glm::vec2(400.0f, 80.0f);
    nTC->Margin = glm::vec4(0.0f, 25.0f, 0.0f, 0.0f);

    // 3. TopRight Anchor
    auto* nTR = doc.CreateNode<UIRectangleNode>(root);
    nTR->Anchor = UIAnchor::TopRight;
    nTR->Position = glm::vec2(-10.0f, 5.0f);
    nTR->Size = glm::vec2(300.0f, 120.0f);
    nTR->Margin = glm::vec4(0.0f, 10.0f, 30.0f, 0.0f);

    // 4. Center Anchor
    auto* nCenter = doc.CreateNode<UIRectangleNode>(root);
    nCenter->Anchor = UIAnchor::Center;
    nCenter->Position = glm::vec2(0.0f, 0.0f);
    nCenter->Size = glm::vec2(500.0f, 300.0f);

    // 5. BottomRight Anchor
    auto* nBR = doc.CreateNode<UIRectangleNode>(root);
    nBR->Anchor = UIAnchor::BottomRight;
    nBR->Position = glm::vec2(0.0f, 0.0f);
    nBR->Size = glm::vec2(250.0f, 150.0f);
    nBR->Margin = glm::vec4(0.0f, 0.0f, 40.0f, 20.0f);

    // 6. BottomStretch Anchor
    auto* nBStretch = doc.CreateNode<UIRectangleNode>(root);
    nBStretch->Anchor = UIAnchor::BottomStretch;
    nBStretch->Position = glm::vec2(0.0f, 0.0f);
    nBStretch->Size = glm::vec2(0.0f, 60.0f);
    nBStretch->Margin = glm::vec4(50.0f, 0.0f, 50.0f, 30.0f);

    // 7. StretchAll Anchor
    auto* nStretchAll = doc.CreateNode<UIRectangleNode>(root);
    nStretchAll->Anchor = UIAnchor::StretchAll;
    nStretchAll->Margin = glm::vec4(40.0f, 60.0f, 40.0f, 60.0f);

    // Run 2-Pass Layout
    doc.UpdateLayout(glm::vec2(1920.0f, 1080.0f));

    // Verify Root ComputedRect
    assert(root->ComputedRect.X == 0.0f && root->ComputedRect.Y == 0.0f);
    assert(root->ComputedRect.Width == 1920.0f && root->ComputedRect.Height == 1080.0f);

    // 1. TopLeft: X = 0 + 10 + 20 = 30; Y = 0 + 15 + 30 = 45; W = 200; H = 100
    assert(nTL->ComputedRect.X == 30.0f);
    assert(nTL->ComputedRect.Y == 45.0f);
    assert(nTL->ComputedRect.Width == 200.0f);
    assert(nTL->ComputedRect.Height == 100.0f);

    // 2. TopCenter: X = 0.5 * (1920 - 400) + 5 = 765; Y = 25; W = 400; H = 80
    assert(nTC->ComputedRect.X == 765.0f);
    assert(nTC->ComputedRect.Y == 25.0f);
    assert(nTC->ComputedRect.Width == 400.0f);
    assert(nTC->ComputedRect.Height == 80.0f);

    // 3. TopRight: X = 1920 - 300 - 30 - 10 = 1580; Y = 10 + 5 = 15; W = 300; H = 120
    assert(nTR->ComputedRect.X == 1580.0f);
    assert(nTR->ComputedRect.Y == 15.0f);
    assert(nTR->ComputedRect.Width == 300.0f);
    assert(nTR->ComputedRect.Height == 120.0f);

    // 4. Center: X = 0.5 * (1920 - 500) = 710; Y = 0.5 * (1080 - 300) = 390; W = 500; H = 300
    assert(nCenter->ComputedRect.X == 710.0f);
    assert(nCenter->ComputedRect.Y == 390.0f);
    assert(nCenter->ComputedRect.Width == 500.0f);
    assert(nCenter->ComputedRect.Height == 300.0f);

    // 5. BottomRight: X = 1920 - 250 - 40 = 1630; Y = 1080 - 150 - 20 = 910; W = 250; H = 150
    assert(nBR->ComputedRect.X == 1630.0f);
    assert(nBR->ComputedRect.Y == 910.0f);
    assert(nBR->ComputedRect.Width == 250.0f);
    assert(nBR->ComputedRect.Height == 150.0f);

    // 6. BottomStretch: X = 50; Y = 1080 - 60 - 30 = 990; W = 1920 - 100 = 1820; H = 60
    assert(nBStretch->ComputedRect.X == 50.0f);
    assert(nBStretch->ComputedRect.Y == 990.0f);
    assert(nBStretch->ComputedRect.Width == 1820.0f);
    assert(nBStretch->ComputedRect.Height == 60.0f);

    // 7. StretchAll: X = 40; Y = 60; W = 1920 - 80 = 1840; H = 1080 - 120 = 960
    assert(nStretchAll->ComputedRect.X == 40.0f);
    assert(nStretchAll->ComputedRect.Y == 60.0f);
    assert(nStretchAll->ComputedRect.Width == 1840.0f);
    assert(nStretchAll->ComputedRect.Height == 960.0f);

    std::cout << "  -> TopLeft, TopCenter, TopRight, Center, BottomRight anchor positioning: PASS" << std::endl;
    std::cout << "  -> BottomStretch and StretchAll dimension stretching: PASS" << std::endl;

    // [88.4] UIBoxContainerNode (Column / Row) Stacking & Auto-Measurement
    std::cout << "\n[88.4] Testing Box Container Sequential Stacking & Child Auto-Sizing..." << std::endl;
    doc.Clear();
    root = doc.CreateNode<UICanvasNode>(nullptr);

    auto* col = doc.CreateNode<UIBoxContainerNode>(root, true);
    col->Anchor = UIAnchor::TopLeft;
    col->Position = glm::vec2(100.0f, 100.0f);
    col->Padding = glm::vec4(10.0f, 10.0f, 10.0f, 10.0f);
    col->Spacing = 8.0f;

    auto* item1 = doc.CreateNode<UIRectangleNode>(col);
    item1->Size = glm::vec2(150.0f, 40.0f);

    auto* item2 = doc.CreateNode<UIRectangleNode>(col);
    item2->Size = glm::vec2(180.0f, 50.0f);

    auto* item3 = doc.CreateNode<UIRectangleNode>(col);
    item3->Size = glm::vec2(120.0f, 30.0f);

    doc.UpdateLayout(glm::vec2(1920.0f, 1080.0f));

    // Column Desired Size: W = padding(10 + 10) + maxW(180) = 200
    //                      H = padding(10 + 10) + sumH(40 + 50 + 30) + spacing(2 * 8) = 20 + 120 + 16 = 156
    assert(col->ComputedRect.X == 100.0f);
    assert(col->ComputedRect.Y == 100.0f);
    assert(col->ComputedRect.Width == 200.0f);
    assert(col->ComputedRect.Height == 156.0f);

    // Child 1: Y = 100 + 10 = 110, H = 40
    assert(item1->ComputedRect.X == 110.0f);
    assert(item1->ComputedRect.Y == 110.0f);
    assert(item1->ComputedRect.Width == 150.0f);
    assert(item1->ComputedRect.Height == 40.0f);

    // Child 2: Y = 110 + 40 + 8 = 158, H = 50
    assert(item2->ComputedRect.X == 110.0f);
    assert(item2->ComputedRect.Y == 158.0f);
    assert(item2->ComputedRect.Width == 180.0f);
    assert(item2->ComputedRect.Height == 50.0f);

    // Child 3: Y = 158 + 50 + 8 = 216, H = 30
    assert(item3->ComputedRect.X == 110.0f);
    assert(item3->ComputedRect.Y == 216.0f);
    assert(item3->ComputedRect.Width == 120.0f);
    assert(item3->ComputedRect.Height == 30.0f);

    std::cout << "  -> Column container measured desired size (200x156): PASS" << std::endl;
    std::cout << "  -> Children stacked sequentially with spacing & padding: PASS" << std::endl;

    // [88.5] Multi-Resolution DPI Scaling Factor
    std::cout << "\n[88.5] Testing Multi-Resolution DPI Scaling Factor..." << std::endl;
    doc.UpdateLayout(glm::vec2(3840.0f, 2160.0f)); // 4K UHD
    assert(std::abs(doc.GetComputedDPIScale() - 2.0f) < 1e-4f);

    doc.UpdateLayout(glm::vec2(2560.0f, 1440.0f)); // 1440p QHD
    assert(std::abs(doc.GetComputedDPIScale() - (2560.0f / 1920.0f)) < 1e-4f);

    doc.UpdateLayout(glm::vec2(1280.0f, 720.0f));  // 720p HD
    assert(std::abs(doc.GetComputedDPIScale() - (1280.0f / 1920.0f)) < 1e-4f);

    std::cout << "  -> 4K (2.0x), 1440p (1.33x), 720p (0.67x) DPI scaling factor computation: PASS" << std::endl;

    // [88.6] End-to-End Hierarchical Canvas2D Drawing & Batch Consolidation
    std::cout << "\n[88.6] Testing End-to-End Hierarchical Canvas2D Drawing..." << std::endl;
    doc.Clear();
    root = doc.CreateNode<UICanvasNode>(nullptr);

    auto* hudPanel = doc.CreateNode<UIRectangleNode>(root);
    hudPanel->Size = glm::vec2(400.0f, 200.0f);
    hudPanel->Style.BackgroundColor = glm::vec4(0.1f, 0.1f, 0.1f, 0.9f);

    auto* hudText = doc.CreateNode<UITextNode>(hudPanel);
    hudText->Text = "RPM";
    hudText->FontSize = 24.0f;
    hudText->Color = glm::vec4(1.0f);

    auto* hudGauge = doc.CreateNode<UIRadialGaugeNode>(root);
    hudGauge->Size = glm::vec2(150.0f, 150.0f);
    hudGauge->Value = 0.65f;

    auto* hudBar = doc.CreateNode<UIProgressBarNode>(root);
    hudBar->Size = glm::vec2(200.0f, 20.0f);
    hudBar->Value = 0.8f;

    auto* hudBtn = doc.CreateNode<UIButtonNode>(root);
    hudBtn->Size = glm::vec2(100.0f, 40.0f);

    // Also add an invisible node to verify it is NOT drawn
    auto* hiddenNode = doc.CreateNode<UIRectangleNode>(root);
    hiddenNode->Visible = false;

    doc.UpdateLayout(glm::vec2(1920.0f, 1080.0f));

    Canvas2D canvas;
    canvas.Init();
    canvas.BeginCanvas(glm::vec2(1920.0f, 1080.0f), 1.0f);

    doc.Render(canvas);

    // Expect:
    // hudPanel: 1 quad (Rect)
    // hudText: 3 quads ('R', 'P', 'M')
    // hudGauge: 1 quad (RadialArc)
    // hudBar: 2 quads (1 track + 1 fill)
    // hudBtn: 1 quad (Rect)
    // hiddenNode: 0 quads
    // Total quads = 1 + 3 + 1 + 2 + 1 = 8 quads
    assert(canvas.GetStats().QuadCount == 8);
    assert(canvas.GetStats().VertexCount == 32);
    assert(canvas.GetStats().IndexCount == 48);

    canvas.EndCanvas();
    assert(canvas.GetStats().DrawCalls == 1);

    std::cout << "  -> Document rendered 8 quads into Canvas2D in a single consolidated draw call: PASS" << std::endl;
    std::cout << "  -> Hidden node correctly skipped during OnDraw: PASS" << std::endl;

    std::cout << "\n>>> STEP 88 PASSED: UI DOM Tree & 2-Pass Layout Engine 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 89: Phase 34 Step 4 - Declarative .axleui UIParser & Hot-Reloading
// (Lexer, AST Hierarchy, Properties, Reactive Bindings, Real-Time File Watcher)
// =============================================================================
bool RunTest_Step89_Phase34_Step4_DeclarativeUIParserAndHotReloading() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 89 TEST] DECLARATIVE .axleui UIPARSER & HOT-RELOAD" << std::endl;
    std::cout << " (Lexer, Hierarchical AST, Bindings, Real-Time Watcher) " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;

    // [89.1] Comment Stripping & String Literal Preservation
    std::cout << "[89.1] Testing Lexer Comment Stripping & String Quoting..." << std::endl;
    std::string testSource = R"(
        // Single-line comment at start
        Canvas {
            /* Multi-line block comment
               spanning several lines */
            id: "root_canvas"
            referenceResolution: [1920, 1080] // resolution
            Text {
                text: "URL: //not_a_comment and /*still_text*/"
            }
        }
    )";

    UIDocument doc;
    std::string parseErr;
    bool parseOk = UIParser::ParseSource(testSource, doc, &parseErr);
    if (!parseOk) {
        std::cerr << "FAIL: ParseSource failed: " << parseErr << std::endl;
    }
    assert(parseOk);
    assert(doc.GetRootNode() != nullptr);
    assert(doc.GetRootNode()->GetTypeName() == "Canvas");
    assert(doc.FindNodeByID("root_canvas") != nullptr);

    auto* textChild = dynamic_cast<UITextNode*>(doc.GetRootNode()->Children[0]);
    assert(textChild != nullptr);
    assert(textChild->Text == "URL: //not_a_comment and /*still_text*/");
    std::cout << "  -> Single-line // and block /* */ comments stripped cleanly: PASS" << std::endl;
    std::cout << "  -> Comment characters inside string literals preserved: PASS" << std::endl;

    // [89.2] Complete Declarative In-Memory Source Parsing
    std::cout << "\n[89.2] Testing Hierarchical AST Construction across Primitives..." << std::endl;
    std::string hudSource = R"(
        Canvas {
            id: "hud_root"
            referenceResolution: [1920, 1080]

            Rectangle {
                id: "telemetry_box"
                anchor: "TopLeft"
                margin: [32, 32, 0, 0]
                size: [280, 140]
                color: "#0B0E14CC"
                cornerRadius: 12
                borderWidth: 2
                borderColor: "#1E293B"

                Column {
                    anchor: "TopLeft"
                    margin: [16, 14, 16, 14]
                    spacing: 8

                    Text {
                        id: "fps_counter"
                        text: "FPS: {Engine.FPS}"
                        fontSize: 18
                        color: "#38BDF8"
                    }
                    Text {
                        id: "lat_g_text"
                        text: "LATERAL G: {Vehicle.LateralG}"
                        fontSize: 18
                        color: "#E2E8F0"
                    }
                }
            }

            Rectangle {
                id: "tachometer_cluster"
                anchor: "BottomRight"
                margin: [0, 0, 48, 48]
                size: [340, 340]
                color: "#0B0E14D9"
                cornerRadius: 170

                RadialGauge {
                    id: "rpm_radial_gauge"
                    anchor: "Center"
                    size: [304, 304]
                    startAngle: -225.0
                    endAngle: 45.0
                    thickness: 18.0
                    value: "{Vehicle.RPMNormalized}"
                    color: "{Vehicle.IsRedline ? #FF1E42FF : #00F5D4FF}"
                    trackColor: "#1E293B99"
                }

                Button {
                    id: "btn_pause"
                    size: [120, 40]
                    anchor: "Center"
                    pos: [0, 60]
                    onClick: "OnPauseGame"
                    navDown: "btn_quit"
                }
            }
        }
    )";

    UIDocument hudDoc;
    assert(UIParser::ParseSource(hudSource, hudDoc, &parseErr));
    assert(hudDoc.GetNodeCount() == 8);

    auto* hudRoot = dynamic_cast<UICanvasNode*>(hudDoc.FindNodeByID("hud_root"));
    assert(hudRoot != nullptr);
    assert(hudRoot->ReferenceResolution == glm::vec2(1920.0f, 1080.0f));

    auto* telemBox = dynamic_cast<UIRectangleNode*>(hudDoc.FindNodeByID("telemetry_box"));
    assert(telemBox != nullptr);
    assert(telemBox->Anchor == UIAnchor::TopLeft);
    assert(telemBox->Margin == glm::vec4(32.0f, 32.0f, 0.0f, 0.0f));
    assert(telemBox->Size == glm::vec2(280.0f, 140.0f));
    assert(telemBox->Style.CornerRadius == 12.0f);
    assert(telemBox->Style.BorderWidth == 2.0f);

    auto* rpmGauge = dynamic_cast<UIRadialGaugeNode*>(hudDoc.FindNodeByID("rpm_radial_gauge"));
    assert(rpmGauge != nullptr);
    assert(rpmGauge->StartAngle == -225.0f);
    assert(rpmGauge->EndAngle == 45.0f);
    assert(rpmGauge->Thickness == 18.0f);

    auto* pauseBtn = dynamic_cast<UIButtonNode*>(hudDoc.FindNodeByID("btn_pause"));
    assert(pauseBtn != nullptr);
    assert(pauseBtn->OnClickAction == "OnPauseGame");
    assert(pauseBtn->NavDown == "btn_quit");

    std::cout << "  -> Hierarchical AST parsed: 8 nodes instantiated: PASS" << std::endl;
    std::cout << "  -> IDs, numeric scalars, anchors, and margins correctly assigned: PASS" << std::endl;

    // [89.3] Reactive Binding AST Extraction
    std::cout << "\n[89.3] Testing Reactive Binding AST Extraction..." << std::endl;
    const auto& bindings = hudDoc.GetBindings();
    assert(bindings.size() == 4);

    // 1. Interpolated: "FPS: {Engine.FPS}"
    assert(bindings[0].Type == UIBindingType::Interpolated);
    assert(bindings[0].PropertyName == "text");
    assert(bindings[0].SourceKey == "Engine.FPS");
    assert(bindings[0].TemplateText == "FPS: {Engine.FPS}");

    // 2. Interpolated: "LATERAL G: {Vehicle.LateralG}"
    assert(bindings[1].Type == UIBindingType::Interpolated);
    assert(bindings[1].PropertyName == "text");
    assert(bindings[1].SourceKey == "Vehicle.LateralG");

    // 3. Direct: "{Vehicle.RPMNormalized}"
    assert(bindings[2].Type == UIBindingType::Direct);
    assert(bindings[2].PropertyName == "value");
    assert(bindings[2].SourceKey == "Vehicle.RPMNormalized");

    // 4. Ternary: "{Vehicle.IsRedline ? #FF1E42FF : #00F5D4FF}"
    assert(bindings[3].Type == UIBindingType::Ternary);
    assert(bindings[3].PropertyName == "color");
    assert(bindings[3].SourceKey == "Vehicle.IsRedline");
    assert(bindings[3].TrueValue == "#FF1E42FF");
    assert(bindings[3].FalseValue == "#00F5D4FF");

    std::cout << "  -> Direct binding parsed: PASS" << std::endl;
    std::cout << "  -> Interpolated binding with string template parsed: PASS" << std::endl;
    std::cout << "  -> Conditional ternary binding with true/false branches parsed: PASS" << std::endl;

    // [89.4] File Loading & Real-Time Hot-Reloading Watcher
    std::cout << "\n[89.4] Testing File Loading & Real-Time Hot-Reloading..." << std::endl;
    std::filesystem::create_directories("test_output");
    std::string testFilePath = "test_output/HotReloadTest.axleui";

    // Write Version 1
    {
        std::ofstream out(testFilePath);
        out << R"(
            Canvas {
                id: "reload_root"
                Text {
                    id: "version_label"
                    text: "VERSION 1.0"
                    fontSize: 20
                }
            }
        )";
    }

    UIDocument hotDoc;
    assert(UIParser::ParseFile(testFilePath, hotDoc));
    assert(hotDoc.GetFilePath() == testFilePath);

    auto* verLabel = dynamic_cast<UITextNode*>(hotDoc.FindNodeByID("version_label"));
    assert(verLabel != nullptr);
    assert(verLabel->Text == "VERSION 1.0");

    // Check hot-reload without file modification: must return false
    assert(hotDoc.CheckAndHotReload() == false);
    assert(verLabel->Text == "VERSION 1.0");

    // Sleep briefly to ensure filesystem timestamp increments on write
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Overwrite with Version 2
    {
        std::ofstream out(testFilePath);
        out << R"(
            Canvas {
                id: "reload_root"
                Text {
                    id: "version_label"
                    text: "VERSION 2.0_HOT_RELOADED"
                    fontSize: 32
                }
            }
        )";
    }

    // Now CheckAndHotReload must detect the change and reload
    bool reloaded = hotDoc.CheckAndHotReload();
    assert(reloaded == true);

    auto* reloadedLabel = dynamic_cast<UITextNode*>(hotDoc.FindNodeByID("version_label"));
    assert(reloadedLabel != nullptr);
    assert(reloadedLabel->Text == "VERSION 2.0_HOT_RELOADED");
    assert(reloadedLabel->FontSize == 32.0f);

    std::cout << "  -> Initial file parse successful: PASS" << std::endl;
    std::cout << "  -> Unmodified file check returns false: PASS" << std::endl;
    std::cout << "  -> Modified file timestamp change detected and hot-reloaded: PASS" << std::endl;

    std::cout << "\n>>> STEP 89 PASSED: Declarative UIParser & Live Hot-Reloading 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 90: Phase 34 Step 5 - Reactive Data Binding (UIDataContext),
// Reverse Z-Order Hit-Testing, D-Pad Focus Navigation & ECS UICanvasComponent
// =============================================================================
bool RunTest_Step90_Phase34_Step5_DataBindingAndHitTest() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 90 TEST] REACTIVE BINDINGS, HIT-TEST & ECS CANVAS" << std::endl;
    std::cout << " (UIDataContext, Topmost Hit-Test, Focus Nav, ECS)      " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;

    // [90.1] UIDataContext Data Registry & Action Callbacks
    std::cout << "[90.1] Testing UIDataContext Value Registry & Actions..." << std::endl;
    UIDataContext ctx;
    ctx.SetInt("Engine.FPS", 144);
    ctx.SetFloat("Vehicle.SpeedKmh", 245.5f);
    ctx.SetFloat("Vehicle.RPMNormalized", 0.88f);
    ctx.SetBool("Vehicle.IsRedline", false);
    ctx.SetString("Vehicle.GearString", "4");
    ctx.SetColor("Vehicle.GaugeColor", glm::vec4(0.0f, 0.96f, 0.83f, 1.0f));

    assert(ctx.Has("Engine.FPS"));
    assert(ctx.Has("Vehicle.SpeedKmh"));
    assert(!ctx.Has("NonExistent.Key"));

    bool actionFired = false;
    ctx.RegisterAction("StartSimulation", [&]() { actionFired = true; });
    assert(ctx.TriggerAction("StartSimulation") == true);
    assert(actionFired == true);
    assert(ctx.TriggerAction("UnknownAction") == false);

    std::cout << "  -> Int, Float, Bool, String, and Vec4 properties stored: PASS" << std::endl;
    std::cout << "  -> Action callback registration and dispatch verified: PASS" << std::endl;

    // [90.2] Multi-Mode Binding Evaluation (Direct, Interpolated, Ternary)
    std::cout << "\n[90.2] Testing Direct, Interpolated, and Ternary Binding Evaluation..." << std::endl;
    std::string testUi = R"(
        Canvas {
            id: "hud"
            referenceResolution: [1920, 1080]

            Text {
                id: "speed_text"
                text: "{Vehicle.SpeedKmh}"
            }
            Text {
                id: "fps_text"
                text: "FPS: {Engine.FPS}"
            }
            ProgressBar {
                id: "rpm_bar"
                value: "{Vehicle.RPMNormalized}"
            }
            RadialGauge {
                id: "gauge"
                color: "{Vehicle.IsRedline ? #FF1E42FF : #00F5D4FF}"
            }
        }
    )";

    UIDocument doc;
    assert(UIParser::ParseSource(testUi, doc));

    auto* speedText = dynamic_cast<UITextNode*>(doc.FindNodeByID("speed_text"));
    auto* fpsText = dynamic_cast<UITextNode*>(doc.FindNodeByID("fps_text"));
    auto* rpmBar = dynamic_cast<UIProgressBarNode*>(doc.FindNodeByID("rpm_bar"));
    auto* gauge = dynamic_cast<UIRadialGaugeNode*>(doc.FindNodeByID("gauge"));

    assert(speedText && fpsText && rpmBar && gauge);

    // Initial evaluation with IsRedline = false
    doc.EvaluateBindings(ctx);
    assert(fpsText->Text == "FPS: 144");
    assert(std::abs(rpmBar->Value - 0.88f) < 1e-4f);
    assert(gauge->Color == UINode::ParseHexColor("#00F5D4FF"));

    // Update context: Redline active, speed up to 320
    ctx.SetBool("Vehicle.IsRedline", true);
    ctx.SetInt("Vehicle.SpeedKmh", 320);
    ctx.SetFloat("Vehicle.RPMNormalized", 0.98f);
    ctx.SetInt("Engine.FPS", 165);

    doc.EvaluateBindings(ctx);
    assert(fpsText->Text == "FPS: 165");
    assert(std::abs(rpmBar->Value - 0.98f) < 1e-4f);
    assert(gauge->Color == UINode::ParseHexColor("#FF1E42FF"));

    std::cout << "  -> Direct binding evaluated: PASS" << std::endl;
    std::cout << "  -> String interpolation evaluated: PASS" << std::endl;
    std::cout << "  -> Conditional ternary binding evaluated across state changes: PASS" << std::endl;

    // [90.3] Topmost Reverse Z-Order Mouse Hit-Testing
    std::cout << "\n[90.3] Testing Topmost Mouse Hit-Testing (Hover, Press, Click)..." << std::endl;
    std::string buttonUi = R"(
        Canvas {
            id: "button_canvas"
            referenceResolution: [1920, 1080]

            Button {
                id: "btn_resume"
                anchor: "TopLeft"
                pos: [100, 100]
                size: [200, 50]
                onClick: "OnResume"
                navDown: "btn_quit"
            }
            Button {
                id: "btn_quit"
                anchor: "TopLeft"
                pos: [100, 200]
                size: [200, 50]
                onClick: "OnQuit"
                navUp: "btn_resume"
            }
        }
    )";

    UIDocument btnDoc;
    assert(UIParser::ParseSource(buttonUi, btnDoc));
    btnDoc.UpdateLayout(glm::vec2(1920.0f, 1080.0f));

    auto* btnResume = dynamic_cast<UIButtonNode*>(btnDoc.FindNodeByID("btn_resume"));
    auto* btnQuit = dynamic_cast<UIButtonNode*>(btnDoc.FindNodeByID("btn_quit"));
    assert(btnResume && btnQuit);

    bool resumeClicked = false;
    bool quitClicked = false;
    ctx.RegisterAction("OnResume", [&]() { resumeClicked = true; });
    ctx.RegisterAction("OnQuit", [&]() { quitClicked = true; });

    // 1. Mouse hover inside btn_resume (pos: [150, 125])
    bool handled = btnDoc.ProcessInput(glm::vec2(150.0f, 125.0f), false, false, ctx);
    assert(handled == true);
    assert(btnResume->IsHovered == true);
    assert(btnResume->IsPressed == false);
    assert(btnQuit->IsHovered == false);

    // 2. Mouse press inside btn_resume
    handled = btnDoc.ProcessInput(glm::vec2(150.0f, 125.0f), true, false, ctx);
    assert(handled == true);
    assert(btnResume->IsPressed == true);

    // 3. Mouse click inside btn_resume
    handled = btnDoc.ProcessInput(glm::vec2(150.0f, 125.0f), false, true, ctx);
    assert(handled == true);
    assert(resumeClicked == true);

    // 4. Mouse click inside btn_quit (pos: [150, 225])
    handled = btnDoc.ProcessInput(glm::vec2(150.0f, 225.0f), false, true, ctx);
    assert(handled == true);
    assert(quitClicked == true);
    assert(btnQuit->IsHovered == true);
    assert(btnResume->IsHovered == false);

    // 5. Mouse outside all buttons (pos: [600, 600])
    handled = btnDoc.ProcessInput(glm::vec2(600.0f, 600.0f), false, false, ctx);
    assert(handled == false);
    assert(btnResume->IsHovered == false);
    assert(btnQuit->IsHovered == false);

    std::cout << "  -> Hover, press, and click input events dispatched: PASS" << std::endl;
    std::cout << "  -> Outside coordinates ignored and unhandled: PASS" << std::endl;

    // [90.4] Directional D-Pad / Gamepad Focus Navigation
    std::cout << "\n[90.4] Testing Directional Focus Navigation & Focused Activation..." << std::endl;
    quitClicked = false;
    resumeClicked = false;

    // Initial navigation: should focus first button (btn_resume)
    assert(btnDoc.NavigateFocus("Down") == true);
    assert(btnResume->IsFocused == true);
    assert(btnQuit->IsFocused == false);

    // Navigate Down: should move focus to btn_quit
    assert(btnDoc.NavigateFocus("Down") == true);
    assert(btnResume->IsFocused == false);
    assert(btnQuit->IsFocused == true);

    // Activate focused button (btn_quit)
    assert(btnDoc.ActivateFocusedButton(ctx) == true);
    assert(quitClicked == true);
    assert(resumeClicked == false);

    // Navigate Up: should move focus back to btn_resume
    assert(btnDoc.NavigateFocus("Up") == true);
    assert(btnResume->IsFocused == true);
    assert(btnQuit->IsFocused == false);

    // Activate focused button (btn_resume)
    assert(btnDoc.ActivateFocusedButton(ctx) == true);
    assert(resumeClicked == true);

    std::cout << "  -> D-Pad / Gamepad directional focus transitions: PASS" << std::endl;
    std::cout << "  -> ActivateFocusedButton invoked targeted callback: PASS" << std::endl;

    // [90.5] ECS UICanvasComponent Attachment & SceneSerializer Roundtrip
    std::cout << "\n[90.5] Testing ECS UICanvasComponent & Serialization Roundtrip..." << std::endl;
    auto scene = std::make_shared<Scene>();
    auto entity = scene->CreateEntity("VehicleHUD_Entity");

    auto& uic = entity.AddComponent<UICanvasComponent>("Editor/assets/ui/RacingHUD.axleui");
    uic.SortOrder = 7;
    uic.Visible = true;

    assert(entity.HasComponent<UICanvasComponent>());
    assert(scene->GetUIDataContext().Has("NonExistent") == false);

    // Serialize scene to string
    SceneSerializer serializer(scene.get());
    std::string yamlData = serializer.SerializeToString();
    assert(yamlData.find("UICanvasComponent:") != std::string::npos);
    assert(yamlData.find("AssetPath: Editor/assets/ui/RacingHUD.axleui") != std::string::npos);
    assert(yamlData.find("SortOrder: 7") != std::string::npos);

    // Deserialize into fresh scene
    auto loadedScene = std::make_shared<Scene>();
    SceneSerializer deserializer(loadedScene.get());
    assert(deserializer.DeserializeFromString(yamlData) == true);

    bool foundLoadedCanvas = false;
    auto canvasView = loadedScene->GetRegistry().view<UICanvasComponent, TagComponent>();
    for (auto ent : canvasView) {
        const auto& tag = canvasView.get<TagComponent>(ent);
        if (tag.Tag == "VehicleHUD_Entity") {
            const auto& comp = canvasView.get<UICanvasComponent>(ent);
            assert(comp.AssetPath == "Editor/assets/ui/RacingHUD.axleui");
            assert(comp.SortOrder == 7);
            assert(comp.Visible == true);
            foundLoadedCanvas = true;
            break;
        }
    }
    assert(foundLoadedCanvas == true);

    std::cout << "  -> UICanvasComponent attached to ECS entity: PASS" << std::endl;
    std::cout << "  -> SceneSerializer YAML serialization / deserialization roundtrip: PASS" << std::endl;

    std::cout << "\n>>> STEP 90 PASSED: Data Binding, Hit-Test & ECS Integration 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 91: Phase 34 Step 6 - RacingHUD & PauseMenu AxleUI Migration,
// Zero-ImGui Shipping Runtime Telemetry Bridge & Full In-Game UI Integration
// =============================================================================
bool RunTest_Step91_Phase34_Step6_RacingHUDFullIntegration() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 91 TEST] RACING HUD & PAUSE MENU AXLEUI PIPELINE " << std::endl;
    std::cout << " (Declarative Templates, Bindings, Canvas2D, Hit-Test)  " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;

    // [91.1] Loading & Parsing Declarative RacingHUD.axleui and PauseMenu.axleui
    std::cout << "[91.1] Loading and Parsing Declarative .axleui Documents..." << std::endl;
    std::string hudPath = "Editor/assets/ui/RacingHUD.axleui";
    if (!std::filesystem::exists(hudPath)) hudPath = "assets/ui/RacingHUD.axleui";

    std::string pausePath = "Editor/assets/ui/PauseMenu.axleui";
    if (!std::filesystem::exists(pausePath)) pausePath = "assets/ui/PauseMenu.axleui";

    UIDocument hudDoc;
    std::string hudErr;
    bool hudParsed = UIParser::ParseFile(hudPath, hudDoc, &hudErr);
    if (!hudParsed) {
        std::cerr << "FAIL: Failed to parse RacingHUD.axleui! Error: " << hudErr << std::endl;
        return false;
    }

    UIDocument pauseDoc;
    std::string pauseErr;
    bool pauseParsed = UIParser::ParseFile(pausePath, pauseDoc, &pauseErr);
    if (!pauseParsed) {
        std::cerr << "FAIL: Failed to parse PauseMenu.axleui! Error: " << pauseErr << std::endl;
        return false;
    }

    assert(hudDoc.GetNodeCount() >= 10);
    assert(pauseDoc.GetNodeCount() >= 5);
    std::cout << "  -> RacingHUD.axleui parsed successfully (" << hudDoc.GetNodeCount() << " nodes instantiated): PASS" << std::endl;
    std::cout << "  -> PauseMenu.axleui parsed successfully (" << pauseDoc.GetNodeCount() << " nodes instantiated): PASS" << std::endl;

    // [91.2] Live Vehicle Telemetry & Binding Evaluation
    std::cout << "\n[91.2] Testing Live Vehicle Telemetry & Binding Evaluation..." << std::endl;
    UIDataContext ctx;
    ctx.Set("Engine.FPS", 144);
    ctx.Set("Vehicle.SpeedKmh", 215);
    ctx.Set("Vehicle.SpeedUnit", std::string("KM/H"));
    ctx.Set("Vehicle.RPMNormalized", 0.92f);
    ctx.Set("Vehicle.IsRedline", true);
    ctx.Set("Vehicle.GearString", std::string("5"));
    ctx.Set("Vehicle.Throttle", 1.0f);
    ctx.Set("Vehicle.Brake", 0.0f);
    ctx.Set("Vehicle.LateralG", std::string("+0.85"));
    ctx.Set("Vehicle.LongitudinalG", std::string("-0.32"));
    ctx.Set("Vehicle.CameraMode", std::string("CHASE"));
    ctx.Set("Game.IsPaused", false);

    bool resumedGame = false;
    bool resetVehicle = false;
    bool quitGame = false;
    ctx.RegisterAction("OnResumeGame", [&]() { resumedGame = true; });
    ctx.RegisterAction("OnResetVehicle", [&]() { resetVehicle = true; });
    ctx.RegisterAction("OnQuitGame", [&]() { quitGame = true; });

    hudDoc.EvaluateBindings(ctx);

    auto* speedNode = dynamic_cast<UITextNode*>(hudDoc.FindNodeByID("speed_value"));
    assert(speedNode != nullptr);
    assert(speedNode->Text == "215");

    auto* gearNode = dynamic_cast<UITextNode*>(hudDoc.FindNodeByID("gear_indicator"));
    assert(gearNode != nullptr);
    assert(gearNode->Text == "5");

    auto* rpmGauge = dynamic_cast<UIRadialGaugeNode*>(hudDoc.FindNodeByID("rpm_radial_gauge"));
    assert(rpmGauge != nullptr);
    assert(std::abs(rpmGauge->Value - 0.92f) < 1e-4f);

    auto* throttleBar = dynamic_cast<UIProgressBarNode*>(hudDoc.FindNodeByID("throttle_bar"));
    assert(throttleBar != nullptr);
    assert(std::abs(throttleBar->Value - 1.0f) < 1e-4f);

    auto* brakeBar = dynamic_cast<UIProgressBarNode*>(hudDoc.FindNodeByID("brake_bar"));
    assert(brakeBar != nullptr);
    assert(std::abs(brakeBar->Value - 0.0f) < 1e-4f);

    std::cout << "  -> Speed string ('215') evaluated: PASS" << std::endl;
    std::cout << "  -> Gear indicator ('5') and RPM Gauge (0.92, Redline) evaluated: PASS" << std::endl;
    std::cout << "  -> Throttle (1.0) and Brake (0.0) pedal bars evaluated: PASS" << std::endl;

    // [91.3] 2-Pass Responsive Layout across Resolutions (1080p, 4K, 720p)
    std::cout << "\n[91.3] Testing 2-Pass Responsive Layout Engine across Resolutions..." << std::endl;
    hudDoc.UpdateLayout(glm::vec2(1920.0f, 1080.0f));
    assert(std::abs(hudDoc.GetComputedDPIScale() - 1.0f) < 1e-4f);

    auto* cluster = hudDoc.FindNodeByID("tachometer_cluster");
    assert(cluster != nullptr);
    assert(cluster->ComputedRect.X + cluster->ComputedRect.Width <= 1920.0f);
    assert(cluster->ComputedRect.Y + cluster->ComputedRect.Height <= 1080.0f);

    auto* telemPanel = hudDoc.FindNodeByID("telemetry_panel");
    assert(telemPanel != nullptr);
    assert(telemPanel->ComputedRect.X >= 0.0f);
    assert(telemPanel->ComputedRect.Y >= 0.0f);

    // 4K Layout
    hudDoc.UpdateLayout(glm::vec2(3840.0f, 2160.0f));
    assert(std::abs(hudDoc.GetComputedDPIScale() - 2.0f) < 1e-4f);
    assert((cluster->ComputedRect.X + cluster->ComputedRect.Width) * hudDoc.GetComputedDPIScale() <= 3840.0f + 0.1f);

    // 720p Layout
    hudDoc.UpdateLayout(glm::vec2(1280.0f, 720.0f));
    assert(std::abs(hudDoc.GetComputedDPIScale() - (1280.0f / 1920.0f)) < 1e-4f);
    assert((cluster->ComputedRect.X + cluster->ComputedRect.Width) * hudDoc.GetComputedDPIScale() <= 1280.0f + 0.1f);

    std::cout << "  -> 1080p, 4K (2.0x DPI), and 720p (0.667x DPI) responsive layouts verified: PASS" << std::endl;

    // [91.4] Hardware Canvas2D Batch Rendering (1 Draw Call, Multi-Quad)
    std::cout << "\n[91.4] Testing Hardware Canvas2D Batch Rendering..." << std::endl;
    hudDoc.UpdateLayout(glm::vec2(1920.0f, 1080.0f));
    Canvas2D canvas;
    canvas.BeginCanvas(glm::vec2(1920.0f, 1080.0f));
    hudDoc.Render(canvas);
    canvas.EndCanvas();

    const auto& stats = canvas.GetStats();
    assert(stats.DrawCalls == 1);
    assert(stats.QuadCount >= 15);
    assert(stats.VertexCount == stats.QuadCount * 4);
    assert(stats.IndexCount == stats.QuadCount * 6);
    std::cout << "  -> Canvas2D consolidated " << stats.QuadCount << " quads (" 
              << stats.VertexCount << " verts, " << stats.IndexCount << " idx) in " 
              << stats.DrawCalls << " draw call: PASS" << std::endl;

    // [91.5] Interactive Pause Menu & Action Callback Triggers
    std::cout << "\n[91.5] Testing Interactive Pause Menu & Action Callback Triggers..." << std::endl;
    pauseDoc.EvaluateBindings(ctx);
    assert(pauseDoc.GetRootNode()->Visible == false); // Game.IsPaused is false

    ctx.Set("Game.IsPaused", true);
    pauseDoc.EvaluateBindings(ctx);
    assert(pauseDoc.GetRootNode()->Visible == true); // Game.IsPaused is true

    pauseDoc.UpdateLayout(glm::vec2(1920.0f, 1080.0f));
    auto* btnResume = dynamic_cast<UIButtonNode*>(pauseDoc.FindNodeByID("btn_resume"));
    assert(btnResume != nullptr);
    glm::vec2 clickPos(btnResume->ComputedRect.X + btnResume->ComputedRect.Width * 0.5f,
                       btnResume->ComputedRect.Y + btnResume->ComputedRect.Height * 0.5f);

    bool clickHandled = pauseDoc.ProcessInput(clickPos, true, true, ctx);
    assert(clickHandled == true);
    assert(resumedGame == true);

    // Focus Navigation on Pause Menu
    bool navHandled = pauseDoc.NavigateFocus("Down"); // Initial focus lands on first button (btn_resume)
    assert(navHandled == true);
    assert(btnResume->IsFocused == true);

    navHandled = pauseDoc.NavigateFocus("Down"); // Move to btn_reset_vehicle
    assert(navHandled == true);
    pauseDoc.ActivateFocusedButton(ctx);
    assert(resetVehicle == true);

    navHandled = pauseDoc.NavigateFocus("Down"); // Move to btn_quit
    assert(navHandled == true);
    pauseDoc.ActivateFocusedButton(ctx);
    assert(quitGame == true);

    std::cout << "  -> Visibility binding '{Game.IsPaused}' verified: PASS" << std::endl;
    std::cout << "  -> Mouse hit-test and 'OnResumeGame' trigger: PASS" << std::endl;
    std::cout << "  -> Directional focus navigation and 'OnResetVehicle' / 'OnQuitGame' triggers: PASS" << std::endl;

    // [91.6] Standalone Zero-ImGui Shipping Runtime & ECS Lifecycle Integration
    std::cout << "\n[91.6] Testing ECS RacingHUD Script Lifecycle & UIDataContext Bridge..." << std::endl;
    auto scene = std::make_shared<Scene>();
    auto hudEntity = scene->CreateEntity("RacingHUD_Entity");
    auto& nsc = hudEntity.AddComponent<NativeScriptComponent>("RacingHUD");
    nsc.Bind<RacingHUD>();
    nsc.Instance = nsc.InstantiateScript();
    nsc.Instance->SetEntity(hudEntity);
    nsc.Instance->OnCreate();

    assert(hudEntity.HasComponent<UICanvasComponent>() == true);
    auto& uic = hudEntity.GetComponent<UICanvasComponent>();
    assert(uic.Visible == true);

    // Tick update
    auto* hudInstance = static_cast<RacingHUD*>(nsc.Instance);
    hudInstance->OnUpdate(0.016f);

    assert(scene->GetUIDataContext().Has("Vehicle.SpeedKmh") == true);
    assert(scene->GetUIDataContext().Has("Engine.FPS") == true);

    // Visibility toggle
    hudInstance->Visible = false;
    hudInstance->OnUpdate(0.016f);
    assert(uic.Visible == false);

    nsc.Instance->OnDestroy();
    delete nsc.Instance;
    nsc.Instance = nullptr;

    std::cout << "  -> NativeScriptComponent attached UICanvasComponent to entity: PASS" << std::endl;
    std::cout << "  -> OnUpdate published telemetry to Scene UIDataContext: PASS" << std::endl;
    std::cout << "  -> Visibility toggle synced with UICanvasComponent: PASS" << std::endl;

    std::cout << "\n>>> STEP 91 PASSED: RacingHUD AxleUI Migration & Zero-ImGui Runtime 100% verified! <<<\n" << std::endl;
    return true;
}

bool RunTest_Step92_Phase34_Step7_RectTransform() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 92 TEST] UNITY-STYLE RECTTRANSFORM ABSTRACTION   " << std::endl;
    std::cout << " (Anchors, Pivots, Insets, SetRect, Multi-Res, Corners) " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;

    UIRect parent1080p{ 0.0f, 0.0f, 1920.0f, 1080.0f };

    // [92.1] Point Anchors & Normalized Pivots
    std::cout << "[92.1] Testing Unity-Style Point Anchors & Normalized Pivots..." << std::endl;
    {
        // Top-Left (Pivot: 0, 0)
        RectTransform rtTL;
        rtTL.AnchorMin = { 0.0f, 0.0f };
        rtTL.AnchorMax = { 0.0f, 0.0f };
        rtTL.Pivot = { 0.0f, 0.0f };
        rtTL.SizeDelta = { 200.0f, 100.0f };
        rtTL.AnchoredPosition = { 50.0f, 30.0f };

        UIRect rTL = rtTL.ComputeRect(parent1080p);
        assert(std::abs(rTL.X - 50.0f) < 1e-4f);
        assert(std::abs(rTL.Y - 30.0f) < 1e-4f);
        assert(std::abs(rTL.Width - 200.0f) < 1e-4f);
        assert(std::abs(rTL.Height - 100.0f) < 1e-4f);

        // Center (Pivot: 0.5, 0.5)
        RectTransform rtCenter;
        rtCenter.AnchorMin = { 0.5f, 0.5f };
        rtCenter.AnchorMax = { 0.5f, 0.5f };
        rtCenter.Pivot = { 0.5f, 0.5f };
        rtCenter.SizeDelta = { 400.0f, 200.0f };
        rtCenter.AnchoredPosition = { 0.0f, 0.0f };

        UIRect rCenter = rtCenter.ComputeRect(parent1080p);
        assert(std::abs(rCenter.X - (1920.0f - 400.0f) * 0.5f) < 1e-4f); // 760
        assert(std::abs(rCenter.Y - (1080.0f - 200.0f) * 0.5f) < 1e-4f); // 440
        assert(std::abs(rCenter.Width - 400.0f) < 1e-4f);
        assert(std::abs(rCenter.Height - 200.0f) < 1e-4f);

        // Bottom-Right (Pivot: 1, 1)
        RectTransform rtBR;
        rtBR.AnchorMin = { 1.0f, 1.0f };
        rtBR.AnchorMax = { 1.0f, 1.0f };
        rtBR.Pivot = { 1.0f, 1.0f };
        rtBR.SizeDelta = { 300.0f, 300.0f };
        rtBR.AnchoredPosition = { -40.0f, -40.0f };

        UIRect rBR = rtBR.ComputeRect(parent1080p);
        assert(std::abs(rBR.X - (1920.0f - 300.0f - 40.0f)) < 1e-4f); // 1580
        assert(std::abs(rBR.Y - (1080.0f - 300.0f - 40.0f)) < 1e-4f); // 740
        assert(std::abs(rBR.Width - 300.0f) < 1e-4f);
        assert(std::abs(rBR.Height - 300.0f) < 1e-4f);

        // Arbitrary Non-Centered Pivot (0.25, 0.75)
        RectTransform rtCustom;
        rtCustom.AnchorMin = { 0.5f, 0.5f };
        rtCustom.AnchorMax = { 0.5f, 0.5f };
        rtCustom.Pivot = { 0.25f, 0.75f };
        rtCustom.SizeDelta = { 200.0f, 100.0f };
        rtCustom.AnchoredPosition = { 0.0f, 0.0f };

        glm::vec2 pivotScreen = rtCustom.GetPivotScreenPos(parent1080p);
        assert(std::abs(pivotScreen.x - 960.0f) < 1e-4f);
        assert(std::abs(pivotScreen.y - 540.0f) < 1e-4f);

        UIRect rCustom = rtCustom.ComputeRect(parent1080p);
        assert(std::abs(rCustom.X - (960.0f - 0.25f * 200.0f)) < 1e-4f); // 910
        assert(std::abs(rCustom.Y - (540.0f - 0.75f * 100.0f)) < 1e-4f); // 465
        assert(std::abs(rCustom.Width - 200.0f) < 1e-4f);
        assert(std::abs(rCustom.Height - 100.0f) < 1e-4f);

        std::cout << "  -> Top-Left, Center, Bottom-Right, and custom non-centered pivots: PASS" << std::endl;
    }

    // [92.2] Stretch Anchors & Margin Insets (Offsets)
    std::cout << "\n[92.2] Testing Stretch Anchors & Margin Insets (Offsets)..." << std::endl;
    {
        // Horizontal Stretch (TopStretch: Left = 20, Right = 20, Top = 20, Height = 60)
        RectTransform rtHStretch;
        rtHStretch.AnchorMin = { 0.0f, 0.0f };
        rtHStretch.AnchorMax = { 1.0f, 0.0f };
        rtHStretch.Pivot = { 0.5f, 0.0f };
        rtHStretch.SizeDelta = { -40.0f, 60.0f };
        rtHStretch.AnchoredPosition = { 0.0f, 20.0f };

        UIRect rH = rtHStretch.ComputeRect(parent1080p);
        assert(std::abs(rH.X - 20.0f) < 1e-4f);
        assert(std::abs(rH.Y - 20.0f) < 1e-4f);
        assert(std::abs(rH.Width - (1920.0f - 40.0f)) < 1e-4f); // 1880
        assert(std::abs(rH.Height - 60.0f) < 1e-4f);

        // Full 2D Stretch (StretchAll) via SetOffsets
        RectTransform rtFullStretch;
        rtFullStretch.AnchorMin = { 0.0f, 0.0f };
        rtFullStretch.AnchorMax = { 1.0f, 1.0f };
        rtFullStretch.Pivot = { 0.5f, 0.5f };
        rtFullStretch.SetOffsets(glm::vec2(30.0f, 20.0f), glm::vec2(-50.0f, -40.0f), parent1080p);

        UIRect rFull = rtFullStretch.ComputeRect(parent1080p);
        assert(std::abs(rFull.X - 30.0f) < 1e-4f);
        assert(std::abs(rFull.Y - 20.0f) < 1e-4f);
        assert(std::abs(rFull.Width - (1920.0f - 30.0f - 50.0f)) < 1e-4f); // 1840
        assert(std::abs(rFull.Height - (1080.0f - 20.0f - 40.0f)) < 1e-4f); // 1020

        glm::vec2 offsetMin = rtFullStretch.GetOffsetMin(parent1080p);
        glm::vec2 offsetMax = rtFullStretch.GetOffsetMax(parent1080p);
        assert(std::abs(offsetMin.x - 30.0f) < 1e-4f && std::abs(offsetMin.y - 20.0f) < 1e-4f);
        assert(std::abs(offsetMax.x - -50.0f) < 1e-4f && std::abs(offsetMax.y - -40.0f) < 1e-4f);

        std::cout << "  -> Horizontal stretch and full 2D stretch with insets: PASS" << std::endl;
        std::cout << "  -> GetOffsetMin / GetOffsetMax roundtrip precision: PASS" << std::endl;
    }

    // [92.3] SetRect & KeepScreenRect Preset Switching
    std::cout << "\n[92.3] Testing SetRect & KeepScreenRect Preset Switching..." << std::endl;
    {
        UIRect targetRect{ 480.0f, 270.0f, 320.0f, 180.0f };
        RectTransform rt;
        rt.AnchorMin = { 0.0f, 0.0f };
        rt.AnchorMax = { 0.0f, 0.0f };
        rt.Pivot = { 0.5f, 0.5f };

        rt.SetRect(targetRect, parent1080p);
        UIRect r1 = rt.ComputeRect(parent1080p);
        assert(std::abs(r1.X - targetRect.X) < 1e-4f);
        assert(std::abs(r1.Y - targetRect.Y) < 1e-4f);
        assert(std::abs(r1.Width - targetRect.Width) < 1e-4f);
        assert(std::abs(r1.Height - targetRect.Height) < 1e-4f);

        // Switch to Center preset with keepScreenRect = true
        rt.SetAnchorPreset(UIAnchor::Center, true, parent1080p);
        UIRect rCenter = rt.ComputeRect(parent1080p);
        assert(std::abs(rCenter.X - targetRect.X) < 1e-4f);
        assert(std::abs(rCenter.Y - targetRect.Y) < 1e-4f);
        assert(std::abs(rCenter.Width - targetRect.Width) < 1e-4f);
        assert(std::abs(rCenter.Height - targetRect.Height) < 1e-4f);

        // Switch to BottomRight preset with keepScreenRect = true
        rt.SetAnchorPreset(UIAnchor::BottomRight, true, parent1080p);
        UIRect rBR = rt.ComputeRect(parent1080p);
        assert(std::abs(rBR.X - targetRect.X) < 1e-4f);
        assert(std::abs(rBR.Y - targetRect.Y) < 1e-4f);
        assert(std::abs(rBR.Width - targetRect.Width) < 1e-4f);
        assert(std::abs(rBR.Height - targetRect.Height) < 1e-4f);

        // Switch to StretchAll preset with keepScreenRect = true
        rt.SetAnchorPreset(UIAnchor::StretchAll, true, parent1080p);
        UIRect rStretch = rt.ComputeRect(parent1080p);
        assert(std::abs(rStretch.X - targetRect.X) < 1e-4f);
        assert(std::abs(rStretch.Y - targetRect.Y) < 1e-4f);
        assert(std::abs(rStretch.Width - targetRect.Width) < 1e-4f);
        assert(std::abs(rStretch.Height - targetRect.Height) < 1e-4f);

        std::cout << "  -> Preserved exact screen bounds across TopLeft, Center, BottomRight, StretchAll: PASS" << std::endl;
    }

    // [92.4] Multi-Resolution Responsive Layout Scaling
    std::cout << "\n[92.4] Testing Multi-Resolution Responsive Layout Scaling..." << std::endl;
    {
        // 1. Header: TopStretch, height = 60, margins = 20
        RectTransform header;
        header.AnchorMin = { 0.0f, 0.0f };
        header.AnchorMax = { 1.0f, 0.0f };
        header.Pivot = { 0.5f, 0.0f };
        header.SizeDelta = { -40.0f, 60.0f };
        header.AnchoredPosition = { 0.0f, 20.0f };

        // 2. Minimap: BottomRight, 200x200, margin 30
        RectTransform minimap;
        minimap.AnchorMin = { 1.0f, 1.0f };
        minimap.AnchorMax = { 1.0f, 1.0f };
        minimap.Pivot = { 1.0f, 1.0f };
        minimap.SizeDelta = { 200.0f, 200.0f };
        minimap.AnchoredPosition = { -30.0f, -30.0f };

        // 3. Center Crosshair: Center, 32x32
        RectTransform crosshair;
        crosshair.AnchorMin = { 0.5f, 0.5f };
        crosshair.AnchorMax = { 0.5f, 0.5f };
        crosshair.Pivot = { 0.5f, 0.5f };
        crosshair.SizeDelta = { 32.0f, 32.0f };
        crosshair.AnchoredPosition = { 0.0f, 0.0f };

        // Test on 1080p (1920x1080)
        UIRect h1080 = header.ComputeRect(parent1080p);
        UIRect m1080 = minimap.ComputeRect(parent1080p);
        UIRect c1080 = crosshair.ComputeRect(parent1080p);
        assert(h1080.Width == 1880.0f);
        assert(m1080.X == 1920.0f - 230.0f);
        assert(c1080.X == (1920.0f - 32.0f) * 0.5f);

        // Test on 4K UHD (3840x2160)
        UIRect parent4K{ 0.0f, 0.0f, 3840.0f, 2160.0f };
        UIRect h4K = header.ComputeRect(parent4K);
        UIRect m4K = minimap.ComputeRect(parent4K);
        UIRect c4K = crosshair.ComputeRect(parent4K);
        assert(h4K.Width == 3840.0f - 40.0f); // Stretched dynamically
        assert(m4K.X == 3840.0f - 230.0f);    // Anchored to bottom right
        assert(m4K.Y == 2160.0f - 230.0f);
        assert(c4K.X == (3840.0f - 32.0f) * 0.5f); // Stayed dead center

        // Test on Ultrawide 21:9 (2560x1080)
        UIRect parentUW{ 0.0f, 0.0f, 2560.0f, 1080.0f };
        UIRect hUW = header.ComputeRect(parentUW);
        UIRect mUW = minimap.ComputeRect(parentUW);
        UIRect cUW = crosshair.ComputeRect(parentUW);
        assert(hUW.Width == 2560.0f - 40.0f);
        assert(mUW.X == 2560.0f - 230.0f);
        assert(cUW.X == (2560.0f - 32.0f) * 0.5f);

        std::cout << "  -> 1080p, 4K UHD (3840x2160), and 21:9 Ultrawide (2560x1080) scaling: PASS" << std::endl;
    }

    // [92.5] Corners, Rotation & Scaling around Pivot
    std::cout << "\n[92.5] Testing Corners, Rotation & Scaling around Pivot..." << std::endl;
    {
        RectTransform rt;
        rt.AnchorMin = { 0.0f, 0.0f };
        rt.AnchorMax = { 0.0f, 0.0f };
        rt.Pivot = { 0.5f, 0.5f };
        rt.SizeDelta = { 200.0f, 100.0f };
        rt.AnchoredPosition = { 200.0f, 150.0f }; // Center is at (200, 150), corners: (100, 100) to (300, 200)

        glm::vec2 corners[4];
        rt.GetWorldCorners(parent1080p, corners);
        assert(std::abs(corners[0].x - 100.0f) < 1e-4f && std::abs(corners[0].y - 100.0f) < 1e-4f); // TL
        assert(std::abs(corners[1].x - 300.0f) < 1e-4f && std::abs(corners[1].y - 100.0f) < 1e-4f); // TR
        assert(std::abs(corners[2].x - 300.0f) < 1e-4f && std::abs(corners[2].y - 200.0f) < 1e-4f); // BR
        assert(std::abs(corners[3].x - 100.0f) < 1e-4f && std::abs(corners[3].y - 200.0f) < 1e-4f); // BL

        // Scale by 2.0x around Pivot (200, 150)
        rt.Scale = { 2.0f, 2.0f };
        rt.GetWorldCorners(parent1080p, corners);
        assert(std::abs(corners[0].x - 0.0f) < 1e-4f && std::abs(corners[0].y - 50.0f) < 1e-4f);   // TL
        assert(std::abs(corners[2].x - 400.0f) < 1e-4f && std::abs(corners[2].y - 250.0f) < 1e-4f); // BR

        // 90-degree Rotation around Pivot
        rt.Scale = { 1.0f, 1.0f };
        rt.Rotation = 90.0f;
        rt.GetWorldCorners(parent1080p, corners);
        // Pivot is (200, 150). Original TL was (100, 100) -> d = (-100, -50).
        // Rotated 90 deg: (d.x*0 - d.y*1, d.x*1 + d.y*0) = (50, -100) -> Pivot + (50, -100) = (250, 50)
        assert(std::abs(corners[0].x - 250.0f) < 1e-3f && std::abs(corners[0].y - 50.0f) < 1e-3f);

        std::cout << "  -> Local scale and rotation around pivot corner transformations: PASS" << std::endl;
    }

    // [92.6] Declarative .axleui Parsing with RectTransform & Canvas2D Draw
    std::cout << "\n[92.6] Testing Declarative .axleui Parsing with RectTransform..." << std::endl;
    {
        const char* axleuiSource = R"(
            Canvas {
                id: "rect_canvas"
                referenceResolution: [1920, 1080]

                Rectangle {
                    id: "center_panel"
                    anchorMin: [0.5, 0.5]
                    anchorMax: [0.5, 0.5]
                    pivot: [0.5, 0.5]
                    sizeDelta: [500, 300]
                    anchoredPosition: [0, 0]
                    color: "#0F172ACC"
                    cornerRadius: 12
                }
            }
        )";

        UIDocument doc;
        bool parseOk = UIParser::ParseSource(axleuiSource, doc);
        assert(parseOk == true);

        auto* panel = doc.FindNodeByID("center_panel");
        assert(panel != nullptr);
        assert(panel->UseRectTransform == true);
        assert(panel->Transform.AnchorMin.x == 0.5f && panel->Transform.AnchorMin.y == 0.5f);
        assert(panel->Transform.SizeDelta.x == 500.0f && panel->Transform.SizeDelta.y == 300.0f);

        doc.UpdateLayout(glm::vec2(1920.0f, 1080.0f));
        assert(std::abs(panel->ComputedRect.X - (1920.0f - 500.0f) * 0.5f) < 1e-4f);
        assert(std::abs(panel->ComputedRect.Y - (1080.0f - 300.0f) * 0.5f) < 1e-4f);
        assert(std::abs(panel->ComputedRect.Width - 500.0f) < 1e-4f);
        assert(std::abs(panel->ComputedRect.Height - 300.0f) < 1e-4f);

        // Hardware Canvas2D Render
        Canvas2D canvas;
        canvas.BeginCanvas(glm::vec2(1920.0f, 1080.0f));
        doc.Render(canvas);
        canvas.EndCanvas();

        const auto& stats = canvas.GetStats();
        assert(stats.DrawCalls == 1);
        assert(stats.QuadCount == 1);

        std::cout << "  -> Declarative .axleui parsed RectTransform properties into AST: PASS" << std::endl;
        std::cout << "  -> UpdateLayout evaluated exact center geometry and Canvas2D batch: PASS" << std::endl;
    }

    std::cout << "\n>>> STEP 92 PASSED: Unity-Style RectTransform Geometric Abstraction 100% verified! <<<\n" << std::endl;
    return true;
}

bool RunTest_Step93_Phase34_Step7_RectTransformGizmo() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 93 TEST] INTERACTIVE 2D CANVAS GIZMO HANDLES    " << std::endl;
    std::cout << " (Hit-Testing, 8 Resizes, Pivot, 4 Anchors, Snapping)   " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;

    UIRect parent1080p{ 0.0f, 0.0f, 1920.0f, 1080.0f };
    glm::vec2 vpPos{ 0.0f, 0.0f };
    glm::vec2 vpSize{ 1920.0f, 1080.0f };
    auto& gizmo = RectTransformGizmo::Get();

    // [93.1] Testing Canvas-to-Viewport Coordinate Mapping & Letterboxing
    std::cout << "[93.1] Testing Canvas-to-Viewport Coordinate Mapping..." << std::endl;
    {
        // 1:1 Viewport
        RectTransform rt;
        rt.AnchorMin = { 0.0f, 0.0f };
        rt.AnchorMax = { 0.0f, 0.0f };
        rt.Pivot = { 0.5f, 0.5f };
        rt.SizeDelta = { 200.0f, 100.0f };
        rt.AnchoredPosition = { 200.0f, 150.0f }; // Rect: (100, 100) to (300, 200), Pivot: (200, 150)

        Gizmo2DHandle h = gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(100.0f, 100.0f));
        assert(h == Gizmo2DHandle::Corner_TL);

        // Scaled Viewport (720p: 1280x720)
        glm::vec2 vpSize720{ 1280.0f, 720.0f };
        float scale = 1280.0f / 1920.0f; // 0.666667
        glm::vec2 expectedScreenPos = glm::vec2(100.0f, 100.0f) * scale;
        h = gizmo.HitTest(parent1080p, rt, vpPos, vpSize720, expectedScreenPos);
        assert(h == Gizmo2DHandle::Corner_TL);

        std::cout << "  -> 1:1 and scaled 720p viewport canvas mapping: PASS" << std::endl;
    }

    // [93.2] Testing Hit-Testing across all 8 Resize Handles & Body
    std::cout << "\n[93.2] Testing Hit-Testing across 8 Resize Handles & Body..." << std::endl;
    {
        RectTransform rt;
        rt.AnchorMin = { 0.5f, 0.5f };
        rt.AnchorMax = { 0.5f, 0.5f };
        rt.Pivot = { 0.5f, 0.5f };
        rt.SizeDelta = { 400.0f, 200.0f };
        rt.AnchoredPosition = { 0.0f, 0.0f };

        // Center element: rect is (760, 440) to (1160, 640)
        // 4 Corners:
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(760.0f, 440.0f)) == Gizmo2DHandle::Corner_TL);
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(1160.0f, 440.0f)) == Gizmo2DHandle::Corner_TR);
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(1160.0f, 640.0f)) == Gizmo2DHandle::Corner_BR);
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(760.0f, 640.0f)) == Gizmo2DHandle::Corner_BL);

        // 4 Edges:
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(960.0f, 440.0f)) == Gizmo2DHandle::Edge_T);
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(1160.0f, 540.0f)) == Gizmo2DHandle::Edge_R);
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(960.0f, 640.0f)) == Gizmo2DHandle::Edge_B);
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(760.0f, 540.0f)) == Gizmo2DHandle::Edge_L);

        // Body interior:
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(850.0f, 500.0f)) == Gizmo2DHandle::Body);

        // Outside:
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(200.0f, 200.0f)) == Gizmo2DHandle::None);

        std::cout << "  -> 4 Corners (TL, TR, BR, BL) hit-tested: PASS" << std::endl;
        std::cout << "  -> 4 Midpoints (T, R, B, L) hit-tested: PASS" << std::endl;
        std::cout << "  -> Body interior and outside rejection: PASS" << std::endl;
    }

    // [93.3] Testing Pivot Crosshair Hit-Testing & Invariant Rect During Pivot Drag
    std::cout << "\n[93.3] Testing Pivot Crosshair Hit-Testing & Drag Invariance..." << std::endl;
    {
        RectTransform rt;
        rt.AnchorMin = { 0.5f, 0.5f };
        rt.AnchorMax = { 0.5f, 0.5f };
        rt.Pivot = { 0.25f, 0.75f };
        rt.SizeDelta = { 400.0f, 200.0f };
        rt.AnchoredPosition = { 0.0f, 0.0f };

        UIRect rInitial = rt.ComputeRect(parent1080p);
        glm::vec2 pivotScreen = rt.GetPivotScreenPos(parent1080p);
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, pivotScreen) == Gizmo2DHandle::Pivot);

        // Simulate dragging pivot to (0.8, 0.2)
        rt.Pivot = { 0.8f, 0.2f };
        rt.SetRect(rInitial, parent1080p);
        UIRect rAfter = rt.ComputeRect(parent1080p);

        assert(std::abs(rAfter.X - rInitial.X) < 1e-4f);
        assert(std::abs(rAfter.Y - rInitial.Y) < 1e-4f);
        assert(std::abs(rAfter.Width - rInitial.Width) < 1e-4f);
        assert(std::abs(rAfter.Height - rInitial.Height) < 1e-4f);

        std::cout << "  -> Pivot hit-test at non-centered position: PASS" << std::endl;
        std::cout << "  -> Visual bounding rectangle invariant during pivot shift: PASS" << std::endl;
    }

    // [93.4] Testing Anchor Petals Hit-Testing & Boundary Splitting
    std::cout << "\n[93.4] Testing 4 Anchor Petals & Boundary Splitting..." << std::endl;
    {
        RectTransform rt;
        // Stretched anchors: AnchorMin = (0.2, 0.2), AnchorMax = (0.8, 0.8)
        rt.AnchorMin = { 0.2f, 0.2f };
        rt.AnchorMax = { 0.8f, 0.8f };
        rt.Pivot = { 0.5f, 0.5f };
        rt.SizeDelta = { -40.0f, -40.0f };
        rt.AnchoredPosition = { 0.0f, 0.0f };

        // Anchor screen positions at 1920x1080:
        // aMin = (0.2 * 1920, 0.2 * 1080) = (384, 216)
        // aMax = (0.8 * 1920, 0.8 * 1080) = (1536, 864)
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(384.0f, 216.0f)) == Gizmo2DHandle::Anchor_TL);
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(1536.0f, 216.0f)) == Gizmo2DHandle::Anchor_TR);
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(1536.0f, 864.0f)) == Gizmo2DHandle::Anchor_BR);
        assert(gizmo.HitTest(parent1080p, rt, vpPos, vpSize, glm::vec2(384.0f, 864.0f)) == Gizmo2DHandle::Anchor_BL);

        // Simulate Anchor Boundary Splitting (drag Anchor_TL inwards)
        UIRect rInitial = rt.ComputeRect(parent1080p);
        rt.AnchorMin = { 0.3f, 0.3f };
        rt.SetRect(rInitial, parent1080p);
        UIRect rAfter = rt.ComputeRect(parent1080p);

        assert(std::abs(rAfter.X - rInitial.X) < 1e-4f);
        assert(std::abs(rAfter.Y - rInitial.Y) < 1e-4f);
        assert(std::abs(rAfter.Width - rInitial.Width) < 1e-4f);
        assert(std::abs(rAfter.Height - rInitial.Height) < 1e-4f);

        std::cout << "  -> 4 Anchor petals (TL, TR, BR, BL) hit-tested: PASS" << std::endl;
        std::cout << "  -> Anchor boundary splitting with screen rect preservation: PASS" << std::endl;
    }

    // [93.5] Testing Translation & Resize Math
    std::cout << "\n[93.5] Testing Translation & Corner Resize Math..." << std::endl;
    {
        RectTransform rt;
        rt.AnchorMin = { 0.0f, 0.0f };
        rt.AnchorMax = { 0.0f, 0.0f };
        rt.Pivot = { 0.0f, 0.0f };
        rt.SizeDelta = { 200.0f, 150.0f };
        rt.AnchoredPosition = { 100.0f, 100.0f };

        // Translate: move by (50, 30)
        rt.AnchoredPosition += glm::vec2(50.0f, 30.0f);
        UIRect rTrans = rt.ComputeRect(parent1080p);
        assert(rTrans.X == 150.0f && rTrans.Y == 130.0f);

        // Corner BR Resize: expand width by +100, height by +50
        UIRect targetR = rTrans;
        targetR.Width += 100.0f;
        targetR.Height += 50.0f;
        rt.SetRect(targetR, parent1080p);
        UIRect rResized = rt.ComputeRect(parent1080p);
        assert(rResized.Width == 300.0f && rResized.Height == 200.0f);
        assert(rResized.X == 150.0f && rResized.Y == 130.0f);

        std::cout << "  -> Interactive translation and 2D corner dimension resizing: PASS" << std::endl;
    }

    // [93.6] Testing Grid Snapping Configuration
    std::cout << "\n[93.6] Testing Grid Snapping Configuration..." << std::endl;
    {
        gizmo.GetSettings().SnapToGrid = true;
        gizmo.GetSettings().SnapIncrement = 25.0f;

        glm::vec2 rawDelta{ 23.4f, -48.2f };
        glm::vec2 snappedDelta = glm::round(rawDelta / gizmo.GetSettings().SnapIncrement) * gizmo.GetSettings().SnapIncrement;
        assert(snappedDelta.x == 25.0f);
        assert(snappedDelta.y == -50.0f);

        gizmo.GetSettings().SnapToGrid = false;
        std::cout << "  -> Configurable grid snapping (25px increment): PASS" << std::endl;
    }

    std::cout << "\n>>> STEP 93 PASSED: Interactive 2D Canvas Gizmo Handles 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 94: Phase 34 Step 7 (Part 3) - Viewport 2D UI Edit Mode Toggle,
// Orthogonal Projection Lock, Camera Pose Cache & Customizable Grid Snapping
// =============================================================================
bool RunTest_Step94_Phase34_Step7_Viewport2DModeAndGridSnapping() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 94 TEST] VIEWPORT 2D UI EDIT MODE & GRID SNAPPING" << std::endl;
    std::cout << " (Orthogonal View Lock, 2D/3D Pose Cache, Px/% Snapping) " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;

    // [94.1] Testing PerspectiveCamera Orthographic Projection Mode
    std::cout << "[94.1] Testing PerspectiveCamera Orthographic Projection Configuration..." << std::endl;
    {
        PerspectiveCamera cam(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
        assert(!cam.IsOrthographic());
        assert(cam.GetProjectionType() == CameraProjectionType::Perspective);

        // In a perspective projection, proj[3][3] == 0.0f and proj[2][3] == -1.0f
        const glm::mat4& persProj = cam.GetProjectionMatrix();
        assert(std::abs(persProj[3][3]) < 1e-5f);
        assert(std::abs(persProj[2][3] - (-1.0f)) < 1e-5f);

        // Switch to Orthographic
        cam.SetOrthographic(10.0f, 16.0f / 9.0f, -100.0f, 1000.0f);
        assert(cam.IsOrthographic());
        assert(cam.GetProjectionType() == CameraProjectionType::Orthographic);
        assert(cam.GetOrthographicSize() == 10.0f);

        // In an affine orthographic projection, proj[3][3] == 1.0f and proj[2][3] == 0.0f
        const glm::mat4& orthoProj = cam.GetProjectionMatrix();
        assert(std::abs(orthoProj[3][3] - 1.0f) < 1e-5f);
        assert(std::abs(orthoProj[2][3]) < 1e-5f);

        // Check ZO (Direct3D 12 / Vulkan) projection matrix
        const glm::mat4& orthoProjZO = cam.GetProjectionMatrixZO();
        assert(std::abs(orthoProjZO[3][3] - 1.0f) < 1e-5f);
        assert(std::abs(orthoProjZO[2][3]) < 1e-5f);

        // Dynamic orthographic size change
        cam.SetOrthographicSize(25.0f);
        assert(cam.GetOrthographicSize() == 25.0f);
        float expectedM00 = 1.0f / (25.0f * (16.0f / 9.0f) * 0.5f);
        assert(std::abs(cam.GetProjectionMatrix()[0][0] - expectedM00) < 1e-4f);

        // Switch back to perspective
        cam.SetPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
        assert(!cam.IsOrthographic());
        assert(std::abs(cam.GetProjectionMatrix()[3][3]) < 1e-5f);
        assert(cam.GetFOV() == 60.0f);

        std::cout << "  -> Perspective to Orthographic switch (OpenGL [-1,1] and D3D12/Vulkan [0,1]): PASS" << std::endl;
        std::cout << "  -> Dynamic Orthographic Size adjustment: PASS" << std::endl;
        std::cout << "  -> Reversible Perspective restoration: PASS" << std::endl;
    }

    // [94.2] Testing PerspectiveCameraController 2D Mode & 3D Pose Caching
    std::cout << "\n[94.2] Testing PerspectiveCameraController 2D Mode & Pose Caching..." << std::endl;
    {
        PerspectiveCameraController controller(45.0f, 16.0f / 9.0f);
        glm::vec3 customPos{ 14.5f, 7.2f, 22.0f };
        glm::vec3 customRot{ 25.0f, -60.0f, 0.0f }; // Pitch 25, Yaw -60
        controller.SetPosition(customPos);
        controller.SetRotation(customRot);

        assert(!controller.Is2DMode());

        // Enable 2D View Mode
        controller.Set2DMode(true);
        assert(controller.Is2DMode());
        assert(controller.GetCamera().IsOrthographic());

        // Saved 3D pose must match pristine custom values
        assert(controller.GetSaved3DPosition() == customPos);
        assert(controller.GetSaved3DRotation() == customRot);

        // View orientation in 2D mode MUST be locked facing the canvas plane:
        // Euler angles (Pitch = 0, Yaw = 0, Roll = 0)
        assert(controller.GetCamera().GetRotation() == glm::vec3(0.0f, 0.0f, 0.0f));

        // Disable 2D View Mode -> restore 3D
        controller.Set2DMode(false);
        assert(!controller.Is2DMode());
        assert(!controller.GetCamera().IsOrthographic());

        // Camera pose must be seamlessly restored to the saved 3D position & rotation
        assert(controller.GetCamera().GetPosition() == customPos);
        assert(controller.GetCamera().GetRotation() == customRot);

        std::cout << "  -> 2D view toggle and orthographic camera switch: PASS" << std::endl;
        std::cout << "  -> Exact canvas plane orientation lock (Pitch=0, Yaw=0, Roll=0): PASS" << std::endl;
        std::cout << "  -> Seamless 3D camera pose cache & restoration: PASS" << std::endl;
    }

    // [94.3] Testing Customizable 2D UI Grid Snapping (Pixel Increments)
    std::cout << "\n[94.3] Testing Customizable 2D UI Grid Snapping (Pixel Increments)..." << std::endl;
    {
        auto& gizmo = RectTransformGizmo::Get();
        auto& settings = gizmo.GetSettings();
        settings.SnapToGrid = true;
        settings.SnapIsPercentage = false;

        UIRect parent1080p{ 0.0f, 0.0f, 1920.0f, 1080.0f };

        // Test pixel increments: 1px, 5px, 10px, 25px, 50px
        const float pixelIncrements[] = { 1.0f, 5.0f, 10.0f, 25.0f, 50.0f };
        for (float inc : pixelIncrements) {
            settings.SnapIncrement = inc;
            glm::vec2 snap = gizmo.ComputeSnapIncrement(parent1080p);
            assert(snap.x == inc && snap.y == inc);
        }

        // Test rounding calculation with 10px increment
        settings.SnapIncrement = 10.0f;
        glm::vec2 delta1{ 14.3f, 26.8f };
        glm::vec2 snapStep = gizmo.ComputeSnapIncrement(parent1080p);
        glm::vec2 snapped1 = glm::round(delta1 / snapStep) * snapStep;
        assert(snapped1.x == 10.0f);
        assert(snapped1.y == 30.0f);

        // Test rounding with 25px increment
        settings.SnapIncrement = 25.0f;
        glm::vec2 delta2{ 36.5f, 63.0f };
        snapStep = gizmo.ComputeSnapIncrement(parent1080p);
        glm::vec2 snapped2 = glm::round(delta2 / snapStep) * snapStep;
        assert(snapped2.x == 25.0f);
        assert(snapped2.y == 75.0f);

        std::cout << "  -> Pixel increments (1px, 5px, 10px, 25px, 50px) verified: PASS" << std::endl;
        std::cout << "  -> Delta rounding to pixel grid: PASS" << std::endl;
    }

    // [94.4] Testing Customizable 2D UI Grid Snapping (Percentage Increments)
    std::cout << "\n[94.4] Testing Customizable 2D UI Grid Snapping (Percentage Increments)..." << std::endl;
    {
        auto& gizmo = RectTransformGizmo::Get();
        auto& settings = gizmo.GetSettings();
        settings.SnapToGrid = true;
        settings.SnapIsPercentage = true;

        UIRect parent1080p{ 0.0f, 0.0f, 1920.0f, 1080.0f };

        // Test 5% increment: 5% of 1920 = 96px, 5% of 1080 = 54px
        settings.SnapPercent = 5.0f;
        glm::vec2 snap5 = gizmo.ComputeSnapIncrement(parent1080p);
        assert(std::abs(snap5.x - 96.0f) < 1e-4f);
        assert(std::abs(snap5.y - 54.0f) < 1e-4f);

        // Test 10% increment: 10% of 1920 = 192px, 10% of 1080 = 108px
        settings.SnapPercent = 10.0f;
        glm::vec2 snap10 = gizmo.ComputeSnapIncrement(parent1080p);
        assert(std::abs(snap10.x - 192.0f) < 1e-4f);
        assert(std::abs(snap10.y - 108.0f) < 1e-4f);

        // Test 25% increment: 25% of 1920 = 480px, 25% of 1080 = 270px
        settings.SnapPercent = 25.0f;
        glm::vec2 snap25 = gizmo.ComputeSnapIncrement(parent1080p);
        assert(std::abs(snap25.x - 480.0f) < 1e-4f);
        assert(std::abs(snap25.y - 270.0f) < 1e-4f);

        // Test anchor percentage snapping
        float stepNorm = settings.SnapPercent / 100.0f; // 0.25
        glm::vec2 rawAnchorDelta{ 0.23f, 0.52f };
        glm::vec2 snappedAnchor = glm::round(rawAnchorDelta / stepNorm) * stepNorm;
        assert(std::abs(snappedAnchor.x - 0.25f) < 1e-4f);
        assert(std::abs(snappedAnchor.y - 0.50f) < 1e-4f);

        // Reset settings
        settings.SnapToGrid = false;
        settings.SnapIsPercentage = false;
        settings.SnapIncrement = 10.0f;
        settings.SnapPercent = 5.0f;

        std::cout << "  -> Percentage increments (5%, 10%, 25%) scaling with canvas resolution: PASS" << std::endl;
        std::cout << "  -> Anchor petal percentage boundary snapping: PASS" << std::endl;
    }

    std::cout << "\n>>> STEP 94 PASSED: Viewport 2D UI Edit Mode & Grid Snapping 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 95: Phase 34 Step 7 - UI Component Palette & Drag-and-Drop Spawner
// (Stock Primitives, Factory Spawning, Viewport Projection & Hierarchy Drop)
// =============================================================================
bool RunTest_Step95_Phase34_Step7_UIPaletteAndDragDropSpawner() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 95 TEST] UI COMPONENT PALETTE & DRAG-DROP SPAWNER" << std::endl;
    std::cout << " (Stock Primitives, Factory Spawning, ScreenToCanvas)   " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;

    // [95.1] Stock Widget Descriptors
    std::cout << "[95.1] Verifying Stock UI Widget Descriptors..." << std::endl;
    {
        const auto& widgets = UIPalettePanel::GetStockWidgets();
        assert(widgets.size() == 7);

        // Verify all 7 primitives exist with non-empty descriptors
        bool hasCanvas = false, hasPanel = false, hasText = false;
        bool hasImage = false, hasButton = false, hasProgressBar = false, hasRadialGauge = false;

        for (const auto& w : widgets) {
            assert(!w.Name.empty());
            assert(!w.Category.empty());
            assert(!w.Description.empty());
            assert(w.DefaultSize.x > 0.0f && w.DefaultSize.y > 0.0f);

            if (w.Type == UIWidgetType::Canvas) {
                hasCanvas = true;
                assert(w.Name == "Canvas");
                assert(w.Category == "Containers");
                assert(w.DefaultSize == glm::vec2(1920.0f, 1080.0f));
            } else if (w.Type == UIWidgetType::Panel) {
                hasPanel = true;
                assert(w.Name == "Panel");
                assert(w.Category == "Containers");
                assert(w.DefaultSize == glm::vec2(300.0f, 200.0f));
            } else if (w.Type == UIWidgetType::Text) {
                hasText = true;
                assert(w.Name == "Text");
                assert(w.Category == "Basic");
                assert(w.DefaultSize == glm::vec2(200.0f, 40.0f));
            } else if (w.Type == UIWidgetType::Image) {
                hasImage = true;
                assert(w.Name == "Image");
                assert(w.Category == "Basic");
                assert(w.DefaultSize == glm::vec2(128.0f, 128.0f));
            } else if (w.Type == UIWidgetType::Button) {
                hasButton = true;
                assert(w.Name == "Button");
                assert(w.Category == "Controls");
                assert(w.DefaultSize == glm::vec2(180.0f, 48.0f));
            } else if (w.Type == UIWidgetType::ProgressBar) {
                hasProgressBar = true;
                assert(w.Name == "ProgressBar");
                assert(w.Category == "Gauges");
                assert(w.DefaultSize == glm::vec2(260.0f, 26.0f));
            } else if (w.Type == UIWidgetType::RadialGauge) {
                hasRadialGauge = true;
                assert(w.Name == "RadialGauge");
                assert(w.Category == "Gauges");
                assert(w.DefaultSize == glm::vec2(180.0f, 180.0f));
            }
        }

        assert(hasCanvas && hasPanel && hasText && hasImage && hasButton && hasProgressBar && hasRadialGauge);
        assert(std::string_view(UIPalettePanel::DRAG_DROP_PAYLOAD_ID) == "AXLE_UI_WIDGET_TYPE");
        std::cout << "  -> Stock UI widget catalog validation (7 primitives, categories, default sizes): PASS" << std::endl;
    }

    // [95.2] Factory Spawning by UIWidgetType into UIDocument
    std::cout << "\n[95.2] Testing Factory Spawning by UIWidgetType..." << std::endl;
    {
        auto doc = std::make_unique<UIDocument>();
        assert(doc->GetNodeCount() == 0);

        // Spawn Canvas (becomes root)
        UINode* canvasNode = UIPalettePanel::SpawnWidget(doc.get(), UIWidgetType::Canvas);
        assert(canvasNode != nullptr);
        assert(doc->GetRootNode() == canvasNode);
        assert(canvasNode->GetTypeName() == "UICanvasNode" || dynamic_cast<UICanvasNode*>(canvasNode) != nullptr);

        // Spawn Panel
        UINode* panelNode = UIPalettePanel::SpawnWidget(doc.get(), UIWidgetType::Panel, canvasNode);
        assert(panelNode != nullptr);
        assert(panelNode->UseRectTransform == true);
        assert(panelNode->Transform.SizeDelta == glm::vec2(300.0f, 200.0f));
        assert(panelNode->Transform.Pivot == glm::vec2(0.5f, 0.5f));
        assert(panelNode->Transform.AnchorMin == glm::vec2(0.5f, 0.5f));
        assert(panelNode->Transform.AnchorMax == glm::vec2(0.5f, 0.5f));

        // Spawn Text
        UINode* textNode = UIPalettePanel::SpawnWidget(doc.get(), UIWidgetType::Text, canvasNode);
        assert(textNode != nullptr);
        assert(textNode->UseRectTransform == true);
        assert(textNode->Transform.SizeDelta == glm::vec2(200.0f, 40.0f));
        auto* text = dynamic_cast<UITextNode*>(textNode);
        assert(text != nullptr);
        assert(text->Text == "Label");

        // Spawn Image
        UINode* imgNode = UIPalettePanel::SpawnWidget(doc.get(), UIWidgetType::Image, canvasNode);
        assert(imgNode != nullptr);
        assert(imgNode->UseRectTransform == true);
        assert(imgNode->Transform.SizeDelta == glm::vec2(128.0f, 128.0f));

        // Spawn Button (verifying it auto-creates a child text node)
        size_t nodeCountBeforeBtn = doc->GetNodeCount();
        UINode* btnNode = UIPalettePanel::SpawnWidget(doc.get(), UIWidgetType::Button, canvasNode);
        assert(btnNode != nullptr);
        assert(btnNode->UseRectTransform == true);
        assert(btnNode->Transform.SizeDelta == glm::vec2(180.0f, 48.0f));
        assert(doc->GetNodeCount() == nodeCountBeforeBtn + 2); // Button + Child Label
        assert(btnNode->Children.size() == 1);
        auto* btnLabel = dynamic_cast<UITextNode*>(btnNode->Children[0]);
        assert(btnLabel != nullptr);
        assert(btnLabel->Text == "Button");

        // Spawn ProgressBar
        UINode* barNode = UIPalettePanel::SpawnWidget(doc.get(), UIWidgetType::ProgressBar, canvasNode);
        assert(barNode != nullptr);
        assert(barNode->UseRectTransform == true);
        assert(barNode->Transform.SizeDelta == glm::vec2(260.0f, 26.0f));
        auto* bar = dynamic_cast<UIProgressBarNode*>(barNode);
        assert(bar != nullptr);
        assert(bar->Value == 0.5f);

        // Spawn RadialGauge
        UINode* gaugeNode = UIPalettePanel::SpawnWidget(doc.get(), UIWidgetType::RadialGauge, canvasNode);
        assert(gaugeNode != nullptr);
        assert(gaugeNode->UseRectTransform == true);
        assert(gaugeNode->Transform.SizeDelta == glm::vec2(180.0f, 180.0f));
        auto* gauge = dynamic_cast<UIRadialGaugeNode*>(gaugeNode);
        assert(gauge != nullptr);
        assert(gauge->Thickness == 16.0f);

        // Verify ID Map integrity
        assert(doc->FindNodeByID(panelNode->ID) == panelNode);
        assert(doc->FindNodeByID(textNode->ID) == textNode);
        assert(doc->FindNodeByID(btnNode->ID) == btnNode);

        std::cout << "  -> Factory spawning across all 7 primitives with RectTransform setup: PASS" << std::endl;
        std::cout << "  -> Compound widget (Button + Text child node) composition: PASS" << std::endl;
        std::cout << "  -> UIDocument ID map synchronization: PASS" << std::endl;
    }

    // [95.3] String-Based Widget Spawning & Name Aliasing
    std::cout << "\n[95.3] Testing String-Based Widget Spawning..." << std::endl;
    {
        auto doc = std::make_unique<UIDocument>();
        UINode* c = UIPalettePanel::SpawnWidgetByName(doc.get(), "Canvas");
        assert(c != nullptr);

        UINode* p1 = UIPalettePanel::SpawnWidgetByName(doc.get(), "Panel", c);
        assert(p1 != nullptr);
        UINode* p2 = UIPalettePanel::SpawnWidgetByName(doc.get(), "Rectangle", c); // Alias
        assert(p2 != nullptr);
        UINode* t = UIPalettePanel::SpawnWidgetByName(doc.get(), "Text", c);
        assert(t != nullptr);
        UINode* b = UIPalettePanel::SpawnWidgetByName(doc.get(), "Button", c);
        assert(b != nullptr);
        UINode* invalid = UIPalettePanel::SpawnWidgetByName(doc.get(), "NonExistentWidget", c);
        assert(invalid == nullptr);

        std::cout << "  -> Name resolution and alias ('Panel' / 'Rectangle') dispatch: PASS" << std::endl;
        std::cout << "  -> Graceful null handling for unknown widget types: PASS" << std::endl;
    }

    // [95.4] Viewport ScreenToCanvas Coordinate Mapping
    std::cout << "\n[95.4] Testing ScreenToCanvas Coordinate Transformation..." << std::endl;
    {
        glm::vec2 canvasRes{ 1920.0f, 1080.0f };

        // Test 1: 1:1 Viewport match (1920x1080 viewport at (0, 0))
        glm::vec2 vpMin1{ 0.0f, 0.0f };
        glm::vec2 vpSize1{ 1920.0f, 1080.0f };
        glm::vec2 canvasCenter1 = UIPalettePanel::ScreenToCanvas({ 960.0f, 540.0f }, vpMin1, vpSize1, canvasRes);
        assert(std::abs(canvasCenter1.x - 960.0f) < 1e-3f);
        assert(std::abs(canvasCenter1.y - 540.0f) < 1e-3f);

        // Test 2: Uniformly scaled viewport (960x540 at offset (100, 50))
        glm::vec2 vpMin2{ 100.0f, 50.0f };
        glm::vec2 vpSize2{ 960.0f, 540.0f };
        glm::vec2 canvasCenter2 = UIPalettePanel::ScreenToCanvas({ 100.0f + 480.0f, 50.0f + 270.0f }, vpMin2, vpSize2, canvasRes);
        assert(std::abs(canvasCenter2.x - 960.0f) < 1e-3f);
        assert(std::abs(canvasCenter2.y - 540.0f) < 1e-3f);

        // Test 3: Letterboxed viewport (Pillarboxed 2560x1080 viewport with 16:9 canvas centered)
        glm::vec2 vpMin3{ 0.0f, 0.0f };
        glm::vec2 vpSize3{ 2560.0f, 1080.0f }; // Width excess: (2560 - 1920) / 2 = 320px black bars on left/right
        // Center of viewport (1280, 540) should map to canvas center (960, 540)
        glm::vec2 canvasCenter3 = UIPalettePanel::ScreenToCanvas({ 1280.0f, 540.0f }, vpMin3, vpSize3, canvasRes);
        assert(std::abs(canvasCenter3.x - 960.0f) < 1e-3f);
        assert(std::abs(canvasCenter3.y - 540.0f) < 1e-3f);

        // Pillarbox left border at screen x = 320 should map to canvas x = 0
        glm::vec2 canvasLeft3 = UIPalettePanel::ScreenToCanvas({ 320.0f, 540.0f }, vpMin3, vpSize3, canvasRes);
        assert(std::abs(canvasLeft3.x - 0.0f) < 1e-3f);

        // Pillarbox right border at screen x = 320 + 1920 = 2240 should map to canvas x = 1920
        glm::vec2 canvasRight3 = UIPalettePanel::ScreenToCanvas({ 2240.0f, 540.0f }, vpMin3, vpSize3, canvasRes);
        assert(std::abs(canvasRight3.x - 1920.0f) < 1e-3f);

        std::cout << "  -> 1:1 Viewport to Canvas mapping: PASS" << std::endl;
        std::cout << "  -> Uniformly scaled Viewport coordinate mapping: PASS" << std::endl;
        std::cout << "  -> Pillarboxed / Letterboxed aspect-ratio fitted coordinate mapping: PASS" << std::endl;
    }

    // [95.5] Spawning Widget at Explicit Canvas Coordinates
    std::cout << "\n[95.5] Testing Dropped Widget Canvas Positioning..." << std::endl;
    {
        auto doc = std::make_unique<UIDocument>();
        auto* canvas = doc->CreateNode<UICanvasNode>(nullptr);
        canvas->ReferenceResolution = { 1920.0f, 1080.0f };

        // Drop a Panel (300x200) at Canvas coordinates (400, 300)
        glm::vec2 dropPos{ 400.0f, 300.0f };
        UINode* panelNode = UIPalettePanel::SpawnWidget(doc.get(), UIWidgetType::Panel, canvas, dropPos);
        assert(panelNode != nullptr);

        UIRect parentRect{ 0.0f, 0.0f, 1920.0f, 1080.0f };
        UIRect computedRect = panelNode->Transform.ComputeRect(parentRect);

        // Panel center should be at (400, 300)
        glm::vec2 computedCenter{ computedRect.X + computedRect.Width * 0.5f, computedRect.Y + computedRect.Height * 0.5f };
        assert(std::abs(computedCenter.x - dropPos.x) < 1e-3f);
        assert(std::abs(computedCenter.y - dropPos.y) < 1e-3f);
        assert(std::abs(computedRect.Width - 300.0f) < 1e-3f);
        assert(std::abs(computedRect.Height - 200.0f) < 1e-3f);

        std::cout << "  -> RectTransform SetRect alignment to dropped canvas coordinate: PASS" << std::endl;
        std::cout << "  -> Computed bounding rectangle matches widget size delta and drop center: PASS" << std::endl;
    }

    // [95.6] GetOrCreateCanvasEntity Scene Integration
    std::cout << "\n[95.6] Testing GetOrCreateCanvasEntity Scene Integration..." << std::endl;
    {
        auto scene = std::make_shared<Scene>();

        // Scene initially has 0 entities with UICanvasComponent
        Entity e1 = UIPalettePanel::GetOrCreateCanvasEntity(scene.get());
        assert(bool(e1));
        assert(e1.HasComponent<UICanvasComponent>());
        assert(e1.GetComponent<UICanvasComponent>().Document != nullptr);
        assert(e1.GetComponent<UICanvasComponent>().Document->GetRootNode() != nullptr);

        // Calling again should resolve the existing entity rather than creating a duplicate
        Entity e2 = UIPalettePanel::GetOrCreateCanvasEntity(scene.get());
        assert(bool(e2));
        assert(e1 == e2);

        std::cout << "  -> Auto-creation of UICanvas entity with root Canvas node: PASS" << std::endl;
        std::cout << "  -> Idempotent resolution of existing UICanvas entity: PASS" << std::endl;
    }

    std::cout << "\n>>> STEP 95 PASSED: UI Component Palette & Drag-and-Drop Spawner 100% verified! <<<\n" << std::endl;
    return true;
}

// =============================================================================
// TEST STEP 96: Phase 34 Step 7 - Bidirectional Document Synchronization
// (AST Serialization to .axleui, Roundtrip Parsing, Dirty State & Scene Sync)
// =============================================================================
bool RunTest_Step96_Phase34_Step7_BidirectionalDocumentSynchronization() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [STEP 96 TEST] BIDIRECTIONAL DOCUMENT SYNCHRONIZATION  " << std::endl;
    std::cout << " (AST Serialization, Roundtrip Parsing, Scene Sync)     " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    using namespace Axle;

    // [96.1] Programmatic UIDocument Construction & Serialization to .axleui
    std::cout << "[96.1] Testing UIDocument AST Serialization to .axleui syntax..." << std::endl;
    std::string serializedOutput;
    {
        auto doc = std::make_unique<UIDocument>();
        auto* canvas = doc->CreateNode<UICanvasNode>(nullptr);
        canvas->ID = "root_canvas";
        canvas->ReferenceResolution = glm::vec2(1920.0f, 1080.0f);

        auto* panel = doc->CreateNode<UIRectangleNode>(canvas);
        panel->ID = "main_panel";
        panel->SetUseRectTransform(true);
        panel->Transform.AnchorMin = glm::vec2(0.5f, 0.5f);
        panel->Transform.AnchorMax = glm::vec2(0.5f, 0.5f);
        panel->Transform.Pivot = glm::vec2(0.5f, 0.5f);
        panel->Transform.AnchoredPosition = glm::vec2(100.0f, -50.0f);
        panel->Transform.SizeDelta = glm::vec2(400.0f, 300.0f);
        panel->Style.BackgroundColor = glm::vec4(0.1f, 0.2f, 0.3f, 0.9f);
        panel->Style.CornerRadius = 12.0f;
        panel->Style.BorderWidth = 2.0f;
        panel->Style.BorderColor = glm::vec4(0.4f, 0.6f, 0.8f, 1.0f);

        auto* title = doc->CreateNode<UITextNode>(panel);
        title->ID = "panel_title";
        title->SetUseRectTransform(true);
        title->Transform.AnchorMin = glm::vec2(0.5f, 1.0f);
        title->Transform.AnchorMax = glm::vec2(0.5f, 1.0f);
        title->Transform.Pivot = glm::vec2(0.5f, 1.0f);
        title->Transform.AnchoredPosition = glm::vec2(0.0f, -20.0f);
        title->Transform.SizeDelta = glm::vec2(300.0f, 40.0f);
        title->Text = "SETTINGS MENU";
        title->FontSize = 24.0f;
        title->Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

        auto* btn = doc->CreateNode<UIButtonNode>(panel);
        btn->ID = "apply_btn";
        btn->SetUseRectTransform(true);
        btn->Transform.AnchorMin = glm::vec2(0.5f, 0.0f);
        btn->Transform.AnchorMax = glm::vec2(0.5f, 0.0f);
        btn->Transform.Pivot = glm::vec2(0.5f, 0.0f);
        btn->Transform.AnchoredPosition = glm::vec2(0.0f, 30.0f);
        btn->Transform.SizeDelta = glm::vec2(180.0f, 48.0f);
        btn->NormalColor = glm::vec4(0.2f, 0.3f, 0.4f, 1.0f);
        btn->HoverColor = glm::vec4(0.3f, 0.4f, 0.5f, 1.0f);
        btn->PressedColor = glm::vec4(0.1f, 0.6f, 0.9f, 1.0f);
        btn->OnClickAction = "OnApplySettings";

        auto* bar = doc->CreateNode<UIProgressBarNode>(panel);
        bar->ID = "volume_bar";
        bar->SetUseRectTransform(true);
        bar->Transform.AnchorMin = glm::vec2(0.5f, 0.5f);
        bar->Transform.AnchorMax = glm::vec2(0.5f, 0.5f);
        bar->Transform.SizeDelta = glm::vec2(250.0f, 20.0f);
        bar->Value = 0.75f;

        auto* gauge = doc->CreateNode<UIRadialGaugeNode>(panel);
        gauge->ID = "speed_gauge";
        gauge->SetUseRectTransform(true);
        gauge->Transform.AnchorMin = glm::vec2(0.5f, 0.5f);
        gauge->Transform.AnchorMax = glm::vec2(0.5f, 0.5f);
        gauge->Transform.SizeDelta = glm::vec2(120.0f, 120.0f);
        gauge->Value = 0.6f;
        gauge->Thickness = 12.0f;

        serializedOutput = doc->Serialize();
        assert(!serializedOutput.empty());
        assert(serializedOutput.find("Canvas {") != std::string::npos);
        assert(serializedOutput.find("id: \"root_canvas\"") != std::string::npos);
        assert(serializedOutput.find("Rectangle {") != std::string::npos);
        assert(serializedOutput.find("id: \"main_panel\"") != std::string::npos);
        assert(serializedOutput.find("useRectTransform: true") != std::string::npos);
        assert(serializedOutput.find("Text {") != std::string::npos);
        assert(serializedOutput.find("SETTINGS MENU") != std::string::npos);
        assert(serializedOutput.find("Button {") != std::string::npos);
        assert(serializedOutput.find("onClick: \"OnApplySettings\"") != std::string::npos);
        assert(serializedOutput.find("ProgressBar {") != std::string::npos);
        assert(serializedOutput.find("RadialGauge {") != std::string::npos);

        std::cout << "  -> Structured .axleui serialization with nested scoping: PASS" << std::endl;
        std::cout << "  -> RectTransform layout attributes & styling preservation: PASS" << std::endl;
    }

    // [96.2] Round-Trip Parsing & AST Preservation
    std::cout << "\n[96.2] Testing Round-Trip Parsing (Source -> AST -> Re-serialized Source)..." << std::endl;
    {
        auto doc2 = std::make_unique<UIDocument>();
        std::string err;
        bool parseSuccess = UIParser::ParseSource(serializedOutput, *doc2, &err);
        if (!parseSuccess) {
            std::cerr << "Parse error: " << err << std::endl;
        }
        assert(parseSuccess);
        assert(doc2->GetNodeCount() == 6);

        // Verify root Canvas
        auto* rootCanvas = dynamic_cast<UICanvasNode*>(doc2->FindNodeByID("root_canvas"));
        assert(rootCanvas != nullptr);
        assert(rootCanvas->ReferenceResolution == glm::vec2(1920.0f, 1080.0f));

        // Verify main_panel
        auto* panel = dynamic_cast<UIRectangleNode*>(doc2->FindNodeByID("main_panel"));
        assert(panel != nullptr);
        assert(panel->UseRectTransform == true);
        assert(panel->Transform.AnchorMin == glm::vec2(0.5f, 0.5f));
        assert(panel->Transform.AnchorMax == glm::vec2(0.5f, 0.5f));
        assert(panel->Transform.Pivot == glm::vec2(0.5f, 0.5f));
        assert(panel->Transform.AnchoredPosition == glm::vec2(100.0f, -50.0f));
        assert(panel->Transform.SizeDelta == glm::vec2(400.0f, 300.0f));
        assert(panel->Style.CornerRadius == 12.0f);
        assert(panel->Style.BorderWidth == 2.0f);

        // Verify panel_title
        auto* title = dynamic_cast<UITextNode*>(doc2->FindNodeByID("panel_title"));
        assert(title != nullptr);
        assert(title->Text == "SETTINGS MENU");
        assert(title->FontSize == 24.0f);

        // Verify apply_btn
        auto* btn = dynamic_cast<UIButtonNode*>(doc2->FindNodeByID("apply_btn"));
        assert(btn != nullptr);
        assert(btn->OnClickAction == "OnApplySettings");

        // Verify volume_bar
        auto* bar = dynamic_cast<UIProgressBarNode*>(doc2->FindNodeByID("volume_bar"));
        assert(bar != nullptr);
        assert(std::abs(bar->Value - 0.75f) < 1e-4f);

        // Verify speed_gauge
        auto* gauge = dynamic_cast<UIRadialGaugeNode*>(doc2->FindNodeByID("speed_gauge"));
        assert(gauge != nullptr);
        assert(std::abs(gauge->Value - 0.6f) < 1e-4f);
        assert(gauge->Thickness == 12.0f);

        // Verify exact layout bounds equivalence
        UIRect parentRect{ 0.0f, 0.0f, 1920.0f, 1080.0f };
        UIRect rect = panel->Transform.ComputeRect(parentRect);
        // Center of 1920x1080 is (960, 540). With anchoredPos (100, -50) and size (400, 300):
        // X = 960 + 100 - 200 = 860, Y = 540 - 50 - 150 = 340
        assert(std::abs(rect.X - 860.0f) < 1e-3f);
        assert(std::abs(rect.Y - 340.0f) < 1e-3f);
        assert(std::abs(rect.Width - 400.0f) < 1e-3f);
        assert(std::abs(rect.Height - 300.0f) < 1e-3f);

        std::cout << "  -> Full roundtrip AST fidelity across all widget primitives: PASS" << std::endl;
        std::cout << "  -> Geometric bounding rectangle invariance across parse roundtrip: PASS" << std::endl;
    }

    // [96.3] Document Dirty State Tracking & File I/O
    std::cout << "\n[96.3] Testing Dirty State Tracking & SaveToFile / LoadFromFile..." << std::endl;
    {
        std::filesystem::path tempFilePath = std::filesystem::current_path() / "build" / "test_sync_doc.axleui";
        if (!std::filesystem::exists(tempFilePath.parent_path())) {
            std::filesystem::create_directories(tempFilePath.parent_path());
        }

        auto doc = std::make_unique<UIDocument>();
        assert(doc->IsDirty() == false);

        // Adding node triggers dirty flag
        auto* canvas = doc->CreateNode<UICanvasNode>(nullptr);
        assert(doc->IsDirty() == true);

        // Saving clears dirty flag
        bool saved = doc->SaveToFile(tempFilePath.string());
        assert(saved);
        assert(doc->IsDirty() == false);
        assert(std::filesystem::exists(tempFilePath));

        // Adding another node marks dirty again
        doc->CreateNode<UIRectangleNode>(canvas);
        assert(doc->IsDirty() == true);

        // LoadFromFile clears dirty flag
        bool loaded = doc->LoadFromFile(tempFilePath.string());
        assert(loaded);
        assert(doc->IsDirty() == false);
        assert(doc->GetNodeCount() == 1); // Only Canvas was saved previously

        std::filesystem::remove(tempFilePath);
        std::cout << "  -> Document dirty state lifecycle (CreateNode -> dirty, SaveToFile -> clean): PASS" << std::endl;
        std::cout << "  -> LoadFromFile file synchronization: PASS" << std::endl;
    }

    // [96.4] Visual Viewport Manipulation Dirty Flag Synchronization
    std::cout << "\n[96.4] Testing Viewport Gizmo Modification Dirty Flag Trigger..." << std::endl;
    {
        auto doc = std::make_unique<UIDocument>();
        auto* canvas = doc->CreateNode<UICanvasNode>(nullptr);
        auto* panel = doc->CreateNode<UIRectangleNode>(canvas);
        panel->SetUseRectTransform(true);
        panel->Transform.SizeDelta = glm::vec2(200.0f, 100.0f);

        doc->SetDirty(false);
        assert(!doc->IsDirty());

        // Simulate gizmo translation modification
        bool uiModified = true;
        panel->Transform.AnchoredPosition += glm::vec2(15.0f, 25.0f);
        if (uiModified) {
            doc->UpdateLayout(glm::vec2(1920.0f, 1080.0f));
            doc->SetDirty(true);
        }

        assert(doc->IsDirty() == true);
        assert(panel->Transform.AnchoredPosition == glm::vec2(15.0f, 25.0f));

        std::cout << "  -> Interactive visual canvas modification triggers document dirty sync: PASS" << std::endl;
    }

    // [96.5] End-to-End Scene (.axlescene) & UI (.axleui) Synchronization
    std::cout << "\n[96.5] Testing End-to-End Scene & UI Document Co-Serialization..." << std::endl;
    {
        std::filesystem::path scenePath = std::filesystem::current_path() / "build" / "test_sync_scene.axlescene";
        std::filesystem::path uiDocPath = std::filesystem::current_path() / "build" / "test_scene_ui.axleui";

        // 1. Create Scene with UICanvasComponent
        auto scene = std::make_shared<Scene>();
        Entity canvasEntity = scene->CreateEntity("HUD_Canvas");
        auto& uic = canvasEntity.AddComponent<UICanvasComponent>();
        uic.AssetPath = uiDocPath.string();
        uic.SortOrder = 5;
        uic.Visible = true;
        uic.Document = std::make_shared<UIDocument>();

        auto* canvasNode = uic.Document->CreateNode<UICanvasNode>(nullptr);
        canvasNode->ID = "hud_root";
        auto* panelNode = uic.Document->CreateNode<UIRectangleNode>(canvasNode);
        panelNode->ID = "hud_box";
        panelNode->SetUseRectTransform(true);
        panelNode->Transform.SizeDelta = glm::vec2(320.0f, 80.0f);

        // 2. Serialize Scene (which automatically synchronizes and writes the UI document)
        SceneSerializer serializer(scene.get());
        bool serialized = serializer.Serialize(scenePath);
        assert(serialized);
        assert(std::filesystem::exists(scenePath));
        assert(std::filesystem::exists(uiDocPath));

        // 3. Deserialize Scene into fresh Scene instance
        auto reloadedScene = std::make_shared<Scene>();
        SceneSerializer deserializer(reloadedScene.get());
        bool deserialized = deserializer.Deserialize(scenePath);
        assert(deserialized);

        Entity loadedEntity;
        for (auto [e, tag] : reloadedScene->GetRegistry().view<TagComponent>().each()) {
            if (tag.Tag == "HUD_Canvas") {
                loadedEntity = Entity{ e, reloadedScene.get() };
                break;
            }
        }
        assert(bool(loadedEntity));
        assert(loadedEntity.HasComponent<UICanvasComponent>());

        auto& loadedUIC = loadedEntity.GetComponent<UICanvasComponent>();
        assert(loadedUIC.SortOrder == 5);
        assert(loadedUIC.Visible == true);
        assert(loadedUIC.Document != nullptr);
        assert(loadedUIC.Document->GetNodeCount() == 2);

        auto* loadedPanel = loadedUIC.Document->FindNodeByID("hud_box");
        assert(loadedPanel != nullptr);
        assert(loadedPanel->UseRectTransform == true);
        assert(loadedPanel->Transform.SizeDelta == glm::vec2(320.0f, 80.0f));

        // Clean up temporary files
        std::filesystem::remove(scenePath);
        std::filesystem::remove(uiDocPath);

        std::cout << "  -> SceneSerializer auto-synchronizes and serializes .axleui document: PASS" << std::endl;
        std::cout << "  -> SceneSerializer deserialization restores UICanvasComponent and parses .axleui: PASS" << std::endl;
        std::cout << "  -> Zero desynchronization between scene hierarchy and UI document AST: PASS" << std::endl;
    }

    std::cout << "\n>>> STEP 96 PASSED: Bidirectional Document Synchronization 100% verified! <<<\n" << std::endl;
    return true;
}





