#include <Axle/AnimationEditorDocument.hpp>
#include <Axle/AnimationAssetCodec.hpp>
#include <algorithm>

namespace Axle {

    AnimationClipDefinition AnimationEditorDocument::CreateNeutralClip(const std::string& name) {
        AnimationClipDefinition def;
        def.SchemaVersion = 1;
        def.Id = GenerateAnimationId();
        def.Name = name.empty() ? "NewClip" : name;
        def.Duration = 1.0f;
        def.FrameRate = 30.0f;
        return def;
    }

    AnimationClipDefinition AnimationEditorDocument::RelocateDefinition(const AnimationClipDefinition& def, const std::string& oldPath, const std::string& newPath) {
        AnimationClipDefinition relocated = def;
        relocated.SkeletonRef = RelocateReferencePath(def.SkeletonRef, oldPath, newPath);
        return relocated;
    }

    AnimationEditorDocument::AnimationEditorDocument()
        : m_Definition(CreateNeutralClip("NewClip"))
        , m_CurrentRevision(1)
        , m_SavedRevision(1)
        , m_NextRevisionToken(1)
    {
    }

    AnimationDocumentStats AnimationEditorDocument::GetStats() const {
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

    LifecycleResult AnimationEditorDocument::NewClip(const std::string& clipName) {
        if (IsDirty()) {
            m_PendingAction = DocumentPendingAction::New;
            m_PendingName = clipName;
            m_PendingRevision = m_CurrentRevision;
            return LifecycleResult::PendingConfirmation;
        }

        m_Definition = CreateNeutralClip(clipName);
        m_FilePath.clear();
        m_CurrentRevision = ++m_NextRevisionToken;
        m_SavedRevision = m_CurrentRevision;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_Selection = AnimationClipSelection{};
        m_PendingAction = DocumentPendingAction::None;
        m_PendingPath.clear();
        m_PendingName.clear();
        return LifecycleResult::Executed;
    }

    LifecycleResult AnimationEditorDocument::OpenClip(const std::string& filePath, std::string* outError) {
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

        AnimationClipDefinition loadedDef;
        if (!AnimationAssetCodec::LoadAnimationClip(filePath, loadedDef, outError)) {
            return LifecycleResult::LoadFailed;
        }

        std::vector<std::string> errs;
        if (!loadedDef.Validate(&errs)) {
            if (outError) {
                *outError = "Opened clip failed validation: ";
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
        m_Selection = AnimationClipSelection{};
        m_PendingAction = DocumentPendingAction::None;
        m_PendingPath.clear();
        m_PendingName.clear();
        return LifecycleResult::Executed;
    }

    LifecycleResult AnimationEditorDocument::Close() {
        if (IsDirty()) {
            m_PendingAction = DocumentPendingAction::Close;
            m_PendingRevision = m_CurrentRevision;
            return LifecycleResult::PendingConfirmation;
        }

        m_Definition = CreateNeutralClip("NewClip");
        m_FilePath.clear();
        m_CurrentRevision = ++m_NextRevisionToken;
        m_SavedRevision = m_CurrentRevision;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_Selection = AnimationClipSelection{};
        m_PendingAction = DocumentPendingAction::None;
        m_PendingPath.clear();
        m_PendingName.clear();
        return LifecycleResult::Executed;
    }

    LifecycleResult AnimationEditorDocument::Reload(std::string* outError) {
        if (m_FilePath.empty()) {
            if (outError) *outError = "Cannot reload untitled document";
            return LifecycleResult::LoadFailed;
        }

        if (IsDirty()) {
            m_PendingAction = DocumentPendingAction::Reload;
            m_PendingRevision = m_CurrentRevision;
            return LifecycleResult::PendingConfirmation;
        }

        AnimationClipDefinition loadedDef;
        if (!AnimationAssetCodec::LoadAnimationClip(m_FilePath, loadedDef, outError)) {
            return LifecycleResult::LoadFailed;
        }

        std::vector<std::string> errs;
        if (!loadedDef.Validate(&errs)) {
            if (outError) {
                *outError = "Reloaded clip failed validation: ";
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

    LifecycleResult AnimationEditorDocument::ConfirmDiscardAndProceed(std::string* outError) {
        if (m_PendingAction == DocumentPendingAction::None) {
            return LifecycleResult::NoPendingAction;
        }
        if (m_PendingRevision != m_CurrentRevision) {
            if (outError) *outError = "Stale response rejected: document modified after pending action initiated";
            return LifecycleResult::StaleResponseRejected;
        }
        return ExecutePendingAction(outError);
    }

    LifecycleResult AnimationEditorDocument::SaveAndProceed(std::string* outError) {
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

    LifecycleResult AnimationEditorDocument::CancelPendingAction() {
        m_PendingAction = DocumentPendingAction::None;
        m_PendingPath.clear();
        m_PendingName.clear();
        m_PendingRevision = 0;
        return LifecycleResult::Cancelled;
    }

    LifecycleResult AnimationEditorDocument::ExecutePendingAction(std::string* outError) {
        DocumentPendingAction action = m_PendingAction;
        std::string targetPath = m_PendingPath;
        std::string targetName = m_PendingName;

        switch (action) {
            case DocumentPendingAction::New: {
                m_Definition = CreateNeutralClip(targetName);
                m_FilePath.clear();
                m_CurrentRevision = ++m_NextRevisionToken;
                m_SavedRevision = m_CurrentRevision;
                m_UndoStack.clear();
                m_RedoStack.clear();
                m_Selection = AnimationClipSelection{};
                m_PendingAction = DocumentPendingAction::None;
                m_PendingPath.clear();
                m_PendingName.clear();
                return LifecycleResult::Executed;
            }
            case DocumentPendingAction::Open: {
                AnimationClipDefinition loadedDef;
                if (!AnimationAssetCodec::LoadAnimationClip(targetPath, loadedDef, outError)) {
                    return LifecycleResult::LoadFailed;
                }
                std::vector<std::string> errs;
                if (!loadedDef.Validate(&errs)) {
                    if (outError) {
                        *outError = "Opened clip failed validation: ";
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
                m_Selection = AnimationClipSelection{};
                m_PendingAction = DocumentPendingAction::None;
                m_PendingPath.clear();
                m_PendingName.clear();
                return LifecycleResult::Executed;
            }
            case DocumentPendingAction::Close: {
                m_Definition = CreateNeutralClip("NewClip");
                m_FilePath.clear();
                m_CurrentRevision = ++m_NextRevisionToken;
                m_SavedRevision = m_CurrentRevision;
                m_UndoStack.clear();
                m_RedoStack.clear();
                m_Selection = AnimationClipSelection{};
                m_PendingAction = DocumentPendingAction::None;
                m_PendingPath.clear();
                m_PendingName.clear();
                return LifecycleResult::Executed;
            }
            case DocumentPendingAction::Reload: {
                AnimationClipDefinition loadedDef;
                if (!AnimationAssetCodec::LoadAnimationClip(m_FilePath, loadedDef, outError)) {
                    return LifecycleResult::LoadFailed;
                }
                std::vector<std::string> errs;
                if (!loadedDef.Validate(&errs)) {
                    if (outError) {
                        *outError = "Reloaded clip failed validation: ";
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

    bool AnimationEditorDocument::Save(std::string* outError) {
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
        if (!AnimationAssetCodec::SaveAnimationClip(m_FilePath, m_Definition, outError)) {
            return false;
        }
        m_SavedRevision = m_CurrentRevision;
        return true;
    }

    bool AnimationEditorDocument::SaveAs(const std::string& newPath, std::string* outError) {
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

        AnimationClipDefinition relocated = RelocateDefinition(m_Definition, m_FilePath, newPath);
        if (!AnimationAssetCodec::SaveAnimationClip(newPath, relocated, outError)) {
            return false;
        }
        m_Definition = std::move(relocated);
        m_FilePath = newPath;
        m_SavedRevision = m_CurrentRevision;
        return true;
    }

    void AnimationEditorDocument::SelectChannel(const std::string& boneName, AnimationChannelType channelType) {
        m_Selection.Kind = ClipSelectionKind::Channel;
        m_Selection.BoneName = boneName;
        m_Selection.ChannelType = channelType;
        m_Selection.EventId.clear();
    }

    void AnimationEditorDocument::SelectEvent(const AnimationId& eventId) {
        m_Selection.Kind = ClipSelectionKind::Event;
        m_Selection.EventId = eventId;
        m_Selection.BoneName.clear();
    }

    void AnimationEditorDocument::ClearSelection() {
        m_Selection = AnimationClipSelection{};
    }

    void AnimationEditorDocument::RepairSelection(const AnimationClipDefinition& def) {
        if (m_Selection.Kind == ClipSelectionKind::Channel) {
            bool found = false;
            for (const auto& ch : def.Channels) {
                if (ch.BoneName == m_Selection.BoneName) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_Selection = AnimationClipSelection{};
            }
        } else if (m_Selection.Kind == ClipSelectionKind::Event) {
            bool found = false;
            for (const auto& ev : def.Events) {
                if (ev.Id == m_Selection.EventId) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_Selection = AnimationClipSelection{};
            }
        }
    }

    bool AnimationEditorDocument::BeginTransaction() {
        if (m_InTransaction) {
            return false;
        }
        m_InTransaction = true;
        m_WorkingCandidate = m_Definition;
        m_TransactionBefore = m_Definition;
        m_TransactionBeforeSelection = m_Selection;
        return true;
    }

    AnimationClipDefinition* AnimationEditorDocument::GetTransactionCandidate() {
        if (!m_InTransaction) return nullptr;
        return &m_WorkingCandidate;
    }

    bool AnimationEditorDocument::CommitTransaction(const std::string& label, std::string* outError) {
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
            m_WorkingCandidate = AnimationClipDefinition{};
            m_TransactionBefore = AnimationClipDefinition{};
            return true;
        }

        size_t beforeBytes = EstimateClipDefinitionMemory(m_TransactionBefore);
        size_t afterBytes = EstimateClipDefinitionMemory(m_WorkingCandidate);
        size_t cmdBytes = sizeof(ClipEditCommand) + label.capacity() + beforeBytes + afterBytes;

        if (cmdBytes > m_MaxSingleCommandBytes) {
            if (outError) *outError = "Edit command exceeds maximum allowed single command memory budget";
            return false;
        }

        uint64_t newRev = ++m_NextRevisionToken;
        ClipEditCommand cmd;
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
        m_TransactionBefore = AnimationClipDefinition{};
        m_WorkingCandidate = AnimationClipDefinition{};

        EnforceHistoryLimits();
        return true;
    }

    void AnimationEditorDocument::CancelTransaction() {
        if (!m_InTransaction) return;
        m_Selection = m_TransactionBeforeSelection;
        m_WorkingCandidate = AnimationClipDefinition{};
        m_TransactionBefore = AnimationClipDefinition{};
        m_InTransaction = false;
    }

    bool AnimationEditorDocument::Modify(const std::string& label, const std::function<bool(AnimationClipDefinition&)>& mutator, std::string* outError) {
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

    bool AnimationEditorDocument::Undo() {
        if (m_InTransaction || m_UndoStack.empty()) return false;
        auto cmd = std::move(m_UndoStack.back());
        m_UndoStack.pop_back();

        m_Definition = cmd.Before;
        m_Selection = cmd.BeforeSelection;
        m_CurrentRevision = cmd.BeforeRevision;
        m_RedoStack.push_back(std::move(cmd));
        return true;
    }

    bool AnimationEditorDocument::Redo() {
        if (m_InTransaction || m_RedoStack.empty()) return false;
        auto cmd = std::move(m_RedoStack.back());
        m_RedoStack.pop_back();

        m_Definition = cmd.After;
        m_Selection = cmd.AfterSelection;
        m_CurrentRevision = cmd.AfterRevision;
        m_UndoStack.push_back(std::move(cmd));
        return true;
    }

    void AnimationEditorDocument::SetHistoryLimits(size_t maxCommands, size_t maxMemoryBytes, size_t maxSingleCommandBytes) {
        m_MaxCommands = maxCommands;
        m_MaxMemoryBytes = maxMemoryBytes;
        m_MaxSingleCommandBytes = maxSingleCommandBytes;
        EnforceHistoryLimits();
    }

    void AnimationEditorDocument::EnforceHistoryLimits() {
        while (!m_UndoStack.empty() && (m_UndoStack.size() > m_MaxCommands || GetHistoryMemoryUsage() > m_MaxMemoryBytes)) {
            m_UndoStack.erase(m_UndoStack.begin());
        }
    }

    size_t AnimationEditorDocument::GetHistoryMemoryUsage() const {
        size_t total = 0;
        for (const auto& cmd : m_UndoStack) total += cmd.EstimatedSizeBytes;
        for (const auto& cmd : m_RedoStack) total += cmd.EstimatedSizeBytes;
        return total;
    }

    bool AnimationEditorDocument::SetClipProperties(const std::string& name, float duration, float frameRate, const std::string& skeletonRef, const std::string& signature) {
        auto mutator = [&](AnimationClipDefinition& def) {
            def.Name = name;
            def.Duration = duration;
            def.FrameRate = frameRate;
            def.SkeletonRef = skeletonRef;
            if (!signature.empty()) def.CompatibilitySignature = signature;
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Set Clip Properties", mutator);
    }

    bool AnimationEditorDocument::AddChannel(const AnimationChannel& channel) {
        auto mutator = [&](AnimationClipDefinition& def) {
            for (const auto& ch : def.Channels) {
                if (ch.BoneName == channel.BoneName) return false;
            }
            def.Channels.push_back(channel);
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Add Channel", mutator);
    }

    bool AnimationEditorDocument::RemoveChannel(const std::string& boneName) {
        auto mutator = [&](AnimationClipDefinition& def) {
            auto it = std::find_if(def.Channels.begin(), def.Channels.end(),
                [&](const AnimationChannel& ch) { return ch.BoneName == boneName; });
            if (it == def.Channels.end()) return false;
            def.Channels.erase(it);
            RepairSelection(def);
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Remove Channel", mutator);
    }

    bool AnimationEditorDocument::ReorderChannels(size_t fromIndex, size_t toIndex) {
        auto mutator = [&](AnimationClipDefinition& def) {
            if (fromIndex >= def.Channels.size() || toIndex >= def.Channels.size() || fromIndex == toIndex) return false;
            auto item = std::move(def.Channels[fromIndex]);
            def.Channels.erase(def.Channels.begin() + fromIndex);
            def.Channels.insert(def.Channels.begin() + toIndex, std::move(item));
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Reorder Channels", mutator);
    }

    bool AnimationEditorDocument::UpdateChannel(const AnimationChannel& channel) {
        auto mutator = [&](AnimationClipDefinition& def) {
            for (auto& ch : def.Channels) {
                if (ch.BoneName == channel.BoneName) {
                    ch = channel;
                    return true;
                }
            }
            return false;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Update Channel", mutator);
    }

    bool AnimationEditorDocument::AddEvent(const AnimationEventDefinition& event) {
        auto mutator = [&](AnimationClipDefinition& def) {
            for (const auto& ev : def.Events) {
                if (ev.Id == event.Id) return false;
            }
            def.Events.push_back(event);
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Add Event", mutator);
    }

    bool AnimationEditorDocument::RemoveEvent(const AnimationId& eventId) {
        auto mutator = [&](AnimationClipDefinition& def) {
            auto it = std::find_if(def.Events.begin(), def.Events.end(),
                [&](const AnimationEventDefinition& ev) { return ev.Id == eventId; });
            if (it == def.Events.end()) return false;
            def.Events.erase(it);
            RepairSelection(def);
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Remove Event", mutator);
    }

    bool AnimationEditorDocument::ReorderEvents(size_t fromIndex, size_t toIndex) {
        auto mutator = [&](AnimationClipDefinition& def) {
            if (fromIndex >= def.Events.size() || toIndex >= def.Events.size() || fromIndex == toIndex) return false;
            auto item = std::move(def.Events[fromIndex]);
            def.Events.erase(def.Events.begin() + fromIndex);
            def.Events.insert(def.Events.begin() + toIndex, std::move(item));
            return true;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Reorder Events", mutator);
    }

    bool AnimationEditorDocument::UpdateEvent(const AnimationEventDefinition& event) {
        auto mutator = [&](AnimationClipDefinition& def) {
            for (auto& ev : def.Events) {
                if (ev.Id == event.Id) {
                    ev = event;
                    return true;
                }
            }
            return false;
        };
        if (m_InTransaction) {
            return mutator(m_WorkingCandidate);
        }
        return Modify("Update Event", mutator);
    }

}

