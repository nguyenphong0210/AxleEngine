#include <Axle/VFXEditorDocument.hpp>
#include <algorithm>

namespace Axle {

    VFXEditorDocument::VFXEditorDocument() {
        NewEffect();
    }

    void VFXEditorDocument::NewEffect(const std::string& effectName) {
        m_Definition = ParticleEffectDefinition();
        m_Definition.Name = effectName;

        m_SelectedEmitterId.clear();
        m_FilePath.clear();
        m_IsDirty = false;
        m_CurrentRevision = 0;
        m_SavedRevision = 0;
        m_NextRevision = 1;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_TransactionBefore.reset();
        m_SavedDefinition = m_Definition;
    }

    bool VFXEditorDocument::OpenEffect(const std::string& filePath, std::vector<VFXDiagnostic>& diagnostics) {
        ParticleEffectDefinition candidate;
        if (!ParticleEffectSerializer::DeserializeFromFile(filePath, candidate, &diagnostics)) {
            return false;
        }

        m_Definition = std::move(candidate);
        m_FilePath = filePath;
        m_IsDirty = false;
        m_CurrentRevision = 0;
        m_SavedRevision = 0;
        m_NextRevision = 1;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_TransactionBefore.reset();
        m_SavedDefinition = m_Definition;

        if (!m_Definition.Emitters.empty()) {
            m_SelectedEmitterId = m_Definition.Emitters[0].Id;
        } else {
            m_SelectedEmitterId.clear();
        }

        return true;
    }

    bool VFXEditorDocument::Save(std::vector<VFXDiagnostic>& diagnostics) {
        if (m_FilePath.empty()) {
            diagnostics.push_back(VFXDiagnostic{
                VFXDiagnosticSeverity::Error,
                "ERR_VFX_SAVE_NO_PATH",
                "",
                "Cannot save document without an authored file path. Use SaveAs."
            });
            return false;
        }
        return SaveAs(m_FilePath, diagnostics);
    }

    bool VFXEditorDocument::SaveAs(const std::string& newPath, std::vector<VFXDiagnostic>& diagnostics) {
        std::vector<std::string> valErrors;
        if (!ValidateParticleEffectDefinition(m_Definition, valErrors)) {
            for (const auto& err : valErrors) {
                diagnostics.push_back(VFXDiagnostic{
                    VFXDiagnosticSeverity::Error,
                    "ERR_VFX_VALIDATION",
                    "",
                    err
                });
            }
            return false;
        }

        if (!ParticleEffectSerializer::SerializeToFile(newPath, m_Definition)) {
            diagnostics.push_back(VFXDiagnostic{
                VFXDiagnosticSeverity::Error,
                "ERR_VFX_WRITE_FAILED",
                "",
                "Failed to write particle effect asset to file: " + newPath
            });
            return false;
        }

        m_FilePath = newPath;
        m_SavedRevision = m_CurrentRevision;
        m_SavedDefinition = m_Definition;
        m_IsDirty = false;
        return true;
    }

    void VFXEditorDocument::SetEffectName(const std::string& name) {
        if (m_Definition.Name != name) {
            ParticleEffectDefinition before = m_Definition;
        const auto beforeSelection = m_SelectedEmitterId;
            m_Definition.Name = name;
            RecordChange("Rename Effect", before, beforeSelection);
        }
    }

    void VFXEditorDocument::SelectEmitter(const std::string& id) {
        if (FindEmitterById(id)) {
            m_SelectedEmitterId = id;
        }
    }

    EmitterDefinition* VFXEditorDocument::GetSelectedEmitter() {
        return FindEmitterById(m_SelectedEmitterId);
    }

    const EmitterDefinition* VFXEditorDocument::GetSelectedEmitter() const {
        return FindEmitterById(m_SelectedEmitterId);
    }

    EmitterDefinition* VFXEditorDocument::FindEmitterById(const std::string& id) {
        for (auto& emitter : m_Definition.Emitters) {
            if (emitter.Id == id) return &emitter;
        }
        return nullptr;
    }

    const EmitterDefinition* VFXEditorDocument::FindEmitterById(const std::string& id) const {
        for (const auto& emitter : m_Definition.Emitters) {
            if (emitter.Id == id) return &emitter;
        }
        return nullptr;
    }

