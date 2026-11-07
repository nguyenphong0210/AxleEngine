#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"
#include <Axle/AssetManager.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/MeshResourceResolver.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RHIObjectPicker.hpp>
#include <Axle/RHI/RHISwitcher.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/Shader.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>

using namespace Axle;
using namespace Axle::RHI;
using namespace Axle::RenderTests;

static bool Policy(const std::filesystem::path& root) {
    RENDER_CHECK(!RHIDevice::Get().IsBackendSelected(), "factory policy starts without a GPU backend");
    PhysicsScene physics;
    auto scene = std::make_shared<Scene>();
    SceneHierarchyPanel panel(scene.get(), &physics);
    for (int i = 0; i < 5; ++i) {
        const auto& definition = BuiltinMeshes[i];
        auto entity = panel.CreatePrimitive(i, {float(i * 4), 2, 0}, false);
        RENDER_CHECK(entity && entity.HasComponent<MeshInstance3D>(), "actual authoring factory always creates its render component");
        const auto& mesh = entity.GetComponent<MeshInstance3D>();
        RENDER_CHECK(mesh.AssetPath == definition.URI && !mesh.VAO && !mesh.RHIMeshAsset, "authored builtin identity does not need GL or NRI");
        RENDER_CHECK(Near(entity.GetComponent<TransformComponent>().Scale, i == 4 ? glm::vec3(20, 1, 20) : glm::vec3(1)), "existing transform defaults");
        RENDER_CHECK(!entity.HasComponent<RigidBody3D>() && mesh.PatternMode == (i == 1 ? 2 : 1) && Near(mesh.TileScale, i == 4 ? 8.f : 4.f), "static and checker defaults preserved");
        RENDER_CHECK(Near(mesh.Color1, i == 4 ? glm::vec4(.35f, .38f, .42f, 1) : glm::vec4(.88f, .89f, .92f, 1)), "existing authoring colors");
        entity.GetComponent<TagComponent>().Tag = "renamed " + std::to_string(i);
        const auto id = entity.GetUUID();
        auto duplicate = scene->DuplicateEntity(entity, &physics);
        RENDER_CHECK(duplicate.GetUUID() != id && duplicate.GetComponent<MeshInstance3D>().AssetPath == definition.URI, "duplicate retains asset identity and receives an independent entity identity");
        auto copy = Scene::Copy(scene);
        RENDER_CHECK(copy->GetEntityByUUID(id).GetComponent<MeshInstance3D>().AssetPath == definition.URI, "Scene::Copy preserves builtin identity");
        if (i < 4) {
            auto dynamic = panel.CreatePrimitive(i, {float(i * 4), 5, 0}, true);
            RENDER_CHECK(dynamic.HasComponent<CollisionShape3D>() && dynamic.HasComponent<RigidBody3D>(), "dynamic factory creates real collision/body components");
            const auto& body = dynamic.GetComponent<RigidBody3D>();
            const auto& collision = dynamic.GetComponent<CollisionShape3D>();
            RENDER_CHECK(body.IsDynamic && body.MotionType == BodyMotionType::Dynamic && physics.IsBodyValid(body.RuntimeBodyID) && Near(collision.Radius, .5f) && Near(collision.HalfHeight, .5f), "real Jolt body and prior physical dimensions preserved");
            scene->DestroyEntity(dynamic, &physics);
        }
    }
    const auto path = root / "headless.axle";
    RENDER_CHECK(SceneSerializer(scene.get()).Serialize(path), "real headless scene save");
    auto reopened = std::make_shared<Scene>();
    RENDER_CHECK(SceneSerializer(reopened.get()).Deserialize(path), "real headless scene reopen");
    RENDER_CHECK(reopened->GetRegistry().view<MeshInstance3D>().size() == 10 && !RHIDevice::Get().IsBackendSelected(), "all primitive identities reopen without native allocation");
    return true;
}

