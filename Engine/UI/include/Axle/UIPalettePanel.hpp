#pragma once
#include <Axle/Types.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/UI/UIDocument.hpp>
#include <Axle/UI/UITypes.hpp>
#include <imgui.h>
#include <string>
#include <string_view>
#include <vector>

namespace Axle {

    enum class UIWidgetType : uint32_t {
        Canvas = 0,
        Panel,
        Text,
        Image,
        Button,
        ProgressBar,
        RadialGauge,
        Count
    };

    struct UIWidgetDescriptor {
        UIWidgetType Type;
        std::string  Name;
        std::string  Icon;
        std::string  Category;
        std::string  Description;
        glm::vec2    DefaultSize;
        ImVec4       AccentColor;
    };

    class UIPalettePanel {
    public:
        static constexpr const char* DRAG_DROP_PAYLOAD_ID = "AXLE_UI_WIDGET_TYPE";

        UIPalettePanel();
        ~UIPalettePanel() = default;

        void SetContext(Scene* scene);
        void OnImGuiRender(bool* pOpen = nullptr);

        /// @brief Get static list of stock widget descriptors
        static const std::vector<UIWidgetDescriptor>& GetStockWidgets();

        /// @brief Spawn stock widget node into a target document
        /// @param doc Target UIDocument
        /// @param type Widget primitive type
        /// @param parent Parent node (defaults to doc->GetRootNode())
        /// @param canvasPos Optional drop position in canvas coordinates
        /// @return Pointer to newly spawned UINode
        static UINode* SpawnWidget(UIDocument* doc, UIWidgetType type, UINode* parent = nullptr, const glm::vec2& canvasPos = { -1.0f, -1.0f });

        /// @brief Spawn stock widget by name string ("Canvas", "Panel", "Text", "Image", "Button", "ProgressBar", "RadialGauge")
        static UINode* SpawnWidgetByName(UIDocument* doc, std::string_view typeName, UINode* parent = nullptr, const glm::vec2& canvasPos = { -1.0f, -1.0f });

        /// @brief Resolve active or auto-create a UICanvasComponent entity in the scene
        static Entity GetOrCreateCanvasEntity(Scene* scene);

        /// @brief Convert screen coordinate in Viewport to Canvas local coordinates
        static glm::vec2 ScreenToCanvas(const glm::vec2& screenPos, const glm::vec2& viewportMin, const glm::vec2& viewportSize, const glm::vec2& canvasRes);

    private:
        Scene* m_Scene = nullptr;
        ImGuiTextFilter m_Filter;
    };

} // namespace Axle
