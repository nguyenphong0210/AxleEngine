#include <Axle/MaterialGraphProgram.hpp>
#include <Axle/MaterialGraphRuntime.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <iostream>

#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " #value "\n"; return 1; } } while (false)

int main() {
    using namespace Axle;
    using namespace Axle::RHI;

    const auto document = MaterialGraph::Create("OpenGL policy");
    MaterialGraphDiagnostic error;
    CompiledMaterialGraphProgram dxc;
    CHECK(MaterialGraphProgram::Compile(document, RHIShaderFormat::DXIL, dxc, &error));
    const auto unchanged = dxc;

    // A graph target must never silently pass HLSL/DXIL or SPIR-V through the
    // legacy OpenGL path. The failure is transactional at the public compiler API.
    CHECK(!MaterialGraphProgram::Compile(document, RHIShaderFormat::DXIL, dxc, &error,
        MaterialNodeRegistry::Builtins(), MaterialHLSLRegistry::Builtins(),
        MaterialGraphCompilerBackend::DXC, RHIType::OpenGL));
    CHECK(error.Code == "MG_OPENGL_UNSUPPORTED");
    CHECK(dxc.ShaderKey == unchanged.ShaderKey && dxc.Vertex.Data == unchanged.Vertex.Data);

    MaterialGraphTarget cpuTarget;
    MaterialGraphRuntime cpu(document.Identity, cpuTarget);
    CHECK(cpu.Reload(document));
    const auto ready = cpu.Ready();
    CHECK(ready && ready->Target.Backend == RHIType::None);

    MaterialGraphTarget glTarget;
    glTarget.Backend = RHIType::OpenGL;
    MaterialGraphRuntime gl(document.Identity, glTarget);
    CHECK(!gl.Reload(document));
    CHECK(gl.Diagnostic().Code == "MG_OPENGL_UNSUPPORTED");
    CHECK(!gl.Ready());
    CHECK(!RHIDevice::Get().IsInitialized());

    std::cout << "PASS explicit OpenGL material graph unsupported policy and transactional diagnostics\n";
    return GetRHIValidationErrorCount() == 0 ? 0 : 1;
}
