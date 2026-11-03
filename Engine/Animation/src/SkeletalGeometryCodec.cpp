#include <Axle/AnimationAssetCodec.hpp>
#include <Axle/FileSystem.hpp>
#include <bit>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Axle {
namespace {
    constexpr uint32_t Magic = 0x424D5341;
    struct Writer {
        std::vector<uint8_t> Bytes;
        void UInt(uint32_t v) { for (int i = 0; i < 4; ++i) Bytes.push_back(uint8_t(v >> (i * 8))); }
        void Float(float v) { UInt(std::bit_cast<uint32_t>(v)); }
        void String(const std::string& v) { UInt(uint32_t(v.size())); Bytes.insert(Bytes.end(), v.begin(), v.end()); }
    };
    struct Reader {
        std::span<const uint8_t> Bytes; size_t Offset = 0; bool Good = true;
        size_t Left() const { return Bytes.size() - Offset; }
        uint32_t UInt() {
            if (Left() < 4) { Good = false; return 0; }
            uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(Bytes[Offset++]) << (i * 8);
            return v;
        }
        float Float() { return std::bit_cast<float>(UInt()); }
        std::string String() {
            const auto n = UInt();
            if (!Good || n > 1048576 || n > Left()) { Good = false; return {}; }
            std::string result(reinterpret_cast<const char*>(Bytes.data() + Offset), n); Offset += n; return result;
        }
    };
    void Bounds(const SkeletalMeshData& data, glm::vec3& lo, glm::vec3& hi) {
        lo = glm::vec3(std::numeric_limits<float>::max()); hi = -lo;
        for (const auto& vertex : data.Vertices) { lo = glm::min(lo, vertex.Position); hi = glm::max(hi, vertex.Position); }
    }
}

bool AnimationAssetCodec::EncodeGeometry(const SkeletalMeshData& data, const std::string& signature,
                                        std::vector<uint8_t>& bytes, std::string* diagnostic) {
    if (!data.Validate(diagnostic)) return false;
    if (signature.empty() || signature.size() > 1024 || data.Name.size() > 1048576 || data.Submeshes.size() > 4096 || data.MaterialSlots.size() > 4096) {
        if (diagnostic) *diagnostic = "Invalid geometry metadata limits"; return false;
    }
    for (const auto& slot : data.MaterialSlots) if (slot.size() > 1048576) return false;
    Writer writer; writer.UInt(Magic); writer.UInt(2); writer.String(signature); writer.String(data.Name);
    writer.UInt(uint32_t(data.Vertices.size())); writer.UInt(uint32_t(data.Indices.size()));
    writer.UInt(uint32_t(data.Submeshes.size())); writer.UInt(uint32_t(data.MaterialSlots.size()));
    glm::vec3 lo, hi; Bounds(data, lo, hi);
    for (int i = 0; i < 3; ++i) writer.Float(lo[i]);
    for (int i = 0; i < 3; ++i) writer.Float(hi[i]);
    for (const auto& vertex : data.Vertices) {
        for (int i = 0; i < 3; ++i) writer.Float(vertex.Position[i]);
        for (int i = 0; i < 3; ++i) writer.Float(vertex.Normal[i]);
        for (int i = 0; i < 2; ++i) writer.Float(vertex.TexCoords[i]);
        for (int i = 0; i < 4; ++i) writer.UInt(std::bit_cast<uint32_t>(vertex.BoneIDs[i]));
        for (int i = 0; i < 4; ++i) writer.Float(vertex.Weights[i]);
    }
    for (auto index : data.Indices) writer.UInt(index);
    for (const auto& range : data.Submeshes) { writer.UInt(range.FirstIndex); writer.UInt(range.IndexCount); writer.UInt(range.MaterialSlot); }
    for (const auto& slot : data.MaterialSlots) writer.String(slot);
    bytes = std::move(writer.Bytes); return true;
}

