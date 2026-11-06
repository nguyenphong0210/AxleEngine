#pragma once
#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <string>
#include <vector>

namespace Axle {

    class Layer {
    public:
        Layer(const std::string& name = "Layer") : m_DebugName(name) {}
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(Timestep ts) {}
        virtual void OnImGuiRender() {}
        virtual void OnWindowResize(u32 width, u32 height) {}
        virtual void OnFileDrop(const std::vector<std::string>& filepaths) {}

        const std::string& GetName() const { return m_DebugName; }

    protected:
        std::string m_DebugName;
    };

}
