#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIBuffer.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>


namespace Axle::RHI {

    // Maximum limits for Forward+ multi-light uniform buffer
    constexpr uint32_t MAX_POINT_LIGHTS = 16;
    constexpr uint32_t MAX_SPOT_LIGHTS  = 16;

    // -------------------------------------------------------------------------
    // Physical Illuminance Constants (Lux = Lumens / m^2)
    // -------------------------------------------------------------------------
    namespace PhysicalIlluminance {
        constexpr float DirectSunlight    = 100000.0f; // Direct noon sunlight (~100k - 120k Lux)
        constexpr float OvercastSky       = 20000.0f;  // Overcast daylight (~15k - 25k Lux)
        constexpr float SunsetSunrise     = 1000.0f;   // Sunset / golden hour (~400 - 1000 Lux)
        constexpr float DeepTwilight      = 10.0f;     // Civil twilight (~10 - 50 Lux)
        constexpr float FullMoon          = 0.25f;     // Full moon night (~0.1 - 0.25 Lux)
        constexpr float VehicleHeadlight  = 3500.0f;   // Automotive LED / Xenon headlight (~3000 - 4500 Lux)
        constexpr float VehicleBrakeLight = 400.0f;    // Automotive tail / brake light (~300 - 500 Lux)
        constexpr float StreetLamp        = 150.0f;    // Municipal sodium street light (~100 - 250 Lux)
    }

    // -------------------------------------------------------------------------
    // High-Level Light Descriptors & Helpers (User-facing / ECS)
    // -------------------------------------------------------------------------
    struct RHIDirectionalLight {
        glm::vec3 Direction{ 0.45f, 0.85f, 0.25f }; // Direction pointing towards sun
        float Illuminance = PhysicalIlluminance::DirectSunlight; // Lux
        glm::vec3 Color{ 1.0f, 0.98f, 0.92f }; // Sunlight warm tint
        float AmbientIntensity = 0.25f;
        glm::vec3 AmbientColor{ 0.22f, 0.25f, 0.32f }; // Atmospheric sky tint
        float ShadowBias = 0.0012f;
        float ShadowFilterRadius = 2.0f;

        static RHIDirectionalLight SunDefault() { return RHIDirectionalLight{}; }
        static RHIDirectionalLight OvercastDay() {
            RHIDirectionalLight l;
            l.Direction = glm::normalize(glm::vec3(0.2f, 0.95f, 0.2f));
            l.Illuminance = PhysicalIlluminance::OvercastSky;
            l.Color = glm::vec3(0.9f, 0.92f, 0.95f);
            l.AmbientIntensity = 0.45f;
            return l;
        }
        static RHIDirectionalLight Sunset() {
            RHIDirectionalLight l;
            l.Direction = glm::normalize(glm::vec3(0.85f, 0.25f, 0.45f));
            l.Illuminance = PhysicalIlluminance::SunsetSunrise;
            l.Color = glm::vec3(1.0f, 0.55f, 0.25f);
            l.AmbientIntensity = 0.2f;
            l.AmbientColor = glm::vec3(0.18f, 0.12f, 0.22f);
            return l;
        }
    };

    struct RHIPointLight {
        glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
        float Radius = 15.0f; // Max reach (m)
        glm::vec3 Color{ 1.0f, 1.0f, 1.0f };
        float Intensity = 1000.0f; // Physical luminous intensity (Lumens or Lux at 1m)
        float Falloff = 1.0f; // Falloff exponent (1.0 = physical inverse-square with smooth windowing)
        float MinRadius = 0.05f; // Radius smoothing to avoid singularity at distance 0

        static RHIPointLight VehicleBrakeLight(const glm::vec3& pos) {
            RHIPointLight l;
            l.Position = pos;
            l.Radius = 10.0f;
            l.Color = glm::vec3(1.0f, 0.05f, 0.02f); // Deep brake red
            l.Intensity = PhysicalIlluminance::VehicleBrakeLight;
            return l;
        }

        static RHIPointLight VehicleReverseLight(const glm::vec3& pos) {
            RHIPointLight l;
            l.Position = pos;
            l.Radius = 8.0f;
            l.Color = glm::vec3(0.95f, 0.98f, 1.0f); // Bright white-blue
            l.Intensity = 300.0f;
            return l;
        }

