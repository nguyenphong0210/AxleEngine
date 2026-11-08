#include "AxleVehicle/VehicleComponents.hpp"
#include "AxleVehicle/VehicleCodecs.hpp"

#include <sstream>
#include <iomanip>

namespace AxleVehicle {

bool SerializeChassis(const VehicleChassisComponent& comp, std::string& out) {
    if (!comp.Validate()) return false;
    std::ostringstream s;
    s << std::setprecision(9)
      << "chassis-v1 "
      << comp.Config.HalfExtents[0] << ' ' << comp.Config.HalfExtents[1] << ' ' << comp.Config.HalfExtents[2] << ' '
      << comp.Config.Mass << ' '
      << comp.Config.CenterOfMassOffset[0] << ' ' << comp.Config.CenterOfMassOffset[1] << ' ' << comp.Config.CenterOfMassOffset[2] << ' '
      << comp.Config.MaxEngineTorque << ' '
      << comp.Config.MinRPM << ' '
      << comp.Config.MaxRPM << ' '
      << comp.Config.MaxSteerAngle << ' '
      << comp.Config.MaxBrakeTorque << ' '
      << comp.Config.MaxHandbrakeTorque << ' '
      << (comp.Config.FourWheelDrive ? 1 : 0) << ' '
      << comp.Config.WheelEntities[0] << ' ' << comp.Config.WheelEntities[1] << ' '
      << comp.Config.WheelEntities[2] << ' ' << comp.Config.WheelEntities[3];
    out = s.str();
    return true;
}

bool DeserializeChassis(std::string_view text, VehicleChassisComponent& comp) {
    if (text.empty()) return false;
    std::string s(text);
    std::istringstream in(s);
    std::string tag;
    if (!(in >> tag) || tag != "chassis-v1") return false;
    VehicleChassisComponent parsed{};
    int fwd = 1;
    if (!(in >> parsed.Config.HalfExtents[0] >> parsed.Config.HalfExtents[1] >> parsed.Config.HalfExtents[2]
             >> parsed.Config.Mass
             >> parsed.Config.CenterOfMassOffset[0] >> parsed.Config.CenterOfMassOffset[1] >> parsed.Config.CenterOfMassOffset[2]
             >> parsed.Config.MaxEngineTorque
             >> parsed.Config.MinRPM
             >> parsed.Config.MaxRPM
             >> parsed.Config.MaxSteerAngle
             >> parsed.Config.MaxBrakeTorque
             >> parsed.Config.MaxHandbrakeTorque
             >> fwd
             >> parsed.Config.WheelEntities[0] >> parsed.Config.WheelEntities[1]
             >> parsed.Config.WheelEntities[2] >> parsed.Config.WheelEntities[3])) {
        return false;
    }
    in >> std::ws;
    if (!in.eof()) return false;
    parsed.Config.FourWheelDrive = (fwd != 0);
    if (!parsed.Validate()) return false;
    parsed.Runtime.Reset();
    comp = parsed;
    return true;
}

bool SerializeWheel(const VehicleWheelComponent& comp, std::string& out) {
    if (!comp.Validate()) return false;
    std::ostringstream s;
    s << std::setprecision(9)
      << "wheel-v1 "
      << comp.Config.ChassisEntity << ' '
      << comp.Config.WheelIndex << ' '
      << comp.Config.Offset[0] << ' ' << comp.Config.Offset[1] << ' ' << comp.Config.Offset[2] << ' '
      << comp.Config.Radius << ' '
      << comp.Config.Width << ' '
      << comp.Config.SuspensionMinLength << ' '
      << comp.Config.SuspensionMaxLength << ' '
      << comp.Config.SpringFrequency << ' '
      << comp.Config.SpringDamping << ' '
      << (comp.Config.CanSteer ? 1 : 0) << ' '
      << (comp.Config.IsDriven ? 1 : 0) << ' '
      << comp.Config.BrakeRatio << ' '
      << comp.Config.HandbrakeRatio;
    out = s.str();
    return true;
}

bool DeserializeWheel(std::string_view text, VehicleWheelComponent& comp) {
    if (text.empty()) return false;
    std::string s(text);
    std::istringstream in(s);
    std::string tag;
    if (!(in >> tag) || tag != "wheel-v1") return false;
    VehicleWheelComponent parsed{};
    int steer = 0, driven = 1;
    if (!(in >> parsed.Config.ChassisEntity
             >> parsed.Config.WheelIndex
             >> parsed.Config.Offset[0] >> parsed.Config.Offset[1] >> parsed.Config.Offset[2]
             >> parsed.Config.Radius
             >> parsed.Config.Width
             >> parsed.Config.SuspensionMinLength
             >> parsed.Config.SuspensionMaxLength
             >> parsed.Config.SpringFrequency
             >> parsed.Config.SpringDamping
             >> steer
             >> driven
             >> parsed.Config.BrakeRatio
             >> parsed.Config.HandbrakeRatio)) {
        return false;
    }
    in >> std::ws;
    if (!in.eof()) return false;
    parsed.Config.CanSteer = (steer != 0);
    parsed.Config.IsDriven = (driven != 0);
    if (!parsed.Validate()) return false;
    parsed.Runtime.Reset();
    comp = parsed;
    return true;
}

} // namespace AxleVehicle
