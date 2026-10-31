#include <Axle/TextureDescriptor.hpp>
#include <fstream>
#include <sstream>

namespace Axle {

    std::string TextureDescriptor::Serialize(const TextureDescriptor& desc) {
        AssetDocument doc;
        doc.Kind = AssetKind::Texture;
        doc.Root = std::make_shared<AssetBlock>();
        doc.Root->Type = "Texture";

        doc.Root->SetString("name", desc.Name);
        doc.Root->SetString("source", desc.Source);
        doc.Root->SetString("type", desc.Type);
        doc.Root->SetString("filter", desc.Filter);
        doc.Root->SetString("wrapU", desc.WrapU);
        doc.Root->SetString("wrapV", desc.WrapV);
        doc.Root->SetBool("generateMips", desc.GenerateMips);
        doc.Root->SetUInt("maxResolution", desc.MaxResolution);
        doc.Root->SetString("compression", desc.Compression);
        doc.Root->SetFloat("exposure", desc.Exposure);
        doc.Root->SetBool("generatePrefilter", desc.GeneratePrefilter);

        return AssetDSL::SerializeDocument(doc);
    }

    bool TextureDescriptor::Deserialize(std::string_view source, TextureDescriptor& outDesc) {
        AssetDocument doc;
        std::string err;
        if (!AssetDSL::ParseSource(source, doc, &err) || !doc.IsValid()) {
            return false;
        }

        if (doc.Kind != AssetKind::Texture || !doc.Root) {
            return false;
        }

        const auto& r = *doc.Root;
        outDesc.Name = r.GetString("name", outDesc.Name);
        outDesc.Source = r.GetString("source", outDesc.Source);
        outDesc.Type = r.GetString("type", outDesc.Type);
        outDesc.Filter = r.GetString("filter", outDesc.Filter);
        outDesc.WrapU = r.GetString("wrapU", outDesc.WrapU);
        outDesc.WrapV = r.GetString("wrapV", outDesc.WrapV);
        outDesc.GenerateMips = r.GetBool("generateMips", outDesc.GenerateMips);
        outDesc.MaxResolution = r.GetUInt("maxResolution", outDesc.MaxResolution);
        outDesc.Compression = r.GetString("compression", outDesc.Compression);
        outDesc.Exposure = r.GetFloat("exposure", outDesc.Exposure);
        outDesc.GeneratePrefilter = r.GetBool("generatePrefilter", outDesc.GeneratePrefilter);

        return true;
    }

    bool TextureDescriptor::Load(const std::string& filepath, TextureDescriptor& outDesc) {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        std::stringstream ss;
        ss << file.rdbuf();
        return Deserialize(ss.str(), outDesc);
    }

    bool TextureDescriptor::Save(const std::string& filepath, const TextureDescriptor& desc) {
        std::ofstream file(filepath);
        if (!file.is_open()) return false;

        file << Serialize(desc);
        return true;
    }

} // namespace Axle
