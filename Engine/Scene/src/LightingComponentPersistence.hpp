#pragma once
#include "LocalShadowPersistence.hpp"
#include <Axle/Components.hpp>
#include <Axle/SceneRenderBridge.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <ostream>
#include <string>

namespace Axle {

    inline std::string TrimLightingPersistenceText(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    inline void WriteDirectionalLightComponent(std::ostream& out, const DirectionalLightComponent& dlc) {
        out << "    DirectionalLightComponent:\n";
        out << "      Color: [ " << dlc.Color.r << " , " << dlc.Color.g << " , " << dlc.Color.b << " ]\n";
        out << "      Intensity: " << dlc.Intensity << "\n";
        out << "      AmbientIntensity: " << dlc.AmbientIntensity << "\n";
        out << "      Azimuth: " << dlc.Azimuth << "\n";
        out << "      Elevation: " << dlc.Elevation << "\n";
        out << "      CastShadows: " << (dlc.CastShadows ? "true" : "false") << "\n";
        out << "      ShadowOrthoSize: " << dlc.ShadowOrthoSize << "\n";
        out << "      ShadowBias: " << dlc.ShadowBias << "\n";
        out << "      ShadowSlopeBias: " << dlc.ShadowSlopeBias << "\n";
        out << "      ShadowNormalBias: " << dlc.ShadowNormalBias << "\n";
        out << "      ShadowFilterRadius: " << dlc.ShadowFilterRadius << "\n";
        out << "      ShadowQuality: " << dlc.ShadowQuality << "\n";
        out << "      CascadeSplitLambda: " << dlc.CascadeSplitLambda << "\n";
        out << "      MaxShadowDistance: " << dlc.MaxShadowDistance << "\n";
        out << "      Exposure: " << dlc.Exposure << "\n";
        out << "      ToneMappingMode: " << dlc.ToneMappingMode << "\n";
        out << "      Gamma: " << dlc.Gamma << "\n";
    }

    inline void ReadDirectionalLightComponentLine(const std::string& trimmed, DirectionalLightComponent& dlc) {
        if (trimmed.rfind("Color:", 0) == 0) {
            float r = 1, g = 1, b = 1;
            std::sscanf(trimmed.c_str(), "Color: [ %f , %f , %f ]", &r, &g, &b);
            dlc.Color = glm::vec3(r, g, b);
        } else if (trimmed.rfind("Intensity:", 0) == 0) {
            try { dlc.Intensity = std::stof(TrimLightingPersistenceText(trimmed.substr(10))); } catch (...) {}
        } else if (trimmed.rfind("AmbientIntensity:", 0) == 0) {
            try { dlc.AmbientIntensity = std::stof(TrimLightingPersistenceText(trimmed.substr(17))); } catch (...) {}
        } else if (trimmed.rfind("Azimuth:", 0) == 0) {
            try { dlc.Azimuth = std::stof(TrimLightingPersistenceText(trimmed.substr(8))); } catch (...) {}
        } else if (trimmed.rfind("Elevation:", 0) == 0) {
            try { dlc.Elevation = std::stof(TrimLightingPersistenceText(trimmed.substr(10))); } catch (...) {}
        } else if (trimmed.rfind("CastShadows:", 0) == 0) {
            dlc.CastShadows = (TrimLightingPersistenceText(trimmed.substr(12)) == "true");
        } else if (trimmed.rfind("ShadowOrthoSize:", 0) == 0) {
            try { dlc.ShadowOrthoSize = std::stof(TrimLightingPersistenceText(trimmed.substr(16))); } catch (...) {}
        } else if (trimmed.rfind("ShadowBias:", 0) == 0) {
            try { dlc.ShadowBias = std::stof(TrimLightingPersistenceText(trimmed.substr(11))); } catch (...) {}
        } else if (trimmed.rfind("ShadowSlopeBias:", 0) == 0) {
            try { dlc.ShadowSlopeBias = std::stof(TrimLightingPersistenceText(trimmed.substr(16))); } catch (...) {}
        } else if (trimmed.rfind("ShadowNormalBias:", 0) == 0) {
            try { dlc.ShadowNormalBias = std::stof(TrimLightingPersistenceText(trimmed.substr(17))); } catch (...) {}
        } else if (trimmed.rfind("ShadowFilterRadius:", 0) == 0) {
            try { dlc.ShadowFilterRadius = std::stof(TrimLightingPersistenceText(trimmed.substr(19))); } catch (...) {}
        } else if (trimmed.rfind("ShadowQuality:", 0) == 0) {
            try { dlc.ShadowQuality = std::stoi(TrimLightingPersistenceText(trimmed.substr(14))); } catch (...) {}
        } else if (trimmed.rfind("CascadeSplitLambda:", 0) == 0) {
            try { dlc.CascadeSplitLambda = std::stof(TrimLightingPersistenceText(trimmed.substr(19))); } catch (...) {}
        } else if (trimmed.rfind("MaxShadowDistance:", 0) == 0) {
            try { dlc.MaxShadowDistance = std::stof(TrimLightingPersistenceText(trimmed.substr(18))); } catch (...) {}
        } else if (trimmed.rfind("Exposure:", 0) == 0) {
            try { dlc.Exposure = std::stof(TrimLightingPersistenceText(trimmed.substr(9))); } catch (...) {}
        } else if (trimmed.rfind("ToneMappingMode:", 0) == 0) {
            try { dlc.ToneMappingMode = std::stoi(TrimLightingPersistenceText(trimmed.substr(16))); } catch (...) {}
        } else if (trimmed.rfind("Gamma:", 0) == 0) {
            try { dlc.Gamma = std::stof(TrimLightingPersistenceText(trimmed.substr(6))); } catch (...) {}
        }
    }

    inline void WriteSkyboxComponent(std::ostream& out, const SkyboxComponent& sbc) {
        out << "    SkyboxComponent:\n";
        out << "      Enabled: " << (sbc.Enabled ? "true" : "false") << "\n";
        out << "      EnvironmentLighting: " << (sbc.EnvironmentLighting ? "true" : "false") << "\n";
        out << "      SkyboxType: " << (int)sbc.Type << "\n";
        out << "      HDRPath: " << sbc.HDRPath << "\n";
        out << "      Intensity: " << sbc.Intensity << "\n";
        out << "      Exposure: " << sbc.Exposure << "\n";
        out << "      EnvironmentRotation: " << sbc.EnvironmentRotation << "\n";
        out << "      ZenithColor: [ " << sbc.ZenithColor.r << " , " << sbc.ZenithColor.g << " , " << sbc.ZenithColor.b << " ]\n";
        out << "      HorizonColor: [ " << sbc.HorizonColor.r << " , " << sbc.HorizonColor.g << " , " << sbc.HorizonColor.b << " ]\n";
        out << "      GroundColor: [ " << sbc.GroundColor.r << " , " << sbc.GroundColor.g << " , " << sbc.GroundColor.b << " ]\n";
        out << "      SunSize: " << sbc.SunSize << "\n";
        out << "      SunGlow: " << sbc.SunGlow << "\n";
        out << "      Turbidity: " << sbc.Turbidity << "\n";
        out << "      SyncWithSun: " << (sbc.SyncWithSun ? "true" : "false") << "\n";
        out << "      CustomAzimuth: " << sbc.CustomAzimuth << "\n";
        out << "      CustomElevation: " << sbc.CustomElevation << "\n";
        out << "      HemisphereAmbient: " << (sbc.HemisphereAmbient ? "true" : "false") << "\n";
    }

    inline void ReadSkyboxComponentLine(const std::string& trimmed, SkyboxComponent& sbc) {
        if (trimmed.rfind("Enabled:", 0) == 0) {
            sbc.Enabled = (TrimLightingPersistenceText(trimmed.substr(8)) == "true");
        } else if (trimmed.rfind("EnvironmentLighting:", 0) == 0) {
            sbc.EnvironmentLighting = (TrimLightingPersistenceText(trimmed.substr(20)) == "true");
        } else if (trimmed.rfind("SkyboxType:", 0) == 0) {
            try { sbc.Type = static_cast<SkyboxType>(std::stoi(TrimLightingPersistenceText(trimmed.substr(11)))); } catch (...) {}
        } else if (trimmed.rfind("HDRPath:", 0) == 0) {
            sbc.HDRPath = TrimLightingPersistenceText(trimmed.substr(8));
        } else if (trimmed.rfind("Intensity:", 0) == 0) {
            try { sbc.Intensity = std::stof(TrimLightingPersistenceText(trimmed.substr(10))); } catch (...) {}
        } else if (trimmed.rfind("Exposure:", 0) == 0) {
            try { sbc.Exposure = std::stof(TrimLightingPersistenceText(trimmed.substr(9))); } catch (...) {}
        } else if (trimmed.rfind("EnvironmentRotation:", 0) == 0) {
            try { sbc.EnvironmentRotation = std::stof(TrimLightingPersistenceText(trimmed.substr(20))); } catch (...) {}
        } else if (trimmed.rfind("ZenithColor:", 0) == 0) {
            float r = 0.18f, g = 0.42f, b = 0.78f;
            std::sscanf(trimmed.c_str(), "ZenithColor: [ %f , %f , %f ]", &r, &g, &b);
            sbc.ZenithColor = glm::vec3(r, g, b);
        } else if (trimmed.rfind("HorizonColor:", 0) == 0) {
            float r = 0.70f, g = 0.82f, b = 0.95f;
            std::sscanf(trimmed.c_str(), "HorizonColor: [ %f , %f , %f ]", &r, &g, &b);
            sbc.HorizonColor = glm::vec3(r, g, b);
        } else if (trimmed.rfind("GroundColor:", 0) == 0) {
            float r = 0.18f, g = 0.16f, b = 0.14f;
            std::sscanf(trimmed.c_str(), "GroundColor: [ %f , %f , %f ]", &r, &g, &b);
            sbc.GroundColor = glm::vec3(r, g, b);
        } else if (trimmed.rfind("SunSize:", 0) == 0) {
            try { sbc.SunSize = std::stof(TrimLightingPersistenceText(trimmed.substr(8))); } catch (...) {}
        } else if (trimmed.rfind("SunGlow:", 0) == 0) {
            try { sbc.SunGlow = std::stof(TrimLightingPersistenceText(trimmed.substr(8))); } catch (...) {}
        } else if (trimmed.rfind("Turbidity:", 0) == 0) {
            try { sbc.Turbidity = std::stof(TrimLightingPersistenceText(trimmed.substr(10))); } catch (...) {}
        } else if (trimmed.rfind("SyncWithSun:", 0) == 0) {
            sbc.SyncWithSun = (TrimLightingPersistenceText(trimmed.substr(12)) == "true");
        } else if (trimmed.rfind("CustomAzimuth:", 0) == 0) {
            try { sbc.CustomAzimuth = std::stof(TrimLightingPersistenceText(trimmed.substr(14))); } catch (...) {}
        } else if (trimmed.rfind("CustomElevation:", 0) == 0) {
            try { sbc.CustomElevation = std::stof(TrimLightingPersistenceText(trimmed.substr(16))); } catch (...) {}
        } else if (trimmed.rfind("HemisphereAmbient:", 0) == 0) {
            sbc.HemisphereAmbient = (TrimLightingPersistenceText(trimmed.substr(18)) == "true");
        }
    }

    inline void WriteReflectionProbeComponent(std::ostream& out, const ReflectionProbeComponent& rpc) {
        out << "    ReflectionProbeComponent:\n";
        out << "      Enabled: " << (rpc.Enabled ? "true" : "false") << "\n";
        out << "      Shape: " << (rpc.Shape == ProbeShape::Sphere ? "Sphere" : "Box") << "\n";
        out << "      BoxExtents: [ " << rpc.BoxExtents.x << " , " << rpc.BoxExtents.y << " , " << rpc.BoxExtents.z << " ]\n";
        out << "      Radius: " << rpc.Radius << "\n";
        out << "      BoxOffset: [ " << rpc.BoxOffset.x << " , " << rpc.BoxOffset.y << " , " << rpc.BoxOffset.z << " ]\n";
        out << "      Intensity: " << rpc.Intensity << "\n";
        out << "      CubemapPath: " << rpc.CubemapPath << "\n";
        out << "      ParallaxCorrected: " << (rpc.ParallaxCorrected ? "true" : "false") << "\n";
        out << "      DynamicCapture: " << (rpc.DynamicCapture ? "true" : "false") << "\n";
        out << "      CaptureResolution: " << rpc.CaptureResolution << "\n";
        out << "      CaptureShadows: " << (rpc.CaptureShadows ? "true" : "false") << "\n";
        out << "      CaptureNearPlane: " << rpc.CaptureNearPlane << "\n";
        out << "      CaptureFarPlane: " << rpc.CaptureFarPlane << "\n";
        out << "      CaptureMode: " << ReflectionProbeCaptureModeToString(rpc.CaptureMode) << "\n";
        out << "      BlendDistance: " << rpc.BlendDistance << "\n";
    }

    inline void ReadReflectionProbeComponentLine(const std::string& trimmed, ReflectionProbeComponent& rpc) {
        if (trimmed.rfind("Enabled:", 0) == 0) {
            rpc.Enabled = (TrimLightingPersistenceText(trimmed.substr(8)) == "true");
        } else if (trimmed.rfind("Shape:", 0) == 0) {
            std::string shape = TrimLightingPersistenceText(trimmed.substr(6));
            rpc.Shape = (shape == "Sphere" || shape == "1") ? ProbeShape::Sphere : ProbeShape::Box;
        } else if (trimmed.rfind("BoxExtents:", 0) == 0) {
            float x = 10.0f, y = 10.0f, z = 10.0f;
            std::sscanf(trimmed.c_str(), "BoxExtents: [ %f , %f , %f ]", &x, &y, &z);
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                rpc.BoxExtents = glm::max(glm::vec3(x, y, z), glm::vec3(0.01f));
            }
        } else if (trimmed.rfind("Radius:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(7))); if (std::isfinite(v)) rpc.Radius = std::max(0.01f, v); } catch (...) {}
        } else if (trimmed.rfind("BoxOffset:", 0) == 0) {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            std::sscanf(trimmed.c_str(), "BoxOffset: [ %f , %f , %f ]", &x, &y, &z);
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                rpc.BoxOffset = glm::vec3(x, y, z);
            }
        } else if (trimmed.rfind("Intensity:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(10))); if (std::isfinite(v)) rpc.Intensity = std::clamp(v, 0.0f, 10.0f); } catch (...) {}
        } else if (trimmed.rfind("CubemapPath:", 0) == 0) {
            rpc.CubemapPath = TrimLightingPersistenceText(trimmed.substr(12));
        } else if (trimmed.rfind("ParallaxCorrected:", 0) == 0) {
            rpc.ParallaxCorrected = (TrimLightingPersistenceText(trimmed.substr(18)) == "true");
        } else if (trimmed.rfind("DynamicCapture:", 0) == 0) {
            rpc.DynamicCapture = (TrimLightingPersistenceText(trimmed.substr(15)) == "true");
        } else if (trimmed.rfind("CaptureResolution:", 0) == 0) {
            try {
                unsigned long res = std::stoul(TrimLightingPersistenceText(trimmed.substr(18)));
                rpc.CaptureResolution = static_cast<uint32_t>(std::clamp<unsigned long>(res, 32ul, 512ul));
            } catch (...) {}
        } else if (trimmed.rfind("CaptureShadows:", 0) == 0) {
            rpc.CaptureShadows = (TrimLightingPersistenceText(trimmed.substr(15)) == "true");
        } else if (trimmed.rfind("CaptureNearPlane:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(17))); if (std::isfinite(v)) rpc.CaptureNearPlane = std::max(0.01f, v); } catch (...) {}
        } else if (trimmed.rfind("CaptureFarPlane:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(16))); if (std::isfinite(v)) rpc.CaptureFarPlane = std::max(0.1f, v); } catch (...) {}
        } else if (trimmed.rfind("CaptureMode:", 0) == 0) {
            rpc.CaptureMode = StringToReflectionProbeCaptureMode(TrimLightingPersistenceText(trimmed.substr(12)));
        } else if (trimmed.rfind("BlendDistance:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(14))); if (std::isfinite(v)) rpc.BlendDistance = std::max(0.0f, v); } catch (...) {}
        }
    }

    inline void WritePlanarReflectionComponent(std::ostream& out, const PlanarReflectionComponent& pr) {
        out << "    PlanarReflectionComponent:\n";
        out << "      Enabled: " << (pr.Enabled ? "true" : "false") << "\n";
        out << "      HalfExtents: [ " << pr.HalfExtents.x << " , " << pr.HalfExtents.y << " ]\n";
        out << "      ResolutionScale: " << pr.ResolutionScale << "\n";
        out << "      ClipPlaneOffset: " << pr.ClipPlaneOffset << "\n";
        out << "      EdgeFadeDistance: " << pr.EdgeFadeDistance << "\n";
        out << "      MaxRoughness: " << pr.MaxRoughness << "\n";
        out << "      Intensity: " << pr.Intensity << "\n";
        out << "      CaptureNearPlane: " << pr.CaptureNearPlane << "\n";
        out << "      CaptureFarPlane: " << pr.CaptureFarPlane << "\n";
        out << "      CaptureShadows: " << (pr.CaptureShadows ? "true" : "false") << "\n";
        out << "      CaptureSkybox: " << (pr.CaptureSkybox ? "true" : "false") << "\n";
        out << "      ClipToExtents: " << (pr.ClipToExtents ? "true" : "false") << "\n";
        out << "      CaptureMode: " << PlanarReflectionCaptureModeToString(pr.CaptureMode) << "\n";
    }

    inline void ReadPlanarReflectionComponentLine(const std::string& trimmed, PlanarReflectionComponent& pr) {
        if (trimmed.rfind("Enabled:", 0) == 0) {
            pr.Enabled = (TrimLightingPersistenceText(trimmed.substr(8)) == "true");
        } else if (trimmed.rfind("HalfExtents:", 0) == 0) {
            float hx = 5.0f, hy = 5.0f;
            std::sscanf(trimmed.c_str(), "HalfExtents: [ %f , %f ]", &hx, &hy);
            if (std::isfinite(hx) && std::isfinite(hy)) pr.HalfExtents = glm::vec2(std::max(0.01f, hx), std::max(0.01f, hy));
        } else if (trimmed.rfind("ResolutionScale:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(16))); if (std::isfinite(v)) pr.ResolutionScale = std::clamp(v, 0.25f, 1.0f); } catch (...) {}
        } else if (trimmed.rfind("ClipPlaneOffset:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(16))); if (std::isfinite(v)) pr.ClipPlaneOffset = v; } catch (...) {}
        } else if (trimmed.rfind("EdgeFadeDistance:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(17))); if (std::isfinite(v)) pr.EdgeFadeDistance = std::max(0.0f, v); } catch (...) {}
        } else if (trimmed.rfind("MaxRoughness:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(13))); if (std::isfinite(v)) pr.MaxRoughness = std::clamp(v, 0.02f, 1.0f); } catch (...) {}
        } else if (trimmed.rfind("Intensity:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(10))); if (std::isfinite(v)) pr.Intensity = std::clamp(v, 0.0f, 10.0f); } catch (...) {}
        } else if (trimmed.rfind("CaptureNearPlane:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(17))); if (std::isfinite(v)) pr.CaptureNearPlane = std::max(0.01f, v); } catch (...) {}
        } else if (trimmed.rfind("CaptureFarPlane:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(16))); if (std::isfinite(v)) pr.CaptureFarPlane = std::max(0.1f, v); } catch (...) {}
        } else if (trimmed.rfind("CaptureShadows:", 0) == 0) {
            pr.CaptureShadows = (TrimLightingPersistenceText(trimmed.substr(15)) == "true");
        } else if (trimmed.rfind("CaptureSkybox:", 0) == 0) {
            pr.CaptureSkybox = (TrimLightingPersistenceText(trimmed.substr(14)) == "true");
        } else if (trimmed.rfind("ClipToExtents:", 0) == 0) {
            pr.ClipToExtents = (TrimLightingPersistenceText(trimmed.substr(14)) == "true");
        } else if (trimmed.rfind("CaptureMode:", 0) == 0) {
            pr.CaptureMode = StringToPlanarReflectionCaptureMode(TrimLightingPersistenceText(trimmed.substr(12)));
        }
    }

    inline void WriteRectLightComponent(std::ostream& out, const RectLightComponent& rlc) {
        out << "    RectLightComponent:\n";
        out << "      Enabled: " << (rlc.Enabled ? "true" : "false") << "\n";
        out << "      Shape: " << (rlc.Shape == AreaLightShape::Disk ? "Disk" : "Rectangle") << "\n";
        out << "      ShadowPolicy: " << static_cast<int>(rlc.ShadowPolicy) << "\n";
        out << "      Color: [ " << rlc.Color.r << " , " << rlc.Color.g << " , " << rlc.Color.b << " ]\n";
        out << "      Intensity: " << rlc.Intensity << "\n";
        out << "      Width: " << rlc.Width << "\n";
        out << "      Height: " << rlc.Height << "\n";
        out << "      Range: " << rlc.Range << "\n";
        out << "      TwoSided: " << (rlc.TwoSided ? "true" : "false") << "\n";
    }

    inline void ReadRectLightComponentLine(const std::string& trimmed, RectLightComponent& rlc) {
        if (trimmed.rfind("Enabled:", 0) == 0) {
            rlc.Enabled = (TrimLightingPersistenceText(trimmed.substr(8)) == "true");
        } else if (trimmed.rfind("Shape:", 0) == 0) {
            std::string shapeStr = TrimLightingPersistenceText(trimmed.substr(6));
            rlc.Shape = (shapeStr == "Disk" || shapeStr == "1") ? AreaLightShape::Disk : AreaLightShape::Rectangle;
        } else if (trimmed.rfind("ShadowPolicy:", 0) == 0) {
            try {
                int policy = std::stoi(TrimLightingPersistenceText(trimmed.substr(13)));
                rlc.ShadowPolicy = (policy == 0) ? AreaLightShadowPolicy::UnshadowedAnalyticLTC : AreaLightShadowPolicy::UnshadowedAnalyticLTC;
            } catch (...) {}
        } else if (trimmed.rfind("Color:", 0) == 0) {
            float r = 1, g = 1, b = 1;
            std::sscanf(trimmed.c_str(), "Color: [ %f , %f , %f ]", &r, &g, &b);
            rlc.Color = glm::vec3(r, g, b);
        } else if (trimmed.rfind("Intensity:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(10))); if (std::isfinite(v)) rlc.Intensity = std::max(0.0f, v); } catch (...) {}
        } else if (trimmed.rfind("Width:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(6))); if (std::isfinite(v)) rlc.Width = std::max(0.01f, v); } catch (...) {}
        } else if (trimmed.rfind("Height:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(7))); if (std::isfinite(v)) rlc.Height = std::max(0.01f, v); } catch (...) {}
        } else if (trimmed.rfind("Range:", 0) == 0) {
            try { float v = std::stof(TrimLightingPersistenceText(trimmed.substr(6))); if (std::isfinite(v)) rlc.Range = std::max(0.0f, v); } catch (...) {}
        } else if (trimmed.rfind("TwoSided:", 0) == 0) {
            rlc.TwoSided = (TrimLightingPersistenceText(trimmed.substr(9)) == "true");
        }
    }

    inline void WriteVolumetricFogComponent(std::ostream& out, const VolumetricFogComponent& vfc) {
        out << "    VolumetricFogComponent:\n";
        out << "      Enabled: " << (vfc.Enabled ? "true" : "false") << "\n";
        out << "      Density: " << vfc.Density << "\n";
        out << "      HeightFalloff: " << vfc.HeightFalloff << "\n";
        out << "      ScatteringAsymmetry: " << vfc.ScatteringAsymmetry << "\n";
        out << "      Color: [ " << vfc.Color.r << " , " << vfc.Color.g << " , " << vfc.Color.b << " ]\n";
        out << "      MaxDistance: " << vfc.MaxDistance << "\n";
        out << "      Intensity: " << vfc.Intensity << "\n";
    }

    inline void ReadVolumetricFogComponentLine(const std::string& trimmed, VolumetricFogComponent& vfc) {
        if (trimmed.rfind("Enabled:", 0) == 0) {
            vfc.Enabled = (TrimLightingPersistenceText(trimmed.substr(8)) == "true");
        } else if (trimmed.rfind("Density:", 0) == 0) {
            try { vfc.Density = std::stof(TrimLightingPersistenceText(trimmed.substr(8))); } catch (...) {}
        } else if (trimmed.rfind("HeightFalloff:", 0) == 0) {
            try { vfc.HeightFalloff = std::stof(TrimLightingPersistenceText(trimmed.substr(14))); } catch (...) {}
        } else if (trimmed.rfind("ScatteringAsymmetry:", 0) == 0) {
            try { vfc.ScatteringAsymmetry = std::stof(TrimLightingPersistenceText(trimmed.substr(20))); } catch (...) {}
        } else if (trimmed.rfind("Color:", 0) == 0) {
            float r = 0.8f, g = 0.85f, b = 0.9f;
            std::sscanf(trimmed.c_str(), "Color: [ %f , %f , %f ]", &r, &g, &b);
            vfc.Color = glm::vec3(r, g, b);
        } else if (trimmed.rfind("MaxDistance:", 0) == 0) {
            try { vfc.MaxDistance = std::stof(TrimLightingPersistenceText(trimmed.substr(12))); } catch (...) {}
        } else if (trimmed.rfind("Intensity:", 0) == 0) {
            try { vfc.Intensity = std::stof(TrimLightingPersistenceText(trimmed.substr(10))); } catch (...) {}
        }
    }

    inline void WriteDDGIProbeGridComponent(std::ostream& out, const DDGIProbeGridComponent& ddgi) {
        out << "    DDGIProbeGridComponent:\n";
        out << "      Enabled: " << (ddgi.Enabled ? "true" : "false") << "\n";
        out << "      GridDimensions: [ " << ddgi.GridDimensions.x << " , " << ddgi.GridDimensions.y << " , " << ddgi.GridDimensions.z << " ]\n";
        out << "      GridSpacing: [ " << ddgi.GridSpacing.x << " , " << ddgi.GridSpacing.y << " , " << ddgi.GridSpacing.z << " ]\n";
        out << "      GridOrigin: [ " << ddgi.GridOrigin.x << " , " << ddgi.GridOrigin.y << " , " << ddgi.GridOrigin.z << " ]\n";
        out << "      EnergyMultiplier: " << ddgi.EnergyMultiplier << "\n";
        out << "      RaysPerProbe: " << ddgi.RaysPerProbe << "\n";
        out << "      UpdateFraction: " << ddgi.UpdateFraction << "\n";
        out << "      MaxProbesPerFrame: " << ddgi.MaxProbesPerFrame << "\n";
        out << "      Hysteresis: " << ddgi.Hysteresis << "\n";
        out << "      ViewBias: " << ddgi.ViewBias << "\n";
        out << "      NormalBias: " << ddgi.NormalBias << "\n";
    }

    inline void ReadDDGIProbeGridComponentLine(const std::string& trimmed, DDGIProbeGridComponent& ddgi) {
        if (trimmed.rfind("Enabled:", 0) == 0) {
            ddgi.Enabled = (TrimLightingPersistenceText(trimmed.substr(8)) == "true");
        } else if (trimmed.rfind("GridDimensions:", 0) == 0) {
            int x = 16, y = 8, z = 16;
            std::sscanf(trimmed.c_str(), "GridDimensions: [ %d , %d , %d ]", &x, &y, &z);
            ddgi.GridDimensions = glm::ivec3(x, y, z);
        } else if (trimmed.rfind("GridSpacing:", 0) == 0) {
            float x = 2.0f, y = 2.0f, z = 2.0f;
            std::sscanf(trimmed.c_str(), "GridSpacing: [ %f , %f , %f ]", &x, &y, &z);
            ddgi.GridSpacing = glm::vec3(x, y, z);
        } else if (trimmed.rfind("GridOrigin:", 0) == 0) {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            std::sscanf(trimmed.c_str(), "GridOrigin: [ %f , %f , %f ]", &x, &y, &z);
            ddgi.GridOrigin = glm::vec3(x, y, z);
        } else if (trimmed.rfind("EnergyMultiplier:", 0) == 0) {
            try { ddgi.EnergyMultiplier = std::stof(TrimLightingPersistenceText(trimmed.substr(17))); } catch (...) {}
        } else if (trimmed.rfind("RaysPerProbe:", 0) == 0) {
            try { ddgi.RaysPerProbe = std::stoi(TrimLightingPersistenceText(trimmed.substr(13))); } catch (...) {}
        } else if (trimmed.rfind("UpdateFraction:", 0) == 0) {
            try { ddgi.UpdateFraction = std::stof(TrimLightingPersistenceText(trimmed.substr(15))); } catch (...) {}
        } else if (trimmed.rfind("MaxProbesPerFrame:", 0) == 0) {
            try { ddgi.MaxProbesPerFrame = std::stoi(TrimLightingPersistenceText(trimmed.substr(18))); } catch (...) {}
        } else if (trimmed.rfind("Hysteresis:", 0) == 0) {
            try { ddgi.Hysteresis = std::stof(TrimLightingPersistenceText(trimmed.substr(11))); } catch (...) {}
        } else if (trimmed.rfind("ViewBias:", 0) == 0) {
            try { ddgi.ViewBias = std::stof(TrimLightingPersistenceText(trimmed.substr(9))); } catch (...) {}
        } else if (trimmed.rfind("NormalBias:", 0) == 0) {
            try { ddgi.NormalBias = std::stof(TrimLightingPersistenceText(trimmed.substr(11))); } catch (...) {}
        }
    }

}
