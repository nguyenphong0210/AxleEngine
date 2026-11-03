#include <Axle/AnimatorEditorDocument.hpp>
#include <Axle/AnimationAssetCodec.hpp>
#include <algorithm>

namespace Axle {

    AnimatorControllerDefinition AnimatorEditorDocument::CreateNeutralController(const std::string& name) {
        AnimatorControllerDefinition def;
        def.SchemaVersion = 1;
        def.Id = GenerateAnimationId();
        def.Name = name.empty() ? "NewController" : name;
        return def;
    }

    AnimatorControllerDefinition AnimatorEditorDocument::RelocateDefinition(const AnimatorControllerDefinition& def, const std::string& oldPath, const std::string& newPath) {
        AnimatorControllerDefinition relocated = def;
        relocated.SkeletonRef = RelocateReferencePath(def.SkeletonRef, oldPath, newPath);
        for (auto& s : relocated.States) {
            s.MotionAssetRef = RelocateReferencePath(s.MotionAssetRef, oldPath, newPath);
            for (auto& bp : s.BlendPoints) {
                bp.ClipRef = RelocateReferencePath(bp.ClipRef, oldPath, newPath);
            }
        }
        for (auto& ic : relocated.InlineClips) {
            ic.Reference = RelocateReferencePath(ic.Reference, oldPath, newPath);
        }
        return relocated;
    }

    AnimatorEditorDocument::AnimatorEditorDocument()
        : m_Definition(CreateNeutralController("NewController"))
        , m_CurrentRevision(1)
        , m_SavedRevision(1)
        , m_NextRevisionToken(1)
    {
    }

    AnimationDocumentStats AnimatorEditorDocument::GetStats() const {
        AnimationDocumentStats stats;
        stats.UndoCount = m_UndoStack.size();
        stats.RedoCount = m_RedoStack.size();
        stats.HistoryMemoryBytes = GetHistoryMemoryUsage();
        stats.CurrentRevision = m_CurrentRevision;
        stats.SavedRevision = m_SavedRevision;
        stats.IsDirty = IsDirty();
        stats.PendingAction = m_PendingAction;
        return stats;
    }

    LifecycleResult AnimatorEditorDocument::NewController(const std::string& controllerName) {
        if (IsDirty()) {
            m_PendingAction = DocumentPendingAction::New;
            m_PendingName = controllerName;
            m_PendingRevision = m_CurrentRevision;
            return LifecycleResult::PendingConfirmation;
        }

        m_Definition = CreateNeutralController(controllerName);
        m_FilePath.clear();
        m_CurrentRevision = ++m_NextRevisionToken;
        m_SavedRevision = m_CurrentRevision;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_Selection = AnimatorControllerSelection{};
        m_PendingAction = DocumentPendingAction::None;
        m_PendingPath.clear();
        m_PendingName.clear();
        return LifecycleResult::Executed;
    }

    LifecycleResult AnimatorEditorDocument::OpenController(const std::string& filePath, std::string* outError) {
        if (filePath.empty()) {
            if (outError) *outError = "File path cannot be empty";
            return LifecycleResult::LoadFailed;
        }

        if (IsDirty()) {
            m_PendingAction = DocumentPendingAction::Open;
            m_PendingPath = filePath;
            m_PendingRevision = m_CurrentRevision;
            return LifecycleResult::PendingConfirmation;
        }

        AnimatorControllerDefinition loadedDef;
        if (!AnimationAssetCodec::LoadAnimatorController(filePath, loadedDef, outError)) {
            return LifecycleResult::LoadFailed;
        }

        std::vector<std::string> errs;
        if (!loadedDef.Validate(&errs)) {
            if (outError) {
                *outError = "Opened controller failed validation: ";
                for (const auto& e : errs) *outError += e + "; ";
            }
            return LifecycleResult::ValidationFailed;
        }

        m_Definition = std::move(loadedDef);
        m_FilePath = filePath;
        m_CurrentRevision = ++m_NextRevisionToken;
        m_SavedRevision = m_CurrentRevision;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_Selection = AnimatorControllerSelection{};
        m_PendingAction = DocumentPendingAction::None;
        m_PendingPath.clear();
        m_PendingName.clear();
        return LifecycleResult::Executed;
    }

