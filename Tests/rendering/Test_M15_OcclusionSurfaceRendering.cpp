#include "OcclusionTestSupport.hpp"
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/MaterialSurfaceShader.hpp>
#include <Axle/RenderContracts.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <fstream>

using namespace Axle;
using namespace Axle::RHI;
using Axle::RenderTests::Read;
using Axle::RenderTests::Upload;

bool RunM15OcclusionSurfaceTests(RHISceneRenderer& renderer, const std::filesystem::path& output) {
    constexpr uint32_t size = 64;
    auto targets = std::make_shared<RHIViewTargets>();
    RHIViewTargetDesc desc;
    desc.Width = desc.Height = size; desc.HDR = true;
    desc.EnableNormal = desc.EnableGTAO = desc.EnableScreenStorage = true;
    RENDER_CHECK(targets->Configure(desc), "U4-E view targets");
    PerspectiveCamera camera(55, 1, .1f, 100);
    camera.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 5), glm::vec3(0), glm::vec3(0, 1, 0)), {0, 0, 5});
    SceneRenderSettings settings;
    settings.EnableParticles = settings.EnableShadows = settings.EnableSkybox = false;
    settings.EnableSubsurfaceScattering = false;
    settings.EnableToneMapping = settings.EnableGTAO = settings.EnableContactShadows = true;
    RenderWorld world;
    world.DirectionalLight = RenderDirectionalLight{};
    world.DirectionalLight->Direction = {0, 0, 1};
    world.DirectionalLight->AmbientIntensity = 0;
    RHIMaterialDesc materialDesc;
    materialDesc.CullMode = CullMode::None; materialDesc.BaseColor = {.6f, .4f, .2f, 1};
    auto material = std::make_shared<RHIMaterial>(materialDesc);
    auto graphMaterial = std::make_shared<RHIMaterial>(materialDesc);
    const uint8_t encoded[]{192, 128, 255, 255};
    const uint8_t grazingEncoded[]{255, 128, 255, 255};
    auto map = std::make_shared<RHITexture>();
    auto grazingMap = std::make_shared<RHITexture>();
    auto mapDesc = RHITextureDesc::RenderTarget2D(1, 1);
    mapDesc.Usage = RHITextureUsage::ShaderResource;
    RENDER_CHECK(map->Init(mapDesc) && Upload(*map, encoded, 4) &&
        grazingMap->Init(mapDesc) && Upload(*grazingMap, grazingEncoded, 4), "real normal texture upload");
    material->SetNormalTexture(map);
    // The canonical graph loads the identical linear sample through its real
    // reflected texture/sampler providers, not a constant-normal stand-in.
    {
        for (bool grazing : {false, true}) {
            std::ofstream tga(output / (grazing ? "u4e-grazing.tga" : "u4e-normal.tga"), std::ios::binary);
            const auto* value = grazing ? grazingEncoded : encoded;
            const uint8_t header[]{0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,32,40};
            const uint8_t bgra[]{value[2], value[1], value[0], value[3]};
            tga.write(reinterpret_cast<const char*>(header), sizeof(header));
            tga.write(reinterpret_cast<const char*>(bgra), sizeof(bgra));
            RENDER_CHECK(bool(tga), "normal-map source fixture");
        }
        std::ofstream graph(output / "u4e-normal.hlsl");
        graph << AssembleMaterialSurfaceShader(
            "cbuffer MapParameters : register(b2,space0) { float4 u_MapSettings; };\n"
            "// [TextureColorSpace(Linear)] [TextureSampler(u_MapSampler, LinearClamp)]\n"
            "Texture2D u_TestNormal : register(t0,space1);\nSamplerState u_MapSampler : register(s0,space1);\n",
            "MaterialSurface AxleEvaluateMaterial(float2 uv) { MaterialSurface s=(MaterialSurface)0; "
            "s.BaseColor=float3(.6,.4,.2); s.Roughness=.5; s.AO=1; s.IOR=1.5; "
            "s.Emission=float3(.7,.2,.1)*u_MapSettings.y; "
            "float2 xy=u_TestNormal.Sample(u_MapSampler,uv).rg*2-1; "
            "s.Normal=normalize(float3(xy*u_MapSettings.x,max(sqrt(saturate(1-dot(xy,xy))),1e-4))); return s; }");
        RENDER_CHECK(bool(graph), "canonical mapped material source");
    }
    auto mesh = std::make_shared<RHIMesh>();
    RenderMeshInstance instance;
    instance.EntityID = 4101; instance.Material.RHIMaterialAsset = material;
    world.Meshes.push_back(instance);
    const std::vector<uint32_t> indices{0,1,2,0,2,3};
    auto publish = [&](float offset, float bend, bool degenerate = false) {
        std::vector<RHIVertex3D> vertices(4);
        const glm::vec2 corners[]{{-1,-1},{1,-1},{1,1},{-1,1}};
        for (size_t i = 0; i < vertices.size(); ++i) {
            auto& v = vertices[i]; const auto p = corners[i];
            v.Position = {p.x + offset, p.y, bend * p.x};
            v.Normal = glm::normalize(glm::vec3(-bend, 0, 1));
            v.Tangent = degenerate ? glm::vec4(v.Normal, 1) : glm::vec4(glm::normalize(glm::vec3(1, 0, bend)), 1);
            v.TexCoord = (p + 1.0f) * .5f;
        }
        RENDER_CHECK(mesh->Init(vertices, indices, "U4EDeformedGeneration"), "publish deformed mesh generation");
        world.Meshes[0].NativeView = mesh->CaptureView();
        return true;
    };
    std::ofstream evidence(output / "M15_U4E_surfaces.txt");
    RENDER_CHECK(evidence.is_open(), "U4-E evidence");
    std::vector<glm::vec4> hdr, normals, ao, aoResolved, depth;
    auto capture = [&](const std::string& name) {
        std::ofstream image(output / ("M15_U4E_" + name + ".ppm"), std::ios::binary);
        image << "P6\n" << size << ' ' << size << "\n255\n";
        for (auto p : hdr) for (int c = 0; c < 3; ++c) {
            const float x = std::max(p[c], 0.0f);
            const float mapped = std::clamp(x * (2.51f*x+.03f) / (x * (2.43f*x+.59f)+.14f), 0.0f, 1.0f);
            const auto value = static_cast<uint8_t>(std::lround(std::pow(mapped, 1.0f/2.2f)*255));
            image.write(reinterpret_cast<const char*>(&value), 1);
        }
        return bool(image);
    };
    auto render = [&](bool readGuides = true) {
        desc.EnableNormal = desc.EnableGTAO = desc.EnableScreenStorage = settings.EnableGTAO || settings.EnableContactShadows;
        if (!targets->IsValid() || targets->GetDesc() != desc)
            RENDER_CHECK(targets->Configure(desc), "view allocation follows the current occlusion settings");
        renderer.RenderWorldView(world, camera, targets->Get(RHIViewTarget::Color), targets->Get(RHIViewTarget::Depth), settings, nullptr, targets);
        for (const auto& error : renderer.GetDiagnostics().GetFrame().Errors) std::cerr << "U4-E renderer: " << error << '\n';
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors(), "U4-E actual renderer frame");
        RENDER_CHECK(Read(*targets->Get(RHIViewTarget::HDR), hdr), "U4-E pre-tone-map HDR readback");
        for (auto p : hdr) for (int c = 0; c < 4; ++c) RENDER_CHECK(std::isfinite(p[c]), "finite mapped/deformed HDR RGBA");
        if (readGuides) {
            RENDER_CHECK(Read(*targets->Get(RHIViewTarget::ScreenNormal), normals) &&
                Read(*targets->GetGTAO(), ao) && targets->GetGTAOResolved() &&
                Read(*targets->GetGTAOResolved(), aoResolved) &&
                Read(*targets->Get(RHIViewTarget::OpaqueDepth), depth), "U4-E actual normal/visibility/depth readbacks");
            for (auto p : normals) for (int c = 0; c < 4; ++c) RENDER_CHECK(std::isfinite(p[c]), "finite mapped ScreenNormal");
            for (auto p : ao) RENDER_CHECK(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1, "bounded mapped AO/contact");
            for (auto p : aoResolved) RENDER_CHECK(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1, "bounded full-res resolved AO/contact");
            for (auto p : depth) RENDER_CHECK(std::isfinite(p.x) && p.x >= 0 && p.x <= 1, "finite deformed depth");
        }
        if (world.Meshes[0].Material.UseCustomShader) {
            const auto& diagnostics = renderer.GetDiagnostics().GetFrame().Materials;
            RENDER_CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& d) {
                return d.EntityID == 4101 && d.Evaluation == SurfaceEvaluation::CustomShader && d.Valid && !d.Fallback && d.ShaderGeneration != 0;
            }), "canonical normal-map pipeline selected without fallback");
        }
        return true;
    };
    auto compare = [&](const std::vector<glm::vec4>& reference) {
        float error = 0;
        for (size_t i = 0; i < hdr.size(); ++i) for (int c = 0; c < 3; ++c)
            error = std::max(error, std::abs(hdr[i][c] - reference[i][c]));
        evidence << " radiance_error=" << error << '\n';
        return error < .003f;
    };
    std::vector<std::vector<glm::vec4>> builtinFrames;
    for (bool graph : {false, true}) {
        auto& data = world.Meshes[0].Material;
        data.UseCustomShader = graph; data.CustomShaderPath = "u4e-normal.hlsl";
        data.RHIMaterialAsset = graph ? graphMaterial : material;
        float flatRadiance = 0;
        size_t strengthIndex = 0;
        for (const float strength : {0.0f, 1.0f, 3.0f, 4.0f}) {
            RENDER_CHECK(publish(0, 0), "flat mapped mesh");
            material->SetNormalStrength(strength);
            material->SetNormalTexture(strength == 4 ? grazingMap : map);
            ShaderPropertyInstance parameter, texture;
            parameter.Desc.Name = "u_MapSettings"; parameter.Desc.ConstantBufferName = "MapParameters";
            parameter.Desc.Type = ShaderPropertyType::Float4; parameter.Vec4Val = {strength,0,0,0};
            texture.Desc.Name = "u_TestNormal"; texture.Desc.Type = ShaderPropertyType::Texture2D;
            texture.TexturePath = strength == 4 ? "u4e-grazing.tga" : "u4e-normal.tga";
            data.CustomProperties = {parameter, texture};
            settings.EnableGTAO = true;
            RENDER_CHECK(render(), "mapped prepass and occlusion");
            const auto* value = strength == 4 ? grazingEncoded : encoded;
            const auto xy = glm::vec2(value[0], value[1]) / 255.0f * 2.0f - 1.0f;
            const auto expected = glm::normalize(glm::vec3(xy * strength, std::max(std::sqrt(std::max(0.0f, 1-glm::dot(xy,xy))), 1e-4f)));
            const auto n = normals[size / 2 * size + size / 2];
            evidence << "graph=" << graph << " strength=" << strength << " normal_error=" << glm::distance(glm::vec3(n), expected);
            RENDER_CHECK(n.w == 1 && glm::distance(glm::vec3(n), expected) < .002f, "actual ScreenNormal decodes the enabled linear texture with authored strength");
            if (strength == 4) {
                size_t backfacing = 0;
                const auto inverse = glm::inverse(camera.GetProjectionMatrixZO());
                for (uint32_t y = 0; y < size / 2; ++y) for (uint32_t x = 0; x < size / 2; ++x) {
                    const uint32_t sx = 2*x+1, sy = 2*y+1;
                    const auto index = sy * size + sx;
                    if (normals[index].w != 1) continue;
                    const auto viewH = inverse * glm::vec4(2*(sx+.5f)/size-1, 1-2*(sy+.5f)/size, depth[index].x, 1);
                    const auto V = -glm::normalize(glm::vec3(viewH) / viewH.w);
                    if (glm::dot(glm::vec3(normals[index]), V) <= 0) {
                        ++backfacing;
                        RENDER_CHECK(ao[y * (size / 2) + x].x == 1, "actual back-facing mapped normals use neutral AO");
                    }
                }
                evidence << " backfacing_samples=" << backfacing << '\n';
                RENDER_CHECK(backfacing > 50, "strong normal map actually crosses the view hemisphere");
            }
            const auto reference = hdr;
            RENDER_CHECK(capture(std::string(graph ? "graph" : "builtin") + "-strength" + std::to_string(int(strength))), "mapped HDR display artifact");
            if (strength < 4) RENDER_CHECK(reference[size / 2 * size + size / 2].x > .01f, "mapped direct lighting is nonzero");
            const float center = reference[size / 2 * size + size / 2].x;
            if (strength == 0) flatRadiance = center;
            else RENDER_CHECK(std::abs(center - flatRadiance) > .01f, "real forward radiance changes when the normal map perturbs the surface");
            evidence << " center_hdr=" << center << '\n';
            if (!graph) builtinFrames.push_back(reference);
            else if (strength < 4) RENDER_CHECK(compare(builtinFrames[strengthIndex]), "canonical mapped color matches built-in for the same front-facing decoded normal");
            ++strengthIndex;
            settings.EnableGTAO = false;
            RENDER_CHECK(render() && compare(reference), "AO does not change actual mapped direct radiance");
        }
        settings.EnableGTAO = true;
        RENDER_CHECK(publish(0, 0, true) && render(), "normal-map tangent degeneracy fallback");
        RENDER_CHECK(normals[size / 2 * size + size / 2].w == 1, "degenerate tangent still writes valid coverage");
        RENDER_CHECK(publish(0, 0), "restore tangent for mirrored transform");
        world.Meshes[0].WorldTransform = glm::scale(glm::mat4(1), glm::vec3(-1.2f, .8f, 1.5f));
        RENDER_CHECK(render(), "mapped mirrored nonuniform model");
        const auto xy = glm::vec2(grazingEncoded[0], grazingEncoded[1]) / 255.0f * 2.0f - 1.0f;
        const auto expectedMirror = glm::normalize(glm::vec3(-xy.x * 4, xy.y * 4, 1e-4f));
        RENDER_CHECK(glm::distance(glm::vec3(normals[size / 2 * size + size / 2]), expectedMirror) < .002f, "mapped mirror preserves tangent handedness");
        world.Meshes[0].WorldTransform = glm::mat4(1);
        const auto sun = world.DirectionalLight;
        world.DirectionalLight.reset();
        material->GetDesc().EmissiveColor = {.7f,.2f,.1f}; material->GetDesc().EmissiveIntensity = 2;
        for (auto& property : data.CustomProperties) if (property.Desc.Name == "u_MapSettings") property.Vec4Val.y = 2;
        RENDER_CHECK(render(), "actual mapped emissive with AO/contact enabled");
        const auto emission = hdr;
        RENDER_CHECK(emission[size / 2 * size + size / 2].x > 1, "mapped emission is nonzero HDR");
        settings.EnableGTAO = settings.EnableContactShadows = false;
        RENDER_CHECK(render(false) && compare(emission), "mapped authored emission survives actual AO/contact toggles");
        settings.EnableGTAO = settings.EnableContactShadows = true;
        world.DirectionalLight = sun;
        material->GetDesc().EmissiveColor = {}; material->GetDesc().EmissiveIntensity = 0;
    }
    world.Meshes[0].Material.UseCustomShader = false;
    world.Meshes[0].Material.RHIMaterialAsset = material;
    material->SetNormalTexture(nullptr);
    material->SetTexture(RHITextureSlot::DetailNormal, map);
    material->GetDesc().DetailNormalStrength = 1;
    const auto detailXY = glm::vec2(encoded[0], encoded[1]) / 255.0f * 2.0f - 1.0f;
    const auto detailNormal = glm::vec3(detailXY, std::sqrt(1-glm::dot(detailXY,detailXY)));
    for (bool masked : {false, true}) {
        material->SetTexture(RHITextureSlot::DetailMask, masked ? RHIMaterial::GetDefaultBlackTexture() : RHIMaterial::GetDefaultWhiteTexture());
        RENDER_CHECK(render(), "actual detail normal and mask prepass");
        const auto expected = masked ? glm::vec3(0,0,1) : detailNormal;
        const float error = glm::distance(glm::vec3(normals[size / 2 * size + size / 2]), expected);
        evidence << "detail_mask_zero=" << masked << " normal_error=" << error << '\n';
        RENDER_CHECK(error < .002f, "detail normal/mask use the same texture registers as ForwardPBR");
    }
    material->SetTexture(RHITextureSlot::DetailNormal, nullptr);
    material->SetTexture(RHITextureSlot::DetailMask, nullptr);
    material->SetNormalTexture(map);
    material->SetNormalStrength(0);
    settings.EnableGTAO = true;
    world.DirectionalLight->AmbientIntensity = .3f;
    auto background = std::make_shared<RHIMesh>();
    std::vector<RHIVertex3D> rear(4);
    const glm::vec2 rearCorners[]{{-5,-5},{5,-5},{5,5},{-5,5}};
    for (size_t i = 0; i < rear.size(); ++i) { rear[i].Position = glm::vec3(rearCorners[i], -1); rear[i].Normal = {0,0,1}; }
    RENDER_CHECK(background->Init(rear, indices, "U4EReceiver"), "real rear receiver geometry");
    RenderMeshInstance receiver;
    auto receiverDesc = materialDesc; receiverDesc.BaseColor = {.2f,.4f,.8f,1};
    receiver.EntityID = 4102; receiver.NativeView = background->CaptureView(); receiver.Material.RHIMaterialAsset = std::make_shared<RHIMaterial>(receiverDesc);
    world.Meshes.push_back(receiver);
    // Changing mesh generations moves a real silhouette. Compare every pixel
    // against a fresh view generation, including the newly revealed background.
    std::vector<glm::vec4> previousNormals;
    size_t disoccluded = 0;
    const auto projection = camera.GetProjectionMatrixZO();
    for (const float offset : {-1.0f, 1.0f, -1.0f}) {
        RENDER_CHECK(publish(offset, .35f), "deformed mesh pose");
        const auto previousTargetGeneration = targets->GetGeneration();
        RENDER_CHECK(render(), "deformed actual prepass");
        if (!previousNormals.empty()) RENDER_CHECK(targets->GetGeneration() == previousTargetGeneration, "moving pose actually reuses the previous view target generation");
        RENDER_CHECK(capture(offset < 0 ? "deformed-left" : "deformed-right"), "deformed HDR display artifact");
        size_t covered = 0;
        for (uint32_t y = 0; y < size; ++y) for (uint32_t x = 0; x < size; ++x) {
            const auto i = y * size + x;
            const bool foreground = normals[i].x < -.2f;
            if (!previousNormals.empty() && previousNormals[i].x < -.2f && !foreground) ++disoccluded;
            const auto ndc = glm::vec4(2 * (x+.5f)/size-1, 1-2*(y+.5f)/size, depth[i].x, 1);
            const auto viewH = glm::inverse(projection) * ndc;
            const auto p = glm::vec3(viewH) / viewH.w + glm::vec3(0,0,5);
            if (foreground) {
                ++covered;
                RENDER_CHECK(std::abs(p.z - .35f*(p.x-offset)) < .001f, "prepass depth reconstructs the current deformed plane");
                RENDER_CHECK(glm::distance(glm::vec3(normals[i]), glm::normalize(glm::vec3(-.35f,0,1))) < .002f, "deformed ScreenNormal matches current geometry");
            } else RENDER_CHECK(normals[i].w == 1 && std::abs(p.z + 1) < .001f, "disoccluded depth belongs to the real rear receiver");
        }
        RENDER_CHECK(covered > 400, "deformed fixture is actually drawn");
        const auto reference = hdr, oldAO = ao, oldResolvedAO = aoResolved;
        float minimumAO = 1;
        for (auto p : ao) minimumAO = std::min(minimumAO, p.x);
        RENDER_CHECK(minimumAO < .95f, "moving mesh genuinely occludes the rear receiver");
        previousNormals = normals;
        targets = std::make_shared<RHIViewTargets>();
        RENDER_CHECK(targets->Configure(desc) && render() && compare(reference), "current deformed image matches a fresh view without stale occlusion");
        for (size_t i = 0; i < ao.size(); ++i) RENDER_CHECK(glm::distance(glm::vec2(ao[i]), glm::vec2(oldAO[i])) < .001f, "no stale AO/contact after disocclusion");
        for (size_t i = 0; i < aoResolved.size(); ++i) RENDER_CHECK(glm::distance(glm::vec2(aoResolved[i]), glm::vec2(oldResolvedAO[i])) < .001f, "no stale full-res resolved AO/contact after disocclusion");
        evidence << "offset=" << offset << " covered=" << covered << " disoccluded=" << disoccluded << " min_ao=" << minimumAO << '\n';
    }
    RENDER_CHECK(disoccluded > 400, "animated sequence really reveals previously covered pixels");
    RENDER_CHECK(GetRHIValidationErrorCount() == 0, "U4-E native validation");
    return true;
}
