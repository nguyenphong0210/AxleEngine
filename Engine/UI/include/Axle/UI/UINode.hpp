#pragma once
#include <Axle/UI/UITypes.hpp>
#include <Axle/UI/RectTransform.hpp>
#include <Axle/UI/Canvas2D.hpp>
#include <Axle/UI/UIFont.hpp>
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

        // Geometric RectTransform (Unity-style layout model)
        bool          UseRectTransform = false;
        RectTransform Transform{};

        void SetUseRectTransform(bool enable);
        void SyncTransformFromLegacy();
        void SyncLegacyFromTransform();

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
        UICanvasNode() {
            Size = ReferenceResolution;
            Anchor = UIAnchor::StretchAll;
        }
        std::string_view GetTypeName() const override { return "Canvas"; }
        glm::vec2 ReferenceResolution{ 1920.0f, 1080.0f };
        bool SetPropertyVec2(std::string_view propName, const glm::vec2& val) override;
        bool SetPropertyFloat(std::string_view propName, float val) override;
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
        explicit UIBoxContainerNode(bool vertical = true) : IsVertical(vertical) {
            Size = glm::vec2(0.0f);
        }
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
