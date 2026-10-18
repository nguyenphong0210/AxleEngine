#include <AxleSpline/SplineSerializer.hpp>
#include <AxleSpline/SplineComponent.hpp>
#include <AxleSpline/TrackGenerator.hpp>
#include <Axle/Log.hpp>
#include <algorithm>
#include <cstdio>
#include <string>

namespace Axle::Spline {

    namespace {
        static std::string Trim(const std::string& str) {
            size_t first = str.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return "";
            size_t last = str.find_last_not_of(" \t\r\n");
            return str.substr(first, (last - first + 1));
        }
    }

    void SplineSerializer::Serialize(Entity entity, std::ostream& out) {
        if (!entity.HasComponent<SplineComponent>()) return;

        auto& sc = entity.GetComponent<SplineComponent>();
        out << "    SplineComponent:\n";
        std::string typeStr = "CatmullRom";
        if (sc.Curve.GetSplineType() == SplineType::CubicBezier) typeStr = "CubicBezier";
        else if (sc.Curve.GetSplineType() == SplineType::Linear) typeStr = "Linear";

        out << "      SplineType: " << typeStr << "\n";
        out << "      ClosedLoop: " << (sc.Curve.IsLoop() ? "true" : "false") << "\n";
        out << "      SubdivisionsPerSegment: " << sc.Curve.GetSubdivisionsPerSegment() << "\n";
        out << "      TrackWidth: " << sc.TrackWidth << "\n";
        out << "      StepDistance: " << sc.StepDistance << "\n";
        out << "      TextureRepeatV: " << sc.TextureRepeatV << "\n";
        out << "      GenerateCurbs: " << (sc.GenerateCurbs ? "true" : "false") << "\n";
        out << "      CurbWidth: " << sc.CurbWidth << "\n";
        out << "      CurbHeight: " << sc.CurbHeight << "\n";
        out << "      GenerateGuardrails: " << (sc.GenerateGuardrails ? "true" : "false") << "\n";
        out << "      GuardrailHeight: " << sc.GuardrailHeight << "\n";
        out << "      GuardrailThickness: " << sc.GuardrailThickness << "\n";
        out << "      GeneratePhysics: " << (sc.GeneratePhysics ? "true" : "false") << "\n";
        out << "      Friction: " << sc.Friction << "\n";
        out << "      Restitution: " << sc.Restitution << "\n";
        out << "      Nodes:\n";
        for (const auto& node : sc.Curve.GetNodes()) {
            out << "        - Position: [ " << node.Position.x << " , " << node.Position.y << " , " << node.Position.z << " ]\n";
            out << "          HandleIn: [ " << node.HandleIn.x << " , " << node.HandleIn.y << " , " << node.HandleIn.z << " ]\n";
            out << "          HandleOut: [ " << node.HandleOut.x << " , " << node.HandleOut.y << " , " << node.HandleOut.z << " ]\n";
            out << "          Roll: " << node.Roll << "\n";
            out << "          Scale: " << node.Scale << "\n";
        }
    }