bool AnimationAssetCodec::DecodeGeometry(std::span<const uint8_t> bytes, SkeletalMeshData& data,
                                        std::string* signature, std::string* diagnostic,
                                        const std::string& expectedSignature) {
    const auto fail = [&](const char* text) { if (diagnostic) *diagnostic = text; return false; };
    Reader reader{bytes};
    if (reader.UInt() != Magic) return fail("Invalid geometry magic");
    const auto version = reader.UInt();
    if (version != 1 && version != 2) return fail("Unsupported geometry schema");
    const auto rig = reader.String();
    if (!reader.Good || rig.empty() || rig.size() > 1024 || (!expectedSignature.empty() && rig != expectedSignature))
        return fail("Missing or incompatible geometry skeleton signature");
    SkeletalMeshData candidate;
    if (version == 2) candidate.Name = reader.String();
    const auto vertices = reader.UInt(), indices = reader.UInt();
    const auto submeshes = version == 2 ? reader.UInt() : 0, slots = version == 2 ? reader.UInt() : 0;
    if (!reader.Good || vertices == 0 || vertices > 10000000 || indices == 0 || indices > 30000000 || indices % 3 || submeshes > 4096 || slots > 4096)
        return fail("Invalid geometry counts");
    glm::vec3 lo, hi;
    for (int i = 0; i < 3; ++i) lo[i] = reader.Float();
    for (int i = 0; i < 3; ++i) hi[i] = reader.Float();
    for (int i = 0; i < 3; ++i) if (!std::isfinite(lo[i]) || !std::isfinite(hi[i]) || lo[i] > hi[i]) return fail("Invalid geometry bounds");
    const uint64_t minimum = uint64_t(vertices) * 64 + uint64_t(indices) * 4 + uint64_t(submeshes) * 12 + uint64_t(slots) * 4;
    if (!reader.Good || minimum > reader.Left()) return fail("Truncated geometry payload");
    candidate.Vertices.resize(vertices); candidate.Indices.resize(indices);
    for (auto& vertex : candidate.Vertices) {
        for (int i = 0; i < 3; ++i) vertex.Position[i] = reader.Float();
        for (int i = 0; i < 3; ++i) vertex.Normal[i] = reader.Float();
        for (int i = 0; i < 2; ++i) vertex.TexCoords[i] = reader.Float();
        for (int i = 0; i < 4; ++i) vertex.BoneIDs[i] = std::bit_cast<int32_t>(reader.UInt());
        for (int i = 0; i < 4; ++i) vertex.Weights[i] = reader.Float();
    }
    for (auto& index : candidate.Indices) index = reader.UInt();
    for (uint32_t i = 0; i < submeshes; ++i) candidate.Submeshes.push_back({reader.UInt(), reader.UInt(), reader.UInt()});
    for (uint32_t i = 0; i < slots; ++i) candidate.MaterialSlots.push_back(reader.String());
    if (!reader.Good || reader.Left() != 0) return fail("Invalid or trailing geometry payload");
    if (!candidate.Validate(diagnostic)) return false;
    Bounds(candidate, candidate.BoundsMin, candidate.BoundsMax);
    for (int i = 0; i < 3; ++i) {
        const float tolerance = 1e-5f * std::max({1.0f, std::abs(lo[i]), std::abs(hi[i])});
        if (std::abs(lo[i] - candidate.BoundsMin[i]) > tolerance || std::abs(hi[i] - candidate.BoundsMax[i]) > tolerance)
            return fail("Geometry bounds do not match vertices");
    }
    data = std::move(candidate); if (signature) *signature = rig; return true;
}

bool AnimationAssetCodec::BakeSkeletalMeshGeometry(const std::string& path, const SkeletalMeshData& data,
                                                 const std::string& signature, std::string* diagnostic) {
    std::vector<uint8_t> bytes;
    return EncodeGeometry(data, signature, bytes, diagnostic) && WriteBinaryFileAtomically(path, bytes.data(), bytes.size(), diagnostic);
}
bool AnimationAssetCodec::LoadSkeletalMeshGeometry(const std::string& path, SkeletalMeshData& data,
                                                 std::string* signature, std::string* diagnostic) {
    const auto bytes = FileSystem::ReadBinaryFile(path);
    return DecodeGeometry(bytes, data, signature, diagnostic);
}
}
