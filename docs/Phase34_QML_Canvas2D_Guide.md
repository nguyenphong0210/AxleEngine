# 🎯 AxleEngine Phase 34 Technical Architecture Guide
## Declarative QML In-Game UI & Hardware-Accelerated `Canvas2D` Subsystem

---

## 1. Executive Summary & Architectural Philosophy

Phase 34 transitions AxleEngine from the temporary Phase 28 Step 6 ImGui-based runtime overlay (**Direction A**) to a dedicated, shipping-grade, zero-ImGui In-Game UI & HUD subsystem (**Direction B**).

### 1.1. Core Design Pillars
1. **Strict Separation of Tooling UI vs. In-Game UI:**
   - **`AxleEditor.exe`** retains **Dear ImGui** for developer tooling (DockSpace, Scene Hierarchy, Inspector, Content Browser, VFX Curve Editor).
   - **`AxleRuntime.exe`** strips all Dear ImGui dependencies, executing a pure, lightweight native 2D rendering and layout pipeline (`Axle::Canvas2D` + `Axle::UIDocument`).
2. **Hybrid Architecture (Macro-ECS + Micro-OOP + DOD Rendering):**
   - **Macro-Level ECS (`UICanvasComponent`):** In the EnTT `Scene` registry, an entire UI screen (e.g., `RacingHUD.axleui` or `PauseMenu.axleui`) is represented by **one single entity** carrying a `UICanvasComponent`. This prevents polluting the 3D spatial ECS registry with hundreds of micro-entities for individual labels, borders, and containers.
   - **Micro-Level OOP (`UIDocument` & `UINode`):** Inside each `UIDocument`, UI elements are organized in a lightweight polymorphic node hierarchy (`UINode` subclasses) owned contiguously in a single vector arena (`std::vector<Scope<UINode>>`). This directly supports recursive two-pass layout algorithms (**Bottom-Up Measure** and **Top-Down Arrange**).
   - **Data-Oriented Rendering (`Canvas2D`):** During rendering, the `UINode` tree does not issue individual GPU draw calls. Instead, nodes append flat `UIVertex` primitives into a contiguous pre-allocated buffer. `Canvas2D` leverages **Bindless Textures** and a **Procedural SDF Uber-Shader (`Canvas2D.hlsl`)** to draw rounded panels, borders, circular gauges, sprites, and MSDF text in **a single RHI draw call** across Direct3D 12, Vulkan 1.3, and OpenGL 4.5.
3. **AI-First Declarative QML Markup (`.axleui`):**
   - UI layouts, styling, and data bindings are authored in clean, declarative `.axleui` text files inspired by Qt QML.
   - Built-in file timestamp monitoring (`std::filesystem::last_write_time`) enables instant **Live Hot-Reloading** whenever a developer or AI agent edits and saves an `.axleui` file.

---

## 2. Subsystem Directory Structure

```plaintext
Engine/
├── Renderer/
│   ├── Canvas2D.hpp                 # Hardware-accelerated 2D batch renderer (RHI & GL)
│   └── Canvas2D.cpp
├── UI/
│   ├── Core/
│   │   ├── UITypes.hpp              # UIRect, UIAnchor, UIStyle, UIVertex, UIPrimitiveType
│   │   ├── UINode.hpp               # Base UINode and 8 concrete QML widget subclasses
│   │   ├── UINode.cpp               # Measure, Arrange, Property reflection, and OnDraw logic
│   │   ├── UIDocument.hpp           # Arena node owner, ID lookup table, bindings, hot-reload
│   │   └── UIDocument.cpp
│   ├── Font/
│   │   ├── UIFont.hpp               # MSDF Font Atlas loader, embedded fallback font, text metrics
│   │   └── UIFont.cpp
│   ├── Binding/
│   │   └── UIDataContext.hpp        # ViewModel bridge between C++ gameplay and .axleui bindings
│   └── Parser/
│       ├── QMLParser.hpp            # Zero-dependency C++20 Tokenizer & Recursive Descent Parser
│       └── QMLParser.cpp
Sandbox/
└── assets/
    ├── shaders/
    │   └── Canvas2D.hlsl            # Cross-API SDF & MSDF Uber-Shader (DX12 / Vulkan / OpenGL)
    ├── fonts/
    │   └── Orbitron-Bold.axlefont   # Pre-baked MSDF font metric & atlas asset
    └── ui/
        ├── RacingHUD.axleui         # Declarative automotive telemetry & tachometer HUD
        └── PauseMenu.axleui         # Declarative interactive pause menu
```

