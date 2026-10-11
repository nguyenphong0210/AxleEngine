#pragma once
#include <string>

namespace Axle {

    class FileDialogs {
    public:
        // Returns empty string if cancelled
        // filter format: "Axle Scene (*.axlescene)\0*.axlescene\0All Files (*.*)\0*.*\0"
        static std::string OpenFile(const char* filter, const char* defaultPath = nullptr);
        static std::string SaveFile(const char* filter, const char* defaultPath = nullptr, const char* defaultExt = nullptr);
    };

}
