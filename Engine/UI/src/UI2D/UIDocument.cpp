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
        for (const auto& binding : m_Bindings) {
            if (!binding.TargetNode) continue;

            if (binding.Type == UIBindingType::Direct) {
                const UIDataValue* val = context.Get(binding.SourceKey);
                if (!val) continue;

                std::visit([&](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, float>) {
                        binding.TargetNode->SetPropertyFloat(binding.PropertyName, arg);
                    } else if constexpr (std::is_same_v<T, int32_t>) {
                        binding.TargetNode->SetPropertyFloat(binding.PropertyName, static_cast<float>(arg));
                        binding.TargetNode->SetProperty(binding.PropertyName, std::to_string(arg));
                    } else if constexpr (std::is_same_v<T, bool>) {
                        binding.TargetNode->SetPropertyBool(binding.PropertyName, arg);
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        binding.TargetNode->SetProperty(binding.PropertyName, arg);
                    } else if constexpr (std::is_same_v<T, glm::vec4>) {
                        binding.TargetNode->SetPropertyVec4(binding.PropertyName, arg);
                    }
                }, *val);
            } else if (binding.Type == UIBindingType::Interpolated) {
                std::string result = binding.TemplateText;
                std::string placeholder = "{" + binding.SourceKey + "}";
                size_t pos = result.find(placeholder);
                if (pos != std::string::npos) {
                    const UIDataValue* val = context.Get(binding.SourceKey);
                    std::string rep = "";
                    if (val) {
                        std::visit([&](auto&& arg) {
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, float>) {
                                char buf[32];
                                snprintf(buf, sizeof(buf), "%.1f", arg);
                                rep = buf;
                            } else if constexpr (std::is_same_v<T, int32_t>) {
                                rep = std::to_string(arg);
                            } else if constexpr (std::is_same_v<T, bool>) {
                                rep = arg ? "true" : "false";
                            } else if constexpr (std::is_same_v<T, std::string>) {
                                rep = arg;
                            }
                        }, *val);
                    }
                    result.replace(pos, placeholder.length(), rep);
                    binding.TargetNode->SetProperty(binding.PropertyName, result);
                }
            } else if (binding.Type == UIBindingType::Ternary) {
                const UIDataValue* val = context.Get(binding.SourceKey);
                bool cond = false;
                if (val) {
                    std::visit([&](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, bool>) {
                            cond = arg;
                        } else if constexpr (std::is_same_v<T, int32_t>) {
                            cond = (arg != 0);
                        } else if constexpr (std::is_same_v<T, float>) {
                            cond = (arg > 0.0f);
                        } else if constexpr (std::is_same_v<T, std::string>) {
                            cond = (arg == "true" || arg == "1");
                        }
                    }, *val);
                }
                binding.TargetNode->SetProperty(binding.PropertyName, cond ? binding.TrueValue : binding.FalseValue);
            }
        }
    }

    bool UIDocument::ProcessInput(const glm::vec2& mousePos, bool mouseDown, bool mouseClicked, const UIDataContext& context) {
        bool handled = false;

        // Traverse in reverse order for topmost hit-testing
        for (auto it = m_NodeArena.rbegin(); it != m_NodeArena.rend(); ++it) {
            auto* btn = dynamic_cast<UIButtonNode*>(it->get());
            if (!btn || !btn->Visible) continue;

            if (!handled && btn->ComputedRect.Contains(mousePos)) {
                btn->IsHovered = true;
                btn->IsPressed = mouseDown;

                if (mouseClicked && !btn->OnClickAction.empty()) {
                    context.TriggerAction(btn->OnClickAction);
                }
                handled = true;
            } else {
                btn->IsHovered = false;
                btn->IsPressed = false;
            }
        }

        return handled;
    }

    bool UIDocument::NavigateFocus(std::string_view direction) {
        UIButtonNode* currentFocus = nullptr;
        UIButtonNode* firstButton = nullptr;

        for (const auto& node : m_NodeArena) {
            auto* btn = dynamic_cast<UIButtonNode*>(node.get());
            if (!btn || !btn->Visible) continue;
            if (!firstButton) firstButton = btn;
            if (btn->IsFocused) {
                currentFocus = btn;
                break;
            }
        }

        if (!currentFocus) {
            if (firstButton) {
                firstButton->IsFocused = true;
                return true;
            }
            return false;
        }

        std::string targetID;
        if (direction == "Up") targetID = currentFocus->NavUp;
        else if (direction == "Down") targetID = currentFocus->NavDown;
        else if (direction == "Left") targetID = currentFocus->NavLeft;
        else if (direction == "Right") targetID = currentFocus->NavRight;

        if (targetID.empty()) return false;

        UINode* targetNode = FindNodeByID(targetID);
        auto* targetBtn = dynamic_cast<UIButtonNode*>(targetNode);
        if (targetBtn && targetBtn->Visible) {
            currentFocus->IsFocused = false;
            targetBtn->IsFocused = true;
            return true;
        }

        return false;
    }

    bool UIDocument::ActivateFocusedButton(const UIDataContext& context) {
        for (const auto& node : m_NodeArena) {
            auto* btn = dynamic_cast<UIButtonNode*>(node.get());
            if (btn && btn->Visible && btn->IsFocused) {
                if (!btn->OnClickAction.empty()) {
                    return context.TriggerAction(btn->OnClickAction);
                }
                return true;
            }
        }
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
