#pragma once
#include <Axle/Types.hpp>
#include <glm/glm.hpp>

namespace Axle {

    class AudioListener {
    public:
        static void SetPosition(const glm::vec3& position);
        static glm::vec3 GetPosition();

        static void SetOrientation(const glm::vec3& forward, const glm::vec3& up);
        static glm::vec3 GetForward();
        static glm::vec3 GetUp();

        static void SetVelocity(const glm::vec3& velocity);
        static glm::vec3 GetVelocity();

        static void SetMasterVolume(f32 volume);
        static f32 GetMasterVolume();

    private:
        static glm::vec3 s_Position;
        static glm::vec3 s_Forward;
        static glm::vec3 s_Up;
        static glm::vec3 s_Velocity;
    };

}
