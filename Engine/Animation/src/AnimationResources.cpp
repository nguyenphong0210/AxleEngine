#include <Axle/AnimationResources.hpp>
#include <Axle/AnimationAssetCodec.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/SkeletalMeshImporter.hpp>

namespace Axle {
std::string ResolveAnimationReference(const std::string& reference, const std::string& owner) {
    if (reference.empty() || reference.starts_with("inline:") || reference.starts_with("procedural:")) return reference;
    const std::filesystem::path path(reference);
    // Child dependencies are relative to their owning asset, then to the project asset root.
    if (path.is_relative() && !owner.empty()) {
        const auto relative = (std::filesystem::path(owner).parent_path() / path).lexically_normal();
        if (FileSystem::Exists(relative)) return relative.generic_string();
    }
    if (FileSystem::Exists(path)) return path.generic_string();
    return FileSystem::ResolveAssetPath(path).generic_string();
}
std::shared_ptr<Skeleton> BuildAnimationSkeleton(const SkeletonDefinition& definition, std::string* diagnostic) {
    std::vector<std::string> errors;
    if (!definition.Validate(&errors) || definition.Bones.size() > 128) {
        if (diagnostic) *diagnostic = errors.empty() ? "Rig exceeds the 128-joint limit" : errors.front();
        return nullptr;
    }
    auto rig = std::make_shared<Skeleton>(definition.Name);
    for (const auto& bone : definition.Bones) rig->AddBone(bone);
    if (!rig->RebuildDerivedData(&errors)) { if (diagnostic) *diagnostic = errors.front(); return nullptr; }
    return rig;
}
std::string EncodeAnimationRigSnapshot(const SkeletalMeshData& geometry, std::string* diagnostic) {
    if (!geometry.SkeletonRef) { if (diagnostic) *diagnostic = "Geometry snapshot requires a rig"; return {}; }
    SkeletonDefinition skeleton; skeleton.Id = "snapshot-rig"; skeleton.Name = geometry.SkeletonRef->GetName();
    skeleton.Bones = geometry.SkeletonRef->GetBones(); skeleton.CompatibilitySignature = SkeletonDefinition::ComputeSignature(skeleton.Bones);
    AssetDocument rig; std::vector<uint8_t> bytes;
    if (!AnimationAssetCodec::EncodeSkeleton(skeleton, rig) || !AnimationAssetCodec::EncodeGeometry(geometry, skeleton.CompatibilitySignature, bytes, diagnostic)) return {};
    AssetDocument document; document.Kind = AssetKind::SkeletalMesh; document.Root = std::make_shared<AssetBlock>();
    document.Root->Type = "SkeletalRigSnapshot"; document.Root->Children.push_back(rig.Root);
    document.Root->SetString("geometry", AnimationAssetCodec::HexEncode(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
    return AssetDSL::SerializeDocument(document);
}
std::shared_ptr<SkeletalMesh> DecodeAnimationRigSnapshot(const std::string& text, std::string* diagnostic) {
    AssetDocument document;
    if (!AssetDSL::ParseSource(text, document, diagnostic) || !document.Root || document.Root->Type != "SkeletalRigSnapshot" || document.Root->Children.size() != 1) return nullptr;
    AssetDocument nested; nested.Kind = AssetKind::Skeleton; nested.Root = document.Root->Children[0];
    SkeletonDefinition definition;
    if (!AnimationAssetCodec::DecodeSkeleton(nested, definition, diagnostic)) return nullptr;
    auto rig = BuildAnimationSkeleton(definition, diagnostic); if (!rig) return nullptr;
    const auto binary = AnimationAssetCodec::HexDecode(document.Root->GetString("geometry"));
    const auto bytes = std::span(reinterpret_cast<const uint8_t*>(binary.data()), binary.size());
    SkeletalMeshData geometry;
    if (!AnimationAssetCodec::DecodeGeometry(bytes, geometry, nullptr, diagnostic, SkeletonDefinition::ComputeSignature(definition.Bones))) return nullptr;
    geometry.SkeletonRef = rig; if (!geometry.Validate(diagnostic)) return nullptr;
    return std::make_shared<SkeletalMesh>(std::move(geometry));
}
std::shared_ptr<SkeletalMesh> LoadAnimationMesh(const std::string& reference, const std::string& fallback,
                                              const std::string& owner, std::string* diagnostic) {
    const auto path = ResolveAnimationReference(reference, owner);
    if (reference == "procedural://biped") {
        SkeletalMeshData geometry; std::shared_ptr<Skeleton> rig; std::vector<std::shared_ptr<AnimationClip>> clips;
        if (SkeletalMeshImporter::CreateProceduralBiped(geometry, rig, clips)) return std::make_shared<SkeletalMesh>(std::move(geometry));
    } else if (!path.empty() && FileSystem::Exists(path)) {
        if (std::filesystem::path(path).extension() == ".axleasset") {
            SkeletalMeshDefinition definition;
            if (!AnimationAssetCodec::LoadSkeletalMesh(path, definition, diagnostic)) return nullptr;
            SkeletonDefinition skeleton;
            if (!AnimationAssetCodec::LoadSkeleton(ResolveAnimationReference(definition.SkeletonRef, path), skeleton, diagnostic)) return nullptr;
            auto rig = BuildAnimationSkeleton(skeleton, diagnostic); if (!rig) return nullptr;
            SkeletalMeshData geometry;
            const auto bytes = FileSystem::ReadBinaryFile(ResolveAnimationReference(definition.GeometryRef, path));
            if (!AnimationAssetCodec::DecodeGeometry(bytes, geometry, nullptr, diagnostic, SkeletonDefinition::ComputeSignature(skeleton.Bones))) return nullptr;
            geometry.SkeletonRef = rig;
            if (!definition.MaterialSlots.empty()) geometry.MaterialSlots = definition.MaterialSlots;
            if (!geometry.Validate(diagnostic)) return nullptr;
            return std::make_shared<SkeletalMesh>(std::move(geometry));
        }
        SkeletalMeshData geometry; std::shared_ptr<Skeleton> rig; std::vector<std::shared_ptr<AnimationClip>> clips;
        if (!SkeletalMeshImporter::Load(path, geometry, rig, clips, diagnostic) || !geometry.Validate(diagnostic)) return nullptr;
        return std::make_shared<SkeletalMesh>(std::move(geometry));
    }
    if (!fallback.empty()) return DecodeAnimationRigSnapshot(fallback, diagnostic);
    if (diagnostic) *diagnostic = "Missing skeletal mesh: " + reference;
    return nullptr;
}
std::shared_ptr<Animator> LoadAnimationAnimator(const std::string& reference, const std::string& fallback,
                                              bool unique, std::shared_ptr<Skeleton> rig,
                                              const std::string& owner, std::string* diagnostic) {
    AnimatorControllerDefinition definition;
    auto definitionOwner = owner;
    const auto path = ResolveAnimationReference(reference, owner);
    if (!unique && !reference.empty() && FileSystem::Exists(path)) {
        if (!AnimationAssetCodec::LoadAnimatorController(path, definition, diagnostic)) return nullptr;
        definitionOwner = path;
    } else if (!fallback.empty()) {
        if (!AnimationAssetCodec::DecodeControllerFromText(fallback, definition, diagnostic)) return nullptr;
    } else { if (diagnostic) *diagnostic = "No controller definition or inline fallback"; return nullptr; }
    auto machine = AnimationAssetCodec::CompileStateMachineFromDefinition(definition, rig, definitionOwner, diagnostic);
    if (!machine) return nullptr;
    if (!rig) rig = machine->GetResourceRig();
    if (!rig) return nullptr;
    auto animator = std::make_shared<Animator>(rig); animator->SetStateMachine(machine); animator->Update(0); return animator;
}
}
