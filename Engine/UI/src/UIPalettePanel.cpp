#include <Axle/UIPalettePanel.hpp>
#include <Axle/UI/UINode.hpp>
#include <Axle/Components.hpp>
#include <Axle/Log.hpp>
#include <algorithm>
#include <iostream>

namespace Axle {

    UIPalettePanel::UIPalettePanel() = default;

    void UIPalettePanel::SetContext(Scene* scene) {
        m_Scene = scene;
    }

    const std::vector<UIWidgetDescriptor>& UIPalettePanel::GetStockWidgets() {
        static const std::vector<UIWidgetDescriptor> s_Widgets = {
            {
                UIWidgetType::Canvas,
                "Canvas",
                "[#]",
                "Containers",
                "Root rendering surface and responsive coordinate space for UI elements",
                glm::vec2(1920.0f, 1080.0f),
                ImVec4(0.20f, 0.50f, 0.95f, 1.0f)
            },
            {
                UIWidgetType::Panel,
                "Panel",
                "[ ]",
                "Containers",
                "Card container with customizable fill color, border outline and corner radius",
                glm::vec2(300.0f, 200.0f),
                ImVec4(0.35f, 0.65f, 0.95f, 1.0f)
            },
            {
                UIWidgetType::Text,
                "Text",
                "[T]",
                "Basic",
                "Hardware-accelerated multi-channel signed distance field (MSDF) typography",
                glm::vec2(200.0f, 40.0f),
                ImVec4(0.95f, 0.75f, 0.15f, 1.0f)
            },
            {
                UIWidgetType::Image,
                "Image",
                "[IMG]",
                "Basic",
                "Texture quad with tint color and bindless GPU texture sampling",
                glm::vec2(128.0f, 128.0f),
                ImVec4(0.85f, 0.35f, 0.85f, 1.0f)
            },
            {
                UIWidgetType::Button,
                "Button",
                "[BTN]",
                "Controls",
                "Interactive push button with Normal, Hover and Pressed styling & action triggers",
                glm::vec2(180.0f, 48.0f),
                ImVec4(0.15f, 0.75f, 0.45f, 1.0f)
            },
            {
                UIWidgetType::ProgressBar,
                "ProgressBar",
                "[===]",
                "Gauges",
                "Telemetry progress bar with dynamic fill percentage and rounded corners",
                glm::vec2(260.0f, 26.0f),
                ImVec4(0.10f, 0.85f, 0.65f, 1.0f)
            },
            {
                UIWidgetType::RadialGauge,
                "RadialGauge",
                "[O]",
                "Gauges",
                "Circular arc sweep meter for speedometers and tachometer RPM redline dials",
                glm::vec2(180.0f, 180.0f),
                ImVec4(0.00f, 0.95f, 0.85f, 1.0f)
            }
        };
        return s_Widgets;
    }

    Entity UIPalettePanel::GetOrCreateCanvasEntity(Scene* scene) {
        if (!scene) return {};

        // 1. Check if an entity with UICanvasComponent already exists
        auto view = scene->GetRegistry().view<UICanvasComponent>();
        for (auto entityID : view) {
            Entity ent{ entityID, scene };
            if (ent && ent.GetComponent<UICanvasComponent>().Document) {
                return ent;
            }
        }

        // 2. Otherwise auto-create a default Canvas Entity
        Entity canvasEntity = scene->CreateEntity("UICanvas");
        auto& uic = canvasEntity.AddComponent<UICanvasComponent>();
        uic.Document = std::make_shared<UIDocument>();
        uic.Document->CreateNode<UICanvasNode>(nullptr);
        AXLE_INFO("[UI Palette] Auto-created new Entity 'UICanvas' with root UICanvasNode");
        return canvasEntity;
    }

    glm::vec2 UIPalettePanel::ScreenToCanvas(
        const glm::vec2& screenPos,
        const glm::vec2& viewportMin,
        const glm::vec2& viewportSize,
        const glm::vec2& canvasRes
    ) {
        float scaleX = (canvasRes.x > 0.0f) ? (viewportSize.x / canvasRes.x) : 1.0f;
        float scaleY = (canvasRes.y > 0.0f) ? (viewportSize.y / canvasRes.y) : 1.0f;
        float scale = std::min(scaleX, scaleY);
        if (scale < 1e-4f) scale = 1.0f;

        glm::vec2 scaledSize = canvasRes * scale;
        glm::vec2 offset = viewportMin + (viewportSize - scaledSize) * 0.5f;

        glm::vec2 canvasPos = (screenPos - offset) / scale;
        canvasPos.x = std::clamp(canvasPos.x, 0.0f, canvasRes.x);
        canvasPos.y = std::clamp(canvasPos.y, 0.0f, canvasRes.y);
        return canvasPos;
    }

