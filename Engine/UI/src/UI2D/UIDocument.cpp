#include <Axle/UI/UIDocument.hpp>
#include <Axle/UI/UIParser.hpp>
#include <algorithm>

namespace Axle {

    UIDocument::UIDocument() {
        m_DefaultFont = std::make_shared<UIFont>();
    }

    void UIDocument::Clear() {
        m_RootNode = nullptr;
        m_NodeArena.clear();
        m_IDMap.clear();
        m_Bindings.clear();
    }

    void UIDocument::RebuildIDMap() {
        m_IDMap.clear();
        for (const auto& node : m_NodeArena) {
            if (node && !node->ID.empty()) {
                m_IDMap[node->ID] = node.get();
            }
        }
    }

    UINode* UIDocument::FindNodeByID(const std::string& id) const {
        auto it = m_IDMap.find(id);
        if (it != m_IDMap.end()) {
            return it->second;
        }
        return nullptr;
    }

    void UIDocument::UpdateLayout(const glm::vec2& viewportSize) {
        if (!m_RootNode) return;

        float refW = 1920.0f;
        float refH = 1080.0f;

        auto* canvasNode = dynamic_cast<UICanvasNode*>(m_RootNode);
        if (canvasNode) {
            if (canvasNode->ReferenceResolution.x > 0.0f) refW = canvasNode->ReferenceResolution.x;
            if (canvasNode->ReferenceResolution.y > 0.0f) refH = canvasNode->ReferenceResolution.y;
        }

        m_ComputedDPIScale = std::min(viewportSize.x / refW, viewportSize.y / refH);
        if (m_ComputedDPIScale <= 0.0f) m_ComputedDPIScale = 1.0f;

        // Pass 1: Bottom-Up Measure
        m_RootNode->OnMeasure(m_DefaultFont);

        // Pass 2: Top-Down Arrange
        UIRect rootRect{ 0.0f, 0.0f, refW, refH };
        m_RootNode->OnArrange(rootRect);
    }

    void UIDocument::Render(Canvas2D& canvas) {
        if (!m_RootNode) return;

        // Render recursive through node hierarchy
        RenderRecursive(m_RootNode, canvas);
    }

    void UIDocument::RenderRecursive(UINode* node, Canvas2D& canvas) {
        if (!node || !node->Visible) return;

        node->OnDraw(canvas, m_DefaultFont);

        for (UINode* child : node->Children) {
            RenderRecursive(child, canvas);
        }
    }

    void UIDocument::EvaluateBindings(const UIDataContext& context) {
        (void)context;
        // Detailed binding evaluation is fully implemented in Step 5
    }

    bool UIDocument::ProcessInput(const glm::vec2& mousePos, bool mouseDown, bool mouseClicked, const UIDataContext& context) {
        (void)mousePos;
        (void)mouseDown;
        (void)mouseClicked;
        (void)context;
        // Input hit-testing is fully implemented in Step 5
        return false;
    }

    bool UIDocument::NavigateFocus(std::string_view direction) {
        (void)direction;
        return false;
    }

    bool UIDocument::ActivateFocusedButton(const UIDataContext& context) {
        (void)context;
        return false;
    }

    void UIDocument::SetFilePath(const std::string& path) {
        m_FilePath = path;
        if (std::filesystem::exists(m_FilePath)) {
            m_LastWriteTime = std::filesystem::last_write_time(m_FilePath);
        }
    }

    bool UIDocument::CheckAndHotReload() {
        if (m_FilePath.empty() || !std::filesystem::exists(m_FilePath)) return false;
        auto currentWriteTime = std::filesystem::last_write_time(m_FilePath);
        if (currentWriteTime > m_LastWriteTime) {
            m_LastWriteTime = currentWriteTime;
            return UIParser::ParseFile(m_FilePath, *this);
        }
        return false;
    }

} // namespace Axle