    LifecycleResult AnimatorEditorDocument::Close() {
        if (IsDirty()) {
            m_PendingAction = DocumentPendingAction::Close;
            m_PendingRevision = m_CurrentRevision;
            return LifecycleResult::PendingConfirmation;
        }

        m_Definition = CreateNeutralController("NewController");
        m_FilePath.clear();
        m_CurrentRevision = ++m_NextRevisionToken;
        m_SavedRevision = m_CurrentRevision;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_Selection = AnimatorControllerSelection{};
        m_PendingAction = DocumentPendingAction::None;
        m_PendingPath.clear();
        m_PendingName.clear();
        return LifecycleResult::Executed;
    }

    LifecycleResult AnimatorEditorDocument::Reload(std::string* outError) {
        if (m_FilePath.empty()) {
            if (outError) *outError = "Cannot reload untitled document";
            return LifecycleResult::LoadFailed;
        }

        if (IsDirty()) {
            m_PendingAction = DocumentPendingAction::Reload;
            m_PendingRevision = m_CurrentRevision;
            return LifecycleResult::PendingConfirmation;
        }

        AnimatorControllerDefinition loadedDef;
        if (!AnimationAssetCodec::LoadAnimatorController(m_FilePath, loadedDef, outError)) {
            return LifecycleResult::LoadFailed;
        }

        std::vector<std::string> errs;
        if (!loadedDef.Validate(&errs)) {
            if (outError) {
                *outError = "Reloaded controller failed validation: ";
                for (const auto& e : errs) *outError += e + "; ";
            }
            return LifecycleResult::ValidationFailed;
        }

        m_Definition = std::move(loadedDef);
        m_CurrentRevision = ++m_NextRevisionToken;
        m_SavedRevision = m_CurrentRevision;
        m_UndoStack.clear();
        m_RedoStack.clear();
        RepairSelection(m_Definition);
        m_PendingAction = DocumentPendingAction::None;
        return LifecycleResult::Executed;
    }

    LifecycleResult AnimatorEditorDocument::ConfirmDiscardAndProceed(std::string* outError) {
        if (m_PendingAction == DocumentPendingAction::None) {
            return LifecycleResult::NoPendingAction;
        }
        if (m_PendingRevision != m_CurrentRevision) {
            if (outError) *outError = "Stale response rejected: document modified after pending action initiated";
            return LifecycleResult::StaleResponseRejected;
        }
        return ExecutePendingAction(outError);
    }

    LifecycleResult AnimatorEditorDocument::SaveAndProceed(std::string* outError) {
        if (m_PendingAction == DocumentPendingAction::None) {
            return LifecycleResult::NoPendingAction;
        }
        if (m_PendingRevision != m_CurrentRevision) {
            if (outError) *outError = "Stale response rejected: document modified after pending action initiated";
            return LifecycleResult::StaleResponseRejected;
        }
        if (!Save(outError)) {
            return LifecycleResult::SaveFailed;
        }
        return ExecutePendingAction(outError);
    }

    LifecycleResult AnimatorEditorDocument::CancelPendingAction() {
        m_PendingAction = DocumentPendingAction::None;
        m_PendingPath.clear();
        m_PendingName.clear();
        m_PendingRevision = 0;
        return LifecycleResult::Cancelled;
    }

