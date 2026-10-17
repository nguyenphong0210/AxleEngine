#pragma once

struct GLFWwindow;

namespace Axle {

    class ApplicationUIContext {
    public:
        virtual ~ApplicationUIContext() = default;
        virtual void Init(GLFWwindow* window) = 0;
        virtual void Shutdown() = 0;
        virtual void Begin() = 0;
        virtual void End() = 0;
    };

}
