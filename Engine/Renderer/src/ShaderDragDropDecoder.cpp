#include <Axle/ShaderDragDropDecoder.hpp>
#include <algorithm>
#include <cctype>

namespace Axle {

    namespace {
        std::string ToLower(std::string str) {
            std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return str;
        }

        std::string Trim(const std::string& str) {
            size_t start = str.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";
            size_t end = str.find_last_not_of(" \t\r\n");
            std::string res = str.substr(start, end - start + 1);
            if (res.size() >= 2 && ((res.front() == '"' && res.back() == '"') || (res.front() == '\'' && res.back() == '\''))) {
                res = res.substr(1, res.size() - 2);
                size_t innerStart = res.find_first_not_of(" \t\r\n");
                if (innerStart == std::string::npos) return "";
                size_t innerEnd = res.find_last_not_of(" \t\r\n");
                res = res.substr(innerStart, innerEnd - innerStart + 1);
            }
            return res;
        }
    }

    bool IsSupportedShaderExtension(const std::filesystem::path& path) {
        std::string ext = ToLower(path.extension().string());
        return ext == ".hlsl" || ext == ".shader";
    }

    DragDropPayloadDecodeResult DecodeAssetPayload(const void* data, size_t size) {
        DragDropPayloadDecodeResult result;
        if (!data || size == 0) {
            result.Error = "Drag-drop payload is empty.";
            return result;
        }

        constexpr size_t kMaxPathBytes = 4096;
        if (size > kMaxPathBytes) {
            result.Error = "Payload exceeds maximum allowable path length.";
            return result;
        }

        const char* bytePtr = static_cast<const char*>(data);
        size_t len = 0;
        while (len < size && bytePtr[len] != '\0') {
            len++;
        }

        if (len == 0) {
            result.Error = "Payload string contains zero characters.";
            return result;
        }

        std::string rawStr(bytePtr, len);
        std::string cleanStr = Trim(rawStr);
        if (cleanStr.empty()) {
            result.Error = "Payload path is blank after trimming.";
            return result;
        }

        // Construct std::filesystem::path from UTF-8 string
#if defined(__cpp_lib_char8_t)
        result.Path = std::filesystem::path(reinterpret_cast<const char8_t*>(cleanStr.data()),
                                            reinterpret_cast<const char8_t*>(cleanStr.data() + cleanStr.size()));
#else
        result.Path = std::filesystem::u8path(cleanStr);
#endif
        result.Success = true;
        return result;
    }

    DragDropPayloadDecodeResult DecodeShaderPayload(const void* data, size_t size) {
        auto res = DecodeAssetPayload(data, size);
        if (!res.Success) return res;

        if (!IsSupportedShaderExtension(res.Path)) {
            res.Success = false;
            std::string ext = res.Path.extension().string();
            if (ext.empty()) {
                res.Error = "Dropped file has no extension. Expected .hlsl or .shader.";
            } else {
                res.Error = "Unsupported shader extension '" + ext + "'. Expected .hlsl or .shader.";
            }
            return res;
        }

        return res;
    }

} // namespace Axle
