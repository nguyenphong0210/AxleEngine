#include "VisualCapture.hpp"
#include "EditorLayer.hpp"
#include <Axle/Application.hpp>
#include <Axle/ApplicationUI.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Layer.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHICommandBuffer.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Axle {
namespace {

std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

uint32_t Crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 1u) ? ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t Adler32(const uint8_t* data, size_t size) {
    uint32_t s1 = 1u, s2 = 0u;
    for (size_t i = 0; i < size; ++i) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16u) | s1;
}

void AppendBE32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24u) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16u) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8u) & 0xFFu));
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
}

void AppendPngChunk(std::vector<uint8_t>& out, const char tag[4], const uint8_t* payload, size_t size) {
    AppendBE32(out, static_cast<uint32_t>(size));
    const size_t typeOffset = out.size();
    out.insert(out.end(), tag, tag + 4);
    if (size) out.insert(out.end(), payload, payload + size);
    const uint32_t crc = Crc32(out.data() + typeOffset, 4 + size);
    AppendBE32(out, crc);
}

bool WritePngRGBA8(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba8) {
    if (!width || !height || rgba8.size() < size_t(width) * height * 4u) return false;
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);

    const size_t stride = size_t(width) * 4u;
    const size_t rawSize = size_t(height) * (1u + stride);
    std::vector<uint8_t> raw(rawSize);
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* dst = raw.data() + size_t(y) * (1u + stride);
        dst[0] = 0;
        std::memcpy(dst + 1, rgba8.data() + size_t(y) * stride, stride);
    }

    std::vector<uint8_t> zlib;
    zlib.reserve(rawSize + (rawSize / 65535u + 1u) * 5u + 6u);
    zlib.push_back(0x78u);
    zlib.push_back(0x01u);
    size_t offset = 0;
    while (offset < rawSize) {
        const size_t block = std::min<size_t>(65535u, rawSize - offset);
        const bool last = (offset + block == rawSize);
        zlib.push_back(last ? 0x01u : 0x00u);
        zlib.push_back(static_cast<uint8_t>(block & 0xFFu));
        zlib.push_back(static_cast<uint8_t>((block >> 8u) & 0xFFu));
        const uint16_t nlen = static_cast<uint16_t>(~static_cast<uint16_t>(block));
        zlib.push_back(static_cast<uint8_t>(nlen & 0xFFu));
        zlib.push_back(static_cast<uint8_t>((nlen >> 8u) & 0xFFu));
        zlib.insert(zlib.end(), raw.data() + offset, raw.data() + offset + block);
        offset += block;
    }
    AppendBE32(zlib, Adler32(raw.data(), raw.size()));

    std::vector<uint8_t> png;
    static constexpr uint8_t kSig[8] = {137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};
    png.insert(png.end(), kSig, kSig + 8);
    std::vector<uint8_t> ihdr;
    AppendBE32(ihdr, width);
    AppendBE32(ihdr, height);
    ihdr.push_back(8u);  // bit depth
    ihdr.push_back(6u);  // RGBA
    ihdr.push_back(0u);  // compression
    ihdr.push_back(0u);  // filter
    ihdr.push_back(0u);  // interlace
    AppendPngChunk(png, "IHDR", ihdr.data(), ihdr.size());
    AppendPngChunk(png, "IDAT", zlib.data(), zlib.size());
    AppendPngChunk(png, "IEND", nullptr, 0);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    file.close();
    return file.good();
}

float HalfToFloat(uint16_t h) {
    const uint32_t sign = (h & 0x8000u) << 16u;
    const uint32_t exp = (h >> 10u) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else {
            uint32_t m = mant;
            int e = -14;
            while ((m & 0x0400u) == 0) { m <<= 1u; --e; }
            m &= 0x03FFu;
            bits = sign | (static_cast<uint32_t>(e + 127) << 23u) | (m << 13u);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13u);
    } else {
        bits = sign | (static_cast<uint32_t>(exp + 112u) << 23u) | (mant << 13u);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

uint8_t FloatToUNorm8(float v) {
    if (!std::isfinite(v)) return 255u;
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lround(clamped * 255.0f));
}

