#include <Axle/RHI/RHILight.hpp>
#include <Axle/Log.hpp>

#include <cstring>

namespace Axle::RHI {

    RHIMultiLightBuffer::~RHIMultiLightBuffer() {
        Shutdown();
    }

    bool RHIMultiLightBuffer::Init(RHIDevice& device) {
        if (m_Initialized) return true;
        if (!device.CanCreateResources()) {
            AXLE_CORE_ERROR("RHIMultiLightBuffer::Init failed: RHIDevice is not initialized!");
            return false;
        }

        m_Device = &device;

        // Allocate constant buffer in HostUpload memory (aligned to 256 bytes)
        RHIBufferDesc desc = RHIBufferDesc::Constant(sizeof(RHIMultiLightBufferData), RHIMemoryLocation::HostUpload, "MultiLightConstantBuffer");
        if (!m_Buffer.Init(desc)) {
            AXLE_CORE_ERROR("RHIMultiLightBuffer: Failed to initialize multi-light constant buffer!");
            return false;
        }

        ResetToDefaultSun();
        m_Initialized = true;
        AXLE_CORE_INFO("RHIMultiLightBuffer: Initialized Forward+ Multi-Light GPU Buffer (Capacity: 1 Sun, {0} Points, {1} Spots, Size: {2} bytes).",
                       MAX_POINT_LIGHTS, MAX_SPOT_LIGHTS, sizeof(RHIMultiLightBufferData));
        return true;
    }

    void RHIMultiLightBuffer::Shutdown() {
        if (!m_Initialized) return;
        m_Buffer.Shutdown();
        m_Device = nullptr;
        m_Initialized = false;
    }

    bool RHIMultiLightBuffer::Update(const RHIMultiLightBufferData& data) {
        m_Data = data;
        if (!m_Buffer.IsValid()) return false;
        return m_Buffer.UpdateData(&m_Data, sizeof(RHIMultiLightBufferData));
    }

    void RHIMultiLightBuffer::ResetToDefaultSun() {
        m_Data = RHIMultiLightBufferData{};

        // Setup default direct sunlight (100,000 Lux)
        m_Data.SunLight.DirectionAndIlluminance = glm::vec4(glm::normalize(glm::vec3(0.45f, 0.85f, 0.25f)), PhysicalIlluminance::DirectSunlight);
        m_Data.SunLight.ColorAndAmbient = glm::vec4(1.0f, 0.98f, 0.92f, 0.25f);
        m_Data.SunLight.AmbientColor = glm::vec4(0.22f, 0.25f, 0.32f, 1.0f);
        m_Data.SunLight.ShadowParams = glm::vec4(0.0012f, 2.0f, 0.0f, 0.0f);

        m_Data.LightCounts = glm::uvec4(0, 0, 1, 0); // 0 points, 0 spots, 1 sun, mode 0
        m_Data.CameraPosAndExposure = glm::vec4(0.0f, 5.0f, 10.0f, 1.0f);

        if (m_Buffer.IsValid()) {
            m_Buffer.UpdateData(&m_Data, sizeof(RHIMultiLightBufferData));
        }
    }

    bool RHIMultiLightBuffer::SetSunLight(const RHIDirectionalLight& sun) {
        m_Data.SunLight.DirectionAndIlluminance = glm::vec4(glm::normalize(sun.Direction), sun.Illuminance);
        m_Data.SunLight.ColorAndAmbient = glm::vec4(sun.Color, sun.AmbientIntensity);
        m_Data.SunLight.AmbientColor = glm::vec4(sun.AmbientColor, 1.0f);
        m_Data.SunLight.ShadowParams = glm::vec4(sun.ShadowBias, sun.ShadowFilterRadius, 0.0f, 0.0f);
        m_Data.LightCounts.z = 1;

        if (m_Buffer.IsValid()) {
            return m_Buffer.UpdateData(&m_Data, sizeof(RHIMultiLightBufferData));
        }
        return true;
    }

    bool RHIMultiLightBuffer::AddPointLight(const RHIPointLight& light) {
        uint32_t count = m_Data.LightCounts.x;
        if (count >= MAX_POINT_LIGHTS) return false;

        m_Data.PointLights[count].PositionAndRadius = glm::vec4(light.Position, light.Radius);
        m_Data.PointLights[count].ColorAndIntensity = glm::vec4(light.Color, light.Intensity);
        m_Data.PointLights[count].Params = glm::vec4(light.Falloff, std::max(0.0025f, light.MinRadius * light.MinRadius), 0.0f, 0.0f);

        m_Data.LightCounts.x = count + 1;

        if (m_Buffer.IsValid()) {
            return m_Buffer.UpdateData(&m_Data, sizeof(RHIMultiLightBufferData));
        }
        return true;
    }

