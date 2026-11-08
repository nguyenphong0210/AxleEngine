#include "DocumentGuard.hpp"
#include <algorithm>
#include <cwctype>
#include <exception>
namespace Axle {
namespace {
bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a.empty() || b.empty()) return false;
    std::error_code error;
    if (std::filesystem::equivalent(a, b, error) && !error) return true;
    error.clear(); auto left = std::filesystem::weakly_canonical(a, error);
    if (error) { error.clear(); left = std::filesystem::absolute(a, error).lexically_normal(); }
    error.clear(); auto right = std::filesystem::weakly_canonical(b, error);
    if (error) { error.clear(); right = std::filesystem::absolute(b, error).lexically_normal(); }
#if defined(_WIN32)
    auto l = left.wstring(), r = right.wstring();
    std::transform(l.begin(), l.end(), l.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    std::transform(r.begin(), r.end(), r.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return l == r;
#else
    return left == right;
#endif
}
}
bool DocumentGuard::Check(std::string action, std::string title, std::function<void()> continuation, std::string documentId) {
    if (action.empty() || !continuation) { m_Error = "A document action and continuation are required."; return false; }
    if (m_Approved == action && !action.empty()) { m_Approved.clear(); return true; }
    if (IsPending()) { m_Error = "Finish or cancel the current document decision first."; return false; }
    m_Error.clear();
    try {
        auto documents = m_Collect ? m_Collect() : std::vector<GuardedDocument>{};
        std::erase_if(documents, [&](const auto& document) { return (!documentId.empty() && document.Id != documentId) || !document.IsDirty; });
        if (std::none_of(documents.begin(), documents.end(), [](const auto& document) { return document.IsDirty(); })) return true;
        m_DocumentId = std::move(documentId);
        m_Documents = std::move(documents); m_Action = std::move(action); m_Title = std::move(title); m_Continue = std::move(continuation);
    } catch (const std::exception& error) { m_Error = error.what(); }
    return false;
}
bool DocumentGuard::Resolve(Choice choice) {
    if (!IsPending()) return false;
    if (choice == Choice::Cancel) { m_Action.clear(); m_Title.clear(); m_Continue = {}; m_Documents.clear(); m_DocumentId.clear(); m_Error.clear(); return false; }
    m_Error.clear();
    struct ResetApproval { std::string& Value; ~ResetApproval() { Value.clear(); } } reset{m_Approved};
    try {
        if (choice == Choice::Save) {
            if (!ValidateSaveTargets(m_Documents, m_Error)) return false;
        }
        for (const auto& document : m_Documents) {
            if (!document.IsDirty()) continue;
            const auto& operation = choice == Choice::Save ? document.Save : document.Discard;
            std::string error;
            if (!operation || !operation(error)) { m_Error = document.Label + ": " + (error.empty() ? "operation failed or was cancelled" : error); return false; }
        }
        // A save may dirty an initially clean scene or introduce another owned
        // document. Recollect the entire action scope before issuing approval.
        auto current = m_Collect ? m_Collect() : m_Documents;
        std::erase_if(current, [&](const auto& document) { return (!m_DocumentId.empty() && document.Id != m_DocumentId) || !document.IsDirty; });
        m_Documents = std::move(current);
        if (std::any_of(m_Documents.begin(), m_Documents.end(), [](const auto& document) { return document.IsDirty(); })) {
            m_Error = "A document changed while applying the decision. Review the remaining edits before continuing."; return false;
        }
        auto continuation = std::move(m_Continue); m_Approved = std::move(m_Action);
        m_Action.clear(); m_Title.clear(); m_Documents.clear(); m_DocumentId.clear();
        if (continuation) continuation();
        m_Approved.clear(); return true;
    } catch (const std::exception& error) { m_Error = error.what(); return false; }
}
bool DocumentGuard::ValidateSaveTargets(const std::vector<GuardedDocument>& documents, std::string& error) {
    for (std::size_t i = 0; i < documents.size(); ++i) for (std::size_t j = i + 1; j < documents.size(); ++j) {
        if (documents[i].IsDirty && documents[j].IsDirty && documents[i].IsDirty() && documents[j].IsDirty() && SamePath(documents[i].Path, documents[j].Path)) {
            error = "Two edited documents target the same file: " + documents[i].Label + " and " + documents[j].Label + ". Save or close one before continuing."; return false;
        }
    }
    return true;
}
}