---

## 3. Deep-Dive Module Specifications

### 3.1. Hardware-Accelerated `Canvas2D` & SDF Uber-Shader (`Engine/Renderer`)

Traditional immediate-mode UI libraries tessellate rounded corners, borders, and circular arcs on the CPU into hundreds of triangles per widget. `Canvas2D` offloads all geometric curve evaluation to the Pixel Shader using **2D Signed Distance Fields (SDF)**. Every UI element—regardless of complexity—is emitted as **1 single Quad (4 vertices, 6 indices)**.

#### Memory Layout of `UIVertex` (80 Bytes, 16-Byte Aligned)
```cpp
struct alignas(16) UIVertex {
    glm::vec2 ScreenPos{ 0.0f };      // [0..7]   Screen-space coordinates in pixels (0..Width, 0..Height)
    glm::vec2 UV{ 0.0f };             // [8..15]  Normalized quad coordinates [0..1] for SDF, or Atlas UV for Sprite/Text
    glm::vec4 Color{ 1.0f };          // [16..31] Primary RGBA color (Fill / Text / Active Gauge Arc)
    glm::vec4 BorderColor{ 0.0f };    // [32..47] Secondary RGBA color (Border / Gauge Background Track)
    glm::vec2 QuadSize{ 100.0f };     // [48..55] Full quad dimensions in pixels (Width, Height)
    float     CornerRadius = 0.0f;    // [56..59] Corner radius in pixels (for SDF_Rect) or Arc thickness in pixels (for SDF_RadialArc)
    float     BorderWidth  = 0.0f;    // [60..63] Border thickness in pixels
    glm::vec2 AngleRange{ 0.0f };     // [64..71] (StartAngleRad, EndAngleRad) for Radial Gauges
    uint32_t  TextureIndex  = 0;      // [72..75] Bindless Texture Descriptor index (0 = Default White Texture)
    uint32_t  PrimitiveType = 0;      // [76..79] 0: SDF_Rect, 1: Sprite, 2: MSDF_Text, 3: SDF_RadialArc
};
static_assert(sizeof(UIVertex) == 80, "UIVertex must be exactly 80 bytes");
```

#### Procedural SDF Mathematics in `Canvas2D.hlsl`
1. **Vertex Shader Coordinate Transformation:**
   Converts pixel coordinates $(x, y) \in [0, W] \times [0, H]$ (top-left origin) into Normalized Device Coordinates (NDC):
   $$\text{NDC}_x = \frac{2.0 \cdot \text{ScreenPos}_x}{\text{ViewportWidth}} - 1.0, \quad \text{NDC}_y = 1.0 - \frac{2.0 \cdot \text{ScreenPos}_y}{\text{ViewportHeight}}$$

2. **`PrimitiveType == 0` (`SDF_Rect` — Rounded Box with Analytical Anti-Aliased Border):**
   Given local pixel position relative to the quad center $p = (\text{UV} - 0.5) \odot \text{QuadSize}$, half-extents $b = 0.5 \cdot \text{QuadSize}$, and clamped radius $r = \min(\text{CornerRadius}, \min(b_x, b_y))$:
   $$q = |p| - b + r$$
   $$d_{\text{outer}} = \|\max(q, 0.0)\|_2 + \min(\max(q_x, q_y), 0.0) - r$$
   - Outer anti-aliased coverage: $\alpha_{\text{outer}} = \text{saturate}(0.5 - d_{\text{outer}})$.
   - Inner fill boundary (when $\text{BorderWidth} > 0$):
     $$d_{\text{inner}} = d_{\text{outer}} + \text{BorderWidth}$$
     $$t_{\text{border}} = \text{saturate}(0.5 - d_{\text{inner}})$$
     $$\text{FinalColor} = \text{lerp}(\text{BorderColor}, \text{Color}, t_{\text{border}}) \cdot (1, 1, 1, \alpha_{\text{outer}})$$