    bool SplineSerializer::Deserialize(Entity entity, const std::string& sectionName, const std::vector<std::string>& lines) {
        if (sectionName != "SplineComponent") return false;

        if (!entity.HasComponent<SplineComponent>()) {
            entity.AddComponent<SplineComponent>();
        }
        auto& sc = entity.GetComponent<SplineComponent>();
        sc.Curve.ClearNodes();

        SplineNode currentNode;
        bool parsingNode = false;

        auto commitNode = [&]() {
            if (parsingNode) {
                sc.Curve.AddNode(currentNode);
                currentNode = SplineNode();
                parsingNode = false;
            }
        };

        for (const auto& rawLine : lines) {
            std::string trimmed = Trim(rawLine);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("- Position:", 0) == 0) {
                commitNode();
                parsingNode = true;
                float x = 0, y = 0, z = 0;
                sscanf(trimmed.c_str(), "- Position: [ %f , %f , %f ]", &x, &y, &z);
                currentNode.Position = glm::vec3(x, y, z);
            } else if (trimmed.rfind("Position:", 0) == 0) {
                float x = 0, y = 0, z = 0;
                sscanf(trimmed.c_str(), "Position: [ %f , %f , %f ]", &x, &y, &z);
                currentNode.Position = glm::vec3(x, y, z);
            } else if (trimmed.rfind("HandleIn:", 0) == 0) {
                float x = 0, y = 0, z = 0;
                sscanf(trimmed.c_str(), "HandleIn: [ %f , %f , %f ]", &x, &y, &z);
                currentNode.HandleIn = glm::vec3(x, y, z);
            } else if (trimmed.rfind("HandleOut:", 0) == 0) {
                float x = 0, y = 0, z = 0;
                sscanf(trimmed.c_str(), "HandleOut: [ %f , %f , %f ]", &x, &y, &z);
                currentNode.HandleOut = glm::vec3(x, y, z);
            } else if (trimmed.rfind("Roll:", 0) == 0) {
                try { currentNode.Roll = std::stof(Trim(trimmed.substr(5))); } catch (...) {}
            } else if (trimmed.rfind("Scale:", 0) == 0) {
                try { currentNode.Scale = std::stof(Trim(trimmed.substr(6))); } catch (...) {}
            } else if (trimmed.rfind("SplineType:", 0) == 0) {
                std::string t = Trim(trimmed.substr(11));
                if (t == "CubicBezier") sc.Curve.SetSplineType(SplineType::CubicBezier);
                else if (t == "Linear") sc.Curve.SetSplineType(SplineType::Linear);
                else sc.Curve.SetSplineType(SplineType::CatmullRom);
            } else if (trimmed.rfind("ClosedLoop:", 0) == 0) {
                sc.Curve.SetLoop(Trim(trimmed.substr(11)) == "true");
            } else if (trimmed.rfind("SubdivisionsPerSegment:", 0) == 0) {
                try { sc.Curve.SetSubdivisionsPerSegment(std::stoul(Trim(trimmed.substr(23)))); } catch (...) {}
            } else if (trimmed.rfind("TrackWidth:", 0) == 0) {
                try { sc.TrackWidth = std::stof(Trim(trimmed.substr(11))); } catch (...) {}
            } else if (trimmed.rfind("StepDistance:", 0) == 0) {
                try { sc.StepDistance = std::stof(Trim(trimmed.substr(13))); } catch (...) {}
            } else if (trimmed.rfind("TextureRepeatV:", 0) == 0) {
                try { sc.TextureRepeatV = std::stof(Trim(trimmed.substr(15))); } catch (...) {}
            } else if (trimmed.rfind("GenerateCurbs:", 0) == 0) {
                sc.GenerateCurbs = (Trim(trimmed.substr(14)) == "true");
            } else if (trimmed.rfind("CurbWidth:", 0) == 0) {
                try { sc.CurbWidth = std::stof(Trim(trimmed.substr(10))); } catch (...) {}
            } else if (trimmed.rfind("CurbHeight:", 0) == 0) {
                try { sc.CurbHeight = std::stof(Trim(trimmed.substr(11))); } catch (...) {}
            } else if (trimmed.rfind("GenerateGuardrails:", 0) == 0) {
                sc.GenerateGuardrails = (Trim(trimmed.substr(19)) == "true");
            } else if (trimmed.rfind("GuardrailHeight:", 0) == 0) {
                try { sc.GuardrailHeight = std::stof(Trim(trimmed.substr(16))); } catch (...) {}
            } else if (trimmed.rfind("GuardrailThickness:", 0) == 0) {
                try { sc.GuardrailThickness = std::stof(Trim(trimmed.substr(19))); } catch (...) {}
            } else if (trimmed.rfind("GeneratePhysics:", 0) == 0) {
                sc.GeneratePhysics = (Trim(trimmed.substr(16)) == "true");
            } else if (trimmed.rfind("Friction:", 0) == 0) {
                try { sc.Friction = std::stof(Trim(trimmed.substr(9))); } catch (...) {}
            } else if (trimmed.rfind("Restitution:", 0) == 0) {
                try { sc.Restitution = std::stof(Trim(trimmed.substr(12))); } catch (...) {}
            }
        }

        commitNode();
        sc.Curve.RebuildCache();
        sc.Dirty = true;
        return true;
    }

}
