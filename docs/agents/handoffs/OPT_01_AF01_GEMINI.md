# AF-01 scope and delivery record

Status: Gemini original delivery reviewed and corrected by Codex under the user's
explicit review-and-fix instruction. See
[combined review](../../reviews/OPT_01_AF01_P41_PLG_REVIEW.md).
This file preserves the bounded AF-01 contract; it does not release another package.

## Latest storage contract

The user's final decision supersedes the earlier registry handoff:

- PNG and other raw external resources use canonical root-relative paths, UUID zero.
- Native Engine assets embed a versioned UUID inside the asset itself:
  "# AxleFileIdentity: 1 <uint64>" as the first line.
- No .meta/.uid/.import sidecars, no authored central registry, no legacy reader or migration.
- Discovery rebuilds a read-only in-memory index; it does not repair or regenerate IDs.
- Duplicate native IDs quarantine all copies. Malformed or unsupported IDs are rejected.
- New-document/import identity is persisted atomically before publication.
- Native move/root relocation preserves ID; explicit native copy creates a new ID.
- Raw rename updates supported path fields; external raw moves need explicit relink.
- Save/replacement failures preserve prior bytes. Multi-file reference rewrite uses
  best-effort rollback, not a crash-atomic multi-file transaction.
- Builtin primitive IDs remain reserved. Derived caches remain disposable and ignored.

## Bounded implementation and verification

Keep the shared atomic writer in Core without introducing upward dependencies.
Update native serializer save seams so saves retain identity. Use real production
APIs and tiny tracked fixtures in owned writable workspaces. Include native/raw
copy and move, actual scene/material references, cold root relocation, invalid and
duplicate IDs, failed persistence, and mounted-package lookup without loose files.
Prove no auxiliary identity files are created. Rebuild affected tests and products
in Debug/Release; distinguish product builds, automated runs and manual UI evidence.

The original registry implementation (7e79d22) followed an earlier recorded scope.
Its delivery report remains historical evidence, not the active format contract.
The review report distinguishes that scope correction from implementation defects.

AF-04 owns the later CPU format/resource split. Do not expand dependencies or start
AF-02+, EJ-01+, NRI/backend migration, plugin framework or Vehicle implementation
from this historical handoff.
