#pragma once
#include <imgui.h>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <string>
#include <algorithm>
#include <cmath>

namespace Axle::UIIcons {

    // --- Minimalist Vector Icon Drawing Helpers (Unity / Unreal Studio Aesthetic) ---

    inline void DrawTransformIcon(ImDrawList* dl, ImVec2 pos, float size) {
        ImVec2 o(pos.x + size * 0.28f, pos.y + size * 0.72f);
        // X-axis (Red)
        dl->AddLine(o, ImVec2(o.x + size * 0.58f, o.y), IM_COL32(235, 87, 87, 255), 1.5f);
        dl->AddCircleFilled(ImVec2(o.x + size * 0.58f, o.y), 1.6f, IM_COL32(235, 87, 87, 255));
        // Y-axis (Green)
        dl->AddLine(o, ImVec2(o.x, o.y - size * 0.58f), IM_COL32(46, 204, 113, 255), 1.5f);
        dl->AddCircleFilled(ImVec2(o.x, o.y - size * 0.58f), 1.6f, IM_COL32(46, 204, 113, 255));
        // Z-axis (Blue)
        dl->AddLine(o, ImVec2(o.x + size * 0.38f, o.y - size * 0.38f), IM_COL32(86, 182, 242, 255), 1.5f);
        dl->AddCircleFilled(ImVec2(o.x + size * 0.38f, o.y - size * 0.38f), 1.6f, IM_COL32(86, 182, 242, 255));
    }