        static RHIPointLight EnvironmentLamp(const glm::vec3& pos, const glm::vec3& color = glm::vec3(1.0f, 0.75f, 0.4f), float radius = 20.0f) {
            RHIPointLight l;
            l.Position = pos;
            l.Radius = radius;
            l.Color = color;
            l.Intensity = PhysicalIlluminance::StreetLamp * 8.0f;
            return l;
        }
    };

    struct RHISpotLight {
        glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
        float Range = 45.0f; // Max range (m)
        glm::vec3 Direction{ 0.0f, 0.0f, -1.0f }; // Normalized forward cone vector
        float Intensity = 3500.0f; // Physical luminous intensity
        glm::vec3 Color{ 1.0f, 0.98f, 0.95f };
        float InnerConeAngle = 18.0f; // Degrees
        float OuterConeAngle = 32.0f; // Degrees
        float Falloff = 1.0f; // Inverse-square attenuation

        float GetCosInnerCone() const { return std::cos(glm::radians(InnerConeAngle)); }
        float GetCosOuterCone() const { return std::cos(glm::radians(OuterConeAngle)); }

        static RHISpotLight VehicleHeadlightHighBeam(const glm::vec3& pos, const glm::vec3& dir = glm::vec3(0.0f, 0.0f, -1.0f)) {
            RHISpotLight l;
            l.Position = pos;
            l.Direction = glm::normalize(dir);
            l.Range = 85.0f;
            l.InnerConeAngle = 14.0f;
            l.OuterConeAngle = 26.0f;
            l.Intensity = PhysicalIlluminance::VehicleHeadlight * 1.5f;
            l.Color = glm::vec3(0.92f, 0.96f, 1.0f); // Xenon cool white
            return l;
        }

        static RHISpotLight VehicleHeadlightLowBeam(const glm::vec3& pos, const glm::vec3& dir = glm::vec3(0.0f, -0.05f, -1.0f)) {
            RHISpotLight l;
            l.Position = pos;
            l.Direction = glm::normalize(dir);
            l.Range = 40.0f;
            l.InnerConeAngle = 20.0f;
            l.OuterConeAngle = 42.0f;
            l.Intensity = PhysicalIlluminance::VehicleHeadlight;
            l.Color = glm::vec3(1.0f, 0.95f, 0.88f); // Warm halogen / LED
            return l;
        }

        static RHISpotLight VehicleFogLight(const glm::vec3& pos, const glm::vec3& dir = glm::vec3(0.0f, -0.1f, -1.0f)) {
            RHISpotLight l;
            l.Position = pos;
            l.Direction = glm::normalize(dir);
            l.Range = 25.0f;
            l.InnerConeAngle = 28.0f;
            l.OuterConeAngle = 55.0f;
            l.Intensity = 1800.0f;
            l.Color = glm::vec3(1.0f, 0.78f, 0.15f); // Amber yellow fog
            return l;
        }
    };

    // -------------------------------------------------------------------------
    // GPU-Layout Structures (Hardware 16-byte aligned for HLSL/GLSL Constant Buffers)
    // -------------------------------------------------------------------------
    struct alignas(16) RHIGPUDirectionalLight {
        glm::vec4 DirectionAndIlluminance{ 0.45f, 0.85f, 0.25f, 100000.0f }; // xyz: Dir, w: Lux
        glm::vec4 ColorAndAmbient{ 1.0f, 0.98f, 0.92f, 0.25f };               // xyz: Color, w: Ambient
        glm::vec4 AmbientColor{ 0.22f, 0.25f, 0.32f, 1.0f };                  // xyz: Ambient Color, w: 1.0
        glm::vec4 ShadowParams{ 0.0012f, 2.0f, 0.0f, 0.0f };                  // x: Bias, y: Radius, zw: 0
    }; // 64 bytes
    static_assert(sizeof(RHIGPUDirectionalLight) == 64, "RHIGPUDirectionalLight must be 64 bytes!");

