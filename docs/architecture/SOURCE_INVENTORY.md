# Source Inventory

> **Dated source snapshot:** this survey predates the 2026-11-04 Vehicle
> retirement and later sample cleanup. It is retained for architectural context,
> not current API/feature support. Follow the [optimization roadmap](../ROADMAP.md)
> for execution; unavailable source paths below are historical references.

Reviewed: 2026-11-01. This inventory scans first-party C++ source, shaders and text assets/configuration. Vendored internals, media, binary terrain/mesh payloads, generated build output, caches and documentation prose are excluded from this source table. Existing domain guides were reviewed separately.

Path maintenance (2026-11-03): test sources and sample assets below now point to
`Tests/src` and `Tests/Fixtures`. The following counts remain the original survey
snapshot, not a fresh line-count measurement.

**420 text files; 113,222 lines.** Every listed file was read by tooling; manual architectural analysis focuses on build/APIs/ownership/integration paths.

| Source | Lines | Extracted class/struct names |
|---|---:|---|
| [Tests/Fixtures/LegacyAssets/scenes/CharacterSandbox.axlescene](../../Tests/Fixtures/LegacyAssets/scenes/CharacterSandbox.axlescene) | 423 | — |
| [Tests/Fixtures/LegacyAssets/scenes/OffRoadTrack.axlescene](../../Tests/Fixtures/LegacyAssets/scenes/OffRoadTrack.axlescene) | 448 | — |
| [Tests/Fixtures/LegacyAssets/ui/PauseMenu.axleasset](../../Tests/Fixtures/LegacyAssets/ui/PauseMenu.axleasset) | 89 | — |
| [Tests/Fixtures/LegacyAssets/ui/RacingHUD.axleasset](../../Tests/Fixtures/LegacyAssets/ui/RacingHUD.axleasset) | 215 | — |
| `Tests/Fixtures/Assets/audio/Collision_Impact.wav.meta` (historical path; unavailable in this checkout) | 4 | — |
| `Tests/Fixtures/Assets/audio/Engine_Coast.wav.meta` (historical path; unavailable in this checkout) | 4 | — |
| `Tests/Fixtures/Assets/audio/Engine_Power.wav.meta` (historical path; unavailable in this checkout) | 4 | — |
| `Tests/Fixtures/Assets/audio/Suspension_Thud.wav.meta` (historical path; unavailable in this checkout) | 4 | — |
| `Tests/Fixtures/Assets/audio/Tire_Squeal.wav.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/materials/BlackRubber.axleasset](../../Tests/Fixtures/Assets/materials/BlackRubber.axleasset) | 19 | — |
| `Tests/Fixtures/Assets/materials/BlackRubber.axleasset.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/materials/GoldMetallic.axleasset](../../Tests/Fixtures/Assets/materials/GoldMetallic.axleasset) | 28 | — |
| `Tests/Fixtures/Assets/materials/GoldMetallic.axleasset.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/materials/SilverCarPaint.axleasset](../../Tests/Fixtures/Assets/materials/SilverCarPaint.axleasset) | 19 | — |
| `Tests/Fixtures/Assets/materials/SilverCarPaint.axleasset.meta` (historical path; unavailable in this checkout) | 4 | — |
| `Tests/Fixtures/Assets/models/.cache/SportsCar_Chassis.axlebin.meta` (historical path; unavailable in this checkout) | 4 | — |
| `Tests/Fixtures/Assets/models/.cache/SportsCar_Wheel.axlebin.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/models/sedan-sports.mtl](../../Tests/Fixtures/Assets/models/sedan-sports.mtl) | 6 | — |
| `Tests/Fixtures/Assets/models/sedan-sports.mtl.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/models/SportsCar_Chassis.axleasset](../../Tests/Fixtures/Assets/models/SportsCar_Chassis.axleasset) | 1165 | — |
| `Tests/Fixtures/Assets/models/SportsCar_Chassis.axleasset.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/models/SportsCar_Chassis.axlemesh](../../Tests/Fixtures/Assets/models/SportsCar_Chassis.axlemesh) | 1172 | — |
| `Tests/Fixtures/Assets/models/SportsCar_Chassis.axlemesh.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/models/SportsCar_Wheel.axleasset](../../Tests/Fixtures/Assets/models/SportsCar_Wheel.axleasset) | 880 | — |
| `Tests/Fixtures/Assets/models/SportsCar_Wheel.axleasset.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/models/SportsCar_Wheel.axlemesh](../../Tests/Fixtures/Assets/models/SportsCar_Wheel.axlemesh) | 887 | — |
| `Tests/Fixtures/Assets/models/SportsCar_Wheel.axlemesh.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/models/wheel-racing.mtl](../../Tests/Fixtures/Assets/models/wheel-racing.mtl) | 6 | — |
| `Tests/Fixtures/Assets/models/wheel-racing.mtl.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/prefabs/SportsCar_Assembly.axle](../../Tests/Fixtures/Assets/prefabs/SportsCar_Assembly.axle) | 383 | — |
| `Tests/Fixtures/Assets/prefabs/SportsCar_Assembly.axle.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/scenes/CarTest.axlescene](../../Tests/Fixtures/Assets/scenes/CarTest.axlescene) | 655 | — |
| `Tests/Fixtures/Assets/scenes/CarTest.axlescene.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/scenes/CharacterSandbox.axlescene](../../Tests/Fixtures/Assets/scenes/CharacterSandbox.axlescene) | 423 | — |
| `Tests/Fixtures/Assets/scenes/CharacterSandbox.axlescene.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/scenes/OffRoadTrack.axlescene](../../Tests/Fixtures/Assets/scenes/OffRoadTrack.axlescene) | 448 | — |
| `Tests/Fixtures/Assets/scenes/OffRoadTrack.axlescene.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/scenes/VehicleSandbox.axlescene](../../Tests/Fixtures/Assets/scenes/VehicleSandbox.axlescene) | 807 | — |
| `Tests/Fixtures/Assets/scenes/VehicleSandbox.axlescene.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/scripts/CarController.hpp](../../Tests/Fixtures/Assets/scripts/CarController.hpp) | 196 | CarController |
| `Tests/Fixtures/Assets/scripts/CarController.hpp.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/scripts/RacingHUD.hpp](../../Tests/Fixtures/Assets/scripts/RacingHUD.hpp) | 514 | RacingHUD |
| `Tests/Fixtures/Assets/scripts/RacingHUD.hpp.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/scripts/RotatorScript.hpp](../../Tests/Fixtures/Assets/scripts/RotatorScript.hpp) | 37 | RotatorScript |
| `Tests/Fixtures/Assets/scripts/RotatorScript.hpp.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/scripts/ThirdPersonCamera.hpp](../../Tests/Fixtures/Assets/scripts/ThirdPersonCamera.hpp) | 180 | ThirdPersonCamera |
| `Tests/Fixtures/Assets/scripts/ThirdPersonCamera.hpp.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/scripts/ThirdPersonController.hpp](../../Tests/Fixtures/Assets/scripts/ThirdPersonController.hpp) | 209 | ThirdPersonController |
| `Tests/Fixtures/Assets/scripts/ThirdPersonController.hpp.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/scripts/VehicleChaseCamera.hpp](../../Tests/Fixtures/Assets/scripts/VehicleChaseCamera.hpp) | 655 | VehicleChaseCamera |
| `Tests/Fixtures/Assets/scripts/VehicleChaseCamera.hpp.meta` (historical path; unavailable in this checkout) | 4 | — |
| `Tests/Fixtures/Assets/terrain/OffRoadTerrain.axleterrain.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/ui/PauseMenu.axleasset](../../Tests/Fixtures/Assets/ui/PauseMenu.axleasset) | 89 | — |
| `Tests/Fixtures/Assets/ui/PauseMenu.axleasset.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Tests/Fixtures/Assets/ui/RacingHUD.axleasset](../../Tests/Fixtures/Assets/ui/RacingHUD.axleasset) | 215 | — |
| `Tests/Fixtures/Assets/ui/RacingHUD.axleasset.meta` (historical path; unavailable in this checkout) | 4 | — |
| [Editor/CMakeLists.txt](../../Editor/CMakeLists.txt) | 153 | — |
| [Editor/src/EditorLayer.cpp](../../Editor/src/EditorLayer.cpp) | 4029 | — |
| [Editor/src/EditorLayer.hpp](../../Editor/src/EditorLayer.hpp) | 249 | EditorLayer, InitialBodyState, ModelImportModalData |
| [Editor/src/Main.cpp](../../Editor/src/Main.cpp) | 250 | — |
| [Editor/src/Scripts/ScriptsModule.cpp](../../Editor/src/Scripts/ScriptsModule.cpp) | 58 | — |
| [Tests/src/Test.cpp](../../Tests/src/Test.cpp) | 446 | — |
| [Tests/src/Test_Milestone1.cpp](../../Tests/src/Test_Milestone1.cpp) | 247 | — |
| [Tests/src/Test_Milestone10.cpp](../../Tests/src/Test_Milestone10.cpp) | 907 | MockTransparentItem |
| [Tests/src/Test_Milestone2.cpp](../../Tests/src/Test_Milestone2.cpp) | 433 | — |
| [Tests/src/Test_Milestone3.cpp](../../Tests/src/Test_Milestone3.cpp) | 878 | TestComponentItem, ReflectionDemoScript |
| [Tests/src/Test_Milestone4.cpp](../../Tests/src/Test_Milestone4.cpp) | 1055 | — |
| [Tests/src/Test_Milestone5.cpp](../../Tests/src/Test_Milestone5.cpp) | 1015 | WheelDef |
| [Tests/src/Test_Milestone6.cpp](../../Tests/src/Test_Milestone6.cpp) | 1918 | — |
| [Tests/src/Test_Milestone7.cpp](../../Tests/src/Test_Milestone7.cpp) | 1972 | WheelDef |
| [Tests/src/Test_Milestone8_Part1.cpp](../../Tests/src/Test_Milestone8_Part1.cpp) | 1507 | — |
| [Tests/src/Test_Milestone8_Part2.cpp](../../Tests/src/Test_Milestone8_Part2.cpp) | 3828 | Resolution, VSInput, VSOutput, PSInput, PushConstants, VSOut, VSInput, VSOutput, VSOutput, VSInput, VSOutput, Particle, VSOut, TestData, Vertex3D, CameraUniforms, ObjectUniforms, Vertex2D, RenderMeshTag, VSOut |
| [Tests/src/Test_Milestone8_Part3.cpp](../../Tests/src/Test_Milestone8_Part3.cpp) | 4765 | ResizingStep |
| [Tests/src/Test_Milestone8_Part4.cpp](../../Tests/src/Test_Milestone8_Part4.cpp) | 2865 | — |
| [Tests/src/Test_Milestone9.cpp](../../Tests/src/Test_Milestone9.cpp) | 1301 | — |
| [Tests/src/TestCommon.hpp](../../Tests/src/TestCommon.hpp) | 349 | ScopedGLContext, CarPlayerScript |
| [Engine/Animation/CMakeLists.txt](../../Engine/Animation/CMakeLists.txt) | 25 | — |
| [Engine/Animation/include/Axle/AnimationClip.hpp](../../Engine/Animation/include/Axle/AnimationClip.hpp) | 78 | Keyframe, AnimationChannel, AnimationClip |
| [Engine/Animation/include/Axle/AnimationPose.hpp](../../Engine/Animation/include/Axle/AnimationPose.hpp) | 49 | BoneTransform, AnimationPose |
| [Engine/Animation/include/Axle/AnimationStateMachine.hpp](../../Engine/Animation/include/Axle/AnimationStateMachine.hpp) | 103 | AnimationState, AnimationTransition, AnimationStateMachine |
| [Engine/Animation/include/Axle/Animator.hpp](../../Engine/Animation/include/Axle/Animator.hpp) | 48 | Animator |
| [Engine/Animation/include/Axle/BlendSpace1D.hpp](../../Engine/Animation/include/Axle/BlendSpace1D.hpp) | 40 | BlendSpacePoint1D, BlendSpace1D |
| [Engine/Animation/include/Axle/Bone.hpp](../../Engine/Animation/include/Axle/Bone.hpp) | 19 | Bone |
| [Engine/Animation/include/Axle/SkeletalMesh.hpp](../../Engine/Animation/include/Axle/SkeletalMesh.hpp) | 49 | SkeletalVertex, SkeletalMeshData, SkeletalMesh |
| [Engine/Animation/include/Axle/SkeletalMeshImporter.hpp](../../Engine/Animation/include/Axle/SkeletalMeshImporter.hpp) | 40 | SkeletalMeshImporter |
| [Engine/Animation/include/Axle/Skeleton.hpp](../../Engine/Animation/include/Axle/Skeleton.hpp) | 47 | Skeleton |
| [Engine/Animation/src/AnimationClip.cpp](../../Engine/Animation/src/AnimationClip.cpp) | 187 | — |
| [Engine/Animation/src/AnimationPose.cpp](../../Engine/Animation/src/AnimationPose.cpp) | 122 | — |
| [Engine/Animation/src/AnimationStateMachine.cpp](../../Engine/Animation/src/AnimationStateMachine.cpp) | 173 | — |
| [Engine/Animation/src/Animator.cpp](../../Engine/Animation/src/Animator.cpp) | 77 | — |
| [Engine/Animation/src/BlendSpace1D.cpp](../../Engine/Animation/src/BlendSpace1D.cpp) | 102 | — |
| [Engine/Animation/src/SkeletalMesh.cpp](../../Engine/Animation/src/SkeletalMesh.cpp) | 79 | — |
| [Engine/Animation/src/SkeletalMeshImporter.cpp](../../Engine/Animation/src/SkeletalMeshImporter.cpp) | 904 | Face |
| [Engine/Animation/src/Skeleton.cpp](../../Engine/Animation/src/Skeleton.cpp) | 74 | — |
| [Engine/Asset/CMakeLists.txt](../../Engine/Asset/CMakeLists.txt) | 24 | — |
| [Engine/Asset/include/Axle/AssetCooker.hpp](../../Engine/Asset/include/Axle/AssetCooker.hpp) | 38 | CookSettings, CookResult, AssetCooker |
| [Engine/Asset/include/Axle/AssetDSL.hpp](../../Engine/Asset/include/Axle/AssetDSL.hpp) | 110 | AssetProperty, AssetBlock, AssetDocument, AssetDSL |
| [Engine/Asset/include/Axle/AssetManager.hpp](../../Engine/Asset/include/Axle/AssetManager.hpp) | 88 | AssetManager |
| [Engine/Asset/include/Axle/AssetMetadata.hpp](../../Engine/Asset/include/Axle/AssetMetadata.hpp) | 23 | AssetMetadata |
| [Engine/Asset/include/Axle/AssetMetadataSerializer.hpp](../../Engine/Asset/include/Axle/AssetMetadataSerializer.hpp) | 17 | AssetMetadataSerializer |
| [Engine/Asset/include/Axle/AssetSerializer.hpp](../../Engine/Asset/include/Axle/AssetSerializer.hpp) | 160 | AxleMeshSource, AxleLoadedMesh, AxleSubmeshSource, AxlePackagePart, AxleLoadedPackage, AssetSerializer, AssetCompileResult |
| [Engine/Asset/include/Axle/AssetTypes.hpp](../../Engine/Asset/include/Axle/AssetTypes.hpp) | 92 | — |
| [Engine/Asset/include/Axle/TerrainAsset.hpp](../../Engine/Asset/include/Axle/TerrainAsset.hpp) | 25 | TerrainAsset |
| [Engine/Asset/include/Axle/TextureDescriptor.hpp](../../Engine/Asset/include/Axle/TextureDescriptor.hpp) | 41 | TextureDescriptor |
| [Engine/Asset/src/AssetCooker.cpp](../../Engine/Asset/src/AssetCooker.cpp) | 325 | — |
| [Engine/Asset/src/AssetDSL.cpp](../../Engine/Asset/src/AssetDSL.cpp) | 784 | — |
| [Engine/Asset/src/AssetManager.cpp](../../Engine/Asset/src/AssetManager.cpp) | 497 | — |
| [Engine/Asset/src/AssetMetadataSerializer.cpp](../../Engine/Asset/src/AssetMetadataSerializer.cpp) | 94 | — |
| [Engine/Asset/src/AssetSerializer.cpp](../../Engine/Asset/src/AssetSerializer.cpp) | 1664 | HeaderProps, ObjGroupData, RawPart, RawPart |
| [Engine/Asset/src/TerrainAsset.cpp](../../Engine/Asset/src/TerrainAsset.cpp) | 79 | — |
| [Engine/Asset/src/TextureDescriptor.cpp](../../Engine/Asset/src/TextureDescriptor.cpp) | 152 | — |
| [Engine/Audio/CMakeLists.txt](../../Engine/Audio/CMakeLists.txt) | 18 | — |
| [Engine/Audio/include/Axle/AudioClip.hpp](../../Engine/Audio/include/Axle/AudioClip.hpp) | 42 | AudioClip |
| [Engine/Audio/include/Axle/AudioEngine.hpp](../../Engine/Audio/include/Axle/AudioEngine.hpp) | 51 | AudioEngineSpecification, AudioEngine |
| [Engine/Audio/include/Axle/AudioListener.hpp](../../Engine/Audio/include/Axle/AudioListener.hpp) | 29 | AudioListener |
| [Engine/Audio/include/Axle/AudioSource.hpp](../../Engine/Audio/include/Axle/AudioSource.hpp) | 128 | AudioSource |
| [Engine/Audio/src/AudioClip.cpp](../../Engine/Audio/src/AudioClip.cpp) | 91 | — |
| [Engine/Audio/src/AudioEngine.cpp](../../Engine/Audio/src/AudioEngine.cpp) | 147 | — |
| [Engine/Audio/src/AudioListener.cpp](../../Engine/Audio/src/AudioListener.cpp) | 66 | — |
| [Engine/Audio/src/AudioSource.cpp](../../Engine/Audio/src/AudioSource.cpp) | 307 | — |
| [Engine/Audio/src/miniaudio_impl.cpp](../../Engine/Audio/src/miniaudio_impl.cpp) | 6 | — |
| [Engine/Core/CMakeLists.txt](../../Engine/Core/CMakeLists.txt) | 26 | — |
| [Engine/Core/include/Axle/Application.hpp](../../Engine/Core/include/Axle/Application.hpp) | 57 | ApplicationSpecification, Application |
| [Engine/Core/include/Axle/ApplicationUI.hpp](../../Engine/Core/include/Axle/ApplicationUI.hpp) | 16 | ApplicationUIContext |
| [Engine/Core/include/Axle/FileSystem.hpp](../../Engine/Core/include/Axle/FileSystem.hpp) | 39 | FileSystem |
| [Engine/Core/include/Axle/JobSystem.hpp](../../Engine/Core/include/Axle/JobSystem.hpp) | 147 | JobHandle, JobSystem, LambdaTaskSet, LambdaRangeTaskSet |
| [Engine/Core/include/Axle/Layer.hpp](../../Engine/Core/include/Axle/Layer.hpp) | 27 | Layer |
| [Engine/Core/include/Axle/LayerStack.hpp](../../Engine/Core/include/Axle/LayerStack.hpp) | 32 | LayerStack |
| [Engine/Core/include/Axle/Log.hpp](../../Engine/Core/include/Axle/Log.hpp) | 50 | Log |
| [Engine/Core/include/Axle/Package.hpp](../../Engine/Core/include/Axle/Package.hpp) | 99 | PackageHeader, PackageEntry, Package, PackageWriter, PendingEntry |
| [Engine/Core/include/Axle/Timestep.hpp](../../Engine/Core/include/Axle/Timestep.hpp) | 19 | Timestep |
| [Engine/Core/include/Axle/Types.hpp](../../Engine/Core/include/Axle/Types.hpp) | 65 | — |
| [Engine/Core/include/Axle/UUID.hpp](../../Engine/Core/include/Axle/UUID.hpp) | 29 | UUID |
| [Engine/Core/src/Application.cpp](../../Engine/Core/src/Application.cpp) | 141 | — |
| [Engine/Core/src/FileSystem.cpp](../../Engine/Core/src/FileSystem.cpp) | 239 | — |
| [Engine/Core/src/JobSystem.cpp](../../Engine/Core/src/JobSystem.cpp) | 204 | JobSystemData, AsyncLambdaTask |
| [Engine/Core/src/LayerStack.cpp](../../Engine/Core/src/LayerStack.cpp) | 30 | — |
| [Engine/Core/src/Log.cpp](../../Engine/Core/src/Log.cpp) | 48 | — |
| [Engine/Core/src/Package.cpp](../../Engine/Core/src/Package.cpp) | 415 | — |
| [Engine/Core/src/UUID.cpp](../../Engine/Core/src/UUID.cpp) | 18 | — |
| [Engine/Physics/CMakeLists.txt](../../Engine/Physics/CMakeLists.txt) | 22 | — |
| [Engine/Physics/include/Axle/CharacterController.hpp](../../Engine/Physics/include/Axle/CharacterController.hpp) | 116 | CharacterControllerSettings, CharacterController, CharacterContactListenerImpl |
| [Engine/Physics/include/Axle/JobSystemEnkiTS.hpp](../../Engine/Physics/include/Axle/JobSystemEnkiTS.hpp) | 65 | JobSystemEnkiTS, EnkiJob, EnkiTask |
| [Engine/Physics/include/Axle/PhysicsDebugRenderer.hpp](../../Engine/Physics/include/Axle/PhysicsDebugRenderer.hpp) | 52 | DebugLineVertex, PhysicsDebugRenderer |
| [Engine/Physics/include/Axle/PhysicsEngine.hpp](../../Engine/Physics/include/Axle/PhysicsEngine.hpp) | 17 | PhysicsEngine |
| [Engine/Physics/include/Axle/PhysicsLayers.hpp](../../Engine/Physics/include/Axle/PhysicsLayers.hpp) | 84 | ObjectLayerPairFilterImpl, BPLayerInterfaceImpl, ObjectVsBroadPhaseLayerFilterImpl |
| [Engine/Physics/include/Axle/PhysicsMaterial.hpp](../../Engine/Physics/include/Axle/PhysicsMaterial.hpp) | 90 | PhysicsMaterial |
| [Engine/Physics/include/Axle/PhysicsMath.hpp](../../Engine/Physics/include/Axle/PhysicsMath.hpp) | 48 | — |
| [Engine/Physics/include/Axle/PhysicsScene.hpp](../../Engine/Physics/include/Axle/PhysicsScene.hpp) | 196 | PhysicsSettings, RayCastHit, BoxBodyProps, SphereBodyProps, CylinderBodyProps, ConeBodyProps, PlaneBodyProps, PhysicsDrawSettings, PhysicsScene |
| [Engine/Physics/include/Axle/RHIPhysicsDebugRenderer.hpp](../../Engine/Physics/include/Axle/RHIPhysicsDebugRenderer.hpp) | 146 | RHIDebugLineVertex, RHIPhysicsDebugConstants, JoltInitHelper, RHIPhysicsDebugRenderer |
| [Engine/Physics/src/CharacterController.cpp](../../Engine/Physics/src/CharacterController.cpp) | 221 | — |
| [Engine/Physics/src/JobSystemEnkiTS.cpp](../../Engine/Physics/src/JobSystemEnkiTS.cpp) | 129 | — |
| [Engine/Physics/src/PhysicsDebugRenderer.cpp](../../Engine/Physics/src/PhysicsDebugRenderer.cpp) | 137 | — |
| [Engine/Physics/src/PhysicsEngine.cpp](../../Engine/Physics/src/PhysicsEngine.cpp) | 73 | — |
| [Engine/Physics/src/PhysicsScene.cpp](../../Engine/Physics/src/PhysicsScene.cpp) | 447 | — |
| [Engine/Physics/src/RHIPhysicsDebugRenderer.cpp](../../Engine/Physics/src/RHIPhysicsDebugRenderer.cpp) | 451 | — |
| [Engine/Platform/CMakeLists.txt](../../Engine/Platform/CMakeLists.txt) | 22 | — |
| [Engine/Platform/include/Axle/Events.hpp](../../Engine/Platform/include/Axle/Events.hpp) | 44 | WindowResizeEvent, WindowCloseEvent, KeyPressedEvent, KeyReleasedEvent, MouseMovedEvent, MouseScrolledEvent, MouseButtonPressedEvent, MouseButtonReleasedEvent |
| [Engine/Platform/include/Axle/Input.hpp](../../Engine/Platform/include/Axle/Input.hpp) | 51 | InputContext, Input |
| [Engine/Platform/include/Axle/KeyCodes.hpp](../../Engine/Platform/include/Axle/KeyCodes.hpp) | 76 | — |
| [Engine/Platform/include/Axle/PlatformUtils.hpp](../../Engine/Platform/include/Axle/PlatformUtils.hpp) | 15 | FileDialogs |
| [Engine/Platform/include/Axle/Window.hpp](../../Engine/Platform/include/Axle/Window.hpp) | 108 | WindowProps, Window, WindowData |
| [Engine/Platform/src/Input.cpp](../../Engine/Platform/src/Input.cpp) | 176 | — |
| [Engine/Platform/src/PlatformUtils.cpp](../../Engine/Platform/src/PlatformUtils.cpp) | 160 | — |
| [Engine/Platform/src/Window.cpp](../../Engine/Platform/src/Window.cpp) | 265 | — |
| [Engine/Project/CMakeLists.txt](../../Engine/Project/CMakeLists.txt) | 14 | — |
| [Engine/Project/include/Axle/Project.hpp](../../Engine/Project/include/Axle/Project.hpp) | 46 | ProjectConfig, Project |
| [Engine/Project/include/Axle/ProjectSerializer.hpp](../../Engine/Project/include/Axle/ProjectSerializer.hpp) | 19 | ProjectSerializer |
| [Engine/Project/src/Project.cpp](../../Engine/Project/src/Project.cpp) | 87 | — |
| [Engine/Project/src/ProjectSerializer.cpp](../../Engine/Project/src/ProjectSerializer.cpp) | 89 | — |
| [Engine/Renderer/CMakeLists.txt](../../Engine/Renderer/CMakeLists.txt) | 31 | — |
| [Engine/Renderer/include/Axle/Buffer.hpp](../../Engine/Renderer/include/Axle/Buffer.hpp) | 121 | BufferElement, BufferLayout, VertexBuffer, IndexBuffer |
| [Engine/Renderer/include/Axle/Cubemap.hpp](../../Engine/Renderer/include/Axle/Cubemap.hpp) | 71 | CubemapSpecification, IBLMaps, Cubemap |
| [Engine/Renderer/include/Axle/Framebuffer.hpp](../../Engine/Renderer/include/Axle/Framebuffer.hpp) | 33 | FramebufferSpecification, Framebuffer |
| [Engine/Renderer/include/Axle/Material.hpp](../../Engine/Renderer/include/Axle/Material.hpp) | 236 | Material |
| [Engine/Renderer/include/Axle/PerspectiveCamera.hpp](../../Engine/Renderer/include/Axle/PerspectiveCamera.hpp) | 67 | PerspectiveCamera |
| [Engine/Renderer/include/Axle/PerspectiveCameraController.hpp](../../Engine/Renderer/include/Axle/PerspectiveCameraController.hpp) | 57 | PerspectiveCameraController |
| [Engine/Renderer/include/Axle/PrimitiveMesh.hpp](../../Engine/Renderer/include/Axle/PrimitiveMesh.hpp) | 40 | MeshInfo, PrimitiveMesh |
| [Engine/Renderer/include/Axle/RenderCommand.hpp](../../Engine/Renderer/include/Axle/RenderCommand.hpp) | 42 | RenderCommand |
| [Engine/Renderer/include/Axle/Shader.hpp](../../Engine/Renderer/include/Axle/Shader.hpp) | 47 | Shader |
| [Engine/Renderer/include/Axle/ShadowMap.hpp](../../Engine/Renderer/include/Axle/ShadowMap.hpp) | 76 | ShadowMapSpecification, ShadowMap |
| [Engine/Renderer/include/Axle/SkinningPipeline.hpp](../../Engine/Renderer/include/Axle/SkinningPipeline.hpp) | 46 | SkinningPipeline |
| [Engine/Renderer/include/Axle/Skybox.hpp](../../Engine/Renderer/include/Axle/Skybox.hpp) | 77 | SkyboxSpecification, Skybox |
| [Engine/Renderer/include/Axle/Texture2D.hpp](../../Engine/Renderer/include/Axle/Texture2D.hpp) | 86 | TextureSpecification, Texture2D |
| [Engine/Renderer/include/Axle/VertexArray.hpp](../../Engine/Renderer/include/Axle/VertexArray.hpp) | 30 | VertexArray |
| [Engine/Renderer/src/Buffer.cpp](../../Engine/Renderer/src/Buffer.cpp) | 78 | — |
| [Engine/Renderer/src/Cubemap.cpp](../../Engine/Renderer/src/Cubemap.cpp) | 678 | — |
| [Engine/Renderer/src/Framebuffer.cpp](../../Engine/Renderer/src/Framebuffer.cpp) | 108 | OpenGLFramebuffer |
| [Engine/Renderer/src/Material.cpp](../../Engine/Renderer/src/Material.cpp) | 734 | — |
| [Engine/Renderer/src/PerspectiveCamera.cpp](../../Engine/Renderer/src/PerspectiveCamera.cpp) | 101 | — |
| [Engine/Renderer/src/PerspectiveCameraController.cpp](../../Engine/Renderer/src/PerspectiveCameraController.cpp) | 157 | — |
| [Engine/Renderer/src/PrimitiveMesh.cpp](../../Engine/Renderer/src/PrimitiveMesh.cpp) | 524 | — |
| [Engine/Renderer/src/RenderCommand.cpp](../../Engine/Renderer/src/RenderCommand.cpp) | 124 | — |
| [Engine/Renderer/src/Shader.cpp](../../Engine/Renderer/src/Shader.cpp) | 170 | — |
| [Engine/Renderer/src/ShadowMap.cpp](../../Engine/Renderer/src/ShadowMap.cpp) | 283 | — |
| [Engine/Renderer/src/SkinningPipeline.cpp](../../Engine/Renderer/src/SkinningPipeline.cpp) | 206 | — |
| [Engine/Renderer/src/Skybox.cpp](../../Engine/Renderer/src/Skybox.cpp) | 223 | — |
| [Engine/Renderer/src/stb_image.cpp](../../Engine/Renderer/src/stb_image.cpp) | 3 | — |
| [Engine/Renderer/src/Texture2D.cpp](../../Engine/Renderer/src/Texture2D.cpp) | 277 | — |
| [Engine/Renderer/src/VertexArray.cpp](../../Engine/Renderer/src/VertexArray.cpp) | 137 | — |
| [Engine/Resources/Shaders/AtmosphericSkybox.hlsl](../../Engine/Resources/Shaders/AtmosphericSkybox.hlsl) | 181 | — |
| [Engine/Resources/Shaders/Canvas2D.hlsl](../../Engine/Resources/Shaders/Canvas2D.hlsl) | 148 | — |
| [Engine/Resources/Shaders/Checkerboard.frag](../../Engine/Resources/Shaders/Checkerboard.frag) | 536 | — |
| [Engine/Resources/Shaders/Checkerboard.vert](../../Engine/Resources/Shaders/Checkerboard.vert) | 22 | — |
| [Engine/Resources/Shaders/ForwardPBR.hlsl](../../Engine/Resources/Shaders/ForwardPBR.hlsl) | 505 | — |
| [Engine/Resources/Shaders/ForwardScene.hlsl](../../Engine/Resources/Shaders/ForwardScene.hlsl) | 127 | — |
| [Engine/Resources/Shaders/FXAA.hlsl](../../Engine/Resources/Shaders/FXAA.hlsl) | 158 | — |
| [Engine/Resources/Shaders/ObjectID.hlsl](../../Engine/Resources/Shaders/ObjectID.hlsl) | 61 | — |
| [Engine/Resources/Shaders/Particle.frag](../../Engine/Resources/Shaders/Particle.frag) | 153 | — |
| [Engine/Resources/Shaders/Particle.hlsl](../../Engine/Resources/Shaders/Particle.hlsl) | 246 | — |
| [Engine/Resources/Shaders/Particle.vert](../../Engine/Resources/Shaders/Particle.vert) | 99 | — |
| [Engine/Resources/Shaders/PhysicsWireframe.hlsl](../../Engine/Resources/Shaders/PhysicsWireframe.hlsl) | 47 | — |
| [Engine/Resources/Shaders/ShadowDepth.frag](../../Engine/Resources/Shaders/ShadowDepth.frag) | 5 | — |
| [Engine/Resources/Shaders/ShadowDepth.vert](../../Engine/Resources/Shaders/ShadowDepth.vert) | 10 | — |
| [Engine/Resources/Shaders/SkeletalMesh.vert](../../Engine/Resources/Shaders/SkeletalMesh.vert) | 53 | — |
| [Engine/Resources/Shaders/SkeletalShadowDepth.vert](../../Engine/Resources/Shaders/SkeletalShadowDepth.vert) | 37 | — |
| [Engine/Resources/Shaders/Skybox.frag](../../Engine/Resources/Shaders/Skybox.frag) | 140 | — |
| [Engine/Resources/Shaders/Skybox.vert](../../Engine/Resources/Shaders/Skybox.vert) | 16 | — |
| [Engine/Resources/Shaders/ToneMapping.hlsl](../../Engine/Resources/Shaders/ToneMapping.hlsl) | 150 | — |
| [Engine/RHI/CMakeLists.txt](../../Engine/RHI/CMakeLists.txt) | 35 | — |
| [Engine/RHI/include/Axle/RHI/RHIBarrier.hpp](../../Engine/RHI/include/Axle/RHI/RHIBarrier.hpp) | 329 | RHITextureBarrier, RHIBufferBarrier, RHIGlobalBarrier, RHIBarrierGroup, RHIBufferState, RHIResourceStateTracker |
| [Engine/RHI/include/Axle/RHI/RHIBindless.hpp](../../Engine/RHI/include/Axle/RHI/RHIBindless.hpp) | 77 | RHIBindlessRegistry |
| [Engine/RHI/include/Axle/RHI/RHIBuffer.hpp](../../Engine/RHI/include/Axle/RHI/RHIBuffer.hpp) | 200 | RHIBufferDesc, RHIBuffer, RHIVideoMemoryInfo, RHIMemoryManager |
| [Engine/RHI/include/Axle/RHI/RHICommandBuffer.hpp](../../Engine/RHI/include/Axle/RHI/RHICommandBuffer.hpp) | 138 | RHICommandBuffer |
| [Engine/RHI/include/Axle/RHI/RHICommon.hpp](../../Engine/RHI/include/Axle/RHI/RHICommon.hpp) | 89 | RHIAdapterInfo, RHIDeviceCapabilities, RHIDeviceDesc |
| [Engine/RHI/include/Axle/RHI/RHIDescriptorPool.hpp](../../Engine/RHI/include/Axle/RHI/RHIDescriptorPool.hpp) | 84 | RHIDescriptorPoolDesc, RHIDescriptorPool |
| [Engine/RHI/include/Axle/RHI/RHIDevice.hpp](../../Engine/RHI/include/Axle/RHI/RHIDevice.hpp) | 81 | NRIInterfaces, RHIDevice |
| [Engine/RHI/include/Axle/RHI/RHIECSStreamer.hpp](../../Engine/RHI/include/Axle/RHI/RHIECSStreamer.hpp) | 150 | GPUInstanceData, GPUIndirectDrawCommand, GPUIndirectDrawIndexedCommand, RHIECSStreamResult, RHIMeshBatchKey, RHIMultiMeshBatch, RHIECSStreamer |
| [Engine/RHI/include/Axle/RHI/RHILight.hpp](../../Engine/RHI/include/Axle/RHI/RHILight.hpp) | 230 | RHIDirectionalLight, RHIPointLight, RHISpotLight, RHIGPUDirectionalLight, RHIGPUPointLight, RHIGPUSpotLight, RHIMultiLightBufferData, RHIMultiLightBuffer |
| [Engine/RHI/include/Axle/RHI/RHIMaterial.hpp](../../Engine/RHI/include/Axle/RHI/RHIMaterial.hpp) | 196 | RHIMaterialDesc, RHIMaterial |
| [Engine/RHI/include/Axle/RHI/RHIMesh.hpp](../../Engine/RHI/include/Axle/RHI/RHIMesh.hpp) | 127 | RHISubmesh, RHIMesh |
| [Engine/RHI/include/Axle/RHI/RHIPipeline.hpp](../../Engine/RHI/include/Axle/RHI/RHIPipeline.hpp) | 241 | RHIPipelineLayout, RHIRasterizerDesc, RHIDepthStencilDesc, RHIBlendAttachmentDesc, RHIInputAssemblyDesc, RHIVertexLayoutAttribute, RHIVertexStream, RHIVertexInputDesc, RHIGraphicsPipelineDesc, RHIComputePipelineDesc, RHIPipelineCache, PipelineEntry |
| [Engine/RHI/include/Axle/RHI/RHISampler.hpp](../../Engine/RHI/include/Axle/RHI/RHISampler.hpp) | 193 | RHISamplerDesc, RHISampler |
| [Engine/RHI/include/Axle/RHI/RHIShader.hpp](../../Engine/RHI/include/Axle/RHI/RHIShader.hpp) | 277 | RHIShaderMacro, RHIShaderCompileDesc, RHIShaderBytecode, RHIShaderVariable, RHIShaderConstantBuffer, RHIShaderResourceBinding, RHIVertexAttribute, RHIAutoPipelineLayout, DescriptorSetStorage, RHIShaderReflection, RHIShaderCompileResult, RHIShaderCompiler |
| [Engine/RHI/include/Axle/RHI/RHIStreamer.hpp](../../Engine/RHI/include/Axle/RHI/RHIStreamer.hpp) | 140 | RHIStreamerDesc, RHIStreamedBuffer, RHIStreamChunk, RHIStreamTextureDesc, RHIStreamer |
| [Engine/RHI/include/Axle/RHI/RHISurface.hpp](../../Engine/RHI/include/Axle/RHI/RHISurface.hpp) | 52 | RHISurface |
| [Engine/RHI/include/Axle/RHI/RHISwapChain.hpp](../../Engine/RHI/include/Axle/RHI/RHISwapChain.hpp) | 107 | RHISwapChainDesc, SwapChainTextureEntry, FrameInFlight, RHISwapChain |
| [Engine/RHI/include/Axle/RHI/RHISwitcher.hpp](../../Engine/RHI/include/Axle/RHI/RHISwitcher.hpp) | 40 | RHIBackendConfig, RHISwitcher |
| [Engine/RHI/include/Axle/RHI/RHITexture.hpp](../../Engine/RHI/include/Axle/RHI/RHITexture.hpp) | 172 | RHITextureDesc, RHITexture |
| [Engine/RHI/include/Axle/RHI/RHITransform.hpp](../../Engine/RHI/include/Axle/RHI/RHITransform.hpp) | 59 | RHIObjectPushConstants, RHISceneConstants |
| [Engine/RHI/include/Axle/RHI/RHIVertexLayout.hpp](../../Engine/RHI/include/Axle/RHI/RHIVertexLayout.hpp) | 193 | RHIVertex3D, RHISkinnedVertex3D |
| [Engine/RHI/src/RHIBarrier.cpp](../../Engine/RHI/src/RHIBarrier.cpp) | 251 | — |
| [Engine/RHI/src/RHIBindless.cpp](../../Engine/RHI/src/RHIBindless.cpp) | 197 | — |
| [Engine/RHI/src/RHIBuffer.cpp](../../Engine/RHI/src/RHIBuffer.cpp) | 431 | — |
| [Engine/RHI/src/RHICommandBuffer.cpp](../../Engine/RHI/src/RHICommandBuffer.cpp) | 562 | — |
| [Engine/RHI/src/RHIDescriptorPool.cpp](../../Engine/RHI/src/RHIDescriptorPool.cpp) | 195 | — |
| [Engine/RHI/src/RHIDevice.cpp](../../Engine/RHI/src/RHIDevice.cpp) | 422 | — |
| [Engine/RHI/src/RHIECSStreamer.cpp](../../Engine/RHI/src/RHIECSStreamer.cpp) | 116 | — |
| [Engine/RHI/src/RHILight.cpp](../../Engine/RHI/src/RHILight.cpp) | 131 | — |
| [Engine/RHI/src/RHIMaterial.cpp](../../Engine/RHI/src/RHIMaterial.cpp) | 631 | — |
| [Engine/RHI/src/RHIMesh.cpp](../../Engine/RHI/src/RHIMesh.cpp) | 835 | FaceDef, SmoothingGroup |
| [Engine/RHI/src/RHIPipeline.cpp](../../Engine/RHI/src/RHIPipeline.cpp) | 494 | — |
| [Engine/RHI/src/RHISampler.cpp](../../Engine/RHI/src/RHISampler.cpp) | 98 | — |
| [Engine/RHI/src/RHIShader.cpp](../../Engine/RHI/src/RHIShader.cpp) | 1229 | — |
| [Engine/RHI/src/RHIStreamer.cpp](../../Engine/RHI/src/RHIStreamer.cpp) | 240 | — |
| [Engine/RHI/src/RHISurface.cpp](../../Engine/RHI/src/RHISurface.cpp) | 89 | — |
| [Engine/RHI/src/RHISwapChain.cpp](../../Engine/RHI/src/RHISwapChain.cpp) | 506 | — |
| [Engine/RHI/src/RHISwitcher.cpp](../../Engine/RHI/src/RHISwitcher.cpp) | 142 | — |
| [Engine/RHI/src/RHITexture.cpp](../../Engine/RHI/src/RHITexture.cpp) | 179 | — |
| [Engine/Scene/CMakeLists.txt](../../Engine/Scene/CMakeLists.txt) | 35 | — |
| [Engine/Scene/include/Axle/AssemblySerializer.hpp](../../Engine/Scene/include/Axle/AssemblySerializer.hpp) | 69 | AssemblyNode, AssemblyData, AssemblySerializer |
| [Engine/Scene/include/Axle/CharacterVFX.hpp](../../Engine/Scene/include/Axle/CharacterVFX.hpp) | 96 | CharacterVFXConfig, CharacterVFX |
| [Engine/Scene/include/Axle/Components.hpp](../../Engine/Scene/include/Axle/Components.hpp) | 1602 | IDComponent, TagComponent, TransformComponent, MeshInstance3D, MaterialComponent, CollisionShape3D, RigidBody3D, HierarchyComponent, NativeScriptComponent, VehicleComponent, WheelComponent, DirectionalLightComponent, PointLightComponent, SpotLightComponent, CameraComponent, SkyboxComponent, AssemblyComponent, AudioListenerComponent, AudioSourceComponent, VehicleAudioComponent, SkeletalMeshComponent, AnimatorComponent, CharacterControllerComponent, ParticleEmitterStack, ParticleEmitterComponent, VehicleVFXComponent, CharacterVFXComponent, UICanvasComponent |
| [Engine/Scene/include/Axle/Entity.hpp](../../Engine/Scene/include/Axle/Entity.hpp) | 87 | Entity |
| [Engine/Scene/include/Axle/ParallelECS.hpp](../../Engine/Scene/include/Axle/ParallelECS.hpp) | 282 | ParallelECS, CommandBuffer, always_false |
| [Engine/Scene/include/Axle/RHIMeshRegistry.hpp](../../Engine/Scene/include/Axle/RHIMeshRegistry.hpp) | 70 | RHIMeshRegistry |
| [Engine/Scene/include/Axle/RHIObjectPicker.hpp](../../Engine/Scene/include/Axle/RHIObjectPicker.hpp) | 102 | RHIObjectIDPushConstants, RHIObjectPicker |
| [Engine/Scene/include/Axle/RHISceneRenderer.hpp](../../Engine/Scene/include/Axle/RHISceneRenderer.hpp) | 305 | RHISkyboxConstants, RHIToneMappingConstants, RHIFXAAConstants, RHISceneRenderer |
| [Engine/Scene/include/Axle/Scene.hpp](../../Engine/Scene/include/Axle/Scene.hpp) | 139 | RuntimeVehicleData, Scene |
| [Engine/Scene/include/Axle/SceneRenderer.hpp](../../Engine/Scene/include/Axle/SceneRenderer.hpp) | 101 | SceneRenderSettings, SceneRenderer |
| [Engine/Scene/include/Axle/SceneSerializer.hpp](../../Engine/Scene/include/Axle/SceneSerializer.hpp) | 46 | SceneSerializerContext, SceneSerializer |
| [Engine/Scene/include/Axle/ScriptableEntity.hpp](../../Engine/Scene/include/Axle/ScriptableEntity.hpp) | 50 | ScriptableEntity |
| [Engine/Scene/include/Axle/ScriptCompiler.hpp](../../Engine/Scene/include/Axle/ScriptCompiler.hpp) | 115 | CompilerDiagnostic, CompilationResult, ScriptCompiler |
| [Engine/Scene/include/Axle/ScriptField.hpp](../../Engine/Scene/include/Axle/ScriptField.hpp) | 156 | ScriptField, ScriptFieldRegistry |
| [Engine/Scene/include/Axle/ScriptHotReloadManager.hpp](../../Engine/Scene/include/Axle/ScriptHotReloadManager.hpp) | 84 | HotReloadResult, ScriptHotReloadManager |
| [Engine/Scene/include/Axle/ScriptModule.hpp](../../Engine/Scene/include/Axle/ScriptModule.hpp) | 63 | ScriptMetadata, ScriptModuleRegistry |
| [Engine/Scene/include/Axle/ScriptRegistry.hpp](../../Engine/Scene/include/Axle/ScriptRegistry.hpp) | 68 | ScriptRegistry |
| [Engine/Scene/src/AssemblySerializer.cpp](../../Engine/Scene/src/AssemblySerializer.cpp) | 1357 | RawAssemblyEntity |
| [Engine/Scene/src/CharacterVFX.cpp](../../Engine/Scene/src/CharacterVFX.cpp) | 171 | — |
| [Engine/Scene/src/Entity.cpp](../../Engine/Scene/src/Entity.cpp) | 220 | — |
| [Engine/Scene/src/RHIMeshRegistry.cpp](../../Engine/Scene/src/RHIMeshRegistry.cpp) | 213 | — |
| [Engine/Scene/src/RHIObjectPicker.cpp](../../Engine/Scene/src/RHIObjectPicker.cpp) | 369 | — |
| [Engine/Scene/src/RHISceneRenderer.cpp](../../Engine/Scene/src/RHISceneRenderer.cpp) | 1794 | VSInput, PSInput, DrawItem |
| [Engine/Scene/src/Scene.cpp](../../Engine/Scene/src/Scene.cpp) | 1767 | — |
| [Engine/Scene/src/SceneRenderer.cpp](../../Engine/Scene/src/SceneRenderer.cpp) | 747 | PointLight, SpotLight |
| [Engine/Scene/src/SceneSerializer.cpp](../../Engine/Scene/src/SceneSerializer.cpp) | 1853 | PendingHierarchy |
| [Engine/Scene/src/ScriptCompiler.cpp](../../Engine/Scene/src/ScriptCompiler.cpp) | 324 | — |
| [Engine/Scene/src/ScriptHotReloadManager.cpp](../../Engine/Scene/src/ScriptHotReloadManager.cpp) | 310 | — |
| [Engine/Scene/src/ScriptRegistry.cpp](../../Engine/Scene/src/ScriptRegistry.cpp) | 214 | — |
| [Engine/UI/CMakeLists.txt](../../Engine/UI/CMakeLists.txt) | 124 | — |
| [Engine/UI/include/Axle/AssemblyEditorPanel.hpp](../../Engine/UI/include/Axle/AssemblyEditorPanel.hpp) | 79 | AssemblyEditorPanel |
| [Engine/UI/include/Axle/AssetPreviewPanel.hpp](../../Engine/UI/include/Axle/AssetPreviewPanel.hpp) | 97 | AssetPreviewPanel |
| [Engine/UI/include/Axle/AssetThumbnailManager.hpp](../../Engine/UI/include/Axle/AssetThumbnailManager.hpp) | 83 | AssetCachedMeta, AssetThumbnailManager |
| [Engine/UI/include/Axle/ConsolePanel.hpp](../../Engine/UI/include/Axle/ConsolePanel.hpp) | 57 | ConsoleMessage, EditorConsoleSink, ConsolePanel |
| [Engine/UI/include/Axle/ContentBrowserPanel.hpp](../../Engine/UI/include/Axle/ContentBrowserPanel.hpp) | 88 | ContentBrowserPanel |
| [Engine/UI/include/Axle/CurveEditorWidget.hpp](../../Engine/UI/include/Axle/CurveEditorWidget.hpp) | 19 | CurveEditorWidget |
| [Engine/UI/include/Axle/DebugPanel.hpp](../../Engine/UI/include/Axle/DebugPanel.hpp) | 50 | TelemetryData, DebugPanel |
| [Engine/UI/include/Axle/HLSLStudioPanel.hpp](../../Engine/UI/include/Axle/HLSLStudioPanel.hpp) | 86 | HLSLStudioPanel |
| [Engine/UI/include/Axle/ImGuiLayer.hpp](../../Engine/UI/include/Axle/ImGuiLayer.hpp) | 125 | ImGuiLayer |
| [Engine/UI/include/Axle/RHIViewportDescriptor.hpp](../../Engine/UI/include/Axle/RHIViewportDescriptor.hpp) | 91 | RHIViewportDesc, RHIViewportDescriptor |
| [Engine/UI/include/Axle/SceneHierarchyPanel.hpp](../../Engine/UI/include/Axle/SceneHierarchyPanel.hpp) | 134 | SceneHierarchyPanel |
| [Engine/UI/include/Axle/UI/Canvas2D.hpp](../../Engine/UI/include/Axle/UI/Canvas2D.hpp) | 68 | Canvas2DStats, Canvas2D |
| [Engine/UI/include/Axle/UI/RectTransform.hpp](../../Engine/UI/include/Axle/UI/RectTransform.hpp) | 49 | RectTransform |
| [Engine/UI/include/Axle/UI/RectTransformGizmo.hpp](../../Engine/UI/include/Axle/UI/RectTransformGizmo.hpp) | 96 | RectTransformGizmoSettings, RectTransformGizmo |
| [Engine/UI/include/Axle/UI/UIDataContext.hpp](../../Engine/UI/include/Axle/UI/UIDataContext.hpp) | 93 | UIDataContext |
| [Engine/UI/include/Axle/UI/UIDocument.hpp](../../Engine/UI/include/Axle/UI/UIDocument.hpp) | 110 | UIPropertyBinding, UIDocument |
| [Engine/UI/include/Axle/UI/UIFont.hpp](../../Engine/UI/include/Axle/UI/UIFont.hpp) | 46 | GlyphMetrics, UIFont |
| [Engine/UI/include/Axle/UI/UINode.hpp](../../Engine/UI/include/Axle/UI/UINode.hpp) | 172 | UINode, UICanvasNode, UIRectangleNode, UITextNode, UIImageNode, UIProgressBarNode, UIRadialGaugeNode, UIBoxContainerNode, UIButtonNode |
| [Engine/UI/include/Axle/UI/UIParser.hpp](../../Engine/UI/include/Axle/UI/UIParser.hpp) | 24 | UIParser |
| [Engine/UI/include/Axle/UI/UITypes.hpp](../../Engine/UI/include/Axle/UI/UITypes.hpp) | 79 | UIRect, UIStyle, UIVertex |
| [Engine/UI/include/Axle/UICanvasDesignerPanel.hpp](../../Engine/UI/include/Axle/UICanvasDesignerPanel.hpp) | 59 | UICanvasDesignerPanel |
| [Engine/UI/include/Axle/UICanvasImGuiRenderer.hpp](../../Engine/UI/include/Axle/UICanvasImGuiRenderer.hpp) | 47 | UICanvasMapping, UICanvasImGuiRenderer |
| [Engine/UI/include/Axle/UIPalettePanel.hpp](../../Engine/UI/include/Axle/UIPalettePanel.hpp) | 70 | UIWidgetDescriptor, UIPalettePanel |
| [Engine/UI/include/Axle/UIVectorIcons.hpp](../../Engine/UI/include/Axle/UIVectorIcons.hpp) | 413 | — |
| [Engine/UI/include/Axle/VFXEditorPanel.hpp](../../Engine/UI/include/Axle/VFXEditorPanel.hpp) | 120 | VFXEditorPanel |
| [Engine/UI/src/AssemblyEditorPanel.cpp](../../Engine/UI/src/AssemblyEditorPanel.cpp) | 635 | — |
| [Engine/UI/src/AssetInspector.cpp](../../Engine/UI/src/AssetInspector.cpp) | 957 | — |
| [Engine/UI/src/AssetPreviewPanel.cpp](../../Engine/UI/src/AssetPreviewPanel.cpp) | 744 | — |
| [Engine/UI/src/AssetThumbnailManager.cpp](../../Engine/UI/src/AssetThumbnailManager.cpp) | 567 | GLStateScope |
| [Engine/UI/src/ConsolePanel.cpp](../../Engine/UI/src/ConsolePanel.cpp) | 334 | DisplayEntry |
| [Engine/UI/src/ContentBrowserPanel.cpp](../../Engine/UI/src/ContentBrowserPanel.cpp) | 1647 | AssetVisualMeta |
| [Engine/UI/src/CurveEditorWidget.cpp](../../Engine/UI/src/CurveEditorWidget.cpp) | 314 | — |
| [Engine/UI/src/DebugPanel.cpp](../../Engine/UI/src/DebugPanel.cpp) | 68 | — |
| [Engine/UI/src/EntityInspector.cpp](../../Engine/UI/src/EntityInspector.cpp) | 2800 | AvailableItem |
| [Engine/UI/src/HLSLStudioPanel.cpp](../../Engine/UI/src/HLSLStudioPanel.cpp) | 468 | TextEditCallbackData |
| [Engine/UI/src/ImGuiLayer.cpp](../../Engine/UI/src/ImGuiLayer.cpp) | 1203 | VulkanDispatchTable, ImGuiLoaderContext |
| [Engine/UI/src/RectTransformGizmo.cpp](../../Engine/UI/src/RectTransformGizmo.cpp) | 465 | CanvasViewportMapping |
| [Engine/UI/src/RHIViewportDescriptor.cpp](../../Engine/UI/src/RHIViewportDescriptor.cpp) | 292 | — |
| [Engine/UI/src/SceneHierarchyPanel.cpp](../../Engine/UI/src/SceneHierarchyPanel.cpp) | 1221 | — |
| [Engine/UI/src/UI2D/Canvas2D.cpp](../../Engine/UI/src/UI2D/Canvas2D.cpp) | 251 | — |
| [Engine/UI/src/UI2D/RectTransform.cpp](../../Engine/UI/src/UI2D/RectTransform.cpp) | 150 | — |
| [Engine/UI/src/UI2D/UIDocument.cpp](../../Engine/UI/src/UI2D/UIDocument.cpp) | 296 | — |
| [Engine/UI/src/UI2D/UIFont.cpp](../../Engine/UI/src/UI2D/UIFont.cpp) | 186 | — |
| [Engine/UI/src/UI2D/UINode.cpp](../../Engine/UI/src/UI2D/UINode.cpp) | 523 | — |
| [Engine/UI/src/UI2D/UIParser.cpp](../../Engine/UI/src/UI2D/UIParser.cpp) | 568 | — |
| [Engine/UI/src/UI2D/UITypes.cpp](../../Engine/UI/src/UI2D/UITypes.cpp) | 58 | — |
| [Engine/UI/src/UICanvasDesignerPanel.cpp](../../Engine/UI/src/UICanvasDesignerPanel.cpp) | 299 | — |
| [Engine/UI/src/UICanvasImGuiRenderer.cpp](../../Engine/UI/src/UICanvasImGuiRenderer.cpp) | 221 | — |
| [Engine/UI/src/UIPalettePanel.cpp](../../Engine/UI/src/UIPalettePanel.cpp) | 428 | — |
| [Engine/UI/src/VFXEditorPanel.cpp](../../Engine/UI/src/VFXEditorPanel.cpp) | 932 | — |
| `Engine/Vehicle/CMakeLists.txt` (historical path; unavailable in this checkout) | 22 | — |
| `Engine/Vehicle/include/Axle/Vehicle.hpp` (historical path; unavailable in this checkout) | 56 | Vehicle |
| `Engine/Vehicle/include/Axle/VehicleAudio.hpp` (historical path; unavailable in this checkout) | 99 | VehicleAudioConfig, VehicleAudio |
| `Engine/Vehicle/include/Axle/VehicleConfig.hpp` (historical path; unavailable in this checkout) | 68 | WheelConfig, VehicleConfig |
| `Engine/Vehicle/include/Axle/VehicleTelemetry.hpp` (historical path; unavailable in this checkout) | 28 | WheelTelemetryData, VehicleTelemetryData |
| `Engine/Vehicle/include/Axle/VehicleVFX.hpp` (historical path; unavailable in this checkout) | 133 | VehicleVFXConfig, VehicleVFX |
| `Engine/Vehicle/src/Vehicle.cpp` (historical path; unavailable in this checkout) | 305 | — |
| `Engine/Vehicle/src/VehicleAudio.cpp` (historical path; unavailable in this checkout) | 254 | — |
| `Engine/Vehicle/src/VehicleVFX.cpp` (historical path; unavailable in this checkout) | 291 | — |
| [Engine/VFX/CMakeLists.txt](../../Engine/VFX/CMakeLists.txt) | 27 | — |
| [Engine/VFX/include/Axle/CurlNoise.hpp](../../Engine/VFX/include/Axle/CurlNoise.hpp) | 50 | CurlNoiseConfig, CurlNoise |
| [Engine/VFX/include/Axle/ParticleCollision.hpp](../../Engine/VFX/include/Axle/ParticleCollision.hpp) | 93 | ParticleCollisionPlane, ParticleCollisionBox, ParticleCollisionConfig, ParticleCollisionSolver |
| [Engine/VFX/include/Axle/ParticleEmitter.hpp](../../Engine/VFX/include/Axle/ParticleEmitter.hpp) | 119 | ParticleEmitterShape, ParticleEmitterConfig |
| [Engine/VFX/include/Axle/ParticleEmitterSerializer.hpp](../../Engine/VFX/include/Axle/ParticleEmitterSerializer.hpp) | 22 | ParticleEmitterSerializer |
| [Engine/VFX/include/Axle/ParticleForceField.hpp](../../Engine/VFX/include/Axle/ParticleForceField.hpp) | 77 | ParticleForceField, ParticleForceFieldManager |
| [Engine/VFX/include/Axle/ParticlePool.hpp](../../Engine/VFX/include/Axle/ParticlePool.hpp) | 118 | ParticleSpawnData, ParticlePool |
| [Engine/VFX/include/Axle/ParticleRenderer.hpp](../../Engine/VFX/include/Axle/ParticleRenderer.hpp) | 133 | ParticleInstanceVertex, ParticlePointLight, ParticleLightEnvironment, ParticleRenderer |
| [Engine/VFX/include/Axle/ParticleSystem.hpp](../../Engine/VFX/include/Axle/ParticleSystem.hpp) | 67 | ParticleSystem |
| [Engine/VFX/include/Axle/RHIParticleRenderer.hpp](../../Engine/VFX/include/Axle/RHIParticleRenderer.hpp) | 165 | RHIParticleConstants, RHIParticleRenderer |
| [Engine/VFX/include/Axle/VFXCommon.hpp](../../Engine/VFX/include/Axle/VFXCommon.hpp) | 348 | FloatRange, ParticleBurst, CurveKeyframe, FloatCurve, GradientColorKey, GradientAlphaKey, ColorGradient, SubEmitterConfig, VFXRandom |
| [Engine/VFX/src/CurlNoise.cpp](../../Engine/VFX/src/CurlNoise.cpp) | 131 | — |
| [Engine/VFX/src/ParticleCollision.cpp](../../Engine/VFX/src/ParticleCollision.cpp) | 213 | — |
| [Engine/VFX/src/ParticleEmitter.cpp](../../Engine/VFX/src/ParticleEmitter.cpp) | 319 | — |
| [Engine/VFX/src/ParticleEmitterSerializer.cpp](../../Engine/VFX/src/ParticleEmitterSerializer.cpp) | 468 | — |
| [Engine/VFX/src/ParticleForceField.cpp](../../Engine/VFX/src/ParticleForceField.cpp) | 129 | — |
| [Engine/VFX/src/ParticlePool.cpp](../../Engine/VFX/src/ParticlePool.cpp) | 89 | — |
| [Engine/VFX/src/ParticleRenderer.cpp](../../Engine/VFX/src/ParticleRenderer.cpp) | 515 | — |
| [Engine/VFX/src/ParticleSystem.cpp](../../Engine/VFX/src/ParticleSystem.cpp) | 322 | — |
| [Engine/VFX/src/RHIParticleRenderer.cpp](../../Engine/VFX/src/RHIParticleRenderer.cpp) | 448 | — |
| [Plugins/Spline/CMakeLists.txt](../../Plugins/Spline/CMakeLists.txt) | 45 | — |
| [Plugins/Spline/include/AxleSpline/Curve3D.hpp](../../Plugins/Spline/include/AxleSpline/Curve3D.hpp) | 116 | SplineNode, SplineFrame, ArcLengthSample, Curve3D |
| [Plugins/Spline/include/AxleSpline/SplineComponent.hpp](../../Plugins/Spline/include/AxleSpline/SplineComponent.hpp) | 46 | SplineComponent |
| [Plugins/Spline/include/AxleSpline/SplineEditor.hpp](../../Plugins/Spline/include/AxleSpline/SplineEditor.hpp) | 33 | SplineEditor |
| [Plugins/Spline/include/AxleSpline/SplinePlugin.hpp](../../Plugins/Spline/include/AxleSpline/SplinePlugin.hpp) | 20 | SplinePlugin |
| [Plugins/Spline/include/AxleSpline/SplineSerializer.hpp](../../Plugins/Spline/include/AxleSpline/SplineSerializer.hpp) | 15 | SplineSerializer |
| [Plugins/Spline/include/AxleSpline/TrackGenerator.hpp](../../Plugins/Spline/include/AxleSpline/TrackGenerator.hpp) | 43 | TrackMeshData, TrackGenerator |
| [Plugins/Spline/src/Curve3D.cpp](../../Plugins/Spline/src/Curve3D.cpp) | 474 | — |
| [Plugins/Spline/src/SplineEditor.cpp](../../Plugins/Spline/src/SplineEditor.cpp) | 383 | — |
| [Plugins/Spline/src/SplinePlugin.cpp](../../Plugins/Spline/src/SplinePlugin.cpp) | 24 | — |
| [Plugins/Spline/src/SplineSerializer.cpp](../../Plugins/Spline/src/SplineSerializer.cpp) | 142 | — |
| [Plugins/Spline/src/TrackGenerator.cpp](../../Plugins/Spline/src/TrackGenerator.cpp) | 220 | ProfilePoint |
| [Plugins/Terrain/CMakeLists.txt](../../Plugins/Terrain/CMakeLists.txt) | 55 | — |
| [Plugins/Terrain/include/AxleTerrain/TerrainChunk.hpp](../../Plugins/Terrain/include/AxleTerrain/TerrainChunk.hpp) | 45 | TerrainVertex, TerrainChunk |
| [Plugins/Terrain/include/AxleTerrain/TerrainCollider.hpp](../../Plugins/Terrain/include/AxleTerrain/TerrainCollider.hpp) | 42 | TerrainCollider |
| [Plugins/Terrain/include/AxleTerrain/TerrainComponent.hpp](../../Plugins/Terrain/include/AxleTerrain/TerrainComponent.hpp) | 35 | TerrainComponent |
| [Plugins/Terrain/include/AxleTerrain/TerrainData.hpp](../../Plugins/Terrain/include/AxleTerrain/TerrainData.hpp) | 85 | TerrainSplatLayers, TerrainData |
| [Plugins/Terrain/include/AxleTerrain/TerrainEditor.hpp](../../Plugins/Terrain/include/AxleTerrain/TerrainEditor.hpp) | 27 | TerrainEditor |
| [Plugins/Terrain/include/AxleTerrain/TerrainMesh.hpp](../../Plugins/Terrain/include/AxleTerrain/TerrainMesh.hpp) | 51 | TerrainMesh |
| [Plugins/Terrain/include/AxleTerrain/TerrainPlugin.hpp](../../Plugins/Terrain/include/AxleTerrain/TerrainPlugin.hpp) | 22 | TerrainPlugin |
| [Plugins/Terrain/include/AxleTerrain/TerrainSculptor.hpp](../../Plugins/Terrain/include/AxleTerrain/TerrainSculptor.hpp) | 62 | SculptSettings, SculptResult, TerrainSculptor |
| [Plugins/Terrain/include/AxleTerrain/TerrainSerializer.hpp](../../Plugins/Terrain/include/AxleTerrain/TerrainSerializer.hpp) | 15 | TerrainSerializer |
| [Plugins/Terrain/include/AxleTerrain/TerrainSplineCarver.hpp](../../Plugins/Terrain/include/AxleTerrain/TerrainSplineCarver.hpp) | 33 | CarveSettings, TerrainSplineCarver |
| [Plugins/Terrain/src/TerrainCollider.cpp](../../Plugins/Terrain/src/TerrainCollider.cpp) | 189 | — |
| [Plugins/Terrain/src/TerrainData.cpp](../../Plugins/Terrain/src/TerrainData.cpp) | 301 | — |
| [Plugins/Terrain/src/TerrainEditor.cpp](../../Plugins/Terrain/src/TerrainEditor.cpp) | 207 | — |
| [Plugins/Terrain/src/TerrainMesh.cpp](../../Plugins/Terrain/src/TerrainMesh.cpp) | 312 | — |
| [Plugins/Terrain/src/TerrainPlugin.cpp](../../Plugins/Terrain/src/TerrainPlugin.cpp) | 24 | — |
| [Plugins/Terrain/src/TerrainSculptor.cpp](../../Plugins/Terrain/src/TerrainSculptor.cpp) | 179 | — |
| [Plugins/Terrain/src/TerrainSerializer.cpp](../../Plugins/Terrain/src/TerrainSerializer.cpp) | 116 | — |
| [Plugins/Terrain/src/TerrainSplineCarver.cpp](../../Plugins/Terrain/src/TerrainSplineCarver.cpp) | 107 | — |
| [Runtime/CMakeLists.txt](../../Runtime/CMakeLists.txt) | 43 | — |
| [Runtime/src/Main.cpp](../../Runtime/src/Main.cpp) | 122 | — |
| [Runtime/src/RuntimeLayer.cpp](../../Runtime/src/RuntimeLayer.cpp) | 472 | CanvasItem |
| [Runtime/src/RuntimeLayer.hpp](../../Runtime/src/RuntimeLayer.hpp) | 55 | RuntimeLayer |
| `Samples/TestProject/assets/materials/BlackRubber.axleasset` (historical path; unavailable in this checkout) | 19 | — |
| `Samples/TestProject/assets/materials/GoldMetallic.axleasset` (historical path; unavailable in this checkout) | 19 | — |
| `Samples/TestProject/assets/materials/SilverCarPaint.axleasset` (historical path; unavailable in this checkout) | 19 | — |
| `Samples/TestProject/assets/models/SportsCar_Chassis.axlemesh` (historical path; unavailable in this checkout) | 1172 | — |
| `Samples/TestProject/assets/models/SportsCar_Wheel.axlemesh` (historical path; unavailable in this checkout) | 887 | — |
| `Samples/TestProject/assets/prefabs/SportsCar_Assembly.axle` (historical path; unavailable in this checkout) | 383 | — |
| [Samples/TestProject/assets/scenes/Test.axlescene](../../Samples/TestProject/assets/scenes/Test.axlescene) | 360 | — |
| `Samples/TestProject/assets/scripts/CarController.hpp` (historical path; unavailable in this checkout) | 196 | CarController |
| `Samples/TestProject/assets/scripts/RacingHUD.hpp` (historical path; unavailable in this checkout) | 514 | RacingHUD |
| `Samples/TestProject/assets/scripts/VehicleChaseCamera.hpp` (historical path; unavailable in this checkout) | 655 | VehicleChaseCamera |
| [Samples/TestProject/TestProject.axleproj](../../Samples/TestProject/TestProject.axleproj) | 5 | — |
| [CMakeLists.txt](../../CMakeLists.txt) | 75 | — |
| [build.sh](../../build.sh) | 101 | — |
| [.gitignore](../../.gitignore) | 90 | — |
| [.gitmodules](../../.gitmodules) | 28 | — |
| [Tests/Fixtures/LegacyProject.axleproj](../../Tests/Fixtures/LegacyProject.axleproj) | 6 | — |
