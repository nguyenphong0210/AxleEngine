#include "VFXTestHarness.hpp"
#include <Axle/ParticleEmitterSerializer.hpp>

namespace {
    bool TestBaselineEmitterSerializer() {
        Axle::ParticleEmitterConfig config;
        config.Name = "TestEmitter";
        config.MaxParticles = 500;
        config.EmissionRate = 25.0f;

        Axle::VFXTests::ScopedTempDir tempDir;
        auto path = tempDir.Path / "test_legacy.axleasset";
        VFX_TEST_CHECK(Axle::ParticleEmitterSerializer::SerializeToFile(path.string(), config), "Legacy serializer save");

        Axle::ParticleEmitterConfig loaded;
        std::string outTexture;
        VFX_TEST_CHECK(Axle::ParticleEmitterSerializer::DeserializeFromFile(path.string(), loaded, outTexture), "Legacy serializer load");
        VFX_TEST_CHECK(loaded.Name == "TestEmitter", "Name preserved");
        VFX_TEST_CHECK(loaded.MaxParticles == 500, "MaxParticles preserved");
        VFX_TEST_APPROX_EQUAL(loaded.EmissionRate, 25.0f, 0.001f, "EmissionRate preserved");
        return true;
    }
}

bool RunVFXPersistenceUnitTests() {
    std::cout << "  - Running TestBaselineEmitterSerializer ...\n";
    VFX_TEST_CHECK(TestBaselineEmitterSerializer(), "TestBaselineEmitterSerializer failed");
    return true;
}