    struct alignas(16) RHIGPUPointLight {
        glm::vec4 PositionAndRadius{ 0.0f, 0.0f, 0.0f, 0.0f }; // xyz: Pos, w: Radius (0 = inactive)
        glm::vec4 ColorAndIntensity{ 0.0f, 0.0f, 0.0f, 0.0f }; // xyz: Color, w: Intensity
        glm::vec4 Params{ 1.0f, 0.05f, 0.0f, 0.0f };           // x: Falloff, y: MinRadius, zw: 0
        glm::vec4 Reserved{ 0.0f };                            // 64 bytes total
    }; // 64 bytes
    static_assert(sizeof(RHIGPUPointLight) == 64, "RHIGPUPointLight must be 64 bytes!");

    struct alignas(16) RHIGPUSpotLight {
        glm::vec4 PositionAndRange{ 0.0f, 0.0f, 0.0f, 0.0f };      // xyz: Pos, w: Range (0 = inactive)
        glm::vec4 DirectionAndIntensity{ 0.0f, 0.0f, -1.0f, 0.0f };// xyz: Dir, w: Intensity
        glm::vec4 ColorAndInnerAngle{ 0.0f, 0.0f, 0.0f, 1.0f };    // xyz: Color, w: cos(InnerAngle)
        glm::vec4 AngleParams{ 0.8f, 1.0f, 0.0f, 0.0f };           // x: cos(OuterAngle), y: Falloff, zw: 0
    }; // 64 bytes
    static_assert(sizeof(RHIGPUSpotLight) == 64, "RHIGPUSpotLight must be 64 bytes!");

    struct alignas(256) RHIMultiLightBufferData {
        RHIGPUDirectionalLight SunLight;                            // 64 bytes (Offset 0)
        glm::uvec4 LightCounts{ 0, 0, 1, 0 };                       // 16 bytes (Offset 64) - x: NumPoint, y: NumSpot, z: HasSun, w: Mode
        glm::vec4 CameraPosAndExposure{ 0.0f, 0.0f, 0.0f, 1.0f };   // 16 bytes (Offset 80) - xyz: CamPos, w: Exposure
        glm::vec4 ReservedHeader[2]{};                              // 32 bytes (Offset 96..128)

        RHIGPUPointLight PointLights[MAX_POINT_LIGHTS];             // 16 * 64 = 1024 bytes (Offset 128..1152)
        RHIGPUSpotLight  SpotLights[MAX_SPOT_LIGHTS];               // 16 * 64 = 1024 bytes (Offset 1152..2176)
        glm::vec4 ReservedTail[8]{};                                // 128 bytes (Offset 2176..2304) -> 2304 bytes total (9 * 256 bytes)

        static constexpr uint32_t GetByteSize() { return sizeof(RHIMultiLightBufferData); }
    };
    static_assert(sizeof(RHIMultiLightBufferData) == 2304, "RHIMultiLightBufferData must be exactly 2304 bytes (256-byte aligned)!");

    // -------------------------------------------------------------------------
    // RHIMultiLightBuffer: GPU-Resident Constant Buffer Subsystem
    // -------------------------------------------------------------------------
    class RHIMultiLightBuffer {
    public:
        RHIMultiLightBuffer() = default;
        ~RHIMultiLightBuffer();

        bool Init(RHIDevice& device = RHIDevice::Get());
        void Shutdown();

        bool IsValid() const { return m_Buffer.IsValid(); }

        // Upload custom raw multi-light data
        bool Update(const RHIMultiLightBufferData& data);


        // Clear all lights and set default sun
        void ResetToDefaultSun();

        // Add individual lights dynamically
        bool SetSunLight(const RHIDirectionalLight& sun);
        bool AddPointLight(const RHIPointLight& light);
        bool AddSpotLight(const RHISpotLight& light);
        void ClearDynamicLights();

        const RHIMultiLightBufferData& GetData() const { return m_Data; }
        nri::Descriptor* GetCBV() const { return m_Buffer.GetCBV(); }
        nri::Buffer* GetBuffer() const { return m_Buffer.GetNative(); }

    private:
        RHIDevice* m_Device = nullptr;
        RHIBuffer m_Buffer;
        RHIMultiLightBufferData m_Data;
        bool m_Initialized = false;
    };

}
