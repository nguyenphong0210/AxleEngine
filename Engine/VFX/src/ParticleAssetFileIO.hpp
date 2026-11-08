#pragma once
#include <Axle/ParticleEffectSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Path.hpp>

namespace Axle::Detail {
    inline bool WriteParticleAssetAtomically(const std::string& filePath, const std::string& text,
                                             std::vector<VFXDiagnostic>* diagnostics = nullptr) {
        std::string error;
        if (FileSystem::WriteIdentifiedTextFile(PathFromUtf8(filePath),text,&error)) return true;
        if (diagnostics)
            diagnostics->push_back({VFXDiagnosticSeverity::Error,"IO_WRITE_FAILED","",error});
        return false;
    }
} // namespace Axle::Detail
