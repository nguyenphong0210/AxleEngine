#pragma once
#include <Axle/RenderWorker.hpp>
namespace Axle::Detail {
    bool CaptureWorkerRgba8(RenderService& service, RenderCpuImage& image, RHI::RHICompletionToken& completion);
}
