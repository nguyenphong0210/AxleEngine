#include "OcclusionTestSupport.hpp"
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHI/RHIDescriptorPool.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIVertexLayout.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/MaterialSurfaceShader.hpp>
#include <Axle/RenderContracts.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace Axle;
using namespace Axle::RHI;

using Axle::RenderTests::Upload;
using Axle::RenderTests::Read;


bool RunM15OcclusionHorizonTests(const std::filesystem::path& output) {
    auto& device = RHIDevice::Get();
    std::ifstream file(AXLE_GTAO_SHADER_PATH);
    RHIShaderCompileDesc shader;
    shader.Source = std::string(std::istreambuf_iterator<char>(file), {});
    shader.SourcePath = AXLE_GTAO_SHADER_PATH;
    shader.EntryPoint = "CSMain"; shader.Stage = RHIShaderStage::Compute;
    shader.Format = device.GetBackend() == RHIType::Vulkan ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
    const auto compiled = RHIShaderCompiler::Get().Compile(shader);
    RENDER_CHECK(compiled.Success, "production GTAO horizon shader");
    nri::RootDescriptorDesc root{};
    root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER; root.shaderStages = nri::StageBits::COMPUTE_SHADER;
    nri::DescriptorRangeDesc inputs[3]{}, storage{};
    for (uint32_t i = 0; i < 3; ++i) {
        inputs[i].baseRegisterIndex = i < 2 ? i : 0;
        inputs[i].descriptorNum = 1; inputs[i].shaderStages = nri::StageBits::COMPUTE_SHADER;
        inputs[i].descriptorType = i < 2 ? nri::DescriptorType::TEXTURE : nri::DescriptorType::SAMPLER;
    }
    storage.descriptorNum = 1; storage.shaderStages = nri::StageBits::COMPUTE_SHADER;
    storage.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
    nri::DescriptorSetDesc sets[2]{};
    sets[0].registerSpace = 1; sets[0].ranges = inputs; sets[0].rangeNum = 3;
    sets[1].registerSpace = 2; sets[1].ranges = &storage; sets[1].rangeNum = 1;
    nri::PipelineLayoutDesc description{};
    description.rootDescriptors = &root; description.rootDescriptorNum = 1;
    description.descriptorSets = sets; description.descriptorSetNum = 2; description.shaderStages = nri::StageBits::COMPUTE_SHADER;
    RHIPipelineLayout layout; RHIPipelineCache cache; RHISampler sampler; RHIBuffer constants;
    RENDER_CHECK(layout.Init(description) && sampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge)) &&
        constants.Init(RHIBufferDesc::Constant(sizeof(RHIGTAOConstants), RHIMemoryLocation::HostUpload)), "horizon fixture resources");
    RHIComputePipelineDesc pipelineDescription;
    pipelineDescription.Layout = &layout; pipelineDescription.ComputeShader = compiled.Bytecode;
    auto* pipeline = cache.GetOrCreateComputePipeline(pipelineDescription);
    RENDER_CHECK(pipeline, "production horizon compute pipeline");
    std::ofstream evidence(output / "M15_U4D_horizons.txt");
    RENDER_CHECK(evidence.is_open(), "horizon evidence file");
    constexpr uint32_t size = 64, half = size / 2;
    for (const int mode : {0, 1, 2, 3}) {
        std::vector<float> depths(size * size);
        std::vector<glm::vec4> normals(size * size, glm::vec4(mode == 1 ? glm::normalize(glm::vec3(.4f, 0, 1)) : glm::vec3(0, 0, 1), 1));
        for (uint32_t y = 0; y < size; ++y) for (uint32_t x = 0; x < size; ++x)
            depths[y * size + x] = mode == 3 ? 1 : mode == 2 && x < size / 2 ? .1f :
                mode == 1 ? (2 + .4f * (2 * (x + .5f) / size - 1)) / 10 : .2f;
        RHITexture depth, normal, result;
        auto input = RHITextureDesc::RenderTarget2D(size, size, RHITextureFormat::R32_FLOAT);
        input.Usage = RHITextureUsage::ShaderResource;
        RENDER_CHECK(depth.Init(input), "horizon depth");
        input.Format = RHITextureFormat::RGBA32_FLOAT;
        RENDER_CHECK(normal.Init(input) && result.Init(RHITextureDesc::StorageTexture2D(half, half, RHITextureFormat::RGBA16_FLOAT)) &&
            Upload(depth, depths.data(), 4) && Upload(normal, normals.data(), 16), "horizon inputs");
        RHIGTAOConstants values{};
        values.InvProjection[2][2] = -10;
        values.ScreenSize = {half, half, 1.0f / half, 1.0f / half};
        values.GTAOParams = {8, 1, .0025f, 2};
        RENDER_CHECK(constants.UpdateData(&values, sizeof(values)), "horizon constants");
        RHIDescriptorPool pool;
        RENDER_CHECK(pool.Init(), "horizon descriptor pool");
        auto* textures = pool.AllocateDescriptorSet(layout, 0);
        auto* outputSet = pool.AllocateDescriptorSet(layout, 1);
        RENDER_CHECK(textures && outputSet && pool.UpdateTexture(textures, 0, 0, depth.GetSRV()) &&
            pool.UpdateTexture(textures, 1, 0, normal.GetSRV()) && pool.UpdateSampler(textures, 2, 0, sampler.GetNative()) &&
            pool.UpdateStorageTexture(outputSet, 0, 0, result.GetUAV()), "horizon bindings");
        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init() && commands.Begin(), "horizon recording");
        commands.TransitionTexture(result.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResourceStorage);
        commands.SetPipeline(pipeline); commands.SetPipelineLayout(nri::BindPoint::COMPUTE, layout);
        commands.SetRootDescriptor(0, constants.GetCBV(), 0, nri::BindPoint::COMPUTE);
        pool.Bind(commands.GetNative());
        commands.SetDescriptorSet(nri::BindPoint::COMPUTE, 0, textures);
        // This standalone fixture has no render-graph access declarations.
        // The generic read-only set helper would transition the UAV to SRV.
        nri::SetDescriptorSetDesc outputBinding{};
        outputBinding.bindPoint = nri::BindPoint::COMPUTE;
        outputBinding.setIndex = 1; outputBinding.descriptorSet = outputSet;
        device.GetNRICore().CmdSetDescriptorSet(*commands.GetNative(), outputBinding);
        commands.Dispatch(half / 8, half / 8, 1);
        std::vector<glm::vec4> pixels;
        RENDER_CHECK(commands.End() && commands.Submit() && device.WaitIdle() && Read(result, pixels), "horizon GPU readback");
        float minimum = 1;
        for (uint32_t y = 0; y < half; ++y) for (uint32_t x = 0; x < half; ++x) {
            const auto p = pixels[y * half + x];
            RENDER_CHECK(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.x <= 1 && p.y == 1, "bounded AO and neutral no-sun contact");
            if ((mode != 2 || x < half / 2 || x >= 28) && p.x != 1) {
                evidence << "nonneutral mode=" << mode << " x=" << x << " y=" << y << " ao=" << p.x << " contact=" << p.y << '\n';
                evidence.flush();
                RENDER_CHECK(false, "open plane, foreground and background are exactly neutral");
            }
            minimum = std::min(minimum, p.x);
        }
        evidence << "mode=" << mode << " min_ao=" << minimum << '\n';
        if (mode == 2) RENDER_CHECK(minimum < .9f, "real foreground blocker occludes the rear plane; AO is not hardcoded neutral");
    }
    return true;
}

