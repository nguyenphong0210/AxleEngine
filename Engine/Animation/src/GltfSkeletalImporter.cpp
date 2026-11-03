#include <Axle/SkeletalMeshImporter.hpp>
#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace Axle {
bool SkeletalMeshImporter::LoadGLTF(const std::filesystem::path& path, SkeletalMeshData& outMesh,
                                   std::shared_ptr<Skeleton>& outSkeleton,
                                   std::vector<std::shared_ptr<AnimationClip>>& outClips,
                                   std::string* diagnostic) {
    auto fail = [&](const char* message) { if (diagnostic) *diagnostic = message; return false; };
    if (diagnostic) diagnostic->clear();
    cgltf_options options{}; cgltf_data* raw = nullptr;
    if (cgltf_parse_file(&options, path.string().c_str(), &raw) != cgltf_result_success) return fail("Cannot parse glTF");
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> owner(raw, cgltf_free);
    if (cgltf_load_buffers(&options, raw, path.string().c_str()) != cgltf_result_success || cgltf_validate(raw) != cgltf_result_success)
        return fail("Invalid glTF accessors, hierarchy or buffers");
    if (raw->skins_count != 1) return fail("Import requires exactly one skin; select/split multi-skin sources explicitly");
    const auto* skin = &raw->skins[0];
    if (skin->joints_count == 0 || skin->joints_count > 128 || raw->materials_count > 4095) return fail("Rig/material count exceeds import limits");
    std::vector<const cgltf_mesh*> meshes;
    for (size_t i = 0; i < raw->nodes_count; ++i) {
        const auto& node = raw->nodes[i];
        if (node.mesh && node.skin == skin && std::find(meshes.begin(), meshes.end(), node.mesh) == meshes.end()) meshes.push_back(node.mesh);
    }
    if (meshes.empty()) return fail("No mesh node is bound to the selected skin");
    auto rig = std::make_shared<Skeleton>(path.stem().string());
    SkeletalMeshData geometry; geometry.Name = path.stem().string(); geometry.SkeletonRef = rig;
    std::vector<std::shared_ptr<AnimationClip>> clips;
    std::unordered_map<const cgltf_node*, int> nodeToBone;
    std::vector<const cgltf_node*> bones;
    std::unordered_set<std::string> names;
    const auto addNode = [&](const cgltf_node* node) {
        if (nodeToBone.contains(node)) return;
        Bone bone;
        const auto index = size_t(node - raw->nodes);
        bone.Name = node->name && *node->name ? node->name : "Node_" + std::to_string(index);
        if (names.contains(bone.Name)) bone.Name += "#" + std::to_string(index);
        names.insert(bone.Name);
        cgltf_float matrix[16]; cgltf_node_transform_local(node, matrix); bone.LocalTransform = glm::make_mat4(matrix);
        if (!node->has_matrix) {
            bone.HasExplicitBindTRS = true;
            bone.BindTranslation = glm::make_vec3(node->translation); bone.BindScale = glm::make_vec3(node->scale);
            bone.BindRotation = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
        }
        nodeToBone[node] = rig->AddBone(bone); bones.push_back(node);
    };
    // Preserve original joint indices; append helper ancestors, including those above root joints.
    for (size_t i = 0; i < skin->joints_count; ++i) {
        if (!skin->joints[i] || nodeToBone.contains(skin->joints[i])) return fail("Duplicate or null skin joint");
        addNode(skin->joints[i]);
    }
    for (size_t i = 0; i < skin->joints_count; ++i) {
        std::unordered_set<const cgltf_node*> visited;
        for (auto* node = skin->joints[i]->parent; node; node = node->parent) {
            if (!visited.insert(node).second) return fail("Cyclic joint ancestors");
            addNode(node);
            if (bones.size() > 128) return fail("Rig including helper nodes exceeds 128 joints");
        }
    }
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i]->parent) rig->GetBones()[i].ParentID = nodeToBone.at(bones[i]->parent);
        if (i < skin->joints_count && skin->inverse_bind_matrices) {
            cgltf_float matrix[16];
            if (!cgltf_accessor_read_float(skin->inverse_bind_matrices, i, matrix, 16)) return fail("Invalid inverse bind accessor");
            rig->GetBones()[i].InverseBindMatrix = glm::make_mat4(matrix);
        }
    }
    if (!rig->RebuildDerivedData()) return fail("Unsupported or invalid rig transforms");
    std::vector<glm::mat4> globals; rig->ComputeBindPoseGlobalTransforms(globals);
    for (size_t i = skin->joints_count; i < bones.size(); ++i) rig->GetBones()[i].InverseBindMatrix = glm::inverse(globals[i]);
    for (size_t i = 0; i < raw->materials_count; ++i)
        geometry.MaterialSlots.push_back(raw->materials[i].name ? raw->materials[i].name : "Material_" + std::to_string(i));
    geometry.MaterialSlots.push_back("Default");
    for (const auto* mesh : meshes) for (size_t p = 0; p < mesh->primitives_count; ++p) {
        const auto& primitive = mesh->primitives[p];
        if (primitive.type != cgltf_primitive_type_triangles || primitive.targets_count) return fail("Only triangle skin primitives without morph targets are supported");
        const cgltf_accessor *position = nullptr, *normal = nullptr, *uv = nullptr;
        const cgltf_accessor* joints[2]{}; const cgltf_accessor* weights[2]{};
        for (size_t a = 0; a < primitive.attributes_count; ++a) {
            const auto& attribute = primitive.attributes[a];
            if (attribute.type == cgltf_attribute_type_position) position = attribute.data;
            if (attribute.type == cgltf_attribute_type_normal) normal = attribute.data;
            if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) uv = attribute.data;
            if (attribute.type == cgltf_attribute_type_joints || attribute.type == cgltf_attribute_type_weights) {
                if (attribute.index > 1) return fail("At most eight source influences are supported");
                (attribute.type == cgltf_attribute_type_joints ? joints : weights)[attribute.index] = attribute.data;
            }
        }
        if (!position || position->count > 10000000 || !joints[0] || !weights[0]) return fail("Skinned primitives require positions, joints and weights");
        for (int set = 0; set < 2; ++set) if (bool(joints[set]) != bool(weights[set])) return fail("Joint/weight attribute sets do not match");
        std::vector<SkeletalVertex> vertices(position->count);
        for (size_t v = 0; v < vertices.size(); ++v) {
            auto& vertex = vertices[v];
            if (!cgltf_accessor_read_float(position, v, &vertex.Position.x, 3)) return fail("Unreadable vertex position");
            if (normal && !cgltf_accessor_read_float(normal, v, &vertex.Normal.x, 3)) return fail("Unreadable vertex normal");
            if (uv && !cgltf_accessor_read_float(uv, v, &vertex.TexCoords.x, 2)) return fail("Unreadable vertex UV");
            std::unordered_map<int, float> influences;
            for (int set = 0; set < 2; ++set) if (joints[set]) {
                cgltf_uint ids[4]{}; cgltf_float values[4]{};
                if (!cgltf_accessor_read_uint(joints[set], v, ids, 4) || !cgltf_accessor_read_float(weights[set], v, values, 4)) return fail("Unreadable skin influences");
                for (int i = 0; i < 4; ++i) {
                    if (ids[i] >= skin->joints_count || !std::isfinite(values[i]) || values[i] < 0) return fail("Invalid source joint or weight");
                    influences[int(ids[i])] += values[i];
                }
            }
            std::vector<std::pair<int,float>> sorted(influences.begin(), influences.end());
            std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
            for (size_t i = 0; i < std::min(size_t(4), sorted.size()); ++i) vertex.AddBoneData(sorted[i].first, sorted[i].second);
            vertex.NormalizeWeights(rig->GetRootBoneIndex());
        }
        const size_t count = primitive.indices ? primitive.indices->count : vertices.size();
        if (count == 0 || count > 30000000 || count % 3) return fail("Invalid triangle index count");
        std::vector<uint32_t> indices(count);
        for (size_t i = 0; i < count; ++i) {
            const size_t index = primitive.indices ? cgltf_accessor_read_index(primitive.indices, i) : i;
            if (index >= vertices.size()) return fail("Source index exceeds vertex count"); indices[i] = uint32_t(index);
        }
        const auto first = uint32_t(geometry.Indices.size()), offset = uint32_t(geometry.Vertices.size());
        if (normal) {
            geometry.Vertices.insert(geometry.Vertices.end(), vertices.begin(), vertices.end());
            for (auto index : indices) geometry.Indices.push_back(offset + index);
        } else {
            for (size_t i = 0; i < indices.size(); i += 3) {
                auto n = glm::cross(vertices[indices[i+1]].Position - vertices[indices[i]].Position, vertices[indices[i+2]].Position - vertices[indices[i]].Position);
                n = glm::length(n) > 1e-6f ? glm::normalize(n) : glm::vec3(0, 1, 0);
                for (int j = 0; j < 3; ++j) {
                    auto vertex = vertices[indices[i+j]]; vertex.Normal = n;
                    geometry.Indices.push_back(uint32_t(geometry.Vertices.size())); geometry.Vertices.push_back(vertex);
                }
            }
        }
        geometry.Submeshes.push_back({first, uint32_t(count), primitive.material ? uint32_t(primitive.material - raw->materials) : uint32_t(raw->materials_count)});
        if (geometry.Vertices.size() > 10000000 || geometry.Indices.size() > 30000000) return fail("Combined geometry exceeds import limits");
    }
    for (size_t a = 0; a < raw->animations_count; ++a) {
        const auto& animation = raw->animations[a];
        auto clip = std::make_shared<AnimationClip>(animation.name ? animation.name : "Clip_" + std::to_string(a), 0.0f);
        std::unordered_map<int, AnimationChannel> channels;
        std::set<std::pair<int,int>> targets;
        float duration = 0;
        for (size_t c = 0; c < animation.channels_count; ++c) {
            const auto& source = animation.channels[c];
            const auto it = nodeToBone.find(source.target_node); if (it == nodeToBone.end()) continue;
            if (source.target_node->has_matrix) return fail("Animated matrix nodes require explicit TRS baking");
            if (!targets.insert({it->second, int(source.target_path)}).second) return fail("Duplicate animated node property");
            const auto* sampler = source.sampler;
            if (!sampler || !sampler->input || !sampler->output || sampler->input->count > 1000000) return fail("Invalid animation sampler");
            AnimationInterpolation mode;
            switch (sampler->interpolation) {
                case cgltf_interpolation_type_step: mode = AnimationInterpolation::Step; break;
                case cgltf_interpolation_type_linear: mode = AnimationInterpolation::Linear; break;
                case cgltf_interpolation_type_cubic_spline: mode = AnimationInterpolation::CubicSpline; break;
                default: return fail("Unsupported animation interpolation");
            }
            const bool cubic = mode == AnimationInterpolation::CubicSpline;
            if (sampler->output->count != sampler->input->count * (cubic ? 3 : 1)) return fail("Animation sampler count mismatch");
            auto& channel = channels[it->second]; channel.BoneIndex = it->second; channel.BoneName = rig->GetBones()[it->second].Name;
            const bool rotation = source.target_path == cgltf_animation_path_type_rotation;
            if (source.target_path == cgltf_animation_path_type_translation) channel.PositionsInterpolation = mode;
            else if (rotation) channel.RotationsInterpolation = mode;
            else if (source.target_path == cgltf_animation_path_type_scale) channel.ScalesInterpolation = mode;
            else return fail("Unsupported animated property");
            for (size_t k = 0; k < sampler->input->count; ++k) {
                float time; glm::vec4 value(0), in(0), out(0);
                if (!cgltf_accessor_read_float(sampler->input, k, &time, 1) || !cgltf_accessor_read_float(sampler->output, k*(cubic?3:1)+(cubic?1:0), &value.x, rotation?4:3)) return fail("Unreadable animation key");
                if (cubic && (!cgltf_accessor_read_float(sampler->output, k*3, &in.x, rotation?4:3) || !cgltf_accessor_read_float(sampler->output, k*3+2, &out.x, rotation?4:3))) return fail("Unreadable cubic tangents");
                duration = std::max(duration, time);
                if (rotation) channel.Rotations.push_back({time, glm::quat(value.w, value.x, value.y, value.z), in, out});
                else if (source.target_path == cgltf_animation_path_type_translation) channel.Positions.push_back({time, glm::vec3(value), in, out});
                else channel.Scales.push_back({time, glm::vec3(value), in, out});
            }
        }
        clip->SetDuration(duration);
        std::vector<int> order; for (const auto& [id, channel] : channels) order.push_back(id);
        std::sort(order.begin(), order.end()); for (auto id : order) clip->AddChannel(channels[id]);
        if (!clip->RebuildDerivedData(rig.get())) return fail("Invalid animation channel data");
        clips.push_back(std::move(clip));
    }
    if (!geometry.Validate(diagnostic)) return false;
    outMesh = std::move(geometry); outSkeleton = std::move(rig); outClips = std::move(clips); return true;
}
}