bool CaptureTextureRGBA8(RHI::RHITexture* texture, uint32_t& outW, uint32_t& outH,
                         std::vector<uint8_t>& outRgba8, std::string& outStats) {
    using namespace RHI;
    auto& device = RHIDevice::Get();
    if (!device.CanSubmit() || !texture || !texture->IsValid() || !texture->GetWidth() || !texture->GetHeight()) return false;
    if (!device.WaitIdle()) return false;
    const auto fmt = texture->GetFormat();
    const uint32_t bpp = (fmt == RHITextureFormat::RGBA16_FLOAT) ? 8u
                       : (fmt == RHITextureFormat::RGBA8_UNORM || fmt == RHITextureFormat::RGBA8_SRGB || fmt == RHITextureFormat::BGRA8_UNORM) ? 4u : 0u;
    if (!bpp) return false;

    outW = texture->GetWidth();
    outH = texture->GetHeight();
    const auto& limits = device.GetNRICore().GetDeviceDesc(*device.GetNRIDevice()).memoryAlignment;
    auto align = [](uint64_t n, uint32_t a) { return a ? (n + a - 1) / a * a : n; };
    const uint64_t rowPitch = align(uint64_t(outW) * bpp, limits.uploadBufferTextureRow);
    const uint64_t slicePitch = align(rowPitch * outH, limits.uploadBufferTextureSlice);
    if (!rowPitch || !slicePitch || rowPitch > (std::numeric_limits<uint32_t>::max)()) return false;

    RHIBufferDesc desc; desc.Size = slicePitch; desc.MemoryLocation = RHIMemoryLocation::HostReadback; desc.DebugName = "VisualViewportReadback";
    RHIBuffer readback; RHICommandBuffer cmd;
    if (!readback.Init(desc) || !cmd.Init() || !cmd.Begin()) return false;
    nri::TextureDataLayoutDesc layout{}; layout.rowPitch = static_cast<uint32_t>(rowPitch); layout.slicePitch = slicePitch;
    nri::TextureRegionDesc region{}; region.width = static_cast<nri::Dim_t>(outW); region.height = static_cast<nri::Dim_t>(outH); region.depth = 1;
    cmd.TransitionTexture(texture->GetNative(), RHIResourceLayout::ShaderResource, RHIResourceLayout::CopySource);
    cmd.ReadbackTextureToBuffer(*readback.GetNative(), layout, *texture->GetNative(), region);
    cmd.TransitionTexture(texture->GetNative(), RHIResourceLayout::CopySource, RHIResourceLayout::ShaderResource);
    if (!cmd.End() || !cmd.Submit()) return false;
    if (!device.WaitForCompletion(device.GetLastSubmittedToken())) return false;

    const auto* mapped = static_cast<const uint8_t*>(readback.Map());
    if (!mapped) return false;
    outRgba8.resize(size_t(outW) * outH * 4u);
    size_t nonFinite = 0, nonBlack = 0;
    float minVal = std::numeric_limits<float>::max(), maxVal = std::numeric_limits<float>::lowest();
    double sumLuma = 0.0;

    for (uint32_t y = 0; y < outH; ++y) {
        const uint8_t* srcRow = mapped + y * rowPitch;
        uint8_t* dstRow = outRgba8.data() + size_t(y) * outW * 4u;
        if (fmt == RHITextureFormat::RGBA16_FLOAT) {
            for (uint32_t x = 0; x < outW; ++x) {
                const uint16_t* h = reinterpret_cast<const uint16_t*>(srcRow + size_t(x) * 8u);
                const float r = HalfToFloat(h[0]), g = HalfToFloat(h[1]), b = HalfToFloat(h[2]);
                if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) ++nonFinite;
                else {
                    minVal = std::min({minVal, r, g, b});
                    maxVal = std::max({maxVal, r, g, b});
                    const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                    sumLuma += luma;
                    if (luma > 1e-4) ++nonBlack;
                }
                dstRow[x * 4u + 0u] = FloatToUNorm8(r);
                dstRow[x * 4u + 1u] = FloatToUNorm8(g);
                dstRow[x * 4u + 2u] = FloatToUNorm8(b);
                dstRow[x * 4u + 3u] = 255u;
            }
        } else {
            const bool isBGRA = (fmt == RHITextureFormat::BGRA8_UNORM);
            for (uint32_t x = 0; x < outW; ++x) {
                const uint8_t r = srcRow[x * 4u + (isBGRA ? 2u : 0u)];
                const uint8_t g = srcRow[x * 4u + 1u];
                const uint8_t b = srcRow[x * 4u + (isBGRA ? 0u : 2u)];
                dstRow[x * 4u + 0u] = r; dstRow[x * 4u + 1u] = g; dstRow[x * 4u + 2u] = b; dstRow[x * 4u + 3u] = 255u;
                const float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
                minVal = std::min({minVal, rf, gf, bf}); maxVal = std::max({maxVal, rf, gf, bf});
                const double luma = 0.2126 * rf + 0.7152 * gf + 0.0722 * bf;
                sumLuma += luma;
                if (r || g || b) ++nonBlack;
            }
        }
    }
    readback.Unmap();
    const double meanLuma = outW && outH ? sumLuma / (double(outW) * outH) : 0.0;
    std::ostringstream ss;
    ss << "size=" << outW << "x" << outH << " nonFinite=" << nonFinite << " nonBlack=" << nonBlack
       << " min=" << minVal << " max=" << maxVal << " meanLuma=" << meanLuma;
    outStats = ss.str();
    return nonFinite == 0;
}

