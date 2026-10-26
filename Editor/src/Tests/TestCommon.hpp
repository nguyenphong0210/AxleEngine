#pragma once

#include <Axle/ScriptRegistry.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Scene.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/Material.hpp>
#include <Axle/AssetMetadataSerializer.hpp>
#include <Axle/AssetTypes.hpp>
#include <Axle/ContentBrowserPanel.hpp>
#include <Axle/PhysicsMaterial.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/ScriptCompiler.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/JobSystemEnkiTS.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/ParallelECS.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/Vehicle.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/Package.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/ShadowMap.hpp>
#include <Axle/Cubemap.hpp>
#include <Axle/Skybox.hpp>
#include <Axle/AudioEngine.hpp>
#include "../assets/scripts/VehicleChaseCamera.hpp"
#include "../assets/scripts/RacingHUD.hpp"
#include "../assets/scripts/ThirdPersonController.hpp"
#include "../assets/scripts/ThirdPersonCamera.hpp"
#include <glm/gtc/quaternion.hpp>
#include <AxleSpline/SplinePlugin.hpp>
#include <AxleSpline/Curve3D.hpp>
#include <AxleSpline/SplineComponent.hpp>
#include <AxleSpline/TrackGenerator.hpp>
#include <AxleSpline/SplineEditor.hpp>
#include <AxleSpline/SplineSerializer.hpp>
#include <AxleTerrain/TerrainPlugin.hpp>
#include <AxleTerrain/TerrainData.hpp>
#include <AxleTerrain/TerrainCollider.hpp>
#include <AxleTerrain/TerrainChunk.hpp>
#include <AxleTerrain/TerrainMesh.hpp>
#include <AxleTerrain/TerrainSculptor.hpp>
#include <AxleTerrain/TerrainSplineCarver.hpp>
#include <AxleTerrain/TerrainComponent.hpp>
#include <AxleTerrain/TerrainSerializer.hpp>
#include <Axle/Skeleton.hpp>
#include <Axle/AnimationClip.hpp>
#include <Axle/AnimationPose.hpp>
#include <Axle/BlendSpace1D.hpp>
#include <Axle/AnimationStateMachine.hpp>
#include <Axle/Animator.hpp>
#include <Axle/SkeletalMesh.hpp>
#include <Axle/SkeletalMeshImporter.hpp>
#include <Axle/SkinningPipeline.hpp>
#include <Axle/SceneRenderer.hpp>
#include <Axle/CharacterController.hpp>
#include <Axle/VFXCommon.hpp>
#include <Axle/ParticlePool.hpp>
#include <Axle/ParticleEmitter.hpp>
#include <Axle/ParticleSystem.hpp>
#include <Axle/ParticleRenderer.hpp>
#include <Axle/CurlNoise.hpp>
#include <Axle/ParticleCollision.hpp>
#include <Axle/ParticleForceField.hpp>
#include <Axle/VehicleVFX.hpp>
#include <Axle/CharacterVFX.hpp>
#include <Axle/VFXEditorPanel.hpp>
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHISurface.hpp>
#include <Axle/RHI/RHISwapChain.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/RHI/RHIPipeline.hpp>
#include <Axle/RHI/RHIBarrier.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIStreamer.hpp>
#include <Axle/RHI/RHIECSStreamer.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHISwitcher.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <Axle/RHI/RHIMesh.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHISampler.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHIBindless.hpp>
#include <Axle/RHI/RHITransform.hpp>
#include <Axle/RHI/RHILight.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHIParticleRenderer.hpp>
#include <Axle/RHIPhysicsDebugRenderer.hpp>
#include <Axle/RHIObjectPicker.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/UI/UITypes.hpp>
#include <Axle/UI/Canvas2D.hpp>
#include <Axle/UI/UIFont.hpp>
#include <Axle/UI/UINode.hpp>
#include <Axle/UI/UIDocument.hpp>
#include <Axle/UI/UIParser.hpp>
#include <Axle/Application.hpp>
#include <Axle/Window.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#ifdef _WIN32
#if defined(APIENTRY)
#undef APIENTRY
#endif
#include <d3d12.h>
#endif
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstring>
#include <chrono>
#include <atomic>
#include <unordered_set>
#include <fstream>
#include <string>
#include <filesystem>
#include <cmath>
#include <cassert>
#include <glm/gtc/matrix_transform.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <crtdbg.h>
#endif

#include <Axle/RenderCommand.hpp>

// Ensure test assertions are active and evaluate expressions even in Release builds (where NDEBUG is defined):
#ifdef assert
#undef assert
#endif
#define assert(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "\n[TEST FAILED] Assertion failed: " << #expr << "\n  File: " << __FILE__ << "\n  Line: " << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while (0)

