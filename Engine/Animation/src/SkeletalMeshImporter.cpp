#include <Axle/SkeletalMeshImporter.hpp>
#include <Axle/Log.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <ufbx.h>
#include <cgltf.h>
#include <iostream>
#include <cmath>
#include <unordered_map>

namespace Axle {

    static glm::mat4 UfbxToGlmMat4(const ufbx_matrix& u) {
        glm::mat4 m(1.0f);
        m[0] = glm::vec4(static_cast<float>(u.cols[0].x), static_cast<float>(u.cols[0].y), static_cast<float>(u.cols[0].z), 0.0f);
        m[1] = glm::vec4(static_cast<float>(u.cols[1].x), static_cast<float>(u.cols[1].y), static_cast<float>(u.cols[1].z), 0.0f);
        m[2] = glm::vec4(static_cast<float>(u.cols[2].x), static_cast<float>(u.cols[2].y), static_cast<float>(u.cols[2].z), 0.0f);
        m[3] = glm::vec4(static_cast<float>(u.cols[3].x), static_cast<float>(u.cols[3].y), static_cast<float>(u.cols[3].z), 1.0f);
        return m;
    }

    bool SkeletalMeshImporter::Load(const std::filesystem::path& path,
                                    SkeletalMeshData& outMesh,
                                    std::shared_ptr<Skeleton>& outSkeleton,
                                    std::vector<std::shared_ptr<AnimationClip>>& outClips) {
        std::string ext = path.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(c));

        if (ext == ".fbx") {
            return LoadFBX(path, outMesh, outSkeleton, outClips);
        } else if (ext == ".gltf" || ext == ".glb") {
            return LoadGLTF(path, outMesh, outSkeleton, outClips);
        }

