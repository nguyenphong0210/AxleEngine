#include <Axle/SkeletalMeshImporter.hpp>
#include <ufbx.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>

namespace Axle {
namespace {
glm::mat4 Matrix(const ufbx_matrix& u) {
    glm::mat4 result(1);
    for (int c = 0; c < 4; ++c) result[c] = glm::vec4(u.cols[c].x, u.cols[c].y, u.cols[c].z, c == 3 ? 1 : 0);
    return result;
}
glm::vec3 Vector(const ufbx_vec3& value) { return {float(value.x),float(value.y),float(value.z)}; }
glm::quat Rotation(const ufbx_quat& value) { return {float(value.w),float(value.x),float(value.y),float(value.z)}; }
}
bool SkeletalMeshImporter::LoadFBX(const std::filesystem::path& path, SkeletalMeshData& outMesh,
                                 std::shared_ptr<Skeleton>& outRig, std::vector<std::shared_ptr<AnimationClip>>& outClips,
                                 std::string* diagnostic) {
    if (diagnostic) diagnostic->clear();
    auto fail = [&](const std::string& message) { if (diagnostic) *diagnostic = message; return false; };
    ufbx_load_opts options{}; options.target_axes = ufbx_axes_right_handed_y_up; options.target_unit_meters = 1;
    options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    options.generate_missing_normals = true;
    ufbx_error error{};
    std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> source(ufbx_load_file(path.string().c_str(), &options, &error), ufbx_free_scene);
    if (!source) return fail("FBX load failed: " + std::string(error.description.data, error.description.length));
    ufbx_mesh* mesh = nullptr; ufbx_skin_deformer* skin = nullptr;
    for (auto candidate : source->meshes) if (candidate->skin_deformers.count) {
        if (mesh || candidate->skin_deformers.count != 1) return fail("Multiple FBX skins/meshes require an explicit selection");
        mesh = candidate; skin = candidate->skin_deformers[0];
    }
    if (!mesh || !skin || skin->clusters.count == 0 || skin->clusters.count > 128) return fail("FBX requires one nonempty skin with at most 128 joints");
    if (skin->skinning_method != UFBX_SKINNING_METHOD_LINEAR && skin->skinning_method != UFBX_SKINNING_METHOD_RIGID)
        return fail("FBX dual-quaternion/blended skinning is not supported");
    // Bake the common mesh bind basis once; inverse binds compensate exactly.
    // Use bind_to_world, not the current animated node_to_world.
    const auto geometryBind = Matrix(skin->clusters[0]->bind_to_world) * Matrix(skin->clusters[0]->geometry_to_bone);
    const float determinant = glm::determinant(geometryBind);
    if (!std::isfinite(determinant) || std::abs(determinant)<1e-12f) return fail("Singular FBX mesh bind basis");
    const auto inverseGeometryBind = glm::inverse(geometryBind);
    const auto normalBasis = glm::transpose(glm::mat3(inverseGeometryBind));
    for (auto cluster : skin->clusters) {
        const auto basis = Matrix(cluster->bind_to_world)*Matrix(cluster->geometry_to_bone);
        for (int c=0;c<4;++c) for (int r=0;r<4;++r)
            if (std::abs(basis[c][r]-geometryBind[c][r])>1e-4f*std::max(1.0f,std::abs(geometryBind[c][r]))) return fail("Inconsistent FBX cluster bind bases");
    }
    if (mesh->vertices.count>10000000 || mesh->num_triangles>10000000) return fail("FBX geometry budget exceeded");
    auto rig = std::make_shared<Skeleton>(path.stem().string());
    std::unordered_map<const ufbx_node*, int> indices;
    std::vector<const ufbx_node*> nodes;
    // Original cluster indices stay unchanged; helper ancestors follow them.
    for (auto cluster : skin->clusters) {
        if (!cluster->bone_node || indices.contains(cluster->bone_node)) return fail("Invalid or duplicate FBX cluster node");
        indices[cluster->bone_node] = int(nodes.size()); nodes.push_back(cluster->bone_node);
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto parent = nodes[i]->parent;
        if (parent && !indices.contains(parent)) {
            if (nodes.size() >= 128) return fail("FBX hierarchy exceeds 128 joints including helper nodes");
            indices[parent] = int(nodes.size()); nodes.push_back(parent);
        }
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto node = nodes[i]; Bone bone;
        bone.Name = std::string(node->name.data,node->name.length);
        if (bone.Name.empty()) bone.Name = "FBX_Node_" + std::to_string(node->element_id);
        bone.ParentID = node->parent ? indices.at(node->parent) : -1;
        bone.LocalTransform = Matrix(node->node_to_parent);
        bone.InverseBindMatrix = i < skin->clusters.count ? Matrix(skin->clusters[i]->geometry_to_bone) * inverseGeometryBind : glm::inverse(Matrix(node->node_to_world));
        bone.HasExplicitBindTRS = true; bone.BindTranslation = Vector(node->local_transform.translation);
        bone.BindRotation = Rotation(node->local_transform.rotation); bone.BindScale = Vector(node->local_transform.scale);
        const auto composed = glm::translate(glm::mat4(1),bone.BindTranslation) * glm::mat4_cast(bone.BindRotation) * glm::scale(glm::mat4(1),bone.BindScale);
        for (int c=0;c<4;++c) for (int r=0;r<4;++r) if (std::abs(composed[c][r]-bone.LocalTransform[c][r]) > 0.001f)
            return fail("FBX local transform cannot be represented by the runtime TRS contract");
        rig->AddBone(bone);
    }
    std::vector<std::string> errors;
    if (!rig->RebuildDerivedData(&errors)) return fail(errors.empty() ? "Invalid FBX hierarchy" : errors.front());
    SkeletalMeshData data; data.Name = std::string(mesh->name.data,mesh->name.length); data.SkeletonRef = rig;
    if (data.Name.empty()) data.Name = path.stem().string();
    std::vector<SkeletalVertex> vertices(mesh->vertices.count);
    for (size_t i=0;i<vertices.size();++i) vertices[i].Position = glm::vec3(geometryBind*glm::vec4(Vector(mesh->vertices[i]),1));
    // All influences participate before the strongest four are chosen.
    std::vector<std::vector<std::pair<int,float>>> influences(vertices.size());
    for (size_t c=0;c<skin->clusters.count;++c) {
        auto cluster = skin->clusters[c];
        if (cluster->vertices.count != cluster->weights.count) return fail("Invalid FBX skin weight arrays");
        for (size_t w=0;w<cluster->vertices.count;++w) {
            const auto v = cluster->vertices[w]; const float weight = float(cluster->weights[w]);
            if (v>=vertices.size() || !std::isfinite(weight) || weight<0) return fail("Invalid FBX skin influence");
            if (weight>0) influences[v].push_back({int(c),weight});
        }
    }
    for (size_t i=0;i<vertices.size();++i) {
        auto& weights = influences[i]; std::stable_sort(weights.begin(),weights.end(),[](const auto& a,const auto& b) { return a.second>b.second; });
        for (size_t w=0;w<std::min<size_t>(4,weights.size());++w) { vertices[i].BoneIDs[w] = weights[w].first; vertices[i].Weights[w] = weights[w].second; }
        vertices[i].NormalizeWeights(0);
    }
    const size_t materialCount = std::max<size_t>(1,mesh->materials.count);
    data.MaterialSlots.resize(materialCount);
    for (size_t m=0;m<mesh->materials.count;++m) data.MaterialSlots[m] = std::string(mesh->materials[m]->name.data,mesh->materials[m]->name.length);
    if (mesh->num_triangles > UINT32_MAX/3) return fail("FBX index limit exceeded");
    std::vector<uint32_t> triangles(std::max<size_t>(3,mesh->max_face_triangles*3));
    for (size_t material=0;material<materialCount;++material) {
        const auto first = uint32_t(data.Indices.size());
        for (size_t f=0;f<mesh->faces.count;++f) {
            const auto faceMaterial = mesh->face_material.count ? mesh->face_material[f] : 0;
            if (faceMaterial>=materialCount) return fail("Invalid FBX material index");
            if (faceMaterial!=material || mesh->faces[f].num_indices<3) continue;
            const auto count = ufbx_triangulate_face(triangles.data(),triangles.size(),mesh,mesh->faces[f]);
            if (!count) return fail("Failed FBX polygon triangulation");
            for (size_t t=0;t<size_t(count)*3;++t) {
                const auto offset = t%3;
                const auto corner = triangles[determinant<0 && offset ? t-offset+(3-offset) : t]; const auto control = mesh->vertex_indices[corner];
                if (control>=vertices.size() || data.Vertices.size()>=UINT32_MAX) return fail("Invalid FBX corner or vertex limit");
                auto vertex = vertices[control];
                if (mesh->vertex_normal.exists) vertex.Normal = glm::normalize(normalBasis*Vector(ufbx_get_vertex_vec3(&mesh->vertex_normal,corner)));
                if (mesh->vertex_uv.exists) { const auto uv = ufbx_get_vertex_vec2(&mesh->vertex_uv,corner); vertex.TexCoords = {float(uv.x),float(uv.y)}; }
                data.Indices.push_back(uint32_t(data.Vertices.size())); data.Vertices.push_back(vertex);
            }
        }
        const auto count = uint32_t(data.Indices.size())-first;
        if (count) data.Submeshes.push_back({first,count,uint32_t(material)});
    }
    if (!data.Validate(diagnostic)) return false;
    std::vector<std::shared_ptr<AnimationClip>> clips;
    // FBX curves are explicitly resampled into the runtime's linear TRS contract.
    // Bound both source duration and total sampled keys before allocating.
    size_t remainingKeys = 3000000;
    for (auto stack : source->anim_stacks) {
        const double duration = stack->time_end-stack->time_begin;
        if (!std::isfinite(duration) || duration<=0 || duration>3600) return fail("Invalid or excessive FBX animation duration");
        const size_t frames = size_t(std::ceil(duration*30));
        const size_t keys = (frames+1)*nodes.size()*3;
        if (keys>remainingKeys) return fail("FBX sampled key budget exceeded"); remainingKeys-=keys;
        auto clip = std::make_shared<AnimationClip>(std::string(stack->name.data,stack->name.length),float(duration),30.0f);
        std::vector<AnimationChannel> channels;
        for (size_t i=0;i<nodes.size();++i) {
            AnimationChannel channel; channel.BoneName = rig->GetBones()[i].Name; channel.BoneIndex = int(i);
            for (size_t f=0;f<=frames;++f) {
                const float time = f==frames ? float(duration) : float(double(f)*duration/frames);
                auto transform = ufbx_evaluate_transform(stack->anim,nodes[i],stack->time_begin+time);
                channel.Positions.push_back({time,Vector(transform.translation)});
                channel.Rotations.push_back({time,Rotation(transform.rotation)});
                channel.Scales.push_back({time,Vector(transform.scale)});
            }
            channels.push_back(std::move(channel));
        }
        if (!clip->ReplaceChannels(std::move(channels),rig.get(),&errors)) return fail(errors.empty() ? "Invalid FBX animation keys" : errors.front());
        clips.push_back(std::move(clip));
    }
    outMesh = std::move(data); outRig = std::move(rig); outClips = std::move(clips); return true;
}
}
