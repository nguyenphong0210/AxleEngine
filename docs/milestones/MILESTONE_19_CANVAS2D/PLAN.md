# Milestone 19 — Canvas2D and In-Game UI Authoring Plan

**Status** — Proposed. Release implementation by checkpoint after M18 acceptance.
**Phase 48** — Documents and layout (48.1), authoring and input (48.2), rendering and product acceptance (48.3).
**Architecture** — [Canvas documents, layout, input and rendering](ARCHITECTURE.md).
**Handoffs** — [M20 packaging](../MILESTONE_20_COOKER/PLAN.md), [M23 Material Graph and Scene Tools](../MILESTONE_23_MATERIAL_GRAPH_TOOLING/PLAN.md).

## 1. Starting point and deliverable

Reuse the `.axleui` document, layout, `UIDataContext`, `UICanvasComponent`, renderer and current [Canvas Designer](../../../Engine/UI/src/UICanvasDesignerPanel.cpp). Use [M08 Phase 34](../MILESTONE_08_VFX_RHI_CROSSPLATFORM/PLAN.md) as the historical source baseline and reproduce current behavior before extending it. Deliver a visual editor and shipping UI path for an interactive HUD, with responsive layout, typed data flow, stable UI actions, undo/redo and portable assets. The Material Graph node editor belongs to [M16 Material Graph Studio](../MILESTONE_16_RAY_TRACING/PLAN.md#material-graph-studio).

## 2. Execution and acceptance checklist

| Checkpoint | Implementation and independently reproducible gate |
| --- | --- |
| **M19-A1 · Documents and lifecycle** | Define stable node identity and validated authored state. Connect document transactions, save/reload, dirty guards and dependency enumeration to the real Designer. |
| **M19-A2 · Runtime layout** | Freeze anchor, sizing, flow, clipping, stacking and DPI rules. Validate deterministic rectangles and hit order across target sizes. |
| **M19-B1 · Visual authoring** | Add node hierarchy, drag/drop, canvas selection, handles, property inspector and responsive preview. Prove production edit/save/reopen round trips. |
| **M19-B2 · Binding and input** | Wire typed value bindings, input routing and named actions to Scene/Runtime. Test pointer capture, focus, keyboard activation and ownership on reload and teardown. |
| **M19-C1 · Render and asset parity** | Verify screen-space and supported render-to-texture paths, textures/fonts, clipping and visual equivalence for the same authored HUD. |
| **M19-C2 · Product acceptance** | Deliver the representative HUD in a packaged Runtime that exercises live game data and a real game action. Verify compatibility and consolidate review. |

A1–A2 implement Phase 48.1, B1–B2 implement Phase 48.2, and C1–C2 close Phase 48.3.

- [ ] **M19-01 · Authoring document (A1).** Open a real `.axleui` file into a typed working model with stable identities. Validate parentage and component values. Save atomically, preserve undo/redo branches and dirty state, and retain the previous valid preview following invalid direct source edits. Declare authored and template dependency examples for M20.
- [ ] **M19-02 · Layout and sizing (A2).** Cover anchors, offsets, pivot, min/max constraints, padding, spacing, row/column layout, clipping, sibling order and visibility. Verify logical versus physical coordinates at 16:9, ultrawide and a narrow viewport with non-default DPI. Test clipping, zero area and nested containers.
- [ ] **M19-03 · Visual editing (B1).** Create, select, move, resize, duplicate, reorder, reparent and delete nodes. Expose Inspector editing for core properties and text/image resources. Group drags into single undo steps and keep hierarchy, canvas and source views synchronized. Test save/reopen, failed save, canceled discard and resource pickers.
- [ ] **M19-04 · Core controls (B1–B2).** Round-trip panel, text, image, button, progress/arc and container controls. Add scroll clipping and navigation where the HUD uses them. Verify disabled/hidden behavior, game focus suppression, mouse/touch-compatible pointer dispatch and keyboard activation. Scope the activation/focus matrix by supported input device.
- [ ] **M19-05 · Live data and actions (B2).** Bind text, progress, visibility or enabled state to typed gameplay values with defaults. Actions dispatch through a scene-owned handler boundary. Exercise stale references, focus cleanup, overlapping canvases, Play/Stop, reload and scene changes using a real gameplay action.
- [ ] **M19-06 · Native drawing and parity (C1).** Use the shipping render path for the Designer's comparable preview, with isolated sample data and input. Validate real fonts/images, frame resize, clipping, blending and optional texture output. Record fallback or unsupported policies for platform/backend/input combinations and avoid implicit live scene changes.
- [ ] **M19-07 · Export and acceptance (C2).** Package the HUD and its transitive fonts/images using existing export. In the detached Runtime, publish a changing game value and trigger a named action from the authored UI. Record required-missing and optional-missing asset behavior, actual interaction and image captures.

## 3. Evidence and exit

Use existing parser/layout/hit-test and Canvas2D regression coverage as a baseline. Add focused tests for transaction boundaries, deterministic layout, input/focus and asset reference traversal. Rebuild the relevant test executables, Editor and Runtime in Debug and Release. Record native D3D12 and Vulkan Editor interaction, loose and packaged Runtime evidence, and declared OpenGL compatibility. Compare exact layout/action traces where deterministic and capture pixels with a recorded tolerance for comparable text and image fixtures.

Archive source identity, matrix, fixtures, diagnostics and requirement dispositions in `docs/reviews/`. Hand M20 a real `.axleui` document attached to a scene with required image and font dependencies. M19 closes on the consolidated independent review of the final source.
