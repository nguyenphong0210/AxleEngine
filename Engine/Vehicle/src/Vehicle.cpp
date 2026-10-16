#include <Axle/Vehicle.hpp>
#include <Axle/PhysicsMath.hpp>
#include <Axle/Log.hpp>

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

namespace Axle {

    Vehicle::Vehicle(PhysicsScene& scene, const VehicleConfig& config)
        : m_PhysicsScene(&scene), m_Config(config) {
        
        // 1. Create chassis collision shape with lowered center of mass for road stability
        JPH::RefConst<JPH::Shape> boxShape = new JPH::BoxShape(ToJolt(config.ChassisHalfExtent));
        JPH::OffsetCenterOfMassShapeSettings comShapeSettings(
            JPH::Vec3(0.0f, -config.ChassisHalfExtent.y, 0.0f),
            boxShape
        );
        JPH::ShapeSettings::ShapeResult shapeResult = comShapeSettings.Create();

        JPH::BodyCreationSettings bodySettings(
            shapeResult.Get(),
            ToJolt(config.InitialPosition),
            ToJolt(config.InitialRotation),
            JPH::EMotionType::Dynamic,
            Layers::MOVING
        );
        bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        bodySettings.mMassPropertiesOverride.mMass = config.ChassisMass;

        JPH::Body* chassisBody = scene.GetBodyInterface().CreateBody(bodySettings);
        m_ChassisBodyID = chassisBody->GetID();
        scene.GetBodyInterface().AddBody(m_ChassisBodyID, JPH::EActivation::Activate);

        // 2. Configure 4-wheel vehicle constraint
        JPH::VehicleConstraintSettings vehicleSettings;
        vehicleSettings.mDrawConstraintSize = 0.1f;
        vehicleSettings.mMaxPitchRollAngle = JPH::DegreesToRadians(60.0f);
        vehicleSettings.mUp = JPH::Vec3(0.0f, 1.0f, 0.0f);
        vehicleSettings.mForward = JPH::Vec3(0.0f, 0.0f, 1.0f);

        auto createWheel = [&](f32 x, f32 y, f32 z, bool isFront) {
            JPH::WheelSettingsWV* w = new JPH::WheelSettingsWV();
            w->mPosition = JPH::Vec3(x, y, z);
            w->mRadius = config.WheelRadius;
            w->mWidth = config.WheelWidth;
            w->mSuspensionMinLength = config.SuspensionMinLength;
            w->mSuspensionMaxLength = config.SuspensionMaxLength;
            w->mSuspensionSpring.mMode = JPH::ESpringMode::FrequencyAndDamping;
            w->mSuspensionSpring.mFrequency = config.SpringFrequency;
            w->mSuspensionSpring.mDamping = config.SpringDamping;

            if (isFront) {
                w->mMaxSteerAngle = JPH::DegreesToRadians(config.MaxSteerAngleDegrees);
                w->mMaxHandBrakeTorque = 0.0f;
                w->mMaxBrakeTorque = config.MaxBrakeTorque;
            } else {
                w->mMaxSteerAngle = 0.0f;
                w->mMaxHandBrakeTorque = config.MaxHandBrakeTorque;
                w->mMaxBrakeTorque = config.MaxBrakeTorque * 0.7f;
            }
            return w;
        };

        // 3. Powertrain controller settings (Engine, Automatic Transmission, Differentials)
        JPH::WheeledVehicleControllerSettings* controllerSettings = new JPH::WheeledVehicleControllerSettings();
        controllerSettings->mEngine.mMaxTorque = config.MaxEngineTorque;
        controllerSettings->mEngine.mMinRPM = config.MinRPM;
        controllerSettings->mEngine.mMaxRPM = config.MaxRPM;

        controllerSettings->mTransmission.mMode = JPH::ETransmissionMode::Auto;
        controllerSettings->mTransmission.mGearRatios = { 2.66f, 1.78f, 1.30f, 1.00f, 0.74f };
        controllerSettings->mTransmission.mReverseGearRatios = { -2.90f };
        controllerSettings->mTransmission.mClutchReleaseTime = 0.05f;
        controllerSettings->mTransmission.mSwitchTime = 0.15f;
        controllerSettings->mTransmission.mClutchStrength = 50.0f;

        if (!config.CustomWheels.empty()) {
            u32 numWheels = static_cast<u32>(config.CustomWheels.size());
            vehicleSettings.mWheels.resize(numWheels);
            for (u32 i = 0; i < numWheels; ++i) {
                const auto& cw = config.CustomWheels[i];
                JPH::WheelSettingsWV* w = new JPH::WheelSettingsWV();
                w->mPosition = JPH::Vec3(cw.Position.x, cw.Position.y, cw.Position.z);
                w->mRadius = cw.Radius;
                w->mWidth = cw.Width;
                w->mSuspensionMinLength = cw.SuspensionMinLength;
                w->mSuspensionMaxLength = cw.SuspensionMaxLength;
                w->mSuspensionSpring.mMode = JPH::ESpringMode::FrequencyAndDamping;
                w->mSuspensionSpring.mFrequency = cw.SpringFrequency;
                w->mSuspensionSpring.mDamping = cw.SpringDamping;
                w->mMaxSteerAngle = cw.CanSteer ? JPH::DegreesToRadians(cw.MaxSteerAngleDegrees) : 0.0f;
                w->mMaxBrakeTorque = cw.MaxBrakeTorque;
                w->mMaxHandBrakeTorque = cw.MaxHandBrakeTorque;
                vehicleSettings.mWheels[i] = w;
            }

            // Configure differentials for driven wheels
            std::vector<u32> drivenWheels;
            for (u32 i = 0; i < numWheels; ++i) {
                if (config.CustomWheels[i].IsDriven) {
                    drivenWheels.push_back(i);
                }
            }

            if (!drivenWheels.empty()) {
                u32 diffCount = (static_cast<u32>(drivenWheels.size()) + 1) / 2;
                controllerSettings->mDifferentials.resize(diffCount);
                float torquePerDiff = 1.0f / static_cast<float>(diffCount);
                for (u32 d = 0; d < diffCount; ++d) {
                    controllerSettings->mDifferentials[d].mLeftWheel = drivenWheels[d * 2];
                    controllerSettings->mDifferentials[d].mRightWheel = (d * 2 + 1 < drivenWheels.size()) ? drivenWheels[d * 2 + 1] : drivenWheels[d * 2];
                    controllerSettings->mDifferentials[d].mEngineTorqueRatio = torquePerDiff;
                }
            }

            // Anti-roll bars for wheel pairs
            u32 pairCount = numWheels / 2;
            vehicleSettings.mAntiRollBars.resize(pairCount);
            for (u32 p = 0; p < pairCount; ++p) {
                vehicleSettings.mAntiRollBars[p].mLeftWheel = p * 2;
                vehicleSettings.mAntiRollBars[p].mRightWheel = p * 2 + 1;
            }
        } else {
            // Default 4-wheel setup for backward compatibility
            vehicleSettings.mWheels.resize(4);
            vehicleSettings.mWheels[Wheel_FrontLeft]  = createWheel( config.WheelOffsetLateral, -config.WheelOffsetVertical,  config.WheelOffsetHorizontal, true);
            vehicleSettings.mWheels[Wheel_FrontRight] = createWheel(-config.WheelOffsetLateral, -config.WheelOffsetVertical,  config.WheelOffsetHorizontal, true);
            vehicleSettings.mWheels[Wheel_RearLeft]   = createWheel( config.WheelOffsetLateral, -config.WheelOffsetVertical, -config.WheelOffsetHorizontal, false);
            vehicleSettings.mWheels[Wheel_RearRight]  = createWheel(-config.WheelOffsetLateral, -config.WheelOffsetVertical, -config.WheelOffsetHorizontal, false);

            if (config.FourWheelDrive) {
                controllerSettings->mDifferentials.resize(2);
                controllerSettings->mDifferentials[0].mLeftWheel = Wheel_FrontLeft;
                controllerSettings->mDifferentials[0].mRightWheel = Wheel_FrontRight;
                controllerSettings->mDifferentials[0].mEngineTorqueRatio = 0.5f;

                controllerSettings->mDifferentials[1].mLeftWheel = Wheel_RearLeft;
                controllerSettings->mDifferentials[1].mRightWheel = Wheel_RearRight;
                controllerSettings->mDifferentials[1].mEngineTorqueRatio = 0.5f;
            } else {
                // Rear-wheel drive (RWD)
                controllerSettings->mDifferentials.resize(1);
                controllerSettings->mDifferentials[0].mLeftWheel = Wheel_RearLeft;
                controllerSettings->mDifferentials[0].mRightWheel = Wheel_RearRight;
                controllerSettings->mDifferentials[0].mEngineTorqueRatio = 1.0f;
            }

            // Anti-roll bars
            vehicleSettings.mAntiRollBars.resize(2);
            vehicleSettings.mAntiRollBars[0].mLeftWheel = Wheel_FrontLeft;
            vehicleSettings.mAntiRollBars[0].mRightWheel = Wheel_FrontRight;
            vehicleSettings.mAntiRollBars[1].mLeftWheel = Wheel_RearLeft;
            vehicleSettings.mAntiRollBars[1].mRightWheel = Wheel_RearRight;
        }

        vehicleSettings.mController = controllerSettings;

        // 4. Create VehicleConstraint and Collision Tester
        m_Constraint = new JPH::VehicleConstraint(*chassisBody, vehicleSettings);
        m_CollisionTester = new JPH::VehicleCollisionTesterCastSphere(Layers::MOVING, 0.5f * config.WheelWidth);
        m_Constraint->SetVehicleCollisionTester(m_CollisionTester);

        scene.AddConstraint(m_Constraint);
        scene.AddStepListener(m_Constraint);

        m_Controller = static_cast<JPH::WheeledVehicleController*>(m_Constraint->GetController());
        m_Controller->SetTireMaxImpulseCallback(
            [](JPH::uint, float& outLongitudinalImpulse, float& outLateralImpulse,
               float inSuspensionImpulse, float inLongitudinalFriction, float inLateralFriction,
               float, float, float)
            {
                outLongitudinalImpulse = 10.0f * inLongitudinalFriction * inSuspensionImpulse;
                outLateralImpulse = inLateralFriction * inSuspensionImpulse;
            }
        );

        AXLE_CORE_INFO("Vehicle initialized successfully (Chassis Mass: {0} kg)", config.ChassisMass);
    }

