#pragma once

#include <Axle/AnimationDocumentCommon.hpp>
#include <functional>
#include <optional>
#include <memory>

namespace Axle {

    struct ClipEditCommand {
        std::string Label;
        AnimationClipDefinition Before;
        AnimationClipDefinition After;
        AnimationClipSelection BeforeSelection;
        AnimationClipSelection AfterSelection;
        uint64_t BeforeRevision = 0;
        uint64_t AfterRevision = 0;
        size_t EstimatedSizeBytes = 0;
    };

    class AnimationEditorDocument {
    public:
        AnimationEditorDocument();
        ~AnimationEditorDocument() = default;

        // Non-copyable, movable
        AnimationEditorDocument(const AnimationEditorDocument&) = delete;
        AnimationEditorDocument& operator=(const AnimationEditorDocument&) = delete;
        AnimationEditorDocument(AnimationEditorDocument&&) noexcept = default;
        AnimationEditorDocument& operator=(AnimationEditorDocument&&) noexcept = default;

        // Lifecycle & File IO
        LifecycleResult NewClip(const std::string& clipName = "NewClip");
        LifecycleResult OpenClip(const std::string& filePath, std::string* outError = nullptr);
        LifecycleResult Close();
        LifecycleResult Reload(std::string* outError = nullptr);

        LifecycleResult ConfirmDiscardAndProceed(std::string* outError = nullptr);
        LifecycleResult SaveAndProceed(std::string* outError = nullptr);
        LifecycleResult CancelPendingAction();

        bool Save(std::string* outError = nullptr);
        bool SaveAs(const std::string& newPath, std::string* outError = nullptr);

        // Document State Queries
        const AnimationClipDefinition& GetDefinition() const { return m_Definition; }
        const std::string& GetFilePath() const { return m_FilePath; }
        bool IsDirty() const { return m_CurrentRevision != m_SavedRevision; }
        uint64_t GetCurrentRevision() const { return m_CurrentRevision; }
        uint64_t GetSavedRevision() const { return m_SavedRevision; }
        DocumentPendingAction GetPendingAction() const { return m_PendingAction; }
        const std::string& GetPendingPath() const { return m_PendingPath; }
        AnimationDocumentStats GetStats() const;

        // Selection
        const AnimationClipSelection& GetSelection() const { return m_Selection; }
        void SelectChannel(const std::string& boneName, AnimationChannelType channelType = AnimationChannelType::Translation);
        void SelectEvent(const AnimationId& eventId);
        void ClearSelection();

        // Transactional History API
        bool BeginTransaction();
        bool IsInTransaction() const { return m_InTransaction; }
        AnimationClipDefinition* GetTransactionCandidate();
        bool CommitTransaction(const std::string& label = "Edit Clip", std::string* outError = nullptr);
        void CancelTransaction();
        bool Modify(const std::string& label, const std::function<bool(AnimationClipDefinition&)>& mutator, std::string* outError = nullptr);

        bool Undo();
        bool Redo();
        bool CanUndo() const { return !m_InTransaction && !m_UndoStack.empty(); }
        bool CanRedo() const { return !m_InTransaction && !m_RedoStack.empty(); }

        // History Limits
        void SetHistoryLimits(size_t maxCommands, size_t maxMemoryBytes, size_t maxSingleCommandBytes);
        size_t GetMaxHistoryCommands() const { return m_MaxCommands; }
        size_t GetMaxHistoryMemoryBytes() const { return m_MaxMemoryBytes; }
        size_t GetMaxSingleCommandBytes() const { return m_MaxSingleCommandBytes; }
        size_t GetHistoryMemoryUsage() const;

        // Clip-level editing helpers (work inside or outside transactions)
        bool SetClipProperties(const std::string& name, float duration, float frameRate, const std::string& skeletonRef, const std::string& signature = "");
        bool AddChannel(const AnimationChannel& channel);
        bool RemoveChannel(const std::string& boneName);
        bool ReorderChannels(size_t fromIndex, size_t toIndex);
        bool UpdateChannel(const AnimationChannel& channel);
        bool AddEvent(const AnimationEventDefinition& event);
        bool RemoveEvent(const AnimationId& eventId);
        bool ReorderEvents(size_t fromIndex, size_t toIndex);
        bool UpdateEvent(const AnimationEventDefinition& event);

    private:
        static AnimationClipDefinition CreateNeutralClip(const std::string& name);
        static AnimationClipDefinition RelocateDefinition(const AnimationClipDefinition& def, const std::string& oldPath, const std::string& newPath);
        void EnforceHistoryLimits();
        void RepairSelection(const AnimationClipDefinition& def);
        LifecycleResult ExecutePendingAction(std::string* outError);

    private:
        AnimationClipDefinition m_Definition;
        std::string m_FilePath;
        AnimationClipSelection m_Selection;

        uint64_t m_CurrentRevision = 1;
        uint64_t m_SavedRevision = 1;
        uint64_t m_NextRevisionToken = 1;

        DocumentPendingAction m_PendingAction = DocumentPendingAction::None;
        std::string m_PendingPath;
        std::string m_PendingName;
        uint64_t m_PendingRevision = 0;

        bool m_InTransaction = false;
        AnimationClipDefinition m_WorkingCandidate;
        AnimationClipDefinition m_TransactionBefore;
        AnimationClipSelection m_TransactionBeforeSelection;

        std::vector<ClipEditCommand> m_UndoStack;
        std::vector<ClipEditCommand> m_RedoStack;

        size_t m_MaxCommands = 100;
        size_t m_MaxMemoryBytes = 32 * 1024 * 1024; // 32 MB
        size_t m_MaxSingleCommandBytes = 8 * 1024 * 1024; // 8 MB
    };

}

