#include <Axle/RHI/RHIECSStreamer.hpp>
#include <Axle/Log.hpp>

namespace Axle::RHI {

    RHIECSStreamResult RHIECSStreamer::StreamInstanceData(
        RHIStreamer& streamer,
        const std::vector<GPUInstanceData>& instances,
        const RHIMeshBatchKey& batchKey)
    {
        if (instances.empty() || !streamer.IsValid()) {
            return {};
        }

        // 1. Stream packed instance structures into dynamic Structured Buffer (SSBO)
        RHIStreamedBuffer instBuf = streamer.StreamBufferData(
            instances.data(),
            instances.size() * sizeof(GPUInstanceData),
            16 // 16-byte alignment
        );

        if (!instBuf.IsValid()) {
            AXLE_CORE_ERROR("RHIECSStreamer::StreamInstanceData failed to stream instance data!");
            return {};
        }

        // 2. Build GPU Indirect Draw Indexed Command
        GPUIndirectDrawIndexedCommand cmd = {};
        cmd.IndexCount = batchKey.IndexCount;
        cmd.InstanceCount = static_cast<uint32_t>(instances.size());
        cmd.FirstIndex = batchKey.FirstIndex;
        cmd.BaseVertex = batchKey.BaseVertex;
        cmd.FirstInstance = 0;

        // 3. Stream indirect argument command into dynamic buffer
        RHIStreamedBuffer indBuf = streamer.StreamBufferData(
            &cmd,
            sizeof(cmd),
            4 // 4-byte uint32 alignment
        );

        if (!indBuf.IsValid()) {
            AXLE_CORE_ERROR("RHIECSStreamer::StreamInstanceData failed to stream indirect draw command!");
            return {};
        }

        RHIECSStreamResult result;
        result.InstanceBuffer = instBuf;
        result.IndirectBuffer = indBuf;
        result.DrawCount = 1;
        result.TotalInstances = static_cast<uint32_t>(instances.size());
        return result;
    }

    RHIECSStreamResult RHIECSStreamer::StreamMultiBatches(
        RHIStreamer& streamer,
        const std::vector<RHIMultiMeshBatch>& batches)
    {
        if (batches.empty() || !streamer.IsValid()) {
            return {};
        }

        size_t totalCount = 0;
        for (const auto& b : batches) {
            totalCount += b.Instances.size();
        }

        if (totalCount == 0) {
            return {};
        }

        std::vector<GPUInstanceData> allInstances;
        allInstances.reserve(totalCount);

        std::vector<GPUIndirectDrawIndexedCommand> allCommands;
        allCommands.reserve(batches.size());

        for (const auto& b : batches) {
            if (b.Instances.empty()) continue;

            uint32_t startInstance = static_cast<uint32_t>(allInstances.size());
            allInstances.insert(allInstances.end(), b.Instances.begin(), b.Instances.end());

            GPUIndirectDrawIndexedCommand cmd = {};
            cmd.IndexCount = b.Key.IndexCount;
            cmd.InstanceCount = static_cast<uint32_t>(b.Instances.size());
            cmd.FirstIndex = b.Key.FirstIndex;
            cmd.BaseVertex = b.Key.BaseVertex;
            cmd.FirstInstance = startInstance;

            allCommands.push_back(cmd);
        }

        // 1. Stream all instance data into contiguous SSBO
        RHIStreamedBuffer instBuf = streamer.StreamBufferData(
            allInstances.data(),
            allInstances.size() * sizeof(GPUInstanceData),
            16
        );

        // 2. Stream all indirect draw commands into contiguous argument buffer
        RHIStreamedBuffer indBuf = streamer.StreamBufferData(
            allCommands.data(),
            allCommands.size() * sizeof(GPUIndirectDrawIndexedCommand),
            4
        );

        RHIECSStreamResult result;
        result.InstanceBuffer = instBuf;
        result.IndirectBuffer = indBuf;
        result.DrawCount = static_cast<uint32_t>(allCommands.size());
        result.TotalInstances = static_cast<uint32_t>(allInstances.size());
        return result;
    }

}
