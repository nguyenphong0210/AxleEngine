#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/Entity.hpp>
namespace Axle {
    Entity::Entity(entt::entity handle, Scene* scene) : Entity(handle, static_cast<SceneData*>(scene)) {}
    Entity UICanvasComponent::GetDataSourceEntity(Scene* scene, Entity currentEntity) const {
        if (DataSourceEntityID == 0) {
            return currentEntity;
        }
        if (scene) {
            Entity target = scene->GetEntityByUUID(DataSourceEntityID);
            if (target) {
                return target;
            }
        }
        return currentEntity;
    }

}