// An isolated convex sphere cannot occlude itself. Read linear HDR, rather
// than checking that a tone-mapped byte happens to be nonzero.
bool RunM15OcclusionSphereTests(RHISceneRenderer& renderer, const RenderWorld& world,
                               const PerspectiveCamera& camera, SceneRenderSettings settings,
                               const std::filesystem::path& output) {
    constexpr uint32_t size = 128;
    RHITexture color, depth;
    RENDER_CHECK(color.Init(RHITextureDesc::RenderTarget2D(size, size, RHITextureFormat::RGBA16_FLOAT)) &&
        depth.Init(RHITextureDesc::DepthStencil2D(size, size)), "sphere HDR targets");
    settings.EnableToneMapping = true; // Select scene-linear forward shading.
    settings.EnableFXAA = settings.EnableTAA = false;
    settings.EnableGTAO = settings.EnableContactShadows = false;
    std::ofstream evidence(output / "M15_U4D_sphere.txt");
    RENDER_CHECK(evidence.is_open(), "sphere evidence file");
    auto render = [&](const char* name, std::vector<glm::vec4>& pixels) {
        renderer.RenderWorldView(world, camera, &color, &depth, settings);
        auto* hdr = renderer.GetIntermediateHDRTarget();
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors() && hdr && Read(*hdr, pixels), "sphere production HDR render before tone mapping");
        std::ofstream image(output / (std::string("M15_U4D_sphere-") + name + ".ppm"), std::ios::binary);
        image << "P6\n" << size << ' ' << size << "\n255\n";
        for (auto pixel : pixels) {
            for (int c = 0; c < 4; ++c) RENDER_CHECK(std::isfinite(pixel[c]), "finite sphere HDR including alpha");
            for (int c = 0; c < 3; ++c) {
                const float x = std::max(pixel[c], 0.0f);
                const float mapped = std::clamp((x * (2.51f * x + .03f)) / (x * (2.43f * x + .59f) + .14f), 0.0f, 1.0f);
                const auto byte = static_cast<unsigned char>(std::lround(std::pow(mapped, 1.0f / 2.2f) * 255));
                image.write(reinterpret_cast<const char*>(&byte), 1);
            }
        }
        RENDER_CHECK(bool(image), "sphere image artifact");
        return true;
    };
    std::vector<glm::vec4> reference, actual;
    RENDER_CHECK(render("off", reference), "sphere reference");
    float minimum = 1e9f, maximum = 0;
    for (uint32_t y = 44; y < 84; ++y) for (uint32_t x = 44; x < 84; ++x) {
        minimum = std::min(minimum, reference[y * size + x].x);
        maximum = std::max(maximum, reference[y * size + x].x);
    }
    RENDER_CHECK(minimum > 0.05f && maximum > 1.0f && maximum - minimum > 0.1f, "sphere linear HDR reference exceeds one and has a visible reflection gradient");
    for (const auto mode : {1, 2, 3, 0}) {
        settings.EnableGTAO = (mode & 1) != 0;
        settings.EnableContactShadows = (mode & 2) != 0;
        RENDER_CHECK(render(mode == 1 ? "ao" : mode == 2 ? "contact" : mode == 3 ? "both" : "disabled-again", actual), "sphere toggle fixture");
        float worstLoss = 0, maxError = 0;
        size_t samples = 0;
        // Avoid the silhouette where screen-space depth becomes undersampled.
        for (int y = 0; y < int(size); ++y) for (int x = 0; x < int(size); ++x) {
            const float dx = x + .5f - size * .5f, dy = y + .5f - size * .5f;
            if (dx * dx + dy * dy > 20 * 20) continue;
            const auto i = y * size + x;
            for (int c = 0; c < 3; ++c) {
                worstLoss = std::max(worstLoss, 1.0f - actual[i][c] / std::max(reference[i][c], 1e-4f));
                maxError = std::max(maxError, std::abs(actual[i][c] - reference[i][c]));
            }
            ++samples;
        }
        evidence << "mode=" << mode << " pixels=" << samples << " worst_relative_loss=" << worstLoss << " max_hdr_error=" << maxError << '\n';
        RENDER_CHECK(samples > 1200 && worstLoss < 0.05f, "unoccluded mirror reflection loses less than 5 percent with real GTAO");
        if (mode == 2 || mode == 0) RENDER_CHECK(maxError == 0, "absent-sun contact and disabled-again render are exactly neutral");
        if (mode == 3) {
            std::vector<glm::vec4> normals;
            RENDER_CHECK(renderer.GetOpaqueNormalTarget() && Read(*renderer.GetOpaqueNormalTarget(), normals), "sphere normal readback");
            float maxNormalError = 0;
            const float tangent = std::tan(glm::radians(55.0f) * .5f);
            for (int y = 44; y < 84; ++y) for (int x = 44; x < 84; ++x) {
                const float dx = x + .5f - size * .5f, dy = y + .5f - size * .5f;
                if (dx * dx + dy * dy > 20 * 20) continue;
                const auto ray = glm::normalize(glm::vec3(dx / 64 * tangent, -dy / 64 * tangent, -1));
                const float b = 5 * ray.z;
                const float distance = -b - std::sqrt(b * b - 24);
                const auto expected = glm::normalize(glm::vec3(0, 0, 5) + ray * distance);
                const auto normal = normals[y * size + x];
                for (int c = 0; c < 4; ++c) RENDER_CHECK(std::isfinite(normal[c]), "finite sphere normal");
                RENDER_CHECK(normal.w == 1, "sphere interior has surface normal data");
                maxNormalError = std::max(maxNormalError, glm::distance(glm::vec3(normal), expected));
            }
            evidence << "max_analytic_normal_error=" << maxNormalError << '\n';
            RENDER_CHECK(maxNormalError < 0.02f, "smooth sphere normals match analytic curvature across the interior");
        }
    }
    RENDER_CHECK(GetRHIValidationErrorCount() == 0, "sphere fixture native validation");

    // M15-U4-GTAO-HALO-FIX: Sphere-on-floor halo regression (actual prepass + compute + normal-depth CSUpsample + ForwardPBR).
    {
        auto floorMesh = std::make_shared<RHIMesh>();
        std::vector<RHIVertex3D> floorVerts(4);
        const glm::vec2 corners[]{{-12.0f, -12.0f}, {12.0f, -12.0f}, {12.0f, 12.0f}, {-12.0f, 12.0f}};
        for (size_t k = 0; k < 4; ++k) {
            floorVerts[k].Position = {corners[k].x, -1.0f, corners[k].y};
            floorVerts[k].Normal = {0.0f, 1.0f, 0.0f};
            floorVerts[k].Tangent = {1.0f, 0.0f, 0.0f, 1.0f};
            floorVerts[k].TexCoord = (corners[k] + 12.0f) / 24.0f;
        }
        const std::vector<uint32_t> floorIndices{0, 2, 1, 0, 3, 2};
        RENDER_CHECK(floorMesh->Init(floorVerts, floorIndices, "U4HaloFloor"), "halo regression floor mesh");

        RHIMaterialDesc glossySphereDesc;
        glossySphereDesc.BaseColor = {0.82f, 0.76f, 0.56f, 1.0f};
        glossySphereDesc.Metallic = 0.0f;
        glossySphereDesc.Roughness = 0.25f;
        auto glossySphereMat = std::make_shared<RHIMaterial>(glossySphereDesc);

        RHIMaterialDesc floorMatDesc;
        floorMatDesc.BaseColor = {0.74f, 0.76f, 0.80f, 1.0f};
        floorMatDesc.Metallic = 0.0f;
        floorMatDesc.Roughness = 0.8f;
        floorMatDesc.CullMode = CullMode::None;
        auto floorMat = std::make_shared<RHIMaterial>(floorMatDesc);

        RenderWorld floorWorld = world;
        floorWorld.Meshes[0].Material.RHIMaterialAsset = glossySphereMat;
        RenderMeshInstance floorInst;
        floorInst.EntityID = 4202;
        floorInst.NativeView = floorMesh->CaptureView();
        floorInst.Material.RHIMaterialAsset = floorMat;
        floorWorld.Meshes.push_back(floorInst);

        const glm::vec3 camPos(0.0f, 1.5f, 5.2f);
        PerspectiveCamera floorCamera(55.0f, 1.0f, 0.1f, 100.0f);
        floorCamera.SetViewMatrix(glm::lookAtRH(camPos, glm::vec3(0.0f, -0.25f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)), camPos);

        auto targets = std::make_shared<RHIViewTargets>();
        RHIViewTargetDesc td;
        td.Width = td.Height = size;
        td.HDR = true;

        SceneRenderSettings floorSettings = settings;
        floorSettings.EnableToneMapping = true;
        floorSettings.EnableFXAA = floorSettings.EnableTAA = floorSettings.EnableContactShadows = false;
        std::vector<glm::vec4> hdrOff, hdrOn, resolvedAO, floorNormals, floorDepth;

        floorSettings.EnableGTAO = false;
        td.EnableNormal = td.EnableGTAO = td.EnableScreenStorage = false;
        RENDER_CHECK(targets->Configure(td), "halo regression baseline targets");
        renderer.RenderWorldView(floorWorld, floorCamera, targets->Get(RHIViewTarget::Color), targets->Get(RHIViewTarget::Depth), floorSettings, nullptr, targets);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors() &&
                     Read(*targets->Get(RHIViewTarget::HDR), hdrOff), "halo regression baseline render");

        floorSettings.EnableGTAO = true;
        td.EnableNormal = td.EnableGTAO = td.EnableScreenStorage = true;
        RENDER_CHECK(targets->Configure(td), "halo regression GTAO targets");
        renderer.RenderWorldView(floorWorld, floorCamera, targets->Get(RHIViewTarget::Color), targets->Get(RHIViewTarget::Depth), floorSettings, nullptr, targets);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors() &&
                     targets->GetGTAOResolved() &&
                     Read(*targets->Get(RHIViewTarget::HDR), hdrOn) &&
                     Read(*targets->GetGTAOResolved(), resolvedAO) &&
                     Read(*targets->Get(RHIViewTarget::ScreenNormal), floorNormals) &&
                     Read(*targets->Get(RHIViewTarget::OpaqueDepth), floorDepth),
            "halo regression GTAO render and readbacks");

        for (const auto label : {"off", "ao"}) {
            const auto& img = (label[0] == 'o') ? hdrOff : hdrOn;
            std::ofstream ppm(output / (std::string("M15_U4D_sphere_floor-") + label + ".ppm"), std::ios::binary);
            ppm << "P6\n" << size << ' ' << size << "\n255\n";
            for (const auto& px : img) {
                for (int c = 0; c < 3; ++c) {
                    const float x = std::max(px[c], 0.0f);
                    const float m = std::clamp((x * (2.51f * x + .03f)) / (x * (2.43f * x + .59f) + .14f), 0.0f, 1.0f);
                    const auto b = static_cast<unsigned char>(std::lround(std::pow(m, 1.0f / 2.2f) * 255));
                    ppm.write(reinterpret_cast<const char*>(&b), 1);
                }
            }
        }

        const glm::mat4 invVP = glm::inverse(floorCamera.GetProjectionMatrixZO() * floorCamera.GetViewMatrix());
        float minFloorContactAO = 1.0f;
        float minMidOverhangFloorAO = 1.0f;
        float minLateralFloorHaloAO = 1.0f;
        float minSphereRimAO = 1.0f;
        size_t floorContactCount = 0, midOverhangCount = 0, lateralFloorCount = 0, sphereRimCount = 0;

        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                const size_t idx = size_t(y) * size + x;
                const auto& norm = floorNormals[idx];
                const float d = floorDepth[idx].x;
                const float aoVal = resolvedAO[idx].x;
                RENDER_CHECK(std::isfinite(aoVal) && aoVal >= 0.0f && aoVal <= 1.0f, "finite bounded sphere-floor resolved AO");
                if (norm.w < 0.5f || d <= 0.0f || d >= 0.9999f) continue;

                const glm::vec4 clip(2.0f * (x + 0.5f) / size - 1.0f, 1.0f - 2.0f * (y + 0.5f) / size, d, 1.0f);
                const glm::vec4 wh = invVP * clip;
                const glm::vec3 wp = glm::vec3(wh) / wh.w;
                const float radialXZ = std::hypot(wp.x, wp.z);

                if (norm.y > 0.95f && std::abs(wp.y + 1.0f) < 0.05f) {
                    if (radialXZ < 0.70f) {
                        minFloorContactAO = std::min(minFloorContactAO, aoVal);
                        ++floorContactCount;
                    } else if (radialXZ >= 1.50f && radialXZ < 2.25f) {
                        minMidOverhangFloorAO = std::min(minMidOverhangFloorAO, aoVal);
                        ++midOverhangCount;
                    } else if (radialXZ >= 2.25f && radialXZ <= 3.40f) {
                        minLateralFloorHaloAO = std::min(minLateralFloorHaloAO, aoVal);
                        ++lateralFloorCount;
                    }
                } else if (glm::length(wp) < 1.10f && wp.y > -0.20f) {
                    const float NdotV = glm::dot(glm::normalize(glm::vec3(norm)), glm::normalize(camPos - wp));
                    if (NdotV > 0.02f && NdotV < 0.42f) {
                        minSphereRimAO = std::min(minSphereRimAO, aoVal);
                        ++sphereRimCount;
                    }
                }
            }
        }
        evidence << "sphere_floor contact_samples=" << floorContactCount << " min_contact_ao=" << minFloorContactAO
                 << " mid_samples=" << midOverhangCount << " min_mid_ao=" << minMidOverhangFloorAO
                 << " lateral_samples=" << lateralFloorCount << " min_lateral_ao=" << minLateralFloorHaloAO
                 << " rim_samples=" << sphereRimCount << " min_rim_ao=" << minSphereRimAO << '\n';
        RENDER_CHECK(floorContactCount > 10 && minFloorContactAO < 0.90f && minFloorContactAO > 0.05f,
            "sphere-on-floor base contact exhibits genuine non-crushed AO");
        RENDER_CHECK(midOverhangCount > 20 && minMidOverhangFloorAO >= 0.75f,
            "sphere-on-floor overhang belt fades smoothly without harsh black ring");
        RENDER_CHECK(lateralFloorCount > 20 && minLateralFloorHaloAO >= 0.95f,
            "sphere-on-floor outer floor margin has no 4m-cutoff floating dark halo");
        RENDER_CHECK(sphereRimCount > 20 && minSphereRimAO >= 0.88f,
            "sphere upper/lateral grazing silhouette has no dark rim bleed from floor or self-occlusion");

        // Smoothness / anti-grain check on hovered sphere (0.11m above floor) + glossy floor contact zone:
        floorWorld.Meshes[0].WorldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.11f, 0.0f));
        floorMatDesc.Roughness = 0.15f;
        floorMat = std::make_shared<RHIMaterial>(floorMatDesc);
        floorWorld.Meshes[1].Material.RHIMaterialAsset = floorMat;
        renderer.RenderWorldView(floorWorld, floorCamera, targets->Get(RHIViewTarget::Color), targets->Get(RHIViewTarget::Depth), floorSettings, nullptr, targets);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors() &&
                     Read(*targets->GetGTAOResolved(), resolvedAO) &&
                     Read(*targets->Get(RHIViewTarget::ScreenNormal), floorNormals) &&
                     Read(*targets->Get(RHIViewTarget::OpaqueDepth), floorDepth),
            "hovered sphere glossy-floor smoothness readback");
        double laplacianSum = 0.0;
        float maxLaplacian = 0.0f;
        size_t smoothSamples = 0;
        for (uint32_t y = 2; y + 2 < size; ++y) {
            for (uint32_t x = 2; x + 2 < size; ++x) {
                const size_t idx = size_t(y) * size + x;
                const float d = floorDepth[idx].x;
                if (floorNormals[idx].w < 0.5f || floorNormals[idx].y <= 0.95f || d <= 0.0f || d >= 0.9999f) continue;
                const glm::vec4 clip(2.0f * (x + 0.5f) / size - 1.0f, 1.0f - 2.0f * (y + 0.5f) / size, d, 1.0f);
                const glm::vec4 wh = invVP * clip;
                const glm::vec3 wp = glm::vec3(wh) / wh.w;
                if (std::abs(wp.y + 1.0f) >= 0.05f || std::hypot(wp.x, wp.z) > 1.3f) continue;
                const size_t nL = idx - 1, nR = idx + 1, nU = idx - size, nD = idx + size;
                if (floorNormals[nL].y <= 0.95f || floorNormals[nR].y <= 0.95f ||
                    floorNormals[nU].y <= 0.95f || floorNormals[nD].y <= 0.95f) continue;
                const float lap = std::abs(resolvedAO[idx].x - 0.25f * (resolvedAO[nL].x + resolvedAO[nR].x + resolvedAO[nU].x + resolvedAO[nD].x));
                laplacianSum += lap;
                maxLaplacian = std::max(maxLaplacian, lap);
                ++smoothSamples;
            }
        }
        const double meanLaplacian = smoothSamples > 0 ? laplacianSum / smoothSamples : 1.0;
        evidence << "hovered_contact_smooth samples=" << smoothSamples << " mean_laplacian=" << meanLaplacian
                 << " max_laplacian=" << maxLaplacian << '\n';
        RENDER_CHECK(smoothSamples > 15 && meanLaplacian < 0.015 && maxLaplacian < 0.08f,
            "hovered sphere floor contact AO is smooth without high-frequency stochastic grain");

        // M15-U4-CONTACT-SUN-FACING-FIX: Camera looking directly toward directional sun above floor.
        // Open sun-lit floor across the entire depth range must stay neutral (no horizontal shadow band),
        // while the sphere resting on the floor still casts a genuine contact shadow in its base footprint.
        floorWorld.Meshes[0].WorldTransform = glm::mat4(1.0f);
        floorWorld.Meshes[1].Material.RHIMaterialAsset = floorMat;
        RenderDirectionalLight sun{};
        sun.Direction = glm::normalize(glm::vec3(0.0f, 0.766044f, -0.642788f)); // ~50 deg elevation forward (-Z)
        sun.Intensity = 1.0f;
        sun.Color = {1.0f, 0.98f, 0.94f};
        floorWorld.DirectionalLight = sun;

        floorSettings.EnableContactShadows = true;
        renderer.RenderWorldView(floorWorld, floorCamera, targets->Get(RHIViewTarget::Color), targets->Get(RHIViewTarget::Depth), floorSettings, nullptr, targets);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors() &&
                     Read(*targets->GetGTAOResolved(), resolvedAO) &&
                     Read(*targets->Get(RHIViewTarget::ScreenNormal), floorNormals) &&
                     Read(*targets->Get(RHIViewTarget::OpaqueDepth), floorDepth),
            "sun-facing contact shadow readbacks");

        float minOpenFloorContact = 1.0f;
        glm::vec3 worstOpenWP(0.0f);
        glm::uvec2 worstOpenXY(0u);
        float minSphereLitContact = 1.0f;
        float minBaseShadowContact = 1.0f;
        size_t openFloorSamples = 0, sphereLitSamples = 0, baseShadowSamples = 0;

        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                const size_t idx = size_t(y) * size + x;
                const auto& norm = floorNormals[idx];
                const float d = floorDepth[idx].x;
                const float contactVal = resolvedAO[idx].y;
                RENDER_CHECK(std::isfinite(contactVal) && contactVal >= 0.0f && contactVal <= 1.0f,
                    "bounded sun-facing resolved contact visibility");
                if (norm.w < 0.5f || d <= 0.0f || d >= 0.9999f) continue;

                const glm::vec4 clip(2.0f * (x + 0.5f) / size - 1.0f, 1.0f - 2.0f * (y + 0.5f) / size, d, 1.0f);
                const glm::vec4 wh = invVP * clip;
                const glm::vec3 wp = glm::vec3(wh) / wh.w;

                if (norm.y > 0.95f && std::abs(wp.y + 1.0f) < 0.05f) {
                    if (std::abs(wp.x) < 0.45f && wp.z > 0.10f && wp.z < 0.85f) {
                        minBaseShadowContact = std::min(minBaseShadowContact, contactVal);
                        ++baseShadowSamples;
                    } else if (std::abs(wp.x) > 1.35f || wp.z < -0.15f || wp.z > 2.25f) {
                        if (contactVal < minOpenFloorContact) {
                            minOpenFloorContact = contactVal;
                            worstOpenWP = wp;
                            worstOpenXY = {x, y};
                        }
                        ++openFloorSamples;
                    }
                } else if (glm::length(wp) < 1.10f && wp.y > 0.25f &&
                           glm::dot(glm::normalize(glm::vec3(norm)), sun.Direction) > 0.20f) {
                    minSphereLitContact = std::min(minSphereLitContact, contactVal);
                    ++sphereLitSamples;
                }
            }
        }
        evidence << "sun_facing open_samples=" << openFloorSamples << " min_open_contact=" << minOpenFloorContact
                 << " worst_open_px=" << worstOpenXY.x << "," << worstOpenXY.y
                 << " worst_open_wp=(" << worstOpenWP.x << "," << worstOpenWP.y << "," << worstOpenWP.z << ")"
                 << " base_samples=" << baseShadowSamples << " min_base_contact=" << minBaseShadowContact
                 << " sphere_lit_samples=" << sphereLitSamples << " min_sphere_contact=" << minSphereLitContact << '\n';
        RENDER_CHECK(openFloorSamples > 200 && minOpenFloorContact >= 0.99f,
            "open floor facing toward the sun has zero false horizontal contact-shadow band");
        RENDER_CHECK(baseShadowSamples > 5 && minBaseShadowContact < 0.85f,
            "sphere base casts genuine contact shadow toward camera when facing the sun");
        RENDER_CHECK(sphereLitSamples > 20 && minSphereLitContact >= 0.98f,
            "sun-lit convex sphere surface exhibits no contact-shadow self-occlusion");

        // Also test a pure open floor (no caster) at a shallow grazing sun-facing angle across the entire foreground.
        RenderWorld flatWorld = floorWorld;
        flatWorld.Meshes.erase(flatWorld.Meshes.begin());
        const glm::vec3 shallowCam(0.0f, 0.25f, 6.2f);
        PerspectiveCamera shallowCamera(55.0f, 1.0f, 0.1f, 100.0f);
        shallowCamera.SetViewMatrix(glm::lookAtRH(shallowCam, glm::vec3(0.0f, -0.75f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)), shallowCam);
        renderer.RenderWorldView(flatWorld, shallowCamera, targets->Get(RHIViewTarget::Color), targets->Get(RHIViewTarget::Depth), floorSettings, nullptr, targets);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors() &&
                     Read(*targets->GetGTAOResolved(), resolvedAO) &&
                     Read(*targets->Get(RHIViewTarget::ScreenNormal), floorNormals),
            "empty shallow sun-facing floor contact readback");
        float minEmptyFloorContact = 1.0f;
        size_t emptyFloorSamples = 0;
        for (size_t idx = 0; idx < resolvedAO.size(); ++idx) {
            if (floorNormals[idx].w < 0.5f || floorNormals[idx].y <= 0.95f) continue;
            minEmptyFloorContact = std::min(minEmptyFloorContact, resolvedAO[idx].y);
            ++emptyFloorSamples;
        }
        evidence << "empty_shallow_floor samples=" << emptyFloorSamples << " min_contact=" << minEmptyFloorContact << '\n';
        RENDER_CHECK(emptyFloorSamples > 1000 && minEmptyFloorContact >= 0.999f,
            "shallow empty floor facing toward the sun has zero false horizontal shadow band");
    }
    return true;
}