    std::string VFXEditorDocument::AddEmitter(const std::string& name, const ParticleEmitterConfig& cfg) {
        ParticleEffectDefinition before = m_Definition;
        const auto beforeSelection = m_SelectedEmitterId;

        EmitterDefinition emitter;
        do { emitter.Id = GenerateUniqueEmitterId(); } while (m_Definition.HasEmitter(emitter.Id));
        emitter.Name = name;
        emitter.Config = cfg;
        emitter.Enabled = true;

        std::string newId = emitter.Id;
        m_Definition.Emitters.push_back(std::move(emitter));
        m_SelectedEmitterId = newId;

        RecordChange("Add Emitter", before, beforeSelection);
        return newId;
    }

    std::string VFXEditorDocument::DuplicateEmitter(const std::string& id) {
        const auto* src = FindEmitterById(id);
        if (!src) return "";

        ParticleEffectDefinition before = m_Definition;
        const auto beforeSelection = m_SelectedEmitterId;

        EmitterDefinition dup = *src;
        do { dup.Id = GenerateUniqueEmitterId(); } while (m_Definition.HasEmitter(dup.Id));
        dup.Name = src->Name + " (Copy)";

        std::string newId = dup.Id;
        m_Definition.Emitters.push_back(std::move(dup));
        m_SelectedEmitterId = newId;

        RecordChange("Duplicate Emitter", before, beforeSelection);
        return newId;
    }

    bool VFXEditorDocument::RemoveEmitter(const std::string& id, std::vector<VFXDiagnostic>& diagnostics) {
        auto it = std::find_if(m_Definition.Emitters.begin(), m_Definition.Emitters.end(),
                               [&](const EmitterDefinition& e) { return e.Id == id; });
        if (it == m_Definition.Emitters.end()) return false;

        // Check if any other emitter references this target in sub-emitters
        for (const auto& emitter : m_Definition.Emitters) {
            if (emitter.Id == id) continue;
            for (const auto& sub : emitter.Config.SubEmitters) {
                if (sub.TargetEmitterName == id || sub.TargetEmitterName == it->Name) {
                    diagnostics.push_back(VFXDiagnostic{
                        VFXDiagnosticSeverity::Warning,
                        "WARN_VFX_DANGLING_LINK",
                        emitter.Id,
                        "Emitter '" + emitter.Name + "' references removed emitter '" + it->Name + "'."
                    });
                }
            }
        }

        ParticleEffectDefinition before = m_Definition;
        const auto beforeSelection = m_SelectedEmitterId;
        const auto removedName = it->Name;
        m_Definition.Emitters.erase(it);
        for (auto& source : m_Definition.Emitters)
            std::erase_if(source.Config.SubEmitters, [&](const auto& link) { return link.TargetEmitterName == id || link.TargetEmitterName == removedName; });

        if (m_SelectedEmitterId == id) {
            m_SelectedEmitterId = m_Definition.Emitters.empty() ? "" : m_Definition.Emitters[0].Id;
        }

        RecordChange("Remove Emitter", before, beforeSelection);
        return true;
    }

    bool VFXEditorDocument::RenameEmitter(const std::string& id, const std::string& newName) {
        auto* emitter = FindEmitterById(id);
        if (!emitter || emitter->Name == newName) return false;

        ParticleEffectDefinition before = m_Definition;
        const auto beforeSelection = m_SelectedEmitterId;
        for (auto& source : m_Definition.Emitters)
            for (auto& link : source.Config.SubEmitters)
                if (link.TargetEmitterName == emitter->Name) link.TargetEmitterName = id;
        emitter->Name = newName;
        RecordChange("Rename Emitter", before, beforeSelection);
        return true;
    }

    bool VFXEditorDocument::ReorderEmitter(size_t fromIndex, size_t toIndex) {
        if (fromIndex >= m_Definition.Emitters.size() || toIndex >= m_Definition.Emitters.size() || fromIndex == toIndex) {
            return false;
        }

        ParticleEffectDefinition before = m_Definition;
        const auto beforeSelection = m_SelectedEmitterId;
        auto elem = std::move(m_Definition.Emitters[fromIndex]);
        m_Definition.Emitters.erase(m_Definition.Emitters.begin() + fromIndex);
        m_Definition.Emitters.insert(m_Definition.Emitters.begin() + toIndex, std::move(elem));

        RecordChange("Reorder Emitters", before, beforeSelection);
        return true;
    }

    bool VFXEditorDocument::SetEmitterEnabled(const std::string& id, bool enabled) {
        auto* emitter = FindEmitterById(id);
        if (!emitter || emitter->Enabled == enabled) return false;

        ParticleEffectDefinition before = m_Definition;
        const auto beforeSelection = m_SelectedEmitterId;
        emitter->Enabled = enabled;
        RecordChange("Toggle Emitter Enabled", before, beforeSelection);
        return true;
    }

