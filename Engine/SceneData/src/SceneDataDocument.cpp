#include <Axle/SceneDataDocument.hpp>
#include <Axle/SceneData.hpp>
#include <Axle/DataComponents.hpp>
#include <Axle/Entity.hpp>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace Axle {
namespace {
    std::string Trim(const std::string& line) {
        const auto first = line.find_first_not_of(" \t\r\n");
        return first == std::string::npos ? "" : line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);
    }
    std::istringstream Numbers(std::string value) {
        for (auto& ch : value) if (ch == '[' || ch == ']' || ch == ',') ch = ' ';
        return std::istringstream(value);
    }
}
    bool SceneDataDocument::Deserialize(const std::string& text, SceneData& scene) {
        if (!scene.CanMutate() || text.find("Entities:") == std::string::npos) return false;
        scene.Clear();
        Entity current;
        std::string section, line;
        std::vector<std::string> passive;
        struct Link { Entity Child; uint64_t Parent = 0; };
        std::vector<Link> links;
        const auto isData = [&] { return section == "TagComponent" || section == "TransformComponent" || section == "HierarchyComponent"; };
        const auto flush = [&] {
            if (current && !section.empty() && !isData()) scene.StorePassiveSection(current, section, passive);
            passive.clear();
        };
        try {
            std::istringstream input(text);
            while (std::getline(input, line)) {
                const auto trimmed = Trim(line);
                if (trimmed.starts_with("- Entity:")) {
                    flush();
                    const auto id = std::stoull(trimmed.substr(9));
                    if (!id || scene.GetEntityByUUID(UUID(id))) throw std::invalid_argument("Invalid entity UUID");
                    current = scene.CreateEntityWithUUID(UUID(id));
                    section.clear();
                } else if (current && line.starts_with("    ") && !line.starts_with("     ") && trimmed.ends_with(':')) {
                    flush();
                    section = trimmed.substr(0, trimmed.size() - 1);
                } else if (current && isData()) {
                    const auto colon = trimmed.find(':');
                    if (colon == std::string::npos) continue;
                    const auto key = trimmed.substr(0, colon), value = Trim(trimmed.substr(colon + 1));
                    auto& transform = current.GetComponent<TransformComponent>();
                    auto& hierarchy = current.GetComponent<HierarchyComponent>();
                    if (section == "TagComponent" && key == "Tag") current.GetComponent<TagComponent>().Tag = value;
                    else if (section == "TransformComponent") {
                        auto numbers = Numbers(value);
                        if (key == "Translation") numbers >> transform.Translation.x >> transform.Translation.y >> transform.Translation.z;
                        else if (key == "Scale") numbers >> transform.Scale.x >> transform.Scale.y >> transform.Scale.z;
                        else if (key == "Rotation") numbers >> transform.Rotation.w >> transform.Rotation.x >> transform.Rotation.y >> transform.Rotation.z;
                        if (numbers.fail()) throw std::invalid_argument("Invalid transform");
                    } else if (section == "HierarchyComponent") {
                        if (key == "Parent") links.push_back({current, std::stoull(value)});
                        else if (key == "InheritTransform") hierarchy.InheritTransform = value == "true";
                        else if (key == "InheritScale") hierarchy.InheritScale = value == "true";
                    }
                } else if (current && !section.empty()) passive.push_back(line);
            }
            flush();
            for (auto& link : links) link.Child.SetParent(scene.GetEntityByUUID(UUID(link.Parent)), false);
            scene.UpdateWorldTransforms();
            return true;
        } catch (const std::exception&) {
            scene.Clear();
            return false;
        }
    }

    std::string SceneDataDocument::Serialize(const SceneData& scene) {
        std::ostringstream out;
        out << std::setprecision(9) << "# Axle Scene Definition File\nScene: Untitled\nEntities:\n";
        const auto& registry = scene.GetRegistry();
        for (auto entity : registry.view<IDComponent, TagComponent, TransformComponent, HierarchyComponent>()) {
            const auto& t = registry.get<TransformComponent>(entity);
            const auto& h = registry.get<HierarchyComponent>(entity);
            uint64_t parent = 0;
            if (registry.valid(h.Parent) && registry.all_of<IDComponent>(h.Parent)) parent = registry.get<IDComponent>(h.Parent).ID;
            out << "  - Entity: " << uint64_t(registry.get<IDComponent>(entity).ID)
                << "\n    TagComponent:\n      Tag: " << registry.get<TagComponent>(entity).Tag
                << "\n    TransformComponent:\n      Translation: [ " << t.Translation.x << " , " << t.Translation.y << " , " << t.Translation.z
                << " ]\n      Rotation: [ " << t.Rotation.w << " , " << t.Rotation.x << " , " << t.Rotation.y << " , " << t.Rotation.z
                << " ]\n      Scale: [ " << t.Scale.x << " , " << t.Scale.y << " , " << t.Scale.z
                << " ]\n    HierarchyComponent:\n      Parent: " << parent
                << "\n      InheritTransform: " << (h.InheritTransform ? "true" : "false")
                << "\n      InheritScale: " << (h.InheritScale ? "true" : "false") << '\n';
            if (registry.all_of<PassiveExtensionComponent>(entity)) {
                for (const auto& section : registry.get<PassiveExtensionComponent>(entity).Sections) {
                    out << "    " << section.SectionName << ":\n";
                    for (const auto& raw : section.Lines) out << raw << '\n';
                }
            }
        }
        return out.str();
    }
}
