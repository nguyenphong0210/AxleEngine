# 🤖 Antigravity Execution Plan: Phase 34 — Declarative QML In-Game UI & `Canvas2D`

> **CRITICAL DIRECTIVES FOR AI AGENT (GOOGLE ANTIGRAVITY):**
> 1. **Incremental Step-by-Step Execution:** Execute Phase 34 strictly one Step at a time (`Step 1` $\rightarrow$ `Step 2` $\rightarrow$ `Step 3` $\rightarrow$ `Step 4` $\rightarrow$ `Step 5` $\rightarrow$ `Step 6`). After completing each Step, compile `AxleEngine`, `AxleEditor`, `AxleRuntime`, and `AxleTests`, and run `AxleTests.exe` to verify 100% pass rate before starting the next Step.
> 2. **Inspect Existing Interfaces First:** Before creating files in Step 1, inspect `CMakeLists.txt`, `Engine/RHI/`, `Engine/Renderer/`, `Engine/Scene/Components.hpp`, `Engine/Scene/Scene.hpp`, and `Editor/src/Test.cpp` to align with existing smart pointer aliases (`Ref<T>`, `Scope<T>`), logging macros, and RHI/OpenGL abstractions.
> 3. **Zero ImGui Leakage in Core UI:** Do NOT `#include <imgui.h>` inside any file under `Engine/Renderer/Canvas2D.*`, `Engine/UI/Core/*`, `Engine/UI/Font/*`, `Engine/UI/Parser/*`, `Engine/UI/Binding/*`, or `Runtime/`.
> 4. **Headless Test Safety:** All unit tests in `AxleTests.exe` must run deterministically even when no OS window or swapchain is active (CPU vertex batching, layout math, font measurement, and QML parsing must never crash when `RHIDevice` or OpenGL context is headless/null).

---

## 🛠️ STEP 1: RHI `Canvas2D` Batch Renderer & Procedural SDF Uber-Shader

### 1.1. Target Files
- **Create:** `Engine/UI/Core/UITypes.hpp`
- **Create:** `Engine/Renderer/Canvas2D.hpp`
- **Create:** `Engine/Renderer/Canvas2D.cpp`
- **Create:** `Sandbox/assets/shaders/Canvas2D.hlsl`
- **Modify:** `CMakeLists.txt` (Register `Canvas2D.cpp` in the engine build target)
- **Modify:** `Editor/src/Test.cpp` (Add `TestPhase34_Step1_Canvas2DBatching`)

### 1.2. Complete Header Specification: `Engine/UI/Core/UITypes.hpp`
```cpp
#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace Axle {

    enum class UIAnchor : uint8_t {
        TopLeft = 0, TopCenter, TopRight,
        CenterLeft,  Center,    CenterRight,
        BottomLeft,  BottomCenter, BottomRight,
        TopStretch,  BottomStretch, StretchAll
    };

    enum class UIPrimitiveType : uint32_t {
        SDF_Rect      = 0,
        Sprite        = 1,
        MSDF_Text     = 2,
        SDF_RadialArc = 3
    };

    struct UIRect {
        float X = 0.0f;
        float Y = 0.0f;
        float Width = 0.0f;
        float Height = 0.0f;

        constexpr bool Contains(const glm::vec2& pt) const {
            return pt.x >= X && pt.x <= (X + Width) &&
                   pt.y >= Y && pt.y <= (Y + Height);
        }

        constexpr UIRect Intersect(const UIRect& other) const {
            float x0 = (X > other.X) ? X : other.X;
            float y0 = (Y > other.Y) ? Y : other.Y;
            float x1 = ((X + Width) < (other.X + other.Width)) ? (X + Width) : (other.X + other.Width);
            float y1 = ((Y + Height) < (other.Y + other.Height)) ? (Y + Height) : (other.Y + other.Height);
            if (x1 <= x0 || y1 <= y0) return { 0.0f, 0.0f, 0.0f, 0.0f };
            return { x0, y0, x1 - x0, y1 - y0 };
        }
    };

    struct UIStyle {
        glm::vec4 BackgroundColor{ 0.07f, 0.09f, 0.13f, 0.85f };
        glm::vec4 BorderColor{ 0.22f, 0.74f, 0.97f, 1.0f };
        float CornerRadius = 0.0f;
        float BorderWidth  = 0.0f;
    };

    struct alignas(16) UIVertex {
        glm::vec2 ScreenPos{ 0.0f };      // [0..7]
        glm::vec2 UV{ 0.0f };             // [8..15]
        glm::vec4 Color{ 1.0f };          // [16..31]
        glm::vec4 BorderColor{ 0.0f };    // [32..47]
        glm::vec2 QuadSize{ 100.0f };     // [48..55]
        float     CornerRadius = 0.0f;    // [56..59]
        float     BorderWidth  = 0.0f;    // [60..63]
        glm::vec2 AngleRange{ 0.0f };     // [64..71]
        uint32_t  TextureIndex  = 0;      // [72..75]
        uint32_t  PrimitiveType = 0;      // [76..79]
    };
    static_assert(sizeof(UIVertex) == 80, "UIVertex size must be 80 bytes");

} // namespace Axle
```

