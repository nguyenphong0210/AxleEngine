#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Mat44.h>

namespace Axle {

    // Converts GLM 3D vector to Jolt Vec3
    inline JPH::Vec3 ToJolt(const glm::vec3& v) {
        return JPH::Vec3(v.x, v.y, v.z);
    }

    // Converts GLM Quaternion to Jolt Quat (GLM order: w, x, y, z -> Jolt: x, y, z, w)
    inline JPH::Quat ToJolt(const glm::quat& q) {
        float len2 = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
        if (len2 > 1e-6f && std::abs(len2 - 1.0f) > 1e-4f) {
            float invLen = 1.0f / std::sqrt(len2);
            return JPH::Quat(q.x * invLen, q.y * invLen, q.z * invLen, q.w * invLen);
        }
        return JPH::Quat(q.x, q.y, q.z, q.w);
    }

    // Converts Jolt Vec3 to GLM 3D vector
    inline glm::vec3 ToGlm(const JPH::Vec3& v) {
        return glm::vec3(static_cast<f32>(v.GetX()), static_cast<f32>(v.GetY()), static_cast<f32>(v.GetZ()));
    }

    // Converts Jolt Quat to GLM Quaternion (Jolt order: x, y, z, w -> GLM: w, x, y, z)
    inline glm::quat ToGlm(const JPH::Quat& q) {
        return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }

    // Converts Jolt 4x4 matrix to GLM 4x4 matrix (column-major)
    inline glm::mat4 ToGlm(const JPH::Mat44& m) {
        glm::mat4 result;
        for (i32 col = 0; col < 4; ++col) {
            const JPH::Vec4 c = m.GetColumn4(col);
            result[col] = glm::vec4(c.GetX(), c.GetY(), c.GetZ(), c.GetW());
        }
        return result;
    }

}
