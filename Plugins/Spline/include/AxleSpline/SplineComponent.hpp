#pragma once
#include <AxleSpline/Curve3D.hpp>
#include <Axle/Types.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/PhysicsScene.hpp>
#include <memory>
#include <vector>

namespace Axle::Spline {

    struct SplineComponent {
        Curve3D Curve;

        // Track Extrusion Profile
        float TrackWidth = 8.0f;           // Total asphalt road width
        float StepDistance = 1.0f;         // Distance between cross-sections along curve
        float TextureRepeatV = 0.25f;      // V texture coordinate repetition per meter

        // Curbs / Rumble Strips
        bool GenerateCurbs = true;
        float CurbWidth = 0.6f;            // Width of curb border
        float CurbHeight = 0.15f;          // Raised curb elevation

        // Guardrails / Safety Barriers
        bool GenerateGuardrails = true;
        float GuardrailHeight = 0.9f;      // Height of metal barrier
        float GuardrailThickness = 0.15f;  // Thickness of barrier post/rail

        // Physics Collider
        bool GeneratePhysics = true;
        float Friction = 0.85f;            // High-grip racing asphalt friction
        float Restitution = 0.05f;         // Low bounce coefficient
        BodyID RuntimeBodyID;

        // Render Mesh Cache
        std::shared_ptr<VertexArray> TrackVAO = nullptr;
        u32 VertexCount = 0;
        u32 TriangleCount = 0;
        bool Dirty = true;

        SplineComponent() = default;
        explicit SplineComponent(SplineType type, bool loop = false)
            : Curve(type, loop) {}
    };

}
