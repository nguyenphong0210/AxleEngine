#pragma once

#include <filesystem>
#include <string>

namespace Axle {

    struct DragDropPayloadDecodeResult {
        bool Success = false;
        std::filesystem::path Path;
        std::string Error;
    };

    // Decodes an arbitrary drag-drop buffer (e.g. from Dear ImGui CONTENT_BROWSER_ITEM) into a path.
    // Handles narrow UTF-8 byte arrays, null-terminated or unterminated bounded buffers,
    // trims whitespace and enclosing quotes, and checks safety boundaries.
    DragDropPayloadDecodeResult DecodeAssetPayload(const void* data, size_t size);

    // Decodes and validates that the payload path has a supported shader extension (.hlsl, .shader, case-insensitive).
    DragDropPayloadDecodeResult DecodeShaderPayload(const void* data, size_t size);

    // Checks whether an extension corresponds to a supported shader (.hlsl, .shader, case-insensitive).
    bool IsSupportedShaderExtension(const std::filesystem::path& path);

} // namespace Axle
