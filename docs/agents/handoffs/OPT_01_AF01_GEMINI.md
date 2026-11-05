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

## Implement AF-01

1. Establish source-controlled authored metadata as the initial asset UUID identity.
   Preserve valid existing UUIDs in the supported schema; never replace identity
   with path hashes. Scope ignore rules so genuinely authored metadata can be
   tracked while test-generated, cache and intermediate sidecars remain ignored.
   Do not bulk-add developer-generated metadata or scan private projects into Git.
2. Define schema/version handling and canonical supported project/plugin asset-root
   identities without absolute machine paths. Builtin primitives use canonical
   builtin identities, not generated authored sidecars. This does not release the
   plugin framework or a universal serializer rewrite.
3. Allocate and atomically persist an ID once for a genuinely new import, checking
   persistence before publishing it. Missing metadata for an already-referenced
   asset, corrupt/unsupported metadata and duplicate UUIDs require explicit
   diagnostics/recovery. Never silently regenerate a referenced asset's identity.
4. Implement an explicit distinction between relocation (rename/move/project-root
   relocation preserves identity) and copying an asset as a new authored identity.
   Preserve serialized references and surface filesystem/save failures.
5. Put reusable atomic persistence at the appropriate shared CPU IO seam and use
   it for relevant project, scene, assembly and metadata saves. Reuse and correctly
   place existing atomic behavior; Core must not depend upward on Animation/Scene.
   Failed writes/replacements leave the previous valid file intact and report the
   actual error. Keep serializer/codec schema responsibilities with their owners.

Engine cleanliness is mandatory: no old-format support, legacy Vehicle holders,
compatibility readers, API wrappers or migration implementation. Unsupported data
may be rejected clearly. Keep current-format behavior and valid UUIDs stable.

Expected areas: AssetManager/AssetMetadataSerializer, narrowly affected shared IO
and serializer save helpers, .gitignore, tiny intentionally authored fixture files,
clean-checkout/relocation/failure tests and baseline documentation. Do not expand
the allowlist to hide new dependencies. ALLOW-008's CPU asset-format extraction is
owned by AF-04, not AF-01; do not perform that later extraction here.

## Verification and delivery

- Use real production APIs and tiny source-controlled fixtures in owned writable
  workspaces. Tests stay active in Debug/Release and use existing process/watchdog
  infrastructure. Never use the active Editor project as a test asset root.
- Verify a fresh temporary checkout/copy without developer caches resolves the
  same serialized references. Exercise rename/move and project-root relocation,
  explicit copy identity, duplicate UUIDs, corrupt/unsupported/missing metadata,
  new-import persistence and deterministic save/replacement failures. Compare
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
