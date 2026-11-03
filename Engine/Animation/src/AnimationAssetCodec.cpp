#include <Axle/AnimationAssetCodec.hpp>
#include <Axle/AnimationStateMachine.hpp>
#include <Axle/FileSystem.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Axle {

    // -------------------------------------------------------------------------
    // Atomic File IO Utilities
    // -------------------------------------------------------------------------

    bool AnimationAssetCodec::WriteTextFileAtomically(const std::string& filepath, const std::string& text, std::string* outError) {
        return WriteBinaryFileAtomically(filepath, text.data(), text.size(), outError);
    }

    bool AnimationAssetCodec::WriteBinaryFileAtomically(const std::string& filepath, const void* data, size_t size, std::string* outError) {
        if (filepath.empty()) {
            if (outError) *outError = "Filepath cannot be empty";
            return false;
        }

        std::filesystem::path target(filepath);
        if (target.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                if (outError) *outError = "Cannot create directory: " + ec.message();
                return false;
            }
        }

        static std::atomic<uint64_t> s_Counter{0};
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path tempPath = target.string() + ".tmp." + std::to_string(now) + "." + std::to_string(s_Counter++);

        {
            std::ofstream out(tempPath, std::ios::binary);
            if (!out.is_open()) {
                if (outError) *outError = "Failed to open temporary file for write: " + tempPath.string();
                return false;
            }
            if (size > 0 && data != nullptr) {
                out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            }
            out.flush();
            if (!out.good()) {
                out.close();
                std::error_code ec;
                std::filesystem::remove(tempPath, ec);
                if (outError) *outError = "Failed to write complete data to temporary file";
                return false;
            }
            out.close();
        }