// Exercise the actual view-target allocator, recorder, compute dispatch and
// apply draw at floor-half odd and minimal extents, with native validation on.
bool RunM15OcclusionDispatchTests(RHISceneRenderer& renderer, const RenderWorld& world,
                                 SceneRenderSettings settings, const std::filesystem::path& output) {
    settings.EnableGTAO = true; settings.EnableContactShadows = false;
    settings.EnableToneMapping = settings.EnableFXAA = settings.EnableTAA = false;
    settings.EnableSSR = settings.EnableBloom = settings.EnableDDGI = settings.EnableVolumetricFog = settings.EnableAutoExposure = false;
    settings.MSAASamples = 1;
    std::ofstream evidence(output / "M15_U4C_dispatch.txt");
    RENDER_CHECK(evidence.is_open(), "GTAO dispatch evidence file");
    const glm::uvec2 extents[] = {{1, 1}, {3, 5}, {127, 63}, {1919, 1079}};
    for (const auto extent : extents) {
        auto targets = std::make_shared<RHIViewTargets>();
        RHIViewTargetDesc desc;
        desc.Width = extent.x; desc.Height = extent.y;
        desc.EnableGTAO = desc.EnableNormal = desc.EnableScreenStorage = true;
        RENDER_CHECK(targets->Configure(desc), "odd GTAO view target allocation");
        PerspectiveCamera camera(55.0f, float(extent.x) / extent.y, 0.1f, 100.0f);
        camera.SetViewMatrix(glm::lookAtRH(glm::vec3(0, 0, 5), glm::vec3(0), glm::vec3(0, 1, 0)), glm::vec3(0, 0, 5));
        renderer.RenderWorldView(world, camera, targets->Get(RHIViewTarget::Color), targets->Get(RHIViewTarget::Depth), settings, nullptr, targets);
        RENDER_CHECK(renderer.WasLastRenderSuccessful() && !renderer.GetDiagnostics().HasErrors(), "odd GTAO production render");
        auto* ao = targets->GetGTAO();
        auto* aoResolved = targets->GetGTAOResolved();
        RENDER_CHECK(ao && ao->GetWidth() == std::max(1u, extent.x >> 1) && ao->GetHeight() == std::max(1u, extent.y >> 1), "actual odd GTAO storage extent");
        RENDER_CHECK(aoResolved && aoResolved->GetWidth() == extent.x && aoResolved->GetHeight() == extent.y, "actual odd full-res resolved GTAO storage extent");
        std::vector<glm::vec4> values, resolvedValues;
        RENDER_CHECK(Read(*ao, values) && Read(*aoResolved, resolvedValues), "odd GTAO half-float readback");
        for (const auto value : values)
            RENDER_CHECK(std::isfinite(value.x) && value.x >= 0 && value.x <= 1 &&
                std::isfinite(value.y) && value.y >= 0 && value.y <= 1, "all dispatched AO/contact texels are finite and bounded");
        for (const auto value : resolvedValues)
            RENDER_CHECK(std::isfinite(value.x) && value.x >= 0 && value.x <= 1 &&
                std::isfinite(value.y) && value.y >= 0 && value.y <= 1, "all resolved full-res AO/contact texels are finite and bounded");
        RENDER_CHECK(GetRHIValidationErrorCount() == 0, "odd GTAO dispatch and reconstruction have no validation errors");
        evidence << extent.x << 'x' << extent.y << " -> half=" << ao->GetWidth() << 'x' << ao->GetHeight()
            << " full=" << aoResolved->GetWidth() << 'x' << aoResolved->GetHeight()
            << " finite_texels=" << resolvedValues.size() << '\n';
    }
    std::cout << "PASS M15-U4-C native GTAO dispatch and reconstruction: 1x1, 3x5, 127x63, 1919x1079\n";
    return true;
}