class VisualCaptureLayer final : public Layer {
public:
    VisualCaptureLayer(std::shared_ptr<EditorLayer> editor, std::filesystem::path planPath, bool& completed)
        : Layer("VisualCaptureLayer"), m_Editor(std::move(editor)), m_PlanPath(std::move(planPath)),
          m_Completed(completed) {
        LoadPlan();
    }

    void OnUpdate(Timestep) override {
        ProcessImmediateCommands();
    }

    void OnImGuiRender() override {
        if (m_Completed) return;
        if (m_CaptureStatus) {
            if (*m_CaptureStatus == CaptureStatus::WriteFailed)
                throw std::runtime_error("VisualCapture: failed writing Editor PNG to " + m_CapturePath.string());
            if (*m_CaptureStatus == CaptureStatus::Pending) {
                if (--m_CaptureFramesRemaining == 0)
                    throw std::runtime_error("VisualCapture: no Editor frame readback for " + m_CapturePath.string());
                return;
            }
            m_CaptureStatus.reset();
        }
        if (m_WaitFrames > 0) {
            --m_WaitFrames;
            if (m_WaitFrames > 0) return;
        }
        // Check once we are ready to execute a frame-capture or assertion command
        while (m_Cursor < m_Commands.size()) {
            const auto& cmd = m_Commands[m_Cursor];
            if (!IsRenderPhaseCommand(cmd.Op)) break;
            ++m_Cursor;
            if (cmd.Op == "wait") {
                m_WaitFrames = std::max(1, std::stoi(cmd.Args));
                return;
            }
            if (cmd.Op == "capture_editor") {
                const auto outPath = ResolveOutput(cmd.Args);
                auto* rc = Application::Get().GetRenderContext();
                if (!rc) throw std::runtime_error("VisualCapture: missing ApplicationRenderContext");
                m_CapturePath = outPath;
                m_CaptureStatus = std::make_shared<CaptureStatus>(CaptureStatus::Pending);
                m_CaptureFramesRemaining = 8;
                rc->RequestNextFrameCapture([outPath, status = m_CaptureStatus](uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba8) {
                    if (!WritePngRGBA8(outPath, w, h, rgba8)) {
                        *status = CaptureStatus::WriteFailed;
                        return;
                    }
                    *status = CaptureStatus::Saved;
                    AXLE_INFO("VISUAL_CAPTURE_EDITOR path={0} size={1}x{2}", outPath.string(), w, h);
                });
                m_WaitFrames = 1;
                return;
            }
            if (cmd.Op == "capture_viewport") {
                const auto outPath = ResolveOutput(cmd.Args);
                auto* vp = m_Editor->GetRHIViewport();
                if (!vp || !vp->IsValid() || !vp->GetColorTexture())
                    throw std::runtime_error("VisualCapture: missing RHIViewport color texture");
                uint32_t w = 0, h = 0; std::vector<uint8_t> rgba8; std::string stats;
                if (!CaptureTextureRGBA8(vp->GetColorTexture(), w, h, rgba8, stats))
                    throw std::runtime_error("VisualCapture: CaptureTextureRGBA8 failed for " + outPath.string());
                if (!WritePngRGBA8(outPath, w, h, rgba8))
                    throw std::runtime_error("VisualCapture: failed writing Viewport PNG to " + outPath.string());
                AXLE_INFO("VISUAL_CAPTURE_VIEWPORT path={0} {1}", outPath.string(), stats);
                continue;
            }
            if (cmd.Op == "assert_no_errors") {
                size_t infos = 0, warns = 0, errs = 0;
                m_Editor->GetConsoleCounts(infos, warns, errs);
                const auto failedViews = m_Editor->GetFailedViewCount();
                const auto okViews = m_Editor->GetSuccessfulViewCount();
                AXLE_INFO("VISUAL_CONSOLE infos={0} warnings={1} errors={2} okViews={3} failedViews={4}",
                          infos, warns, errs, okViews, failedViews);
                if (errs != 0 || failedViews != 0 || okViews == 0)
                    throw std::runtime_error("VisualCapture: console errors or failed views detected");
                continue;
            }
            if (cmd.Op == "quit") {
                m_Completed = true;
                Application::Get().Close();
                return;
            }
        }
        if (m_Cursor >= m_Commands.size()) {
            m_Completed = true;
            Application::Get().Close();
        }
    }

private:
    enum class CaptureStatus { Pending, Saved, WriteFailed };
    struct Command { std::string Op, Args; };
    std::shared_ptr<EditorLayer> m_Editor;
    std::filesystem::path m_PlanPath, m_OutputBase;
    std::vector<Command> m_Commands;
    size_t m_Cursor = 0;
    int m_WaitFrames = 2;
    std::filesystem::path m_CapturePath;
    std::shared_ptr<CaptureStatus> m_CaptureStatus;
    unsigned m_CaptureFramesRemaining = 0;
    bool& m_Completed;

