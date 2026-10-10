#pragma once
#include <Axle/Types.hpp>
#include <Axle/Vehicle.hpp>
#include <Axle/PhysicsScene.hpp>

namespace Axle {

    class VehicleTunerPanel {
    public:
        VehicleTunerPanel() = default;
        explicit VehicleTunerPanel(Vehicle* vehicle, PhysicsScene* physicsScene = nullptr);
        ~VehicleTunerPanel() = default;

        void SetContext(Vehicle* vehicle, PhysicsScene* physicsScene = nullptr);

        void OnImGuiRender(bool* p_open = nullptr);

    private:
        Vehicle* m_Vehicle = nullptr;
        PhysicsScene* m_PhysicsScene = nullptr;
    };

}