    UINode* UIPalettePanel::SpawnWidget(
        UIDocument* doc,
        UIWidgetType type,
        UINode* parent,
        const glm::vec2& canvasPos
    ) {
        if (!doc) return nullptr;

        UINode* targetParent = parent ? parent : doc->GetRootNode();
        if (!targetParent && type != UIWidgetType::Canvas) {
            // Auto-create root Canvas if document is empty
            targetParent = doc->CreateNode<UICanvasNode>(nullptr);
        }

        UINode* node = nullptr;
        std::string suffix = std::to_string(doc->GetNodeCount() + 1);

        switch (type) {
            case UIWidgetType::Canvas: {
                if (!doc->GetRootNode()) {
                    node = doc->CreateNode<UICanvasNode>(nullptr);
                    node->ID = "Canvas";
                } else {
                    node = doc->GetRootNode();
                }
                break;
            }
            case UIWidgetType::Panel: {
                auto* p = doc->CreateNode<UIRectangleNode>(targetParent);
                p->ID = "Panel_" + suffix;
                p->SetUseRectTransform(true);
                p->Transform.AnchorMin = { 0.5f, 0.5f };
                p->Transform.AnchorMax = { 0.5f, 0.5f };
                p->Transform.Pivot = { 0.5f, 0.5f };
                p->Transform.SizeDelta = { 300.0f, 200.0f };
                p->Style.BackgroundColor = { 0.12f, 0.16f, 0.23f, 0.88f };
                p->Style.BorderColor = { 0.35f, 0.45f, 0.60f, 0.90f };
                p->Style.BorderWidth = 1.5f;
                p->Style.CornerRadius = 8.0f;
                node = p;
                break;
            }
            case UIWidgetType::Text: {
                auto* t = doc->CreateNode<UITextNode>(targetParent);
                t->ID = "Text_" + suffix;
                t->SetUseRectTransform(true);
                t->Transform.AnchorMin = { 0.5f, 0.5f };
                t->Transform.AnchorMax = { 0.5f, 0.5f };
                t->Transform.Pivot = { 0.5f, 0.5f };
                t->Transform.SizeDelta = { 200.0f, 40.0f };
                t->Text = "Label";
                t->FontSize = 24.0f;
                t->Color = { 1.0f, 1.0f, 1.0f, 1.0f };
                t->AutoSize = false;
                node = t;
                break;
            }
            case UIWidgetType::Image: {
                auto* img = doc->CreateNode<UIImageNode>(targetParent);
                img->ID = "Image_" + suffix;
                img->SetUseRectTransform(true);
                img->Transform.AnchorMin = { 0.5f, 0.5f };
                img->Transform.AnchorMax = { 0.5f, 0.5f };
                img->Transform.Pivot = { 0.5f, 0.5f };
                img->Transform.SizeDelta = { 128.0f, 128.0f };
                img->TintColor = { 1.0f, 1.0f, 1.0f, 1.0f };
                node = img;
                break;
            }
            case UIWidgetType::Button: {
                auto* b = doc->CreateNode<UIButtonNode>(targetParent);
                b->ID = "Button_" + suffix;
                b->SetUseRectTransform(true);
                b->Transform.AnchorMin = { 0.5f, 0.5f };
                b->Transform.AnchorMax = { 0.5f, 0.5f };
                b->Transform.Pivot = { 0.5f, 0.5f };
                b->Transform.SizeDelta = { 180.0f, 48.0f };
                b->NormalColor = { 0.15f, 0.22f, 0.32f, 0.95f };
                b->HoverColor = { 0.22f, 0.35f, 0.52f, 1.0f };
                b->PressedColor = { 0.08f, 0.55f, 0.85f, 1.0f };
                b->Style.CornerRadius = 6.0f;
                b->Style.BorderWidth = 1.0f;
                b->Style.BorderColor = { 0.30f, 0.50f, 0.75f, 0.80f };

                auto* label = doc->CreateNode<UITextNode>(b);
                label->ID = b->ID + "_Label";
                label->SetUseRectTransform(true);
                label->Transform.AnchorMin = { 0.5f, 0.5f };
                label->Transform.AnchorMax = { 0.5f, 0.5f };
                label->Transform.Pivot = { 0.5f, 0.5f };
                label->Transform.AnchoredPosition = { 0.0f, 0.0f };
                label->Transform.SizeDelta = { 160.0f, 32.0f };
                label->Text = "Button";
                label->FontSize = 20.0f;
                label->Color = { 1.0f, 1.0f, 1.0f, 1.0f };
                node = b;
                break;
            }
            case UIWidgetType::ProgressBar: {
                auto* bar = doc->CreateNode<UIProgressBarNode>(targetParent);
                bar->ID = "ProgressBar_" + suffix;
                bar->SetUseRectTransform(true);
                bar->Transform.AnchorMin = { 0.5f, 0.5f };
                bar->Transform.AnchorMax = { 0.5f, 0.5f };
                bar->Transform.Pivot = { 0.5f, 0.5f };
                bar->Transform.SizeDelta = { 260.0f, 26.0f };
                bar->Value = 0.5f;
                bar->FillColor = { 0.06f, 0.72f, 0.50f, 1.0f };
                bar->BackgroundColor = { 0.12f, 0.16f, 0.23f, 0.90f };
                bar->CornerRadius = 6.0f;
                node = bar;
                break;
            }
            case UIWidgetType::RadialGauge: {
                auto* gauge = doc->CreateNode<UIRadialGaugeNode>(targetParent);
                gauge->ID = "RadialGauge_" + suffix;
                gauge->SetUseRectTransform(true);
                gauge->Transform.AnchorMin = { 0.5f, 0.5f };
                gauge->Transform.AnchorMax = { 0.5f, 0.5f };
                gauge->Transform.Pivot = { 0.5f, 0.5f };
                gauge->Transform.SizeDelta = { 180.0f, 180.0f };
                gauge->Value = 0.5f;
                gauge->StartAngle = -225.0f;
                gauge->EndAngle = 45.0f;
                gauge->Thickness = 16.0f;
                gauge->Color = { 0.0f, 0.96f, 0.83f, 1.0f };
                gauge->TrackColor = { 0.12f, 0.16f, 0.23f, 0.60f };
                node = gauge;
                break;
            }
            default:
                break;
        }

        // Apply drop position if specified
        if (node && type != UIWidgetType::Canvas) {
            auto* rootCanvas = dynamic_cast<UICanvasNode*>(doc->GetRootNode());
            glm::vec2 canvasRes = rootCanvas ? rootCanvas->ReferenceResolution : glm::vec2(1920.0f, 1080.0f);
            UIRect parentRect{ 0.0f, 0.0f, canvasRes.x, canvasRes.y };

            if (canvasPos.x >= 0.0f && canvasPos.y >= 0.0f) {
                UIRect targetRect{
                    canvasPos.x - node->Transform.SizeDelta.x * 0.5f,
                    canvasPos.y - node->Transform.SizeDelta.y * 0.5f,
                    node->Transform.SizeDelta.x,
                    node->Transform.SizeDelta.y
                };
                node->Transform.SetRect(targetRect, parentRect);
            }
            doc->UpdateLayout(canvasRes);
        }

        doc->RebuildIDMap();
        doc->SetDirty(true);
        AXLE_INFO("[UI Palette] Spawned stock widget '{0}' (ID: '{1}')", node ? node->GetTypeName() : "None", node ? node->ID : "");
        return node;
    }

