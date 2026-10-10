#include <Axle/LayerStack.hpp>
#include <algorithm>

namespace Axle {

    LayerStack::~LayerStack() {
        Clear();
    }

    void LayerStack::PushLayer(std::shared_ptr<Layer> layer) {
        m_Layers.emplace_back(layer);
        layer->OnAttach();
    }

    void LayerStack::PopLayer(std::shared_ptr<Layer> layer) {
        const auto it = std::find(m_Layers.begin(), m_Layers.end(), layer);
        if (it != m_Layers.end()) {
            (*it)->OnDetach();
            m_Layers.erase(it);
        }
    }

    void LayerStack::Clear() {
        for (auto& layer : m_Layers) {
            layer->OnDetach();
        }
        m_Layers.clear();
    }

}
