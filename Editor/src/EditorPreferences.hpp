#pragma once

#include <string>

namespace Axle {
    class Window;

    class EditorPreferences {
    public:
        static bool LoadVSync();

        void Open() { m_Open = true; }
        void Draw(Window& window);

    private:
        bool m_Open = false;
        std::string m_SaveError;
    };
}
