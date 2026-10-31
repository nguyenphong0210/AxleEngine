#include <Axle/TextureDescriptor.hpp>
#include <Axle/ParticleEmitterSerializer.hpp>
#include <Axle/ContentBrowserPanel.hpp>
#include <Axle/AssetThumbnailManager.hpp>
#include <Axle/AssetPreviewPanel.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/AssetTypes.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/PhysicsMaterial.hpp>
#include <Axle/AssetDSL.hpp>
#include <Axle/Log.hpp>
#include <imgui.h>
#include <algorithm>
#include <vector>
#include <cctype>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace Axle {

    static bool CaseInsensitiveFind(const std::string& str, const std::string& query) {
        if (query.empty()) return true;
        auto it = std::search(
            str.begin(), str.end(),
            query.begin(), query.end(),
            [](char c1, char c2) { return std::tolower(c1) == std::tolower(c2); }
        );
        return it != str.end();
    }

    enum class CBAssetKind {
        Folder,
        Assembly,
        Model3D,
        Texture,
        Shader,
        Scene,
        Project,
        Material,
        PhysicsMaterial,
        Script,
        Audio,
        Document,
        Other
    };

    struct AssetVisualMeta {
        CBAssetKind Kind;
        const char* TypeLabel;
        const char* Tag;
        ImU32 AccentColor;
    };

    static AssetVisualMeta GetAssetVisualMeta(const std::filesystem::directory_entry& entry) {
        if (entry.is_directory()) {
            return { CBAssetKind::Folder, "Folder", "DIR", IM_COL32(245, 185, 45, 255) };
        }

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });

        if (ext == ".axlemesh") {
            return { CBAssetKind::Model3D, "3D Mesh", "MSH", IM_COL32(65, 195, 255, 255) };
        }
        if (ext == ".axle") {
            return { CBAssetKind::Assembly, "Assembly Prefab", "ASY", IM_COL32(255, 175, 45, 255) };
        }
        if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
            return { CBAssetKind::Model3D, "3D Mesh", "3D", IM_COL32(65, 195, 255, 255) };
        }
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
            return { CBAssetKind::Texture, "Texture", "IMG", IM_COL32(46, 204, 113, 255) };
        }
        if (ext == ".vert" || ext == ".frag" || ext == ".glsl" || ext == ".comp") {
            return { CBAssetKind::Shader, "Shader", "SHD", IM_COL32(205, 105, 255, 255) };
        }
        if (ext == ".axlescene" || ext == ".scene") {
            return { CBAssetKind::Scene, "Axle Scene", "SCN", IM_COL32(255, 85, 95, 255) };
        }
        if (ext == ".axleproj") {
            return { CBAssetKind::Project, "Axle Project", "PRJ", IM_COL32(0, 210, 180, 255) };
        }
        if (ext == ".axlemat" || ext == ".amat" || ext == ".mat") {
            return { CBAssetKind::Material, "Material", "MAT", IM_COL32(255, 135, 45, 255) };
        }
        if (ext == ".physmat") {
            return { CBAssetKind::PhysicsMaterial, "Physics Material", "PHY", IM_COL32(255, 165, 50, 255) };
        }
        if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".lua" || ext == ".py") {
            return { CBAssetKind::Script, "Source Code", "CODE", IM_COL32(75, 160, 245, 255) };
        }
        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
            return { CBAssetKind::Audio, "Audio Clip", "AUD", IM_COL32(255, 95, 160, 255) };
        }
        if (ext == ".axleui") {
            return { CBAssetKind::Document, "UI Canvas", "UI", IM_COL32(56, 189, 248, 255) };
        }
        if (ext == ".axleasset") {
            auto dslKind = AssetDSL::PeekAssetKindFromFile(entry.path().string());
            switch (dslKind) {
                case Axle::AssetKind::Material:
                    return { CBAssetKind::Material, "Material Asset", "MAT", IM_COL32(255, 135, 45, 255) };
                case Axle::AssetKind::Texture:
                    return { CBAssetKind::Texture, "Texture Asset", "TEX", IM_COL32(46, 204, 113, 255) };
                case Axle::AssetKind::Canvas:
                    return { CBAssetKind::Document, "UI Canvas", "UI", IM_COL32(56, 189, 248, 255) };
                case Axle::AssetKind::ParticleEmitter:
                    return { CBAssetKind::Other, "Particle VFX", "VFX", IM_COL32(255, 75, 75, 255) };
                case Axle::AssetKind::Mesh:
                    return { CBAssetKind::Model3D, "3D Mesh", "MSH", IM_COL32(65, 195, 255, 255) };
                case Axle::AssetKind::Terrain:
                    return { CBAssetKind::Other, "Terrain", "TRN", IM_COL32(34, 197, 94, 255) };
                case Axle::AssetKind::PhysicsMaterial:
                    return { CBAssetKind::PhysicsMaterial, "Physics Material", "PHY", IM_COL32(255, 165, 50, 255) };
                default:
                    return { CBAssetKind::Other, "Axle Asset", "AST", IM_COL32(180, 180, 180, 255) };
            }
        }
        if (ext == ".md" || ext == ".txt" || ext == ".json" || ext == ".yaml") {
            return { CBAssetKind::Document, "Document", "DOC", IM_COL32(165, 180, 200, 255) };
        }

        return { CBAssetKind::Other, "File", "FILE", IM_COL32(140, 150, 165, 255) };
    }

    // -----------------------------------------------------------------------------
    // Procedural Vector Icon Drawing Routines (Crisp, Resolution-Independent)
    // -----------------------------------------------------------------------------

    static void DrawFolderIcon(ImDrawList* drawList, ImVec2 center, float size) {
        float w = size * 1.45f;
        float h = size * 1.05f;
        float halfW = w * 0.5f;
        float halfH = h * 0.5f;

        ImU32 colTab         = IM_COL32(235, 170, 35, 255);
        ImU32 colBack        = IM_COL32(215, 150, 25, 255);
        ImU32 colPaper       = IM_COL32(245, 248, 255, 230);
        ImU32 colPaperLines  = IM_COL32(180, 195, 215, 180);
        ImU32 colFront       = IM_COL32(250, 190, 50, 255);
        ImU32 colFrontEdge   = IM_COL32(255, 220, 105, 255);
        ImU32 colFrontBorder = IM_COL32(190, 130, 20, 220);

        // 1. Top tab
        ImVec2 tabMin = ImVec2(center.x - halfW, center.y - halfH - size * 0.22f);
        ImVec2 tabMax = ImVec2(center.x - halfW * 0.15f, center.y - halfH + 2.0f);
        drawList->AddRectFilled(tabMin, tabMax, colTab, 3.0f, ImDrawFlags_RoundTop);

        // 2. Back folder body
        ImVec2 backMin = ImVec2(center.x - halfW, center.y - halfH);
        ImVec2 backMax = ImVec2(center.x + halfW, center.y + halfH);
        drawList->AddRectFilled(backMin, backMax, colBack, 4.0f);

        // 3. Document paper sheet sticking out
        ImVec2 paperMin = ImVec2(center.x - halfW * 0.65f, center.y - halfH - size * 0.12f);
        ImVec2 paperMax = ImVec2(center.x + halfW * 0.70f, center.y + halfH * 0.15f);
        drawList->AddRectFilled(paperMin, paperMax, colPaper, 2.0f);
        float lineY1 = paperMin.y + size * 0.10f;
        float lineY2 = paperMin.y + size * 0.20f;
        drawList->AddLine(ImVec2(paperMin.x + 3.0f, lineY1), ImVec2(paperMax.x - 3.0f, lineY1), colPaperLines, 1.2f);
        drawList->AddLine(ImVec2(paperMin.x + 3.0f, lineY2), ImVec2(paperMax.x - 6.0f, lineY2), colPaperLines, 1.2f);

        // 4. Front flap cover
        ImVec2 frontMin = ImVec2(center.x - halfW, center.y - halfH * 0.15f);
        ImVec2 frontMax = ImVec2(center.x + halfW, center.y + halfH);
        drawList->AddRectFilled(frontMin, frontMax, colFront, 4.0f, ImDrawFlags_RoundBottom);
        drawList->AddLine(ImVec2(frontMin.x + 1.0f, frontMin.y), ImVec2(frontMax.x - 1.0f, frontMin.y), colFrontEdge, 1.8f);
        drawList->AddRect(frontMin, frontMax, colFrontBorder, 4.0f, ImDrawFlags_RoundBottom, 1.0f);
    }

    static void DrawModel3DIcon(ImDrawList* drawList, ImVec2 center, float size) {
        float r = size * 0.85f;
        float cos30 = 0.866025f;
        float sin30 = 0.5f;

        ImVec2 V0  = center;
        ImVec2 VT  = ImVec2(center.x, center.y - r);
        ImVec2 VTL = ImVec2(center.x - r * cos30, center.y - r * sin30);
        ImVec2 VTR = ImVec2(center.x + r * cos30, center.y - r * sin30);
        ImVec2 VBL = ImVec2(center.x - r * cos30, center.y + r * sin30);
        ImVec2 VBR = ImVec2(center.x + r * cos30, center.y + r * sin30);
        ImVec2 VB  = ImVec2(center.x, center.y + r);

        // Faces with isometric lighting
        ImU32 colTop   = IM_COL32(85, 215, 255, 255);  // Lit cyan
        ImU32 colLeft  = IM_COL32(40, 145, 230, 255);  // Mid-tone blue
        ImU32 colRight = IM_COL32(22, 90, 180, 255);   // Shadow navy

        drawList->AddQuadFilled(VT, VTR, V0, VTL, colTop);
        drawList->AddQuadFilled(VTL, V0, VB, VBL, colLeft);
        drawList->AddQuadFilled(V0, VTR, VBR, VB, colRight);

        // Inner edges
        ImU32 colEdgeInner = IM_COL32(200, 245, 255, 255);
        drawList->AddLine(V0, VT, colEdgeInner, 1.5f);
        drawList->AddLine(V0, VBL, colEdgeInner, 1.5f);
        drawList->AddLine(V0, VBR, colEdgeInner, 1.5f);

        // Outer contour
        const ImVec2 outerPts[6] = { VT, VTR, VBR, VB, VBL, VTL };
        drawList->AddPolyline(outerPts, 6, IM_COL32(140, 235, 255, 255), ImDrawFlags_Closed, 1.5f);

        // Glowing vertices
        ImU32 colDot = IM_COL32(255, 255, 255, 255);
        drawList->AddCircleFilled(V0, 1.8f, colDot);
        drawList->AddCircleFilled(VT, 1.8f, colDot);
        drawList->AddCircleFilled(VTL, 1.8f, colDot);
        drawList->AddCircleFilled(VTR, 1.8f, colDot);
        drawList->AddCircleFilled(VB, 1.8f, colDot);
        drawList->AddCircleFilled(VBL, 1.8f, colDot);
        drawList->AddCircleFilled(VBR, 1.8f, colDot);
    }

    static void DrawTexturePictureIcon(ImDrawList* drawList, ImVec2 center, float size) {
        float w = size * 1.40f;
        float h = size * 1.05f;
        float halfW = w * 0.5f;
        float halfH = h * 0.5f;

        ImVec2 fMin = ImVec2(center.x - halfW, center.y - halfH);
        ImVec2 fMax = ImVec2(center.x + halfW, center.y + halfH);

        // Canvas frame
        drawList->AddRectFilled(fMin, fMax, IM_COL32(18, 25, 36, 255), 4.0f);
        drawList->AddRect(fMin, fMax, IM_COL32(60, 95, 130, 240), 4.0f, 0, 1.5f);

        // Clipped landscape
        drawList->PushClipRect(ImVec2(fMin.x + 1.5f, fMin.y + 1.5f), ImVec2(fMax.x - 1.5f, fMax.y - 1.5f), true);

        // Sun
        ImVec2 sunCenter = ImVec2(center.x + halfW * 0.38f, center.y - halfH * 0.28f);
        drawList->AddCircleFilled(sunCenter, size * 0.22f, IM_COL32(255, 210, 65, 255));

        // Distant mountain
        ImVec2 m1A = ImVec2(center.x - halfW * 0.90f, fMax.y);
        ImVec2 m1B = ImVec2(center.x - halfW * 0.15f, center.y - halfH * 0.20f);
        ImVec2 m1C = ImVec2(center.x + halfW * 0.65f, fMax.y);
        drawList->AddTriangleFilled(m1A, m1B, m1C, IM_COL32(30, 135, 110, 255));

        // Foreground mountain
        ImVec2 m2A = ImVec2(center.x - halfW * 0.25f, fMax.y);
        ImVec2 m2B = ImVec2(center.x + halfW * 0.28f, center.y + halfH * 0.05f);
        ImVec2 m2C = ImVec2(center.x + halfW * 0.95f, fMax.y);
        drawList->AddTriangleFilled(m2A, m2B, m2C, IM_COL32(46, 204, 113, 255));

        drawList->PopClipRect();
    }

    static void DrawShaderIcon(ImDrawList* drawList, ImVec2 center, float size) {
        float r = size * 0.95f;
        ImVec2 pTop   = ImVec2(center.x, center.y - r);
        ImVec2 pBot   = ImVec2(center.x, center.y + r);
        ImVec2 pLeft  = ImVec2(center.x - r * 0.72f, center.y);
        ImVec2 pRight = ImVec2(center.x + r * 0.72f, center.y);
        ImVec2 pMid   = ImVec2(center.x, center.y + r * 0.16f);

        ImU32 cTL = IM_COL32(225, 120, 255, 255); // Radiant light purple
        ImU32 cTR = IM_COL32(185, 75, 245, 255);  // Neon violet
        ImU32 cBL = IM_COL32(150, 40, 215, 255);  // Deep amethyst
        ImU32 cBR = IM_COL32(110, 25, 170, 255);  // Shadow purple

        drawList->AddTriangleFilled(pTop, pLeft, pMid, cTL);
        drawList->AddTriangleFilled(pTop, pMid, pRight, cTR);
        drawList->AddTriangleFilled(pLeft, pBot, pMid, cBL);
        drawList->AddTriangleFilled(pMid, pBot, pRight, cBR);

        // Facets wireframe
        ImU32 colWire = IM_COL32(245, 205, 255, 255);
        drawList->AddLine(pTop, pMid, colWire, 1.4f);
        drawList->AddLine(pBot, pMid, colWire, 1.4f);
        drawList->AddLine(pLeft, pMid, colWire, 1.4f);
        drawList->AddLine(pRight, pMid, colWire, 1.4f);

        const ImVec2 diamondPts[4] = { pTop, pRight, pBot, pLeft };
        drawList->AddPolyline(diamondPts, 4, IM_COL32(255, 225, 255, 255), ImDrawFlags_Closed, 1.5f);

        // Shimmer sparkle
        drawList->AddCircleFilled(pMid, 1.8f, IM_COL32(255, 255, 255, 255));
    }

    static void DrawAssemblyIcon(ImDrawList* drawList, ImVec2 center, float size) {
        float r = size * 0.90f;
        drawList->AddRectFilled(ImVec2(center.x - r * 0.65f, center.y - r * 0.65f),
                                ImVec2(center.x + r * 0.65f, center.y + r * 0.65f),
                                IM_COL32(235, 145, 30, 240), 4.0f);
        drawList->AddRect(ImVec2(center.x - r * 0.65f, center.y - r * 0.65f),
                          ImVec2(center.x + r * 0.65f, center.y + r * 0.65f),
                          IM_COL32(255, 215, 110, 255), 4.0f, 0, 1.6f);

        float pinW = r * 0.35f;
        float pinH = r * 0.22f;
        drawList->AddRectFilled(ImVec2(center.x - pinW * 0.5f, center.y - r * 0.95f),
                                ImVec2(center.x + pinW * 0.5f, center.y - r * 0.60f),
                                IM_COL32(255, 195, 70, 255), 2.0f);
        drawList->AddRectFilled(ImVec2(center.x - pinW * 0.5f, center.y + r * 0.60f),
                                ImVec2(center.x + pinW * 0.5f, center.y + r * 0.95f),
                                IM_COL32(255, 195, 70, 255), 2.0f);
        drawList->AddRectFilled(ImVec2(center.x - r * 0.95f, center.y - pinH * 0.5f),
                                ImVec2(center.x - r * 0.60f, center.y + pinH * 0.5f),
                                IM_COL32(255, 195, 70, 255), 2.0f);
        drawList->AddRectFilled(ImVec2(center.x + r * 0.60f, center.y - pinH * 0.5f),
                                ImVec2(center.x + r * 0.95f, center.y + pinH * 0.5f),
                                IM_COL32(255, 195, 70, 255), 2.0f);

        drawList->AddCircleFilled(center, r * 0.25f, IM_COL32(26, 32, 42, 255));
        drawList->AddCircle(center, r * 0.25f, IM_COL32(255, 215, 100, 255), 0, 1.2f);
    }

    static void DrawSceneIcon(ImDrawList* drawList, ImVec2 center, float size) {
        float w = size * 1.40f;
        float h = size * 1.10f;
        float halfW = w * 0.5f;
        float halfH = h * 0.5f;

        // Base clapperboard
        ImVec2 bMin = ImVec2(center.x - halfW, center.y - halfH * 0.20f);
        ImVec2 bMax = ImVec2(center.x + halfW, center.y + halfH);
        drawList->AddRectFilled(bMin, bMax, IM_COL32(36, 42, 54, 255), 3.0f, ImDrawFlags_RoundBottom);
        drawList->AddRect(bMin, bMax, IM_COL32(80, 95, 115, 220), 3.0f, ImDrawFlags_RoundBottom, 1.0f);

        // Play triangle
        float triR = size * 0.22f;
        ImVec2 t1 = ImVec2(center.x - triR * 0.7f, center.y + halfH * 0.38f - triR);
        ImVec2 t2 = ImVec2(center.x - triR * 0.7f, center.y + halfH * 0.38f + triR);
        ImVec2 t3 = ImVec2(center.x + triR, center.y + halfH * 0.38f);
        drawList->AddTriangleFilled(t1, t2, t3, IM_COL32(255, 80, 95, 255));

        // Clapper top stick
        ImVec2 sMin = ImVec2(center.x - halfW, center.y - halfH);
        ImVec2 sMax = ImVec2(center.x + halfW, center.y - halfH * 0.20f);
        drawList->AddRectFilled(sMin, sMax, IM_COL32(28, 32, 42, 255), 3.0f, ImDrawFlags_RoundTop);

        // Angled stripes
        drawList->PushClipRect(sMin, sMax, true);
        for (float sx = sMin.x - 10.0f; sx < sMax.x + 10.0f; sx += 14.0f) {
            ImVec2 p1 = ImVec2(sx, sMax.y);
            ImVec2 p2 = ImVec2(sx + 8.0f, sMin.y);
            drawList->AddLine(p1, p2, IM_COL32(245, 195, 55, 255), 4.5f);
        }
        drawList->PopClipRect();
        drawList->AddRect(sMin, sMax, IM_COL32(80, 95, 115, 220), 3.0f, ImDrawFlags_RoundTop, 1.0f);
    }

    static void DrawProjectIcon(ImDrawList* drawList, ImVec2 center, float size) {
        float w = size * 1.35f;
        float h = size * 1.25f;
        float halfW = w * 0.5f;
        float halfH = h * 0.5f;

        // Blueprint card background
        ImVec2 pMin = ImVec2(center.x - halfW, center.y - halfH);
        ImVec2 pMax = ImVec2(center.x + halfW, center.y + halfH);
        drawList->AddRectFilled(pMin, pMax, IM_COL32(18, 38, 58, 255), 4.0f);
        drawList->AddRect(pMin, pMax, IM_COL32(0, 190, 160, 220), 4.0f, 0, 1.5f);

        // Blueprint grid lines
        drawList->AddLine(ImVec2(center.x - halfW * 0.7f, center.y), ImVec2(center.x + halfW * 0.7f, center.y), IM_COL32(30, 80, 100, 180), 1.0f);
        drawList->AddLine(ImVec2(center.x, center.y - halfH * 0.6f), ImVec2(center.x, center.y + halfH * 0.6f), IM_COL32(30, 80, 100, 180), 1.0f);

        // Inner glowing cube / badge
        float bSize = size * 0.45f;
        ImVec2 b1 = ImVec2(center.x, center.y - bSize * 0.7f);
        ImVec2 b2 = ImVec2(center.x + bSize * 0.7f, center.y - bSize * 0.2f);
        ImVec2 b3 = ImVec2(center.x, center.y + bSize * 0.35f);
        ImVec2 b4 = ImVec2(center.x - bSize * 0.7f, center.y - bSize * 0.2f);
        drawList->AddQuadFilled(b1, b2, b3, b4, IM_COL32(0, 230, 195, 200));
        drawList->AddQuad(b1, b2, b3, b4, IM_COL32(255, 255, 255, 240), 1.5f);
    }

    static void DrawMaterialIcon(ImDrawList* drawList, ImVec2 center, float size) {
        float r = size * 0.78f;

        // Pedestal shadow ring
        ImVec2 pedCenter = ImVec2(center.x, center.y + r * 0.75f);
        drawList->AddCircleFilled(pedCenter, r * 0.60f, IM_COL32(35, 42, 54, 200));

        // Base sphere
        drawList->AddCircleFilled(center, r, IM_COL32(235, 115, 40, 255));

        // Shadow crescent
        drawList->PushClipRect(ImVec2(center.x - r, center.y), ImVec2(center.x + r, center.y + r), true);
        drawList->AddCircleFilled(ImVec2(center.x + r * 0.25f, center.y + r * 0.25f), r * 0.85f, IM_COL32(140, 50, 15, 160));
        drawList->PopClipRect();

        // Specular highlight
        ImVec2 specCenter = ImVec2(center.x - r * 0.35f, center.y - r * 0.35f);
        drawList->AddCircleFilled(specCenter, r * 0.32f, IM_COL32(255, 225, 185, 200));
        drawList->AddCircleFilled(ImVec2(specCenter.x - 1.0f, specCenter.y - 1.0f), r * 0.12f, IM_COL32(255, 255, 255, 255));

        // Rim contour
        drawList->AddCircle(center, r, IM_COL32(255, 165, 95, 220), 24, 1.2f);
    }

    static void DrawAudioIcon(ImDrawList* drawList, ImVec2 center, float size) {
        float r = size * 0.85f;
        float bx = center.x - r * 0.35f;
        float by = center.y;
        float bw = r * 0.32f;
        float bh = r * 0.48f;
        ImVec2 boxMin(bx - bw * 0.5f, by - bh * 0.5f);
        ImVec2 boxMax(bx + bw * 0.5f, by + bh * 0.5f);
        drawList->AddRectFilled(boxMin, boxMax, IM_COL32(235, 75, 140, 255), 2.0f);

        ImVec2 c1(bx + bw * 0.5f, by - bh * 0.5f);
        ImVec2 c2(center.x + r * 0.12f, by - r * 0.65f);
        ImVec2 c3(center.x + r * 0.12f, by + r * 0.65f);
        ImVec2 c4(bx + bw * 0.5f, by + bh * 0.5f);
        drawList->AddQuadFilled(c1, c2, c3, c4, IM_COL32(255, 95, 160, 255));

        float arcX = center.x + r * 0.30f;
        drawList->AddLine(ImVec2(arcX, by - r * 0.35f), ImVec2(arcX + r * 0.15f, by), IM_COL32(255, 180, 215, 240), 2.0f);
        drawList->AddLine(ImVec2(arcX + r * 0.15f, by), ImVec2(arcX, by + r * 0.35f), IM_COL32(255, 180, 215, 240), 2.0f);

        float arc2X = center.x + r * 0.55f;
        drawList->AddLine(ImVec2(arc2X, by - r * 0.58f), ImVec2(arc2X + r * 0.20f, by), IM_COL32(255, 140, 190, 240), 2.0f);
        drawList->AddLine(ImVec2(arc2X + r * 0.20f, by), ImVec2(arc2X, by + r * 0.58f), IM_COL32(255, 140, 190, 240), 2.0f);
    }

    static void DrawDocumentIcon(ImDrawList* drawList, ImVec2 center, float size, ImU32 lineCol) {
        float w = size * 1.15f;
        float h = size * 1.45f;
        float halfW = w * 0.5f;
        float halfH = h * 0.5f;

        ImVec2 pMin = ImVec2(center.x - halfW, center.y - halfH);
        ImVec2 pMax = ImVec2(center.x + halfW, center.y + halfH);
        float fold = size * 0.38f;

        ImVec2 ptTopLeft   = pMin;
        ImVec2 ptTopRight1 = ImVec2(pMax.x - fold, pMin.y);
        ImVec2 ptFoldRight = ImVec2(pMax.x, pMin.y + fold);
        ImVec2 ptBotRight  = pMax;
        ImVec2 ptBotLeft   = ImVec2(pMin.x, pMax.y);

        const ImVec2 pagePts[5] = { ptTopLeft, ptTopRight1, ptFoldRight, ptBotRight, ptBotLeft };
        drawList->AddConvexPolyFilled(pagePts, 5, IM_COL32(230, 236, 245, 255));
        drawList->AddPolyline(pagePts, 5, IM_COL32(170, 180, 195, 255), ImDrawFlags_Closed, 1.2f);

        ImVec2 foldCorner = ImVec2(pMax.x - fold, pMin.y + fold);
        drawList->AddTriangleFilled(ptTopRight1, ptFoldRight, foldCorner, IM_COL32(190, 200, 215, 255));
        drawList->AddTriangle(ptTopRight1, ptFoldRight, foldCorner, IM_COL32(150, 160, 175, 255), 1.0f);

        float startY = pMin.y + size * 0.55f;
        for (int i = 0; i < 3; ++i) {
            float ly = startY + i * size * 0.25f;
            float lw = (i == 2) ? halfW * 0.9f : halfW * 1.4f;
            drawList->AddLine(ImVec2(center.x - halfW * 0.65f, ly), ImVec2(center.x - halfW * 0.65f + lw, ly), lineCol, 1.6f);
        }
    }

    static void DrawGenericFileIcon(ImDrawList* drawList, ImVec2 center, float size) {
        DrawDocumentIcon(drawList, center, size, IM_COL32(130, 145, 165, 200));
    }

    // -----------------------------------------------------------------------------
    // ContentBrowserPanel Class Implementation
    // -----------------------------------------------------------------------------

    ContentBrowserPanel::ContentBrowserPanel() {
        m_ThumbnailManager = std::make_unique<AssetThumbnailManager>();
        SetRootPath(FileSystem::GetAssetDirectory());
    }

    ContentBrowserPanel::ContentBrowserPanel(const std::filesystem::path& rootPath) {
        m_ThumbnailManager = std::make_unique<AssetThumbnailManager>();
        SetRootPath(rootPath);
    }

    ContentBrowserPanel::~ContentBrowserPanel() = default;

    void ContentBrowserPanel::SetRootPath(const std::filesystem::path& rootPath) {
        m_RootDirectory = rootPath;
        if (!std::filesystem::exists(m_RootDirectory)) {
            std::filesystem::create_directories(m_RootDirectory);
        }
        m_CurrentDirectory = m_RootDirectory;
        AssetManager::Sync(m_RootDirectory);
    }

    void ContentBrowserPanel::RefreshThumbnails() {
        if (m_ThumbnailManager) {
            m_ThumbnailManager->Clear();
        }
        AssetManager::Sync(m_CurrentDirectory);
    }

    std::filesystem::path ContentBrowserPanel::CreateNewFolder(const std::filesystem::path& parentDir, const std::string& baseName) {
        std::filesystem::path newPath = parentDir / baseName;
        int counter = 1;
        while (std::filesystem::exists(newPath)) {
            newPath = parentDir / (baseName + "_" + std::to_string(counter++));
        }
        std::error_code ec;
        std::filesystem::create_directory(newPath, ec);
        return newPath;
    }

    std::filesystem::path ContentBrowserPanel::CreateNewScene(const std::filesystem::path& parentDir, const std::string& baseName) {
        std::filesystem::path newPath = parentDir / (baseName + ".axlescene");
        int counter = 1;
        while (std::filesystem::exists(newPath)) {
            newPath = parentDir / (baseName + "_" + std::to_string(counter++) + ".axlescene");
        }
        std::ofstream fout(newPath);
        if (fout.is_open()) {
            fout << "# Axle Scene Definition File\n";
            fout << "Scene: " << newPath.stem().string() << "\n";
            fout << "Entities:\n";
            fout.close();
            AssetManager::GetOrCreateMetadata(newPath);
        }
        return newPath;
    }

    std::filesystem::path ContentBrowserPanel::CreateNewMaterial(const std::filesystem::path& parentDir, const std::string& baseName) {
        std::filesystem::path newPath = parentDir / (baseName + ".axleasset");
        int counter = 1;
        while (std::filesystem::exists(newPath)) {
            newPath = parentDir / (baseName + "_" + std::to_string(counter++) + ".axleasset");
        }
        Material mat(newPath.stem().string());
        if (mat.Save(newPath)) {
            AssetManager::GetOrCreateMetadata(newPath);
        }
        return newPath;
    }

    std::filesystem::path ContentBrowserPanel::CreateNewTexture(const std::filesystem::path& parentDir, const std::string& baseName) {
        std::filesystem::path newPath = parentDir / (baseName + ".axleasset");
        int counter = 1;
        while (std::filesystem::exists(newPath)) {
            newPath = parentDir / (baseName + "_" + std::to_string(counter++) + ".axleasset");
        }
        TextureDescriptor desc;
        desc.Name = newPath.stem().string();
        desc.Type = "Color";
        desc.Filter = "Anisotropic16x";
        desc.WrapU = "Repeat";
        desc.WrapV = "Repeat";
        desc.GenerateMips = true;
        if (TextureDescriptor::Save(newPath.string(), desc)) {
            AssetManager::GetOrCreateMetadata(newPath);
        }
        return newPath;
    }

    std::filesystem::path ContentBrowserPanel::CreateNewVFXAsset(const std::filesystem::path& parentDir, const std::string& baseName) {
        std::filesystem::path newPath = parentDir / (baseName + ".axleasset");
        int counter = 1;
        while (std::filesystem::exists(newPath)) {
            newPath = parentDir / (baseName + "_" + std::to_string(counter++) + ".axleasset");
        }
        ParticleEmitterConfig config;
        config.Name = newPath.stem().string();
        config.MaxParticles = 1000;
        config.EmissionRate = 50.0f;
        config.Lifetime = FloatRange(1.0f, 2.0f);
        config.InitialSpeed = FloatRange(2.0f, 5.0f);
        config.Shape.Type = ParticleEmitterShapeType::Cone;
        config.Shape.ConeAngle = 0.52359877f;
        config.Shape.Radius = 0.5f;
        config.ColorOverLifetime.StartColor = glm::vec4(1.0f, 0.6f, 0.2f, 1.0f);
        config.ColorOverLifetime.EndColor = glm::vec4(0.2f, 0.05f, 0.0f, 0.0f);
        config.BlendMode = ParticleBlendMode::Additive;

        if (ParticleEmitterSerializer::SerializeToFile(newPath.string(), config)) {
            AssetManager::GetOrCreateMetadata(newPath);
        }
        return newPath;
    }

    std::filesystem::path ContentBrowserPanel::CreateNewScript(const std::filesystem::path& parentDir, const std::string& baseName) {
        std::string className = baseName;
        std::filesystem::path newPath = parentDir / (className + ".hpp");
        int counter = 1;
        while (std::filesystem::exists(newPath)) {
            className = baseName + "_" + std::to_string(counter++);
            newPath = parentDir / (className + ".hpp");
        }
        std::ofstream fout(newPath);
        if (fout.is_open()) {
            fout << "#pragma once\n";
            fout << "#include <Axle/ScriptableEntity.hpp>\n";
            fout << "#include <Axle/ScriptRegistry.hpp>\n";
            fout << "#include <Axle/Components.hpp>\n";
            fout << "#include <Axle/Input.hpp>\n";
            fout << "#include <Axle/KeyCodes.hpp>\n\n";
            fout << "class " << className << " : public Axle::ScriptableEntity {\n";
            fout << "public:\n";
            fout << "    float Speed = 10.0f;\n";
            fout << "    bool Enabled = true;\n\n";
            fout << "    void OnCreate() override {\n";
            fout << "        // Called once on play mode start\n";
            fout << "    }\n\n";
            fout << "    void OnUpdate(Axle::Timestep ts) override {\n";
            fout << "        (void)ts;\n";
            fout << "        // Update per-frame logic\n";
            fout << "    }\n\n";
            fout << "    void OnDestroy() override {\n";
            fout << "        // Called on cleanup\n";
            fout << "    }\n\n";
            fout << "    AXLE_SCRIPT_FIELDS(" << className << ",\n";
            fout << "        AXLE_FIELD(Speed, 0.0f, 100.0f, \"Movement speed\"),\n";
            fout << "        AXLE_FIELD(Enabled, \"Enable script execution\")\n";
            fout << "    )\n";
            fout << "};\n\n";
            fout << "AXLE_REGISTER_SCRIPT(" << className << ");\n";
            fout.close();
            AssetManager::GetOrCreateMetadata(newPath);
        }
        return newPath;
    }

    std::pair<std::filesystem::path, std::filesystem::path> ContentBrowserPanel::CreateNewClass(const std::filesystem::path& parentDir, const std::string& baseName) {
        std::string className = baseName;
        std::filesystem::path hppPath = parentDir / (className + ".hpp");
        std::filesystem::path cppPath = parentDir / (className + ".cpp");
        int counter = 1;
        while (std::filesystem::exists(hppPath) || std::filesystem::exists(cppPath)) {
            className = baseName + "_" + std::to_string(counter++);
            hppPath = parentDir / (className + ".hpp");
            cppPath = parentDir / (className + ".cpp");
        }

        // 1. Write Header (.hpp)
        std::ofstream hppOut(hppPath);
        if (hppOut.is_open()) {
            hppOut << "#pragma once\n";
            hppOut << "#include <Axle/ScriptableEntity.hpp>\n";
            hppOut << "#include <Axle/ScriptRegistry.hpp>\n";
            hppOut << "#include <Axle/Components.hpp>\n\n";
            hppOut << "class " << className << " : public Axle::ScriptableEntity {\n";
            hppOut << "public:\n";
            hppOut << "    float Speed = 10.0f;\n";
            hppOut << "    bool Enabled = true;\n\n";
            hppOut << "    void OnCreate() override;\n";
            hppOut << "    void OnUpdate(Axle::Timestep ts) override;\n";
            hppOut << "    void OnDestroy() override;\n\n";
            hppOut << "    AXLE_SCRIPT_FIELDS(" << className << ",\n";
            hppOut << "        AXLE_FIELD(Speed, 0.0f, 100.0f, \"Movement speed\"),\n";
            hppOut << "        AXLE_FIELD(Enabled, \"Enable script execution\")\n";
            hppOut << "    )\n";
            hppOut << "};\n";
            hppOut.close();
            AssetManager::GetOrCreateMetadata(hppPath);
        }

        // 2. Write Implementation (.cpp)
        std::ofstream cppOut(cppPath);
        if (cppOut.is_open()) {
            cppOut << "#include \"" << className << ".hpp\"\n";
            cppOut << "#include <Axle/Log.hpp>\n\n";
            cppOut << "AXLE_REGISTER_SCRIPT(" << className << ");\n\n";
            cppOut << "void " << className << "::OnCreate() {\n";
            cppOut << "    AXLE_INFO(\"" << className << "::OnCreate() called\");\n";
            cppOut << "}\n\n";
            cppOut << "void " << className << "::OnUpdate(Axle::Timestep ts) {\n";
            cppOut << "    (void)ts;\n";
            cppOut << "}\n\n";
            cppOut << "void " << className << "::OnDestroy() {\n";
            cppOut << "    AXLE_INFO(\"" << className << "::OnDestroy() called\");\n";
            cppOut << "}\n";
            cppOut.close();
            AssetManager::GetOrCreateMetadata(cppPath);
        }

        return { hppPath, cppPath };
    }

    std::filesystem::path ContentBrowserPanel::CreateNewPhysicsMaterial(const std::filesystem::path& parentDir, const std::string& baseName) {
        std::filesystem::path newPath = parentDir / (baseName + ".physmat");
        int counter = 1;
        while (std::filesystem::exists(newPath)) {
            newPath = parentDir / (baseName + "_" + std::to_string(counter++) + ".physmat");
        }
        PhysicsMaterial defaultPhysMat;
        if (PhysicsMaterial::Save(newPath, defaultPhysMat)) {
            AssetManager::GetOrCreateMetadata(newPath);
        }
        return newPath;
    }

    void ContentBrowserPanel::SetStatusNotification(const std::string& message, float duration) {
        m_StatusNotificationMessage = message;
        m_StatusNotificationTimer = duration;
    }

    void ContentBrowserPanel::OnImGuiRender(bool* p_open) {
        if (p_open && !*p_open) return;

        ImGui::SetNextWindowSize(ImVec2(650.0f, 320.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Project", nullptr)) {
            m_IsHovered = false;
            ImGui::End();
            return;
        }

        m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

        if (m_StatusNotificationTimer > 0.0f) {
            m_StatusNotificationTimer -= ImGui::GetIO().DeltaTime;
            if (m_StatusNotificationTimer < 0.0f) {
                m_StatusNotificationTimer = 0.0f;
            }
        }

        if (m_ThumbnailManager) {
            m_ThumbnailManager->OnUpdate();
        }

        RenderTopBar();
        ImGui::Spacing();
        RenderBreadcrumb();
        ImGui::Separator();
        RenderDirectoryContents();

        ImGui::End();
    }

    void ContentBrowserPanel::RenderTopBar() {
        // [+ ▾] Unity Create Button
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("+ ▾", ImVec2(36.0f, 24.0f))) {
            ImGui::OpenPopup("ProjectCreatePopup");
        }
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Create New Asset in Current Directory");
        }

        if (ImGui::BeginPopup("ProjectCreatePopup")) {
            if (ImGui::MenuItem("Folder")) {
                CreateNewFolder(m_CurrentDirectory);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("C++ Script (ScriptableEntity)")) {
                CreateNewScript(m_CurrentDirectory);
            }
            if (ImGui::MenuItem("C++ Class (.h & .cpp)")) {
                CreateNewClass(m_CurrentDirectory);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Scene")) {
                CreateNewScene(m_CurrentDirectory);
            }
            if (ImGui::MenuItem("Material Asset (.axleasset)")) {
                CreateNewMaterial(m_CurrentDirectory);
            }
            if (ImGui::MenuItem("Texture Asset (.axleasset)")) {
                CreateNewTexture(m_CurrentDirectory);
            }
            if (ImGui::MenuItem("VFX Asset (.axleasset)")) {
                CreateNewVFXAsset(m_CurrentDirectory);
            }
            if (ImGui::MenuItem("Physics Material")) {
                CreateNewPhysicsMaterial(m_CurrentDirectory);
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();

        // Back Button
        bool canGoBack = (m_CurrentDirectory != m_RootDirectory) && std::filesystem::exists(m_CurrentDirectory.parent_path());
        if (!canGoBack) {
            ImGui::BeginDisabled(true);
        }
        if (ImGui::Button("< Back", ImVec2(60.0f, 24.0f))) {
            m_CurrentDirectory = m_CurrentDirectory.parent_path();
            m_SelectedEntry.clear();
        }
        if (!canGoBack) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(62.0f, 24.0f))) {
            // View refreshes automatically next frame
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(170.0f);
        ImGui::InputTextWithHint("##CBSearch", "Search assets...", m_SearchFilter, sizeof(m_SearchFilter));
        if (m_SearchFilter[0] != '\0') {
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                m_SearchFilter[0] = '\0';
            }
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.40f, 0.48f, 1.0f), "|");

        ImGui::SameLine();
        ImGui::TextDisabled("Thumb:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(75.0f);
        ImGui::SliderFloat("##ThumbSize", &m_ThumbnailSize, 60.0f, 120.0f, "%.0f px");

        ImGui::SameLine();
        if (ImGui::Button("Open Folder", ImVec2(95.0f, 24.0f))) {
#ifdef _WIN32
            ShellExecuteW(nullptr, L"open", m_CurrentDirectory.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
#endif
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.40f, 0.48f, 1.0f), "|");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.44f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.54f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.36f, 0.10f, 1.0f));
        if (ImGui::Button("Compile Assets (F9)", ImVec2(140.0f, 24.0f))) {
            if (m_OnCompileAssetsCallback) {
                m_OnCompileAssetsCallback();
            }
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Compile modified .axle models into GPU binary cache (.axlebin) and hot-reload active scene entities (F9)");
        }
    }

    void ContentBrowserPanel::RenderBreadcrumb() {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.20f, 0.26f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.32f, 0.42f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.42f, 0.55f, 1.0f));

        // Root "Assets" Chip
        if (ImGui::Button("Assets")) {
            m_CurrentDirectory = m_RootDirectory;
            m_SelectedEntry.clear();
        }

        // Relative path segments
        std::error_code ec;
        auto relative = std::filesystem::relative(m_CurrentDirectory, m_RootDirectory, ec);
        if (!ec && relative != "." && !relative.empty()) {
            std::filesystem::path accum = m_RootDirectory;
            for (const auto& part : relative) {
                accum /= part;
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.45f, 0.52f, 0.62f, 1.0f), ">");
                ImGui::SameLine();
                if (ImGui::Button(part.string().c_str())) {
                    m_CurrentDirectory = accum;
                    m_SelectedEntry.clear();
                }
            }
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }

    void ContentBrowserPanel::RenderStatusBar(size_t folderCount, size_t fileCount) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("%zu folder%s, %zu file%s", 
            folderCount, folderCount == 1 ? "" : "s", 
            fileCount, fileCount == 1 ? "" : "s");

        if (!m_SelectedEntry.empty() && std::filesystem::exists(m_SelectedEntry)) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.35f, 0.40f, 0.48f, 1.0f), "|");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.40f, 0.75f, 1.0f, 1.0f), "Selected: %s", m_SelectedEntry.filename().string().c_str());
        }

        if (m_StatusNotificationTimer > 0.0f && !m_StatusNotificationMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.35f, 0.40f, 0.48f, 1.0f), "|");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.25f, 0.90f, 0.55f, 1.0f), "[OK] %s", m_StatusNotificationMessage.c_str());
        }

        // Right side: Unity Project Thumbnail Zoom Slider
        const float sliderWidth = 110.0f;
        float rightX = ImGui::GetWindowWidth() - sliderWidth - 20.0f;
        if (rightX > ImGui::GetCursorPosX()) {
            ImGui::SameLine(rightX);
            ImGui::SetNextItemWidth(sliderWidth);
            ImGui::SliderFloat("##ThumbnailSize", &m_ThumbnailSize, 48.0f, 140.0f, "");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Adjust Thumbnail Size (Unity Project Slider)");
            }
        }
    }

    void ContentBrowserPanel::RenderDirectoryContents() {
        if (!std::filesystem::exists(m_CurrentDirectory)) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Directory not found: %s", m_CurrentDirectory.string().c_str());
            return;
        }

        // Collect and sort directory entries (directories first, then alphabetical)
        std::vector<std::filesystem::directory_entry> directories;
        std::vector<std::filesystem::directory_entry> files;

        for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory)) {
            const auto filename = entry.path().filename().string();
            if (!m_ShowHiddenFiles && !filename.empty() && filename[0] == '.') {
                continue;
            }

            // Companion .meta files are hidden from content grid
            if (entry.path().extension() == ".meta") {
                continue;
            }

            if (m_SearchFilter[0] != '\0' && !CaseInsensitiveFind(filename, m_SearchFilter)) {
                continue;
            }

            if (entry.is_directory()) {
                directories.push_back(entry);
            } else {
                files.push_back(entry);
            }
        }

        std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
            return a.path().filename().string() < b.path().filename().string();
        });
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
            return a.path().filename().string() < b.path().filename().string();
        });

        std::vector<std::filesystem::directory_entry> allEntries;
        allEntries.reserve(directories.size() + files.size());
        allEntries.insert(allEntries.end(), directories.begin(), directories.end());
        allEntries.insert(allEntries.end(), files.begin(), files.end());

        if (allEntries.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("This folder is empty.");
            RenderStatusBar(0, 0);
            return;
        }

        float panelWidth = ImGui::GetContentRegionAvail().x;
        float cellSize = m_ThumbnailSize + m_Padding;
        int columnCount = static_cast<int>(panelWidth / cellSize);
        if (columnCount < 1) columnCount = 1;

        // Custom Child Window for scrollable items area
        float childHeight = ImGui::GetContentRegionAvail().y - 28.0f; // Leave space for status bar
        if (childHeight < 80.0f) childHeight = 80.0f;

        if (ImGui::BeginChild("CBGridChild", ImVec2(0.0f, childHeight), false)) {
            if (ImGui::BeginTable("CBGridTable", columnCount)) {
                int itemIndex = 0;
                for (const auto& entry : allEntries) {
                    ImGui::TableNextColumn();
                    ImGui::PushID(itemIndex++);

                    const auto& path = entry.path();
                    std::string filenameStr = path.filename().string();
                    bool isDir = entry.is_directory();
                    auto meta = GetAssetVisualMeta(entry);

                    ImVec2 cardSize(m_ThumbnailSize, m_ThumbnailSize * 1.16f);

                    // Clickable Card item
                    ImGui::InvisibleButton("##AssetCard", cardSize);
                    bool isHovered = ImGui::IsItemHovered();
                    bool isClicked = ImGui::IsItemClicked();
                    bool isDoubleClicked = isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                    bool isSelected = (m_SelectedEntry == path);

                    if (isHovered && !isDir) {
                        UUID uuid = AssetManager::GetAssetUUID(path);
                        AssetType type = AssetManager::GetAssetType(path);
                        ImGui::BeginTooltip();
                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", filenameStr.c_str());
                        ImGui::TextDisabled("Type: %s", AssetTypeToString(type));
                        ImGui::TextDisabled("UUID: %llu", (unsigned long long)(uint64_t)uuid);
                        ImGui::EndTooltip();
                    }

                    if (isClicked) {
                        m_SelectedEntry = path;
                        if (!isDir) {
                            if (m_OnSelectAssetCallback) {
                                m_OnSelectAssetCallback(path);
                            }
                        } else {
                            if (m_OnSelectAssetCallback) {
                                m_OnSelectAssetCallback({});
                            }
                        }
                    }

                    if (isDoubleClicked) {
                        if (isDir) {
                            m_CurrentDirectory /= path.filename();
                            m_SelectedEntry.clear();
                        } else {
                            std::string ext = path.extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                            if ((ext == ".axlescene" || ext == ".scene") && m_OnOpenSceneCallback) {
                                m_OnOpenSceneCallback(path);
                                SetStatusNotification("Loaded scene: " + path.filename().string());
                            } else if (ext == ".axleproj" && m_OnOpenProjectCallback) {
                                m_OnOpenProjectCallback(path);
                                SetStatusNotification("Loaded project: " + path.filename().string());
                            } else if ((ext == ".axleui" || ext == ".axleasset") && m_OnOpenUICanvasCallback) {
                                if (ext == ".axleui" || AssetDSL::PeekAssetKindFromFile(path.string()) == Axle::AssetKind::Canvas) {
                                    m_OnOpenUICanvasCallback(path);
                                    SetStatusNotification("Opened UI Designer: " + path.filename().string());
                                }
                            }
                            // Note: .axle files are inspected via single-click into Properties + Asset Preview tabs
                        }
                    }

                    // Right-Click Context Menu on Asset Item
                    if (ImGui::BeginPopupContextItem()) {
                        m_SelectedEntry = path;
                        if (!isDir && m_OnSelectAssetCallback) {
                            m_OnSelectAssetCallback(path);
                        }
                        if (m_AssetPreviewPanel && !isDir) {
                            std::string ext = path.extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                            if (ext == ".axle" || ext == ".axlemesh" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                                m_AssetPreviewPanel->SetAsset(path);
                            }
                        }

                        std::string ext = path.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.0f, 1.0f), "%s", filenameStr.c_str());
                        if (!isDir) {
                            UUID uuid = AssetManager::GetAssetUUID(path);
                            ImGui::TextDisabled("UUID: %llu", (unsigned long long)(uint64_t)uuid);
                        }
                        ImGui::Separator();

                        if (ext == ".axle") {
                            if (ImGui::MenuItem("Open in Assembly Editor")) {
                                if (m_OnOpenAssemblyCallback) {
                                    m_OnOpenAssemblyCallback(path);
                                }
                            }
                            if (ImGui::MenuItem("Instantiate into Active Scene")) {
                                if (m_OnInstantiateAssemblyCallback) {
                                    m_OnInstantiateAssemblyCallback(path);
                                    SetStatusNotification("Spawned Assembly: " + path.filename().string());
                                }
                            }
                            ImGui::Separator();
                        }

                        if (ext == ".axlescene" || ext == ".scene") {
                            if (ImGui::MenuItem("Open Scene in Viewport")) {
                                if (m_OnOpenSceneCallback) {
                                    m_OnOpenSceneCallback(path);
                                    SetStatusNotification("Loaded scene: " + path.filename().string());
                                }
                            }
                            ImGui::Separator();
                        } else if (ext == ".axleproj") {
                            if (ImGui::MenuItem("Open Project")) {
                                if (m_OnOpenProjectCallback) {
                                    m_OnOpenProjectCallback(path);
                                    SetStatusNotification("Loaded project: " + path.filename().string());
                                }
                            }
                            ImGui::Separator();
                        } else if (ext == ".axleui" || ext == ".axleasset") {
                            bool isCanvas = (ext == ".axleui");
                            if (ext == ".axleasset") {
                                isCanvas = (AssetDSL::PeekAssetKindFromFile(path.string()) == Axle::AssetKind::Canvas);
                            }
                            if (isCanvas) {
                                if (ImGui::MenuItem("Open in UI Canvas Designer")) {
                                    if (m_OnOpenUICanvasCallback) {
                                        m_OnOpenUICanvasCallback(path);
                                    }
                                }
                                ImGui::Separator();
                            }
                        }

                        if (ext == ".axlemesh" || ext == ".axlebin") {
                            if (ImGui::MenuItem("Convert to Wavefront OBJ (.obj)")) {
                                auto outObj = path.parent_path() / (path.stem().string() + ".obj");
                                if (AssetSerializer::ConvertAxleToOBJ(path, outObj)) {
                                    SetStatusNotification("Exported to " + outObj.filename().string());
                                }
                            }

                            if (ext == ".axle") {
                                std::ifstream manifestCheck(path);
                                std::string manifestLine;
                                bool isPackage = false;
                                while (std::getline(manifestCheck, manifestLine)) {
                                    if (manifestLine.rfind("Parts:", 0) == 0) {
                                        isPackage = true;
                                        break;
                                    }
                                }
                                if (isPackage) {
                                    if (ImGui::MenuItem("Export Subparts to Separate .obj Files")) {
                                        if (AssetSerializer::ConvertAxlePackagePartsToOBJ(path)) {
                                            SetStatusNotification("Exported subparts to " + path.stem().string() + "_obj_parts/");
                                        }
                                    }
                                }
                            }
                            ImGui::Separator();
                        } else if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                            if (ImGui::MenuItem("Convert to Axle Mesh (.axle)")) {
                                auto outAxle = path.parent_path() / (path.stem().string() + ".axle");
                                if (AssetSerializer::AutoConvertModel(path, outAxle)) {
                                    SetStatusNotification("Converted to " + outAxle.filename().string());
                                }
                            }
                            ImGui::Separator();
                        }

                        if (!isDir && m_AssetPreviewPanel) {
                            if (ImGui::MenuItem("Inspect in 3D Asset Preview")) {
                                m_AssetPreviewPanel->SetAsset(path);
                            }
                            ImGui::Separator();
                        }

                        if (ImGui::MenuItem("Open in System Explorer")) {
#ifdef _WIN32
                            ShellExecuteW(nullptr, L"open", m_CurrentDirectory.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
#endif
                        }

                        if (ImGui::MenuItem("Copy Relative Path")) {
                            std::error_code relEc;
                            auto rel = std::filesystem::relative(path, m_RootDirectory, relEc);
                            if (!relEc) {
                                ImGui::SetClipboardText(rel.string().c_str());
                            }
                        }

                        ImGui::EndPopup();
                    }

                    // Drag & Drop Source
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        std::string fullPathStr = path.string();
                        ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", fullPathStr.c_str(), fullPathStr.size() + 1);
                        ImGui::Text("Asset: %s", filenameStr.c_str());
                        ImGui::EndDragDropSource();
                    }

                    // Render Card Visuals
                    ImVec2 cardMin = ImGui::GetItemRectMin();
                    ImVec2 cardMax = ImGui::GetItemRectMax();
                    float cardW = cardMax.x - cardMin.x;
                    float cardH = cardMax.y - cardMin.y;
                    ImDrawList* drawList = ImGui::GetWindowDrawList();

                    // Background color
                    ImU32 bgCol = isSelected ? IM_COL32(32, 54, 82, 235)
                                : (isHovered ? IM_COL32(38, 44, 56, 230)
                                             : IM_COL32(25, 29, 37, 210));
                    drawList->AddRectFilled(cardMin, cardMax, bgCol, 6.0f);

                    // Border outline
                    ImU32 borderCol = isSelected ? IM_COL32(95, 175, 255, 255)
                                    : (isHovered ? meta.AccentColor
                                                 : IM_COL32(48, 54, 66, 170));
                    float borderThickness = (isSelected || isHovered) ? 1.5f : 1.0f;
                    drawList->AddRect(cardMin, cardMax, borderCol, 6.0f, 0, borderThickness);

                    // Upper Icon Area (68% height)
                    float iconAreaH = cardH * 0.68f;
                    ImVec2 iconCenter = ImVec2(cardMin.x + cardW * 0.5f, cardMin.y + iconAreaH * 0.52f);
                    float iconScale = iconAreaH * 0.36f;

                    u32 thumbTex = 0;
                    if ((meta.Kind == CBAssetKind::Model3D || meta.Kind == CBAssetKind::Assembly || 
                         meta.Kind == CBAssetKind::Material || meta.Kind == CBAssetKind::Texture) && m_ThumbnailManager) {
                        thumbTex = m_ThumbnailManager->GetOrCreateThumbnail(path);
                    }

                    if (thumbTex != 0) {
                        ImVec2 thumbMin = ImVec2(cardMin.x + 3.0f, cardMin.y + 3.0f);
                        ImVec2 thumbMax = ImVec2(cardMax.x - 3.0f, cardMin.y + iconAreaH);
                        drawList->AddImage(
                            (ImTextureID)(intptr_t)thumbTex,
                            thumbMin, thumbMax,
                            ImVec2(0, 1), ImVec2(1, 0)
                        );
                        // Subtle type badge pill in top right
                        ImVec2 badgeMax = ImVec2(cardMax.x - 4.0f, cardMin.y + 16.0f);
                        ImVec2 badgeMin = ImVec2(badgeMax.x - 32.0f, cardMin.y + 4.0f);
                        drawList->AddRectFilled(badgeMin, badgeMax, IM_COL32(18, 24, 34, 210), 3.0f);
                        drawList->AddText(ImVec2(badgeMin.x + 3.0f, badgeMin.y + 1.0f), meta.AccentColor, meta.Tag);
                    } else {
                        switch (meta.Kind) {
                            case CBAssetKind::Folder:   DrawFolderIcon(drawList, iconCenter, iconScale); break;
                            case CBAssetKind::Assembly: DrawAssemblyIcon(drawList, iconCenter, iconScale); break;
                            case CBAssetKind::Model3D:  DrawModel3DIcon(drawList, iconCenter, iconScale); break;
                            case CBAssetKind::Texture:  DrawTexturePictureIcon(drawList, iconCenter, iconScale); break;
                            case CBAssetKind::Shader:   DrawShaderIcon(drawList, iconCenter, iconScale); break;
                            case CBAssetKind::Scene:    DrawSceneIcon(drawList, iconCenter, iconScale); break;
                            case CBAssetKind::Project:         DrawProjectIcon(drawList, iconCenter, iconScale); break;
                            case CBAssetKind::Material:        
                            case CBAssetKind::PhysicsMaterial: DrawMaterialIcon(drawList, iconCenter, iconScale); break;
                            case CBAssetKind::Script:          DrawDocumentIcon(drawList, iconCenter, iconScale, IM_COL32(75, 160, 245, 230)); break;
                            case CBAssetKind::Audio:           DrawAudioIcon(drawList, iconCenter, iconScale); break;
                            case CBAssetKind::Document: DrawDocumentIcon(drawList, iconCenter, iconScale, IM_COL32(150, 165, 185, 230)); break;
                            default:                  DrawGenericFileIcon(drawList, iconCenter, iconScale); break;
                        }
                    }

                    // Lower Footer Area (32% height)
                    float footerH = cardH - iconAreaH;
                    ImVec2 footerMin = ImVec2(cardMin.x, cardMin.y + iconAreaH);
                    ImVec2 footerMax = cardMax;

                    ImU32 footerBg = isSelected ? IM_COL32(20, 36, 56, 245)
                                   : (isHovered ? IM_COL32(28, 33, 43, 240)
                                                : IM_COL32(20, 23, 30, 230));
                    drawList->AddRectFilled(footerMin, footerMax, footerBg, 6.0f, ImDrawFlags_RoundBottom);
                    drawList->AddLine(footerMin, ImVec2(footerMax.x, footerMin.y), IM_COL32(45, 52, 64, 150), 1.0f);

                    // Centered formatted filename (strip extension / show subpart cleanly)
                    std::string label = filenameStr;
                    auto hashPos = label.find('#');
                    if (hashPos != std::string::npos) {
                        label = label.substr(0, hashPos);
                    } else {
                        auto dotPos = label.rfind('.');
                        if (dotPos != std::string::npos && dotPos > 0 && !isDir) {
                            label = label.substr(0, dotPos);
                        }
                    }

                    ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                    float textMaxW = cardW - 8.0f;
                    if (textSize.x > textMaxW && label.length() > 4) {
                        while (label.length() > 3 && ImGui::CalcTextSize((label + "..").c_str()).x > textMaxW) {
                            label.pop_back();
                        }
                        label += "..";
                        textSize = ImGui::CalcTextSize(label.c_str());
                    }

                    ImVec2 textPos = ImVec2(cardMin.x + (cardW - textSize.x) * 0.5f, footerMin.y + (footerH - textSize.y) * 0.5f);
                    ImU32 textCol = isSelected ? IM_COL32(255, 255, 255, 255)
                                  : (isHovered ? IM_COL32(245, 248, 255, 255)
                                               : IM_COL32(205, 215, 228, 240));
                    drawList->AddText(textPos, textCol, label.c_str());

                    // Rich Tooltip
                    if (isHovered) {
                        ImGui::BeginTooltip();

                        if (thumbTex != 0) {
                            ImGui::Image((ImTextureID)(intptr_t)thumbTex, ImVec2(140.0f, 140.0f), ImVec2(0, 1), ImVec2(1, 0));
                            ImGui::Spacing();
                        }

                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", filenameStr.c_str());
                        ImGui::Separator();
                        ImGui::TextDisabled("Type: %s", meta.TypeLabel);

                        if (m_ThumbnailManager) {
                            if (const auto* cachedMeta = m_ThumbnailManager->GetCachedMeta(path)) {
                                ImGui::Text("Triangles: %zu", cachedMeta->TriangleCount);
                                ImGui::Text("Vertices: %zu", cachedMeta->VertexCount);
                                ImGui::Text("Dimensions: %.2fm x %.2fm x %.2fm", 
                                    cachedMeta->BoundingSize.x, cachedMeta->BoundingSize.y, cachedMeta->BoundingSize.z);
                                if (cachedMeta->SubPartCount > 1) {
                                    ImGui::Text("Subparts: %zu components", cachedMeta->SubPartCount);
                                }
                            }
                        }

                        if (!isDir) {
                            std::error_code ec;
                            auto sz = std::filesystem::file_size(path, ec);
                            if (!ec) {
                                if (sz < 1024) {
                                    ImGui::Text("File Size: %llu B", sz);
                                } else if (sz < 1024 * 1024) {
                                    ImGui::Text("File Size: %.1f KB", sz / 1024.0f);
                                } else {
                                    ImGui::Text("File Size: %.2f MB", sz / (1024.0f * 1024.0f));
                                }
                            }
                        }

                        std::error_code relEc;
                        auto rel = std::filesystem::relative(path, m_RootDirectory, relEc);
                        if (!relEc) {
                            ImGui::TextDisabled("Path: %s", rel.string().c_str());
                        }

                        ImGui::EndTooltip();
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            // Drag & Drop Target: Accept SCENE_HIERARCHY_ENTITY to package as Prefab Assembly (.axle)
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
                    uint32_t droppedId = *static_cast<const uint32_t*>(payload->Data);
                    if (m_OnPackageEntityCallback) {
                        m_OnPackageEntityCallback(droppedId, m_CurrentDirectory);
                        RefreshThumbnails();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Left click on blank space in Content Browser -> deselect
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
                m_SelectedEntry.clear();
                if (m_OnSelectAssetCallback) {
                    m_OnSelectAssetCallback({});
                }
            }

            // Right-click context menu on blank space
            if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                if (ImGui::BeginMenu("Create")) {
                    if (ImGui::MenuItem("New Folder")) {
                        auto p = CreateNewFolder(m_CurrentDirectory);
                        SetStatusNotification("Created Folder: " + p.filename().string());
                        RefreshThumbnails();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Scene (.axlescene)")) {
                        auto p = CreateNewScene(m_CurrentDirectory);
                        SetStatusNotification("Created Scene: " + p.filename().string());
                        RefreshThumbnails();
                    }
                    if (ImGui::MenuItem("Material Asset (.axleasset)")) {
                        auto p = CreateNewMaterial(m_CurrentDirectory);
                        SetStatusNotification("Created Material Asset: " + p.filename().string());
                        if (m_OnSelectAssetCallback) {
                            m_OnSelectAssetCallback(p);
                        }
                        RefreshThumbnails();
                    }
                    if (ImGui::MenuItem("Texture Asset (.axleasset)")) {
                        auto p = CreateNewTexture(m_CurrentDirectory);
                        SetStatusNotification("Created Texture Asset: " + p.filename().string());
                        if (m_OnSelectAssetCallback) {
                            m_OnSelectAssetCallback(p);
                        }
                        RefreshThumbnails();
                    }
                    if (ImGui::MenuItem("VFX Asset (.axleasset)")) {
                        auto p = CreateNewVFXAsset(m_CurrentDirectory);
                        SetStatusNotification("Created VFX Asset: " + p.filename().string());
                        if (m_OnSelectAssetCallback) {
                            m_OnSelectAssetCallback(p);
                        }
                        RefreshThumbnails();
                    }
                    if (ImGui::MenuItem("C++ Script (Header-Only .hpp)")) {
                        auto p = CreateNewScript(m_CurrentDirectory);
                        SetStatusNotification("Created C++ Script: " + p.filename().string());
                        RefreshThumbnails();
                    }
                    if (ImGui::MenuItem("C++ Class (.hpp + .cpp)")) {
                        auto [hppP, cppP] = CreateNewClass(m_CurrentDirectory);
                        SetStatusNotification("Created C++ Class: " + hppP.stem().string() + " (.hpp & .cpp)");
                        RefreshThumbnails();
                    }
                    if (ImGui::MenuItem("Physics Material (.physmat)")) {
                        auto p = CreateNewPhysicsMaterial(m_CurrentDirectory);
                        SetStatusNotification("Created Physics Material: " + p.filename().string());
                        RefreshThumbnails();
                    }
                    if (ImGui::MenuItem("Assembly (.axle)")) {
                        std::filesystem::path newAsmPath = m_CurrentDirectory / "New_Assembly.axle";
                        int counter = 1;
                        while (std::filesystem::exists(newAsmPath)) {
                            newAsmPath = m_CurrentDirectory / ("New_Assembly_" + std::to_string(counter++) + ".axle");
                        }
                        if (AssemblySerializer::CreateEmptyAssembly(newAsmPath, newAsmPath.stem().string())) {
                            AssetManager::GetOrCreateMetadata(newAsmPath);
                            SetStatusNotification("Created Assembly: " + newAsmPath.filename().string());
                            if (m_OnOpenAssemblyCallback) {
                                m_OnOpenAssemblyCallback(newAsmPath);
                            }
                            RefreshThumbnails();
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();

                if (ImGui::MenuItem("Open in System Explorer")) {
#ifdef _WIN32
                    ShellExecuteW(nullptr, L"open", m_CurrentDirectory.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
#endif
                }
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();

        // Status bar
        RenderStatusBar(directories.size(), files.size());
    }

}
