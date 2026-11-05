# Architecture Violation Fixture (AF-00)

This fixture intentionally introduces forbidden architectural boundary violations:
1. `Engine/Core` source code includes `Axle/Scene.hpp` (Scene layer) and `Axle/ImGuiLayer.hpp` (UI layer).
2. `Engine/Core/CMakeLists.txt` links `Axle::Scene` and `Axle::UI`.

Neither edge is permitted by `scripts/architecture_rules.json` nor covered by any baseline allowance.
Executing `python scripts/check_architecture.py --root Tests/Fixtures/ArchitectureViolationFixture` must detect both violations and exit with code 1.
