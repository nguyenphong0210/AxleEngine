#include <Axle/AnimationAssetCodec.hpp>
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
            BoneTransform::TryFromMatrix(bone.LocalTransform, bt);
            bBlock->SetVec3("translation", bt.Translation);
            bBlock->SetVec4("rotation", glm::vec4(bt.Rotation.x, bt.Rotation.y, bt.Rotation.z, bt.Rotation.w));
            bBlock->SetVec3("scale", bt.Scale);

            root->Children.push_back(bBlock);
        }

        outDoc.Root = root;
        return true;
    }

    bool AnimationAssetCodec::DecodeSkeleton(const AssetDocument& doc, SkeletonDefinition& outDef, std::string* outError) {
        if (!doc.Root || doc.Root->Type != "Skeleton") {
            if (outError) *outError = "Document root is not of type Skeleton";
            return false;
        }

        outDef.Id = doc.Root->GetString("id");
        outDef.Name = doc.Root->GetString("name");
        outDef.SchemaVersion = doc.Root->GetUInt("version", 1);
        outDef.CompatibilitySignature = doc.Root->GetString("signature");
        outDef.Bones.clear();

        for (const auto& child : doc.Root->Children) {
            if (child->Type == "Bone") {
                Bone b;
                b.Name = child->GetString("name");
                b.ParentID = child->GetInt("parentId", -1);
                glm::vec3 t = child->GetVec3("translation", glm::vec3(0.0f));
                glm::vec4 r = child->GetVec4("rotation", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                glm::vec3 s = child->GetVec3("scale", glm::vec3(1.0f));
                BoneTransform bt(t, glm::quat(r.w, r.x, r.y, r.z), s);
                b.LocalTransform = bt.ToMatrix();
                outDef.Bones.push_back(b);
            }
        }

        if (outDef.CompatibilitySignature.empty()) {
            outDef.CompatibilitySignature = SkeletonDefinition::ComputeSignature(outDef.Bones);
        }

        return outDef.Validate(outError ? nullptr : nullptr);
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
        if (!doc.Root || doc.Root->Type != "SkeletalMesh") {
            if (outError) *outError = "Document root is not of type SkeletalMesh";
            return false;
        }

        outDef.Id = doc.Root->GetString("id");
        outDef.Name = doc.Root->GetString("name");
        outDef.SchemaVersion = doc.Root->GetUInt("version", 1);
        outDef.SkeletonRef = doc.Root->GetString("skeletonRef");
        outDef.GeometryRef = doc.Root->GetString("geometryRef");
        outDef.SourceImportPath = doc.Root->GetString("sourceImportPath");

        outDef.MaterialSlots.clear();
        if (doc.Root->HasProperty("materialSlots")) {
            outDef.MaterialSlots = doc.Root->GetProperty("materialSlots")->AsStringArray();
        }

        return outDef.Validate(outError ? nullptr : nullptr);
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

            for (const auto& k : ch.Positions) {
                auto kBlock = std::make_shared<AssetBlock>();
                kBlock->Type = "PosKey";
                kBlock->SetFloat("time", k.TimeStamp);
                kBlock->SetVec3("val", k.Value);
                chBlock->Children.push_back(kBlock);
            }
            for (const auto& k : ch.Rotations) {
                auto kBlock = std::make_shared<AssetBlock>();
                kBlock->Type = "RotKey";
                kBlock->SetFloat("time", k.TimeStamp);
                kBlock->SetVec4("val", glm::vec4(k.Value.x, k.Value.y, k.Value.z, k.Value.w));
                chBlock->Children.push_back(kBlock);
            }
            for (const auto& k : ch.Scales) {
                auto kBlock = std::make_shared<AssetBlock>();
                kBlock->Type = "ScaleKey";
                kBlock->SetFloat("time", k.TimeStamp);
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
        if (!doc.Root || doc.Root->Type != "AnimationClip") {
            if (outError) *outError = "Document root is not of type AnimationClip";
            return false;
        }

        outDef.Id = doc.Root->GetString("id");
        outDef.Name = doc.Root->GetString("name");
        outDef.SchemaVersion = doc.Root->GetUInt("version", 1);
        outDef.SkeletonRef = doc.Root->GetString("skeletonRef");
        outDef.CompatibilitySignature = doc.Root->GetString("signature");
        outDef.Duration = doc.Root->GetFloat("duration", 0.0f);
        outDef.FrameRate = doc.Root->GetFloat("frameRate", 30.0f);

        outDef.Channels.clear();
        outDef.Events.clear();

        for (const auto& child : doc.Root->Children) {
            if (child->Type == "Channel") {
                AnimationChannel ch;
                ch.BoneName = child->GetString("boneName");
                ch.BoneIndex = child->GetInt("boneIndex", -1);

                for (const auto& kChild : child->Children) {
                    if (kChild->Type == "PosKey") {
                        Keyframe<glm::vec3> k;
                        k.TimeStamp = kChild->GetFloat("time");
                        k.Value = kChild->GetVec3("val");
                        ch.Positions.push_back(k);
                    } else if (kChild->Type == "RotKey") {
                        Keyframe<glm::quat> k;
                        k.TimeStamp = kChild->GetFloat("time");
                        glm::vec4 r = kChild->GetVec4("val");
                        k.Value = glm::quat(r.w, r.x, r.y, r.z);
                        ch.Rotations.push_back(k);
                    } else if (kChild->Type == "ScaleKey") {
                        Keyframe<glm::vec3> k;
                        k.TimeStamp = kChild->GetFloat("time");
                        k.Value = kChild->GetVec3("val", glm::vec3(1.0f));
                        ch.Scales.push_back(k);
                    }
                }
                outDef.Channels.push_back(ch);
            } else if (child->Type == "Event") {
                AnimationEventDefinition ev;
                ev.Id = child->GetString("id");
                ev.Name = child->GetString("name");
                ev.Time = child->GetFloat("time");
                ev.StringParam = child->GetString("stringParam");
                ev.FloatParam = child->GetFloat("floatParam");
                ev.IntParam = child->GetInt("intParam");
                outDef.Events.push_back(ev);
            }
        }

        return outDef.Validate(outError ? nullptr : nullptr);
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
        if (!doc.Root || doc.Root->Type != "AnimatorController") {
            if (outError) *outError = "Document root is not of type AnimatorController";
            return false;
        }

        outDef.Id = doc.Root->GetString("id");
        outDef.Name = doc.Root->GetString("name");
        outDef.SchemaVersion = doc.Root->GetUInt("version", 1);
        outDef.SkeletonRef = doc.Root->GetString("skeletonRef");
        outDef.CompatibilitySignature = doc.Root->GetString("signature");
        outDef.EntryStateId = doc.Root->GetString("entryStateId");

        outDef.Parameters.clear();
        outDef.States.clear();
        outDef.Transitions.clear();

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
                outDef.Parameters.push_back(p);
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
                outDef.States.push_back(s);
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
                outDef.Transitions.push_back(t);
            }
        }

        return outDef.Validate(outError ? nullptr : nullptr);
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

    static constexpr uint32_t GEOMETRY_MAGIC = 0x424D5341; // 'ASMB' in little endian
    static constexpr uint32_t GEOMETRY_VERSION = 1;

    bool AnimationAssetCodec::BakeSkeletalMeshGeometry(const std::string& binaryPath, const SkeletalMeshData& data, const std::string& skeletonSignature, std::string* outError) {
        if (data.Vertices.empty()) {
            if (outError) *outError = "Cannot bake mesh geometry with zero vertices";
            return false;
        }

        std::vector<uint8_t> buffer;
        uint32_t sigLen = static_cast<uint32_t>(skeletonSignature.size());
        uint32_t vertCount = static_cast<uint32_t>(data.Vertices.size());
        uint32_t idxCount = static_cast<uint32_t>(data.Indices.size());

        size_t totalBytes = sizeof(uint32_t) * 2 // magic, version
                          + sizeof(uint32_t) + sigLen
                          + sizeof(uint32_t) * 2 // vertCount, idxCount
                          + sizeof(glm::vec3) * 2 // BoundsMin, BoundsMax
                          + vertCount * sizeof(SkeletalVertex)
                          + idxCount * sizeof(uint32_t);

        buffer.resize(totalBytes);
        uint8_t* ptr = buffer.data();

        auto Append = [&](const void* src, size_t n) {
            std::memcpy(ptr, src, n);
            ptr += n;
        };

        Append(&GEOMETRY_MAGIC, sizeof(GEOMETRY_MAGIC));
        Append(&GEOMETRY_VERSION, sizeof(GEOMETRY_VERSION));
        Append(&sigLen, sizeof(sigLen));
        if (sigLen > 0) {
            Append(skeletonSignature.data(), sigLen);
        }
        Append(&vertCount, sizeof(vertCount));
        Append(&idxCount, sizeof(idxCount));

        glm::vec3 bMin(std::numeric_limits<float>::max());
        glm::vec3 bMax(std::numeric_limits<float>::lowest());
        for (const auto& v : data.Vertices) {
            bMin = glm::min(bMin, v.Position);
            bMax = glm::max(bMax, v.Position);
        }
        Append(&bMin, sizeof(bMin));
        Append(&bMax, sizeof(bMax));

        Append(data.Vertices.data(), vertCount * sizeof(SkeletalVertex));
        if (idxCount > 0) {
            Append(data.Indices.data(), idxCount * sizeof(uint32_t));
        }

        return WriteBinaryFileAtomically(binaryPath, buffer.data(), buffer.size(), outError);
    }

    bool AnimationAssetCodec::LoadSkeletalMeshGeometry(const std::string& binaryPath, SkeletalMeshData& outData, std::string* outSignature, std::string* outError) {
        std::vector<uint8_t> bytes = FileSystem::ReadBinaryFile(binaryPath);
        if (bytes.empty()) {
            if (outError) *outError = "Geometry binary file not found or empty: " + binaryPath;
            return false;
        }

        if (bytes.size() < sizeof(uint32_t) * 5 + sizeof(glm::vec3) * 2) {
            if (outError) *outError = "Geometry binary file truncated (header too small)";
            return false;
        }

        const uint8_t* ptr = bytes.data();
        size_t remaining = bytes.size();

        auto Consume = [&](void* dst, size_t n) -> bool {
            if (remaining < n) return false;
            std::memcpy(dst, ptr, n);
            ptr += n;
            remaining -= n;
            return true;
        };

        uint32_t magic = 0, version = 0;
        if (!Consume(&magic, sizeof(magic)) || !Consume(&version, sizeof(version))) {
            if (outError) *outError = "Failed to read geometry header";
            return false;
        }

        if (magic != GEOMETRY_MAGIC) {
            if (outError) *outError = "Invalid geometry binary magic";
            return false;
        }
        if (version != GEOMETRY_VERSION) {
            if (outError) *outError = "Unsupported geometry binary version";
            return false;
        }

        uint32_t sigLen = 0;
        if (!Consume(&sigLen, sizeof(sigLen))) {
            if (outError) *outError = "Truncated signature length";
            return false;
        }

        if (remaining < sigLen) {
            if (outError) *outError = "Truncated signature string";
            return false;
        }
        std::string sig(reinterpret_cast<const char*>(ptr), sigLen);
        ptr += sigLen;
        remaining -= sigLen;
        if (outSignature) *outSignature = sig;

        uint32_t vertCount = 0, idxCount = 0;
        if (!Consume(&vertCount, sizeof(vertCount)) || !Consume(&idxCount, sizeof(idxCount))) {
            if (outError) *outError = "Truncated vertex/index counts";
            return false;
        }

        // Bounded count check (max 10M vertices/indices)
        if (vertCount > 10000000 || idxCount > 30000000) {
            if (outError) *outError = "Geometry counts exceed reasonable threshold";
            return false;
        }

        glm::vec3 bMin, bMax;
        if (!Consume(&bMin, sizeof(bMin)) || !Consume(&bMax, sizeof(bMax))) {
            if (outError) *outError = "Truncated bounding box";
            return false;
        }

        size_t expectedVertBytes = vertCount * sizeof(SkeletalVertex);
        size_t expectedIdxBytes = idxCount * sizeof(uint32_t);

        if (remaining != expectedVertBytes + expectedIdxBytes) {
            if (outError) *outError = "Truncated binary: payload size does not match vertex/index counts";
            return false;
        }

        outData.Vertices.resize(vertCount);
        Consume(outData.Vertices.data(), expectedVertBytes);

        outData.Indices.resize(idxCount);
        if (idxCount > 0) {
            Consume(outData.Indices.data(), expectedIdxBytes);
        }

        outData.Normalize();
        return true;
    }

}