        AXLE_CORE_ERROR("[SkeletalMeshImporter] Unsupported asset format: {}", path.string());
        return false;
    }

    bool SkeletalMeshImporter::LoadFBX(const std::filesystem::path& path,
                                       SkeletalMeshData& outMesh,
                                       std::shared_ptr<Skeleton>& outSkeleton,
                                       std::vector<std::shared_ptr<AnimationClip>>& outClips) {
        ufbx_load_opts opts{};
        opts.target_axes = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters = 1.0f;
        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(path.string().c_str(), &opts, &error);
        if (!scene) {
            AXLE_CORE_ERROR("[SkeletalMeshImporter] Failed to load FBX '{}': {}", path.string(), error.description.data);
            return false;
        }

        outSkeleton = std::make_shared<Skeleton>(path.stem().string());

        // Find primary mesh and skin deformer
        ufbx_mesh* targetMesh = nullptr;
        ufbx_skin_deformer* skin = nullptr;

        for (size_t m = 0; m < scene->meshes.count; ++m) {
            ufbx_mesh* msh = scene->meshes.data[m];
            if (msh->skin_deformers.count > 0) {
                targetMesh = msh;
                skin = msh->skin_deformers.data[0];
                break;
            }
        }

        if (!targetMesh && scene->meshes.count > 0) {
            targetMesh = scene->meshes.data[0];
        }

        if (!targetMesh) {
            AXLE_CORE_WARN("[SkeletalMeshImporter] No meshes found in FBX: {}", path.string());
            ufbx_free_scene(scene);
            return false;
        }

        // Map ufbx nodes to skeleton bone indices
        std::unordered_map<const ufbx_node*, int> nodeToBoneIndex;

        if (skin) {
            for (size_t c = 0; c < skin->clusters.count; ++c) {
                ufbx_skin_cluster* cluster = skin->clusters.data[c];
                ufbx_node* boneNode = cluster->bone_node;

                Bone bone;
                bone.Name = boneNode ? boneNode->name.data : ("Bone_" + std::to_string(c));
                bone.InverseBindMatrix = UfbxToGlmMat4(cluster->geometry_to_bone);
                if (boneNode) {
                    bone.LocalTransform = UfbxToGlmMat4(boneNode->node_to_parent);
                }

                int boneIdx = outSkeleton->AddBone(bone);
                if (boneNode) {
                    nodeToBoneIndex[boneNode] = boneIdx;
                }
            }

            // Resolve parent hierarchy
            for (size_t c = 0; c < skin->clusters.count; ++c) {
                ufbx_skin_cluster* cluster = skin->clusters.data[c];
                ufbx_node* boneNode = cluster->bone_node;
                if (!boneNode) continue;

                int currentBoneIdx = nodeToBoneIndex[boneNode];
                if (boneNode->parent && nodeToBoneIndex.find(boneNode->parent) != nodeToBoneIndex.end()) {
                    int parentIdx = nodeToBoneIndex[boneNode->parent];
                    outSkeleton->GetBones()[currentBoneIdx].ParentID = parentIdx;
                    outSkeleton->GetBones()[parentIdx].Children.push_back(currentBoneIdx);
                }
            }
        }

        if (!outSkeleton->IsValid()) {
            Bone rootBone;
            rootBone.Name = "Root";
            rootBone.LocalTransform = glm::mat4(1.0f);
            rootBone.InverseBindMatrix = glm::mat4(1.0f);
            outSkeleton->AddBone(rootBone);
        }

        // Extract vertex data and faces
        outMesh.Name = targetMesh->name.data;
        outMesh.SkeletonRef = outSkeleton;

        size_t numVerts = targetMesh->vertices.count;
        outMesh.Vertices.resize(numVerts);

        for (size_t v = 0; v < numVerts; ++v) {
            outMesh.Vertices[v].Position = glm::vec3(
                static_cast<float>(targetMesh->vertices.data[v].x),
                static_cast<float>(targetMesh->vertices.data[v].y),
                static_cast<float>(targetMesh->vertices.data[v].z)
            );
        }

        // Extract bone weights per vertex if available
        if (skin) {
            for (size_t c = 0; c < skin->clusters.count; ++c) {
                ufbx_skin_cluster* cluster = skin->clusters.data[c];
                int boneID = static_cast<int>(c);
                for (size_t w = 0; w < cluster->vertices.count; ++w) {
                    uint32_t vIdx = cluster->vertices.data[w];
                    float weight = static_cast<float>(cluster->weights.data[w]);
                    if (vIdx < outMesh.Vertices.size()) {
                        outMesh.Vertices[vIdx].AddBoneData(boneID, weight);
                    }
                }
            }
        }

        // Extract triangles and normal/UV attributes from faces
        for (size_t f = 0; f < targetMesh->faces.count; ++f) {
            ufbx_face face = targetMesh->faces.data[f];
            if (face.num_indices < 3) continue;

            for (size_t fi = 1; fi + 1 < face.num_indices; ++fi) {
                uint32_t i0 = targetMesh->vertex_indices.data[face.index_begin];
                uint32_t i1 = targetMesh->vertex_indices.data[face.index_begin + fi];
                uint32_t i2 = targetMesh->vertex_indices.data[face.index_begin + fi + 1];

                outMesh.Indices.push_back(i0);
                outMesh.Indices.push_back(i1);
                outMesh.Indices.push_back(i2);
            }
        }

        // Extract animation clips
        for (size_t s = 0; s < scene->anim_stacks.count; ++s) {
            ufbx_anim_stack* stack = scene->anim_stacks.data[s];
            float duration = static_cast<float>(stack->time_end - stack->time_begin);
            if (duration <= 0.0f) duration = 1.0f;

            auto clip = std::make_shared<AnimationClip>(stack->name.data, duration, 30.0f);

            float fps = 30.0f;
            int totalFrames = static_cast<int>(std::ceil(duration * fps));
            if (totalFrames < 1) totalFrames = 1;

            for (const auto& [node, boneIdx] : nodeToBoneIndex) {
                AnimationChannel channel;
                channel.BoneName = outSkeleton->GetBones()[boneIdx].Name;
                channel.BoneIndex = boneIdx;

                for (int f = 0; f <= totalFrames; ++f) {
                    float t = static_cast<float>(f) / fps;
                    double evalTime = stack->time_begin + t;
                    ufbx_transform xf = ufbx_evaluate_transform(stack->anim, node, evalTime);

                    channel.Positions.push_back({ t, glm::vec3(xf.translation.x, xf.translation.y, xf.translation.z) });
                    channel.Rotations.push_back({ t, glm::quat(xf.rotation.w, xf.rotation.x, xf.rotation.y, xf.rotation.z) });
                    channel.Scales.push_back({ t, glm::vec3(xf.scale.x, xf.scale.y, xf.scale.z) });
                }
                clip->AddChannel(channel);
            }

            outClips.push_back(clip);
        }

        ufbx_free_scene(scene);
        AXLE_CORE_INFO("[SkeletalMeshImporter] Loaded FBX '{}': {} verts, {} indices, {} bones, {} clips",
                 path.filename().string(), outMesh.Vertices.size(), outMesh.Indices.size(), outSkeleton->GetBoneCount(), outClips.size());
        return true;
    }

    bool SkeletalMeshImporter::LoadGLTF(const std::filesystem::path& path,
                                        SkeletalMeshData& outMesh,
                                        std::shared_ptr<Skeleton>& outSkeleton,
                                        std::vector<std::shared_ptr<AnimationClip>>& outClips) {
        cgltf_options options{};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, path.string().c_str(), &data);
        if (result != cgltf_result_success) {
            AXLE_CORE_ERROR("[SkeletalMeshImporter] Failed to parse glTF: {}", path.string());
            return false;
        }

        result = cgltf_load_buffers(&options, data, path.string().c_str());
        if (result != cgltf_result_success) {
            AXLE_CORE_ERROR("[SkeletalMeshImporter] Failed to load glTF buffers: {}", path.string());
            cgltf_free(data);
            return false;
        }

        outSkeleton = std::make_shared<Skeleton>(path.stem().string());
        std::unordered_map<const cgltf_node*, int> nodeToBoneMap;

        if (data->skins_count > 0) {
            cgltf_skin* skin = &data->skins[0];
            std::vector<glm::mat4> invBind(skin->joints_count, glm::mat4(1.0f));

            if (skin->inverse_bind_matrices) {
                for (size_t j = 0; j < skin->joints_count; ++j) {
                    cgltf_float mat[16];
                    cgltf_accessor_read_float(skin->inverse_bind_matrices, j, mat, 16);
                    invBind[j] = glm::make_mat4(mat);
                }
            }

            for (size_t j = 0; j < skin->joints_count; ++j) {
                cgltf_node* joint = skin->joints[j];
                Bone bone;
                bone.Name = joint->name ? joint->name : ("Joint_" + std::to_string(j));
                bone.InverseBindMatrix = invBind[j];

                cgltf_float localMat[16];
                cgltf_node_transform_local(joint, localMat);
                bone.LocalTransform = glm::make_mat4(localMat);

                int idx = outSkeleton->AddBone(bone);
                nodeToBoneMap[joint] = idx;
            }

            for (size_t j = 0; j < skin->joints_count; ++j) {
                cgltf_node* joint = skin->joints[j];
                int currentIdx = nodeToBoneMap[joint];
                if (joint->parent && nodeToBoneMap.find(joint->parent) != nodeToBoneMap.end()) {
                    int parentIdx = nodeToBoneMap[joint->parent];
                    outSkeleton->GetBones()[currentIdx].ParentID = parentIdx;
                    outSkeleton->GetBones()[parentIdx].Children.push_back(currentIdx);
                }
            }
        }

        if (!outSkeleton->IsValid()) {
            Bone root;
            root.Name = "Root";
            outSkeleton->AddBone(root);
        }

        outMesh.Name = path.stem().string();
        outMesh.SkeletonRef = outSkeleton;

        // Iterate primitives
        for (size_t m = 0; m < data->meshes_count; ++m) {
            cgltf_mesh* mesh = &data->meshes[m];
            for (size_t p = 0; p < mesh->primitives_count; ++p) {
                cgltf_primitive* prim = &mesh->primitives[p];
                if (prim->type != cgltf_primitive_type_triangles) continue;

                cgltf_accessor* posAcc = nullptr;
                cgltf_accessor* normAcc = nullptr;
                cgltf_accessor* uvAcc = nullptr;
                cgltf_accessor* jointAcc = nullptr;
                cgltf_accessor* weightAcc = nullptr;

                for (size_t a = 0; a < prim->attributes_count; ++a) {
                    if (prim->attributes[a].type == cgltf_attribute_type_position) posAcc = prim->attributes[a].data;
                    if (prim->attributes[a].type == cgltf_attribute_type_normal) normAcc = prim->attributes[a].data;
                    if (prim->attributes[a].type == cgltf_attribute_type_texcoord) uvAcc = prim->attributes[a].data;
                    if (prim->attributes[a].type == cgltf_attribute_type_joints) jointAcc = prim->attributes[a].data;
                    if (prim->attributes[a].type == cgltf_attribute_type_weights) weightAcc = prim->attributes[a].data;
                }

                if (!posAcc) continue;

                u32 vertexOffset = static_cast<u32>(outMesh.Vertices.size());
                outMesh.Vertices.resize(vertexOffset + posAcc->count);

                for (size_t v = 0; v < posAcc->count; ++v) {
                    SkeletalVertex& vert = outMesh.Vertices[vertexOffset + v];
                    cgltf_accessor_read_float(posAcc, v, &vert.Position.x, 3);

                    if (normAcc) cgltf_accessor_read_float(normAcc, v, &vert.Normal.x, 3);
                    if (uvAcc) cgltf_accessor_read_float(uvAcc, v, &vert.TexCoords.x, 2);

                    if (jointAcc && weightAcc) {
                        cgltf_uint joints[4] = { 0, 0, 0, 0 };
                        cgltf_accessor_read_uint(jointAcc, v, joints, 4);
                        cgltf_float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        cgltf_accessor_read_float(weightAcc, v, weights, 4);

                        for (int k = 0; k < 4; ++k) {
                            vert.AddBoneData(static_cast<int>(joints[k]), weights[k]);
                        }
                    }
                }

                if (prim->indices) {
                    for (size_t i = 0; i < prim->indices->count; ++i) {
                        u32 idx = static_cast<u32>(cgltf_accessor_read_index(prim->indices, i));
                        outMesh.Indices.push_back(vertexOffset + idx);
                    }
                }
            }
        }

        // Extract glTF animations
        for (size_t a = 0; a < data->animations_count; ++a) {
            cgltf_animation* anim = &data->animations[a];
            float maxTime = 0.0f;

            for (size_t c = 0; c < anim->channels_count; ++c) {
                cgltf_animation_sampler* s = anim->channels[c].sampler;
                if (s && s->input && s->input->count > 0) {
                    cgltf_float t = 0.0f;
                    cgltf_accessor_read_float(s->input, s->input->count - 1, &t, 1);
                    if (t > maxTime) maxTime = t;
                }
            }

            auto clip = std::make_shared<AnimationClip>(anim->name ? anim->name : ("Clip_" + std::to_string(a)), maxTime, 30.0f);

            for (size_t c = 0; c < anim->channels_count; ++c) {
                cgltf_animation_channel* ch = &anim->channels[c];
                if (!ch->target_node) continue;

                std::string bName = ch->target_node->name ? ch->target_node->name : "";
                int bIdx = outSkeleton->FindBoneIndex(bName);

                AnimationChannel* channelPtr = const_cast<AnimationChannel*>(clip->FindChannel(bName));
                if (!channelPtr) {
                    AnimationChannel newChan;
                    newChan.BoneName = bName;
                    newChan.BoneIndex = bIdx;
                    clip->AddChannel(newChan);
                    channelPtr = const_cast<AnimationChannel*>(clip->FindChannel(bName));
                }

                cgltf_animation_sampler* s = ch->sampler;
                if (!s || !s->input || !s->output) continue;

                size_t keyCount = s->input->count;
                for (size_t k = 0; k < keyCount; ++k) {
                    cgltf_float timeVal = 0.0f;
                    cgltf_accessor_read_float(s->input, k, &timeVal, 1);

                    if (ch->target_path == cgltf_animation_path_type_translation) {
                        cgltf_float vec[3];
                        cgltf_accessor_read_float(s->output, k, vec, 3);
                        channelPtr->Positions.push_back({ timeVal, glm::vec3(vec[0], vec[1], vec[2]) });
                    } else if (ch->target_path == cgltf_animation_path_type_rotation) {
                        cgltf_float quat[4];
                        cgltf_accessor_read_float(s->output, k, quat, 4);
                        channelPtr->Rotations.push_back({ timeVal, glm::quat(quat[3], quat[0], quat[1], quat[2]) });
                    } else if (ch->target_path == cgltf_animation_path_type_scale) {
                        cgltf_float scl[3];
                        cgltf_accessor_read_float(s->output, k, scl, 3);
                        channelPtr->Scales.push_back({ timeVal, glm::vec3(scl[0], scl[1], scl[2]) });
                    }
                }
            }

            outClips.push_back(clip);
        }

        cgltf_free(data);
        AXLE_CORE_INFO("[SkeletalMeshImporter] Loaded glTF '{}': {} verts, {} indices, {} bones, {} clips",
                 path.filename().string(), outMesh.Vertices.size(), outMesh.Indices.size(), outSkeleton->GetBoneCount(), outClips.size());
        return true;
    }

    bool SkeletalMeshImporter::CreateProceduralBiped(SkeletalMeshData& outMesh,
                                                    std::shared_ptr<Skeleton>& outSkeleton,
                                                    std::vector<std::shared_ptr<AnimationClip>>& outClips) {
        outSkeleton = std::make_shared<Skeleton>("ProceduralBipedSkeleton");

        // 1. Define 8-bone Humanoid Hierarchy
        // 0: Root (Origin)
        // 1: Hips (Pelvis)
        // 2: Spine (Torso)
        // 3: Head
        // 4: LeftArm
        // 5: RightArm
        // 6: LeftLeg
        // 7: RightLeg
        Bone root;
        root.Name = "Root";
        root.LocalTransform = glm::mat4(1.0f);
        outSkeleton->AddBone(root);

        Bone hips;
        hips.Name = "Hips";
        hips.ParentID = 0;
        hips.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        outSkeleton->AddBone(hips);

        Bone spine;
        spine.Name = "Spine";
        spine.ParentID = 1;
        spine.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.4f, 0.0f));
        outSkeleton->AddBone(spine);

        Bone head;
        head.Name = "Head";
        head.ParentID = 2;
        head.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.45f, 0.0f));
        outSkeleton->AddBone(head);

        Bone leftArm;
        leftArm.Name = "LeftArm";
        leftArm.ParentID = 2;
        leftArm.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-0.35f, 0.15f, 0.0f));
        outSkeleton->AddBone(leftArm);

        Bone rightArm;
        rightArm.Name = "RightArm";
        rightArm.ParentID = 2;
        rightArm.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.35f, 0.15f, 0.0f));
        outSkeleton->AddBone(rightArm);

        Bone leftLeg;
        leftLeg.Name = "LeftLeg";
        leftLeg.ParentID = 1;
        leftLeg.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-0.18f, -0.45f, 0.0f));
        outSkeleton->AddBone(leftLeg);

        Bone rightLeg;
        rightLeg.Name = "RightLeg";
        rightLeg.ParentID = 1;
        rightLeg.LocalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.18f, -0.45f, 0.0f));
        outSkeleton->AddBone(rightLeg);

        // Precompute Inverse Bind Matrices for all bones
        outSkeleton->ComputeInverseBindMatrices();

        // 2. Build Character Geometry (Torso, Head, Limbs)
        outMesh.Name = "ProceduralBipedMesh";
        outMesh.SkeletonRef = outSkeleton;

        auto AddBox = [&](glm::vec3 center, glm::vec3 halfExtents, int boneID, glm::vec3 colorTint) {
            u32 baseIdx = static_cast<u32>(outMesh.Vertices.size());
            glm::vec3 h = halfExtents;

            // 8 Box Corners
            glm::vec3 p[8] = {
                center + glm::vec3(-h.x, -h.y,  h.z),
                center + glm::vec3( h.x, -h.y,  h.z),
                center + glm::vec3( h.x,  h.y,  h.z),
                center + glm::vec3(-h.x,  h.y,  h.z),
                center + glm::vec3(-h.x, -h.y, -h.z),
                center + glm::vec3( h.x, -h.y, -h.z),
                center + glm::vec3( h.x,  h.y, -h.z),
                center + glm::vec3(-h.x,  h.y, -h.z),
            };

            // 6 Faces (Front, Back, Left, Right, Top, Bottom)
            struct Face { int i0, i1, i2, i3; glm::vec3 normal; };
            Face faces[6] = {
                { 0, 1, 2, 3, glm::vec3( 0.0f,  0.0f,  1.0f) }, // Front
                { 5, 4, 7, 6, glm::vec3( 0.0f,  0.0f, -1.0f) }, // Back
                { 4, 0, 3, 7, glm::vec3(-1.0f,  0.0f,  0.0f) }, // Left
                { 1, 5, 6, 2, glm::vec3( 1.0f,  0.0f,  0.0f) }, // Right
                { 3, 2, 6, 7, glm::vec3( 0.0f,  1.0f,  0.0f) }, // Top
                { 4, 5, 1, 0, glm::vec3( 0.0f, -1.0f,  0.0f) }, // Bottom
            };

            for (int f = 0; f < 6; ++f) {
                u32 fStart = static_cast<u32>(outMesh.Vertices.size());
                int idxs[4] = { faces[f].i0, faces[f].i1, faces[f].i2, faces[f].i3 };
                glm::vec2 uvs[4] = { {0,0}, {1,0}, {1,1}, {0,1} };

                for (int v = 0; v < 4; ++v) {
                    SkeletalVertex vert;
                    vert.Position = p[idxs[v]];
                    vert.Normal = faces[f].normal;
                    vert.TexCoords = uvs[v];
                    vert.AddBoneData(boneID, 1.0f);
                    outMesh.Vertices.push_back(vert);
                }

                outMesh.Indices.push_back(fStart + 0);
                outMesh.Indices.push_back(fStart + 1);
                outMesh.Indices.push_back(fStart + 2);
                outMesh.Indices.push_back(fStart + 2);
                outMesh.Indices.push_back(fStart + 3);
                outMesh.Indices.push_back(fStart + 0);
            }
        };

        // Pelvis (Hips: Bone 1)
        AddBox(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.2f, 0.1f, 0.15f), 1, glm::vec3(0.2f, 0.4f, 0.8f));
        // Torso / Chest (Spine: Bone 2)
        AddBox(glm::vec3(0.0f, 1.35f, 0.0f), glm::vec3(0.22f, 0.22f, 0.16f), 2, glm::vec3(0.8f, 0.3f, 0.2f));
        // Head (Head: Bone 3)
        AddBox(glm::vec3(0.0f, 1.75f, 0.0f), glm::vec3(0.14f, 0.15f, 0.14f), 3, glm::vec3(0.9f, 0.8f, 0.6f));
        // Left Arm (Bone 4)
        AddBox(glm::vec3(-0.35f, 1.3f, 0.0f), glm::vec3(0.08f, 0.25f, 0.08f), 4, glm::vec3(0.3f, 0.7f, 0.3f));
        // Right Arm (Bone 5)
        AddBox(glm::vec3(0.35f, 1.3f, 0.0f), glm::vec3(0.08f, 0.25f, 0.08f), 5, glm::vec3(0.3f, 0.7f, 0.3f));
        // Left Leg (Bone 6)
        AddBox(glm::vec3(-0.16f, 0.5f, 0.0f), glm::vec3(0.09f, 0.45f, 0.09f), 6, glm::vec3(0.2f, 0.2f, 0.7f));
        // Right Leg (Bone 7)
        AddBox(glm::vec3(0.16f, 0.5f, 0.0f), glm::vec3(0.09f, 0.45f, 0.09f), 7, glm::vec3(0.2f, 0.2f, 0.7f));

        // 3. Create Procedural Animation Clips: "Idle" and "Walk"
        // --- Clip A: "Idle" (Duration: 2.0s) ---
        auto idleClip = std::make_shared<AnimationClip>("Idle", 2.0f, 30.0f);
        {
            // Spine breathing channel
            AnimationChannel spineCh;
            spineCh.BoneName = "Spine";
            spineCh.BoneIndex = 2;
            for (int i = 0; i <= 20; ++i) {
                float t = i * 0.1f;
                float breath = std::sin(t * 3.14159f);
                glm::quat rot = glm::angleAxis(glm::radians(breath * 3.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                spineCh.Rotations.push_back({ t, rot });
                spineCh.Positions.push_back({ t, glm::vec3(0.0f, 0.4f, 0.0f) });
                spineCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            idleClip->AddChannel(spineCh);

            // Hips gentle vertical idle bob
            AnimationChannel hipsCh;
            hipsCh.BoneName = "Hips";
            hipsCh.BoneIndex = 1;
            for (int i = 0; i <= 20; ++i) {
                float t = i * 0.1f;
                float bob = std::sin(t * 3.14159f) * 0.015f;
                hipsCh.Positions.push_back({ t, glm::vec3(0.0f, 1.0f + bob, 0.0f) });
                hipsCh.Rotations.push_back({ t, glm::quat(1.0f, 0.0f, 0.0f, 0.0f) });
                hipsCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            idleClip->AddChannel(hipsCh);
        }
        outClips.push_back(idleClip);

        // --- Clip B: "Walk" (Duration: 1.0s, cyclic) ---
        auto walkClip = std::make_shared<AnimationClip>("Walk", 1.0f, 30.0f);
        {
            float twoPi = 6.2831853f;

            // Left Leg swinging forward & backward (+/- 30 deg)
            AnimationChannel lLegCh;
            lLegCh.BoneName = "LeftLeg";
            lLegCh.BoneIndex = 6;
            for (int i = 0; i <= 20; ++i) {
                float t = i * 0.05f;
                float angle = std::sin(t * twoPi) * glm::radians(30.0f);
                lLegCh.Rotations.push_back({ t, glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f)) });
                lLegCh.Positions.push_back({ t, glm::vec3(-0.18f, -0.45f, 0.0f) });
                lLegCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            walkClip->AddChannel(lLegCh);

            // Right Leg swinging opposite (+/- 30 deg)
            AnimationChannel rLegCh;
            rLegCh.BoneName = "RightLeg";
            rLegCh.BoneIndex = 7;
            for (int i = 0; i <= 20; ++i) {
                float t = i * 0.05f;
                float angle = -std::sin(t * twoPi) * glm::radians(30.0f);
                rLegCh.Rotations.push_back({ t, glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f)) });
                rLegCh.Positions.push_back({ t, glm::vec3(0.18f, -0.45f, 0.0f) });
                rLegCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            walkClip->AddChannel(rLegCh);

            // Left Arm swings opposite to Left Leg
            AnimationChannel lArmCh;
            lArmCh.BoneName = "LeftArm";
            lArmCh.BoneIndex = 4;
            for (int i = 0; i <= 20; ++i) {
                float t = i * 0.05f;
                float angle = -std::sin(t * twoPi) * glm::radians(25.0f);
                lArmCh.Rotations.push_back({ t, glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f)) });
                lArmCh.Positions.push_back({ t, glm::vec3(-0.35f, 0.15f, 0.0f) });
                lArmCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            walkClip->AddChannel(lArmCh);

            // Right Arm swings opposite to Right Leg
            AnimationChannel rArmCh;
            rArmCh.BoneName = "RightArm";
            rArmCh.BoneIndex = 5;
            for (int i = 0; i <= 20; ++i) {
                float t = i * 0.05f;
                float angle = std::sin(t * twoPi) * glm::radians(25.0f);
                rArmCh.Rotations.push_back({ t, glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f)) });
                rArmCh.Positions.push_back({ t, glm::vec3(0.35f, 0.15f, 0.0f) });
                rArmCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            walkClip->AddChannel(rArmCh);

            // Hips bounce twice per step cycle
            AnimationChannel hipsCh;
            hipsCh.BoneName = "Hips";
            hipsCh.BoneIndex = 1;
            for (int i = 0; i <= 20; ++i) {
                float t = i * 0.05f;
                float bounce = std::cos(t * twoPi * 2.0f) * 0.03f;
                hipsCh.Positions.push_back({ t, glm::vec3(0.0f, 1.0f + bounce, 0.0f) });
                hipsCh.Rotations.push_back({ t, glm::quat(1.0f, 0.0f, 0.0f, 0.0f) });
                hipsCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            walkClip->AddChannel(hipsCh);
        }
        outClips.push_back(walkClip);

        AXLE_CORE_INFO("[SkeletalMeshImporter] Created Procedural Biped: {} verts, {} indices, {} bones, {} clips",
                 outMesh.Vertices.size(), outMesh.Indices.size(), outSkeleton->GetBoneCount(), outClips.size());
        return true;
    }

    void SkeletalMeshImporter::GenerateHumanoidClips(const Skeleton& skeleton,
                                                     std::vector<std::shared_ptr<AnimationClip>>& outClips) {
        float twoPi = 6.2831853f;

        // 1. Idle (2.0s)
        auto idleClip = std::make_shared<AnimationClip>("Idle", 2.0f, 30.0f);
        {
            AnimationChannel spineCh;
            spineCh.BoneName = "Spine";
            spineCh.BoneIndex = skeleton.FindBoneIndex("Spine");
            for (int i = 0; i <= 20; ++i) {
                float t = i * 0.1f;
                float breath = std::sin(t * 3.14159f);
                spineCh.Rotations.push_back({ t, glm::angleAxis(glm::radians(breath * 3.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
                spineCh.Positions.push_back({ t, glm::vec3(0.0f, 0.4f, 0.0f) });
                spineCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            idleClip->AddChannel(spineCh);

            AnimationChannel hipsCh;
            hipsCh.BoneName = "Hips";
            hipsCh.BoneIndex = skeleton.FindBoneIndex("Hips");
            for (int i = 0; i <= 20; ++i) {
                float t = i * 0.1f;
                float bob = std::sin(t * 3.14159f) * 0.015f;
                hipsCh.Positions.push_back({ t, glm::vec3(0.0f, 1.0f + bob, 0.0f) });
                hipsCh.Rotations.push_back({ t, glm::quat(1.0f, 0.0f, 0.0f, 0.0f) });
                hipsCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            idleClip->AddChannel(hipsCh);
        }
        outClips.push_back(idleClip);

        // 2. Walk (1.0s)
        auto walkClip = std::make_shared<AnimationClip>("Walk", 1.0f, 30.0f);
        {
            AnimationChannel lLegCh;
            lLegCh.BoneName = "LeftLeg";
            lLegCh.BoneIndex = skeleton.FindBoneIndex("LeftLeg");
            AnimationChannel rLegCh;
            rLegCh.BoneName = "RightLeg";
            rLegCh.BoneIndex = skeleton.FindBoneIndex("RightLeg");
            AnimationChannel lArmCh;
            lArmCh.BoneName = "LeftArm";
            lArmCh.BoneIndex = skeleton.FindBoneIndex("LeftArm");
            AnimationChannel rArmCh;
            rArmCh.BoneName = "RightArm";
            rArmCh.BoneIndex = skeleton.FindBoneIndex("RightArm");
            AnimationChannel hipsCh;
            hipsCh.BoneName = "Hips";
            hipsCh.BoneIndex = skeleton.FindBoneIndex("Hips");

            for (int i = 0; i <= 20; ++i) {
                float t = i * 0.05f;
                float phase = t * twoPi;

                lLegCh.Rotations.push_back({ t, glm::angleAxis(std::sin(phase) * glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
                lLegCh.Positions.push_back({ t, glm::vec3(-0.18f, -0.45f, 0.0f) });
                lLegCh.Scales.push_back({ t, glm::vec3(1.0f) });

                rLegCh.Rotations.push_back({ t, glm::angleAxis(-std::sin(phase) * glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
                rLegCh.Positions.push_back({ t, glm::vec3(0.18f, -0.45f, 0.0f) });
                rLegCh.Scales.push_back({ t, glm::vec3(1.0f) });

                lArmCh.Rotations.push_back({ t, glm::angleAxis(-std::sin(phase) * glm::radians(25.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
                lArmCh.Positions.push_back({ t, glm::vec3(-0.35f, 0.15f, 0.0f) });
                lArmCh.Scales.push_back({ t, glm::vec3(1.0f) });

                rArmCh.Rotations.push_back({ t, glm::angleAxis(std::sin(phase) * glm::radians(25.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
                rArmCh.Positions.push_back({ t, glm::vec3(0.35f, 0.15f, 0.0f) });
                rArmCh.Scales.push_back({ t, glm::vec3(1.0f) });

                float bounce = std::cos(phase * 2.0f) * 0.03f;
                hipsCh.Positions.push_back({ t, glm::vec3(0.0f, 1.0f + bounce, 0.0f) });
                hipsCh.Rotations.push_back({ t, glm::quat(1.0f, 0.0f, 0.0f, 0.0f) });
                hipsCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            walkClip->AddChannel(lLegCh);
            walkClip->AddChannel(rLegCh);
            walkClip->AddChannel(lArmCh);
            walkClip->AddChannel(rArmCh);
            walkClip->AddChannel(hipsCh);
        }
        outClips.push_back(walkClip);

        // 3. Run (0.6s)
        auto runClip = std::make_shared<AnimationClip>("Run", 0.6f, 30.0f);
        {
            AnimationChannel lLegCh;
            lLegCh.BoneName = "LeftLeg";
            lLegCh.BoneIndex = skeleton.FindBoneIndex("LeftLeg");
            AnimationChannel rLegCh;
            rLegCh.BoneName = "RightLeg";
            rLegCh.BoneIndex = skeleton.FindBoneIndex("RightLeg");
            AnimationChannel lArmCh;
            lArmCh.BoneName = "LeftArm";
            lArmCh.BoneIndex = skeleton.FindBoneIndex("LeftArm");
            AnimationChannel rArmCh;
            rArmCh.BoneName = "RightArm";
            rArmCh.BoneIndex = skeleton.FindBoneIndex("RightArm");
            AnimationChannel hipsCh;
            hipsCh.BoneName = "Hips";
            hipsCh.BoneIndex = skeleton.FindBoneIndex("Hips");
            AnimationChannel spineCh;
            spineCh.BoneName = "Spine";
            spineCh.BoneIndex = skeleton.FindBoneIndex("Spine");

            for (int i = 0; i <= 20; ++i) {
                float t = i * (0.6f / 20.0f);
                float phase = (t / 0.6f) * twoPi;

                lLegCh.Rotations.push_back({ t, glm::angleAxis(std::sin(phase) * glm::radians(50.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
                lLegCh.Positions.push_back({ t, glm::vec3(-0.18f, -0.45f, 0.0f) });
                lLegCh.Scales.push_back({ t, glm::vec3(1.0f) });

                rLegCh.Rotations.push_back({ t, glm::angleAxis(-std::sin(phase) * glm::radians(50.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
                rLegCh.Positions.push_back({ t, glm::vec3(0.18f, -0.45f, 0.0f) });
                rLegCh.Scales.push_back({ t, glm::vec3(1.0f) });

                lArmCh.Rotations.push_back({ t, glm::angleAxis(-std::sin(phase) * glm::radians(45.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
                lArmCh.Positions.push_back({ t, glm::vec3(-0.35f, 0.15f, 0.0f) });
                lArmCh.Scales.push_back({ t, glm::vec3(1.0f) });

                rArmCh.Rotations.push_back({ t, glm::angleAxis(std::sin(phase) * glm::radians(45.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
                rArmCh.Positions.push_back({ t, glm::vec3(0.35f, 0.15f, 0.0f) });
                rArmCh.Scales.push_back({ t, glm::vec3(1.0f) });

                float bounce = std::cos(phase * 2.0f) * 0.06f;
                hipsCh.Positions.push_back({ t, glm::vec3(0.0f, 1.0f + bounce, 0.0f) });
                hipsCh.Rotations.push_back({ t, glm::quat(1.0f, 0.0f, 0.0f, 0.0f) });
                hipsCh.Scales.push_back({ t, glm::vec3(1.0f) });

                // Athletic forward torso lean (12 degrees)
                spineCh.Rotations.push_back({ t, glm::angleAxis(glm::radians(12.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
                spineCh.Positions.push_back({ t, glm::vec3(0.0f, 0.4f, 0.0f) });
                spineCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            runClip->AddChannel(lLegCh);
            runClip->AddChannel(rLegCh);
            runClip->AddChannel(lArmCh);
            runClip->AddChannel(rArmCh);
            runClip->AddChannel(hipsCh);
            runClip->AddChannel(spineCh);
        }
        outClips.push_back(runClip);

        // 4. Jump (0.5s)
        auto jumpClip = std::make_shared<AnimationClip>("Jump", 0.5f, 30.0f);
        {
            AnimationChannel hipsCh;
            hipsCh.BoneName = "Hips";
            hipsCh.BoneIndex = skeleton.FindBoneIndex("Hips");
            AnimationChannel lArmCh;
            lArmCh.BoneName = "LeftArm";
            lArmCh.BoneIndex = skeleton.FindBoneIndex("LeftArm");
            AnimationChannel rArmCh;
            rArmCh.BoneName = "RightArm";
            rArmCh.BoneIndex = skeleton.FindBoneIndex("RightArm");
            AnimationChannel lLegCh;
            lLegCh.BoneName = "LeftLeg";
            lLegCh.BoneIndex = skeleton.FindBoneIndex("LeftLeg");
            AnimationChannel rLegCh;
            rLegCh.BoneName = "RightLeg";
            rLegCh.BoneIndex = skeleton.FindBoneIndex("RightLeg");

            for (int i = 0; i <= 20; ++i) {
                float t = i * (0.5f / 20.0f);
                float lift = std::sin((t / 0.5f) * 3.14159f) * 0.25f;

                hipsCh.Positions.push_back({ t, glm::vec3(0.0f, 1.0f + lift, 0.0f) });
                hipsCh.Rotations.push_back({ t, glm::quat(1.0f, 0.0f, 0.0f, 0.0f) });
                hipsCh.Scales.push_back({ t, glm::vec3(1.0f) });

                // Arms raised upward for momentum
                glm::quat armUp = glm::angleAxis(glm::radians(-40.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                lArmCh.Rotations.push_back({ t, armUp });
                lArmCh.Positions.push_back({ t, glm::vec3(-0.35f, 0.15f, 0.0f) });
                lArmCh.Scales.push_back({ t, glm::vec3(1.0f) });

                rArmCh.Rotations.push_back({ t, armUp });
                rArmCh.Positions.push_back({ t, glm::vec3(0.35f, 0.15f, 0.0f) });
                rArmCh.Scales.push_back({ t, glm::vec3(1.0f) });

                // Legs tucked slightly up/back
                glm::quat legTuck = glm::angleAxis(glm::radians(20.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                lLegCh.Rotations.push_back({ t, legTuck });
                lLegCh.Positions.push_back({ t, glm::vec3(-0.18f, -0.45f, 0.0f) });
                lLegCh.Scales.push_back({ t, glm::vec3(1.0f) });

                rLegCh.Rotations.push_back({ t, legTuck });
                rLegCh.Positions.push_back({ t, glm::vec3(0.18f, -0.45f, 0.0f) });
                rLegCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            jumpClip->AddChannel(hipsCh);
            jumpClip->AddChannel(lArmCh);
            jumpClip->AddChannel(rArmCh);
            jumpClip->AddChannel(lLegCh);
            jumpClip->AddChannel(rLegCh);
        }
        outClips.push_back(jumpClip);

        // 5. Fall (0.8s)
        auto fallClip = std::make_shared<AnimationClip>("Fall", 0.8f, 30.0f);
        {
            AnimationChannel hipsCh;
            hipsCh.BoneName = "Hips";
            hipsCh.BoneIndex = skeleton.FindBoneIndex("Hips");
            AnimationChannel lArmCh;
            lArmCh.BoneName = "LeftArm";
            lArmCh.BoneIndex = skeleton.FindBoneIndex("LeftArm");
            AnimationChannel rArmCh;
            rArmCh.BoneName = "RightArm";
            rArmCh.BoneIndex = skeleton.FindBoneIndex("RightArm");

            for (int i = 0; i <= 20; ++i) {
                float t = i * (0.8f / 20.0f);
                hipsCh.Positions.push_back({ t, glm::vec3(0.0f, 1.0f, 0.0f) });
                hipsCh.Rotations.push_back({ t, glm::quat(1.0f, 0.0f, 0.0f, 0.0f) });
                hipsCh.Scales.push_back({ t, glm::vec3(1.0f) });

                // Arms spread out sideways for aerodynamic balance
                lArmCh.Rotations.push_back({ t, glm::angleAxis(glm::radians(-25.0f), glm::vec3(0.0f, 0.0f, 1.0f)) });
                lArmCh.Positions.push_back({ t, glm::vec3(-0.35f, 0.15f, 0.0f) });
                lArmCh.Scales.push_back({ t, glm::vec3(1.0f) });

                rArmCh.Rotations.push_back({ t, glm::angleAxis(glm::radians(25.0f), glm::vec3(0.0f, 0.0f, 1.0f)) });
                rArmCh.Positions.push_back({ t, glm::vec3(0.35f, 0.15f, 0.0f) });
                rArmCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            fallClip->AddChannel(hipsCh);
            fallClip->AddChannel(lArmCh);
            fallClip->AddChannel(rArmCh);
        }
        outClips.push_back(fallClip);

        // 6. Land (0.4s)
        auto landClip = std::make_shared<AnimationClip>("Land", 0.4f, 30.0f);
        {
            AnimationChannel hipsCh;
            hipsCh.BoneName = "Hips";
            hipsCh.BoneIndex = skeleton.FindBoneIndex("Hips");
            AnimationChannel spineCh;
            spineCh.BoneName = "Spine";
            spineCh.BoneIndex = skeleton.FindBoneIndex("Spine");

            for (int i = 0; i <= 20; ++i) {
                float t = i * (0.4f / 20.0f);
                float normT = t / 0.4f;
                // Compression curve: dips down to -0.15m at t=0.15s, then rebounds
                float dip = std::sin(normT * 3.14159f) * 0.15f;

                hipsCh.Positions.push_back({ t, glm::vec3(0.0f, 1.0f - dip, 0.0f) });
                hipsCh.Rotations.push_back({ t, glm::quat(1.0f, 0.0f, 0.0f, 0.0f) });
                hipsCh.Scales.push_back({ t, glm::vec3(1.0f) });

                // Torso crouch pitch
                float pitch = std::sin(normT * 3.14159f) * glm::radians(18.0f);
                spineCh.Rotations.push_back({ t, glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f)) });
                spineCh.Positions.push_back({ t, glm::vec3(0.0f, 0.4f, 0.0f) });
                spineCh.Scales.push_back({ t, glm::vec3(1.0f) });
            }
            landClip->AddChannel(hipsCh);
            landClip->AddChannel(spineCh);
        }
        outClips.push_back(landClip);
    }

}