    UINode* UIPalettePanel::SpawnWidgetByName(
        UIDocument* doc,
        std::string_view typeName,
        UINode* parent,
        const glm::vec2& canvasPos
    ) {
        if (typeName == "Canvas")       return SpawnWidget(doc, UIWidgetType::Canvas, parent, canvasPos);
        if (typeName == "Panel" || typeName == "Rectangle") return SpawnWidget(doc, UIWidgetType::Panel, parent, canvasPos);
        if (typeName == "Text")         return SpawnWidget(doc, UIWidgetType::Text, parent, canvasPos);
        if (typeName == "Image")        return SpawnWidget(doc, UIWidgetType::Image, parent, canvasPos);
        if (typeName == "Button")       return SpawnWidget(doc, UIWidgetType::Button, parent, canvasPos);
        if (typeName == "ProgressBar")  return SpawnWidget(doc, UIWidgetType::ProgressBar, parent, canvasPos);
        if (typeName == "RadialGauge")  return SpawnWidget(doc, UIWidgetType::RadialGauge, parent, canvasPos);

        AXLE_WARN("[UI Palette] Unknown widget type name '{0}'", typeName);
        return nullptr;
    }

    void UIPalettePanel::OnImGuiRender(bool* pOpen) {
        if (pOpen && !*pOpen) return;

        ImGui::SetNextWindowSize(ImVec2(280.0f, 480.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("UI Library", nullptr)) {
            ImGui::End();
            return;
        }

        // Unity UI Library Search Bar
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.5f);
        m_Filter.Draw("##WidgetFilter", ImGui::GetContentRegionAvail().x);
        if (!m_Filter.IsActive()) {
            ImVec2 minPos = ImGui::GetItemRectMin();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(minPos.x + 8.0f, minPos.y + 4.0f), IM_COL32(130, 130, 130, 200), "Search elements...");
        }
        ImGui::PopStyleVar();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const auto& widgets = GetStockWidgets();
        std::string lastCategory = "";
        bool categoryOpen = true;

