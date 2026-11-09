#include <Axle/RHI/RHIParallelRecording.hpp>

#include <algorithm>
#include <functional>
#include <unordered_map>

namespace Axle::RHI {

    RHIParallelRecordingProbeResult ProbeParallelRecording(const RHIDeviceCapabilities& capabilities) {
        RHIParallelRecordingProbeResult result;
        if (capabilities.ActiveBackend == RHIType::OpenGL || capabilities.ActiveBackend == RHIType::None) {
            result.Diagnostic = "native parallel recording is unavailable without a modern RHI backend";
            return result;
        }
        result.Mode = RHIParallelRecordingMode::CpuPreparation;
        result.Diagnostic = "native allocator isolation and merge are not enabled: pinned NRI exposes primary command buffers and batch submit, but no portable secondary execute/merge contract or completed validation probe";
        return result;
    }

    bool RHIRecordingPlan::Validate(std::string* diagnostic) const {
        std::unordered_map<uint32_t, size_t> index;
        for (size_t i = 0; i < Passes.size(); ++i) {
            if (!index.emplace(Passes[i].Id, i).second) {
                if (diagnostic) *diagnostic = "duplicate recording pass id";
                return false;
            }
        }
        std::vector<uint8_t> visiting(Passes.size(), 0);
        std::function<bool(size_t)> visit = [&](size_t i) {
            if (visiting[i] == 1) return false;
            if (visiting[i] == 2) return true;
            visiting[i] = 1;
            for (const auto dependency : Passes[i].Dependencies) {
                const auto found = index.find(dependency);
                if (found == index.end() || !visit(found->second)) return false;
            }
            visiting[i] = 2;
            return true;
        };
        for (size_t i = 0; i < Passes.size(); ++i) {
            if (!visit(i)) {
                if (diagnostic) *diagnostic = "missing dependency or cycle in recording plan";
                return false;
            }
        }
        return true;
    }

    std::vector<std::vector<uint32_t>> RHIRecordingPlan::BuildLevels() const {
        if (!Validate()) return {};
        std::unordered_map<uint32_t, size_t> index;
        for (size_t i = 0; i < Passes.size(); ++i) {
            if (!index.emplace(Passes[i].Id, i).second) return {};
        }
        std::vector<uint32_t> level(Passes.size(), 0);
        std::vector<uint8_t> visiting(Passes.size(), 0);
        std::function<bool(size_t)> visit = [&](size_t i) {
            if (visiting[i] == 1) return false;
            if (visiting[i] == 2) return true;
            visiting[i] = 1;
            for (const auto dependency : Passes[i].Dependencies) {
                const auto found = index.find(dependency);
                if (found == index.end() || !visit(found->second)) return false;
                level[i] = std::max(level[i], level[found->second] + 1);
            }
            visiting[i] = 2;
            return true;
        };
        for (size_t i = 0; i < Passes.size(); ++i) if (!visit(i)) return {};
        uint32_t maxLevel = 0;
        for (const auto value : level) maxLevel = std::max(maxLevel, value);
        std::vector<std::vector<uint32_t>> levels(maxLevel + (Passes.empty() ? 0 : 1));
        for (size_t i = 0; i < Passes.size(); ++i) levels[level[i]].push_back(Passes[i].Id);
        for (auto& levelPasses : levels) std::sort(levelPasses.begin(), levelPasses.end());
        return levels;
    }

    std::vector<RHIRecordingBatch> BuildCpuPreparationBatches(const RHIRecordingPlan& plan,
                                                               uint32_t workerCount) {
        const auto levels = plan.BuildLevels();
        if (levels.empty()) return {};
        std::unordered_map<uint32_t, bool> threadSafe;
        for (const auto& pass : plan.Passes) threadSafe[pass.Id] = pass.ThreadSafe;
        std::vector<RHIRecordingBatch> batches;
        for (uint32_t level = 0; level < levels.size(); ++level) {
            std::vector<uint32_t> safe;
            auto flushSafe = [&] {
                if (safe.empty()) return;
                const uint32_t partitions = workerCount ? std::min<uint32_t>(workerCount, static_cast<uint32_t>(safe.size())) : 1;
                const uint32_t chunk = (static_cast<uint32_t>(safe.size()) + partitions - 1) / partitions;
                for (uint32_t offset = 0; offset < safe.size(); offset += chunk) {
                    RHIRecordingBatch batch; batch.Level = level;
                    const auto end = std::min<uint32_t>(offset + chunk, static_cast<uint32_t>(safe.size()));
                    batch.PassIds.insert(batch.PassIds.end(), safe.begin() + offset, safe.begin() + end);
                    batches.push_back(std::move(batch));
                }
                safe.clear();
            };
            for (const auto id : levels[level]) {
                if (threadSafe[id]) safe.push_back(id);
                else {
                    flushSafe();
                    RHIRecordingBatch batch; batch.Level = level; batch.PassIds.push_back(id);
                    batches.push_back(std::move(batch));
                }
            }
            flushSafe();
        }
        return batches;
    }

}
