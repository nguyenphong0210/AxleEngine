#include "AxleVehicle/VehiclePhysics.hpp"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>

#include <cmath>
#include <algorithm>

namespace AxleVehicle {

VehiclePhysics::~VehiclePhysics() {
    Teardown();
}

VehiclePhysics::VehiclePhysics(VehiclePhysics&& other) noexcept {
    *this = std::move(other);
}

VehiclePhysics& VehiclePhysics::operator=(VehiclePhysics&& other) noexcept {
    if (this != &other) {
        Teardown();
        m_PhysicsScene = other.m_PhysicsScene;
        m_ChassisBodyID = other.m_ChassisBodyID;
        m_Constraint = std::move(other.m_Constraint);
        m_CollisionTester = std::move(other.m_CollisionTester);
        m_Controller = other.m_Controller;
        m_ChassisConfig = other.m_ChassisConfig;
        for (int i = 0; i < 4; ++i) m_WheelConfigs[i] = other.m_WheelConfigs[i];

        other.m_PhysicsScene = nullptr;
        other.m_ChassisBodyID = Axle::BodyID();
        other.m_Controller = nullptr;
    }
    return *this;
}

void VehiclePhysics::Teardown() {
    if (m_PhysicsScene) {
        if (m_Constraint) {
            m_PhysicsScene->RemoveStepListener(m_Constraint);
            m_PhysicsScene->RemoveConstraint(m_Constraint);
            m_Constraint = nullptr;
            m_CollisionTester = nullptr;
            m_Controller = nullptr;
        }
        if (!m_ChassisBodyID.IsInvalid()) {
            m_PhysicsScene->DestroyBody(m_ChassisBodyID);
            m_ChassisBodyID = Axle::BodyID();
        }
        m_PhysicsScene = nullptr;
    }
}

bool VehiclePhysics::Initialize(Axle::PhysicsScene& scene, const VehicleInitParams& params, std::string* outError) {
    Teardown();

    // 1. Validation
    VehicleChassisComponent tempChassis{};
    tempChassis.Config = params.Chassis;
    if (!tempChassis.Validate()) {
        if (outError) *outError = "Chassis configuration is invalid or contains non-finite values";
        return false;
    }

    for (std::uint32_t i = 0; i < 4; ++i) {
        VehicleWheelComponent tempWheel{};
        tempWheel.Config = params.Wheels[i];
        if (!tempWheel.Validate()) {
            if (outError) *outError = "Wheel " + std::to_string(i) + " configuration is invalid";
            return false;
        }
    }

    m_PhysicsScene = &scene;
    m_ChassisConfig = params.Chassis;
    for (int i = 0; i < 4; ++i) m_WheelConfigs[i] = params.Wheels[i];

    // 2. Chassis Shape
    const JPH::Vec3 halfExtents(params.Chassis.HalfExtents[0], params.Chassis.HalfExtents[1], params.Chassis.HalfExtents[2]);
    JPH::RefConst<JPH::Shape> boxShape = new JPH::BoxShape(halfExtents);
    JPH::RefConst<JPH::Shape> chassisShape = boxShape;

    const bool hasComOffset = (params.Chassis.CenterOfMassOffset[0] != 0.0f ||
                               params.Chassis.CenterOfMassOffset[1] != 0.0f ||
                               params.Chassis.CenterOfMassOffset[2] != 0.0f);
    if (hasComOffset) {
        JPH::OffsetCenterOfMassShapeSettings comSettings(
            JPH::Vec3(params.Chassis.CenterOfMassOffset[0], params.Chassis.CenterOfMassOffset[1], params.Chassis.CenterOfMassOffset[2]),
            boxShape
        );
        auto shapeResult = comSettings.Create();
        if (shapeResult.IsValid()) {
            chassisShape = shapeResult.Get();
        }
    }

    // 3. Body Creation
    const glm::vec3 initPos(params.InitialPosition[0], params.InitialPosition[1], params.InitialPosition[2]);
    const glm::quat initRot(params.InitialRotation[0], params.InitialRotation[1], params.InitialRotation[2], params.InitialRotation[3]);

    JPH::BodyCreationSettings bodySettings(
        chassisShape,
        Axle::ToJolt(initPos),
        Axle::ToJolt(initRot),
        JPH::EMotionType::Dynamic,
        Axle::Layers::MOVING
    );
    bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    bodySettings.mMassPropertiesOverride.mMass = params.Chassis.Mass;

    JPH::Body* chassisBody = scene.GetBodyInterface().CreateBody(bodySettings);
    if (!chassisBody) {
        if (outError) *outError = "Failed to create chassis Jolt rigid body";
        Teardown();
        return false;
    }
    m_ChassisBodyID = chassisBody->GetID();
    scene.GetBodyInterface().AddBody(m_ChassisBodyID, JPH::EActivation::Activate);

    // 4. Vehicle Constraint Settings
    JPH::VehicleConstraintSettings vehicleSettings;
    vehicleSettings.mDrawConstraintSize = 0.1f;
    vehicleSettings.mMaxPitchRollAngle = JPH::DegreesToRadians(60.0f);
    vehicleSettings.mUp = JPH::Vec3(0.0f, 1.0f, 0.0f);
    vehicleSettings.mForward = JPH::Vec3(0.0f, 0.0f, 1.0f);

    vehicleSettings.mWheels.resize(4);
    for (std::uint32_t i = 0; i < 4; ++i) {
        const auto& wCfg = params.Wheels[i];
        float x = wCfg.Offset[0];
        float y = wCfg.Offset[1];
        float z = wCfg.Offset[2];
        if (std::abs(x) < 1e-4f && std::abs(z) < 1e-4f) {
            // Default 4-corner positions based on chassis half extents:
            // FL (0): +X, +Z; FR (1): -X, +Z; RL (2): +X, -Z; RR (3): -X, -Z
            const float signX = (i == 0 || i == 2) ? 1.0f : -1.0f;
            const float signZ = (i == 0 || i == 1) ? 1.0f : -1.0f;
            x = signX * (params.Chassis.HalfExtents[0] * 0.9f);
            z = signZ * (params.Chassis.HalfExtents[2] * 0.75f);
            y = -params.Chassis.HalfExtents[1] * 0.6f;
        }

        JPH::WheelSettingsWV* w = new JPH::WheelSettingsWV();
        w->mPosition = JPH::Vec3(x, y, z);
        w->mRadius = wCfg.Radius;
        w->mWidth = wCfg.Width;
        w->mSuspensionMinLength = wCfg.SuspensionMinLength;
        w->mSuspensionMaxLength = wCfg.SuspensionMaxLength;
        w->mSuspensionSpring.mMode = JPH::ESpringMode::FrequencyAndDamping;
        w->mSuspensionSpring.mFrequency = wCfg.SpringFrequency;
        w->mSuspensionSpring.mDamping = wCfg.SpringDamping;
        w->mMaxSteerAngle = wCfg.CanSteer ? JPH::DegreesToRadians(params.Chassis.MaxSteerAngle) : 0.0f;
        w->mMaxBrakeTorque = params.Chassis.MaxBrakeTorque * wCfg.BrakeRatio;
        w->mMaxHandBrakeTorque = params.Chassis.MaxHandbrakeTorque * wCfg.HandbrakeRatio;

        vehicleSettings.mWheels[i] = w;
    }

    // 5. Powertrain Controller Settings
    auto* controllerSettings = new JPH::WheeledVehicleControllerSettings();
    controllerSettings->mEngine.mMaxTorque = params.Chassis.MaxEngineTorque;
    controllerSettings->mEngine.mMinRPM = params.Chassis.MinRPM;
    controllerSettings->mEngine.mMaxRPM = params.Chassis.MaxRPM;

    controllerSettings->mTransmission.mMode = JPH::ETransmissionMode::Auto;
    controllerSettings->mTransmission.mGearRatios = { 2.66f, 1.78f, 1.30f, 1.00f, 0.74f };
    controllerSettings->mTransmission.mReverseGearRatios = { -2.90f };

    if (params.Chassis.FourWheelDrive) {
        controllerSettings->mDifferentials.resize(2);
        controllerSettings->mDifferentials[0].mLeftWheel = 0;
        controllerSettings->mDifferentials[0].mRightWheel = 1;
        controllerSettings->mDifferentials[0].mDifferentialRatio = 3.42f;
        controllerSettings->mDifferentials[0].mEngineTorqueRatio = 0.5f;

        controllerSettings->mDifferentials[1].mLeftWheel = 2;
        controllerSettings->mDifferentials[1].mRightWheel = 3;
        controllerSettings->mDifferentials[1].mDifferentialRatio = 3.42f;
        controllerSettings->mDifferentials[1].mEngineTorqueRatio = 0.5f;
    } else {
        controllerSettings->mDifferentials.resize(1);
        controllerSettings->mDifferentials[0].mLeftWheel = 2;
        controllerSettings->mDifferentials[0].mRightWheel = 3;
        controllerSettings->mDifferentials[0].mDifferentialRatio = 3.42f;
        controllerSettings->mDifferentials[0].mEngineTorqueRatio = 1.0f;
    }
    controllerSettings->mDifferentialLimitedSlipRatio = 1.4f;
    vehicleSettings.mController = controllerSettings;

    // 6. Collision Tester
    m_CollisionTester = new JPH::VehicleCollisionTesterRay(Axle::Layers::MOVING);

    // 7. Instantiate Constraint and Attach to Scene
    m_Constraint = new JPH::VehicleConstraint(*chassisBody, vehicleSettings);
    m_Constraint->SetVehicleCollisionTester(m_CollisionTester);
    scene.AddConstraint(m_Constraint);
    scene.AddStepListener(m_Constraint);
    m_Controller = static_cast<JPH::WheeledVehicleController*>(m_Constraint->GetController());

    return true;
}

void VehiclePhysics::SetDriverInput(float forward, float right, float brake, bool handbrake) {
    if (m_Controller) {
        m_Controller->SetDriverInput(forward, right, brake, handbrake ? 1.0f : 0.0f);
    }
    if (m_PhysicsScene && !m_ChassisBodyID.IsInvalid()) {
        if (forward != 0.0f || right != 0.0f || brake != 0.0f || handbrake) {
            m_PhysicsScene->GetBodyInterface().ActivateBody(m_ChassisBodyID);
        }
    }
}

void VehiclePhysics::Reset(const float position[3], const float rotation[4]) {
    if (!m_PhysicsScene || m_ChassisBodyID.IsInvalid()) return;

    const glm::vec3 pos(position[0], position[1], position[2]);
    const glm::quat rot(rotation[0], rotation[1], rotation[2], rotation[3]);

    m_PhysicsScene->SetBodyPositionAndRotation(m_ChassisBodyID, pos, rot, true);
    m_PhysicsScene->SetLinearVelocity(m_ChassisBodyID, glm::vec3(0.0f));
    m_PhysicsScene->GetBodyInterface().SetAngularVelocity(m_ChassisBodyID, JPH::Vec3::sZero());
    m_PhysicsScene->GetBodyInterface().ActivateBody(m_ChassisBodyID);

    if (m_Controller) {
        m_Controller->SetDriverInput(0.0f, 0.0f, 0.0f, 0.0f);
    }
    if (m_Constraint) {
        m_Constraint->ResetWarmStart();
        for (auto* wheel : m_Constraint->GetWheels()) {
            if (wheel) {
                wheel->SetAngularVelocity(0.0f);
                wheel->SetRotationAngle(0.0f);
                wheel->SetSteerAngle(0.0f);
            }
        }
    }
}

void VehiclePhysics::Synchronize(ChassisRuntimeState& outChassis, WheelRuntimeState outWheels[4]) const {
    if (!m_PhysicsScene || m_ChassisBodyID.IsInvalid()) return;

    const glm::vec3 vel = m_PhysicsScene->GetLinearVelocity(m_ChassisBodyID);
    const float speedMs = glm::length(vel);
    outChassis.CurrentSpeedKmh = speedMs * 3.6f;

    if (m_Controller) {
        outChassis.CurrentRPM = m_Controller->GetEngine().GetCurrentRPM();
        outChassis.CurrentGear = m_Controller->GetTransmission().GetCurrentGear();
    }

    bool anyContact = false;
    if (m_Constraint) {
        const auto& wheels = m_Constraint->GetWheels();
        for (std::uint32_t i = 0; i < 4 && i < wheels.size(); ++i) {
            const auto* w = wheels[i];
            if (!w) continue;
            outWheels[i].CurrentSuspensionLength = w->GetSuspensionLength();
            outWheels[i].IsOnGround = w->HasContact();
            outWheels[i].SteerAngle = w->GetSteerAngle();
            outWheels[i].AngularVelocity = w->GetAngularVelocity();
            outWheels[i].RotationAngle = w->GetRotationAngle();
            if (w->HasContact()) {
                anyContact = true;
                const JPH::Vec3 norm = w->GetContactNormal();
                outWheels[i].ContactNormal[0] = norm.GetX();
                outWheels[i].ContactNormal[1] = norm.GetY();
                outWheels[i].ContactNormal[2] = norm.GetZ();
            } else {
                outWheels[i].ContactNormal[0] = 0.0f;
                outWheels[i].ContactNormal[1] = 1.0f;
                outWheels[i].ContactNormal[2] = 0.0f;
            }

            const auto* wv = static_cast<const JPH::WheelWV*>(w);
            outWheels[i].LongitudinalSlip = wv->mLongitudinalSlip;
            outWheels[i].LateralSlip = wv->mLateralSlip;
        }
    }
    outChassis.IsOnGround = anyContact;
}

bool VehiclePhysics::GetWheelWorldTransform(std::uint32_t wheelIndex, float outMatrix16[16]) const {
    if (!m_Constraint || wheelIndex >= static_cast<std::uint32_t>(m_Constraint->GetWheels().size()) || !outMatrix16) {
        return false;
    }

    const JPH::Wheel* wheel = m_Constraint->GetWheel(wheelIndex);
    const bool isLeft = (wheel && wheel->GetSettings() && wheel->GetSettings()->mPosition.GetX() >= 0.0f);
    const JPH::Vec3 wheelRight = isLeft ? -JPH::Vec3::sAxisX() : JPH::Vec3::sAxisX();
    const JPH::Vec3 wheelUp = JPH::Vec3::sAxisY();

    const JPH::RMat44 joltMat = m_Constraint->GetWheelWorldTransform(wheelIndex, wheelRight, wheelUp);
    const JPH::Mat44 mat = joltMat.ToMat44();

    for (int col = 0; col < 4; ++col) {
        const JPH::Vec4 c = mat.GetColumn4(col);
        outMatrix16[col * 4 + 0] = c.GetX();
        outMatrix16[col * 4 + 1] = c.GetY();
        outMatrix16[col * 4 + 2] = c.GetZ();
        outMatrix16[col * 4 + 3] = c.GetW();
    }
    return true;
}

} // namespace AxleVehicle