    // --- Unity Standard Transport Controls (Centered Toolbar) ---
    inline void DrawPlayIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col = IM_COL32(230, 235, 245, 255)) {
        float h = size * 0.5f;
        float w = size * 0.44f;
        ImVec2 p0(center.x - w * 0.5f, center.y - h);
        ImVec2 p1(center.x + w * 0.65f, center.y);
        ImVec2 p2(center.x - w * 0.5f, center.y + h);
        dl->AddTriangleFilled(p0, p1, p2, col);
    }

    inline void DrawPauseIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col = IM_COL32(230, 235, 245, 255)) {
        float h = size * 0.48f;
        float barW = size * 0.16f;
        float gap = size * 0.12f;
        dl->AddRectFilled(ImVec2(center.x - gap - barW, center.y - h), ImVec2(center.x - gap, center.y + h), col, 1.0f);
        dl->AddRectFilled(ImVec2(center.x + gap, center.y - h), ImVec2(center.x + gap + barW, center.y + h), col, 1.0f);
    }

    inline void DrawStepIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col = IM_COL32(230, 235, 245, 255)) {
        float h = size * 0.45f;
        float w = size * 0.35f;
        ImVec2 p0(center.x - w, center.y - h);
        ImVec2 p1(center.x + w * 0.2f, center.y);
        ImVec2 p2(center.x - w, center.y + h);
        dl->AddTriangleFilled(p0, p1, p2, col);
        float barW = size * 0.14f;
        dl->AddRectFilled(ImVec2(center.x + w * 0.45f, center.y - h), ImVec2(center.x + w * 0.45f + barW, center.y + h), col, 0.8f);
    }

    inline void DrawStopIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col = IM_COL32(230, 235, 245, 255)) {
        float half = size * 0.38f;
        dl->AddRectFilled(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half), col, 1.5f);
    }

    // --- Unity Standard Transform Tools (Top-Left Segmented Capsule) ---
    inline void DrawToolSelectIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col = IM_COL32(220, 225, 235, 255)) {
        ImVec2 tip(center.x - size * 0.28f, center.y - size * 0.40f);
        ImVec2 bottom(center.x - size * 0.28f, center.y + size * 0.26f);
        ImVec2 notch(center.x - size * 0.04f, center.y + size * 0.08f);
        ImVec2 right(center.x + size * 0.28f, center.y - size * 0.02f);

        // Chevron pointer arrow head (two triangles to avoid concave polygon fan artifacts)
        dl->AddTriangleFilled(tip, bottom, notch, col);
        dl->AddTriangleFilled(tip, notch, right, col);

        // Arrow stem extending down-right
        ImVec2 stemStart(center.x + size * 0.02f, center.y + size * 0.10f);
        ImVec2 stemEnd(center.x + size * 0.30f, center.y + size * 0.38f);
        dl->AddLine(stemStart, stemEnd, col, size * 0.16f);
    }

    inline void DrawToolMoveIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col = IM_COL32(220, 225, 235, 255)) {
        float arm = size * 0.46f;
        float arr = size * 0.18f;

        // Central cross lines
        dl->AddLine(ImVec2(center.x - arm, center.y), ImVec2(center.x + arm, center.y), col, 1.5f);
        dl->AddLine(ImVec2(center.x, center.y - arm), ImVec2(center.x, center.y + arm), col, 1.5f);

        // Central solid anchor dot
        dl->AddCircleFilled(center, 1.5f, col);

        // 4 Arrowheads (North, South, West, East)
        dl->AddTriangleFilled(ImVec2(center.x, center.y - arm),
                              ImVec2(center.x - arr, center.y - arm + arr * 1.2f),
                              ImVec2(center.x + arr, center.y - arm + arr * 1.2f), col);
        dl->AddTriangleFilled(ImVec2(center.x, center.y + arm),
                              ImVec2(center.x - arr, center.y + arm - arr * 1.2f),
                              ImVec2(center.x + arr, center.y + arm - arr * 1.2f), col);
        dl->AddTriangleFilled(ImVec2(center.x - arm, center.y),
                              ImVec2(center.x - arm + arr * 1.2f, center.y - arr),
                              ImVec2(center.x - arm + arr * 1.2f, center.y + arr), col);
        dl->AddTriangleFilled(ImVec2(center.x + arm, center.y),
                              ImVec2(center.x + arm - arr * 1.2f, center.y - arr),
                              ImVec2(center.x + arm - arr * 1.2f, center.y + arr), col);
    }

    inline void DrawToolRotateIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col = IM_COL32(220, 225, 235, 255)) {
        float r = size * 0.38f;
        // Top arc (clockwise)
        dl->PathArcTo(center, r, -2.6f, -0.3f, 12);
        dl->PathStroke(col, 0, 1.5f);
        // Tangent arrowhead at end of top arc
        ImVec2 tip1(center.x + r * cosf(-0.3f), center.y + r * sinf(-0.3f));
        float tLen = size * 0.22f;
        float tWidth = size * 0.16f;
        ImVec2 tDir(0.295f, 0.955f);
        ImVec2 nDir(-0.955f, 0.295f);
        ImVec2 base1(tip1.x - tDir.x * tLen, tip1.y - tDir.y * tLen);
        dl->AddTriangleFilled(tip1,
                              ImVec2(base1.x - nDir.x * tWidth, base1.y - nDir.y * tWidth),
                              ImVec2(base1.x + nDir.x * tWidth, base1.y + nDir.y * tWidth), col);

        // Bottom arc (clockwise)
        dl->PathArcTo(center, r, 0.55f, 2.85f, 12);
        dl->PathStroke(col, 0, 1.5f);
        // Tangent arrowhead at end of bottom arc
        ImVec2 tip2(center.x + r * cosf(2.85f), center.y + r * sinf(2.85f));
        ImVec2 tDir2(-0.295f, -0.955f);
        ImVec2 nDir2(0.955f, -0.295f);
        ImVec2 base2(tip2.x - tDir2.x * tLen, tip2.y - tDir2.y * tLen);
        dl->AddTriangleFilled(tip2,
                              ImVec2(base2.x - nDir2.x * tWidth, base2.y - nDir2.y * tWidth),
                              ImVec2(base2.x + nDir2.x * tWidth, base2.y + nDir2.y * tWidth), col);
    }

    inline void DrawToolScaleIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col = IM_COL32(220, 225, 235, 255)) {
        float arm = size * 0.40f;
        ImVec2 p0(center.x - arm, center.y + arm);
        ImVec2 p1(center.x + arm, center.y - arm);
        dl->AddLine(p0, p1, col, 1.5f);

        float b1 = size * 0.14f;
        dl->AddRectFilled(ImVec2(p0.x - b1, p0.y - b1), ImVec2(p0.x + b1, p0.y + b1), col, 0.5f);

        float b2 = size * 0.22f;
        dl->AddRectFilled(ImVec2(p1.x - b2, p1.y - b2), ImVec2(p1.x + b2, p1.y + b2), col, 0.8f);
    }

    inline void DrawToolRectIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col = IM_COL32(220, 225, 235, 255)) {
        float half = size * 0.36f;
        ImVec2 rMin(center.x - half, center.y - half);
        ImVec2 rMax(center.x + half, center.y + half);
        dl->AddRect(rMin, rMax, col, 0.5f, 0, 1.3f);

        float h = size * 0.11f;
        auto drawBox = [&](ImVec2 p) {
            dl->AddRectFilled(ImVec2(p.x - h, p.y - h), ImVec2(p.x + h, p.y + h), col, 0.5f);
        };
        drawBox(rMin);
        drawBox(ImVec2(rMax.x, rMin.y));
        drawBox(ImVec2(rMin.x, rMax.y));
        drawBox(rMax);
    }

    inline void DrawCameraIcon(ImDrawList* dl, ImVec2 pos, float size) {
        ImU32 col = IM_COL32(65, 175, 195, 255);
        // Camera Body
        ImVec2 bMin(pos.x + 1.0f, pos.y + size * 0.25f);
        ImVec2 bMax(pos.x + size * 0.65f, pos.y + size * 0.85f);
        dl->AddRectFilled(bMin, bMax, col, 1.5f);
        // Viewfinder
        dl->AddRectFilled(ImVec2(pos.x + 2.5f, pos.y + size * 0.10f), ImVec2(pos.x + size * 0.45f, bMin.y), col, 1.0f);
        // Lens Cone
        ImVec2 p0(bMax.x, pos.y + size * 0.55f);
        ImVec2 p1(pos.x + size * 0.98f, pos.y + size * 0.25f);
        ImVec2 p2(pos.x + size * 0.98f, pos.y + size * 0.85f);
        dl->AddTriangleFilled(p0, p1, p2, col);
        // Lens circle inside
        dl->AddCircleFilled(ImVec2((bMin.x + bMax.x) * 0.5f, (bMin.y + bMax.y) * 0.5f), size * 0.16f, IM_COL32(235, 245, 255, 240));
    }

    inline void DrawLightIcon(ImDrawList* dl, ImVec2 pos, float size) {
        ImU32 col = IM_COL32(245, 195, 65, 255);
        ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);
        float r = size * 0.24f;
        dl->AddCircleFilled(center, r, col);

        // Radiant Beams
        float d1 = size * 0.32f;
        float d2 = size * 0.48f;
        dl->AddLine(ImVec2(center.x, center.y - d1), ImVec2(center.x, center.y - d2), col, 1.3f);
        dl->AddLine(ImVec2(center.x, center.y + d1), ImVec2(center.x, center.y + d2), col, 1.3f);
        dl->AddLine(ImVec2(center.x - d1, center.y), ImVec2(center.x - d2, center.y), col, 1.3f);
        dl->AddLine(ImVec2(center.x + d1, center.y), ImVec2(center.x + d2, center.y), col, 1.3f);

        float diag1 = d1 * 0.707f;
        float diag2 = d2 * 0.707f;
        dl->AddLine(ImVec2(center.x + diag1, center.y - diag1), ImVec2(center.x + diag2, center.y - diag2), col, 1.1f);
        dl->AddLine(ImVec2(center.x - diag1, center.y - diag1), ImVec2(center.x - diag2, center.y - diag2), col, 1.1f);
        dl->AddLine(ImVec2(center.x + diag1, center.y + diag1), ImVec2(center.x + diag2, center.y + diag2), col, 1.1f);
        dl->AddLine(ImVec2(center.x - diag1, center.y + diag1), ImVec2(center.x - diag2, center.y + diag2), col, 1.1f);
    }

    inline void DrawMeshIcon(ImDrawList* dl, ImVec2 pos, float size) {
        // Isometric 3D Cube
        ImVec2 pTop(pos.x + size * 0.5f, pos.y + size * 0.12f);
        ImVec2 pMid(pos.x + size * 0.5f, pos.y + size * 0.55f);
        ImVec2 pBot(pos.x + size * 0.5f, pos.y + size * 0.95f);
        ImVec2 pLeft(pos.x + size * 0.12f, pos.y + size * 0.35f);
        ImVec2 pRight(pos.x + size * 0.88f, pos.y + size * 0.35f);
        ImVec2 pLeftB(pos.x + size * 0.12f, pos.y + size * 0.75f);
        ImVec2 pRightB(pos.x + size * 0.88f, pos.y + size * 0.75f);

        // Top Face (Light)
        dl->AddQuadFilled(pTop, pRight, pMid, pLeft, IM_COL32(170, 225, 145, 255));
        // Left Face (Mid tone)
        dl->AddQuadFilled(pLeft, pMid, pBot, pLeftB, IM_COL32(120, 185, 95, 255));
        // Right Face (Shadow tone)
        dl->AddQuadFilled(pMid, pRight, pRightB, pBot, IM_COL32(80, 145, 65, 255));
        // Outlines
        ImU32 wire = IM_COL32(230, 255, 220, 180);
        dl->AddLine(pMid, pTop, wire, 1.0f);
        dl->AddLine(pMid, pBot, wire, 1.0f);
        dl->AddLine(pMid, pLeft, wire, 1.0f);
        dl->AddLine(pMid, pRight, wire, 1.0f);
    }

    inline void DrawAudioIcon(ImDrawList* dl, ImVec2 pos, float size) {
        ImU32 col = IM_COL32(185, 120, 235, 255);
        // Speaker base & cone
        ImVec2 b0(pos.x + size * 0.12f, pos.y + size * 0.38f);
        ImVec2 b1(pos.x + size * 0.35f, pos.y + size * 0.62f);
        dl->AddRectFilled(b0, b1, col, 1.0f);

        ImVec2 p0(b1.x, b0.y);
        ImVec2 p1(pos.x + size * 0.62f, pos.y + size * 0.15f);
        ImVec2 p2(pos.x + size * 0.62f, pos.y + size * 0.85f);
        ImVec2 p3(b1.x, b1.y);
        dl->AddQuadFilled(p0, p1, p2, p3, col);

        // Sound Waves
        ImVec2 arcCenter(pos.x + size * 0.55f, pos.y + size * 0.5f);
        dl->PathArcTo(arcCenter, size * 0.24f, -0.78f, 0.78f);
        dl->PathStroke(col, 0, 1.4f);
        dl->PathArcTo(arcCenter, size * 0.40f, -0.78f, 0.78f);
        dl->PathStroke(col, 0, 1.4f);
    }

    inline void DrawVehicleIcon(ImDrawList* dl, ImVec2 pos, float size) {
        ImU32 bodyCol = IM_COL32(235, 85, 85, 255);
        // Chassis Lower
        ImVec2 cMin(pos.x + size * 0.08f, pos.y + size * 0.50f);
        ImVec2 cMax(pos.x + size * 0.92f, pos.y + size * 0.78f);
        dl->AddRectFilled(cMin, cMax, bodyCol, 1.5f);
        // Cabin / Roof
        ImVec2 r0(pos.x + size * 0.28f, cMin.y);
        ImVec2 r1(pos.x + size * 0.42f, pos.y + size * 0.25f);
        ImVec2 r2(pos.x + size * 0.70f, pos.y + size * 0.25f);
        ImVec2 r3(pos.x + size * 0.82f, cMin.y);
        dl->AddQuadFilled(r0, r1, r2, r3, bodyCol);
        // Wheels
        ImU32 wheelCol = IM_COL32(40, 40, 45, 255);
        ImU32 rimCol = IM_COL32(200, 210, 225, 255);
        ImVec2 w1(pos.x + size * 0.28f, pos.y + size * 0.80f);
        ImVec2 w2(pos.x + size * 0.72f, pos.y + size * 0.80f);
        float wRadius = size * 0.16f;
        dl->AddCircleFilled(w1, wRadius, wheelCol);
        dl->AddCircleFilled(w2, wRadius, wheelCol);
        dl->AddCircleFilled(w1, wRadius * 0.45f, rimCol);
        dl->AddCircleFilled(w2, wRadius * 0.45f, rimCol);
    }

    inline void DrawUIIcon(ImDrawList* dl, ImVec2 pos, float size) {
        ImU32 col = IM_COL32(75, 165, 245, 255);
        // Outer Rectangle
        ImVec2 rMin(pos.x + size * 0.15f, pos.y + size * 0.15f);
        ImVec2 rMax(pos.x + size * 0.85f, pos.y + size * 0.85f);
        dl->AddRect(rMin, rMax, col, 1.5f, 0, 1.2f);
        // 4 Corner Anchor Dots
        float dR = 1.3f;
        dl->AddCircleFilled(rMin, dR, col);
        dl->AddCircleFilled(ImVec2(rMax.x, rMin.y), dR, col);
        dl->AddCircleFilled(ImVec2(rMin.x, rMax.y), dR, col);
        dl->AddCircleFilled(rMax, dR, col);
        // Center Pivot Crosshair
        ImVec2 c(pos.x + size * 0.5f, pos.y + size * 0.5f);
        dl->AddLine(ImVec2(c.x - 2.0f, c.y), ImVec2(c.x + 2.0f, c.y), col, 1.0f);
        dl->AddLine(ImVec2(c.x, c.y - 2.0f), ImVec2(c.x, c.y + 2.0f), col, 1.0f);
    }

    inline void DrawVFXIcon(ImDrawList* dl, ImVec2 pos, float size) {
        ImU32 col = IM_COL32(245, 175, 45, 255);
        ImVec2 c(pos.x + size * 0.5f, pos.y + size * 0.5f);
        // 4-point Diamond Sparkle Star
        float rOut = size * 0.42f;
        float rIn  = size * 0.12f;
        ImVec2 pts[8] = {
            ImVec2(c.x, c.y - rOut),
            ImVec2(c.x + rIn, c.y - rIn),
            ImVec2(c.x + rOut, c.y),
            ImVec2(c.x + rIn, c.y + rIn),
            ImVec2(c.x, c.y + rOut),
            ImVec2(c.x - rIn, c.y + rIn),
            ImVec2(c.x - rOut, c.y),
            ImVec2(c.x - rIn, c.y - rIn)
        };
        dl->AddPolyline(pts, 8, col, ImDrawFlags_Closed, 1.2f);
        dl->AddCircleFilled(c, rIn * 0.8f, col);
        // Embers
        dl->AddCircleFilled(ImVec2(pos.x + size * 0.20f, pos.y + size * 0.25f), 1.0f, col);
        dl->AddCircleFilled(ImVec2(pos.x + size * 0.82f, pos.y + size * 0.78f), 1.0f, col);
    }

    inline void DrawPhysicsIcon(ImDrawList* dl, ImVec2 pos, float size) {
        ImU32 col = IM_COL32(40, 205, 185, 255);
        // Dynamic Physics Body (Circle with velocity arrow)
        ImVec2 c(pos.x + size * 0.45f, pos.y + size * 0.45f);
        float r = size * 0.32f;
        dl->AddCircle(c, r, col, 0, 1.3f);
        dl->AddCircleFilled(c, r * 0.45f, col);
        // Impulse arrow
        ImVec2 a0(c.x + r * 0.5f, c.y + r * 0.5f);
        ImVec2 a1(pos.x + size * 0.90f, pos.y + size * 0.90f);
        dl->AddLine(a0, a1, col, 1.4f);
    }

    inline void DrawScriptIcon(ImDrawList* dl, ImVec2 pos, float size) {
        ImU32 sheetCol = IM_COL32(230, 130, 45, 255);
        ImVec2 sMin(pos.x + size * 0.20f, pos.y + size * 0.12f);
        ImVec2 sMax(pos.x + size * 0.80f, pos.y + size * 0.88f);
        dl->AddRectFilled(sMin, sMax, sheetCol, 1.2f);
        // Folded Corner
        dl->AddTriangleFilled(ImVec2(sMax.x - size * 0.25f, sMin.y), sMax, ImVec2(sMax.x, sMin.y + size * 0.25f), IM_COL32(35, 35, 35, 255));
        // Code lines
        ImU32 lineCol = IM_COL32(255, 255, 255, 220);
        float lineX0 = sMin.x + size * 0.12f;
        float lineX1 = sMax.x - size * 0.12f;
        dl->AddLine(ImVec2(lineX0, pos.y + size * 0.42f), ImVec2(lineX1 - size * 0.12f, pos.y + size * 0.42f), lineCol, 1.1f);
        dl->AddLine(ImVec2(lineX0, pos.y + size * 0.58f), ImVec2(lineX1, pos.y + size * 0.58f), lineCol, 1.1f);
        dl->AddLine(ImVec2(lineX0, pos.y + size * 0.74f), ImVec2(lineX0 + size * 0.30f, pos.y + size * 0.74f), lineCol, 1.1f);
    }

    inline void DrawDefaultEntityIcon(ImDrawList* dl, ImVec2 pos, float size) {
        ImU32 col = IM_COL32(165, 175, 185, 240);
        // Modern Unity GameObject minimalist diamond ring
        ImVec2 c(pos.x + size * 0.5f, pos.y + size * 0.5f);
        float r = size * 0.38f;
        ImVec2 pts[4] = {
            ImVec2(c.x, c.y - r),
            ImVec2(c.x + r, c.y),
            ImVec2(c.x, c.y + r),
            ImVec2(c.x - r, c.y)
        };
        dl->AddPolyline(pts, 4, col, ImDrawFlags_Closed, 1.3f);
        dl->AddCircleFilled(c, 1.2f, col);
    }

    // --- Entity Contextual Icon Resolver (Hierarchy Tree Node) ---
    inline void DrawEntityVectorIcon(ImDrawList* dl, ImVec2 pos, float size, Entity entity) {
        if (!dl || !entity) {
            DrawDefaultEntityIcon(dl, pos, size);
            return;
        }

        if (entity.HasComponent<VehicleComponent>()) {
            DrawVehicleIcon(dl, pos, size);
        } else if (entity.HasComponent<CameraComponent>()) {
            DrawCameraIcon(dl, pos, size);
        } else if (entity.HasComponent<DirectionalLightComponent>() || entity.HasComponent<PointLightComponent>() || entity.HasComponent<SpotLightComponent>()) {
            DrawLightIcon(dl, pos, size);
        } else if (entity.HasComponent<UICanvasComponent>()) {
            DrawUIIcon(dl, pos, size);
        } else if (entity.HasComponent<AudioSourceComponent>()) {
            DrawAudioIcon(dl, pos, size);
        } else if (entity.HasComponent<MeshInstance3D>()) {
            DrawMeshIcon(dl, pos, size);
        } else if (entity.HasComponent<ParticleEmitterComponent>()) {
            DrawVFXIcon(dl, pos, size);
        } else if (entity.HasComponent<RigidBodyComponent>()) {
            DrawPhysicsIcon(dl, pos, size);
        } else if (entity.HasComponent<NativeScriptComponent>()) {
            DrawScriptIcon(dl, pos, size);
        } else {
            DrawDefaultEntityIcon(dl, pos, size);
        }
    }

    // --- Component Contextual Icon Resolver (Inspector Headers) ---
    inline void DrawComponentVectorIcon(ImDrawList* dl, ImVec2 pos, float size, const std::string& name) {
        if (!dl) return;

        if (name == "Transform") {
            DrawTransformIcon(dl, pos, size);
        } else if (name.find("Camera") != std::string::npos) {
            DrawCameraIcon(dl, pos, size);
        } else if (name.find("Light") != std::string::npos) {
            DrawLightIcon(dl, pos, size);
        } else if (name.find("Mesh") != std::string::npos) {
            DrawMeshIcon(dl, pos, size);
        } else if (name.find("Audio") != std::string::npos) {
            DrawAudioIcon(dl, pos, size);
        } else if (name.find("Vehicle") != std::string::npos) {
            DrawVehicleIcon(dl, pos, size);
        } else if (name.find("UI") != std::string::npos || name.find("Canvas") != std::string::npos || name.find("RectTransform") != std::string::npos) {
            DrawUIIcon(dl, pos, size);
        } else if (name.find("Particle") != std::string::npos || name.find("VFX") != std::string::npos) {
            DrawVFXIcon(dl, pos, size);
        } else if (name.find("Physics") != std::string::npos || name.find("Body") != std::string::npos || name.find("Collision") != std::string::npos) {
            DrawPhysicsIcon(dl, pos, size);
        } else if (name.find("Script") != std::string::npos) {
            DrawScriptIcon(dl, pos, size);
        } else {
            DrawDefaultEntityIcon(dl, pos, size);
        }
    }

} // namespace Axle::UIIcons
