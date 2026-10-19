#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace Axle {

    struct CurlNoiseConfig {
        bool Enabled = false;
        float Frequency = 0.5f;                  // Spatial scale (lower = larger swirls)
        float Strength = 1.0f;                   // Turbulence force magnitude
        uint32_t Octaves = 1;                    // Detail octaves (1 = base noise, 2-4 = fBm)
        float Lacunarity = 2.0f;                 // Frequency multiplier per octave
        float Persistence = 0.5f;                // Amplitude multiplier per octave
        glm::vec3 ScrollSpeed = glm::vec3(0.0f); // Time evolution / advection rate
    };

    class CurlNoise {
    public:
        // Base scalar 3D Simplex noise in range [-1, 1]
        static float Simplex(const glm::vec3& p);

        // Fractal Brownian Motion (fBm) multi-octave scalar noise in range [-1, 1]
        static float FBM(const glm::vec3& p, uint32_t octaves = 3, float lacunarity = 2.0f, float persistence = 0.5f);

        // Vector potential field: 3 uncorrelated noise channels
        static glm::vec3 VectorPotential(const glm::vec3& p, uint32_t octaves = 1, float lacunarity = 2.0f, float persistence = 0.5f);

        // Evaluates divergence-free 3D curl noise vector field at position
        static glm::vec3 Evaluate(const glm::vec3& position,
                                  float frequency = 1.0f,
                                  float strength = 1.0f,
                                  uint32_t octaves = 1,
                                  float lacunarity = 2.0f,
                                  float persistence = 0.5f);

        // Evaluates time-animated curl noise using CurlNoiseConfig
        static glm::vec3 EvaluateAnimated(const glm::vec3& position,
                                          float time,
                                          const CurlNoiseConfig& config);

        // Computes numerical divergence (div = dFx/dx + dFy/dy + dFz/dz) at position
        // Mathematically guaranteed to be ~0 for curl fields (divergence-free)
        static float ComputeDivergence(const glm::vec3& position,
                                       float frequency = 1.0f,
                                       float strength = 1.0f,
                                       uint32_t octaves = 1);
    };

}
