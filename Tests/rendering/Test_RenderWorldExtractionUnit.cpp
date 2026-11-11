#include "RenderTestCommon.hpp"
#include <Axle/RenderWorld.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/DataComponents.hpp>
#include <Axle/SceneLighting.hpp>

namespace Axle::RenderTests {

    static bool Test_RenderWorldEmptyScene() {
        Scene scene;
        RenderWorld world = ExtractRenderWorld(scene);
        RENDER_CHECK(world.IsEmpty(), "Empty scene extracts empty RenderWorld");
        RENDER_CHECK(world.GetMeshCount() == 0, "No meshes in empty scene");
        RENDER_CHECK(world.GetPointLightCount() == 0, "No point lights in empty scene");
        RENDER_CHECK(world.GetSpotLightCount() == 0, "No spot lights in empty scene");
        RENDER_CHECK(!world.DirectionalLight.has_value(), "No directional light in empty scene");
        return true;
    }

    static bool Test_RenderWorldMeshAndMaterialExtraction() {
        Scene scene;
        Entity e1 = scene.CreateEntity("CubeEntity");
        auto& tc1 = e1.GetComponent<TransformComponent>();
        tc1.Translation = glm::vec3(2.0f, 3.0f, 4.0f);
        tc1.Scale = glm::vec3(1.5f);

        auto& mc1 = e1.AddComponent<MeshInstance3D>();
        mc1.AssetPath = "builtin://cube";
        mc1.Color1 = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        mc1.Color2 = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
        mc1.TileScale = 2.0f;
        mc1.PatternMode = 2;

        std::unordered_set<uint32_t> selected{ static_cast<uint32_t>(e1) };
        SceneExtractionOptions options;
        options.SelectedEntities = &selected;

        RenderWorld world = ExtractRenderWorld(scene, options);
        RENDER_CHECK(!world.IsEmpty(), "Extracted world is not empty");
        RENDER_CHECK(world.GetMeshCount() == 1, "Exactly one mesh instance extracted");

        const auto& meshInst = world.Meshes[0];
        RENDER_CHECK(meshInst.EntityID == static_cast<uint32_t>(e1), "EntityID matches");
        RENDER_CHECK(meshInst.StableUUID == e1.GetUUID(), "StableUUID matches IDComponent");
        RENDER_CHECK(meshInst.MeshURI == "builtin://cube", "MeshURI matches");
        RENDER_CHECK(meshInst.BuiltinId.has_value() && meshInst.BuiltinId.value() == BuiltinMeshId::Cube, "BuiltinId parsed correctly");
        RENDER_CHECK(meshInst.Selected, "Entity correctly marked as selected");
        RENDER_CHECK(meshInst.Material.AlbedoColor == mc1.Color1, "Color1 matches");
        RENDER_CHECK(meshInst.Material.SecondaryColor == mc1.Color2, "Color2 matches");
        RENDER_CHECK(meshInst.Material.TileScale == glm::vec2(2.0f), "TileScale matches");
        RENDER_CHECK(meshInst.Material.PatternMode == 2, "PatternMode matches");

        // Verify world transform translation matches
        glm::vec3 worldPos = glm::vec3(meshInst.WorldTransform[3]);
        RENDER_CHECK(glm::distance(worldPos, glm::vec3(2.0f, 3.0f, 4.0f)) < 0.001f, "Extracted world transform translation matches");

        // Verify entity to stable UUID lookup
        auto stableUUID = world.FindStableUUID(static_cast<uint32_t>(e1));
        RENDER_CHECK(stableUUID.has_value() && stableUUID.value() == e1.GetUUID(), "FindStableUUID correctly returns entity UUID");

        return true;
    }

    static bool Test_RenderWorldNestedTransforms() {
        Scene scene;
        Entity parent = scene.CreateEntity("Parent");
        auto& ptc = parent.GetComponent<TransformComponent>();
        ptc.Translation = glm::vec3(10.0f, 0.0f, 0.0f);

        Entity child = scene.CreateEntity("Child");
        auto& ctc = child.GetComponent<TransformComponent>();
        ctc.Translation = glm::vec3(0.0f, 5.0f, 0.0f);
        auto& cmc = child.AddComponent<MeshInstance3D>();
        cmc.AssetPath = "builtin://sphere";

        scene.SetParent(child, parent, false); // Child local offset is relative to parent

        RenderWorld world = ExtractRenderWorld(scene);
        RENDER_CHECK(world.GetMeshCount() == 1, "One mesh extracted from child");

        glm::vec3 childWorldPos = glm::vec3(world.Meshes[0].WorldTransform[3]);
        RENDER_CHECK(glm::distance(childWorldPos, glm::vec3(10.0f, 5.0f, 0.0f)) < 0.001f, "Parent transform propagated correctly to child mesh");

        return true;
    }

