#pragma once

#include <Axle/ParticleEffectDefinition.hpp>
#include <Axle/ParticleEffectSerializer.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace Axle {

    struct EffectEditCommand {
        std::string Label;
        ParticleEffectDefinition Before;
        ParticleEffectDefinition After;
        std::string SelectedEmitterId;
    };

    class VFXEditorDocument {
    public:
        VFXEditorDocument();
        ~VFXEditorDocument() = default;

        // Lifecycle & File IO
        void NewEffect(const std::string& effectName = "NewEffect");
        bool OpenEffect(const std::string& filePath, std::vector<VFXDiagnostic>& diagnostics);
        bool Save(std::vector<VFXDiagnostic>& diagnostics);
        bool SaveAs(const std::string& newPath, std::vector<VFXDiagnostic>& diagnostics);

        const std::string& GetFilePath() const { return m_FilePath; }
        bool IsDirty() const { return m_IsDirty; }
        uint64_t GetCurrentRevision() const { return m_CurrentRevision; }

        // Effect Definition Access
        const ParticleEffectDefinition& GetDefinition() const { return m_Definition; }
        ParticleEffectDefinition& GetDefinition() { return m_Definition; }
        void SetEffectName(const std::string& name);

        // Emitter Operations
        const std::string& GetSelectedEmitterId() const { return m_SelectedEmitterId; }
        void SelectEmitter(const std::string& id);
        EmitterDefinition* GetSelectedEmitter();
        const EmitterDefinition* GetSelectedEmitter() const;
        EmitterDefinition* FindEmitterById(const std::string& id);
        const EmitterDefinition* FindEmitterById(const std::string& id) const;

        std::string AddEmitter(const std::string& name = "New Emitter", const ParticleEmitterConfig& cfg = {});
        std::string DuplicateEmitter(const std::string& id);
        bool RemoveEmitter(const std::string& id, std::vector<VFXDiagnostic>& diagnostics);
        bool RenameEmitter(const std::string& id, const std::string& newName);
        bool ReorderEmitter(size_t fromIndex, size_t toIndex);
        bool SetEmitterEnabled(const std::string& id, bool enabled);
        bool ReplaceEmitterConfig(const std::string& id, const ParticleEmitterConfig& cfg);
        bool SetEmitterResources(const std::string& id, const EmitterResources& res);

        // Undo / Redo
        void BeginTransaction();
        void CommitTransaction(const std::string& label);
        bool Undo();
        bool Redo();
        bool CanUndo() const { return !m_UndoStack.empty(); }
        bool CanRedo() const { return !m_RedoStack.empty(); }

    private:
        void RecordChange(const std::string& label, const ParticleEffectDefinition& before);
        void MarkDirty();

    private:
        ParticleEffectDefinition m_Definition;
        std::string m_FilePath;
        std::string m_SelectedEmitterId;

        bool m_IsDirty = false;
        uint64_t m_CurrentRevision = 0;
        uint64_t m_SavedRevision = 0;

        std::optional<ParticleEffectDefinition> m_TransactionBefore;

        std::vector<EffectEditCommand> m_UndoStack;
        std::vector<EffectEditCommand> m_RedoStack;
    };

}