### 1.3. Complete Header Specification: `Engine/Renderer/Canvas2D.hpp`
```cpp
#pragma once
#include "Engine/UI/Core/UITypes.hpp"
#include <vector>
#include <memory>
#include <string_view>

namespace Axle {

    class UIFont;

    struct Canvas2DStats {
        uint32_t QuadCount   = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount  = 0;
        uint32_t DrawCalls   = 0;
    };

    class Canvas2D {
    public:
        static constexpr uint32_t MaxQuads    = 10000;
        static constexpr uint32_t MaxVertices = MaxQuads * 4;
        static constexpr uint32_t MaxIndices  = MaxQuads * 6;

        Canvas2D();
        ~Canvas2D();

        void Init();
        void Shutdown();

        void BeginCanvas(const glm::vec2& viewportSize, float dpiScale = 1.0f);
        void EndCanvas();

        void DrawRect(const UIRect& rect, const UIStyle& style);
        void DrawSprite(const UIRect& rect, uint32_t bindlessTexIndex, const glm::vec4& tint = glm::vec4(1.0f),
                        const glm::vec4& uvBounds = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
        void DrawRadialArc(const UIRect& rect, float startAngleDeg, float endAngleDeg, float valueNormalized,
                           float thicknessPx, const glm::vec4& fillColor, const glm::vec4& trackColor);
        void DrawTextString(std::string_view text, const std::shared_ptr<UIFont>& font,
                            const UIRect& rect, float fontSize, const glm::vec4& color);

        void PushClipRect(const UIRect& clipRect);
        void PopClipRect();
        UIRect GetCurrentClipRect() const;

        const std::vector<UIVertex>& GetVertexBuffer() const { return m_Vertices; }
        const std::vector<uint32_t>& GetIndexBuffer() const { return m_Indices; }
        const Canvas2DStats& GetStats() const { return m_Stats; }
        glm::vec2 GetViewportSize() const { return m_ViewportSize; }
        float GetDPIScale() const { return m_DPIScale; }

    private:
        void EmitQuad(const UIRect& unscaledRect, const glm::vec4& uvBounds,
                      const glm::vec4& color, const glm::vec4& borderColor,
                      float cornerRadius, float borderWidth, const glm::vec2& angleRange,
                      uint32_t texIndex, UIPrimitiveType type);

    private:
        bool m_Initialized = false;
        glm::vec2 m_ViewportSize{ 1920.0f, 1080.0f };
        float m_DPIScale = 1.0f;

        std::vector<UIVertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
        std::vector<UIRect>   m_ClipStack;
        Canvas2DStats         m_Stats{};
    };

} // namespace Axle
```

