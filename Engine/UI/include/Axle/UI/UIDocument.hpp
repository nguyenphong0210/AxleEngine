#pragma once
#include <Axle/UI/UINode.hpp>
#include <Axle/UI/UIDataContext.hpp>
#include <memory>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace Axle {

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

        // Cache for smart dirty-checking & delta evaluation
        UIDataValue   LastValue{};
        bool          HasEvaluated = false;
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
            m_IsDirty = true;
            m_LayoutDirty = true;
            return rawPtr;
        }

        void Clear();
        void RebuildIDMap();
        UINode* FindNodeByID(const std::string& id) const;

        void UpdateLayout(const glm::vec2& viewportSize, bool force = false);
        void MarkLayoutDirty() { m_LayoutDirty = true; }
        bool IsLayoutDirty() const { return m_LayoutDirty; }
        void Render(Canvas2D& canvas);

        // Binding & Interaction Hooks
        void AddBinding(const UIPropertyBinding& binding) { m_Bindings.push_back(binding); }
        const std::vector<UIPropertyBinding>& GetBindings() const { return m_Bindings; }
        void EvaluateBindings(const UIDataContext& context);
        bool ProcessInput(const glm::vec2& mousePos, bool mouseDown, bool mouseClicked, const UIDataContext& context);
        bool NavigateFocus(std::string_view direction); // "Up", "Down", "Left", "Right"
        bool ActivateFocusedButton(const UIDataContext& context);

        // File Management, Dirty State & Serialization
        void SetFilePath(const std::string& path);
        const std::string& GetFilePath() const { return m_FilePath; }
        bool CheckAndHotReload();

        bool IsDirty() const { return m_IsDirty; }
        void SetDirty(bool dirty = true) { m_IsDirty = dirty; }

        std::string Serialize() const;
        bool SaveToFile(const std::string& path = "");
        bool LoadFromFile(const std::string& path);

        UINode* GetRootNode() const { return m_RootNode; }
        void SetRootNode(UINode* root) { m_RootNode = root; }
        float GetComputedDPIScale() const { return m_ComputedDPIScale; }
        const std::shared_ptr<UIFont>& GetDefaultFont() const { return m_DefaultFont; }
        void SetDefaultFont(const std::shared_ptr<UIFont>& font) { m_DefaultFont = font; }

        size_t GetNodeCount() const { return m_NodeArena.size(); }

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
        bool m_IsDirty = false;

        glm::vec2 m_LastLayoutViewportSize{ 0.0f, 0.0f };
        bool m_LayoutDirty = true;
    };

} // namespace Axle
