#include <Axle/CurlNoise.hpp>
#include <glm/gtc/noise.hpp>
#include <cmath>

namespace Axle {

    namespace {
        // High-entropy irrational coordinate offsets for independent vector potential components
        constexpr glm::vec3 OFFSET_Y(31.4159265f, -47.8530718f, 12.9876543f);
        constexpr glm::vec3 OFFSET_Z(-61.1234567f, 73.4567891f, -89.0123456f);
        constexpr float EPSILON = 0.001f;
    }

    float CurlNoise::Simplex(const glm::vec3& p) {
        return glm::simplex(p);
    }

    float CurlNoise::FBM(const glm::vec3& p, uint32_t octaves, float lacunarity, float persistence) {
        if (octaves <= 1) {
            return glm::simplex(p);
        }

        float total = 0.0f;
        float amplitude = 1.0f;
        float maxAmplitude = 0.0f;
        glm::vec3 freqPos = p;

        for (uint32_t i = 0; i < octaves; ++i) {
            total += glm::simplex(freqPos) * amplitude;
            maxAmplitude += amplitude;
            amplitude *= persistence;
            freqPos *= lacunarity;
        }

        return (maxAmplitude > 0.0f) ? (total / maxAmplitude) : 0.0f;
    }

    glm::vec3 CurlNoise::VectorPotential(const glm::vec3& p, uint32_t octaves, float lacunarity, float persistence) {
        if (octaves <= 1) {
            return glm::vec3(
                glm::simplex(p),
                glm::simplex(p + OFFSET_Y),
                glm::simplex(p + OFFSET_Z)
            );
        }

        return glm::vec3(
            FBM(p, octaves, lacunarity, persistence),
            FBM(p + OFFSET_Y, octaves, lacunarity, persistence),
            FBM(p + OFFSET_Z, octaves, lacunarity, persistence)
        );
    }

    glm::vec3 CurlNoise::Evaluate(const glm::vec3& position,
                                  float frequency,
                                  float strength,
                                  uint32_t octaves,
                                  float lacunarity,
                                  float persistence) {
        if (strength == 0.0f) {
            return glm::vec3(0.0f);
        }

        glm::vec3 p = position * frequency;
        const float eps = EPSILON;
        const float inv2Eps = 1.0f / (2.0f * eps);

        // Finite central differences for partial derivatives
        glm::vec3 pxPlus  = VectorPotential(p + glm::vec3(eps, 0.0f, 0.0f), octaves, lacunarity, persistence);
        glm::vec3 pxMinus = VectorPotential(p - glm::vec3(eps, 0.0f, 0.0f), octaves, lacunarity, persistence);

        glm::vec3 pyPlus  = VectorPotential(p + glm::vec3(0.0f, eps, 0.0f), octaves, lacunarity, persistence);
        glm::vec3 pyMinus = VectorPotential(p - glm::vec3(0.0f, eps, 0.0f), octaves, lacunarity, persistence);

        glm::vec3 pzPlus  = VectorPotential(p + glm::vec3(0.0f, 0.0f, eps), octaves, lacunarity, persistence);
        glm::vec3 pzMinus = VectorPotential(p - glm::vec3(0.0f, 0.0f, eps), octaves, lacunarity, persistence);

        // Curl = (dPsi_z/dy - dPsi_y/dz, dPsi_x/dz - dPsi_z/dx, dPsi_y/dx - dPsi_x/dy)
        float dPsi_z_dy = (pyPlus.z - pyMinus.z) * inv2Eps;
        float dPsi_y_dz = (pzPlus.y - pzMinus.y) * inv2Eps;

        float dPsi_x_dz = (pzPlus.x - pzMinus.x) * inv2Eps;
        float dPsi_z_dx = (pxPlus.z - pxMinus.z) * inv2Eps;

        float dPsi_y_dx = (pxPlus.y - pxMinus.y) * inv2Eps;
        float dPsi_x_dy = (pyPlus.x - pyMinus.x) * inv2Eps;

        glm::vec3 curl(
            dPsi_z_dy - dPsi_y_dz,
            dPsi_x_dz - dPsi_z_dx,
            dPsi_y_dx - dPsi_x_dy
        );

        return curl * strength;
    }

    glm::vec3 CurlNoise::EvaluateAnimated(const glm::vec3& position,
                                          float time,
                                          const CurlNoiseConfig& config) {
        if (!config.Enabled || config.Strength == 0.0f) {
            return glm::vec3(0.0f);
        }

        glm::vec3 animatedPos = position + config.ScrollSpeed * time;
        return Evaluate(animatedPos, config.Frequency, config.Strength, config.Octaves, config.Lacunarity, config.Persistence);
    }

    float CurlNoise::ComputeDivergence(const glm::vec3& position,
                                       float frequency,
                                       float strength,
                                       uint32_t octaves) {
        const float eps = (frequency > 0.0f) ? (EPSILON / frequency) : EPSILON;
        const float inv2Eps = 1.0f / (2.0f * eps);

        glm::vec3 fxPlus  = Evaluate(position + glm::vec3(eps, 0.0f, 0.0f), frequency, strength, octaves);
        glm::vec3 fxMinus = Evaluate(position - glm::vec3(eps, 0.0f, 0.0f), frequency, strength, octaves);

        glm::vec3 fyPlus  = Evaluate(position + glm::vec3(0.0f, eps, 0.0f), frequency, strength, octaves);
        glm::vec3 fyMinus = Evaluate(position - glm::vec3(0.0f, eps, 0.0f), frequency, strength, octaves);

        glm::vec3 fzPlus  = Evaluate(position + glm::vec3(0.0f, 0.0f, eps), frequency, strength, octaves);
        glm::vec3 fzMinus = Evaluate(position - glm::vec3(0.0f, 0.0f, eps), frequency, strength, octaves);

        float dfx_dx = (fxPlus.x - fxMinus.x) * inv2Eps;
        float dfy_dy = (fyPlus.y - fyMinus.y) * inv2Eps;
        float dfz_dz = (fzPlus.z - fzMinus.z) * inv2Eps;

        return dfx_dx + dfy_dy + dfz_dz;
    }

}