3. **`PrimitiveType == 2` (`MSDF_Text` — Multi-Channel Signed Distance Field):**
   Samples the RGB channels from `BindlessTextures[input.TextureIndex]`:
   $$d_{\text{msdf}} = \text{median}(R, G, B) - 0.5$$
   $$\alpha_{\text{text}} = \text{saturate}(d_{\text{msdf}} \cdot \text{ScreenPxRange} + 0.5)$$

4. **`PrimitiveType == 3` (`SDF_RadialArc` — Automotive Tachometer & Circular Gauge):**
   Converts centered UV into polar radius $\rho = \|p\|_2$ and angle $\theta = \text{atan2}(p_y, p_x)$.
   - Ring radial distance: $d_{\text{ring}} = |\rho - (R_{\text{outer}} - 0.5 \cdot \text{Thickness})| - 0.5 \cdot \text{Thickness}$.
   - Evaluates angular coverage within $[\theta_{\text{start}}, \theta_{\text{end}}]$ for the background track (`BorderColor`) and $[\theta_{\text{start}}, \theta_{\text{active}}]$ for the active value bar (`Color`), where $\theta_{\text{active}} = \text{BorderWidth}$ (repurposed to store the active angle in radians).

---

### 3.2. Two-Pass Layout Engine & DPI Scaling (`Engine/UI/Core`)

All coordinates inside `.axleui` files are authored in a virtual **Reference Resolution** (default `1920x1080`).

#### Uniform DPI Scale Factor
$$\text{DPIScale} = \min\left(\frac{\text{ActualViewportWidth}}{\text{ReferenceWidth}}, \frac{\text{ActualViewportHeight}}{\text{ReferenceHeight}}\right)$$

#### Pass 1: Bottom-Up `Measure`
Traverses post-order (children before parents):
- **Leaf Nodes (`UITextNode`):** If `AutoSize == true`, queries `UIFont::MeasureText(Text, FontSize)` to determine `DesiredSize`. Otherwise uses explicit `Size`.
- **Container Nodes (`UIBoxContainerNode` — `Row` / `Column`):**
  - For `Column` (`IsVertical = true`):
    $$\text{DesiredWidth} = \text{Padding}_{\text{L}} + \text{Padding}_{\text{R}} + \max_{i}(\text{Child}_i.\text{DesiredWidth})$$
    $$\text{DesiredHeight} = \text{Padding}_{\text{T}} + \text{Padding}_{\text{B}} + \sum_{i} \text{Child}_i.\text{DesiredHeight} + \max(0, N - 1) \cdot \text{Spacing}$$

#### Pass 2: Top-Down `Arrange`
Traverses pre-order (parents before children), computing `ComputedRect` $(X, Y, W, H)$ inside `parentRect` $(P_x, P_y, P_w, P_h)$ with `Margin` $(M_L, M_T, M_R, M_B)$ and offset `Position` $(\Delta_x, \Delta_y)$:

