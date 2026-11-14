#pragma once

#include <Axle/RHISceneRenderer.hpp>

namespace Axle {
    // Borrowed inputs for synchronous pass execution. No pass callback may
    // outlive this call; GPU dependencies are retained by the command journal.
    struct RHISceneRenderer::FramePassInputs {
        const RenderWorld& world;
        const ReflectionFrame& reflectionFrame;
        const PerspectiveCamera& camera;
        const SceneRenderSettings& settings;
        PhysicsScene* physicsScene;
        const std::shared_ptr<RHI::RHIViewTargets>& viewTargets;
        nri::Texture* colorTarget;
        nri::Descriptor* colorRTV;
        uint32_t width;
        uint32_t height;
        bool useMSAA;
        bool useToneMap;
        bool useFXAA;
        uint8_t activeSampleCount;
        nri::Texture* sceneTargetTexture;
        nri::Descriptor* sceneTargetRTV;
        nri::Texture* sceneDepthTexture;
        nri::Descriptor* sceneDepthDSV;
        nri::Format sceneTargetFormat;
        nri::Pipeline* activePipeline;
        const glm::mat4& projZO;
        const glm::mat4& viewProjZO;
        float jitterX;
        float jitterY;
        bool giUpdateScheduled;
        RenderPassCapabilities capabilities;
    };
}
