# Core Technical Design Specifications & Contracts

The active execution order is the root [ROADMAP.md](../../ROADMAP.md). Milestone-level plans and companion architectures live under [docs/milestones](../milestones/).

This directory contains the standing technical contracts and deep specifications referenced across engine subsystems:

| Domain | Contract / Specification | Purpose |
| :--- | :--- | :--- |
| **Architecture Foundation** | [`ARCHITECTURE_FOUNDATION_MASTER_PLAN.md`](ARCHITECTURE_FOUNDATION_MASTER_PLAN.md) | Layering invariants, decoupling mandates, and subsystem ownership. |
| **Hardware Abstraction** | [`NRI_INTEGRATION_CONTRACT.md`](NRI_INTEGRATION_CONTRACT.md) | Low-level NVIDIA NRI integration contract for Direct3D 12 and Vulkan. |
| **Modern Renderer** | [`RHI_PRIMARY_RENDERER_ARCHITECTURE.md`](RHI_PRIMARY_RENDERER_ARCHITECTURE.md) | RenderService, RenderWorld snapshots, state tracking, and fallback policies. |
| **Material Pipeline** | [`OPT_12_MATERIAL_GRAPH_FORMAT.md`](OPT_12_MATERIAL_GRAPH_FORMAT.md) | Node-based material AST, document model, and DXC shader lowering format. |
| **Plugin Architecture** | [`PLUGIN_SYSTEM_ARCHITECTURE.md`](PLUGIN_SYSTEM_ARCHITECTURE.md) | Manifest schema, dynamic DLL host session, and static link runtime export. |
| **Plugin SDK** | [`PLUGIN_SDK_AUTHORING.md`](PLUGIN_SDK_AUTHORING.md) | C-ABI developer guide for authoring external plugins and modules. |
| **Algorithms & Math** | [`OPT_11_ARCHITECTURE_AND_ALGORITHMS.md`](OPT_11_ARCHITECTURE_AND_ALGORITHMS.md) | Core numerical formulations, lighting approximations, and scheduler math. |

Historical sub-package execution plans, task breakdowns, and completed delivery reports have been archived under [`docs/archive/design/`](../archive/design/).