    LifecycleResult AnimatorEditorDocument::ExecutePendingAction(std::string* outError) {
        DocumentPendingAction action = m_PendingAction;
        std::string targetPath = m_PendingPath;
        std::string targetName = m_PendingName;

        switch (action) {
            case DocumentPendingAction::New: {
                m_Definition = CreateNeutralController(targetName);
                m_FilePath.clear();
                m_CurrentRevision = ++m_NextRevisionToken;
                m_SavedRevision = m_CurrentRevision;
                m_UndoStack.clear();
                m_RedoStack.clear();
                m_Selection = AnimatorControllerSelection{};
                m_PendingAction = DocumentPendingAction::None;
                m_PendingPath.clear();
                m_PendingName.clear();
                return LifecycleResult::Executed;
            }
            case DocumentPendingAction::Open: {
                AnimatorControllerDefinition loadedDef;
                if (!AnimationAssetCodec::LoadAnimatorController(targetPath, loadedDef, outError)) {
                    return LifecycleResult::LoadFailed;
                }
                std::vector<std::string> errs;
                if (!loadedDef.Validate(&errs)) {
                    if (outError) {
                        *outError = "Opened controller failed validation: ";
                        for (const auto& e : errs) *outError += e + "; ";
                    }
                    return LifecycleResult::ValidationFailed;
                }
                m_Definition = std::move(loadedDef);
                m_FilePath = targetPath;
                m_CurrentRevision = ++m_NextRevisionToken;
                m_SavedRevision = m_CurrentRevision;
                m_UndoStack.clear();
                m_RedoStack.clear();
                m_Selection = AnimatorControllerSelection{};
                m_PendingAction = DocumentPendingAction::None;
                m_PendingPath.clear();
                m_PendingName.clear();
                return LifecycleResult::Executed;
            }
            case DocumentPendingAction::Close: {
                m_Definition = CreateNeutralController("NewController");
                m_FilePath.clear();
                m_CurrentRevision = ++m_NextRevisionToken;
                m_SavedRevision = m_CurrentRevision;
                m_UndoStack.clear();
                m_RedoStack.clear();
                m_Selection = AnimatorControllerSelection{};
                m_PendingAction = DocumentPendingAction::None;
                m_PendingPath.clear();
                m_PendingName.clear();
                return LifecycleResult::Executed;
            }
            case DocumentPendingAction::Reload: {
                AnimatorControllerDefinition loadedDef;
                if (!AnimationAssetCodec::LoadAnimatorController(m_FilePath, loadedDef, outError)) {
                    return LifecycleResult::LoadFailed;
                }
                std::vector<std::string> errs;
                if (!loadedDef.Validate(&errs)) {
                    if (outError) {
                        *outError = "Reloaded controller failed validation: ";
                        for (const auto& e : errs) *outError += e + "; ";
                    }
                    return LifecycleResult::ValidationFailed;
                }
                m_Definition = std::move(loadedDef);
                m_CurrentRevision = ++m_NextRevisionToken;
                m_SavedRevision = m_CurrentRevision;
                m_UndoStack.clear();
                m_RedoStack.clear();
                RepairSelection(m_Definition);
                m_PendingAction = DocumentPendingAction::None;
                return LifecycleResult::Executed;
            }
            default:
                return LifecycleResult::NoPendingAction;
        }
    }

    bool AnimatorEditorDocument::Save(std::string* outError) {
        if (m_FilePath.empty()) {
            if (outError) *outError = "Document has no file path; use SaveAs";
            return false;
        }
        if (IsPackageOrReadOnlyPath(m_FilePath)) {
            if (outError) *outError = "Cannot overwrite package-bound or read-only path: '" + m_FilePath + "'; physical SaveAs required";
            return false;
        }
        std::vector<std::string> errs;
        if (!m_Definition.Validate(&errs)) {
            if (outError) {
                *outError = "Document validation failed: ";
                for (const auto& e : errs) *outError += e + "; ";
            }
            return false;
        }
        if (!AnimationAssetCodec::SaveAnimatorController(m_FilePath, m_Definition, outError)) {
            return false;
        }
        m_SavedRevision = m_CurrentRevision;
        return true;
    }

    bool AnimatorEditorDocument::SaveAs(const std::string& newPath, std::string* outError) {
        if (newPath.empty()) {
            if (outError) *outError = "Target path cannot be empty";
            return false;
        }
        if (IsPackageOrReadOnlyPath(newPath)) {
            if (outError) *outError = "Target path cannot be a package-bound virtual path: '" + newPath + "'";
            return false;
        }
        std::vector<std::string> errs;
        if (!m_Definition.Validate(&errs)) {
            if (outError) {
                *outError = "Document validation failed: ";
                for (const auto& e : errs) *outError += e + "; ";
            }
            return false;
        }

        AnimatorControllerDefinition relocated = RelocateDefinition(m_Definition, m_FilePath, newPath);
        if (!AnimationAssetCodec::SaveAnimatorController(newPath, relocated, outError)) {
            return false;
        }
        m_Definition = std::move(relocated);
        m_FilePath = newPath;
        m_SavedRevision = m_CurrentRevision;
        return true;
    }