struct ScopedGLContext {
    std::unique_ptr<Axle::Window> Window;
    ScopedGLContext() {
        if (!Axle::Window::HasActiveContext()) {
            if (!Axle::Window::IsGLFWInitialized()) {
                glfwInit();
            }
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            Axle::WindowProps props;
            props.Title = "Axle Test GL Context";
            props.Width = 640;
            props.Height = 480;
            props.GraphicsBackend = Axle::GraphicsAPI::OpenGL;
            props.Maximized = false;
            Window = std::make_unique<Axle::Window>(props);
            Axle::RenderCommand::Init(reinterpret_cast<Axle::GLProcAddressFn>(glfwGetProcAddress));
        }
    }
    ~ScopedGLContext() {
        if (Window) {
            glfwMakeContextCurrent(nullptr);
            Window.reset();
        }
    }
};

// =============================================================================
// Test Mock Script for Testing Reflection & Serialization
// =============================================================================
class CarPlayerScript : public Axle::ScriptableEntity {
public:
    float ForwardTorque = 1500.0f;
    float MaxSteerAngle = 35.0f;
    float BrakeTorque = 2500.0f;
    bool EnableBoost = true;

    void OnCreate() override {}
    void OnUpdate(Axle::Timestep ts) override { (void)ts; }
    void OnDestroy() override {}

    AXLE_SCRIPT_FIELDS(CarPlayerScript,
        AXLE_FIELD(ForwardTorque, 100.0f, 10000.0f, "Engine drive torque in N.m"),
        AXLE_FIELD(MaxSteerAngle, 10.0f, 60.0f, "Maximum steering angle in degrees"),
        AXLE_FIELD(BrakeTorque, 100.0f, 10000.0f, "Braking force in N.m"),
        AXLE_FIELD(EnableBoost, "Enable speed boost")
    )
};

AXLE_REGISTER_SCRIPT(CarPlayerScript);

inline bool FloatEq(float a, float b, float eps = 1e-3f) {
    return std::abs(a - b) <= eps;
}

inline bool Vec3Eq(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) {
    return FloatEq(a.x, b.x, eps) && FloatEq(a.y, b.y, eps) && FloatEq(a.z, b.z, eps);
}

// =============================================================================
// Test Step Declarations (Steps 1 to 84)
// =============================================================================
// Milestone 1 (Steps 1-4)
bool RunTest_Step1_ScriptRegistry();
bool RunTest_Step2_HierarchyTraversal();
bool RunTest_Step3_FieldBuffersAndBinding();
bool RunTest_Step4_AssemblyRoundtrip();

// Milestone 2 (Steps 5-7)
bool RunTest_Step5_RelationshipAndTransform();
bool RunTest_Step6_Phase15_ArchetypePresets();
bool RunTest_Step7_Phase15_HierarchicalTreeView();

// Milestone 3 (Steps 8-13)
bool RunTest_Step8_Phase15_DragAndDropReparenting();
bool RunTest_Step9_Phase15_DeferredAndRecursiveLifecycle();
bool RunTest_Step10_Phase16_StructuredComponentWidgets();
bool RunTest_Step11_Phase16_CategorizedComponentPopup();
bool RunTest_Step12_Phase16_ScriptReflectionMacro();
bool RunTest_Step13_Phase16_DualContextPropertiesInspector();

// Milestone 4 (Steps 14-21)
bool RunTest_Step14_Phase17_AssetUUIDAndMetaPipeline();
bool RunTest_Step15_Phase17_RightClickAssetCreation();
bool RunTest_Step16_Phase17_DedicatedMaterialInspector();
bool RunTest_Step17_Phase18_DragDropPipelinesAndClassCreation();
bool RunTest_Step18_Phase18_SafeSceneStateSimulation();
bool RunTest_Step19_Phase18_DecoupledAxleScriptsModule();
bool RunTest_Step20_Phase18_AntiLockShadowCopyAndStatePreservation();
bool RunTest_Step21_Phase18_AsyncBackgroundScriptCompiler();

// Milestone 5 (Steps 22-26)
bool RunTest_Step22_Phase19_CoreJobSystemEngine();
bool RunTest_Step23_Phase19_JoltPhysicsBridgeEnkiTS();
bool RunTest_Step24_Phase19_ParallelECSProcessing();
bool RunTest_Step25_Phase20_ComponentPerWheelVehicle();
bool RunTest_Step26_CarTestSceneSimulate();

// Milestone 6 (Steps 27-32)
bool RunTest_Step27_Phase23_PackageAndVFS();
bool RunTest_Step28_Phase24_MaterialAndFlyweight();
bool RunTest_Step29_Phase25_SoftShadowsAndDynamicShadowMap();
bool RunTest_Step30_Phase26_HDRCubemapAndIBL();
bool RunTest_Step31_Phase27_CoreAudioSubsystem();
bool RunTest_Step32_Phase28_MultiPartVehicleRigging();