| `UIAnchor` Mode | Computed $X$ & $Y$ | Computed $W$ & $H$ |
| :--- | :--- | :--- |
| **`TopLeft`** | $X = P_x + M_L + \Delta_x$<br>$Y = P_y + M_T + \Delta_y$ | $W = \text{DesiredWidth}$<br>$H = \text{DesiredHeight}$ |
| **`TopCenter`** | $X = P_x + 0.5(P_w - W) + \Delta_x$<br>$Y = P_y + M_T + \Delta_y$ | $W = \text{DesiredWidth}$<br>$H = \text{DesiredHeight}$ |
| **`TopRight`** | $X = P_x + P_w - W - M_R + \Delta_x$<br>$Y = P_y + M_T + \Delta_y$ | $W = \text{DesiredWidth}$<br>$H = \text{DesiredHeight}$ |
| **`CenterLeft`** | $X = P_x + M_L + \Delta_x$<br>$Y = P_y + 0.5(P_h - H) + \Delta_y$ | $W = \text{DesiredWidth}$<br>$H = \text{DesiredHeight}$ |
| **`Center`** | $X = P_x + 0.5(P_w - W) + \Delta_x$<br>$Y = P_y + 0.5(P_h - H) + \Delta_y$ | $W = \text{DesiredWidth}$<br>$H = \text{DesiredHeight}$ |
| **`CenterRight`** | $X = P_x + P_w - W - M_R + \Delta_x$<br>$Y = P_y + 0.5(P_h - H) + \Delta_y$ | $W = \text{DesiredWidth}$<br>$H = \text{DesiredHeight}$ |
| **`BottomLeft`** | $X = P_x + M_L + \Delta_x$<br>$Y = P_y + P_h - H - M_B + \Delta_y$ | $W = \text{DesiredWidth}$<br>$H = \text{DesiredHeight}$ |
| **`BottomCenter`** | $X = P_x + 0.5(P_w - W) + \Delta_x$<br>$Y = P_y + P_h - H - M_B + \Delta_y$ | $W = \text{DesiredWidth}$<br>$H = \text{DesiredHeight}$ |
| **`BottomRight`** | $X = P_x + P_w - W - M_R + \Delta_x$<br>$Y = P_y + P_h - H - M_B + \Delta_y$ | $W = \text{DesiredWidth}$<br>$H = \text{DesiredHeight}$ |
| **`TopStretch`** | $X = P_x + M_L$<br>$Y = P_y + M_T + \Delta_y$ | $W = P_w - (M_L + M_R)$<br>$H = \text{DesiredHeight}$ |
| **`BottomStretch`**| $X = P_x + M_L$<br>$Y = P_y + P_h - H - M_B + \Delta_y$ | $W = P_w - (M_L + M_R)$<br>$H = \text{DesiredHeight}$ |
| **`StretchAll`** | $X = P_x + M_L$<br>$Y = P_y + M_T$ | $W = P_w - (M_L + M_R)$<br>$H = P_h - (M_T + M_B)$ |

---

### 3.3. `.axleui` QML Grammar & Reactive Binding Specification

#### Formal EBNF Grammar
```ebnf
Document       ::= Element
Element        ::= Identifier "{" ( Property | Element )* "}"
Property       ::= Identifier ( "." Identifier )? ":" Value
Value          ::= StringLiteral | NumberLiteral | BoolLiteral | ArrayLiteral
ArrayLiteral   ::= "[" Value ( "," Value )* "]"
```

#### Supported Binding Expressions inside `StringLiteral`
1. **Direct Property Binding (`"{Source.Key}"`):**
   - Example: `value: "{Vehicle.RPMNormalized}"` or `text: "{Vehicle.SpeedKmh}"`
   - Evaluates `UIDataContext::Get("Vehicle.RPMNormalized")` each frame and applies the float/int/string value to the target property.
2. **String Interpolation Binding (`"Prefix {Source.Key} Suffix"`):**
   - Example: `text: "LAT G: {Vehicle.LateralG}G"`
   - Replaces `{Vehicle.LateralG}` inline with the formatted string representation from `UIDataContext`.
3. **Conditional Ternary Binding (`"{ConditionKey ? TrueValue : FalseValue}"`):**
   - Example: `color: "{Vehicle.IsRedline ? #FF1E42FF : #00F5D4FF}"`
   - Evaluates boolean `UIDataContext::Get("Vehicle.IsRedline")`. If `true`, parses and applies `TrueValue`; otherwise applies `FalseValue`.

---

## 4. Reference `.axleui` Production Templates