    void AnimatorEditorDocument::SelectState(const AnimationId& stateId) {
        m_Selection.Kind = ControllerSelectionKind::State;
        m_Selection.SelectedId = stateId;
    }

    void AnimatorEditorDocument::SelectTransition(const AnimationId& transitionId) {
        m_Selection.Kind = ControllerSelectionKind::Transition;
        m_Selection.SelectedId = transitionId;
    }

    void AnimatorEditorDocument::SelectParameter(const AnimationId& parameterId) {
        m_Selection.Kind = ControllerSelectionKind::Parameter;
        m_Selection.SelectedId = parameterId;
    }

    void AnimatorEditorDocument::ClearSelection() {
        m_Selection = AnimatorControllerSelection{};
    }

    void AnimatorEditorDocument::RepairSelection(const AnimatorControllerDefinition& def) {
        if (m_Selection.Kind == ControllerSelectionKind::State) {
            if (!def.FindState(m_Selection.SelectedId)) {
                m_Selection = AnimatorControllerSelection{};
            }
        } else if (m_Selection.Kind == ControllerSelectionKind::Transition) {
            bool found = false;
            for (const auto& t : def.Transitions) {
                if (t.Id == m_Selection.SelectedId) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_Selection = AnimatorControllerSelection{};
            }
        } else if (m_Selection.Kind == ControllerSelectionKind::Parameter) {
            if (!def.FindParameter(m_Selection.SelectedId)) {
                m_Selection = AnimatorControllerSelection{};
            }
        }
    }

    bool AnimatorEditorDocument::BeginTransaction() {
        if (m_InTransaction) {
            return false;
        }
        m_InTransaction = true;
        m_WorkingCandidate = m_Definition;
        m_TransactionBefore = m_Definition;
        m_TransactionBeforeSelection = m_Selection;
        return true;
    }

    AnimatorControllerDefinition* AnimatorEditorDocument::GetTransactionCandidate() {
        if (!m_InTransaction) return nullptr;
        return &m_WorkingCandidate;
    }

    bool AnimatorEditorDocument::CommitTransaction(const std::string& label, std::string* outError) {
        if (!m_InTransaction) {
            if (outError) *outError = "No transaction active";
            return false;
        }

        std::vector<std::string> errs;
        if (!m_WorkingCandidate.Validate(&errs)) {
            if (outError) {
                *outError = "Validation failed: ";
                for (const auto& e : errs) *outError += e + "; ";
            }
            return false;
        }

        // Semantic no-op check:
        if (m_WorkingCandidate == m_TransactionBefore && m_Selection == m_TransactionBeforeSelection) {
            m_InTransaction = false;
            m_WorkingCandidate = AnimatorControllerDefinition{};
            m_TransactionBefore = AnimatorControllerDefinition{};
            return true;
        }

        size_t beforeBytes = EstimateControllerDefinitionMemory(m_TransactionBefore);
        size_t afterBytes = EstimateControllerDefinitionMemory(m_WorkingCandidate);
        size_t cmdBytes = sizeof(ControllerEditCommand) + label.capacity() + beforeBytes + afterBytes;

        if (cmdBytes > m_MaxSingleCommandBytes) {
            if (outError) *outError = "Edit command exceeds maximum allowed single command memory budget";
            return false;
        }

        uint64_t newRev = ++m_NextRevisionToken;
        ControllerEditCommand cmd;
        cmd.Label = label;
        cmd.Before = std::move(m_TransactionBefore);
        cmd.After = m_WorkingCandidate;
        cmd.BeforeSelection = m_TransactionBeforeSelection;
        cmd.AfterSelection = m_Selection;
        cmd.BeforeRevision = m_CurrentRevision;
        cmd.AfterRevision = newRev;
        cmd.EstimatedSizeBytes = cmdBytes;

        m_UndoStack.push_back(std::move(cmd));
        m_RedoStack.clear();

        m_Definition = std::move(m_WorkingCandidate);
        m_CurrentRevision = newRev;
        m_InTransaction = false;
        m_TransactionBefore = AnimatorControllerDefinition{};
        m_WorkingCandidate = AnimatorControllerDefinition{};

        EnforceHistoryLimits();
        return true;
    }

