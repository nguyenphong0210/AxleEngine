#pragma once
#include "VehicleComponents.hpp"
#include <string>
#include <string_view>

namespace AxleVehicle {

bool SerializeChassis(const VehicleChassisComponent& comp, std::string& out);
bool DeserializeChassis(std::string_view text, VehicleChassisComponent& comp);

bool SerializeWheel(const VehicleWheelComponent& comp, std::string& out);
bool DeserializeWheel(std::string_view text, VehicleWheelComponent& comp);

} // namespace AxleVehicle
