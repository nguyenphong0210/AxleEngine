#pragma once
#include <Axle/Types.hpp>
#include <string_view>

namespace Axle {

    enum class SimulationPhase : uint8_t {
        None = 0,
        FrameScripts,          // Frame gameplay OnUpdate (simulation-frame dt)
        FixedControls,         // Apply tick commands, OnFixedUpdate, pre-physics controller updates
        Physics,               // PhysicsScene::Step(fixedDt)
        PostStepSync,          // RigidBody & Character transform synchronization, telemetry readback
        AnimationVFX,          // Animator & VFX particle updates, intermediate world transform completion
        LatePresentation       // OnLateUpdate, final world transforms, spatial audio sync, camera/UI
    };

    inline std::string_view SimulationPhaseToString(SimulationPhase phase) {
        switch (phase) {
            case SimulationPhase::FrameScripts:     return "FrameScripts";
            case SimulationPhase::FixedControls:    return "FixedControls";
            case SimulationPhase::Physics:          return "Physics";
            case SimulationPhase::PostStepSync:     return "PostStepSync";
            case SimulationPhase::AnimationVFX:     return "AnimationVFX";
            case SimulationPhase::LatePresentation: return "LatePresentation";
            default:                                return "None";
        }
    }

}
