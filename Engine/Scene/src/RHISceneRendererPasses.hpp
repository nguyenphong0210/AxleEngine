#pragma once

#include <Axle/RHISceneRenderer.hpp>

namespace Axle {
    // Borrowed inputs for synchronous pass execution. No pass callback may
    // outlive this call; GPU dependencies are retained by the command journal.
    struct RHISceneRenderer::FramePassInputs {
        const RenderWorld& world;
        const ReflectionFrame& reflectionFrame;
        PhysicsScene* physicsScene;
        nri::Texture* colorTarget;
        nri::Descriptor* colorRTV;
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
        const glm::mat4& unjitteredViewProjZO;
        const GIFrame& giFrame;
        RenderPassCapabilities capabilities;
    };
}
