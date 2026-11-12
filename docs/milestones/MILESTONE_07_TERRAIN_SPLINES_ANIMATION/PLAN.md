# Milestone 07 — Large-Scale Terrain, Spline Tracks & Skeletal Character Systems Plan

**Status:** Completed Historical Milestone.
**Parent Document:** [`ROADMAP.md`](../../ROADMAP.md).

---

## Work Breakdown & Historical Checklist

- [x] **Phase 29: Modular Plugin Architecture — Spline Track & Large-Scale Terrain System (`Plugins/Spline` & `Plugins/Terrain`)**
  - [x] **Phase 29A: Spline Curve & Procedural Track Generator Plugin (`Plugins/Spline`)**
    - [x] **Step 1 - Plugin Infrastructure & CMake Integration**: Establish the modular static plugin standard under `Plugins/Spline/` with clean linkage into `AxleEditor`, `AxleRuntime`, and `AxleTests`.
    - [x] **Step 2 - 3D Curve Mathematics Core (`Curve3D`)**: Parametric curves (Catmull-Rom & Cubic Bézier), arc-length reparameterization (uniform speed sampling), and Rotation Minimizing Frame (RMF / Parallel Transport) to prevent 180° flipping on steep loops and banked turns.
    - [x] **Step 3 - `SplineComponent` & Procedural Track Extrusion**: Extrude custom 2D cross-sections (Asphalt road, curbs, rumble strips, guardrails) along 3D splines with variable width, roll/banking angles, and continuous texture UV mapping.
    - [x] **Step 4 - Physics Scene Integration**: Dynamically generate high-performance static collision meshes (`JPH::MeshShape`) via `Axle::Physics` so vehicles can drive, bank, and collide on the generated tracks.
    - [x] **Step 5 - Viewport Spline Gizmo & Editor Tooling**: Interactive Editor gizmos for selecting, adding, moving, and curving control points directly within the 3D Viewport.
    - [x] **Step 6 - Automated Test Suite 33 (`AxleTests.exe`)**: Unit tests for curve arc-length interpolation, tangent/normal frame continuity, track mesh triangle generation, and serialization.
  - [x] **Phase 29B: Off-Road Terrain & Sculpting System Plugin (`Plugins/Terrain`)**
    - [x] **Step 1 - Plugin Setup & Heightfield Physics Engine**: Establish `Plugins/Terrain/` and integrate high-performance `JPH::HeightFieldShape` for realistic off-road vehicle suspension travel and tire damping on irregular soil.
    - [x] **Step 2 - Multithreaded Chunked Grid & LOD (`enkiTS`)**: Divide large terrains into hierarchical chunks with distance-based Level of Detail (LOD), dynamically generated without hitching via `enkiTS` worker threads.
    - [x] **Step 3 - Interactive Sculpting Tools (Editor)**: Viewport circular brushes (Raise, Lower, Smooth, Flatten) with real-time vertex buffer deforming and asynchronous Jolt physics shape re-baking.
    - [x] **Step 4 - Multi-Layer Splatmap & Triplanar Shaders**: Dynamic multi-texture blending (Grass, Dirt, Gravel, Mud, Rock) with slope-based triplanar projection shaders and texture painting brush tools.
    - [x] **Step 5 - Spline Road Carving & Terrain Snapping**: One-way integration allowing the Terrain plugin to query `SplineComponent` paths and automatically carve/flatten terrain elevations directly beneath road ribbons.
    - [x] **Step 6 - Automated Test Suite 34 (`AxleTests.exe`)**: Unit tests for heightfield matrix generation, brush deformation math, LOD stitching, and terrain save/load serialization.
