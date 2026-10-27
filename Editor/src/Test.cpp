#include "Tests/TestCommon.hpp"

// =============================================================================
// MAIN ENTRY POINT FOR TEST SUITE
// =============================================================================
int main(int argc, char** argv) {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    int targetStep = 0; // 0 = all
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "1" || arg == "step1" || arg == "--step1") targetStep = 1;
        else if (arg == "2" || arg == "step2" || arg == "--step2") targetStep = 2;
        else if (arg == "3" || arg == "step3" || arg == "--step3") targetStep = 3;
        else if (arg == "4" || arg == "step4" || arg == "--step4") targetStep = 4;
        else if (arg == "5" || arg == "step5" || arg == "--step5" || arg == "--phase14") targetStep = 5;
        else if (arg == "6" || arg == "step6" || arg == "--step6" || arg == "--phase15") targetStep = 6;
        else if (arg == "7" || arg == "step7" || arg == "--step7" || arg == "--phase15.2") targetStep = 7;
        else if (arg == "8" || arg == "step8" || arg == "--step8" || arg == "--phase15.3") targetStep = 8;
        else if (arg == "9" || arg == "step9" || arg == "--step9" || arg == "--phase15.4") targetStep = 9;
        else if (arg == "10" || arg == "step10" || arg == "--step10" || arg == "--phase16" || arg == "--phase16.1") targetStep = 10;
        else if (arg == "11" || arg == "step11" || arg == "--step11" || arg == "--phase16.2") targetStep = 11;
        else if (arg == "12" || arg == "step12" || arg == "--step12" || arg == "--phase16.3") targetStep = 12;
        else if (arg == "13" || arg == "step13" || arg == "--step13" || arg == "--phase16.4") targetStep = 13;
        else if (arg == "14" || arg == "step14" || arg == "--step14" || arg == "--phase17" || arg == "--phase17.1") targetStep = 14;
        else if (arg == "15" || arg == "step15" || arg == "--step15" || arg == "--phase17.2") targetStep = 15;
        else if (arg == "16" || arg == "step16" || arg == "--step16" || arg == "--phase17.3") targetStep = 16;
        else if (arg == "17" || arg == "step17" || arg == "--step17" || arg == "--phase18" || arg == "--phase18.1") targetStep = 17;
        else if (arg == "18" || arg == "step18" || arg == "--step18" || arg == "--phase18.2") targetStep = 18;
        else if (arg == "19" || arg == "step19" || arg == "--step19" || arg == "--phase18.3") targetStep = 19;
        else if (arg == "20" || arg == "step20" || arg == "--step20") targetStep = 20;
        else if (arg == "21" || arg == "step21" || arg == "--step21") targetStep = 21;
        else if (arg == "22" || arg == "step22" || arg == "--step22" || arg == "--phase19" || arg == "--phase19.2") targetStep = 22;
        else if (arg == "23" || arg == "step23" || arg == "--step23" || arg == "--phase19.3") targetStep = 23;
        else if (arg == "24" || arg == "step24" || arg == "--step24" || arg == "--phase19.4") targetStep = 24;
        else if (arg == "25" || arg == "step25" || arg == "--step25" || arg == "--phase20" || arg == "--phase20.1") targetStep = 25;
        else if (arg == "26" || arg == "step26" || arg == "--step26") targetStep = 26;
        else if (arg == "27" || arg == "step27" || arg == "--step27" || arg == "--phase23") targetStep = 27;
        else if (arg == "28" || arg == "step28" || arg == "--step28" || arg == "--phase24") targetStep = 28;
        else if (arg == "29" || arg == "step29" || arg == "--step29" || arg == "--phase25") targetStep = 29;
        else if (arg == "30" || arg == "step30" || arg == "--step30" || arg == "--phase26") targetStep = 30;
        else if (arg == "31" || arg == "step31" || arg == "--step31" || arg == "--phase27") targetStep = 31;
        else if (arg == "32" || arg == "step32" || arg == "--step32" || arg == "--phase28") targetStep = 32;
        else if (arg == "33" || arg == "step33" || arg == "--step33" || arg == "--phase29" || arg == "--phase29a") targetStep = 33;
        else if (arg == "34" || arg == "step34" || arg == "--step34" || arg == "--phase29b") targetStep = 34;
        else if (arg == "35" || arg == "step35" || arg == "--step35" || arg == "--phase29c" || arg == "--scene") targetStep = 35;
        else if (arg == "36" || arg == "step36" || arg == "--step36" || arg == "--phase30" || arg == "--phase30a") targetStep = 36;
        else if (arg == "37" || arg == "step37" || arg == "--step37" || arg == "--phase30b") targetStep = 37;
        else if (arg == "38" || arg == "step38" || arg == "--step38" || arg == "--phase30c") targetStep = 38;
        else if (arg == "39" || arg == "step39" || arg == "--step39" || arg == "--phase30d") targetStep = 39;
        else if (arg == "40" || arg == "step40" || arg == "--step40" || arg == "--phase30e") targetStep = 40;
        else if (arg == "41" || arg == "step41" || arg == "--step41" || arg == "--phase30f" || arg == "--sandbox") targetStep = 41;
        else if (arg == "42" || arg == "step42" || arg == "--step42" || arg == "--phase31" || arg == "--phase31a" || arg == "--vfx") targetStep = 42;
        else if (arg == "43" || arg == "step43" || arg == "--step43" || arg == "--phase31b" || arg == "--instancing") targetStep = 43;
        else if (arg == "44" || arg == "step44" || arg == "--step44" || arg == "--phase31c" || arg == "--dynamics") targetStep = 44;
        else if (arg == "45" || arg == "step45" || arg == "--step45" || arg == "--phase31d" || arg == "--shading") targetStep = 45;
        else if (arg == "46" || arg == "step46" || arg == "--step46" || arg == "--phase31e" || arg == "--ecs-vfx") targetStep = 46;
        else if (arg == "47" || arg == "step47" || arg == "--step47" || arg == "--phase31f" || arg == "--vfx-editor") targetStep = 47;
        else if (arg == "48" || arg == "step48" || arg == "--step48" || arg == "--phase32" || arg == "--phase32a" || arg == "--rhi") targetStep = 48;
        else if (arg == "49" || arg == "step49" || arg == "--step49" || arg == "--swapchain" || arg == "--phase32b") targetStep = 49;
        else if (arg == "50" || arg == "step50" || arg == "--step50" || arg == "--shader" || arg == "--phase32c") targetStep = 50;
        else if (arg == "51" || arg == "step51" || arg == "--step51" || arg == "--pso" || arg == "--phase32d") targetStep = 51;
        else if (arg == "52" || arg == "step52" || arg == "--step52" || arg == "--barrier" || arg == "--phase32e") targetStep = 52;
        else if (arg == "53" || arg == "step53" || arg == "--step53" || arg == "--memory" || arg == "--buffer" || arg == "--phase32f") targetStep = 53;
        else if (arg == "54" || arg == "step54" || arg == "--step54" || arg == "--streamer" || arg == "--phase32g") targetStep = 54;
        else if (arg == "55" || arg == "step55" || arg == "--step55" || arg == "--ecs-stream" || arg == "--indirect" || arg == "--phase32h") targetStep = 55;
        else if (arg == "61" || arg == "step61" || arg == "--step61" || arg == "--imgui-d3d12") targetStep = 61;
        else if (arg == "62" || arg == "step62" || arg == "--step62" || arg == "--viewport-descriptor") targetStep = 62;
        else if (arg == "63" || arg == "step63" || arg == "--step63" || arg == "--rhi-switcher") targetStep = 63;
        else if (arg == "64" || arg == "step64" || arg == "--step64" || arg == "--headless" || arg == "--warp" || arg == "--rhi-headless") targetStep = 64;
        else if (arg == "65" || arg == "step65" || arg == "--step65" || arg == "--parallel-rhi" || arg == "--concurrent-rhi") targetStep = 65;
        else if (arg == "66" || arg == "step66" || arg == "--step66" || arg == "--rhi-validation" || arg == "--pso-swapchain") targetStep = 66;
        else if (arg == "67" || arg == "step67" || arg == "--step67" || arg == "--geometry" || arg == "--mesh-streaming") targetStep = 67;
        else if (arg == "68" || arg == "step68" || arg == "--step68" || arg == "--scene-renderer" || arg == "--transforms") targetStep = 68;
        else if (arg == "69" || arg == "step69" || arg == "--step69" || arg == "--pbr-pipeline" || arg == "--hlsl2021" || arg == "--forward-pbr") targetStep = 69;
        else if (arg == "70" || arg == "step70" || arg == "--step70" || arg == "--pbr-material" || arg == "--metallic-roughness" || arg == "--pbr-maps") targetStep = 70;
        else if (arg == "71" || arg == "step71" || arg == "--step71" || arg == "--bindless" || arg == "--anisotropic") targetStep = 71;
        else if (arg == "72" || arg == "step72" || arg == "--step72" || arg == "--multi-light" || arg == "--forward-plus" || arg == "--lights") targetStep = 72;
        else if (arg == "73" || arg == "step73" || arg == "--step73" || arg == "--csm" || arg == "--cascades") targetStep = 73;
        else if (arg == "74" || arg == "step74" || arg == "--step74" || arg == "--ibl" || arg == "--environment-maps") targetStep = 74;
        else if (arg == "75" || arg == "step75" || arg == "--step75" || arg == "--skybox" || arg == "--atmospheric") targetStep = 75;
        else if (arg == "76" || arg == "step76" || arg == "--step76" || arg == "--hdr" || arg == "--resize" || arg == "--hdr-target") targetStep = 76;
        else if (arg == "77" || arg == "step77" || arg == "--step77" || arg == "--tonemap" || arg == "--tone-mapping" || arg == "--exposure") targetStep = 77;
        else if (arg == "78" || arg == "step78" || arg == "--step78" || arg == "--aa" || arg == "--fxaa" || arg == "--msaa" || arg == "--anti-aliasing") targetStep = 78;
        else if (arg == "79" || arg == "step79" || arg == "--step79" || arg == "--particles" || arg == "--gpu-particles" || arg == "--vfx-rhi") targetStep = 79;
        else if (arg == "80" || arg == "step80" || arg == "--step80" || arg == "--physics-debug" || arg == "--wireframe" || arg == "--line-pipeline") targetStep = 80;
        else if (arg == "81" || arg == "step81" || arg == "--step81" || arg == "--physics-scene" || arg == "--debug-overlay") targetStep = 81;
        else if (arg == "82" || arg == "step82" || arg == "--step82" || arg == "--object-id" || arg == "--picking" || arg == "--gpu-picking") targetStep = 82;
        else if (arg == "83" || arg == "step83" || arg == "--step83" || arg == "--click-picking" || arg == "--viewport-picking") targetStep = 83;
        else if (arg == "84" || arg == "step84" || arg == "--step84" || arg == "--triple-backend" || arg == "--parity" || arg == "--rhi-parity") targetStep = 84;
        else if (arg == "85" || arg == "step85" || arg == "--step85" || arg == "--axleui2d" || arg == "--ui-types" || arg == "--decoupled-ui") targetStep = 85;
        else if (arg == "86" || arg == "step86" || arg == "--step86" || arg == "--canvas2d" || arg == "--batch" || arg == "--sdf-shader") targetStep = 86;
        else if (arg == "87" || arg == "step87" || arg == "--step87" || arg == "--msdf" || arg == "--font" || arg == "--text-layout") targetStep = 87;
        else if (arg == "88" || arg == "step88" || arg == "--step88" || arg == "--dom-tree" || arg == "--layout-2pass" || arg == "--uinode") targetStep = 88;
        else if (arg == "89" || arg == "step89" || arg == "--step89" || arg == "--parser" || arg == "--uiparser" || arg == "--hot-reload") targetStep = 89;
        else if (arg == "90" || arg == "step90" || arg == "--step90" || arg == "--databinding" || arg == "--hit-test" || arg == "--uicontext") targetStep = 90;
        else if (arg == "91" || arg == "step91" || arg == "--step91" || arg == "--racinghud" || arg == "--pausemenu" || arg == "--hud-integration") targetStep = 91;
        else if (arg.rfind("--step=", 0) == 0) {
            try { targetStep = std::stoi(arg.substr(7)); } catch (...) {}
        }
    }

    std::cout << "\n========================================================" << std::endl;
    std::cout << "        AXLE ENGINE - CORE & SCRIPTING TEST SUITE       " << std::endl;
    if (targetStep == 0) {
        std::cout << "              RUNNING ALL 68 STEPS                      " << std::endl;
    } else {
        std::cout << "              RUNNING STEP " << targetStep << " ONLY                    " << std::endl;
    }
    std::cout << "========================================================" << std::endl;

    bool allPassed = true;

    if (targetStep == 0 || targetStep == 1) {
        if (!RunTest_Step1_ScriptRegistry()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 2) {
        if (!RunTest_Step2_HierarchyTraversal()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 3) {
        if (!RunTest_Step3_FieldBuffersAndBinding()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 4) {
        if (!RunTest_Step4_AssemblyRoundtrip()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 5) {
        if (!RunTest_Step5_RelationshipAndTransform()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 6) {
        if (!RunTest_Step6_Phase15_ArchetypePresets()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 7) {
        if (!RunTest_Step7_Phase15_HierarchicalTreeView()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 8) {
        if (!RunTest_Step8_Phase15_DragAndDropReparenting()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 9) {
        if (!RunTest_Step9_Phase15_DeferredAndRecursiveLifecycle()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 10) {
        if (!RunTest_Step10_Phase16_StructuredComponentWidgets()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 11) {
        if (!RunTest_Step11_Phase16_CategorizedComponentPopup()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 12) {
        if (!RunTest_Step12_Phase16_ScriptReflectionMacro()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 13) {
        if (!RunTest_Step13_Phase16_DualContextPropertiesInspector()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 14) {
        if (!RunTest_Step14_Phase17_AssetUUIDAndMetaPipeline()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 15) {
        if (!RunTest_Step15_Phase17_RightClickAssetCreation()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 16) {
        if (!RunTest_Step16_Phase17_DedicatedMaterialInspector()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 17) {
        if (!RunTest_Step17_Phase18_DragDropPipelinesAndClassCreation()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 18) {
        if (!RunTest_Step18_Phase18_SafeSceneStateSimulation()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 19) {
        if (!RunTest_Step19_Phase18_DecoupledAxleScriptsModule()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 20) {
        if (!RunTest_Step20_Phase18_AntiLockShadowCopyAndStatePreservation()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 21) {
        if (!RunTest_Step21_Phase18_AsyncBackgroundScriptCompiler()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 22) {
        if (!RunTest_Step22_Phase19_CoreJobSystemEngine()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 23) {
        if (!RunTest_Step23_Phase19_JoltPhysicsBridgeEnkiTS()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 24) {
        if (!RunTest_Step24_Phase19_ParallelECSProcessing()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 25) {
        if (!RunTest_Step25_Phase20_ComponentPerWheelVehicle()) allPassed = false;
    }
    if (targetStep == 26) {
        if (!RunTest_Step26_CarTestSceneSimulate()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 27) {
        if (!RunTest_Step27_Phase23_PackageAndVFS()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 28) {
        if (!RunTest_Step28_Phase24_MaterialAndFlyweight()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 29) {
        if (!RunTest_Step29_Phase25_SoftShadowsAndDynamicShadowMap()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 30) {
        if (!RunTest_Step30_Phase26_HDRCubemapAndIBL()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 31) {
        if (!RunTest_Step31_Phase27_CoreAudioSubsystem()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 32) {
        if (!RunTest_Step32_Phase28_MultiPartVehicleRigging()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 33) {
        if (!RunTest_Step33_Phase29A_SplineTrackPlugin()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 34) {
        if (!RunTest_Step34_Phase29B_TerrainPlugin()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 35) {
        if (!RunTest_Step35_SceneWithSplineAndTerrain()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 36) {
        if (!RunTest_Step36_Phase30_Step1_CoreSkeletalDataStructures()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 37) {
        if (!RunTest_Step37_Phase30_Step2_GPUSkinningAndShaderPipeline()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 38) {
        if (!RunTest_Step38_Phase30_Step3_AnimationPoseEvaluatorAndBlendingTree()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 39) {
        if (!RunTest_Step39_Phase30_Step4_KinematicCharacterController()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 40) {
        if (!RunTest_Step40_Phase30_Step5_ECSIntegrationAndThirdPersonGameplayController()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 41) {
        if (!RunTest_Step41_Phase30_Step6_CharacterSandboxPlaygroundAndSceneIntegration()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 42) {
        if (!RunTest_Step42_Phase31_Step1_CoreVFXArchitectureAndParticlePool()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 43) {
        if (!RunTest_Step43_Phase31_Step2_GPUInstancingAndBillboardingPipeline()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 44) {
        if (!RunTest_Step44_Phase31_Step3_CurlNoisePhysicsCollisionsAndForceFields()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 45) {
        if (!RunTest_Step45_Phase31_Step4_TextureSheetFlipbooksAndShadingPolish()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 46) {
        if (!RunTest_Step46_Phase31_Step5_ECSIntegrationAndGameplayEventVFXEmitters()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 47) {
        if (!RunTest_Step47_Phase31_Step6_VFXEditorPanelAndFinalVFXIntegration()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 48) {
        if (!RunTest_Step48_Phase32_Step1_NRICoreAndRHIDeviceLifecycle()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 49) {
        if (!RunTest_Step49_Phase32_Step2_SwapchainAndFramesInFlight()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 50) {
        if (!RunTest_Step50_Phase32_Step3_CrossAPIShaderCompiler()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 51) {
        if (!RunTest_Step51_Phase32_Step4_PipelineLayoutAndPSOHashCache()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 52) {
        if (!RunTest_Step52_Phase32_Step4_ExplicitBarrierManagement()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 53) {
        if (!RunTest_Step53_Phase32_Step5_Part1_GPUMemoryAndBuffers()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 54) {
        if (!RunTest_Step54_Phase32_Step5_Part2_DynamicStagingStreamer()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 55) {
        if (!RunTest_Step55_Phase32_Step5_Part3_ECSStreamingAndIndirectDraw()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 61) {
        if (!RunTest_Step61_Phase32_Step6_Part1_MultiBackendImGuiCore()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 62) {
        if (!RunTest_Step62_Phase32_Step6_Part2_ViewportDescriptorBinding()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 63) {
        if (!RunTest_Step63_Phase32_Step6_Part3_EngineRHISwitcherAndEditorWiring()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 64) {
        if (!RunTest_Step64_Phase32_Step7_HeadlessRHIDeviceInitialization()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 65) {
        if (!RunTest_Step65_Phase32_Step7_MultithreadedCommandRecording()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 66) {
        if (!RunTest_Step66_Phase32_Step7_ValidationBarrierStateMachinePSOCacheSwapchain()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 67) {
        if (!RunTest_Step67_Phase33_Step1_CoreGeometryAndStreamer()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 68) {
        if (!RunTest_Step68_Phase33_Step1_TransformStreamingAndRHISceneRenderer()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 69) {
        if (!RunTest_Step69_Phase33_Step2_UnifiedHLSLShaderPipelineAndForwardPBR()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 70) {
        if (!RunTest_Step70_Phase33_Step2_MetallicRoughnessPBRPipeline()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 71) {
        if (!RunTest_Step71_Phase33_Step2_BindlessTexturesAndAnisotropicFiltering()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 72) {
        if (!RunTest_Step72_Phase33_Step2_ForwardPlusMultiLightGPUBuffer()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 73) {
        if (!RunTest_Step73_DirectionalCascadedShadowMapping()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 74) {
        if (!RunTest_Step74_ImageBasedLightingAndEnvironmentMaps()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 75) {
        if (!RunTest_Step75_ProceduralAtmosphericSkyboxPass()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 76) {
        if (!RunTest_Step76_HDRRenderTargetAndResizeSync()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 77) {
        if (!RunTest_Step77_ExposureAndToneMappingPass()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 78) {
        if (!RunTest_Step78_AntiAliasingPass()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 79) {
        if (!RunTest_Step79_GPUParticleVFXPass()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 80) {
        if (!RunTest_Step80_RHIPhysicsDebugLinePipeline()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 81) {
        if (!RunTest_Step81_PhysicsDebugSceneRendererIntegration()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 82) {
        if (!RunTest_Step82_GPUObjectIDPicking()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 83) {
        if (!RunTest_Step83_ViewportClickPickingIntegration()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 84) {
        if (!RunTest_Step84_TripleBackendSwitchingAndVisualParity()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 85) {
        if (!RunTest_Step85_Phase34_Step1_Part1_DecoupledAxleUI2DLibraryAndUITypes()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 86) {
        if (!RunTest_Step86_Phase34_Step1_Part2_Canvas2DBatchingAndSDFPipeline()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 87) {
        if (!RunTest_Step87_Phase34_Step2_MSDFFontAndTextLayout()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 88) {
        if (!RunTest_Step88_Phase34_Step3_DOMTreeAnd2PassLayoutEngine()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 89) {
        if (!RunTest_Step89_Phase34_Step4_DeclarativeUIParserAndHotReloading()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 90) {
        if (!RunTest_Step90_Phase34_Step5_DataBindingAndHitTest()) allPassed = false;
    }
    if (targetStep == 0 || targetStep == 91) {
        if (!RunTest_Step91_Phase34_Step6_RacingHUDFullIntegration()) allPassed = false;
    }

    if (Axle::JobSystem::IsInitialized()) {
        Axle::JobSystem::Shutdown();
    }
    Axle::ScriptHotReloadManager::Get().Shutdown();
    if (Axle::RHI::RHIDevice::Get().IsInitialized()) {
        Axle::RHI::RHIDevice::Get().Shutdown();
    }

    std::cout << "========================================================" << std::endl;
    if (allPassed) {
        std::cout << " [RESULT] >>> ALL REQUESTED TESTS PASSED (100%) <<<    " << std::endl;
    } else {
        std::cerr << " [RESULT] >>> SOME TESTS FAILED!                      " << std::endl;
    }
    std::cout << "========================================================\n" << std::endl;

    return allPassed ? 0 : 1;
}