    Vehicle::~Vehicle() {
        if (m_PhysicsScene) {
            if (m_Constraint) {
                m_PhysicsScene->RemoveStepListener(m_Constraint);
                m_PhysicsScene->RemoveConstraint(m_Constraint);
            }
            if (!m_ChassisBodyID.IsInvalid()) {
                m_PhysicsScene->DestroyBody(m_ChassisBodyID);
            }
        }
        AXLE_CORE_INFO("Vehicle destroyed");
    }

    void Vehicle::SetDriverInput(f32 forward, f32 right, f32 brake, f32 handBrake) {
        if (m_Controller) {
            m_Controller->SetDriverInput(forward, right, brake, handBrake);
        }
        if (m_PhysicsScene && !m_ChassisBodyID.IsInvalid()) {
            if (forward != 0.0f || right != 0.0f || brake != 0.0f || handBrake != 0.0f) {
                m_PhysicsScene->GetBodyInterface().ActivateBody(m_ChassisBodyID);
            }
        }
    }

    void Vehicle::Reset(const glm::vec3& position, const glm::quat& rotation) {
        if (m_PhysicsScene && !m_ChassisBodyID.IsInvalid()) {
            m_PhysicsScene->SetBodyPositionAndRotation(m_ChassisBodyID, position, rotation, true);
            m_PhysicsScene->SetLinearVelocity(m_ChassisBodyID, glm::vec3(0.0f));
            m_PhysicsScene->GetBodyInterface().ActivateBody(m_ChassisBodyID);
            if (m_Controller) {
                m_Controller->SetDriverInput(0.0f, 0.0f, 0.0f, 0.0f);
            }
        }
    }