### 1.4. Implementation Details for `Canvas2D.cpp` & `Canvas2D.hlsl`
- In `EmitQuad(...)`:
  - Multiply `unscaledRect.X`, `Y`, `Width`, `Height`, `cornerRadius`, and `borderWidth` (unless `type == UIPrimitiveType::SDF_RadialArc`, where `borderWidth` stores the active angle in radians) by `m_DPIScale`.
  - If `m_ClipStack` is non-empty, intersect the quad rectangle with `GetCurrentClipRect()`. If the intersection area is $\le 0$, cull the quad immediately.
  - Append 4 `UIVertex` entries (Top-Left, Top-Right, Bottom-Right, Bottom-Left) and increment `m_Stats.QuadCount`, `VertexCount += 4`, `IndexCount += 6`.
- In `EndCanvas()`:
  - If `m_Vertices.empty()`, set `m_Stats.DrawCalls = 0` and return.
  - Upload `m_Vertices` to the active RHI / OpenGL dynamic vertex buffer if a GPU context is active, dispatch 1 indexed draw call, and set `m_Stats.DrawCalls = 1`.
- **Unit Test (`TestPhase34_Step1_Canvas2DBatching`):**
  - Instantiate `Canvas2D`, call `Init()`, `BeginCanvas({1920.0f, 1080.0f}, 1.0f)`.
  - Call `DrawRect({100, 100, 200, 80}, style)` and `DrawRadialArc({400, 400, 300, 300}, -225.0f, 45.0f, 0.75f, 16.0f, fill, track)`.
  - Call `EndCanvas()`. Assert `GetStats().QuadCount == 2`, `GetStats().VertexCount == 8`, `GetStats().DrawCalls == 1`.
  - Test `PushClipRect({0, 0, 50, 50})` and verify that drawing a rect completely outside `{200, 200, 50, 50}` gets culled (`QuadCount` does not increase).

---

## 🔤 STEP 2: MSDF Font Asset & Text Layout Pipeline

### 2.1. Target Files
- **Create:** `Engine/UI/Font/UIFont.hpp`
- **Create:** `Engine/UI/Font/UIFont.cpp`
- **Modify:** `Engine/Renderer/Canvas2D.cpp` (Complete `DrawTextString` implementation)
- **Modify:** `CMakeLists.txt` (Add `UIFont.cpp`)
- **Modify:** `Editor/src/Test.cpp` (Add `TestPhase34_Step2_MSDFFontLayout`)

### 2.2. Complete Header Specification: `Engine/UI/Font/UIFont.hpp`
```cpp
#pragma once
#include "Engine/UI/Core/UITypes.hpp"
#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>

namespace Axle {

    struct GlyphMetrics {
        uint32_t  Codepoint = 0;
        float     AdvanceX  = 0.6f;                  // Normalized to 1.0 em
        glm::vec4 PlaneBounds{ 0.0f, 0.0f, 0.55f, 0.8f }; // Left, Bottom, Right, Top (em)
        glm::vec4 AtlasBounds{ 0.0f, 0.0f, 1.0f, 1.0f };  // u0, v0, u1, v1 (normalized UV)
    };

    class UIFont {
    public:
        UIFont();
        ~UIFont() = default;

        void InitDefaultEmbeddedFont();
        bool LoadFromAxleFont(const std::string& filepath);
        bool SaveToAxleFont(const std::string& filepath) const;

        const GlyphMetrics* GetGlyph(uint32_t codepoint) const;
        float GetKerning(uint32_t first, uint32_t second) const;
        void SetKerning(uint32_t first, uint32_t second, float amount);

        glm::vec2 MeasureText(std::string_view text, float fontSize) const;

        uint32_t GetBindlessTextureIndex() const { return m_BindlessTextureIndex; }
        void SetBindlessTextureIndex(uint32_t idx) { m_BindlessTextureIndex = idx; }
        float GetLineHeight() const { return m_LineHeight; }

    private:
        float m_LineHeight = 1.15f;
        uint32_t m_BindlessTextureIndex = 0;
        std::unordered_map<uint32_t, GlyphMetrics> m_Glyphs;
        std::unordered_map<uint64_t, float> m_KerningMap;
    };

} // namespace Axle
```

