#include "VFXTestHarness.hpp"
#include <Axle/VFXEditorDocument.hpp>
#include <filesystem>
#include <fstream>

namespace {
    bool TestBaselineDocumentContract() {
        Axle::VFXEditorDocument doc;
        VFX_TEST_CHECK(!doc.GetDefinition().Name.empty(), "Document initialized with default effect name");
        VFX_TEST_CHECK(doc.GetDefinition().Emitters.size() == 1, "Document initialized with exactly one default emitter");
        VFX_TEST_CHECK(!doc.IsDirty(), "New document is clean");
        return true;
    }

    bool TestDocumentAddDuplicateRemoveEmitter() {
        Axle::VFXEditorDocument doc;
        std::vector<Axle::VFXDiagnostic> diags;

        std::string e2Id = doc.AddEmitter("Sparks");
        VFX_TEST_CHECK(!e2Id.empty(), "Added emitter has valid ID");
        VFX_TEST_CHECK(doc.GetDefinition().Emitters.size() == 2, "Document has 2 emitters");
        VFX_TEST_CHECK(doc.GetSelectedEmitterId() == e2Id, "Selected emitter updated to newly added emitter");

        std::string dupId = doc.DuplicateEmitter(e2Id);
        VFX_TEST_CHECK(!dupId.empty(), "Duplicated emitter has valid ID");
        VFX_TEST_CHECK(dupId != e2Id, "Duplicated emitter assigned fresh unique ID");
        VFX_TEST_CHECK(doc.GetDefinition().Emitters.size() == 3, "Document has 3 emitters after duplication");

        bool removed = doc.RemoveEmitter(dupId, diags);
        VFX_TEST_CHECK(removed, "Successfully removed duplicated emitter");
        VFX_TEST_CHECK(doc.GetDefinition().Emitters.size() == 2, "Document has 2 emitters after removal");

        // Attempting to remove until 0 must fail and diagnose
        doc.RemoveEmitter(e2Id, diags);
        VFX_TEST_CHECK(doc.GetDefinition().Emitters.size() == 1, "Only 1 emitter left");
        bool removeLast = doc.RemoveEmitter(doc.GetDefinition().Emitters[0].Id, diags);
        VFX_TEST_CHECK(!removeLast, "Cannot remove the only emitter");
        VFX_TEST_CHECK(doc.GetDefinition().Emitters.size() == 1, "Emitter count stays at 1");

        return true;
    }

    bool TestDocumentDirtyAndSaveState() {
        Axle::VFXEditorDocument doc;
        std::vector<Axle::VFXDiagnostic> diags;

        std::string tempPath = (std::filesystem::temp_directory_path() / "test_vfx_doc_dirty.axle_vfx").string();

        // 1. Initial state is clean
        VFX_TEST_CHECK(!doc.IsDirty(), "Initial document is clean");

        // 2. Edit -> Dirty
        doc.AddEmitter("Flame");
        VFX_TEST_CHECK(doc.IsDirty(), "Document dirty after adding emitter");

        // 3. Save -> Clean
        bool saved = doc.SaveAs(tempPath, diags);
        VFX_TEST_CHECK(saved, "Saved successfully");
        VFX_TEST_CHECK(!doc.IsDirty(), "Document clean after save");

        // 4. Edit -> Dirty again
        doc.AddEmitter("Smoke");
        VFX_TEST_CHECK(doc.IsDirty(), "Document dirty after second edit");

        // 5. Undo back to saved state -> Clean
        bool undone = doc.Undo();
        VFX_TEST_CHECK(undone, "Undo succeeded");
        VFX_TEST_CHECK(!doc.IsDirty(), "Document clean again after undo back to saved revision");

        // 6. Redo -> Dirty again
        bool redone = doc.Redo();
        VFX_TEST_CHECK(redone, "Redo succeeded");
        VFX_TEST_CHECK(doc.IsDirty(), "Document dirty again after redo past saved revision");

        std::filesystem::remove(tempPath);
        return true;
    }

    bool TestDocumentFailedSavePreservesState() {
        Axle::VFXEditorDocument doc;
        std::vector<Axle::VFXDiagnostic> diags;

        doc.AddEmitter("Debris");
        VFX_TEST_CHECK(doc.IsDirty(), "Document is dirty");

        // Attempt save without path
        bool savedNoPath = doc.Save(diags);
        VFX_TEST_CHECK(!savedNoPath, "Save without path fails");
        VFX_TEST_CHECK(doc.IsDirty(), "Dirty state preserved on failed save");
        VFX_TEST_CHECK(doc.GetDefinition().Emitters.size() == 2, "Working data preserved on failed save");

        return true;
    }