### 4.1. `Sandbox/assets/ui/RacingHUD.axleui`
```qml
Canvas {
    id: "racing_hud_root"
    referenceResolution: [1920, 1080]

    // Top-Left Telemetry & G-Force Panel
    Rectangle {
        id: "telemetry_panel"
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
            Text {
                id: "long_g_text"
                text: "LONGITUDINAL G: {Vehicle.LongitudinalG}"
                fontSize: 18
                color: "#E2E8F0"
            }
            Text {
                id: "camera_mode_text"
                text: "CAM: {Vehicle.CameraMode}"
                fontSize: 16
                color: "#94A3B8"
            }
        }
    }

    // Bottom-Right Tachometer, Gear & Speedometer Cluster
    Rectangle {
        id: "tachometer_cluster"
        anchor: "BottomRight"
        margin: [0, 0, 48, 48]
        size: [340, 340]
        color: "#0B0E14D9"
        cornerRadius: 170
        borderWidth: 3
        borderColor: "#334155"

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

        Text {
            id: "gear_indicator"
            anchor: "Center"
            pos: [0, -24]
            text: "{Vehicle.GearString}"
            fontSize: 88
            color: "{Vehicle.IsRedline ? #FF1E42FF : #FFFFFFFF}"
        }

        Column {
            anchor: "BottomCenter"
            margin: [0, 0, 0, 52]
            spacing: 2

            Text {
                id: "speed_value"
                text: "{Vehicle.SpeedKmh}"
                fontSize: 52
                color: "#38BDF8"
            }
            Text {
                id: "speed_unit"
                text: "KM/H"
                fontSize: 16
                color: "#94A3B8"
            }
        }
    }

    // Bottom-Right Pedal Input Bars (Throttle & Brake)
    Rectangle {
        id: "pedal_inputs_panel"
        anchor: "BottomRight"
        margin: [0, 0, 408, 48]
        size: [180, 96]
        color: "#0B0E14CC"
        cornerRadius: 10
        borderWidth: 2
        borderColor: "#1E293B"

        Column {
            anchor: "Center"
            size: [152, 68]
            spacing: 10

            Text {
                text: "THROTTLE"
                fontSize: 12
                color: "#94A3B8"
            }
            ProgressBar {
                id: "throttle_bar"
                size: [152, 12]
                value: "{Vehicle.Throttle}"
                fillColor: "#10B981"
                backgroundColor: "#1E293B"
                cornerRadius: 6
            }
            Text {
                text: "BRAKE"
                fontSize: 12
                color: "#94A3B8"
            }
            ProgressBar {
                id: "brake_bar"
                size: [152, 12]
                value: "{Vehicle.Brake}"
                fillColor: "#EF4444"
                backgroundColor: "#1E293B"
                cornerRadius: 6
            }
        }
    }
}
```

### 4.2. `Sandbox/assets/ui/PauseMenu.axleui`
```qml
Canvas {
    id: "pause_menu_root"
    referenceResolution: [1920, 1080]
    visible: "{Game.IsPaused}"

    Rectangle {
        id: "dim_backdrop"
        anchor: "StretchAll"
        color: "#000000AA"

        Rectangle {
            id: "pause_dialog"
            anchor: "Center"
            size: [460, 380]
            color: "#0F172AF2"
            cornerRadius: 16
            borderWidth: 2
            borderColor: "#38BDF8"

            Column {
                anchor: "Center"
                size: [360, 280]
                spacing: 20

                Text {
                    id: "pause_title"
                    text: "SIMULATION PAUSED"
                    fontSize: 32
                    color: "#F8FAFC"
                }

                Button {
                    id: "btn_resume"
                    size: [360, 56]
                    cornerRadius: 8
                    normalColor: "#1E293B"
                    hoverColor: "#334155"
                    pressedColor: "#0284C7"
                    onClick: "OnResumeGame"
                    navDown: "btn_reset_vehicle"

                    Text {
                        anchor: "Center"
                        text: "RESUME DRIVING"
                        fontSize: 20
                        color: "#FFFFFF"
                    }
                }

                Button {
                    id: "btn_reset_vehicle"
                    size: [360, 56]
                    cornerRadius: 8
                    normalColor: "#1E293B"
                    hoverColor: "#334155"
                    pressedColor: "#0284C7"
                    onClick: "OnResetVehicle"
                    navUp: "btn_resume"
                    navDown: "btn_quit"

                    Text {
                        anchor: "Center"
                        text: "RESET VEHICLE (R)"
                        fontSize: 20
                        color: "#FFFFFF"
                    }
                }

                Button {
                    id: "btn_quit"
                    size: [360, 56]
                    cornerRadius: 8
                    normalColor: "#7F1D1D"
                    hoverColor: "#991B1B"
                    pressedColor: "#DC2626"
                    onClick: "OnQuitGame"
                    navUp: "btn_reset_vehicle"

                    Text {
                        anchor: "Center"
                        text: "EXIT TO DESKTOP"
                        fontSize: 20
                        color: "#FFFFFF"
                    }
                }
            }
        }
    }
}
```