    void AnimatorEditorDocument::CancelTransaction() {
        if (!m_InTransaction) return;
        m_Selection = m_TransactionBeforeSelection;
        m_WorkingCandidate = AnimatorControllerDefinition{};
        m_TransactionBefore = AnimatorControllerDefinition{};
        m_InTransaction = false;
    }

    bool AnimatorEditorDocument::Modify(const std::string& label, const std::function<bool(AnimatorControllerDefinition&)>& mutator, std::string* outError) {
        if (m_InTransaction) {
            if (outError) *outError = "Cannot call Modify while an explicit transaction is already active";
            return false;
        }
        if (!BeginTransaction()) return false;
        if (!mutator(m_WorkingCandidate)) {
            CancelTransaction();
            if (outError) *outError = "Mutator function rejected modification";
            return false;
        }
        RepairSelection(m_WorkingCandidate);
        if (!CommitTransaction(label, outError)) {
            CancelTransaction();
            return false;
        }
        return true;
    }

    bool AnimatorEditorDocument::Undo() {
        if (m_InTransaction || m_UndoStack.empty()) return false;
        auto cmd = std::move(m_UndoStack.back());
        m_UndoStack.pop_back();

        m_Definition = cmd.Before;
        m_Selection = cmd.BeforeSelection;
        m_CurrentRevision = cmd.BeforeRevision;
        m_RedoStack.push_back(std::move(cmd));
        return true;
    }

    bool AnimatorEditorDocument::Redo() {
        if (m_InTransaction || m_RedoStack.empty()) return false;
        auto cmd = std::move(m_RedoStack.back());
        m_RedoStack.pop_back();

        m_Definition = cmd.After;
        m_Selection = cmd.AfterSelection;
        m_CurrentRevision = cmd.AfterRevision;
        m_UndoStack.push_back(std::move(cmd));
        return true;
    }

    void AnimatorEditorDocument::SetHistoryLimits(size_t maxCommands, size_t maxMemoryBytes, size_t maxSingleCommandBytes) {
        m_MaxCommands = maxCommands;
        m_MaxMemoryBytes = maxMemoryBytes;
        m_MaxSingleCommandBytes = maxSingleCommandBytes;
        EnforceHistoryLimits();
    }

    void AnimatorEditorDocument::EnforceHistoryLimits() {
        while (!m_UndoStack.empty() && (m_UndoStack.size() > m_MaxCommands || GetHistoryMemoryUsage() > m_MaxMemoryBytes)) {
            m_UndoStack.erase(m_UndoStack.begin());
        }
    }

    size_t AnimatorEditorDocument::GetHistoryMemoryUsage() const {
        size_t total = 0;
        for (const auto& cmd : m_UndoStack) total += cmd.EstimatedSizeBytes;
        for (const auto& cmd : m_RedoStack) total += cmd.EstimatedSizeBytes;
        return total;
    }