    static bool IsRenderPhaseCommand(const std::string& op) {
        return op == "wait" || op == "capture_editor" || op == "capture_viewport" || op == "assert_no_errors" || op == "quit";
    }

    void LoadPlan() {
        std::ifstream in(m_PlanPath);
        if (!in) throw std::runtime_error("VisualCapture: cannot open capture plan " + m_PlanPath.string());
        m_OutputBase = m_PlanPath.parent_path();
        std::string line;
        while (std::getline(in, line)) {
            const auto trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;
            const auto sp = trimmed.find_first_of(" \t");
            if (sp == std::string::npos) m_Commands.push_back({trimmed, ""});
            else m_Commands.push_back({trimmed.substr(0, sp), Trim(trimmed.substr(sp + 1))});
        }
    }

    std::filesystem::path ResolveOutput(const std::string& raw) const {
        std::filesystem::path p(raw);
        return p.is_absolute() ? p : (m_OutputBase / p);
    }

    void ProcessImmediateCommands() {
        if (m_Completed || m_WaitFrames > 0 || m_CaptureStatus) return;
        while (m_Cursor < m_Commands.size()) {
            const auto& cmd = m_Commands[m_Cursor];
            if (IsRenderPhaseCommand(cmd.Op)) break;
            ++m_Cursor;
            if (cmd.Op == "output_dir") {
                m_OutputBase = ResolveOutput(cmd.Args);
            } else if (cmd.Op == "scene") {
                auto p = std::filesystem::path(cmd.Args);
                if (!p.is_absolute()) {
                    auto relAsset = FileSystem::ResolveAssetPath(p);
                    if (std::filesystem::exists(relAsset)) p = relAsset;
                    else p = m_PlanPath.parent_path() / p;
                }
                m_Editor->OpenScene(p);
                m_WaitFrames = 2;
                return;
            } else if (cmd.Op == "camera" || cmd.Op == "camera_continuous") {
                std::istringstream ss(cmd.Args);
                float px = 0, py = 0, pz = 0, pitch = 0, yaw = 0;
                if (!(ss >> px >> py >> pz >> pitch >> yaw))
                    throw std::runtime_error("VisualCapture: invalid camera args: " + cmd.Args);
                m_Editor->SetCameraPose({px, py, pz}, {pitch, yaw, 0.0f}, cmd.Op == "camera");
            } else if (cmd.Op == "fx") {
                EditorLayer::RenderFXOverride fx;
                std::istringstream ss(cmd.Args);
                std::string tok;
                while (ss >> tok) {
                    const auto eq = tok.find('=');
                    if (eq == std::string::npos) continue;
                    const auto k = tok.substr(0, eq);
                    const auto v = tok.substr(eq + 1);
                    auto asBool = [&] { return v == "1" || v == "true" || v == "on"; };
                    if (k == "shadows") fx.Shadows = asBool();
                    else if (k == "ssr") fx.SSR = asBool();
                    else if (k == "planar") fx.Planar = asBool();
                    else if (k == "gtao") fx.GTAO = asBool();
                    else if (k == "contact") fx.Contact = asBool();
                    else if (k == "bloom") fx.Bloom = asBool();
                    else if (k == "autoexp") fx.AutoExposure = asBool();
                    else if (k == "ddgi") fx.DDGI = asBool();
                    else if (k == "taa") fx.TAA = asBool();
                    else if (k == "sss") fx.SSS = asBool();
                    else if (k == "msaa") fx.MSAA = std::stoi(v);
                    else if (k == "hdr") fx.HDRViewport = asBool();
                    else if (k == "popup") fx.PinRenderFXPopup = asBool();
                    else if (k == "hiz") fx.HiZLevel = std::stoi(v);
                    else if (k == "fog_quality") fx.FogQuality = std::stoi(v);
                    else if (k == "fog_temporal") fx.FogTemporalWeight = std::stof(v);
                    else if (k == "fog") fx.FogOverride = (v == "scene") ? -1 : (asBool() ? 1 : 0);
                }
                m_Editor->ApplyRenderFXOverride(fx);
            } else if (cmd.Op == "play") {
                m_Editor->OnScenePlay();
            } else if (cmd.Op == "pause") {
                m_Editor->OnScenePause();
            } else if (cmd.Op == "stop") {
                m_Editor->OnSceneStop();
            } else if (cmd.Op == "eject") {
                m_Editor->SetGameCameraEjected(cmd.Args == "1" || cmd.Args == "true");
            } else if (cmd.Op == "select_entity") {
                if (!m_Editor->SelectEntityByName(cmd.Args))
                    throw std::runtime_error("VisualCapture: entity not found: " + cmd.Args);
            } else if (cmd.Op == "select_asset") {
                if (!m_Editor->SelectAssetByPath(cmd.Args))
                    throw std::runtime_error("VisualCapture: asset not found: " + cmd.Args);
            } else if (cmd.Op == "preview_mesh") {
                if (cmd.Args == "sphere") m_Editor->SetPreviewMesh(PreviewMeshType::Sphere);
                else if (cmd.Args == "cube") m_Editor->SetPreviewMesh(PreviewMeshType::Cube);
                else if (cmd.Args == "cylinder") m_Editor->SetPreviewMesh(PreviewMeshType::Cylinder);
                else if (cmd.Args == "plane") m_Editor->SetPreviewMesh(PreviewMeshType::Plane);
            } else if (cmd.Op == "preview_sun") {
                std::istringstream ss(cmd.Args);
                float az = 45.0f, el = 35.264f;
                if (ss >> az >> el) m_Editor->SetPreviewSun(az, el);
            } else if (cmd.Op == "focus") {
                m_Editor->RequestFocusWindow(cmd.Args);
            } else if (cmd.Op == "scroll_inspector") {
                m_Editor->RequestInspectorScroll(std::stof(cmd.Args));
            } else if (cmd.Op == "telemetry") {
                m_Editor->SetTelemetryVisible(cmd.Args == "1" || cmd.Args == "true");
            } else {
                throw std::runtime_error("VisualCapture: unknown command: " + cmd.Op);
            }
        }
    }
};

} // namespace

std::shared_ptr<Layer> CreateEditorVisualCaptureLayer(
    std::shared_ptr<EditorLayer> editor,
    const std::filesystem::path& planPath,
    bool& completed) {
    return std::make_shared<VisualCaptureLayer>(std::move(editor), planPath, completed);
}

} // namespace Axle
