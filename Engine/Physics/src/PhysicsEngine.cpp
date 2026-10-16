#include <Axle/PhysicsEngine.hpp>
#include <Axle/Log.hpp>
#include <Axle/JobSystem.hpp>

// The Jolt headers don't include Jolt.h. Always include Jolt.h before including any other Jolt header.
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>

#include <cstdarg>
#include <cstdio>

namespace Axle {

    // Jolt trace callback mapped to Axle::Log
    static void JoltTraceImpl(const char* inFMT, ...) {
        va_list list;
        va_start(list, inFMT);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), inFMT, list);
        va_end(list);

        AXLE_CORE_TRACE("[Jolt] {0}", buffer);
    }

#ifdef JPH_ENABLE_ASSERTS
    // Jolt assert callback mapped to Axle::Log
    static bool JoltAssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine) {
        AXLE_CORE_ERROR("[Jolt Assert] {0}:{1}: ({2}) {3}", inFile, inLine, inExpression, inMessage ? inMessage : "");
        return true; // Trigger breakpoint
    }
#endif

    void PhysicsEngine::Init() {
        if (s_Initialized) return;

        // Ensure Axle JobSystem is initialized before physics initializes
        if (!JobSystem::IsInitialized()) {
            JobSystem::Init();
        }

        // 1. Register default allocator
        JPH::RegisterDefaultAllocator();

        // 2. Install trace and assert callbacks
        JPH::Trace = JoltTraceImpl;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailedImpl;)

        // 3. Create Jolt factory
        JPH::Factory::sInstance = new JPH::Factory();

        // 4. Register all physics types
        JPH::RegisterTypes();

        s_Initialized = true;
        AXLE_CORE_INFO("Jolt Physics Engine initialized successfully (v5.2.0)");
    }

    void PhysicsEngine::Shutdown() {
        if (!s_Initialized) return;

        // 1. Unregister all physics types
        JPH::UnregisterTypes();

        // 2. Destroy factory
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        s_Initialized = false;
        AXLE_CORE_INFO("Jolt Physics Engine shut down");
    }

}
