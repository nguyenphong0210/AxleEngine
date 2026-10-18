#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace Axle {

    struct Bone {
        int ID = -1;                          // Index in Skeleton::Bones array
        std::string Name;                     // Bone identifier (e.g. "Root", "Spine", "Head")
        int ParentID = -1;                    // Parent bone index (-1 = root)
        glm::mat4 LocalTransform{ 1.0f };     // Local bind pose relative to parent
        glm::mat4 InverseBindMatrix{ 1.0f };  // Maps vertex from model space into bone space
        std::vector<int> Children;            // Child bone indices in Skeleton
    };

}
