# Phase 00 — Architecture foundation

## Target

Keep Core, SceneData, Asset, Scene, Renderer, RHI, Physics, Plugin and product
boundaries explicit. Ownership, lifetime, serialization and fallback behavior
must be testable without relying on Editor state.

## Implementation

Use immutable RenderWorld snapshots, owner-scoped jobs, shared RenderService,
transactional native resources, selected Runtime composition and explicit plugin
boundaries. Preserve the pinned NRI/Jolt/enkiTS contracts.

## Technology and limits

EnTT owns scene data, enkiTS owns jobs, Jolt owns physics and NRI owns native
device/resource operations. OpenGL remains a compatibility backend. Legacy
Phase labels are historical references only.