    bool RHIMultiLightBuffer::AddSpotLight(const RHISpotLight& light) {
        uint32_t count = m_Data.LightCounts.y;
        if (count >= MAX_SPOT_LIGHTS) return false;

        float cosInner = light.GetCosInnerCone();
        float cosOuter = light.GetCosOuterCone();

        m_Data.SpotLights[count].PositionAndRange = glm::vec4(light.Position, light.Range);
        m_Data.SpotLights[count].DirectionAndIntensity = glm::vec4(glm::normalize(light.Direction), light.Intensity);
        m_Data.SpotLights[count].ColorAndInnerAngle = glm::vec4(light.Color, cosInner);
        m_Data.SpotLights[count].AngleParams = glm::vec4(cosOuter, light.Falloff, 0.0f, 0.0f);

        m_Data.LightCounts.y = count + 1;

        if (m_Buffer.IsValid()) {
            return m_Buffer.UpdateData(&m_Data, sizeof(RHIMultiLightBufferData));
        }
        return true;
    }

    bool RHIMultiLightBuffer::AddRectLight(const RHIRectLight& light) {
        uint32_t count = static_cast<uint32_t>(m_Data.ReservedHeader[1].x);
        if (count >= 2) return false;

        auto safeNormalize = [](const glm::vec3& v, const glm::vec3& fallback) {
            const float lenSq = glm::dot(v, v);
            if (!std::isfinite(lenSq) || lenSq <= 1e-12f) return fallback;
            return v * (1.0f / std::sqrt(lenSq));
        };
        const glm::vec3 dir = safeNormalize(light.Direction, glm::vec3(0.0f, 0.0f, -1.0f));
        const glm::vec3 orthoFallback = (std::abs(dir.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 orthoCandidate = light.Up - dir * glm::dot(light.Up, dir);
        const glm::vec3 up = safeNormalize(orthoCandidate, safeNormalize(glm::cross(orthoFallback, dir), glm::vec3(0.0f, 1.0f, 0.0f)));

        const float width = (std::isfinite(light.Width) && light.Width > 0.01f) ? light.Width : 0.01f;
        const float height = (std::isfinite(light.Height) && light.Height > 0.01f) ? light.Height : 0.01f;
        const float signedHeight = (light.Shape == 1u) ? -height : height;
        const float range = (std::isfinite(light.Range) && light.Range > 0.0f) ? light.Range : 0.0f;
        const float absIntensity = std::isfinite(light.Intensity) ? std::abs(light.Intensity) : 0.0f;
        const float intensity = light.TwoSided ? -absIntensity : absIntensity;
        const glm::vec3 safeColor(
            (std::isfinite(light.Color.r) && light.Color.r > 0.0f) ? light.Color.r : 0.0f,
            (std::isfinite(light.Color.g) && light.Color.g > 0.0f) ? light.Color.g : 0.0f,
            (std::isfinite(light.Color.b) && light.Color.b > 0.0f) ? light.Color.b : 0.0f);

        m_Data.RectLights[count].PositionAndWidth = glm::vec4(light.Position, width);
        m_Data.RectLights[count].DirectionAndHeight = glm::vec4(dir, signedHeight);
        m_Data.RectLights[count].UpAndRange = glm::vec4(up, range);
        m_Data.RectLights[count].ColorAndIntensity = glm::vec4(safeColor, intensity);

        m_Data.ReservedHeader[1].x = static_cast<float>(count + 1);

        if (m_Buffer.IsValid()) {
            return m_Buffer.UpdateData(&m_Data, sizeof(RHIMultiLightBufferData));
        }
        return true;
    }

    void RHIMultiLightBuffer::ClearDynamicLights() {
        for (uint32_t i = 0; i < MAX_POINT_LIGHTS; ++i) {
            m_Data.PointLights[i] = RHIGPUPointLight{};
        }
        for (uint32_t i = 0; i < MAX_SPOT_LIGHTS; ++i) {
            m_Data.SpotLights[i] = RHIGPUSpotLight{};
        }
        m_Data.RectLights[0] = RHIGPURectLight{};
        m_Data.RectLights[1] = RHIGPURectLight{};
        m_Data.LightCounts.x = 0;
        m_Data.LightCounts.y = 0;
        m_Data.ReservedHeader[1].x = 0.0f;

        if (m_Buffer.IsValid()) {
            m_Buffer.UpdateData(&m_Data, sizeof(RHIMultiLightBufferData));
        }
    }



}