    bool TestDocumentReorderAndRename() {
        Axle::VFXEditorDocument doc;
        std::string e1 = doc.GetDefinition().Emitters[0].Id;
        std::string e2 = doc.AddEmitter("Second");
        std::string e3 = doc.AddEmitter("Third");

        // Initial order: e1, e2, e3
        VFX_TEST_CHECK(doc.GetDefinition().Emitters[0].Id == e1, "Emitter 0 is e1");
        VFX_TEST_CHECK(doc.GetDefinition().Emitters[1].Id == e2, "Emitter 1 is e2");
        VFX_TEST_CHECK(doc.GetDefinition().Emitters[2].Id == e3, "Emitter 2 is e3");

        // Reorder: move e1 to position 2 -> e2, e3, e1
        bool reordered = doc.ReorderEmitter(0, 2);
        VFX_TEST_CHECK(reordered, "Reorder succeeded");
        VFX_TEST_CHECK(doc.GetDefinition().Emitters[0].Id == e2, "Emitter 0 is now e2");
        VFX_TEST_CHECK(doc.GetDefinition().Emitters[1].Id == e3, "Emitter 1 is now e3");
        VFX_TEST_CHECK(doc.GetDefinition().Emitters[2].Id == e1, "Emitter 2 is now e1");

        // Rename e2
        bool renamed = doc.RenameEmitter(e2, "PrimaryRenamed");
        VFX_TEST_CHECK(renamed, "Rename succeeded");
        VFX_TEST_CHECK(doc.FindEmitterById(e2)->Name == "PrimaryRenamed", "Name updated");
        VFX_TEST_CHECK(doc.FindEmitterById(e2)->Id == e2, "Stable ID unchanged after rename");

        return true;
    }
}

static bool TestHistoryRestoresValuesAndBranchIdentity() {
    Axle::VFXEditorDocument doc;
    auto original = doc.GetDefinition();
    auto originalSelection = doc.GetSelectedEmitterId();
    auto added = doc.AddEmitter("Second");
    auto edited = doc.GetDefinition();
    VFX_TEST_CHECK(doc.Undo() && doc.GetDefinition() == original, "Undo restores values");
    VFX_TEST_CHECK(doc.GetSelectedEmitterId() == originalSelection, "Undo restores previous selection");
    VFX_TEST_CHECK(doc.Redo() && doc.GetDefinition() == edited, "Redo restores added emitter, not the undo snapshot");
    VFX_TEST_CHECK(doc.GetSelectedEmitterId() == added, "Redo restores edited selection");
    Axle::VFXTests::ScopedTempDir temp;
    std::vector<Axle::VFXDiagnostic> diagnostics;
    VFX_TEST_CHECK(doc.SaveAs((temp.Path / "saved.axle_vfx").string(), diagnostics), "Save history state");
    VFX_TEST_CHECK(doc.Undo(), "Undo saved edit");
    doc.AddEmitter("Different branch");
    VFX_TEST_CHECK(doc.IsDirty(), "New branch at same history depth is dirty");
    VFX_TEST_CHECK(!doc.CanRedo(), "Branch discards stale redo");
    auto beforeTransaction = doc.GetDefinition();
    doc.BeginTransaction();
    doc.AddEmitter("Third");
    doc.AddEmitter("Fourth");
    doc.CommitTransaction("Grouped edit");
    VFX_TEST_CHECK(doc.Undo() && doc.GetDefinition() == beforeTransaction, "Transaction is one undo step");
    auto config = doc.GetDefinition().Emitters[0].Config;
    config.Drag += 0.123f;
    doc.ReplaceEmitterConfig(doc.GetDefinition().Emitters[0].Id, config);
    VFX_TEST_CHECK(doc.Undo() && doc.GetDefinition() == beforeTransaction, "Non-core module edits participate in history");
    return true;
}

bool RunVFXDocumentUnitTests() {
    VFX_TEST_CHECK(TestHistoryRestoresValuesAndBranchIdentity(), "History regression failed");
    std::cout << "  - Running TestBaselineDocumentContract ...\n";
    VFX_TEST_CHECK(TestBaselineDocumentContract(), "TestBaselineDocumentContract failed");

    std::cout << "  - Running TestDocumentAddDuplicateRemoveEmitter ...\n";
    VFX_TEST_CHECK(TestDocumentAddDuplicateRemoveEmitter(), "TestDocumentAddDuplicateRemoveEmitter failed");

    std::cout << "  - Running TestDocumentDirtyAndSaveState ...\n";
    VFX_TEST_CHECK(TestDocumentDirtyAndSaveState(), "TestDocumentDirtyAndSaveState failed");

    std::cout << "  - Running TestDocumentFailedSavePreservesState ...\n";
    VFX_TEST_CHECK(TestDocumentFailedSavePreservesState(), "TestDocumentFailedSavePreservesState failed");

    std::cout << "  - Running TestDocumentReorderAndRename ...\n";
    VFX_TEST_CHECK(TestDocumentReorderAndRename(), "TestDocumentReorderAndRename failed");

    return true;
}
