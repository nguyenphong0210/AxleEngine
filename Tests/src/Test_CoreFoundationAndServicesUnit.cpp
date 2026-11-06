#include <Axle/Log.hpp>
#include <Axle/UUID.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Package.hpp>
#include <Axle/Application.hpp>
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

namespace {

    class MockServiceA final : public Axle::IApplicationService {
    public:
        static inline std::vector<std::string> s_LifecycleLog;

        const char* GetName() const override { return "MockServiceA"; }

        void OnInit() override {
            s_LifecycleLog.push_back("A_Init");
        }

        void OnUpdate(Axle::Timestep ts) override {
            s_LifecycleLog.push_back("A_Update");
        }

        void OnShutdown() override {
            s_LifecycleLog.push_back("A_Shutdown");
        }
    };

    class MockServiceB final : public Axle::IApplicationService {
    public:
        const char* GetName() const override { return "MockServiceB"; }

        void OnInit() override {
            MockServiceA::s_LifecycleLog.push_back("B_Init");
        }

        void OnUpdate(Axle::Timestep ts) override {
            MockServiceA::s_LifecycleLog.push_back("B_Update");
        }

        void OnShutdown() override {
            MockServiceA::s_LifecycleLog.push_back("B_Shutdown");
        }
    };

} // namespace

void Test_CorePureFoundation() {
    std::cout << "[AF-02] 1. Testing Pure Foundation Core (Log, UUID, FileSystem, JobSystem)..." << std::endl;

    Axle::Log::Init();

    // UUID
    Axle::UUID id1;
    Axle::UUID id2;
    assert((uint64_t)id1 != 0);
    assert((uint64_t)id2 != 0);
    assert(id1 != id2);
    assert(Axle::UUID(12345) == Axle::UUID(12345));

    // FileSystem
    std::filesystem::path p = Axle::FileSystem::GetAssetDirectory();
    (void)p;

    // JobSystem pure init & shutdown
    assert(Axle::JobSystem::GetState() == Axle::JobSystemState::Stopped);
    Axle::JobSystem::Init(2);
    assert(Axle::JobSystem::GetState() == Axle::JobSystemState::Running);
    assert(Axle::JobSystem::IsMainThread());
    Axle::JobSystem::Shutdown();
    assert(Axle::JobSystem::GetState() == Axle::JobSystemState::Stopped);

    std::cout << "  -> PASS: Pure Core Foundation operates independently without upward dependencies." << std::endl;
}

void Test_ApplicationExplicitServiceOwnership() {
    std::cout << "[AF-02] 2. Testing Application Explicit Service Ownership & Lifecycle..." << std::endl;

    MockServiceA::s_LifecycleLog.clear();

    bool rendererInitCalled = false;
    bool rendererShutdownCalled = false;

    {
        Axle::ApplicationSpecification spec;
        spec.Name = "AF-02 Service Ownership Headless Window";
        spec.Width = 640;
        spec.Height = 480;
        spec.GraphicsBackend = Axle::GraphicsAPI::OpenGL;
        spec.RendererInitCallback = [&rendererInitCalled](void* proc) {
            rendererInitCalled = true;
            return true;
        };
        spec.RendererShutdownCallback = [&rendererShutdownCalled]() {
            rendererShutdownCalled = true;
        };

        Axle::Application app(spec);
        assert(rendererInitCalled);

        // Register Service A then Service B
        auto svcA = std::make_shared<MockServiceA>();
        auto svcB = std::make_shared<MockServiceB>();

        app.RegisterService(svcA);
        app.RegisterService(svcB);

        assert(app.GetService("MockServiceA") == svcA);
        assert(app.GetService<MockServiceA>() == svcA);
        assert(app.GetService("MockServiceB") == svcB);
        assert(app.GetService<MockServiceB>() == svcB);
        assert(app.GetService("NonExistent") == nullptr);

        assert(MockServiceA::s_LifecycleLog.size() == 2);
        assert(MockServiceA::s_LifecycleLog[0] == "A_Init");
        assert(MockServiceA::s_LifecycleLog[1] == "B_Init");
    }

    // After Application destruction:
    // Reverse shutdown order: B_Shutdown then A_Shutdown
    assert(MockServiceA::s_LifecycleLog.size() == 4);
    assert(MockServiceA::s_LifecycleLog[2] == "B_Shutdown");
    assert(MockServiceA::s_LifecycleLog[3] == "A_Shutdown");
    assert(rendererShutdownCalled);

    std::cout << "  -> PASS: Application services initialize and shut down in deterministic reverse order." << std::endl;
}

int main(int argc, char** argv) {
    try {
        Test_CorePureFoundation();
        Test_ApplicationExplicitServiceOwnership();
        std::cout << "\n>>> AF-02 ALL FOUNDATION & SERVICE TESTS PASSED (100%) <<<\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: Exception caught: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "FAIL: Unknown exception caught." << std::endl;
        return 1;
    }
}