// Milestone 7 (Steps 33-41)
bool RunTest_Step33_Phase29A_SplineTrackPlugin();
bool RunTest_Step34_Phase29B_TerrainPlugin();
bool RunTest_Step35_SceneWithSplineAndTerrain();
bool RunTest_Step36_Phase30_Step1_CoreSkeletalDataStructures();
bool RunTest_Step37_Phase30_Step2_GPUSkinningAndShaderPipeline();
bool RunTest_Step38_Phase30_Step3_AnimationPoseEvaluatorAndBlendingTree();
bool RunTest_Step39_Phase30_Step4_KinematicCharacterController();
bool RunTest_Step40_Phase30_Step5_ECSIntegrationAndThirdPersonGameplayController();
bool RunTest_Step41_Phase30_Step6_CharacterSandboxPlaygroundAndSceneIntegration();

// Milestone 8 - Part 1: VFX & Particle Pipelines (Steps 42-47)
bool RunTest_Step42_Phase31_Step1_CoreVFXArchitectureAndParticlePool();
bool RunTest_Step43_Phase31_Step2_GPUInstancingAndBillboardingPipeline();
bool RunTest_Step44_Phase31_Step3_CurlNoisePhysicsCollisionsAndForceFields();
bool RunTest_Step45_Phase31_Step4_TextureSheetFlipbooksAndShadingPolish();
bool RunTest_Step46_Phase31_Step5_ECSIntegrationAndGameplayEventVFXEmitters();
bool RunTest_Step47_Phase31_Step6_VFXEditorPanelAndFinalVFXIntegration();

// Milestone 8 - Part 2: Modern NRI RHI Foundations (Steps 48-66)
bool RunTest_Step48_Phase32_Step1_NRICoreAndRHIDeviceLifecycle();
bool RunTest_Step49_Phase32_Step2_SwapchainAndFramesInFlight();
bool RunTest_Step50_Phase32_Step3_CrossAPIShaderCompiler();
bool RunTest_Step51_Phase32_Step4_PipelineLayoutAndPSOHashCache();
bool RunTest_Step52_Phase32_Step4_ExplicitBarrierManagement();
bool RunTest_Step53_Phase32_Step5_Part1_GPUMemoryAndBuffers();
bool RunTest_Step54_Phase32_Step5_Part2_DynamicStagingStreamer();
bool RunTest_Step55_Phase32_Step5_Part3_ECSStreamingAndIndirectDraw();
bool RunTest_Step61_Phase32_Step6_Part1_MultiBackendImGuiCore();
bool RunTest_Step62_Phase32_Step6_Part2_ViewportDescriptorBinding();
bool RunTest_Step63_Phase32_Step6_Part3_EngineRHISwitcherAndEditorWiring();
bool RunTest_Step64_Phase32_Step7_HeadlessRHIDeviceInitialization();
bool RunTest_Step65_Phase32_Step7_MultithreadedCommandRecording();
bool RunTest_Step66_Phase32_Step7_ValidationBarrierStateMachinePSOCacheSwapchain();

// Milestone 8 - Part 3: Viewport Multi-Pass PBR Scene Renderer (Steps 67-84)
bool RunTest_Step67_Phase33_Step1_CoreGeometryAndStreamer();
bool RunTest_Step68_Phase33_Step1_TransformStreamingAndRHISceneRenderer();
bool RunTest_Step69_Phase33_Step2_UnifiedHLSLShaderPipelineAndForwardPBR();
bool RunTest_Step70_Phase33_Step2_MetallicRoughnessPBRPipeline();
bool RunTest_Step71_Phase33_Step2_BindlessTexturesAndAnisotropicFiltering();
bool RunTest_Step72_Phase33_Step2_ForwardPlusMultiLightGPUBuffer();
bool RunTest_Step73_DirectionalCascadedShadowMapping();
bool RunTest_Step74_ImageBasedLightingAndEnvironmentMaps();
bool RunTest_Step75_ProceduralAtmosphericSkyboxPass();
bool RunTest_Step76_HDRRenderTargetAndResizeSync();
bool RunTest_Step77_ExposureAndToneMappingPass();
bool RunTest_Step78_AntiAliasingPass();
bool RunTest_Step79_GPUParticleVFXPass();
bool RunTest_Step80_RHIPhysicsDebugLinePipeline();
bool RunTest_Step81_PhysicsDebugSceneRendererIntegration();
bool RunTest_Step82_GPUObjectIDPicking();
bool RunTest_Step83_ViewportClickPickingIntegration();
bool RunTest_Step84_TripleBackendSwitchingAndVisualParity();

// Milestone 8 - Part 4: Phase 34 Declarative AxleUI In-Game UI & Hardware-Accelerated Canvas2D Subsystem (Steps 85+)
bool RunTest_Step85_Phase34_Step1_Part1_DecoupledAxleUI2DLibraryAndUITypes();
bool RunTest_Step86_Phase34_Step1_Part2_Canvas2DBatchingAndSDFPipeline();
bool RunTest_Step87_Phase34_Step2_MSDFFontAndTextLayout();
bool RunTest_Step88_Phase34_Step3_DOMTreeAnd2PassLayoutEngine();
bool RunTest_Step89_Phase34_Step4_DeclarativeUIParserAndHotReloading();
bool RunTest_Step90_Phase34_Step5_DataBindingAndHitTest();