### 2.3. Implementation Details & Verification
- `UIFont()` constructor must call `InitDefaultEmbeddedFont()` automatically so every `UIFont` instance has valid metrics for ASCII characters `32..126` (plus newline `\n` handling) immediately upon construction.
- In `Canvas2D::DrawTextString`:
  - Iterate through `text`. Skip emitting quads for whitespace `' '` or `'\n'` (only advance cursor `cursorX` or `cursorY`), and emit 1 quad per visible character with `UIPrimitiveType::MSDF_Text`.
- **Unit Test (`TestPhase34_Step2_MSDFFontLayout`):**
  - Verify `MeasureText("280 KM/H", 48.0f)` returns positive width and height.
  - Verify `DrawTextString("280 KM/H", font, rect, 48.0f, color)` emits exactly `7` quads (since the space `' '` is not emitted as a geometry quad).

---

## 🌳 STEP 3: Hybrid OOP UI DOM Tree (`UINode` & `UIDocument`) & 2-Pass Layout Engine

### 3.1. Target Files
- **Create:** `Engine/UI/Core/UINode.hpp`
- **Create:** `Engine/UI/Core/UINode.cpp`
- **Create:** `Engine/UI/Core/UIDocument.hpp`
- **Create:** `Engine/UI/Core/UIDocument.cpp`
- **Modify:** `CMakeLists.txt` (Add `UINode.cpp`, `UIDocument.cpp`)
- **Modify:** `Editor/src/Test.cpp` (Add `TestPhase34_Step3_LayoutEngine2Pass`)