    static bool Test_RenderWorldLightsAndSkyboxExtraction() {
        Scene scene;
        // Directional Light
        Entity sun = scene.CreateEntity("Sun");
        auto& dlc = sun.AddComponent<DirectionalLightComponent>();
        dlc.Color = glm::vec3(1.0f, 0.95f, 0.9f);
        dlc.Intensity = 2.5f;

        // Point Light
        Entity lamp = scene.CreateEntity("Lamp");
        auto& ltc = lamp.GetComponent<TransformComponent>();
        ltc.Translation = glm::vec3(1.0f, 2.0f, 3.0f);
        auto& plc = lamp.AddComponent<PointLightComponent>();
        plc.Color = glm::vec3(0.0f, 1.0f, 1.0f);
        plc.Radius = 15.0f;

        // Spot Light
        Entity spot = scene.CreateEntity("Spot");
        auto& stc = spot.GetComponent<TransformComponent>();
        stc.Translation = glm::vec3(5.0f, 10.0f, 5.0f);
        auto& slc = spot.AddComponent<SpotLightComponent>();
        slc.Intensity = 12.0f;
        slc.Range = 40.0f;

        // Skybox
        Entity sky = scene.CreateEntity("Sky");
        auto& sc = sky.AddComponent<SkyboxComponent>();
        sc.Type = SkyboxType::HDRCubemap;
        sc.EnvironmentLighting = true;
        sc.HDRPath = "assets/textures/sky.hdr";
        sc.Intensity = 1.8f;

        RenderWorld world = ExtractRenderWorld(scene);
        RENDER_CHECK(world.DirectionalLight.has_value(), "Directional light extracted");
        RENDER_CHECK(world.DirectionalLight->Intensity == 2.5f, "Directional light intensity matches");
        RENDER_CHECK(world.DirectionalLight->Color == dlc.Color, "Directional light color matches");

        RENDER_CHECK(world.GetPointLightCount() == 1, "Point light extracted");
        RENDER_CHECK(glm::distance(world.PointLights[0].Position, glm::vec3(1.0f, 2.0f, 3.0f)) < 0.001f, "Point light position matches world transform");
        RENDER_CHECK(world.PointLights[0].Radius == 15.0f, "Point light radius matches");

        RENDER_CHECK(world.GetSpotLightCount() == 1, "Spot light extracted");
        RENDER_CHECK(glm::distance(world.SpotLights[0].Position, glm::vec3(5.0f, 10.0f, 5.0f)) < 0.001f, "Spot light position matches world transform");
        RENDER_CHECK(world.SpotLights[0].Range == 40.0f, "Spot light range matches");

        RENDER_CHECK(world.Skybox.Enabled, "Skybox enabled");
        RENDER_CHECK(world.Skybox.EnvironmentLighting, "Skybox environment lighting opt-in extracted");
        RENDER_CHECK(world.Skybox.Type == static_cast<int>(SkyboxType::HDRCubemap), "Skybox type matches");
        RENDER_CHECK(world.Skybox.HDRPath == "assets/textures/sky.hdr", "Skybox HDR path matches");
        RENDER_CHECK(world.Skybox.Intensity == 1.8f, "Skybox intensity matches");

        return true;
    }

    static bool Test_SkyBackgroundDoesNotImplyLighting() {
        RenderWorld world;
        world.Skybox.Authored = true;
        world.Skybox.Enabled = true;
        world.Skybox.EnvironmentLighting = false;
        const auto lighting = ResolveSceneLighting(world);
        RENDER_CHECK(lighting.Environment, "Sky background remains visible when authored");
        RENDER_CHECK(!lighting.EnvironmentLighting, "Sky background does not opt into environment lighting");
        RENDER_CHECK(lighting.AmbientIntensity == 0.0f, "Sky background does not illuminate receivers by default");

        world.Skybox.EnvironmentLighting = true;
        const auto enabled = ResolveSceneLighting(world);
        RENDER_CHECK(enabled.EnvironmentLighting && enabled.AmbientIntensity > 0.0f,
                     "Explicit sky environment lighting contributes ambient energy");
        return true;
    }