    bool VFXEditorDocument::ReplaceEmitterConfig(const std::string& id, const ParticleEmitterConfig& cfg) {
        auto* emitter = FindEmitterById(id);
        if (!emitter) return false;

        ParticleEffectDefinition before = m_Definition;
        const auto beforeSelection = m_SelectedEmitterId;
        emitter->Config = cfg;
        RecordChange("Edit Emitter Config", before, beforeSelection);
        return true;
    }

    bool VFXEditorDocument::SetEmitterResources(const std::string& id, const EmitterResources& res) {
        auto* emitter = FindEmitterById(id);
        if (!emitter) return false;

        ParticleEffectDefinition before = m_Definition;
        const auto beforeSelection = m_SelectedEmitterId;
        emitter->Resources = res;
        RecordChange("Edit Emitter Resources", before, beforeSelection);
        return true;
    }

    void VFXEditorDocument::LoadDefinition(const ParticleEffectDefinition& definition) {
        auto value = definition;
        NewEffect(value.Name);
        m_Definition = std::move(value);
        m_SavedDefinition = m_Definition;
        m_SelectedEmitterId = m_Definition.Emitters.empty() ? "" : m_Definition.Emitters.front().Id;
    }

    void VFXEditorDocument::DiscardChanges() {
        auto file = m_FilePath;
        LoadDefinition(m_SavedDefinition);
        m_FilePath = std::move(file);
    }

    bool VFXEditorDocument::ReplaceDefinition(const ParticleEffectDefinition& definition, const std::string& label) {
        if (m_Definition == definition) return false;
        auto before = m_Definition;
        auto selection = m_SelectedEmitterId;
        m_Definition = definition;
        if (!FindEmitterById(m_SelectedEmitterId))
            m_SelectedEmitterId = definition.Emitters.empty() ? "" : definition.Emitters.front().Id;
        RecordChange(label, before, selection);
        return true;
    }

    void VFXEditorDocument::BeginTransaction() {
        if (!m_TransactionBefore) {
            m_TransactionBefore = m_Definition;
            m_TransactionSelection = m_SelectedEmitterId;
        }
    }

    void VFXEditorDocument::CommitTransaction(const std::string& label) {
        if (m_TransactionBefore) {
            auto before = std::move(*m_TransactionBefore);
            m_TransactionBefore.reset();
            RecordChange(label, before, m_TransactionSelection);
        }
    }

    void VFXEditorDocument::RecordChange(const std::string& label, const ParticleEffectDefinition& before, const std::string& selection) {
        if (before == m_Definition) return;
        if (m_TransactionBefore) { MarkDirty(); return; }
        EffectEditCommand cmd;
        cmd.Label = label;
        cmd.Before = before;
        cmd.After = m_Definition;
        cmd.BeforeSelection = selection;
        cmd.AfterSelection = m_SelectedEmitterId;
        cmd.BeforeRevision = m_CurrentRevision;
        cmd.AfterRevision = m_NextRevision++;
        m_CurrentRevision = cmd.AfterRevision;
        m_UndoStack.push_back(std::move(cmd));
        m_RedoStack.clear();
        MarkDirty();
    }

    bool VFXEditorDocument::Undo() {
        if (m_UndoStack.empty()) return false;
        auto cmd = std::move(m_UndoStack.back());
        m_UndoStack.pop_back();
        m_Definition = cmd.Before;
        m_SelectedEmitterId = cmd.BeforeSelection;
        m_CurrentRevision = cmd.BeforeRevision;
        m_RedoStack.push_back(std::move(cmd));
        MarkDirty();
        return true;
    }

    bool VFXEditorDocument::Redo() {
        if (m_RedoStack.empty()) return false;
        auto cmd = std::move(m_RedoStack.back());
        m_RedoStack.pop_back();
        m_Definition = cmd.After;
        m_SelectedEmitterId = cmd.AfterSelection;
        m_CurrentRevision = cmd.AfterRevision;
        m_UndoStack.push_back(std::move(cmd));
        MarkDirty();
        return true;
    }

    void VFXEditorDocument::MarkDirty() {
        m_IsDirty = (m_CurrentRevision != m_SavedRevision) || (m_TransactionBefore && *m_TransactionBefore != m_Definition);
    }
}