### 3.2. Complete Header Specification: `Engine/UI/Core/UINode.hpp`
```cpp
#pragma once
#include "Engine/UI/Core/UITypes.hpp"
#include "Engine/Renderer/Canvas2D.hpp"
#include "Engine/UI/Font/UIFont.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace Axle {

    class UINode {
    public:
        virtual ~UINode() = default;
        virtual std::string_view GetTypeName() const = 0;

        std::string ID;
        UINode* Parent = nullptr;
        std::vector<UINode*> Children;

        UIAnchor  Anchor   = UIAnchor::TopLeft;
        glm::vec2 Position{ 0.0f, 0.0f };
        glm::vec2 Size{ 100.0f, 100.0f };
        glm::vec4 Margin{ 0.0f };  // x: Left, y: Top, z: Right, w: Bottom
        glm::vec4 Padding{ 0.0f }; // x: Left, y: Top, z: Right, w: Bottom
        int32_t   ZIndex   = 0;
        bool      Visible  = true;

        glm::vec2 DesiredSize{ 0.0f };
        UIRect    ComputedRect{};

        virtual glm::vec2 OnMeasure(const std::shared_ptr<UIFont>& defaultFont);
        virtual void OnArrange(const UIRect& parentRect);
        virtual void OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) = 0;

        virtual bool SetProperty(std::string_view propName, std::string_view strVal);
        virtual bool SetPropertyFloat(std::string_view propName, float val);
        virtual bool SetPropertyBool(std::string_view propName, bool val);
        virtual bool SetPropertyVec2(std::string_view propName, const glm::vec2& val);
        virtual bool SetPropertyVec4(std::string_view propName, const glm::vec4& val);

        static glm::vec4 ParseHexColor(std::string_view hexStr);
        static UIAnchor ParseAnchor(std::string_view anchorStr);
    };

    class UICanvasNode : public UINode {
    public:
        std::string_view GetTypeName() const override { return "Canvas"; }
        glm::vec2 ReferenceResolution{ 1920.0f, 1080.0f };
        bool SetPropertyVec2(std::string_view propName, const glm::vec2& val) override;
        void OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) override {}
    };

    class UIRectangleNode : public UINode {
    public:
        std::string_view GetTypeName() const override { return "Rectangle"; }
        UIStyle Style{};
        bool SetProperty(std::string_view propName, std::string_view strVal) override;
        bool SetPropertyFloat(std::string_view propName, float val) override;
        bool SetPropertyVec4(std::string_view propName, const glm::vec4& val) override;
        void OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) override;
    };

    class UITextNode : public UINode {
    public:
        std::string_view GetTypeName() const override { return "Text"; }
        std::string Text = "";
        float FontSize = 24.0f;
        glm::vec4 Color{ 1.0f };
        bool AutoSize = true;

        glm::vec2 OnMeasure(const std::shared_ptr<UIFont>& defaultFont) override;
        bool SetProperty(std::string_view propName, std::string_view strVal) override;
        bool SetPropertyFloat(std::string_view propName, float val) override;
        bool SetPropertyVec4(std::string_view propName, const glm::vec4& val) override;
        void OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) override;
    };

    class UIImageNode : public UINode {
    public:
        std::string_view GetTypeName() const override { return "Image"; }
        std::string TexturePath;
        uint32_t BindlessTextureIndex = 0;
        glm::vec4 TintColor{ 1.0f };
        bool SetProperty(std::string_view propName, std::string_view strVal) override;
        bool SetPropertyVec4(std::string_view propName, const glm::vec4& val) override;
        void OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) override;
    };

    class UIProgressBarNode : public UINode {
    public:
        std::string_view GetTypeName() const override { return "ProgressBar"; }
        float Value = 0.5f;
        glm::vec4 FillColor{ 0.06f, 0.72f, 0.5f, 1.0f };
        glm::vec4 BackgroundColor{ 0.12f, 0.16f, 0.23f, 0.9f };
        float CornerRadius = 4.0f;

        bool SetProperty(std::string_view propName, std::string_view strVal) override;
        bool SetPropertyFloat(std::string_view propName, float val) override;
        bool SetPropertyVec4(std::string_view propName, const glm::vec4& val) override;
        void OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) override;
    };

    class UIRadialGaugeNode : public UINode {
    public:
        std::string_view GetTypeName() const override { return "RadialGauge"; }
        float Value      = 0.0f;
        float StartAngle = -225.0f;
        float EndAngle   = 45.0f;
        float Thickness  = 18.0f;
        glm::vec4 Color{ 0.0f, 0.96f, 0.83f, 1.0f };
        glm::vec4 TrackColor{ 0.12f, 0.16f, 0.23f, 0.6f };

        bool SetProperty(std::string_view propName, std::string_view strVal) override;
        bool SetPropertyFloat(std::string_view propName, float val) override;
        bool SetPropertyVec4(std::string_view propName, const glm::vec4& val) override;
        void OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) override;
    };

    class UIBoxContainerNode : public UINode {
    public:
        explicit UIBoxContainerNode(bool vertical = true) : IsVertical(vertical) {}
        std::string_view GetTypeName() const override { return IsVertical ? "Column" : "Row"; }
        bool IsVertical = true;
        float Spacing = 4.0f;

        glm::vec2 OnMeasure(const std::shared_ptr<UIFont>& defaultFont) override;
        void OnArrange(const UIRect& parentRect) override;
        bool SetPropertyFloat(std::string_view propName, float val) override;
        void OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) override {}
    };

    class UIButtonNode : public UIRectangleNode {
    public:
        std::string_view GetTypeName() const override { return "Button"; }
        glm::vec4 NormalColor{ 0.12f, 0.16f, 0.23f, 0.95f };
        glm::vec4 HoverColor{ 0.20f, 0.25f, 0.33f, 1.0f };
        glm::vec4 PressedColor{ 0.01f, 0.52f, 0.78f, 1.0f };
        std::string OnClickAction;
        std::string NavUp, NavDown, NavLeft, NavRight;
        bool IsHovered = false;
        bool IsPressed = false;
        bool IsFocused = false;

        bool SetProperty(std::string_view propName, std::string_view strVal) override;
        bool SetPropertyVec4(std::string_view propName, const glm::vec4& val) override;
        void OnDraw(Canvas2D& canvas, const std::shared_ptr<UIFont>& defaultFont) override;
    };

} // namespace Axle
```

