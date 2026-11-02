#include <Axle/VFXEditorDocument.hpp>
#include <algorithm>

namespace Axle {

    VFXEditorDocument::VFXEditorDocument() {
        NewEffect();
    }

    void VFXEditorDocument::NewEffect(const std::string& effectName) {
        m_Definition = ParticleEffectDefinition();
        m_Definition.Name = effectName;

        EmitterDefinition baseEmitter;
        baseEmitter.Id = GenerateUniqueEmitterId();
        baseEmitter.Name = "Base Emitter";
        baseEmitter.Enabled = true;
        m_Definition.Emitters.push_back(std::move(baseEmitter));

        m_SelectedEmitterId = m_Definition.Emitters[0].Id;
        m_FilePath.clear();
        m_IsDirty = false;
        m_CurrentRevision = 0;
        m_SavedRevision = 0;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_TransactionBefore.reset();
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
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_TransactionBefore.reset();

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
        m_IsDirty = false;
        return true;
    }

    void VFXEditorDocument::SetEffectName(const std::string& name) {
        if (m_Definition.Name != name) {
            ParticleEffectDefinition before = m_Definition;
            m_Definition.Name = name;
            RecordChange("Rename Effect", before);
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

        EmitterDefinition emitter;
        emitter.Id = GenerateUniqueEmitterId();
        emitter.Name = name;
        emitter.Config = cfg;
        emitter.Enabled = true;

        std::string newId = emitter.Id;
        m_Definition.Emitters.push_back(std::move(emitter));
        m_SelectedEmitterId = newId;

        RecordChange("Add Emitter", before);
        return newId;
    }

    std::string VFXEditorDocument::DuplicateEmitter(const std::string& id) {
        const auto* src = FindEmitterById(id);
        if (!src) return "";

        ParticleEffectDefinition before = m_Definition;

        EmitterDefinition dup = *src;
        dup.Id = GenerateUniqueEmitterId();
        dup.Name = src->Name + " (Copy)";

        std::string newId = dup.Id;
        m_Definition.Emitters.push_back(std::move(dup));
        m_SelectedEmitterId = newId;

        RecordChange("Duplicate Emitter", before);
        return newId;
    }

    bool VFXEditorDocument::RemoveEmitter(const std::string& id, std::vector<VFXDiagnostic>& diagnostics) {
        if (m_Definition.Emitters.size() <= 1) {
            diagnostics.push_back(VFXDiagnostic{
                VFXDiagnosticSeverity::Warning,
                "WARN_VFX_REMOVE_LAST",
                id,
                "Cannot remove the only emitter in the effect."
            });
            return false;
        }

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
        m_Definition.Emitters.erase(it);

        if (m_SelectedEmitterId == id) {
            m_SelectedEmitterId = m_Definition.Emitters.empty() ? "" : m_Definition.Emitters[0].Id;
        }

        RecordChange("Remove Emitter", before);
        return true;
    }

    bool VFXEditorDocument::RenameEmitter(const std::string& id, const std::string& newName) {
        auto* emitter = FindEmitterById(id);
        if (!emitter || emitter->Name == newName) return false;

        ParticleEffectDefinition before = m_Definition;
        emitter->Name = newName;
        RecordChange("Rename Emitter", before);
        return true;
    }

    bool VFXEditorDocument::ReorderEmitter(size_t fromIndex, size_t toIndex) {
        if (fromIndex >= m_Definition.Emitters.size() || toIndex >= m_Definition.Emitters.size() || fromIndex == toIndex) {
            return false;
        }

        ParticleEffectDefinition before = m_Definition;
        auto elem = std::move(m_Definition.Emitters[fromIndex]);
        m_Definition.Emitters.erase(m_Definition.Emitters.begin() + fromIndex);
        m_Definition.Emitters.insert(m_Definition.Emitters.begin() + toIndex, std::move(elem));

        RecordChange("Reorder Emitters", before);
        return true;
    }

    bool VFXEditorDocument::SetEmitterEnabled(const std::string& id, bool enabled) {
        auto* emitter = FindEmitterById(id);
        if (!emitter || emitter->Enabled == enabled) return false;

        ParticleEffectDefinition before = m_Definition;
        emitter->Enabled = enabled;
        RecordChange("Toggle Emitter Enabled", before);
        return true;
    }

    bool VFXEditorDocument::ReplaceEmitterConfig(const std::string& id, const ParticleEmitterConfig& cfg) {
        auto* emitter = FindEmitterById(id);
        if (!emitter) return false;

        ParticleEffectDefinition before = m_Definition;
        emitter->Config = cfg;
        RecordChange("Edit Emitter Config", before);
        return true;
    }

    bool VFXEditorDocument::SetEmitterResources(const std::string& id, const EmitterResources& res) {
        auto* emitter = FindEmitterById(id);
        if (!emitter) return false;

        ParticleEffectDefinition before = m_Definition;
        emitter->Resources = res;
        RecordChange("Edit Emitter Resources", before);
        return true;
    }

    void VFXEditorDocument::BeginTransaction() {
        if (!m_TransactionBefore.has_value()) {
            m_TransactionBefore = m_Definition;
        }
    }

    void VFXEditorDocument::CommitTransaction(const std::string& label) {
        if (m_TransactionBefore.has_value()) {
            RecordChange(label, *m_TransactionBefore);
            m_TransactionBefore.reset();
        }
    }

    void VFXEditorDocument::RecordChange(const std::string& label, const ParticleEffectDefinition& before) {
        EffectEditCommand cmd;
        cmd.Label = label;
        cmd.Before = before;
        cmd.After = m_Definition;
        cmd.SelectedEmitterId = m_SelectedEmitterId;

        m_UndoStack.push_back(std::move(cmd));
        m_RedoStack.clear();

        m_CurrentRevision++;
        MarkDirty();
    }

    bool VFXEditorDocument::Undo() {
        if (m_UndoStack.empty()) return false;

        auto cmd = std::move(m_UndoStack.back());
        m_UndoStack.pop_back();

        EffectEditCommand redoCmd;
        redoCmd.Label = cmd.Label;
        redoCmd.Before = m_Definition;
        redoCmd.After = cmd.Before;
        redoCmd.SelectedEmitterId = m_SelectedEmitterId;
        m_RedoStack.push_back(std::move(redoCmd));

        m_Definition = std::move(cmd.Before);
        m_SelectedEmitterId = cmd.SelectedEmitterId;

        if (m_CurrentRevision > 0) m_CurrentRevision--;
        MarkDirty();
        return true;
    }

    bool VFXEditorDocument::Redo() {
        if (m_RedoStack.empty()) return false;

        auto cmd = std::move(m_RedoStack.back());
        m_RedoStack.pop_back();

        EffectEditCommand undoCmd;
        undoCmd.Label = cmd.Label;
        undoCmd.Before = m_Definition;
        undoCmd.After = cmd.After;
        undoCmd.SelectedEmitterId = m_SelectedEmitterId;
        m_UndoStack.push_back(std::move(undoCmd));

        m_Definition = std::move(cmd.After);
        m_SelectedEmitterId = cmd.SelectedEmitterId;

        m_CurrentRevision++;
        MarkDirty();
        return true;
    }

    void VFXEditorDocument::MarkDirty() {
        m_IsDirty = (m_CurrentRevision != m_SavedRevision);
    }

}