static std::vector<uint8_t> GLPixels(Scene& scene, Entity entity, const PerspectiveCamera& camera) {
    auto& mesh = entity.GetComponent<MeshInstance3D>();
    if (!ResolveMeshResources(mesh) || !mesh.VAO) return {};
    auto target = Framebuffer::Create({96, 96, 1, false});
    Shader shader("#version 450 core\nlayout(location=0) in vec3 p; uniform mat4 vp; uniform mat4 model; void main(){gl_Position=vp*model*vec4(p,1);}",
        "#version 450 core\nlayout(location=0) out vec4 c; void main(){c=vec4(.75,.25,.125,1);}");
    if (!target || !shader.GetRendererID()) return {};
    target->Bind(); RenderCommand::SetViewport(0, 0, 96, 96);
    RenderCommand::SetClearColor({0, 0, 0, 1}); RenderCommand::Clear();
    shader.Bind(); shader.SetMat4("vp", camera.GetViewProjectionMatrix());
    shader.SetMat4("model", scene.GetWorldTransform(static_cast<entt::entity>(entity)));
    RenderCommand::DrawIndexed(mesh.VAO);
    std::vector<uint8_t> pixels(96 * 96 * 4);
    glReadPixels(0, 0, 96, 96, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto error = glGetError(); target->Unbind();
    return error == GL_NO_ERROR ? pixels : std::vector<uint8_t>{};
}

static bool Pixels(const std::filesystem::path& root, bool gl) {
    RHISceneRenderer renderer;
    RHIObjectPicker picker;
    RHITexture color, depth;
    if (!gl) RENDER_CHECK(renderer.Init(RHIDevice::Get(), false) && picker.Init() &&
        color.Init(RHITextureDesc::RenderTarget2D(96, 96)) && depth.Init(RHITextureDesc::DepthStencil2D(96, 96)), "real native scene/picking resources");
    SceneRenderSettings settings;
    settings.EnableSkybox = settings.EnableShadows = settings.EnableToneMapping = settings.EnableFXAA = settings.EnableParticles = false;
    settings.MSAASamples = 1; settings.ClearColor = {0, 0, 0, 1};
    for (int i = 0; i < 5; ++i) {
        auto scene = std::make_shared<Scene>();
        SceneHierarchyPanel panel(scene.get());
        auto entity = panel.CreatePrimitive(i, {0, 0, 0}, false);
        RENDER_CHECK(entity.HasComponent<MeshInstance3D>(), "real product factory has a mesh on selected backend");
        entity.GetComponent<TagComponent>().Tag = "unrelated name " + std::to_string(i);
        const auto id = entity.GetUUID();
        auto duplicate = scene->DuplicateEntity(entity);
        duplicate.GetComponent<TransformComponent>().Translation = {1000, 0, 0};
        auto unresolved = scene->CreateEntity("Sphere");
        unresolved.AddComponent<MeshInstance3D>();
        auto absent = scene->CreateEntity("Cube");
        const auto absentID = absent.GetUUID();
        const auto unresolvedID = unresolved.GetUUID();
        const auto path = root / ("primitive-" + std::to_string(i) + ".axle");
        RENDER_CHECK(SceneSerializer(scene.get()).Serialize(path), "actual primitive scene save");
        auto reopened = std::make_shared<Scene>();
        RENDER_CHECK(SceneSerializer(reopened.get()).Deserialize(path), "actual primitive scene reopen");
        auto restored = reopened->GetEntityByUUID(id);
        RENDER_CHECK(restored && restored.GetComponent<MeshInstance3D>().AssetPath == BuiltinMeshes[i].URI, "rename/save/reopen retains canonical identity");
        RENDER_CHECK(!reopened->GetEntityByUUID(absentID).HasComponent<MeshInstance3D>() &&
            reopened->GetEntityByUUID(unresolvedID).GetComponent<MeshInstance3D>().AssetPath.empty(), "names cannot reconstruct absent mesh references");
        auto copy = Scene::Copy(reopened);
        auto copied = copy->GetEntityByUUID(id);
        RENDER_CHECK(copied.GetComponent<MeshInstance3D>().AssetPath == BuiltinMeshes[i].URI, "reopened scene copy retains identity");
        const auto assembly = root / ("primitive-" + std::to_string(i) + "-assembly.axle");
        RENDER_CHECK(AssemblySerializer::Serialize(assembly, restored), "actual assembly save");
        const auto data = AssemblySerializer::LoadAssemblyData(assembly);
        RENDER_CHECK(data.IsValid && data.RootNode.HasMesh && data.RootNode.Mesh.AssetPath == BuiltinMeshes[i].URI, "assembly data resolves its own builtin, never an implicit cube");
        auto assemblyScene = std::make_shared<Scene>();
        auto assembled = AssemblySerializer::Deserialize(assembly, assemblyScene.get());
        RENDER_CHECK(assembled && assembled.GetComponent<MeshInstance3D>().AssetPath == BuiltinMeshes[i].URI, "actual assembly instantiation retains identity");
        PerspectiveCamera camera(45, 1, .1f, 200);
        const float distance = i == 4 ? 35.f : 4.f;
        const glm::vec3 position(distance * .55f, distance * .45f, distance);
        camera.SetViewMatrix(glm::lookAt(position, glm::vec3(0), glm::vec3(0, 1, 0)), position);
        auto draw = [&](const std::shared_ptr<Scene>& world, Entity target) {
            if (gl) return GLPixels(*world, target, camera);
            renderer.RenderScene(world, camera, &color, &depth, settings);
            return ReadTexture(color);
        };
        const auto pixels = draw(reopened, restored);
        size_t colored = 0;
        for (size_t pixel = 0; pixel + 3 < pixels.size(); pixel += 4)
            if (pixels[pixel] > 8 || pixels[pixel + 1] > 8 || pixels[pixel + 2] > 8) ++colored;
        RENDER_CHECK(pixels.size() == 96 * 96 * 4 && colored > 20 && colored < 96 * 96 - 20, "all five real primitives produce mesh pixels and background");
        RENDER_CHECK(draw(copy, copied) == pixels, "copy preserves actual mesh pixels after rename/persistence");
        if (!gl) {
            auto& mesh = restored.GetComponent<MeshInstance3D>().RHIMeshAsset;
            RENDER_CHECK(mesh && mesh->IsValid() && renderer.GetRenderedEntityCount() == 2, "unresolved mesh is skipped rather than guessed from its name");
            const glm::vec3 expected = i == 0 ? glm::vec3(1) : i == 4 ? glm::vec3(1, 0, 1) : glm::vec3(2);
            RENDER_CHECK(Near(mesh->GetBoundingSize(), expected, .001f), "native builtin bounds match the canonical GL unit geometry");
            RHIMeshRegistry::Get().Shutdown();
            RENDER_CHECK(mesh->IsValid() && draw(reopened, restored) == pixels, "cache shutdown cannot invalidate a scene's shared mesh");
            picker.RenderPickingPass(reopened, camera, 96, 96);
            RENDER_CHECK(picker.ReadPixel(48, 48) == static_cast<uint32_t>(static_cast<entt::entity>(restored)), "picking resolves the same primitive identity after rename");
        }
        std::ofstream output(root / ("primitive-" + std::to_string(i) + ".rgba"), std::ios::binary);
        output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        RENDER_CHECK(bool(output), "native/GL pixel artifact written into the owned fixture workspace");
    }
    return true;
}

int main(int argc, char** argv) {
    ConfigureFailureReporting(); Log::Init(); JobSystem::Init(2); PhysicsEngine::Init();
    const std::string mode = argc > 1 ? argv[1] : "policy";
    const auto root = std::filesystem::path(AXLE_TEST_WORKSPACE_DIR) / "opt04-primitives" / mode;
    std::filesystem::create_directories(root);
    AssetManager::Init(root);
    bool ok = false;
    std::unique_ptr<Window> window;
    try {
        ok = Policy(root);
        if (ok && mode != "policy") {
            const bool gl = mode == "gl" || mode == "fallback";
            if (gl) {
                ScopedRHIFault fault([&](std::string_view operation) -> std::optional<nri::Result> {
                    if (mode == "fallback" && operation == "CreateDevice") return nri::Result::FAILURE;
                    return {};
                });
                RHIBackendConfig config; config.PreferredBackend = mode == "gl" ? RHIType::OpenGL : RHIType::Direct3D12;
                ok = RHISwitcher::InitializeProduct(config, [&](GraphicsAPI api) {
                    WindowProps props; props.Title = "OPT-04 canonical primitive pixels"; props.Width = props.Height = 96; props.Maximized = false; props.GraphicsBackend = api;
                    window = std::make_unique<Window>(props);
                    return api == GraphicsAPI::OpenGL && window->GetNativeWindow() && RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress));
                });
            } else {
                RHIDeviceDesc desc; desc.PreferredBackend = mode == "vulkan" ? RHIType::Vulkan : RHIType::Direct3D12;
                desc.AllowFallback = false; desc.Headless = true; desc.EnableNRIValidation = true;
                desc.EnableValidation = RHIDevice::IsGraphicsAPIValidationAvailable(desc.PreferredBackend);
                ok = RHIDevice::Get().Init(desc) && !RHIDevice::Get().IsSoftwareAdapter() && RHIDevice::Get().GetBackend() == desc.PreferredBackend;
            }
            if (ok) ok = Pixels(root, gl);
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; ok = false; }
    RHIMeshRegistry::Get().Shutdown(); RHIDevice::Get().Shutdown(); window.reset();
    AssetManager::Shutdown(); PhysicsEngine::Shutdown(); JobSystem::Shutdown();
    ok = Axle::RenderTests::Check(Axle::RHI::GetRHIValidationErrorCount() == 0,
        "native validation reported no errors, including teardown", __FILE__, __LINE__) && ok;
    std::cout << "Primitives: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