### 3.3. Complete Header Specification: `Engine/UI/Core/UIDocument.hpp`
```cpp
#pragma once
#include "Engine/UI/Core/UINode.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace Axle {

    class UIDataContext; // Forward declaration for Step 5

    enum class UIBindingType : uint8_t {
        Direct = 0,        // "{Vehicle.SpeedKmh}"
        Interpolated,      // "FPS: {Engine.FPS}"
        Ternary            // "{Vehicle.IsRedline ? #FF1E42FF : #00F5D4FF}"
    };

    struct UIPropertyBinding {
        UINode*       TargetNode = nullptr;
        std::string   PropertyName;
        UIBindingType Type = UIBindingType::Direct;
        std::string   SourceKey;
        std::string   TemplateText; // Used for Interpolated strings
        std::string   TrueValue;    // Used for Ternary
        std::string   FalseValue;   // Used for Ternary
    };

    class UIDocument {
    public:
        UIDocument();
        ~UIDocument() = default;

        template<typename T, typename... Args>
        T* CreateNode(UINode* parent, Args&&... args) {
            auto node = std::make_unique<T>(std::forward<Args>(args)...);
            T* rawPtr = node.get();
            if (parent) {
                rawPtr->Parent = parent;
                parent->Children.push_back(rawPtr);
            } else if (!m_RootNode) {
                m_RootNode = rawPtr;
            }
            m_NodeArena.push_back(std::move(node));
            return rawPtr;
        }

        void Clear();
        void RebuildIDMap();
        UINode* FindNodeByID(const std::string& id) const;

        void UpdateLayout(const glm::vec2& viewportSize);
        void Render(Canvas2D& canvas);

        // Step 4 & 5 Hooks
        void AddBinding(const UIPropertyBinding& binding) { m_Bindings.push_back(binding); }
        const std::vector<UIPropertyBinding>& GetBindings() const { return m_Bindings; }
        void EvaluateBindings(const UIDataContext& context);
        bool ProcessInput(const glm::vec2& mousePos, bool mouseDown, bool mouseClicked, const UIDataContext& context);
        bool NavigateFocus(std::string_view direction); // "Up", "Down", "Left", "Right"
        bool ActivateFocusedButton(const UIDataContext& context);

        // File Management & Hot-Reloading
        void SetFilePath(const std::string& path);
        const std::string& GetFilePath() const { return m_FilePath; }
        bool CheckAndHotReload();

        UINode* GetRootNode() const { return m_RootNode; }
        float GetComputedDPIScale() const { return m_ComputedDPIScale; }

    private:
        void MeasureRecursive(UINode* node);
        void ArrangeRecursive(UINode* node, const UIRect& parentRect);
        void RenderRecursive(UINode* node, Canvas2D& canvas);

    private:
        UINode* m_RootNode = nullptr;
        std::vector<std::unique_ptr<UINode>> m_NodeArena;
        std::unordered_map<std::string, UINode*> m_IDMap;
        std::vector<UIPropertyBinding> m_Bindings;
        std::shared_ptr<UIFont> m_DefaultFont;

        std::string m_FilePath;
        std::filesystem::file_time_type m_LastWriteTime{};
        float m_ComputedDPIScale = 1.0f;
    };

} // namespace Axle
```