    static bool Test_RenderWorldImmutabilityAndIsolation() {
        Scene scene;
        Entity e1 = scene.CreateEntity("ObjectA");
        e1.AddComponent<MeshInstance3D>().AssetPath = "builtin://cube";
        Entity e2 = scene.CreateEntity("ObjectB");
        e2.AddComponent<MeshInstance3D>().AssetPath = "builtin://sphere";

        // Snapshot
        RenderWorld snapshot = ExtractRenderWorld(scene);
        RENDER_CHECK(snapshot.GetMeshCount() == 2, "Snapshot has 2 meshes");

        // Mutate scene after extraction: delete e1, change e2 transform
        scene.DestroyEntity(e1);
        auto& tc2 = e2.GetComponent<TransformComponent>();
        tc2.Translation = glm::vec3(100.0f, 0.0f, 0.0f);
        scene.UpdateWorldTransforms();

        // Verify snapshot remains completely unaffected
        RENDER_CHECK(snapshot.GetMeshCount() == 2, "Snapshot mesh count unaffected by subsequent entity deletion");
        const RenderMeshInstance* snap1 = nullptr;
        const RenderMeshInstance* snap2 = nullptr;
        for (const auto& m : snapshot.Meshes) {
            if (m.EntityID == static_cast<uint32_t>(e1)) snap1 = &m;
            if (m.EntityID == static_cast<uint32_t>(e2)) snap2 = &m;
        }
        RENDER_CHECK(snap1 != nullptr && snap1->MeshURI == "builtin://cube", "First mesh unchanged in snapshot");
        RENDER_CHECK(snap2 != nullptr && snap2->MeshURI == "builtin://sphere", "Second mesh unchanged in snapshot");
        RENDER_CHECK(snap2 != nullptr && glm::distance(glm::vec3(snap2->WorldTransform[3]), glm::vec3(0.0f)) < 0.001f,
            "Snapshot transform unaffected by subsequent scene mutation");

        // Re-extracting scene now reflects the new state
        RenderWorld newWorld = ExtractRenderWorld(scene);
        RENDER_CHECK(newWorld.GetMeshCount() == 1, "New world reflects deleted entity");
        RENDER_CHECK(newWorld.Meshes[0].EntityID == static_cast<uint32_t>(e2), "Remaining mesh is e2");
        RENDER_CHECK(glm::distance(glm::vec3(newWorld.Meshes[0].WorldTransform[3]), glm::vec3(100.0f, 0.0f, 0.0f)) < 0.001f,
            "New world reflects updated translation");

        return true;
    }

    static bool Test_OwnedSnapshotValues() {
        Scene scene;
        auto entity = scene.CreateEntity("Snapshot owner");
        auto& mesh = entity.AddComponent<MeshInstance3D>(); mesh.AssetPath = "builtin://cube";
        auto& material = entity.AddComponent<MaterialComponent>(); material.AlbedoColor = {1,0,0,1};
        auto& emitter = entity.AddComponent<ParticleEmitterComponent>();
        const auto beforeRHI = material.RHIMaterialAsset;
        const auto beforeVAO = mesh.VAO;
        auto snapshot = ExtractRenderWorld(static_cast<const Scene&>(scene));
        RENDER_CHECK(material.RHIMaterialAsset == beforeRHI && mesh.VAO == beforeVAO && !mesh.RHIMeshAsset,
            "const extraction never publishes caches or creates GPU resources");
        RENDER_CHECK(snapshot.Meshes.size() == 1 && snapshot.Meshes[0].Material.MaterialAsset && snapshot.Meshes[0].Material.RHIMaterialAsset,
            "snapshot owns CPU material values");
        material.AlbedoColor = {0,1,0,1}; material.SyncToRHI();
        RENDER_CHECK(snapshot.Meshes[0].Material.RHIMaterialAsset->GetDesc().BaseColor.r == 1 &&
            snapshot.Meshes[0].Material.MaterialAsset->GetAlbedoColor().r == 1, "later authoring cannot change snapshot material values");
        return true;
    }

} // namespace Axle::RenderTests

bool RunRenderWorldExtractionUnitTests() {
    std::cout << "  [RenderWorld] Running RenderWorld extraction unit tests...\n";
    if (!Axle::RenderTests::Test_OwnedSnapshotValues()) return false;
    if (!Axle::RenderTests::Test_RenderWorldEmptyScene()) return false;
    if (!Axle::RenderTests::Test_RenderWorldMeshAndMaterialExtraction()) return false;
    if (!Axle::RenderTests::Test_RenderWorldNestedTransforms()) return false;
    if (!Axle::RenderTests::Test_RenderWorldLightsAndSkyboxExtraction()) return false;
    if (!Axle::RenderTests::Test_SkyBackgroundDoesNotImplyLighting()) return false;
    if (!Axle::RenderTests::Test_RenderWorldImmutabilityAndIsolation()) return false;
    std::cout << "  [RenderWorld] All RenderWorld extraction unit tests passed.\n";
    return true;
}