#ifdef _WIN32
        if (!MoveFileExW(tempPath.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            if (outError) *outError = "Failed to atomically replace target file: Windows error " + std::to_string(GetLastError());
            return false;
        }
#else
        std::error_code ec;
        std::filesystem::rename(tempPath, target, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            if (outError) *outError = "Failed to atomically replace target file: " + ec.message();
            return false;
        }
#endif

        return true;
    }

    // -------------------------------------------------------------------------
    // Skeleton Codec
    // -------------------------------------------------------------------------

    bool AnimationAssetCodec::EncodeSkeleton(const SkeletonDefinition& def, AssetDocument& outDoc) {
        if (!def.Validate()) return false;
        outDoc.Kind = AssetKind::Skeleton;
        auto root = std::make_shared<AssetBlock>();
        root->Type = "Skeleton";
        root->SetString("id", def.Id);
        root->SetString("name", def.Name);
        root->SetUInt("version", def.SchemaVersion);
        root->SetString("signature", def.CompatibilitySignature.empty() ? SkeletonDefinition::ComputeSignature(def.Bones) : def.CompatibilitySignature);

        for (const auto& bone : def.Bones) {
            auto bBlock = std::make_shared<AssetBlock>();
            bBlock->Type = "Bone";
            bBlock->SetString("name", bone.Name);
            bBlock->SetInt("parentId", bone.ParentID);

            BoneTransform bt;
            bone.GetBindTransform(bt);
            bBlock->SetBool("explicitBindTRS", bone.HasExplicitBindTRS);
            bBlock->SetVec3("translation", bt.Translation);
            bBlock->SetVec4("rotation", glm::vec4(bt.Rotation.x, bt.Rotation.y, bt.Rotation.z, bt.Rotation.w));
            bBlock->SetVec3("scale", bt.Scale);
            for (int column = 0; column < 4; ++column) {
                bBlock->SetVec4("local" + std::to_string(column), bone.LocalTransform[column]);
                bBlock->SetVec4("inverseBind" + std::to_string(column), bone.InverseBindMatrix[column]);
            }

            root->Children.push_back(bBlock);
        }

        outDoc.Root = root;
        return true;
    }

    bool AnimationAssetCodec::DecodeSkeleton(const AssetDocument& doc, SkeletonDefinition& outDef, std::string* outError) {
        SkeletonDefinition candidate;
        if (!doc.Root || doc.Root->Type != "Skeleton") {
            if (outError) *outError = "Document root is not of type Skeleton";
            return false;
        }

        candidate.Id = doc.Root->GetString("id");
        candidate.Name = doc.Root->GetString("name");
        candidate.SchemaVersion = doc.Root->GetUInt("version", 1);
        candidate.CompatibilitySignature = doc.Root->GetString("signature");
        candidate.Bones.clear();

        for (const auto& child : doc.Root->Children) {
            if (child->Type == "Bone") {
                Bone b;
                b.Name = child->GetString("name");
                b.ParentID = child->GetInt("parentId", -1);
                glm::vec3 t = child->GetVec3("translation", glm::vec3(0.0f));
                glm::vec4 r = child->GetVec4("rotation", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                glm::vec3 s = child->GetVec3("scale", glm::vec3(1.0f));
                BoneTransform bt(t, glm::quat(r.w, r.x, r.y, r.z), s);
                b.HasExplicitBindTRS = child->GetBool("explicitBindTRS", false);
                b.BindTranslation = t; b.BindRotation = bt.Rotation; b.BindScale = s;
                b.LocalTransform = bt.ToMatrix();
                for (int column = 0; column < 4; ++column) {
                    const auto key = "inverseBind" + std::to_string(column);
                    if (!child->HasProperty(key)) {
                        if (outError) *outError = "Skeleton asset lacks inverse binds; reimport the source rig.";
                        return false;
                    }
                    b.InverseBindMatrix[column] = child->GetVec4(key);
                    if (child->HasProperty("local" + std::to_string(column)))
                        b.LocalTransform[column] = child->GetVec4("local" + std::to_string(column));
                }
                candidate.Bones.push_back(b);
            }
        }

        if (candidate.CompatibilitySignature.empty()) {
            candidate.CompatibilitySignature = SkeletonDefinition::ComputeSignature(candidate.Bones);
        }

        std::vector<std::string> errors;
        if (!candidate.Validate(&errors)) {
            if (outError) *outError = errors.empty() ? "Invalid animation asset" : errors.front();
            return false;
        }
        outDef = std::move(candidate);
        return true;
    }

    bool AnimationAssetCodec::SaveSkeleton(const std::string& filepath, const SkeletonDefinition& def, std::string* outError) {
        AssetDocument doc;
        if (!EncodeSkeleton(def, doc)) {
            if (outError) *outError = "Failed to encode skeleton";
            return false;
        }
        std::string text = AssetDSL::SerializeDocument(doc);
        return WriteTextFileAtomically(filepath, text, outError);
    }

    bool AnimationAssetCodec::LoadSkeleton(const std::string& filepath, SkeletonDefinition& outDef, std::string* outError) {
        std::string content = FileSystem::ReadTextFile(filepath);
        if (content.empty() && !FileSystem::Exists(filepath)) {
            if (outError) *outError = "Skeleton file not found: " + filepath;
            return false;
        }
        AssetDocument doc;
        if (!AssetDSL::ParseSource(content, doc, outError)) {
            return false;
        }
        return DecodeSkeleton(doc, outDef, outError);
    }

    // -------------------------------------------------------------------------
    // Skeletal Mesh Codec
    // -------------------------------------------------------------------------

    bool AnimationAssetCodec::EncodeSkeletalMesh(const SkeletalMeshDefinition& def, AssetDocument& outDoc) {
        if (!def.Validate()) return false;
        outDoc.Kind = AssetKind::SkeletalMesh;
        auto root = std::make_shared<AssetBlock>();
        root->Type = "SkeletalMesh";
        root->SetString("id", def.Id);
        root->SetString("name", def.Name);
        root->SetUInt("version", def.SchemaVersion);
        root->SetString("skeletonRef", def.SkeletonRef);
        root->SetString("geometryRef", def.GeometryRef);
        root->SetString("sourceImportPath", def.SourceImportPath);
        root->SetStringArray("materialSlots", def.MaterialSlots);

        outDoc.Root = root;
        return true;
    }

    bool AnimationAssetCodec::DecodeSkeletalMesh(const AssetDocument& doc, SkeletalMeshDefinition& outDef, std::string* outError) {
        SkeletalMeshDefinition candidate;
        if (!doc.Root || doc.Root->Type != "SkeletalMesh") {
            if (outError) *outError = "Document root is not of type SkeletalMesh";
            return false;
        }

        candidate.Id = doc.Root->GetString("id");
        candidate.Name = doc.Root->GetString("name");
        candidate.SchemaVersion = doc.Root->GetUInt("version", 1);
        candidate.SkeletonRef = doc.Root->GetString("skeletonRef");
        candidate.GeometryRef = doc.Root->GetString("geometryRef");
        candidate.SourceImportPath = doc.Root->GetString("sourceImportPath");

        candidate.MaterialSlots.clear();
        if (doc.Root->HasProperty("materialSlots")) {
            candidate.MaterialSlots = doc.Root->GetProperty("materialSlots")->AsStringArray();
        }

        std::vector<std::string> errors;
        if (!candidate.Validate(&errors)) {
            if (outError) *outError = errors.empty() ? "Invalid animation asset" : errors.front();
            return false;
        }
        outDef = std::move(candidate);
        return true;
    }

    bool AnimationAssetCodec::SaveSkeletalMesh(const std::string& filepath, const SkeletalMeshDefinition& def, std::string* outError) {
        AssetDocument doc;
        if (!EncodeSkeletalMesh(def, doc)) {
            if (outError) *outError = "Failed to encode skeletal mesh";
            return false;
        }
        std::string text = AssetDSL::SerializeDocument(doc);
        return WriteTextFileAtomically(filepath, text, outError);
    }

    bool AnimationAssetCodec::LoadSkeletalMesh(const std::string& filepath, SkeletalMeshDefinition& outDef, std::string* outError) {
        std::string content = FileSystem::ReadTextFile(filepath);
        if (content.empty() && !FileSystem::Exists(filepath)) {
            if (outError) *outError = "Skeletal mesh file not found: " + filepath;
            return false;
        }
        AssetDocument doc;
        if (!AssetDSL::ParseSource(content, doc, outError)) {
            return false;
        }
        return DecodeSkeletalMesh(doc, outDef, outError);
    }

    // -------------------------------------------------------------------------
    // Animation Clip Codec
    // -------------------------------------------------------------------------

    bool AnimationAssetCodec::EncodeAnimationClip(const AnimationClipDefinition& def, AssetDocument& outDoc) {
        if (!def.Validate()) return false;
        outDoc.Kind = AssetKind::AnimationClip;
        auto root = std::make_shared<AssetBlock>();
        root->Type = "AnimationClip";
        root->SetString("id", def.Id);
        root->SetString("name", def.Name);
        root->SetUInt("version", def.SchemaVersion);
        root->SetString("skeletonRef", def.SkeletonRef);
        root->SetString("signature", def.CompatibilitySignature);
        root->SetFloat("duration", def.Duration);
        root->SetFloat("frameRate", def.FrameRate);

        for (const auto& ch : def.Channels) {
            auto chBlock = std::make_shared<AssetBlock>();
            chBlock->Type = "Channel";
            chBlock->SetString("boneName", ch.BoneName);
            chBlock->SetInt("boneIndex", ch.BoneIndex);
            chBlock->SetUInt("positionInterpolation", uint32_t(ch.PositionsInterpolation));
            chBlock->SetUInt("rotationInterpolation", uint32_t(ch.RotationsInterpolation));
            chBlock->SetUInt("scaleInterpolation", uint32_t(ch.ScalesInterpolation));

            for (const auto& k : ch.Positions) {
                auto kBlock = std::make_shared<AssetBlock>();
                kBlock->Type = "PosKey";
                kBlock->SetFloat("time", k.TimeStamp);
                kBlock->SetVec4("inTangent", k.InTangent);
                kBlock->SetVec4("outTangent", k.OutTangent);
                kBlock->SetVec3("val", k.Value);
                chBlock->Children.push_back(kBlock);
            }
            for (const auto& k : ch.Rotations) {
                auto kBlock = std::make_shared<AssetBlock>();
                kBlock->Type = "RotKey";
                kBlock->SetFloat("time", k.TimeStamp);
                kBlock->SetVec4("inTangent", k.InTangent);
                kBlock->SetVec4("outTangent", k.OutTangent);
                kBlock->SetVec4("val", glm::vec4(k.Value.x, k.Value.y, k.Value.z, k.Value.w));
                chBlock->Children.push_back(kBlock);
            }
            for (const auto& k : ch.Scales) {
                auto kBlock = std::make_shared<AssetBlock>();
                kBlock->Type = "ScaleKey";
                kBlock->SetFloat("time", k.TimeStamp);
                kBlock->SetVec4("inTangent", k.InTangent);
                kBlock->SetVec4("outTangent", k.OutTangent);
                kBlock->SetVec3("val", k.Value);
                chBlock->Children.push_back(kBlock);
            }

            root->Children.push_back(chBlock);
        }

        for (const auto& ev : def.Events) {
            auto evBlock = std::make_shared<AssetBlock>();
            evBlock->Type = "Event";
            evBlock->SetString("id", ev.Id);
            evBlock->SetString("name", ev.Name);
            evBlock->SetFloat("time", ev.Time);
            if (!ev.StringParam.empty()) evBlock->SetString("stringParam", ev.StringParam);
            if (ev.FloatParam != 0.0f) evBlock->SetFloat("floatParam", ev.FloatParam);
            if (ev.IntParam != 0) evBlock->SetInt("intParam", ev.IntParam);
            root->Children.push_back(evBlock);
        }

        outDoc.Root = root;
        return true;
    }

    bool AnimationAssetCodec::DecodeAnimationClip(const AssetDocument& doc, AnimationClipDefinition& outDef, std::string* outError) {
        AnimationClipDefinition candidate;
        if (!doc.Root || doc.Root->Type != "AnimationClip") {
            if (outError) *outError = "Document root is not of type AnimationClip";
            return false;
        }

        candidate.Id = doc.Root->GetString("id");
        candidate.Name = doc.Root->GetString("name");
        candidate.SchemaVersion = doc.Root->GetUInt("version", 1);
        candidate.SkeletonRef = doc.Root->GetString("skeletonRef");
        candidate.CompatibilitySignature = doc.Root->GetString("signature");
        candidate.Duration = doc.Root->GetFloat("duration", 0.0f);
        candidate.FrameRate = doc.Root->GetFloat("frameRate", 30.0f);

        candidate.Channels.clear();
        candidate.Events.clear();

        for (const auto& child : doc.Root->Children) {
            if (child->Type == "Channel") {
                AnimationChannel ch;
                ch.BoneName = child->GetString("boneName");
                ch.BoneIndex = child->GetInt("boneIndex", -1);
                ch.PositionsInterpolation = AnimationInterpolation(child->GetUInt("positionInterpolation", 1));
                ch.RotationsInterpolation = AnimationInterpolation(child->GetUInt("rotationInterpolation", 1));
                ch.ScalesInterpolation = AnimationInterpolation(child->GetUInt("scaleInterpolation", 1));

                for (const auto& kChild : child->Children) {
                    if (kChild->Type == "PosKey") {
                        Keyframe<glm::vec3> k;
                        k.TimeStamp = kChild->GetFloat("time");
                        k.InTangent = kChild->GetVec4("inTangent", glm::vec4(0));
                        k.OutTangent = kChild->GetVec4("outTangent", glm::vec4(0));
                        k.Value = kChild->GetVec3("val");
                        ch.Positions.push_back(k);
                    } else if (kChild->Type == "RotKey") {
                        Keyframe<glm::quat> k;
                        k.TimeStamp = kChild->GetFloat("time");
                        k.InTangent = kChild->GetVec4("inTangent", glm::vec4(0));
                        k.OutTangent = kChild->GetVec4("outTangent", glm::vec4(0));
                        glm::vec4 r = kChild->GetVec4("val");
                        k.Value = glm::quat(r.w, r.x, r.y, r.z);
                        ch.Rotations.push_back(k);
                    } else if (kChild->Type == "ScaleKey") {
                        Keyframe<glm::vec3> k;
                        k.TimeStamp = kChild->GetFloat("time");
                        k.InTangent = kChild->GetVec4("inTangent", glm::vec4(0));
                        k.OutTangent = kChild->GetVec4("outTangent", glm::vec4(0));
                        k.Value = kChild->GetVec3("val", glm::vec3(1.0f));
                        ch.Scales.push_back(k);
                    }
                }
                candidate.Channels.push_back(ch);
            } else if (child->Type == "Event") {
                AnimationEventDefinition ev;
                ev.Id = child->GetString("id");
                ev.Name = child->GetString("name");
                ev.Time = child->GetFloat("time");
                ev.StringParam = child->GetString("stringParam");
                ev.FloatParam = child->GetFloat("floatParam");
                ev.IntParam = child->GetInt("intParam");
                candidate.Events.push_back(ev);
            }
        }

        std::vector<std::string> errors;
        if (!candidate.Validate(&errors)) {
            if (outError) *outError = errors.empty() ? "Invalid animation asset" : errors.front();
            return false;
        }
        outDef = std::move(candidate);
        return true;
    }

    bool AnimationAssetCodec::SaveAnimationClip(const std::string& filepath, const AnimationClipDefinition& def, std::string* outError) {
        AssetDocument doc;
        if (!EncodeAnimationClip(def, doc)) {
            if (outError) *outError = "Failed to encode animation clip";
            return false;
        }
        std::string text = AssetDSL::SerializeDocument(doc);
        return WriteTextFileAtomically(filepath, text, outError);
    }

    bool AnimationAssetCodec::LoadAnimationClip(const std::string& filepath, AnimationClipDefinition& outDef, std::string* outError) {
        std::string content = FileSystem::ReadTextFile(filepath);
        if (content.empty() && !FileSystem::Exists(filepath)) {
            if (outError) *outError = "Animation clip file not found: " + filepath;
            return false;
        }
        AssetDocument doc;
        if (!AssetDSL::ParseSource(content, doc, outError)) {
            return false;
        }
        return DecodeAnimationClip(doc, outDef, outError);
    }

    // -------------------------------------------------------------------------
    // Animator Controller Codec
    // -------------------------------------------------------------------------

    static std::string ParamTypeToString(AnimatorParameterType type) {
        switch (type) {
            case AnimatorParameterType::Float: return "Float";
            case AnimatorParameterType::Int: return "Int";
            case AnimatorParameterType::Bool: return "Bool";
            case AnimatorParameterType::Trigger: return "Trigger";
            default: return "Float";
        }
    }

    static AnimatorParameterType StringToParamType(std::string_view str) {
        if (str == "Float") return AnimatorParameterType::Float;
        if (str == "Int") return AnimatorParameterType::Int;
        if (str == "Bool") return AnimatorParameterType::Bool;
        if (str == "Trigger") return AnimatorParameterType::Trigger;
        return AnimatorParameterType::Float;
    }

    static std::string OperatorToString(ConditionOperator op) {
        switch (op) {
            case ConditionOperator::AlwaysTrue: return "AlwaysTrue";
            case ConditionOperator::Greater: return "Greater";
            case ConditionOperator::Less: return "Less";
            case ConditionOperator::GreaterOrEqual: return "GreaterOrEqual";
            case ConditionOperator::LessOrEqual: return "LessOrEqual";
            case ConditionOperator::Equal: return "Equal";
            case ConditionOperator::NotEqual: return "NotEqual";
            case ConditionOperator::IsTrue: return "IsTrue";
            case ConditionOperator::IsFalse: return "IsFalse";
            case ConditionOperator::Triggered: return "Triggered";
            default: return "AlwaysTrue";
        }
    }

    static ConditionOperator StringToOperator(std::string_view str) {
        if (str == "AlwaysTrue") return ConditionOperator::AlwaysTrue;
        if (str == "Greater") return ConditionOperator::Greater;
        if (str == "Less") return ConditionOperator::Less;
        if (str == "GreaterOrEqual") return ConditionOperator::GreaterOrEqual;
        if (str == "LessOrEqual") return ConditionOperator::LessOrEqual;
        if (str == "Equal") return ConditionOperator::Equal;
        if (str == "NotEqual") return ConditionOperator::NotEqual;
        if (str == "IsTrue") return ConditionOperator::IsTrue;
        if (str == "IsFalse") return ConditionOperator::IsFalse;
        if (str == "Triggered") return ConditionOperator::Triggered;
        return ConditionOperator::AlwaysTrue;
    }

    static std::string NodeTypeToString(AnimationNodeType type) {
        switch (type) {
            case AnimationNodeType::Clip: return "Clip";
            case AnimationNodeType::BlendSpace1D: return "BlendSpace1D";
            case AnimationNodeType::BlendSpace2D: return "BlendSpace2D";
            default: return "Clip";
        }
    }

    static AnimationNodeType StringToNodeType(std::string_view str) {
        if (str == "Clip") return AnimationNodeType::Clip;
        if (str == "BlendSpace1D") return AnimationNodeType::BlendSpace1D;
        if (str == "BlendSpace2D") return AnimationNodeType::BlendSpace2D;
        return AnimationNodeType::Clip;
    }

    bool AnimationAssetCodec::EncodeAnimatorController(const AnimatorControllerDefinition& def, AssetDocument& outDoc) {
        if (!def.Validate()) return false;
        outDoc.Kind = AssetKind::AnimatorController;
        auto root = std::make_shared<AssetBlock>();
        root->Type = "AnimatorController";
        root->SetString("id", def.Id);
        root->SetString("name", def.Name);
        root->SetUInt("version", def.SchemaVersion);
        root->SetString("skeletonRef", def.SkeletonRef);
        root->SetString("signature", def.CompatibilitySignature);
        root->SetString("entryStateId", def.EntryStateId);

        // Parameters
        for (const auto& p : def.Parameters) {
            auto pBlock = std::make_shared<AssetBlock>();
            pBlock->Type = "Parameter";
            pBlock->SetString("id", p.Id);
            pBlock->SetString("name", p.Name);
            pBlock->SetString("type", ParamTypeToString(p.Type));
            if (p.Type == AnimatorParameterType::Float && p.Default.index() == 0) {
                pBlock->SetFloat("default", std::get<float>(p.Default));
            } else if (p.Type == AnimatorParameterType::Int && p.Default.index() == 1) {
                pBlock->SetInt("default", std::get<int32_t>(p.Default));
            } else if (p.Type == AnimatorParameterType::Bool && p.Default.index() == 2) {
                pBlock->SetBool("default", std::get<bool>(p.Default));
            }
            root->Children.push_back(pBlock);
        }

        // States
        for (const auto& s : def.States) {
            auto sBlock = std::make_shared<AssetBlock>();
            sBlock->Type = "State";
            sBlock->SetString("id", s.Id);
            sBlock->SetString("name", s.Name);
            sBlock->SetString("nodeType", NodeTypeToString(s.NodeType));
            sBlock->SetString("motionRef", s.MotionAssetRef);
            sBlock->SetFloat("speedMultiplier", s.SpeedMultiplier);
            sBlock->SetBool("loop", s.Loop);
            if (!s.BlendParameterX.empty()) sBlock->SetString("blendParameterX", s.BlendParameterX);
            if (!s.BlendParameterY.empty()) sBlock->SetString("blendParameterY", s.BlendParameterY);
            sBlock->SetVec2("graphPosition", s.GraphPosition);
            root->Children.push_back(sBlock);
        }

        // Transitions
        for (const auto& t : def.Transitions) {
            auto tBlock = std::make_shared<AssetBlock>();
            tBlock->Type = "Transition";
            tBlock->SetString("id", t.Id);
            tBlock->SetString("fromState", t.FromState);
            tBlock->SetString("toState", t.ToState);
            tBlock->SetFloat("crossfadeSeconds", t.CrossfadeSeconds);
            tBlock->SetBool("hasExitTime", t.HasExitTime);
            tBlock->SetFloat("exitPhase", t.ExitPhase);
            tBlock->SetInt("priority", t.Priority);

            for (const auto& c : t.AllConditions) {
                auto cBlock = std::make_shared<AssetBlock>();
                cBlock->Type = "Condition";
                cBlock->SetString("parameter", c.Parameter);
                cBlock->SetString("operator", OperatorToString(c.Operator));
                if (c.Operand.index() == 0) {
                    cBlock->SetFloat("operand", std::get<float>(c.Operand));
                } else if (c.Operand.index() == 1) {
                    cBlock->SetInt("operand", std::get<int32_t>(c.Operand));
                } else if (c.Operand.index() == 2) {
                    cBlock->SetBool("operand", std::get<bool>(c.Operand));
                }
                tBlock->Children.push_back(cBlock);
            }
            root->Children.push_back(tBlock);
        }

        outDoc.Root = root;
        return true;
    }

    bool AnimationAssetCodec::DecodeAnimatorController(const AssetDocument& doc, AnimatorControllerDefinition& outDef, std::string* outError) {
        AnimatorControllerDefinition candidate;
        if (!doc.Root || doc.Root->Type != "AnimatorController") {
            if (outError) *outError = "Document root is not of type AnimatorController";
            return false;
        }

        candidate.Id = doc.Root->GetString("id");
        candidate.Name = doc.Root->GetString("name");
        candidate.SchemaVersion = doc.Root->GetUInt("version", 1);
        candidate.SkeletonRef = doc.Root->GetString("skeletonRef");
        candidate.CompatibilitySignature = doc.Root->GetString("signature");
        candidate.EntryStateId = doc.Root->GetString("entryStateId");

        candidate.Parameters.clear();
        candidate.States.clear();
        candidate.Transitions.clear();

        for (const auto& child : doc.Root->Children) {
            if (child->Type == "Parameter") {
                ParameterDefinition p;
                p.Id = child->GetString("id");
                p.Name = child->GetString("name");
                p.Type = StringToParamType(child->GetString("type"));
                if (p.Type == AnimatorParameterType::Float) {
                    p.Default = child->GetFloat("default", 0.0f);
                } else if (p.Type == AnimatorParameterType::Int) {
                    p.Default = child->GetInt("default", 0);
                } else if (p.Type == AnimatorParameterType::Bool) {
                    p.Default = child->GetBool("default", false);
                } else {
                    p.Default = false;
                }
                candidate.Parameters.push_back(p);
            } else if (child->Type == "State") {
                StateDefinition s;
                s.Id = child->GetString("id");
                s.Name = child->GetString("name");
                s.NodeType = StringToNodeType(child->GetString("nodeType"));
                s.MotionAssetRef = child->GetString("motionRef");
                s.SpeedMultiplier = child->GetFloat("speedMultiplier", 1.0f);
                s.Loop = child->GetBool("loop", true);
                s.BlendParameterX = child->GetString("blendParameterX");
                s.BlendParameterY = child->GetString("blendParameterY");
                s.GraphPosition = child->GetVec2("graphPosition", glm::vec2(0.0f));
                candidate.States.push_back(s);
            } else if (child->Type == "Transition") {
                TransitionDefinition t;
                t.Id = child->GetString("id");
                t.FromState = child->GetString("fromState");
                t.ToState = child->GetString("toState");
                t.CrossfadeSeconds = child->GetFloat("crossfadeSeconds", 0.2f);
                t.HasExitTime = child->GetBool("hasExitTime", false);
                t.ExitPhase = child->GetFloat("exitPhase", 1.0f);
                t.Priority = child->GetInt("priority", 0);

                for (const auto& cChild : child->Children) {
                    if (cChild->Type == "Condition") {
                        ConditionDefinition c;
                        c.Parameter = cChild->GetString("parameter");
                        c.Operator = StringToOperator(cChild->GetString("operator"));
                        // Identify operand type from parameter if possible, or probe property
                        if (cChild->HasProperty("operand")) {
                            const auto* prop = cChild->GetProperty("operand");
                            // If raw value is true/false
                            if (prop->RawValue == "true" || prop->RawValue == "false") {
                                c.Operand = prop->AsBool();
                            } else if (prop->RawValue.find('.') != std::string::npos) {
                                c.Operand = prop->AsFloat();
                            } else {
                                c.Operand = prop->AsInt();
                            }
                        }
                        t.AllConditions.push_back(c);
                    }
                }
                candidate.Transitions.push_back(t);
            }
        }

        std::vector<std::string> errors;
        if (!candidate.Validate(&errors)) {
            if (outError) *outError = errors.empty() ? "Invalid animation asset" : errors.front();
            return false;
        }
        outDef = std::move(candidate);
        return true;
    }

    bool AnimationAssetCodec::SaveAnimatorController(const std::string& filepath, const AnimatorControllerDefinition& def, std::string* outError) {
        AssetDocument doc;
        if (!EncodeAnimatorController(def, doc)) {
            if (outError) *outError = "Failed to encode animator controller";
            return false;
        }
        std::string text = AssetDSL::SerializeDocument(doc);
        return WriteTextFileAtomically(filepath, text, outError);
    }

    bool AnimationAssetCodec::LoadAnimatorController(const std::string& filepath, AnimatorControllerDefinition& outDef, std::string* outError) {
        std::string content = FileSystem::ReadTextFile(filepath);
        if (content.empty() && !FileSystem::Exists(filepath)) {
            if (outError) *outError = "Animator controller file not found: " + filepath;
            return false;
        }
        AssetDocument doc;
        if (!AssetDSL::ParseSource(content, doc, outError)) {
            return false;
        }
        return DecodeAnimatorController(doc, outDef, outError);
    }

    // -------------------------------------------------------------------------
    // Binary Cooked Geometry Baking & Loading
    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // State Machine Compilation & Extraction
    // -------------------------------------------------------------------------

    std::shared_ptr<AnimationStateMachine> AnimationAssetCodec::CompileStateMachineFromDefinition(const AnimatorControllerDefinition& def) {
        auto sm = std::make_shared<AnimationStateMachine>(def.Name.empty() ? "AnimatorController" : def.Name);

        for (const auto& param : def.Parameters) {
            sm->DeclareParameter(param);
        }

        std::unordered_map<AnimationId, std::string> idToName;
        for (const auto& sDef : def.States) {
            idToName[sDef.Id] = sDef.Name;
            AnimationState s;
            s.Name = sDef.Name;
            s.Type = sDef.NodeType;
            s.SpeedMultiplier = sDef.SpeedMultiplier;
            s.Loop = sDef.Loop;
            s.BlendParameterName = sDef.BlendParameterX.empty() ? "Speed" : sDef.BlendParameterX;
            sm->AddState(s);
        }

        if (!def.EntryStateId.empty()) {
            auto it = idToName.find(def.EntryStateId);
            if (it != idToName.end()) {
                sm->SetEntryState(it->second);
            }
        }

        for (const auto& tDef : def.Transitions) {
            AnimationTransition t;
            auto itFrom = idToName.find(tDef.FromState);
            auto itTo = idToName.find(tDef.ToState);
            t.FromState = (itFrom != idToName.end()) ? itFrom->second : tDef.FromState;
            t.ToState = (itTo != idToName.end()) ? itTo->second : tDef.ToState;
            t.Duration = tDef.CrossfadeSeconds;
            t.HasExitTime = tDef.HasExitTime;
            t.ExitTime = tDef.ExitPhase;
            t.ExitPhase = tDef.ExitPhase;
            t.Priority = tDef.Priority;
            t.PureConditions = tDef.AllConditions;

            for (const auto& cond : tDef.AllConditions) {
                TransitionRule rule;
                rule.Parameter = cond.Parameter;
                switch (cond.Operator) {
                    case ConditionOperator::AlwaysTrue: rule.Operator = TransitionConditionOperator::AlwaysTrue; break;
                    case ConditionOperator::Greater: rule.Operator = TransitionConditionOperator::Greater; break;
                    case ConditionOperator::Less: rule.Operator = TransitionConditionOperator::Less; break;
                    case ConditionOperator::GreaterOrEqual: rule.Operator = TransitionConditionOperator::GreaterOrEqual; break;
                    case ConditionOperator::LessOrEqual: rule.Operator = TransitionConditionOperator::LessOrEqual; break;
                    case ConditionOperator::Equal: rule.Operator = TransitionConditionOperator::Equal; break;
                    case ConditionOperator::NotEqual: rule.Operator = TransitionConditionOperator::NotEqual; break;
                    case ConditionOperator::IsTrue: rule.Operator = TransitionConditionOperator::IsTrue; break;
                    case ConditionOperator::IsFalse: rule.Operator = TransitionConditionOperator::IsFalse; break;
                    case ConditionOperator::Triggered: rule.Operator = TransitionConditionOperator::Triggered; break;
                }
                if (cond.Operand.index() == 0) {
                    rule.FloatOperand = std::get<0>(cond.Operand);
                } else if (cond.Operand.index() == 1) {
                    rule.IntOperand = std::get<1>(cond.Operand);
                } else if (cond.Operand.index() == 2) {
                    rule.BoolOperand = std::get<2>(cond.Operand);
                }
                t.Rules.push_back(rule);
            }

            sm->AddTransition(t);
        }

        return sm;
    }

    AnimatorControllerDefinition AnimationAssetCodec::ExtractDefinitionFromStateMachine(const AnimationStateMachine& sm) {
        AnimatorControllerDefinition def;
        def.Name = sm.GetName();
        def.Id = sm.GetName().empty() ? "Controller_Def" : (sm.GetName() + "_Def");

        for (const auto& [id, param] : sm.GetDeclaredParameters()) {
            def.Parameters.push_back(param);
        }

        std::unordered_map<std::string, AnimationId> nameToId;
        for (const auto& [name, state] : sm.GetStates()) {
            StateDefinition sDef;
            sDef.Id = "State_" + name;
            sDef.Name = name;
            sDef.NodeType = state.Type;
            sDef.SpeedMultiplier = state.SpeedMultiplier;
            sDef.Loop = state.Loop;
            sDef.BlendParameterX = state.BlendParameterName;
            def.States.push_back(sDef);
            nameToId[name] = sDef.Id;
        }

        if (!sm.GetInitialStateName().empty()) {
            auto it = nameToId.find(sm.GetInitialStateName());
            if (it != nameToId.end()) {
                def.EntryStateId = it->second;
            }
        }

        int transCounter = 0;
        for (const auto& trans : sm.GetTransitions()) {
            TransitionDefinition tDef;
            tDef.Id = "Trans_" + std::to_string(transCounter++);
            auto itFrom = nameToId.find(trans.FromState);
            auto itTo = nameToId.find(trans.ToState);
            tDef.FromState = (itFrom != nameToId.end()) ? itFrom->second : trans.FromState;
            tDef.ToState = (itTo != nameToId.end()) ? itTo->second : trans.ToState;
            tDef.CrossfadeSeconds = trans.Duration;
            tDef.HasExitTime = trans.HasExitTime;
            tDef.ExitPhase = trans.ExitPhase;
            tDef.Priority = trans.Priority;
            tDef.AllConditions = trans.PureConditions;
            def.Transitions.push_back(tDef);
        }

        return def;
    }

    std::string AnimationAssetCodec::EncodeControllerToText(const AnimatorControllerDefinition& def) {
        AssetDocument doc;
        if (!EncodeAnimatorController(def, doc)) return "";
        return AssetDSL::SerializeDocument(doc);
    }

    bool AnimationAssetCodec::DecodeControllerFromText(const std::string& text, AnimatorControllerDefinition& outDef, std::string* outError) {
        if (text.empty()) {
            if (outError) *outError = "Empty controller text";
            return false;
        }
        AssetDocument doc;
        if (!AssetDSL::ParseSource(text, doc, outError)) return false;
        return DecodeAnimatorController(doc, outDef, outError);
    }

    std::string AnimationAssetCodec::HexEncode(const std::string& input) {
        static const char hexChars[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(input.size() * 2);
        for (unsigned char c : input) {
            out.push_back(hexChars[(c >> 4) & 0x0F]);
            out.push_back(hexChars[c & 0x0F]);
        }
        return out;
    }

    std::string AnimationAssetCodec::HexDecode(const std::string& hex) {
        std::string out;
        if (hex.size() % 2 != 0) return out;
        out.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2) {
            char high = hex[i];
            char low = hex[i + 1];
            auto parseNibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return 0;
            };
            unsigned char byte = (parseNibble(high) << 4) | parseNibble(low);
            out.push_back(static_cast<char>(byte));
        }
        return out;
    }

}

