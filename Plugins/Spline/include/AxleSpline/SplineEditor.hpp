#pragma once
#include <AxleSpline/SplineComponent.hpp>
#include <Axle/Entity.hpp>
#include <Axle/PhysicsScene.hpp>
#include <glm/glm.hpp>

namespace Axle::Spline {

    class SplineEditor {
    public:
        // Draw Inspector property editor for SplineComponent
        static void DrawInspector(Entity entity, PhysicsScene* physicsScene = nullptr);

        // Draw interactive Viewport 3D Spline ribbon and Node manipulation gizmos
        static void DrawViewportGizmo(
            Entity entity,
            const glm::mat4& viewMatrix,
            const glm::mat4& projectionMatrix,
            const glm::vec2& viewportMin,
            const glm::vec2& viewportMax,
            int gizmoOperation,
            PhysicsScene* physicsScene = nullptr
        );

        // Active node selected in the editor for gizmo manipulation
        static int GetSelectedNodeIndex() { return s_SelectedNodeIndex; }
        static void SetSelectedNodeIndex(int index) { s_SelectedNodeIndex = index; }

    private:
        static int s_SelectedNodeIndex;
    };

}
