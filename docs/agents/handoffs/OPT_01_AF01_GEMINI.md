# Gemini handoff: OPT-01 / AF-01 only

Date: **2026-11-05**. Implementer: **Gemini**. Coordinator/reviewer: **Codex**.
User authority: finish the AF-00/EJ-00 corrections, then provide the next package
prompt in English. The next released package is **AF-01: stable asset identity
and safe persistence**. Implementation preparation baseline: `af5721b`; record
actual starting HEAD after the documentation/handoff commits.

## Start here

Read root GEMINI.md and AGENTS.md, docs/agents/WORKING_PROTOCOL.md, the current
exchange task card and your inbox using agent_exchange.py. Read:

- docs/ROADMAP.md, OPT-01.
- docs/design/ARCHITECTURE_FOUNDATION_MASTER_PLAN.md, AF-01 and relevant prerequisites.
- docs/architecture/OPT_01_BASELINE_INVENTORY.md and its JSON snapshot.
- docs/reviews/OPT_01_BASELINE_CORRECTIONS.md.

Inspect current source and Git status/HEAD. ACK the Codex HANDOFF in the exchange
as Gemini, with starting commit and expected files, before editing. Preserve all
unrelated changes. Historical planning-only/unreleased statements do not override
this bounded user-directed AF-01 assignment.

AF-00/EJ-00 corrections were authored and verified by Codex, not independently
reviewed by another agent after the fixes. Keep that evidence distinction. OPT-00
has not been marked accepted; do not restore its superseded legacy requirements.

## Latest user steering: no per-asset sidecars

The user explicitly rejects generating many .meta files. This overrides the
original sidecar-oriented handoff and Gemini ACK of that earlier scope.
**Do not generate per-asset .meta files, or substitute .uid/.import sidecars.**
Use one source-controlled versioned asset registry per independently owned asset
root (normally one project registry; package-local registries only for existing
independently distributed roots). A suitable default project filename is
AssetRegistry.axleregistry. Caches and imported/compiled outputs remain ignored.
This registry is authored project data even when the Editor initially creates it.

A canonical registry owns UUID/path/import-setting records for current assets.
Native formats with an already-persisted asset UUID must agree with that mapping;
do not create two independently generated identities. Do not add embedded UUIDs
to every native format as a universal serializer rewrite. No legacy .meta reader,
converter or fallback is required. Existing sidecar-based code must not remain
as a second active identity system.

## Implement AF-01

1. Implement the source-controlled root registry as the stable identity authority.
   Use deterministic text ordering and schema/version validation. Keep UUIDs stable
   in the supported schema, independent of paths/content hashes. Track intentionally
   authored registry fixtures; keep per-asset .meta files and generated caches ignored.
   Do not bulk-add developer-generated metadata or scan private projects into Git.
2. Define canonical root-relative project/package paths without absolute machine
   paths. Builtin primitives use canonical builtin identities. Existing native
   identity fields must be consistent with the registry; avoid independent duplicate
   authorities. This does not release a plugin framework or universal serializer rewrite.
3. Allocate an ID once for a genuinely new import and atomically persist its registry
   entry before publishing it. Corrupt/unsupported registries, duplicate UUIDs/paths,
   unresolved referenced IDs and missing registry entries require diagnostics/recovery.
   Never silently regenerate a referenced asset's identity or use content hash as UUID.
4. Editor rename/move must update the registry path without changing UUID; copying
   a project root preserves identity, while copying an asset as a new asset follows
   an explicit new-identity policy. If a file moves outside Editor, detect the
   unresolved entry and provide explicit relink preserving the UUID. Do not claim
   arbitrary external moves can be matched automatically without evidence. Identical
   files make content hashes ambiguous; fingerprints may assist recovery only.
5. Put reusable atomic persistence at the appropriate shared CPU IO seam for the
   registry and relevant project/scene/assembly saves. Registry failure must leave
   both the prior valid file and published in-memory identity state unchanged;
   coordinate rename/relink failure recovery explicitly. Reuse correctly placed
   atomic behavior; Core must not depend upward on Animation/Scene. Keep serializer
   schema responsibilities with owners. A missing registry for already-referenced
   project data must not be silently replaced with newly generated identities.

Engine cleanliness is mandatory: no old-format support, legacy Vehicle holders,
compatibility readers, API wrappers or migration implementation. Unsupported data
may be rejected clearly. Keep current-format behavior and valid UUIDs stable.

Expected areas: AssetManager and the canonical registry serializer, retirement of
active sidecar generation paths, narrowly affected shared IO/serializer save helpers,
.gitignore registry/cache policy, tiny intentionally authored registry/reference fixtures,
clean-checkout/relocation/failure tests and baseline documentation. Do not expand
the allowlist to hide new dependencies. ALLOW-008's CPU asset-format extraction is
owned by AF-04, not AF-01; do not perform that later extraction here.

## Verification and delivery

- Use real production APIs and tiny source-controlled fixtures in owned writable
  workspaces. Tests stay active in Debug/Release and use existing process/watchdog
  infrastructure. Never use the active Editor project as a test asset root.
- Verify a fresh temporary checkout/copy without developer caches resolves the
  same serialized references. Exercise rename/move and project-root relocation,
  explicit copy identity, external moves and relink, identical-content ambiguity,
  duplicate UUIDs/paths, corrupt/unsupported/missing registries or referenced entries,
  new-import persistence and deterministic save/replacement failures. Assert that
  discovery/import/save/rename/reopen create no per-asset sidecars. Compare
  actual UUIDs and prior file bytes, not only tags, counters or printed PASS.
- Rebuild affected targets in Debug/Release before execution. Build Editor and
  Runtime after shared Engine/serializer changes; distinguish builds from actual
  product, manual UI and export execution. Run architecture
  checks (including mutation controls), baseline tool regressions, jobs/ECS controls
  and relevant project/material/VFX/animation/serialization suites. Keep known
  ECS/jobs baseline defects visible; this task must not fix them or mask them.
- Record exact commands, configuration, exit codes, reproducible inputs/logs and
  NOT RUN limits. Update roadmap/task state as delivered and review-pending, not
  self-accepted. Keep historical reports/results intact.
- Make coherent local commits with explicit staging; no push. Repository code,
  comments, documents and commits use English.
- Post DELIVERY as Gemini, linked to the Codex HANDOFF, with actual implementation
  commits, evidence and limitations. Stop for Codex review after AF-01.

Do not start P41-00, PLG-00, AF-02+, EJ-01+, rendering/NRI, plugin/Vehicle work,
vendor upgrades, feature expansion or unrelated production fixes.