// Execute the production PSMain with authored depth/visibility, independently of
// GTAO's horizon estimator. This isolates reconstruction errors that average
// scene luminance and CPU-only weight tests cannot detect.
bool RunM15OcclusionRenderingTests(const std::filesystem::path& output) {
    auto& device = RHIDevice::Get();
    std::ifstream sourceFile(AXLE_GTAO_SHADER_PATH);
    const std::string source((std::istreambuf_iterator<char>(sourceFile)), std::istreambuf_iterator<char>());
    RENDER_CHECK(!source.empty(), "production GTAO shader source");
    RHIShaderCompileDesc compile;
    compile.Source = source; compile.SourcePath = AXLE_GTAO_SHADER_PATH;
    compile.Format = device.GetBackend() == RHIType::Vulkan ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
    compile.EntryPoint = "VSMain"; compile.Stage = RHIShaderStage::Vertex;
    const auto vs = RHIShaderCompiler::Get().Compile(compile);
    compile.EntryPoint = "PSMain"; compile.Stage = RHIShaderStage::Fragment;
    const auto ps = RHIShaderCompiler::Get().Compile(compile);
    RENDER_CHECK(vs.Success && ps.Success, "compile production GTAO apply shaders");

    nri::RootDescriptorDesc root{};
    root.descriptorType = nri::DescriptorType::CONSTANT_BUFFER; root.shaderStages = nri::StageBits::ALL;
    nri::DescriptorRangeDesc ranges[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        ranges[i].baseRegisterIndex = i < 3 ? i : 0;
        ranges[i].descriptorNum = 1;
        ranges[i].descriptorType = i < 3 ? nri::DescriptorType::TEXTURE : nri::DescriptorType::SAMPLER;
        ranges[i].shaderStages = nri::StageBits::FRAGMENT_SHADER;
    }
    nri::DescriptorSetDesc setDesc{};
    setDesc.registerSpace = 1; setDesc.ranges = ranges; setDesc.rangeNum = 4;
    nri::PipelineLayoutDesc layoutDesc{};
    layoutDesc.rootDescriptors = &root; layoutDesc.rootDescriptorNum = 1;
    layoutDesc.descriptorSets = &setDesc; layoutDesc.descriptorSetNum = 1;
    layoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;
    RHIPipelineLayout layout;
    RENDER_CHECK(layout.Init(layoutDesc), "GTAO fixture apply layout");
    RHIGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.Layout = &layout; pipelineDesc.Shaders = {vs.Bytecode, ps.Bytecode};
    pipelineDesc.Rasterizer.CullMode = nri::CullMode::NONE;
    pipelineDesc.DepthStencil.DepthTestEnable = pipelineDesc.DepthStencil.DepthWriteEnable = false;
    pipelineDesc.ColorAttachments = {RHIBlendAttachmentDesc::Opaque()};
    pipelineDesc.ColorAttachmentFormats = {nri::Format::RGBA32_SFLOAT};
    // A local cache releases this fixture's PSO before its layout goes away.
    RHIPipelineCache cache;
    auto* pipeline = cache.GetOrCreateGraphicsPipeline(pipelineDesc);
    RHISampler sampler;
    RENDER_CHECK(pipeline && sampler.Init(RHISamplerDesc::Bilinear(RHIAddressMode::ClampToEdge)), "GTAO fixture apply pipeline");
    RHIBuffer uniform;
    RENDER_CHECK(uniform.Init(RHIBufferDesc::Constant(sizeof(RHIGTAOConstants), RHIMemoryLocation::HostUpload)), "GTAO fixture constants");
    std::ofstream evidence(output / "M15_U4C_reconstruction.txt");
    RENDER_CHECK(evidence.is_open(), "GTAO reconstruction evidence file");

    struct Case { uint32_t W, H; bool Horizontal = false, Reverse = false, Thin = false, Flat = false; };
    const Case cases[] = {
        {8, 4}, {8, 4, false, true}, {9, 5}, {9, 5, true},
        {127, 63}, {1919, 1079}, {8, 4, false, false, true},
        {1, 1, false, false, false, true}, {1, 5, true}, {5, 1}, {3, 5}, {2, 1}, {1, 2, true},
        {9, 5, false, false, false, true}
    };
    for (const auto& c : cases) {
        const uint32_t halfW = std::max(1u, c.W >> 1), halfH = std::max(1u, c.H >> 1);
        const auto foreground = [&](uint32_t x, uint32_t y) {
            return c.Thin ? (x == 0 && y == 0) : c.Flat || (c.Horizontal ? y < c.H / 2 : x < c.W / 2);
        };
        const auto visibility = [&](bool fg) {
            return (fg != c.Reverse) ? glm::vec2(0.2f, 0.85f) : glm::vec2(0.95f, 0.1f);
        };
        std::vector<float> depths(size_t(c.W) * c.H);
        std::vector<glm::vec4> colors(depths.size(), glm::vec4(1, 1, 1, 0.6f));
        for (uint32_t y = 0; y < c.H; ++y) for (uint32_t x = 0; x < c.W; ++x)
            depths[size_t(y) * c.W + x] = foreground(x, y) ? 0.2f : 0.6f;
        std::vector<glm::vec4> occlusion(size_t(halfW) * halfH);
        bool hasForeground = false, hasBackground = false;
        for (uint32_t y = 0; y < halfH; ++y) for (uint32_t x = 0; x < halfW; ++x) {
            // Independent rational mapping of each half-res center to its
            // representative full-res source pixel, including floor odd sizes.
            const auto sourceX = uint32_t((uint64_t(2 * x + 1) * c.W) / (2 * halfW));
            const auto sourceY = uint32_t((uint64_t(2 * y + 1) * c.H) / (2 * halfH));
            const bool fg = foreground(sourceX, sourceY);
            hasForeground |= fg; hasBackground |= !fg;
            occlusion[size_t(y) * halfW + x] = glm::vec4(visibility(fg), 0, 0);
        }
        RHITexture depth, ao, color, target;
        auto inputDesc = RHITextureDesc::RenderTarget2D(c.W, c.H, RHITextureFormat::R32_FLOAT);
        inputDesc.Usage = RHITextureUsage::ShaderResource;
        RENDER_CHECK(depth.Init(inputDesc), "GTAO synthetic scene depth");
        inputDesc.Format = RHITextureFormat::RGBA32_FLOAT;
        RENDER_CHECK(color.Init(inputDesc), "GTAO synthetic scene color");
        inputDesc.Width = halfW; inputDesc.Height = halfH;
        RENDER_CHECK(ao.Init(inputDesc) && target.Init(RHITextureDesc::RenderTarget2D(c.W, c.H, RHITextureFormat::RGBA32_FLOAT)), "GTAO synthetic visibility and output");
        RENDER_CHECK(Upload(depth, depths.data(), 4) && Upload(ao, occlusion.data(), 16) && Upload(color, colors.data(), 16), "GTAO synthetic input upload");
        RHIGTAOConstants constants{};
        constants.InvProjection[2][2] = 10.0f; // Raw depths .2/.6 reconstruct to view Z=2/6.
        constants.ScreenSize = glm::vec4(float(c.W), float(c.H), 1.0f / c.W, 1.0f / c.H);
        constants.GTAOParams.y = constants.ContactParams.z = 1.0f;
        RENDER_CHECK(uniform.UpdateData(&constants, sizeof(constants)), "GTAO synthetic constants update");
        RHIDescriptorPool pool;
        RENDER_CHECK(pool.Init(), "GTAO synthetic descriptor pool");
        auto* set = pool.AllocateDescriptorSet(layout, 0);
        RENDER_CHECK(set && pool.UpdateTexture(set, 0, 0, depth.GetSRV()) && pool.UpdateTexture(set, 1, 0, ao.GetSRV()) &&
            pool.UpdateTexture(set, 2, 0, color.GetSRV()) && pool.UpdateSampler(set, 3, 0, sampler.GetNative()), "GTAO synthetic bindings");
        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init() && commands.Begin(), "GTAO synthetic recording");
        commands.TransitionTexture(target.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
        nri::AttachmentDesc attachment{};
        attachment.descriptor = target.GetRTV(); attachment.loadOp = nri::LoadOp::CLEAR; attachment.storeOp = nri::StoreOp::STORE;
        nri::RenderingDesc rendering{}; rendering.colors = &attachment; rendering.colorNum = 1;
        commands.BeginRendering(rendering);
        commands.SetViewport({0, 0, float(c.W), float(c.H), 0, 1});
        commands.SetScissor({0, 0, static_cast<nri::Dim_t>(c.W), static_cast<nri::Dim_t>(c.H)});
        commands.SetPipeline(pipeline); commands.SetPipelineLayout(nri::BindPoint::GRAPHICS, layout);
        commands.SetRootDescriptor(0, uniform.GetCBV(), 0, nri::BindPoint::GRAPHICS);
        pool.Bind(commands.GetNative()); commands.SetDescriptorSet(nri::BindPoint::GRAPHICS, 0, set);
        commands.Draw(3); commands.EndRendering();
        RENDER_CHECK(commands.End() && commands.Submit() && device.WaitIdle(), "GTAO production apply submission");
        std::vector<glm::vec4> actual;
        RENDER_CHECK(Read(target, actual), "GTAO production apply readback");
        float maxError = 0;
        for (uint32_t y = 0; y < c.H; ++y) for (uint32_t x = 0; x < c.W; ++x) {
            const bool fg = foreground(x, y);
            const auto v = (fg ? hasForeground : hasBackground) ? visibility(fg) : glm::vec2(1);
            const float expected = (0.8375f + 0.1625f * v.x) * (0.9f + 0.1f * v.y);
            const auto pixel = actual[size_t(y) * c.W + x];
            RENDER_CHECK(std::isfinite(pixel.x) && std::isfinite(pixel.y) && std::isfinite(pixel.z) && std::isfinite(pixel.w), "finite GTAO reconstruction output");
            maxError = std::max({maxError, std::abs(pixel.x - expected), std::abs(pixel.y - expected), std::abs(pixel.z - expected), std::abs(pixel.w - 0.6f)});
        }
        evidence << c.W << 'x' << c.H << " horizontal=" << c.Horizontal << " reverse=" << c.Reverse
            << " thin=" << c.Thin << " flat=" << c.Flat << " max_error=" << maxError << '\n';
        evidence.flush();
        if (maxError >= 2e-5f) std::cerr << "GTAO edge error at " << c.W << 'x' << c.H << ": " << maxError << '\n';
        RENDER_CHECK(maxError < 2e-5f, "production bilateral reconstruction preserves each surface and neutral unmatched fallback");
    }
    std::cout << "PASS M15-U4-C production GTAO reconstruction: edge isolation, reversed visibility, thin fallback, odd and minimal extents\n";

    // M15-U4-GTAO-HALO-FIX: Validate CSUpsample normal & plane-depth edge isolation directly on GPU.
    {
        compile.EntryPoint = "CSUpsample";
        compile.Stage = RHIShaderStage::Compute;
        const auto upsampleCS = RHIShaderCompiler::Get().Compile(compile);
        RENDER_CHECK(upsampleCS.Success, "compile production GTAO CSUpsample shader");

        nri::RootDescriptorDesc upRoot{};
        upRoot.descriptorType = nri::DescriptorType::CONSTANT_BUFFER;
        upRoot.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::DescriptorRangeDesc upInputs[4]{}, upStorage{};
        for (uint32_t k = 0; k < 4; ++k) {
            upInputs[k].baseRegisterIndex = k < 3 ? k : 0;
            upInputs[k].descriptorNum = 1;
            upInputs[k].shaderStages = nri::StageBits::COMPUTE_SHADER;
            upInputs[k].descriptorType = k < 3 ? nri::DescriptorType::TEXTURE : nri::DescriptorType::SAMPLER;
        }
        upStorage.descriptorNum = 1;
        upStorage.shaderStages = nri::StageBits::COMPUTE_SHADER;
        upStorage.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
        nri::DescriptorSetDesc upSets[2]{};
        upSets[0].registerSpace = 1; upSets[0].ranges = upInputs; upSets[0].rangeNum = 4;
        upSets[1].registerSpace = 2; upSets[1].ranges = &upStorage; upSets[1].rangeNum = 1;
        nri::PipelineLayoutDesc upLayoutDesc{};
        upLayoutDesc.rootDescriptors = &upRoot; upLayoutDesc.rootDescriptorNum = 1;
        upLayoutDesc.descriptorSets = upSets; upLayoutDesc.descriptorSetNum = 2;
        upLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        RHIPipelineLayout upLayout;
        RENDER_CHECK(upLayout.Init(upLayoutDesc), "GTAO CSUpsample fixture layout");
        RHIComputePipelineDesc upPsoDesc;
        upPsoDesc.Layout = &upLayout;
        upPsoDesc.ComputeShader = upsampleCS.Bytecode;
        auto* upPipeline = cache.GetOrCreateComputePipeline(upPsoDesc);
        RENDER_CHECK(upPipeline, "GTAO CSUpsample pipeline");

        for (const glm::uvec2 ext : {glm::uvec2(16, 16), glm::uvec2(17, 13)}) {
            const uint32_t hw = std::max(1u, ext.x >> 1), hh = std::max(1u, ext.y >> 1);
            std::vector<float> upDepths(size_t(ext.x) * ext.y);
            std::vector<glm::vec4> upNorms(size_t(ext.x) * ext.y);
            const glm::vec3 rimNorm = glm::normalize(glm::vec3(-0.96f, 0.20f, 0.15f));
            const glm::vec3 floorNorm(0.0f, 1.0f, 0.0f);
            for (uint32_t y = 0; y < ext.y; ++y) {
                for (uint32_t x = 0; x < ext.x; ++x) {
                    const bool rim = x < ext.x / 2;
                    // Only 0.025 normalized depth apart (0.192 vs 0.200 -> 8cm at Z~2m):
                    // shallow enough that depth-only 0.05*Z would bleed, yet normal edge must isolate!
                    upDepths[size_t(y) * ext.x + x] = rim ? 0.192f : 0.200f;
                    upNorms[size_t(y) * ext.x + x] = glm::vec4(rim ? rimNorm : floorNorm, 1.0f);
                }
            }
            std::vector<glm::vec4> upHalf(size_t(hw) * hh);
            for (uint32_t y = 0; y < hh; ++y) {
                for (uint32_t x = 0; x < hw; ++x) {
                    const uint32_t sx = uint32_t((uint64_t(2 * x + 1) * ext.x) / (2 * hw));
                    const bool rim = sx < ext.x / 2;
                    upHalf[size_t(y) * hw + x] = glm::vec4(rim ? 1.0f : 0.15f, 1.0f, 0.0f, 0.0f);
                }
            }
            RHITexture dTex, nTex, hTex, outTex;
            auto dDesc = RHITextureDesc::RenderTarget2D(ext.x, ext.y, RHITextureFormat::R32_FLOAT);
            dDesc.Usage = RHITextureUsage::ShaderResource;
            RENDER_CHECK(dTex.Init(dDesc), "upsample depth");
            dDesc.Format = RHITextureFormat::RGBA32_FLOAT;
            RENDER_CHECK(nTex.Init(dDesc), "upsample normal");
            dDesc.Width = hw; dDesc.Height = hh;
            RENDER_CHECK(hTex.Init(dDesc) &&
                outTex.Init(RHITextureDesc::StorageTexture2D(ext.x, ext.y, RHITextureFormat::RGBA16_FLOAT)) &&
                Upload(dTex, upDepths.data(), 4) && Upload(nTex, upNorms.data(), 16) && Upload(hTex, upHalf.data(), 16),
                "upsample inputs");

            RHIGTAOConstants upConsts{};
            upConsts.InvProjection[2][2] = 10.0f;
            upConsts.ScreenSize = {float(ext.x), float(ext.y), 1.0f / ext.x, 1.0f / ext.y};
            RENDER_CHECK(uniform.UpdateData(&upConsts, sizeof(upConsts)), "upsample constants");
            RHIDescriptorPool upPool;
            RENDER_CHECK(upPool.Init(), "upsample descriptor pool");
            auto* tSet = upPool.AllocateDescriptorSet(upLayout, 0);
            auto* sSet = upPool.AllocateDescriptorSet(upLayout, 1);
            RENDER_CHECK(tSet && sSet &&
                upPool.UpdateTexture(tSet, 0, 0, dTex.GetSRV()) &&
                upPool.UpdateTexture(tSet, 1, 0, nTex.GetSRV()) &&
                upPool.UpdateTexture(tSet, 2, 0, hTex.GetSRV()) &&
                upPool.UpdateSampler(tSet, 3, 0, sampler.GetNative()) &&
                upPool.UpdateStorageTexture(sSet, 0, 0, outTex.GetUAV()), "upsample bindings");

            RHICommandBuffer cmd;
            RENDER_CHECK(cmd.Init() && cmd.Begin(), "upsample cmd");
            cmd.TransitionTexture(outTex.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ShaderResourceStorage);
            cmd.SetPipeline(upPipeline);
            cmd.SetPipelineLayout(nri::BindPoint::COMPUTE, upLayout);
            cmd.SetRootDescriptor(0, uniform.GetCBV(), 0, nri::BindPoint::COMPUTE);
            upPool.Bind(cmd.GetNative());
            cmd.SetDescriptorSet(nri::BindPoint::COMPUTE, 0, tSet);
            nri::SetDescriptorSetDesc outBind{};
            outBind.bindPoint = nri::BindPoint::COMPUTE;
            outBind.setIndex = 1;
            outBind.descriptorSet = sSet;
            device.GetNRICore().CmdSetDescriptorSet(*cmd.GetNative(), outBind);
            cmd.Dispatch((ext.x + 7) / 8, (ext.y + 7) / 8, 1);
            std::vector<glm::vec4> upActual;
            RENDER_CHECK(cmd.End() && cmd.Submit() && device.WaitIdle() && Read(outTex, upActual), "upsample readback");
            for (uint32_t y = 0; y < ext.y; ++y) {
                for (uint32_t x = 0; x < ext.x; ++x) {
                    const bool rim = x < ext.x / 2;
                    const float aoOut = upActual[size_t(y) * ext.x + x].x;
                    RENDER_CHECK(std::isfinite(aoOut), "finite upsample output");
                    if (rim) {
                        RENDER_CHECK(aoOut >= 0.99f, "CSUpsample isolates sphere-rim normal from shallow adjacent floor AO bleed");
                    } else {
                        RENDER_CHECK(std::abs(aoOut - 0.15f) < 0.02f, "CSUpsample isolates floor AO cleanly without sphere bleed");
                    }
                }
            }
        }
        std::cout << "PASS M15-U4-GTAO-HALO-FIX CSUpsample: normal and plane-depth edge isolation on even and odd extents\n";
    }
    return true;
}