        // Resolve active target canvas entity
        Entity activeCanvasEntity = GetOrCreateCanvasEntity(m_Scene);
        UIDocument* targetDoc = nullptr;
        if (activeCanvasEntity && activeCanvasEntity.HasComponent<UICanvasComponent>()) {
            targetDoc = activeCanvasEntity.GetComponent<UICanvasComponent>().Document.get();
        }

        for (const auto& widget : widgets) {
            if (!m_Filter.PassFilter(widget.Name.c_str()) && !m_Filter.PassFilter(widget.Category.c_str())) {
                continue;
            }

            // Unity Foldout Category Header
            if (widget.Category != lastCategory) {
                lastCategory = widget.Category;
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
                categoryOpen = ImGui::CollapsingHeader(widget.Category.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::PopStyleColor(2);
            }

            if (!categoryOpen) {
                continue;
            }

            ImGui::PushID(widget.Name.c_str());

            // Unity UI Library Compact Element Card
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.19f, 0.19f, 0.19f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.23f, 0.47f, 0.72f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));

            float cardWidth = ImGui::GetContentRegionAvail().x;
            float cardHeight = 30.0f;
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::Button("##WidgetCardBtn", ImVec2(cardWidth, cardHeight));

            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);

            // Draw custom Unity-style contents on top of button
            ImDrawList* dl = ImGui::GetWindowDrawList();
            
            // Icon Badge with translucent accent tint
            ImVec2 badgeMin = ImVec2(cursorPos.x + 4.0f, cursorPos.y + 4.0f);
            ImVec2 badgeMax = ImVec2(cursorPos.x + 26.0f, cursorPos.y + 26.0f);
            ImU32 badgeBgColor = ImColor(widget.AccentColor.x, widget.AccentColor.y, widget.AccentColor.z, 0.20f);
            ImU32 badgeTextColor = ImColor(widget.AccentColor.x, widget.AccentColor.y, widget.AccentColor.z, 1.0f);
            dl->AddRectFilled(badgeMin, badgeMax, badgeBgColor, 3.0f);

            // Centered icon text inside badge
            ImVec2 iconSize = ImGui::CalcTextSize(widget.Icon.c_str());
            ImVec2 iconPos = ImVec2(badgeMin.x + (22.0f - iconSize.x) * 0.5f, badgeMin.y + (22.0f - iconSize.y) * 0.5f);
            dl->AddText(iconPos, badgeTextColor, widget.Icon.c_str());

            // Widget Name
            dl->AddText(ImVec2(cursorPos.x + 34.0f, cursorPos.y + 6.0f), IM_COL32(220, 220, 220, 255), widget.Name.c_str());

            // Right hint text (type tag)
            ImVec2 hintSize = ImGui::CalcTextSize(widget.Category.c_str());
            float hintX = cursorPos.x + cardWidth - hintSize.x - 8.0f;
            if (hintX > cursorPos.x + 34.0f + 70.0f) {
                dl->AddText(ImVec2(hintX, cursorPos.y + 7.0f), IM_COL32(110, 110, 110, 255), widget.Category.c_str());
            }

            // Drag and Drop Source
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload(DRAG_DROP_PAYLOAD_ID, widget.Name.c_str(), widget.Name.size() + 1);
                ImGui::PushStyleColor(ImGuiCol_Text, widget.AccentColor);
                ImGui::Text("%s %s", widget.Icon.c_str(), widget.Name.c_str());
                ImGui::PopStyleColor();
                ImGui::TextDisabled("Drop onto Viewport Canvas or Scene Hierarchy");
                ImGui::EndDragDropSource();
            }

            // Click to spawn directly into target canvas
            if (clicked && targetDoc) {
                SpawnWidget(targetDoc, widget.Type);
            }

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextColored(widget.AccentColor, "%s (%s)", widget.Name.c_str(), widget.Category.c_str());
                ImGui::Separator();
                ImGui::TextUnformatted(widget.Description.c_str());
                ImGui::TextDisabled("Default Size: %.0f x %.0f px", widget.DefaultSize.x, widget.DefaultSize.y);
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "Drag onto Viewport canvas or click to add");
                ImGui::EndTooltip();
            }

            ImGui::PopID();
        }

        ImGui::End();
    }

} // namespace Axle
