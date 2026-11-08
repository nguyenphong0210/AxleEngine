#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace Axle {
struct GuardedDocument {
    std::string Id, Label;
    std::filesystem::path Path;
    std::function<bool()> IsDirty;
    std::function<bool(std::string&)> Save;
    std::function<bool(std::string&)> Discard;
};
class DocumentGuard final {
public:
    enum class Choice { Save, Discard, Cancel };
    using Collector = std::function<std::vector<GuardedDocument>()>;
    void SetCollector(Collector collect) { m_Collect = std::move(collect); }
    // An approved continuation calls the same action again and consumes one
    // matching approval. Unrelated actions cannot reuse that approval.
    bool Check(std::string actionId, std::string title, std::function<void()> continuation,
        std::string documentId = {});
    bool Resolve(Choice choice);
    static bool ValidateSaveTargets(const std::vector<GuardedDocument>& documents, std::string& error);
    bool IsPending() const { return !m_Action.empty(); }
    const std::string& Title() const { return m_Title; }
    const std::string& Error() const { return m_Error; }
    const std::vector<GuardedDocument>& Documents() const { return m_Documents; }
private:
    Collector m_Collect;
    std::string m_Action, m_Title, m_Error, m_Approved;
    std::string m_DocumentId;
    std::function<void()> m_Continue;
    std::vector<GuardedDocument> m_Documents;
};
}