// Bind deterministic visibility/depth to the actual production ForwardPBR PSO.
// Isolated radiance terms prove invariance even when GTAO happens to estimate a
// neutral surface in an ordinary environment fixture.
bool RunM15OcclusionForwardTests(RHISceneRenderer& renderer, const std::filesystem::path& output) {
    constexpr uint32_t size = 8;
    RHIVertex3D vertices[3];
    vertices[0].Position = {-1, -1, 0.5f}; vertices[1].Position = {3, -1, 0.5f}; vertices[2].Position = {-1, 3, 0.5f};
    for (auto& vertex : vertices) vertex.Normal = {0, 0, 1};
    RHIBuffer vertexBuffer, sceneBuffer, materialBuffer, cascadeBuffer, reflectionProbeBuffer;
    RENDER_CHECK(vertexBuffer.Init(RHIBufferDesc::Vertex(sizeof(vertices), RHIMemoryLocation::HostUpload)) &&
        vertexBuffer.UpdateData(vertices, sizeof(vertices)), "forward invariance vertex buffer");
    RENDER_CHECK(sceneBuffer.Init(RHIBufferDesc::Constant(sizeof(RHISceneConstants), RHIMemoryLocation::HostUpload)) &&
        materialBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIMaterialConstants), RHIMemoryLocation::HostUpload)) &&
        cascadeBuffer.Init(RHIBufferDesc::Constant(sizeof(RHICascadeConstants), RHIMemoryLocation::HostUpload)) &&
        reflectionProbeBuffer.Init(RHIBufferDesc::Constant(sizeof(RHIReflectionProbeBufferData), RHIMemoryLocation::HostUpload)), "forward invariance buffers");
    {
        const RHIReflectionProbeBufferData emptyProbes{};
        RENDER_CHECK(reflectionProbeBuffer.UpdateData(&emptyProbes, sizeof(emptyProbes)), "forward invariance empty reflection probe buffer");
    }
    std::ofstream evidence(output / "M15_U4AB_forward.txt");
    RENDER_CHECK(evidence.is_open(), "forward invariance evidence");
    enum class Term { Sun, Specular, Ambient, Emissive, Local, Spot, SunAndLocal };
    // Compile the canonical graph surface template too. These are genuine graph
    // VS/PS bytecodes with reflected descriptors, not a built-in fallback.
    RHIShaderCompileDesc graphCompile;
    graphCompile.Format = RHIDevice::Get().GetBackend() == RHIType::Vulkan ? RHIShaderFormat::SPIRV : RHIShaderFormat::DXIL;
    graphCompile.SourcePath = "occlusion_graph_fixture.hlsl";
    graphCompile.Source = AssembleMaterialSurfaceShader(
        "cbuffer TestMaterial : register(b2,space0) { float4 testBase; float4 testEmission; float4 testParams; };",
        "MaterialSurface AxleEvaluateMaterial(float2 uv) { MaterialSurface s = (MaterialSurface)0; "
        "s.BaseColor=testBase.rgb; s.Emission=testEmission.rgb*testEmission.w; "
        "s.Roughness=testParams.z; s.Metallic=testParams.w; s.Normal=float3(0,0,1); s.AO=1; s.IOR=1.5; s.ClearcoatRoughness=.04; return s; }");
    graphCompile.EntryPoint = "VSMain"; graphCompile.Stage = RHIShaderStage::Vertex;
    const auto graphVS = RHIShaderCompiler::Get().Compile(graphCompile);
    graphCompile.EntryPoint = "PSMain"; graphCompile.Stage = RHIShaderStage::Fragment;
    const auto graphPS = RHIShaderCompiler::Get().Compile(graphCompile);
    RENDER_CHECK(graphVS.Success && graphPS.Success, "canonical graph radiance shader compilation");
    const auto graphReflection = RHIShaderReflection::Merge({graphVS.Reflection, graphPS.Reflection});
    const auto graphLayoutDescription = graphReflection.BuildPipelineLayoutDesc(5);
    RHIPipelineLayout graphLayout;
    RENDER_CHECK(graphLayout.Init(graphLayoutDescription), "canonical graph radiance layout");
    RHIGraphicsPipelineDesc graphPipelineDescription;
    graphPipelineDescription.Layout = &graphLayout;
    graphPipelineDescription.Shaders = {graphVS.Bytecode, graphPS.Bytecode};
    graphPipelineDescription.Rasterizer.CullMode = nri::CullMode::NONE;
    graphPipelineDescription.VertexInput = VertexLayoutContract::GetCanonicalVertexInputDesc();
    graphPipelineDescription.ColorAttachments = {RHIBlendAttachmentDesc::Opaque()};
    graphPipelineDescription.ColorAttachmentFormats = {nri::Format::RGBA16_SFLOAT};
    RHIPipelineCache graphCache;
    auto* graphPipeline = graphCache.GetOrCreateGraphicsPipeline(graphPipelineDescription);
    RHISampler graphShadowSampler;
    RENDER_CHECK(graphPipeline && graphShadowSampler.Init(RHISamplerDesc::ShadowPCF()), "canonical graph radiance pipeline");
    RHIBuffer graphObjectBuffer, graphSceneBuffer, graphMaterialBuffer;
    RENDER_CHECK(graphObjectBuffer.Init(RHIBufferDesc::Constant(256, RHIMemoryLocation::HostUpload)) &&
        graphSceneBuffer.Init(RHIBufferDesc::Constant(256, RHIMemoryLocation::HostUpload)) &&
        graphMaterialBuffer.Init(RHIBufferDesc::Constant(256, RHIMemoryLocation::HostUpload)), "canonical graph test buffers");
    bool graphMode = false;
    auto render = [&](const char* name, Term term, float ao, float contact, std::vector<glm::vec4>& pixels,
                      bool enableAO = true, bool enableContact = true, int csm = 0, float guideDepth = 0.5f) -> bool {
        const bool sun = term == Term::Sun || term == Term::Specular || term == Term::SunAndLocal;
        const bool local = term == Term::Local || term == Term::SunAndLocal;
        RHISceneConstants scene;
        scene.CameraPosition = {0, 0, 5, 1}; scene.View = glm::translate(glm::mat4(1), glm::vec3(0, 0, -5));
        scene.Projection[2][2] = -1.0f;
        scene.SunDirection = {0, 0, 1, 0};
        RHIMaterialConstants material;
        material.Flags.y = 1;
        RHIObjectPushConstants object;
        object.ColorPrimary = {0.6f, 0.4f, 0.2f, 1}; object.ColorSecondary = {};
        object.Params = {1, 3, term == Term::Specular ? 0.2f : 0.5f, term == Term::Specular ? 1.0f : 0.0f};
        object.MaterialExtra = {};
        if (term == Term::Emissive) object.ColorSecondary = {0.7f, 0.2f, 0.1f, 2};
        RHIMultiLightBufferData lights;
        lights.CameraPosAndExposure = scene.CameraPosition;
        lights.SunLight.DirectionAndIlluminance = {0, 0, 1, sun ? 30000.0f : 0.0f};
        lights.SunLight.ColorAndAmbient = {1, 1, 1, 0};
        lights.SunLight.AmbientColor = {0.25f, 0.3f, 0.4f, 1};
        lights.ReservedHeader[0] = {1, term == Term::Ambient ? 1.0f : 0.0f, 0, 0};
        lights.ReservedHeader[1] = {0, enableAO ? 1.0f : 0.0f, enableContact ? 1.0f : 0.0f, 0};
        if (local) {
            lights.LightCounts.x = 1;
            lights.PointLights[0].PositionAndRadius = {0, 0, 3, 10};
            lights.PointLights[0].ColorAndIntensity = {1, 0.8f, 0.6f, 30};
        }
        if (term == Term::Spot) {
            lights.LightCounts.y = 1;
            lights.SpotLights[0].PositionAndRange = {0, 0, 3, 10};
            lights.SpotLights[0].DirectionAndIntensity = {0, 0, -1, 30};
            lights.SpotLights[0].ColorAndInnerAngle = {1, 0.8f, 0.6f, 0.98f};
            lights.SpotLights[0].AngleParams = {0.9f, 1, 0, 0};
        }
        RHICascadeConstants cascades;
        std::vector<float> shadowPixels(size * size, 1.0f);
        if (csm) {
            cascades.Matrices[0] = glm::mat4(0); cascades.Matrices[0][3] = {0, 0, 0.5f, 1};
            cascades.Splits = glm::vec4(100); cascades.Atlas = {1, 1.0f / size, size / 2, 0.1f};
            cascades.Bias = {0, 0, 0, 1};
            for (uint32_t y = 0; y < size; ++y) for (uint32_t x = 0; x < size; ++x)
                shadowPixels[y * size + x] = csm == 2 ? 0.0f : (x < size / 4 ? 0.0f : 1.0f);
        }
        RENDER_CHECK(sceneBuffer.UpdateData(&scene, sizeof(scene)) && materialBuffer.UpdateData(&material, sizeof(material)) &&
            cascadeBuffer.UpdateData(&cascades, sizeof(cascades)) && renderer.GetLightBuffer().Update(lights), "forward invariance constants");
        RHITexture target, targetDepth, visibility, guide, shadow;
        RENDER_CHECK(target.Init(RHITextureDesc::RenderTarget2D(size, size, RHITextureFormat::RGBA16_FLOAT)) &&
            targetDepth.Init(RHITextureDesc::DepthStencil2D(size, size)) && shadow.Init(RHITextureDesc::DepthStencil2D(size, size)), "forward invariance targets");
        auto description = RHITextureDesc::RenderTarget2D(size / 2, size / 2, RHITextureFormat::RGBA32_FLOAT);
        description.Usage = RHITextureUsage::ShaderResource;
        RENDER_CHECK(visibility.Init(description), "forward synthetic visibility");
        description.Width = description.Height = size; description.Format = RHITextureFormat::R32_FLOAT;
        RENDER_CHECK(guide.Init(description), "forward synthetic guide");
        std::vector<glm::vec4> values(size * size / 4, glm::vec4(ao, contact, 0, 0));
        std::vector<float> depths(size * size, guideDepth);
        RENDER_CHECK(Upload(visibility, values.data(), 16) && Upload(guide, depths.data(), 4) &&
            Upload(shadow, shadowPixels.data(), 4), "forward synthetic visibility/depth upload");
        auto& pool = renderer.GetDescriptorPool();
        auto* materialSet = pool.AllocateDescriptorSet(renderer.GetPipelineLayout(), 0);
        auto* visibilitySet = pool.AllocateDescriptorSet(renderer.GetPipelineLayout(), 1);
        std::vector<nri::Descriptor*> textures = {
            RHIMaterial::GetDefaultWhiteTexture()->GetSRV(), RHIMaterial::GetDefaultFlatNormalTexture()->GetSRV(),
            RHIMaterial::GetDefaultRoughnessTexture()->GetSRV(), RHIMaterial::GetDefaultBlackTexture()->GetSRV(),
            RHIMaterial::GetDefaultWhiteTexture()->GetSRV(), RHIMaterial::GetDefaultBlackTexture()->GetSRV(),
            RHIMaterial::GetDefaultIrradianceCubemap()->GetSRV(), RHIMaterial::GetDefaultPrefilterCubemap()->GetSRV(),
            RHIMaterial::GetDefaultBRDFLUT()->GetSRV()};
        for (uint32_t slot = 6; slot < uint32_t(RHITextureSlot::Count); ++slot)
            textures.push_back(RHIMaterial::GetFallbackTexture(RHITextureSlot(slot))->GetSRV());
        textures.insert(textures.end(), {RHIMaterial::GetDefaultBlackTexture()->GetSRV(), RHIMaterial::GetDefaultWhiteTexture()->GetSRV(),
            shadow.GetSRV(), renderer.GetLocalShadows().GetSRV()});
        auto* defaultProbeSRV = RHIMaterial::GetDefaultPrefilterCubemap()->GetSRV();
        RENDER_CHECK(materialSet && visibilitySet && defaultProbeSRV &&
            pool.UpdateTextureRange(materialSet, 0, 0, textures) &&
            pool.UpdateTextureRange(visibilitySet, 0, 0, {visibility.GetSRV(), guide.GetSRV()}) &&
            pool.UpdateTextureRange(visibilitySet, 1, 0, {defaultProbeSRV, defaultProbeSRV, defaultProbeSRV, defaultProbeSRV}),
            "forward synthetic descriptors");
        RHIDescriptorPool graphPool;
        std::vector<nri::DescriptorSet*> graphSets;
        if (graphMode) {
            struct GraphScene { glm::mat4 VP; glm::vec4 Camera, SunDirection, SunColor; glm::mat4 Projection; };
            const GraphScene graphScene{scene.ViewProjection, scene.CameraPosition, scene.SunDirection, scene.SunColor, scene.Projection};
            const glm::vec4 parameters[]{object.ColorPrimary, object.ColorSecondary, object.Params};
            RENDER_CHECK(graphObjectBuffer.UpdateData(&object.Model, sizeof(glm::mat4)) &&
                graphSceneBuffer.UpdateData(&graphScene, sizeof(graphScene)) &&
                graphMaterialBuffer.UpdateData(parameters, sizeof(parameters)) && graphPool.Init(), "canonical graph synthetic constants");
            for (size_t i = 0; i < graphLayoutDescription.DescriptorSets.size(); ++i) {
                auto* set = graphPool.AllocateDescriptorSet(graphLayout, uint32_t(i));
                RENDER_CHECK(set, "canonical graph synthetic descriptor allocation");
                graphSets.push_back(set);
                const auto& setDescription = graphLayoutDescription.DescriptorSets[i];
                for (size_t r = 0; r < setDescription.Ranges.size(); ++r) {
                    const auto& range = setDescription.Ranges[r];
                    const auto space = setDescription.RegisterSpace, slot = range.baseRegisterIndex;
                    nri::Descriptor* descriptor = nullptr;
                    if (range.descriptorType == nri::DescriptorType::CONSTANT_BUFFER) {
                        if (space == 0 && slot == 0) descriptor = graphObjectBuffer.GetCBV();
                        else if (space == 0 && slot == 1) descriptor = graphSceneBuffer.GetCBV();
                        else if (space == 0 && slot == 2) descriptor = graphMaterialBuffer.GetCBV();
                        else if (space == 0 && slot == 4) descriptor = cascadeBuffer.GetCBV();
                        else if (space == 0 && slot == 5) descriptor = renderer.GetLocalShadows().GetConstantsCBV();
                        else if (space == 0 && slot == 6) descriptor = reflectionProbeBuffer.GetCBV();
                        else if (space == 3 && slot == 0) descriptor = renderer.GetLightBuffer().GetCBV();
                    } else if (range.descriptorType == nri::DescriptorType::TEXTURE) {
                        if (space == 2 && slot < 3) descriptor = textures[6 + slot];
                        else if (space == 2 && slot == 3) descriptor = shadow.GetSRV();
                        else if (space == 2 && slot == 4) descriptor = renderer.GetLocalShadows().GetSRV();
                        else if (space == 4 && slot == 0) descriptor = visibility.GetSRV();
                        else if (space == 4 && slot == 1) descriptor = guide.GetSRV();
                        else if (space == 4 && slot >= 2 && slot < 6) descriptor = defaultProbeSRV;
                    } else if (range.descriptorType == nri::DescriptorType::SAMPLER) {
                        descriptor = slot == 1 ? graphShadowSampler.GetNative() : renderer.GetSampler().GetNative();
                    }
                    RENDER_CHECK(descriptor && range.descriptorNum == 1 && graphPool.UpdateTexture(set, uint32_t(r), 0, descriptor), "canonical graph synthetic descriptor binding");
                }
            }
        }
        RHICommandBuffer commands;
        RENDER_CHECK(commands.Init() && commands.Begin(), "forward production recording");
        commands.TransitionTexture(target.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::ColorAttachment);
        commands.TransitionTexture(targetDepth.GetNative(), RHIResourceLayout::Undefined, RHIResourceLayout::DepthStencilWrite);
        nri::AttachmentDesc colorAttachment{}, depthAttachment{};
        colorAttachment.descriptor = target.GetRTV(); colorAttachment.loadOp = nri::LoadOp::CLEAR; colorAttachment.storeOp = nri::StoreOp::STORE;
        depthAttachment.descriptor = targetDepth.GetDSV(); depthAttachment.loadOp = nri::LoadOp::CLEAR; depthAttachment.storeOp = nri::StoreOp::STORE;
        depthAttachment.clearValue.depthStencil.depth = 1;
        nri::RenderingDesc rendering{}; rendering.colors = &colorAttachment; rendering.colorNum = 1; rendering.depth = depthAttachment;
        commands.BeginRendering(rendering);
        commands.SetViewport({0, 0, float(size), float(size), 0, 1});
        commands.SetScissor({0, 0, size, size});
        if (graphMode) {
            commands.SetPipeline(graphPipeline); commands.SetPipelineLayout(nri::BindPoint::GRAPHICS, graphLayout);
            graphPool.Bind(commands.GetNative());
            for (size_t i = 0; i < graphSets.size(); ++i) commands.SetDescriptorSet(nri::BindPoint::GRAPHICS, uint32_t(i), graphSets[i]);
        } else {
            commands.SetPipeline(renderer.GetHDRPipeline()); commands.SetPipelineLayout(nri::BindPoint::GRAPHICS, renderer.GetPipelineLayout());
            commands.SetRootDescriptor(0, sceneBuffer.GetCBV()); commands.SetRootDescriptor(1, renderer.GetLightBuffer().GetCBV());
            commands.SetRootDescriptor(2, materialBuffer.GetCBV()); commands.SetRootDescriptor(3, cascadeBuffer.GetCBV());
            commands.SetRootDescriptor(4, renderer.GetLocalShadows().GetConstantsCBV());
            commands.SetRootDescriptor(5, reflectionProbeBuffer.GetCBV());
            pool.Bind(commands.GetNative()); commands.SetDescriptorSet(nri::BindPoint::GRAPHICS, 0, materialSet);
            commands.SetDescriptorSet(nri::BindPoint::GRAPHICS, 1, visibilitySet);
            commands.SetRootConstants(0, &object, sizeof(object));
        }
        commands.SetVertexBuffer(0, vertexBuffer, 0, sizeof(RHIVertex3D));
        commands.Draw(3); commands.EndRendering();
        RENDER_CHECK(commands.End() && commands.Submit() && RHIDevice::Get().WaitIdle() && Read(target, pixels), "production forward radiance readback");
        const auto center = pixels[size * size / 2 + size / 2];
        evidence << (graphMode ? "graph " : "builtin ") << name << " center=" << center.x << ',' << center.y << ',' << center.z << '\n';
        for (const auto pixel : pixels)
            RENDER_CHECK(std::isfinite(pixel.x) && std::isfinite(pixel.y) && std::isfinite(pixel.z) && std::isfinite(pixel.w), "finite production forward radiance");
        return true;
    };
    auto equal = [&](const std::vector<glm::vec4>& actual, const std::vector<glm::vec4>& expected, float scale = 1.0f) {
        float maxError = 0, maximum = 0;
        for (size_t i = 0; i < actual.size(); ++i) for (size_t channel = 0; channel < 3; ++channel) {
            maxError = std::max(maxError, std::abs(actual[i][channel] - expected[i][channel] * scale));
            maximum = std::max(maximum, std::abs(expected[i][channel]));
        }
        evidence << "max_error=" << maxError << '\n';
        return maxError <= 0.002f * std::max(maximum, 1.0f);
    };
    std::vector<glm::vec4> reference, modified, sun, local, combined;
    for (const bool useGraph : {false, true}) {
        graphMode = useGraph;
        for (const auto term : {Term::Sun, Term::Specular, Term::Emissive, Term::Local, Term::Spot}) {
            RENDER_CHECK(render("ao-reference", term, 1, 1, reference) && render("ao-zero", term, 0, 1, modified) && equal(modified, reference),
                "AO leaves production sun diffuse/specular, emissive and local direct invariant");
            RENDER_CHECK(reference[36].x > 0.01f, "isolated radiance fixture is nonzero");
        }
        RENDER_CHECK(render("ambient-reference", Term::Ambient, 1, 1, reference) &&
            render("ambient-half", Term::Ambient, 0.5f, 0, modified) && equal(modified, reference, 0.5f) &&
            render("ambient-zero", Term::Ambient, 0, 0, modified) && equal(modified, reference, 0) &&
            render("ao-disabled", Term::Ambient, 0, 0, modified, false) && equal(modified, reference), "AO scales only ambient and disabled AO stays neutral");
        RENDER_CHECK(render("sun-reference", Term::Sun, 1, 1, sun) && render("sun-half", Term::Sun, 1, 0.5f, modified) && equal(modified, sun, 0.5f) &&
            render("sun-blocked", Term::Sun, 1, 0, modified) && equal(modified, sun, 0) &&
            render("contact-disabled", Term::Sun, 1, 0, modified, true, false) && equal(modified, sun), "contact scales only matching sun and respects its enable flag");
        for (const auto term : {Term::Ambient, Term::Emissive, Term::Local, Term::Spot})
            RENDER_CHECK(render("contact-reference", term, 1, 1, reference) && render("contact-blocked", term, 1, 0, modified) && equal(modified, reference),
                "contact leaves ambient, emissive and local lights invariant without sun");
        RENDER_CHECK(render("local-reference", Term::Local, 1, 1, local) &&
            render("mixed-sun-blocked", Term::SunAndLocal, 1, 0, combined) && equal(combined, local), "local light survives contact-blocked sun in mixed scene");
        RENDER_CHECK(render("csm-penumbra", Term::Sun, 1, 1, reference, true, true, 1) && equal(reference, sun, 0.5f) &&
            render("csm-contact-penumbra", Term::Sun, 1, 0.5f, modified, true, true, 1) && equal(modified, reference) &&
            render("csm-zero", Term::Sun, 1, 0.5f, modified, true, true, 2) && equal(modified, sun, 0), "CSM/contact use minimum without penumbra double attenuation");
        RENDER_CHECK(render("unrepresented-fragment", Term::Sun, 0, 0, modified, true, true, 0, 0.6f) && equal(modified, sun),
            "fragment absent from prepass cannot inherit background visibility");

        // M15-U4-D: Regression Fixtures (U4-06.1 - U4-06.5)
        // 1. M15-U4-06.1: Test no-sun: sun absent, contact variations have 0 effect on radiance
        RENDER_CHECK(render("no-sun-contact1", Term::Local, 1, 1, reference) &&
            render("no-sun-contact0", Term::Local, 1, 0, modified) && equal(modified, reference),
            "M15-U4-06.1: no-sun regression: contact variations have zero effect on local light radiance");
        RENDER_CHECK(render("no-sun-amb-contact1", Term::Ambient, 1, 1, reference) &&
            render("no-sun-amb-contact0", Term::Ambient, 1, 0, modified) && equal(modified, reference),
            "M15-U4-06.1: no-sun regression: contact variations have zero effect on ambient radiance");

        // 2. M15-U4-06.2: Test no-AO: when AO is disabled, ambient receives neutral 1.0 factor
        RENDER_CHECK(render("no-ao-reference", Term::Ambient, 1, 1, reference, true) &&
            render("no-ao-disabled-zero", Term::Ambient, 0, 1, modified, false) && equal(modified, reference),
            "M15-U4-06.2: no-AO regression: disabled AO preserves full neutral ambient radiance at ao=0");

        // 3. M15-U4-06.3: Test no-contact: when contact is disabled, sun receives normal CSM visibility
        RENDER_CHECK(render("no-contact-reference", Term::Sun, 1, 1, reference, true, true) &&
            render("no-contact-disabled-zero", Term::Sun, 1, 0, modified, true, false) && equal(modified, reference),
            "M15-U4-06.3: no-contact regression: disabled contact preserves full sun radiance at contact=0");
        for (const int csm : {1, 2}) {
            RENDER_CHECK(render("no-contact-csm-reference", Term::Sun, 1, 1, reference, true, true, csm) &&
                render("no-contact-csm-disabled", Term::Sun, 1, 0, modified, true, false, csm) && equal(modified, reference),
                "disabled contact retains native partial and fully blocked CSM");
            RENDER_CHECK(equal(reference, sun, csm == 1 ? 0.5f : 0.0f), "no-contact reference follows CSM independently");
        }

        // 4. M15-U4-06.4: Test emissive: emissive surfaces are strictly invariant under AO and contact variations
        RENDER_CHECK(render("emissive-bright-ref", Term::Emissive, 1, 1, reference) &&
            render("emissive-darkened-test", Term::Emissive, 0, 0, modified) && equal(modified, reference),
            "M15-U4-06.4: emissive regression: AO=0 and contact=0 never darken emissive surfaces");
        RENDER_CHECK(reference[36].x > 0.05f, "emissive reference fixture is strictly nonzero");
        for (float ao : {0.0f, 0.5f, 1.0f}) for (float contact : {0.0f, 0.5f, 1.0f})
            RENDER_CHECK(render("emissive-sweep", Term::Emissive, ao, contact, modified) && equal(modified, reference),
                "native emissive radiance is invariant over the AO/contact sweep");

        // 5. M15-U4-06.5: Test mirror / unoccluded specular: metallic specular glints invariant under AO
        RENDER_CHECK(render("mirror-specular-ref", Term::Specular, 1, 1, reference) &&
            render("mirror-specular-ao0", Term::Specular, 0, 1, modified) && equal(modified, reference),
            "M15-U4-06.5: mirror/unoccluded specular regression: AO=0 never darkens specular glints");


    }
    RENDER_CHECK(GetRHIValidationErrorCount() == 0, "production forward invariance fixture validation");
    std::cout << "PASS M15-U4-A/B/D production forward radiance: AO ambient-only, sun-only contact, CSM minimum, emissive/local invariance\n";
    return true;
}