- [x] **Phase 30: Skeletal Animation & Character Controller System (`Engine/Animation`)**
  - [x] **Step 1 - Core Skeletal Data Structures & Asset Importer (`ufbx` & `cgltf`)**:
    - [x] Native bone hierarchy, joint indices, inverse bind matrices (`InverseBindPose`), and skeleton metadata.
    - [x] Animation track sampling data structures (`AnimationClip`, `AnimationChannel`, keyframe timestamps, translation/rotation/scale curves).
    - [x] Lightweight, zero-dependency asset parsing using embedded `ufbx` (`.fbx`) and `cgltf` (`.gltf`/`.glb`).
  - [x] **Step 2 - GPU Vertex Skinning & Shader Pipeline (`Engine/Renderer`)**:
    - [x] Extended vertex layout: `a_BoneIDs` (`ivec4`) and `a_BoneWeights` (`vec4`) supporting up to 4 influencing bones per vertex (64-byte aligned stride).
    - [x] Dynamic bone matrix palette (`u_BoneMatrices[128]`) updated per animated draw call via `SetMat4Array` with identity pose fallback.
    - [x] Optimized Linear Blend Skinning (LBS) vertex shaders (`SkeletalMesh.vert` and `SkeletalShadowDepth.vert`) for PBR lighting pass and directional shadow depth pass.
    - [x] Dedicated `SkinningPipeline` decoupled renderer class integrated directly into `SceneRenderer`.
    - [x] Automated test suite Step 37 in `Editor/src/Test.cpp` verifying shader compilation, dynamic bone palette upload, 64-byte layout verification, and animated skinning dispatch.
  - [x] **Step 3 - Animation Pose Evaluator & Blending Tree (`Engine/Animation`)**:
    - [x] Runtime track sampling with shortest-path spherical linear interpolation (`glm::slerp`) for quaternions and linear interpolation (`glm::mix`) for translation/scale via `BoneTransform` & `AnimationPose`.
    - [x] Hierarchical forward kinematics solving global bone transform matrices and GPU skinning palettes from local bone transforms.
    - [x] 1D Blend Spaces (`BlendSpace1D`): Smooth locomotion interpolation (Idle $\leftrightarrow$ Walk $\leftrightarrow$ Run) with phase-matched footstep time synchronization.
    - [x] Hierarchical Animation State Machine (`AnimationStateMachine`): Clip & BlendSpace states, parameter control (`Speed`, `IsGrounded`, `VerticalVelocity`, triggers), smooth cross-fading, exit times, and state transitions (Locomotion $\leftrightarrow$ Jump $\leftrightarrow$ Fall $\leftrightarrow$ Land).
    - [x] High-level runtime `Animator` controller with direct GPU skinning palette upload to `SkinningPipeline`.
    - [x] Automated test suite Step 38 in `Editor/src/Test.cpp` verifying SLERP/LERP arithmetic, 1D blend spaces, crossfades, complete jump lifecycle, and GPU skinning dispatch.
  - [x] **Step 4 - Kinematic Character Controller (KCC) via Jolt Physics (`Engine/Physics`)**:
    - [x] Robust character physics integration using `JPH::CharacterVirtual` with capsule collision shape (`CharacterController`).
    - [x] Slope traversal handling (`MaxSlopeAngle`), automatic stair/curb stepping (`StepOffset`), and ground snapping (`StickToFloor`).
    - [x] Dynamic collision response with dynamic physics objects (pushback resistance and forward impulse transfer to rigid bodies).
    - [x] Automated test suite Step 39 in `Editor/src/Test.cpp` verifying freefall landing, kinematic movement & jump trajectory, stair stepping, steep wall blocking, and dynamic box pushing.
  - [x] **Step 5 - ECS Integration & Third-Person Gameplay Controller**:
    - [x] Decoupled ECS components: `SkeletalMeshComponent`, `AnimatorComponent`, and `CharacterControllerComponent` in `Engine/Scene`.
    - [x] Third-person player controller script (`ThirdPersonController.hpp`) supporting camera-relative directional movement, smooth yaw turning toward velocity, jump action, and automatic animator parameter synchronization (`Speed`, `IsGrounded`, `VerticalVelocity`).
    - [x] Third-person chase camera script (`ThirdPersonCamera.hpp`) with spring-arm orbit, mouse look, pitch clamping, target tracking, and height offset.
    - [x] Scene renderer integration: `Scene::RenderSkeletalMeshes` dispatches dynamic GPU bone palettes to both forward PBR lighting and shadow depth passes.
    - [x] Scene serialization & deserialization support for all 3 components in `SceneSerializer`.
    - [x] Automated test suite Step 40 in `Editor/src/Test.cpp` validating component attachment, kinematics, animator sync, camera-relative movement vectors, spring-arm camera, and full serialization fidelity.
  - [x] **Step 6 - Character Sandbox Playground & Scene Integration**:
    - [x] Interactive 3D scene (`CharacterSandbox.axlescene`) integrating animated skeletal mesh, third-person camera, stairs, slopes, and dynamic obstacles.
    - [x] Automated test suite Step 41 in `Editor/src/Test.cpp` validating full character sandbox scene, staircase ascent via `StepOffset`, 90-degree wall blocking, dynamic crate push, spring-arm follow, and multi-pass skeletal mesh rendering.
---
