#pragma once
#include <Axle/EnvironmentBake.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <memory>
#include <string>
#include <vector>
namespace Axle {
    struct RHIEnvironmentGeneration {
        std::shared_ptr<RHI::RHITexture> Radiance, Irradiance, Prefilter, BRDF;
        std::string Key, Request;
        uint64_t Sequence=0, Used=0;
        bool IsValid() const;
    };
    // Owner-thread preparation, before recording scene draws. The bounded cache
    // and exact native descriptor leases retain generations through submissions.
    class RHIEnvironment {
    public:
        bool SetBakeSettings(const EnvironmentBakeSettings&,std::string* error=nullptr);
        bool Prepare(const RenderSkyboxData&);
        void Clear();
        std::shared_ptr<const RHIEnvironmentGeneration> Ready() const {return m_Selected;}
        const std::string& Diagnostic() const {return m_Diagnostic;}
        float SourceExposure() const {return m_SourceExposure;}
        uint64_t BakeCount() const {return m_BakeCount;}
        uint64_t UploadCount() const {return m_UploadCount;}
        size_t CacheSize() const {return m_Cache.size();}
    private:
        bool Fallback(const std::string& request,const std::string& message);
        EnvironmentBakeSettings m_Settings;
        std::vector<std::shared_ptr<RHIEnvironmentGeneration>> m_Cache;
        std::shared_ptr<RHIEnvironmentGeneration> m_Selected;
        std::string m_Diagnostic, m_FailedBakeKey, m_FailedBakeDiagnostic;
        uint64_t m_Clock=0,m_Sequence=0,m_BakeCount=0,m_UploadCount=0;
        float m_SourceExposure=1;
    };
}
