#pragma once

#include <Axle/AnimationDefinition.hpp>
#include <Axle/SkeletalMesh.hpp>
#include <Axle/AssetDSL.hpp>
#include <string>
#include <vector>
#include <memory>

namespace Axle {

    class AnimationStateMachine;

    class AnimationAssetCodec {
    public:
        // --- Atomic File IO Utilities ---
        static bool WriteTextFileAtomically(const std::string& filepath, const std::string& text, std::string* outError = nullptr);
        static bool WriteBinaryFileAtomically(const std::string& filepath, const void* data, size_t size, std::string* outError = nullptr);

        // --- Skeleton Codec ---
        static bool EncodeSkeleton(const SkeletonDefinition& def, AssetDocument& outDoc);
        static bool DecodeSkeleton(const AssetDocument& doc, SkeletonDefinition& outDef, std::string* outError = nullptr);
        static bool SaveSkeleton(const std::string& filepath, const SkeletonDefinition& def, std::string* outError = nullptr);
        static bool LoadSkeleton(const std::string& filepath, SkeletonDefinition& outDef, std::string* outError = nullptr);

        // --- Skeletal Mesh Codec ---
        static bool EncodeSkeletalMesh(const SkeletalMeshDefinition& def, AssetDocument& outDoc);
        static bool DecodeSkeletalMesh(const AssetDocument& doc, SkeletalMeshDefinition& outDef, std::string* outError = nullptr);
        static bool SaveSkeletalMesh(const std::string& filepath, const SkeletalMeshDefinition& def, std::string* outError = nullptr);
        static bool LoadSkeletalMesh(const std::string& filepath, SkeletalMeshDefinition& outDef, std::string* outError = nullptr);

        // --- Animation Clip Codec ---
        static bool EncodeAnimationClip(const AnimationClipDefinition& def, AssetDocument& outDoc);
        static bool DecodeAnimationClip(const AssetDocument& doc, AnimationClipDefinition& outDef, std::string* outError = nullptr);
        static bool SaveAnimationClip(const std::string& filepath, const AnimationClipDefinition& def, std::string* outError = nullptr);
        static bool LoadAnimationClip(const std::string& filepath, AnimationClipDefinition& outDef, std::string* outError = nullptr);

        // --- Animator Controller Codec ---
        static bool EncodeAnimatorController(const AnimatorControllerDefinition& def, AssetDocument& outDoc);
        static bool DecodeAnimatorController(const AssetDocument& doc, AnimatorControllerDefinition& outDef, std::string* outError = nullptr);
        static bool SaveAnimatorController(const std::string& filepath, const AnimatorControllerDefinition& def, std::string* outError = nullptr);
        static bool LoadAnimatorController(const std::string& filepath, AnimatorControllerDefinition& outDef, std::string* outError = nullptr);

        // --- State Machine Compilation & Extraction ---
        static std::shared_ptr<AnimationStateMachine> CompileStateMachineFromDefinition(const AnimatorControllerDefinition& def);
        static AnimatorControllerDefinition ExtractDefinitionFromStateMachine(const AnimationStateMachine& sm);
        static std::string EncodeControllerToText(const AnimatorControllerDefinition& def);
        static bool DecodeControllerFromText(const std::string& text, AnimatorControllerDefinition& outDef, std::string* outError = nullptr);

        // --- Hex String Utilities ---
        static std::string HexEncode(const std::string& input);
        static std::string HexDecode(const std::string& hex);

        // --- Binary Cooked Geometry Baking & Loading ---
        static bool BakeSkeletalMeshGeometry(const std::string& binaryPath, const SkeletalMeshData& data, const std::string& skeletonSignature, std::string* outError = nullptr);
        static bool LoadSkeletalMeshGeometry(const std::string& binaryPath, SkeletalMeshData& outData, std::string* outSignature = nullptr, std::string* outError = nullptr);
    };

}