    bool AnimatorEditorDocument::SetControllerProperties(const std::string& name, const std::string& skeletonRef, const std::string& signature) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            def.Name = name;
            def.SkeletonRef = skeletonRef;
            if (!signature.empty()) def.CompatibilitySignature = signature;
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Set Controller Properties", mutator);
    }

    bool AnimatorEditorDocument::SetEntryState(const AnimationId& stateId) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            if (!stateId.empty() && !def.FindState(stateId)) return false;
            def.EntryStateId = stateId;
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Set Entry State", mutator);
    }

    bool AnimatorEditorDocument::AddParameter(const ParameterDefinition& param) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            if (def.FindParameter(param.Id) || def.FindParameterByName(param.Name)) return false;
            def.Parameters.push_back(param);
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Add Parameter", mutator);
    }

    bool AnimatorEditorDocument::RemoveParameter(const AnimationId& paramId) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            auto it = std::find_if(def.Parameters.begin(), def.Parameters.end(),
                [&](const ParameterDefinition& p) { return p.Id == paramId; });
            if (it == def.Parameters.end()) return false;
            def.Parameters.erase(it);
            // Remove conditions referencing this parameter
            for (auto& t : def.Transitions) {
                t.AllConditions.erase(
                    std::remove_if(t.AllConditions.begin(), t.AllConditions.end(),
                        [&](const ConditionDefinition& c) { return c.Parameter == paramId; }),
                    t.AllConditions.end());
            }
            RepairSelection(def);
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Remove Parameter", mutator);
    }

    bool AnimatorEditorDocument::UpdateParameter(const ParameterDefinition& param) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            for (auto& p : def.Parameters) {
                if (p.Id == param.Id) {
                    p = param;
                    return true;
                }
            }
            return false;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Update Parameter", mutator);
    }

    bool AnimatorEditorDocument::ReorderParameters(size_t fromIndex, size_t toIndex) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            if (fromIndex >= def.Parameters.size() || toIndex >= def.Parameters.size() || fromIndex == toIndex) return false;
            auto item = std::move(def.Parameters[fromIndex]);
            def.Parameters.erase(def.Parameters.begin() + fromIndex);
            def.Parameters.insert(def.Parameters.begin() + toIndex, std::move(item));
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Reorder Parameters", mutator);
    }

    bool AnimatorEditorDocument::AddState(const StateDefinition& state) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            if (def.FindState(state.Id) || def.FindStateByName(state.Name)) return false;
            def.States.push_back(state);
            if (def.EntryStateId.empty()) {
                def.EntryStateId = state.Id;
            }
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Add State", mutator);
    }

    bool AnimatorEditorDocument::RemoveState(const AnimationId& stateId) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            auto it = std::find_if(def.States.begin(), def.States.end(),
                [&](const StateDefinition& s) { return s.Id == stateId; });
            if (it == def.States.end()) return false;
            def.States.erase(it);

            // Remove transitions referencing this state
            def.Transitions.erase(
                std::remove_if(def.Transitions.begin(), def.Transitions.end(),
                    [&](const TransitionDefinition& t) { return t.FromState == stateId || t.ToState == stateId; }),
                def.Transitions.end());

            // Repair entry state if removed
            if (def.EntryStateId == stateId) {
                def.EntryStateId = def.States.empty() ? "" : def.States.front().Id;
            }
            RepairSelection(def);
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Remove State", mutator);
    }

    bool AnimatorEditorDocument::UpdateState(const StateDefinition& state) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            for (auto& s : def.States) {
                if (s.Id == state.Id) {
                    s = state;
                    return true;
                }
            }
            return false;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Update State", mutator);
    }

    bool AnimatorEditorDocument::ReorderStates(size_t fromIndex, size_t toIndex) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            if (fromIndex >= def.States.size() || toIndex >= def.States.size() || fromIndex == toIndex) return false;
            auto item = std::move(def.States[fromIndex]);
            def.States.erase(def.States.begin() + fromIndex);
            def.States.insert(def.States.begin() + toIndex, std::move(item));
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Reorder States", mutator);
    }

    bool AnimatorEditorDocument::AddTransition(const TransitionDefinition& trans) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            for (const auto& t : def.Transitions) {
                if (t.Id == trans.Id) return false;
            }
            def.Transitions.push_back(trans);
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Add Transition", mutator);
    }

    bool AnimatorEditorDocument::RemoveTransition(const AnimationId& transId) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            auto it = std::find_if(def.Transitions.begin(), def.Transitions.end(),
                [&](const TransitionDefinition& t) { return t.Id == transId; });
            if (it == def.Transitions.end()) return false;
            def.Transitions.erase(it);
            RepairSelection(def);
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Remove Transition", mutator);
    }

    bool AnimatorEditorDocument::UpdateTransition(const TransitionDefinition& trans) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            for (auto& t : def.Transitions) {
                if (t.Id == trans.Id) {
                    t = trans;
                    return true;
                }
            }
            return false;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Update Transition", mutator);
    }

    bool AnimatorEditorDocument::ReorderTransitions(size_t fromIndex, size_t toIndex) {
        auto mutator = [&](AnimatorControllerDefinition& def) {
            if (fromIndex >= def.Transitions.size() || toIndex >= def.Transitions.size() || fromIndex == toIndex) return false;
            auto item = std::move(def.Transitions[fromIndex]);
            def.Transitions.erase(def.Transitions.begin() + fromIndex);
            def.Transitions.insert(def.Transitions.begin() + toIndex, std::move(item));
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Reorder Transitions", mutator);
    }

}

