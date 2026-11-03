#pragma once

#include <Axle/AnimationDocumentCommon.hpp>
#include <functional>
#include <optional>
#include <memory>

namespace Axle {

    struct ControllerEditCommand {
        std::string Label;
        AnimatorControllerDefinition Before;
        AnimatorControllerDefinition After;
        AnimatorControllerSelection BeforeSelection;
        AnimatorControllerSelection AfterSelection;
        uint64_t BeforeRevision = 0;
        uint64_t AfterRevision = 0;
        size_t EstimatedSizeBytes = 0;
    };

    class AnimatorEditorDocument {
    public:
        AnimatorEditorDocument();
        ~AnimatorEditorDocument() = default;

        // Non-copyable, movable
        AnimatorEditorDocument(const AnimatorEditorDocument&) = delete;
        AnimatorEditorDocument& operator=(const AnimatorEditorDocument&) = delete;
        AnimatorEditorDocument(AnimatorEditorDocument&&) noexcept = default;
        AnimatorEditorDocument& operator=(AnimatorEditorDocument&&) noexcept = default;

        // Lifecycle & File IO
        LifecycleResult NewController(const std::string& controllerName = "NewController");
        LifecycleResult OpenController(const std::string& filePath, std::string* outError = nullptr);
        LifecycleResult Close();
        LifecycleResult Reload(std::string* outError = nullptr);

        LifecycleResult ConfirmDiscardAndProceed(std::string* outError = nullptr);
        LifecycleResult SaveAndProceed(std::string* outError = nullptr);
        LifecycleResult CancelPendingAction();

        bool Save(std::string* outError = nullptr);
        bool SaveAs(const std::string& newPath, std::string* outError = nullptr);

        // Document State Queries
        const AnimatorControllerDefinition& GetDefinition() const { return m_Definition; }
        const std::string& GetFilePath() const { return m_FilePath; }
        bool IsDirty() const { return m_CurrentRevision != m_SavedRevision; }
        uint64_t GetCurrentRevision() const { return m_CurrentRevision; }
        uint64_t GetSavedRevision() const { return m_SavedRevision; }
        DocumentPendingAction GetPendingAction() const { return m_PendingAction; }
        const std::string& GetPendingPath() const { return m_PendingPath; }
        AnimationDocumentStats GetStats() const;

        // Selection by stable authored ID
        const AnimatorControllerSelection& GetSelection() const { return m_Selection; }
        void SelectState(const AnimationId& stateId);
        void SelectTransition(const AnimationId& transitionId);
        void SelectParameter(const AnimationId& parameterId);
        void ClearSelection();

        // Transactional History API
        bool BeginTransaction();
        bool IsInTransaction() const { return m_InTransaction; }
        AnimatorControllerDefinition* GetTransactionCandidate();
        bool CommitTransaction(const std::string& label = "Edit Controller", std::string* outError = nullptr);
        void CancelTransaction();
        bool Modify(const std::string& label, const std::function<bool(AnimatorControllerDefinition&)>& mutator, std::string* outError = nullptr);

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

        // Controller-level editing helpers (work inside or outside transactions)
        bool SetControllerProperties(const std::string& name, const std::string& skeletonRef, const std::string& signature = "");
        bool SetEntryState(const AnimationId& stateId);

        bool AddParameter(const ParameterDefinition& param);
        bool RemoveParameter(const AnimationId& paramId);
        bool UpdateParameter(const ParameterDefinition& param);
        bool ReorderParameters(size_t fromIndex, size_t toIndex);

        bool AddState(const StateDefinition& state);
        bool RemoveState(const AnimationId& stateId);
        bool UpdateState(const StateDefinition& state);
        bool ReorderStates(size_t fromIndex, size_t toIndex);

        bool AddTransition(const TransitionDefinition& trans);
        bool RemoveTransition(const AnimationId& transId);
        bool UpdateTransition(const TransitionDefinition& trans);
        bool ReorderTransitions(size_t fromIndex, size_t toIndex);

    private:
        static AnimatorControllerDefinition CreateNeutralController(const std::string& name);
        static AnimatorControllerDefinition RelocateDefinition(const AnimatorControllerDefinition& def, const std::string& oldPath, const std::string& newPath);
        void EnforceHistoryLimits();
        void RepairSelection(const AnimatorControllerDefinition& def);
        LifecycleResult ExecutePendingAction(std::string* outError);

    private:
        AnimatorControllerDefinition m_Definition;
        std::string m_FilePath;
        AnimatorControllerSelection m_Selection;

        uint64_t m_CurrentRevision = 1;
        uint64_t m_SavedRevision = 1;
        uint64_t m_NextRevisionToken = 1;

        DocumentPendingAction m_PendingAction = DocumentPendingAction::None;
        std::string m_PendingPath;
        std::string m_PendingName;
        uint64_t m_PendingRevision = 0;

        bool m_InTransaction = false;
        AnimatorControllerDefinition m_WorkingCandidate;
        AnimatorControllerDefinition m_TransactionBefore;
        AnimatorControllerSelection m_TransactionBeforeSelection;

        std::vector<ControllerEditCommand> m_UndoStack;
        std::vector<ControllerEditCommand> m_RedoStack;

        size_t m_MaxCommands = 100;
        size_t m_MaxMemoryBytes = 32 * 1024 * 1024; // 32 MB
        size_t m_MaxSingleCommandBytes = 8 * 1024 * 1024; // 8 MB
    };

}