---

## 📜 STEP 4: Declarative QML-Style `.axleui` Parser & Live Hot-Reloading

### 4.1. Target Files
- **Create:** `Engine/UI/Parser/QMLParser.hpp`
- **Create:** `Engine/UI/Parser/QMLParser.cpp`
- **Modify:** `Engine/UI/Core/UIDocument.cpp` (Wire `CheckAndHotReload()` to `QMLParser::ParseFile`)
- **Modify:** `CMakeLists.txt` (Add `QMLParser.cpp`)
- **Modify:** `Editor/src/Test.cpp` (Add `TestPhase34_Step4_QMLParserAndBindings`)

### 4.2. Complete Header Specification: `Engine/UI/Parser/QMLParser.hpp`
```cpp
#pragma once
#include "Engine/UI/Core/UIDocument.hpp"
#include <string>
#include <string_view>

namespace Axle {

    class QMLParser {
    public:
        static bool ParseSource(std::string_view qmlSource, UIDocument& outDocument, std::string* outError = nullptr);
        static bool ParseFile(const std::string& filepath, UIDocument& outDocument, std::string* outError = nullptr);
    };

} // namespace Axle
```

### 4.3. Parser Implementation Requirements
- Strip single-line comments `//...` and block comments `/*...*/`.
- Support element declarations: `Canvas`, `Rectangle`, `Panel` (alias for `UIRectangleNode`), `Text`, `Image`, `ProgressBar`, `RadialGauge`, `Column`, `VBox` (alias for `Column`), `Row`, `HBox` (alias for `Row`), and `Button`.
- Support property names with dots (e.g., `anchors.fill: "parent"` sets `Anchor = UIAnchor::StretchAll`).
- Automatically inspect string values for `{` and `}`:
  - If the string contains `?` and `:` inside `{...}`, parse as `UIBindingType::Ternary` (`SourceKey`, `TrueValue`, `FalseValue`).
  - If the string starts with `{` and ends with `}` with no other text outside, parse as `UIBindingType::Direct`.
  - If the string has surrounding text like `"FPS: {Engine.FPS}"`, parse as `UIBindingType::Interpolated`.
- **Unit Test (`TestPhase34_Step4_QMLParserAndBindings`):**
  - Parse the full `RacingHUD.axleui` string in memory.
  - Assert `FindNodeByID("tachometer_cluster") != nullptr`, `FindNodeByID("rpm_radial_gauge") != nullptr`, and `GetBindings().size() >= 8`.

---

## 🔗 STEP 5: Reactive Data Binding (`UIDataContext`), Input Events & ECS Integration

### 5.1. Target Files
- **Create:** `Engine/UI/Binding/UIDataContext.hpp`
- **Modify:** `Engine/UI/Core/UIDocument.cpp` (Implement `EvaluateBindings`, `ProcessInput`, `NavigateFocus`, `ActivateFocusedButton`)
- **Modify:** `Engine/Scene/Components.hpp` (Add `UICanvasComponent`)
- **Modify:** `Engine/Scene/Scene.hpp` & `Engine/Scene/Scene.cpp` (Add `UIDataContext m_UIDataContext` and `Canvas2D m_Canvas2D`, evaluate and render active `UICanvasComponent` entities)
- **Modify:** `Engine/Scene/SceneSerializer.cpp` (Serialize/Deserialize `UICanvasComponent`)
- **Modify:** `Editor/src/Test.cpp` (Add `TestPhase34_Step5_DataBindingAndHitTest`)

### 5.2. `UICanvasComponent` Specification (`Engine/Scene/Components.hpp`)
```cpp
struct UICanvasComponent {
    std::string AssetPath;
    int32_t SortOrder = 0;
    bool Visible = true;
    std::shared_ptr<UIDocument> Document = nullptr;

    UICanvasComponent() = default;
    UICanvasComponent(const UICanvasComponent&) = default;
    explicit UICanvasComponent(const std::string& path) : AssetPath(path) {}
};
```

