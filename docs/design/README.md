# Active Architecture Designs

Start with **[the optimization roadmap](../ROADMAP.md)**. It owns execution order
and package progress. This folder contains only the contracts supporting the
current architecture cleanup; historical Phase 41/42/43 labels are domain IDs.

| Read when | Document | Purpose |
| --- | --- | --- |
| OPT-01..10 | [Master contract](ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) | Fixed decisions, AF packages and cross-plan dependencies |
| OPT-01..03 | [EnTT/enkiTS plan](ENTT_ENKITS_REFACTOR_PLAN.md) | Threading, task lifetime, ECS and shared simulation |
| OPT-04..05 | [NRI contract](NRI_INTEGRATION_CONTRACT.md) | Pinned vendor, device/resource/frame ownership |
| OPT-04..05 | [Rendering architecture](RHI_PRIMARY_RENDERER_ARCHITECTURE.md) | Shared rendering services and boundaries |
| OPT-04..05, 09/11 | [Rendering packages](RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md) | P41 contracts and acceptance |
| During rendering implementation | [Rendering coding reference](RHI_PRIMARY_RENDERER_IMPLEMENTATION_GUIDE.md) | Algorithms and test fixtures |
| OPT-06..09 | [Plugin architecture](PLUGIN_SYSTEM_ARCHITECTURE.md) | Editor DLLs, selected static Runtime and project plugins |
| OPT-06..09 | [Plugin packages](PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md) | SDK, Plugins window, fresh Vehicle and export |
| During plugin implementation | [Plugin coding reference](PLUGIN_SYSTEM_IMPLEMENTATION_GUIDE.md) | Lifecycle/build algorithms and fixtures |

Detailed contracts can include later packages needed to describe an end-to-end
architecture. Their presence does not advance them ahead of the OPT sequence.

## Removed from the active folder

Deferred feature plans and dated delivery/review reports are in the
[design archive](../archive/design/README.md). Material/VFX/Animation expansion,
old handoffs and completed delivery reports no longer occupy this folder.
Original standalone prompts remain in the [agent archive](../agents/archive/README.md);
redundant redirect files were removed.

Archive placement does not mean every old phase is accepted or complete. In
particular, [Vehicle retirement](../archive/design/reports/VEHICLE_RETIREMENT_DELIVERY.md)
is delivered with corrections required by the
[independent review](../reviews/OPT_00_VEHICLE_RETIREMENT_REVIEW.md). Use the roadmap
for current status.
