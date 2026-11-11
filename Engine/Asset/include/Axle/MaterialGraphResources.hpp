#pragma once
#include <Axle/MaterialGraphHLSL.hpp>
#include <Axle/DecodedImage.hpp>
#include <span>

namespace Axle {
    struct MaterialGraphDependency {
        uint64_t NodeID = 0;
        std::string Path;
        bool Descriptor = false;
        bool Available = false;
        uint64_t Revision = 0;
    };
    struct MaterialGraphImage {
        MaterialHLSLTexture Binding;
        ImageSourceData Source;
        std::string Key;
        bool UsedFallback = false;
    };
    struct MaterialGraphResourceSet {
        std::vector<MaterialGraphImage> Images;
        std::vector<MaterialGraphDependency> Dependencies;
        std::vector<MaterialGraphDiagnostic> Warnings;
        std::string Key;
    };
    class MaterialGraphResources {
    public:
        // Strict project-root/VFS references; never discovers companion descriptors
        // or borrows another source's last-good image. Graph sampling is authoritative.
        // null liveSymbols resolves all lowered textures; an empty span resolves none.
        // Outputs are transactional. Only authored I/O/decode fallbacks are allowed;
        // invalid descriptors, paths and interpretation contracts always fail.
        static bool Resolve(const MaterialGraphDocument& document, const MaterialHLSLSource& graph,
                            MaterialGraphResourceSet& resources, MaterialGraphDiagnostic* error = nullptr,
                            const std::span<const std::string>* liveSymbols = nullptr);
    };
}
