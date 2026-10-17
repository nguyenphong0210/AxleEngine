#include <Axle/AudioListener.hpp>
#include <Axle/AudioEngine.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <miniaudio.h>

namespace Axle {

    glm::vec3 AudioListener::s_Position = glm::vec3(0.0f);
    glm::vec3 AudioListener::s_Forward  = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 AudioListener::s_Up       = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 AudioListener::s_Velocity = glm::vec3(0.0f);

    void AudioListener::SetPosition(const glm::vec3& position) {
        s_Position = position;
        ma_engine* engine = AudioEngine::GetNativeEngine();
        if (engine && !AudioEngine::IsHeadless()) {
            ma_engine_listener_set_position(engine, 0, position.x, position.y, position.z);
        }
    }

    glm::vec3 AudioListener::GetPosition() {
        return s_Position;
    }

    void AudioListener::SetOrientation(const glm::vec3& forward, const glm::vec3& up) {
        s_Forward = forward;
        s_Up = up;
        ma_engine* engine = AudioEngine::GetNativeEngine();
        if (engine && !AudioEngine::IsHeadless()) {
            ma_engine_listener_set_direction(engine, 0, forward.x, forward.y, forward.z);
            ma_engine_listener_set_world_up(engine, 0, up.x, up.y, up.z);
        }
    }

    glm::vec3 AudioListener::GetForward() {
        return s_Forward;
    }

    glm::vec3 AudioListener::GetUp() {
        return s_Up;
    }

    void AudioListener::SetVelocity(const glm::vec3& velocity) {
        s_Velocity = velocity;
        ma_engine* engine = AudioEngine::GetNativeEngine();
        if (engine && !AudioEngine::IsHeadless()) {
            ma_engine_listener_set_velocity(engine, 0, velocity.x, velocity.y, velocity.z);
        }
    }

    glm::vec3 AudioListener::GetVelocity() {
        return s_Velocity;
    }

    void AudioListener::SetMasterVolume(f32 volume) {
        AudioEngine::SetMasterVolume(volume);
    }

    f32 AudioListener::GetMasterVolume() {
        return AudioEngine::GetMasterVolume();
    }

}
