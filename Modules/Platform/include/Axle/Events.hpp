#pragma once
#include <Axle/Types.hpp>
#include <Axle/KeyCodes.hpp>

namespace Axle {

    // Window events
    struct WindowResizeEvent {
        u32 Width;
        u32 Height;
    };

    struct WindowCloseEvent {};

    // Keyboard events
    struct KeyPressedEvent {
        KeyCode Key;
        bool IsRepeat;
    };

    struct KeyReleasedEvent {
        KeyCode Key;
    };

    // Mouse events
    struct MouseMovedEvent {
        f32 X;
        f32 Y;
    };

    struct MouseScrolledEvent {
        f32 XOffset;
        f32 YOffset;
    };

    struct MouseButtonPressedEvent {
        MouseCode Button;
    };

    struct MouseButtonReleasedEvent {
        MouseCode Button;
    };

}