    VehicleTelemetryData Vehicle::GetTelemetry() const {
        VehicleTelemetryData data;
        if (!m_PhysicsScene || m_ChassisBodyID.IsInvalid()) {
            return data;
        }

        data.Position = m_PhysicsScene->GetBodyPosition(m_ChassisBodyID);
        data.LinearVelocity = m_PhysicsScene->GetLinearVelocity(m_ChassisBodyID);
        data.SpeedMps = glm::length(data.LinearVelocity);
        data.SpeedKmh = data.SpeedMps * 3.6f;

        const glm::quat rot = m_PhysicsScene->GetBodyRotation(m_ChassisBodyID);
        data.ForwardVector = rot * glm::vec3(0.0f, 0.0f, 1.0f);

        if (m_Controller) {
            data.EngineRPM = m_Controller->GetEngine().GetCurrentRPM();
            data.CurrentGear = m_Controller->GetTransmission().GetCurrentGear();
        }

        if (m_Constraint) {
            const auto& wheels = m_Constraint->GetWheels();
            for (u32 i = 0; i < 4 && i < wheels.size(); ++i) {
                const auto* w = wheels[i];
                data.Wheels[i].WorldTransform = GetWheelTransform(i);
                data.Wheels[i].SuspensionLength = w->GetSuspensionLength();
                data.Wheels[i].SteerAngle = w->GetSteerAngle();
                data.Wheels[i].AngularVelocity = w->GetAngularVelocity();
                data.Wheels[i].HasContact = w->HasContact();

                const auto* wv = static_cast<const JPH::WheelWV*>(w);
                data.Wheels[i].LongitudinalSlip = wv->mLongitudinalSlip;
                data.Wheels[i].LateralSlip = wv->mLateralSlip;
            }
        }

        return data;
    }

    glm::mat4 Vehicle::GetChassisTransform() const {
        if (m_PhysicsScene && !m_ChassisBodyID.IsInvalid()) {
            return m_PhysicsScene->GetBodyTransform(m_ChassisBodyID);
        }
        return glm::mat4(1.0f);
    }

    glm::mat4 Vehicle::GetWheelTransform(u32 wheelIndex) const {
        if (!m_Constraint || wheelIndex >= static_cast<u32>(m_Constraint->GetWheels().size())) {
            return glm::mat4(1.0f);
        }
        const JPH::RMat44 joltMat = m_Constraint->GetWheelWorldTransform(wheelIndex, JPH::Vec3::sAxisY(), JPH::Vec3::sAxisX());
        return ToGlm(joltMat.ToMat44());
    }

    glm::vec3 Vehicle::GetPosition() const {
        if (m_PhysicsScene && !m_ChassisBodyID.IsInvalid()) {
            return m_PhysicsScene->GetBodyPosition(m_ChassisBodyID);
        }
        return glm::vec3(0.0f);
    }

    glm::vec3 Vehicle::GetForwardVector() const {
        if (m_PhysicsScene && !m_ChassisBodyID.IsInvalid()) {
            const glm::quat rot = m_PhysicsScene->GetBodyRotation(m_ChassisBodyID);
            return rot * glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }

}