### 5.3. Binding & Input Logic (`UIDocument.cpp`)
- In `EvaluateBindings(const UIDataContext& context)`:
  - For each `UIPropertyBinding`:
    - Look up `context.Get(binding.SourceKey)`.
    - For `UIBindingType::Direct`: dispatch to `TargetNode->SetPropertyFloat`, `SetPropertyBool`, `SetPropertyVec4`, or `SetProperty` (converting `int32_t`/`float` to formatted string if the target property is `"text"`).
    - For `UIBindingType::Interpolated`: replace `{Key}` in `TemplateText` with the stringified value from `context` and call `TargetNode->SetProperty(binding.PropertyName, resultStr)`.
    - For `UIBindingType::Ternary`: read boolean value from `context.Get(binding.SourceKey)`. Choose `TrueValue` if true, else `FalseValue`, and call `TargetNode->SetProperty(binding.PropertyName, chosenStr)`.
- In `ProcessInput(const glm::vec2& mousePos, bool mouseDown, bool mouseClicked, const UIDataContext& context)`:
  - Convert `mousePos` into Reference Resolution coordinates by dividing by `m_ComputedDPIScale`.
  - Traverse visible `UIButtonNode` instances in reverse Z-order / reverse pre-order.
  - Update `IsHovered` and `IsPressed`. If `mouseClicked` is true on a hovered button, call `context.TriggerAction(button->OnClickAction)` and return `true` (input consumed).

---

## 🏁 STEP 6: `RacingHUD.axleui` Migration, Zero-ImGui `AxleRuntime` & Final Verification

### 6.1. Target Files
- **Create:** `Sandbox/assets/ui/RacingHUD.axleui` (Use template from Section 4.1 of `Phase34_QML_Canvas2D_Guide.md`)
- **Create:** `Sandbox/assets/ui/PauseMenu.axleui` (Use template from Section 4.2 of `Phase34_QML_Canvas2D_Guide.md`)
- **Modify:** `Runtime/src/RuntimeLayer.cpp` & `Runtime/CMakeLists.txt` (Bind vehicle telemetry to `Scene::GetUIDataContext()`, render `RacingHUD.axleui` and `PauseMenu.axleui` via `Canvas2D`, and remove `ImGuiLayer` from `AxleRuntime`)
- **Modify:** `Editor/src/Test.cpp` (Add `TestPhase34_Step6_RacingHUDFullIntegration`)
- **Modify:** `Roadmap.md` (Mark all Phase 34 checkboxes `[x]` and update completed phase count in Executive Summary)

### 6.2. Final Acceptance Test (`TestPhase34_Step6_RacingHUDFullIntegration`)
1. Load `Sandbox/assets/ui/RacingHUD.axleui` and `Sandbox/assets/ui/PauseMenu.axleui` via `QMLParser::ParseFile`.
2. Populate `UIDataContext` with realistic telemetry:
   - `Engine.FPS = 144`
   - `Vehicle.SpeedKmh = 215`
   - `Vehicle.RPMNormalized = 0.92f`
   - `Vehicle.IsRedline = true`
   - `Vehicle.GearString = "5"`
   - `Vehicle.Throttle = 1.0f`, `Vehicle.Brake = 0.0f`
3. Run `EvaluateBindings(ctx)`, `UpdateLayout({1920.0f, 1080.0f})`, and `Render(canvas)`.
4. Assert:
   - `FindNodeByID("speed_value")` has `Text == "215"`.
   - `FindNodeByID("rpm_radial_gauge")` has redline color `#FF1E42FF`.
   - `canvas.GetStats().DrawCalls == 1` and `canvas.GetStats().QuadCount > 20`.
   - Simulating a click on `btn_resume` inside `PauseMenu.axleui` invokes the bound `OnResumeGame` C++ callback.