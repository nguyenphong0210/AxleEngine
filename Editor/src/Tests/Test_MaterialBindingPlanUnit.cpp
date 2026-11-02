#include "MaterialTestHarness.hpp"
#include <Axle/MaterialBindingPlan.hpp>
#include <Axle/CustomShaderInterface.hpp>
#include <cstring>
#include <vector>

using namespace Axle;
using namespace Axle::MaterialTests;

namespace {

    // 15.4 Exact 64-byte user-buffer fixture
    bool TestExact64BytePackingFixture() {
        MaterialBindingPlan plan;
        ConstantBufferPlan cbp;
        cbp.Name = "MaterialParameters";
        cbp.Address = { 0, 2 }; // b2, space0
        cbp.LogicalSize = 64;
        plan.Buffers.push_back(cbp);

        // Define reflected variable bindings
        // Offset 0: Gain (Float)
        plan.Uniforms.push_back(UniformBinding{ 0, 0, 4, UniformKind::Float32, PropertyKey{ "MaterialParameters", "Gain" } });
        // Offset 4: Enabled (Bool)
        plan.Uniforms.push_back(UniformBinding{ 0, 4, 4, UniformKind::Bool32, PropertyKey{ "MaterialParameters", "Enabled" } });
        // Offset 8..15: Padding
        // Offset 16: Tint (Float3)
        plan.Uniforms.push_back(UniformBinding{ 0, 16, 12, UniformKind::Float3, PropertyKey{ "MaterialParameters", "Tint" } });
        // Offset 28: Mode (Int)
        plan.Uniforms.push_back(UniformBinding{ 0, 28, 4, UniformKind::Int32, PropertyKey{ "MaterialParameters", "Mode" } });
        // Offset 32: Accent (Color / Float4)
        plan.Uniforms.push_back(UniformBinding{ 0, 32, 16, UniformKind::Float4, PropertyKey{ "MaterialParameters", "Accent" } });
        // Offset 48: UVScale (Float2)
        plan.Uniforms.push_back(UniformBinding{ 0, 48, 8, UniformKind::Float2, PropertyKey{ "MaterialParameters", "UVScale" } });
        // Offset 56: UVOffset (Float2)
        plan.Uniforms.push_back(UniformBinding{ 0, 56, 8, UniformKind::Float2, PropertyKey{ "MaterialParameters", "UVOffset" } });

        ResolvedMaterialProperties props;
        props.Properties.push_back(ResolvedMaterialProperty{ { "MaterialParameters", "Gain" }, UniformKind::Float32, 0.5f });
        props.Properties.push_back(ResolvedMaterialProperty{ { "MaterialParameters", "Enabled" }, UniformKind::Bool32, 1u });
        props.Properties.push_back(ResolvedMaterialProperty{ { "MaterialParameters", "Tint" }, UniformKind::Float3, glm::vec3(2.0f, 0.25f, 1.0f) });
        props.Properties.push_back(ResolvedMaterialProperty{ { "MaterialParameters", "Mode" }, UniformKind::Int32, static_cast<int32_t>(-2) });
        props.Properties.push_back(ResolvedMaterialProperty{ { "MaterialParameters", "Accent" }, UniformKind::Float4, glm::vec4(0.125f, 0.5f, 4.0f, 0.75f) });
        props.Properties.push_back(ResolvedMaterialProperty{ { "MaterialParameters", "UVScale" }, UniformKind::Float2, glm::vec2(2.0f, 7.0f) });
        props.Properties.push_back(ResolvedMaterialProperty{ { "MaterialParameters", "UVOffset" }, UniformKind::Float2, glm::vec2(-0.25f, 0.75f) });

        EngineMaterialValues engValues;
        auto packResult = PackMaterialUniforms(plan, engValues, props);
        MAT_TEST_CHECK(std::holds_alternative<PackedMaterialResultData>(packResult), "PackMaterialUniforms failed");

        const auto& packedData = std::get<PackedMaterialResultData>(packResult);
        MAT_TEST_CHECK(packedData.Buffers.size() == 1, "Expected 1 packed buffer");
        const auto& buf = packedData.Buffers[0].Bytes;
        MAT_TEST_CHECK(buf.size() == 64, "Expected buffer size of 64 bytes");

        // Validate Gain at 0: 0.5f
        float gainVal = 0.0f;
        std::memcpy(&gainVal, buf.data() + 0, 4);
        MAT_TEST_APPROX_EQUAL(gainVal, 0.5f, 1e-5f, "Gain mismatch");

        // Validate Enabled at 4: uint32_t 1
        uint32_t enabledVal = 0;
        std::memcpy(&enabledVal, buf.data() + 4, 4);
        MAT_TEST_CHECK(enabledVal == 1, "Enabled bool32 mismatch");

        // Validate Padding at 8..15: 8 zero bytes
        for (size_t i = 8; i < 16; ++i) {
            MAT_TEST_CHECK(buf[i] == 0, "Padding byte not zero");
        }

        // Validate Tint at 16: exactly 12 bytes [2.0, 0.25, 1.0]
        float tintVal[3]{};
        std::memcpy(tintVal, buf.data() + 16, 12);
        MAT_TEST_APPROX_EQUAL(tintVal[0], 2.0f, 1e-5f, "Tint X mismatch");
        MAT_TEST_APPROX_EQUAL(tintVal[1], 0.25f, 1e-5f, "Tint Y mismatch");
        MAT_TEST_APPROX_EQUAL(tintVal[2], 1.0f, 1e-5f, "Tint Z mismatch");

        // Validate Mode at 28: int32_t -2
        int32_t modeVal = 0;
        std::memcpy(&modeVal, buf.data() + 28, 4);
        MAT_TEST_CHECK(modeVal == -2, "Mode mismatch");

        // Validate Accent at 32: [0.125, 0.5, 4.0, 0.75]
        float accentVal[4]{};
        std::memcpy(accentVal, buf.data() + 32, 16);
        MAT_TEST_APPROX_EQUAL(accentVal[0], 0.125f, 1e-5f, "Accent R mismatch");
        MAT_TEST_APPROX_EQUAL(accentVal[1], 0.5f, 1e-5f, "Accent G mismatch");
        MAT_TEST_APPROX_EQUAL(accentVal[2], 4.0f, 1e-5f, "Accent B HDR mismatch");
        MAT_TEST_APPROX_EQUAL(accentVal[3], 0.75f, 1e-5f, "Accent A mismatch");

        // Validate UVScale at 48: [2.0, 7.0]
        float uvScale[2]{};
        std::memcpy(uvScale, buf.data() + 48, 8);
        MAT_TEST_APPROX_EQUAL(uvScale[0], 2.0f, 1e-5f, "UVScale X mismatch");
        MAT_TEST_APPROX_EQUAL(uvScale[1], 7.0f, 1e-5f, "UVScale Y mismatch");

        // Validate UVOffset at 56: [-0.25, 0.75]
        float uvOffset[2]{};
        std::memcpy(uvOffset, buf.data() + 56, 8);
        MAT_TEST_APPROX_EQUAL(uvOffset[0], -0.25f, 1e-5f, "UVOffset X mismatch");
        MAT_TEST_APPROX_EQUAL(uvOffset[1], 0.75f, 1e-5f, "UVOffset Y mismatch");

        return true;
    }

    // Isolated Float3 write into sentinel-filled storage
    bool TestIsolatedFloat3SentinelStorage() {
        MaterialBindingPlan plan;
        ConstantBufferPlan cbp;
        cbp.Name = "TestBuf";
        cbp.LogicalSize = 32;
        plan.Buffers.push_back(cbp);
        plan.Uniforms.push_back(UniformBinding{ 0, 16, 12, UniformKind::Float3, PropertyKey{ "TestBuf", "Vec3Val" } });

        ResolvedMaterialProperties props;
        props.Properties.push_back(ResolvedMaterialProperty{ { "TestBuf", "Vec3Val" }, UniformKind::Float3, glm::vec3(1.0f, 2.0f, 3.0f) });

        auto packResult = PackMaterialUniforms(plan, EngineMaterialValues{}, props);
        MAT_TEST_CHECK(std::holds_alternative<PackedMaterialResultData>(packResult), "Packing failed");

        const auto& packedData = std::get<PackedMaterialResultData>(packResult);
        const auto& bytes = packedData.Buffers[0].Bytes;
        MAT_TEST_CHECK(bytes.size() == 32, "Buffer size mismatch");

        // Bytes 28..31 must be untouched (zero bytes)
        for (size_t i = 28; i < 32; ++i) {
            MAT_TEST_CHECK(bytes[i] == 0, "Bytes 28..31 must remain untouched");
        }

        return true;
    }

    // Required negative cases
    bool TestPackingNegativeCases() {
        // 1. Float4 at offset 56 in 64-byte buffer returns OutOfBounds (56 + 16 = 72 > 64)
        {
            MaterialBindingPlan plan;
            plan.Buffers.push_back(ConstantBufferPlan{ "Buf", { 0, 0 }, 64 });
            plan.Uniforms.push_back(UniformBinding{ 0, 56, 16, UniformKind::Float4, PropertyKey{ "Buf", "Overflow" } });

            ResolvedMaterialProperties props;
            props.Properties.push_back(ResolvedMaterialProperty{ { "Buf", "Overflow" }, UniformKind::Float4, glm::vec4(1.0f) });

            auto packResult = PackMaterialUniforms(plan, EngineMaterialValues{}, props);
            MAT_TEST_CHECK(std::holds_alternative<MaterialBindingError>(packResult), "Expected OutOfBounds error");
            const auto& err = std::get<MaterialBindingError>(packResult);
            MAT_TEST_CHECK(err.Code == "OutOfBounds", "Expected OutOfBounds error code");
        }

        // 2. Offset > buffer size is rejected before subtracting
        {
            MaterialBindingPlan plan;
            plan.Buffers.push_back(ConstantBufferPlan{ "Buf", { 0, 0 }, 64 });
            plan.Uniforms.push_back(UniformBinding{ 0, 128, 4, UniformKind::Float32, PropertyKey{ "Buf", "PastEnd" } });

            ResolvedMaterialProperties props;
            props.Properties.push_back(ResolvedMaterialProperty{ { "Buf", "PastEnd" }, UniformKind::Float32, 1.0f });

            auto packResult = PackMaterialUniforms(plan, EngineMaterialValues{}, props);
            MAT_TEST_CHECK(std::holds_alternative<MaterialBindingError>(packResult), "Expected OutOfBounds error");
        }

        // 3. User property targeting u_Model rejected as EngineUniformCollision
        {
            RHI::RHIShaderReflection refl;
            ValidatedShaderInterface iface;
            std::vector<ShaderPropertyDesc> props;
            ShaderPropertyDesc p;
            p.Name = "u_Model"; // collision!
            props.push_back(p);

            auto planResult = BuildMaterialBindingPlan(refl, iface, props);
            MAT_TEST_CHECK(std::holds_alternative<MaterialBindingError>(planResult), "Expected EngineUniformCollision");
            const auto& err = std::get<MaterialBindingError>(planResult);
            MAT_TEST_CHECK(err.Code == "EngineUniformCollision", "Expected EngineUniformCollision error code");
        }

        // 4. Reflected Int with saved Float uses default policy without bitcast
        {
            MaterialBindingPlan plan;
            plan.Buffers.push_back(ConstantBufferPlan{ "Params", { 0, 0 }, 16 });
            plan.Uniforms.push_back(UniformBinding{ 0, 0, 4, UniformKind::Int32, PropertyKey{ "Params", "Mode" } });

            std::vector<ShaderPropertyInstance> saved;
            ShaderPropertyInstance inst;
            inst.Desc.Name = "Mode";
            inst.Desc.Type = ShaderPropertyType::Float; // Incompatible type
            inst.FloatVal = 3.14159f;
            saved.push_back(inst);

            std::vector<ShaderPropertyDesc> declared;
            ShaderPropertyDesc desc;
            desc.Name = "Mode";
            desc.Type = ShaderPropertyType::Int;
            desc.DefaultInt = 42;
            declared.push_back(desc);

            auto resolveResult = ResolveMaterialProperties(plan, saved, declared);
            MAT_TEST_CHECK(std::holds_alternative<ResolvedMaterialProperties>(resolveResult), "Resolve failed");
            const auto& res = std::get<ResolvedMaterialProperties>(resolveResult);
            MAT_TEST_CHECK(res.Properties.size() == 1, "Expected 1 resolved property");
            int32_t val = std::get<int32_t>(res.Properties[0].Value);
            MAT_TEST_CHECK(val == 42, "Incompatible type must receive declared default without bit-cast");
        }

        return true;
    }

    // Matrix column-major multiplication convention test
    bool TestMatrixConvention() {
        MaterialBindingPlan plan;
        plan.Buffers.push_back(ConstantBufferPlan{ "EngineObject", { 0, 0 }, 64 });
        plan.Uniforms.push_back(UniformBinding{ 0, 0, 64, UniformKind::Matrix4x4, EngineUniform::Model });

        EngineMaterialValues engValues;
        // Asymmetric matrix:
        // Column 0: (1, 2, 3, 4)
        // Column 1: (5, 6, 7, 8)
        // Column 2: (9, 10, 11, 12)
        // Column 3: (13, 14, 15, 16)
        engValues.Model = glm::mat4(
            glm::vec4(1.0f, 2.0f, 3.0f, 4.0f),
            glm::vec4(5.0f, 6.0f, 7.0f, 8.0f),
            glm::vec4(9.0f, 10.0f, 11.0f, 12.0f),
            glm::vec4(13.0f, 14.0f, 15.0f, 16.0f)
        );

        ResolvedMaterialProperties props;
        auto packResult = PackMaterialUniforms(plan, engValues, props);
        MAT_TEST_CHECK(std::holds_alternative<PackedMaterialResultData>(packResult), "Matrix pack failed");

        const auto& packedData = std::get<PackedMaterialResultData>(packResult);
        const auto& bytes = packedData.Buffers[0].Bytes;
        MAT_TEST_CHECK(bytes.size() == 64, "Matrix buffer must be 64 bytes");

        // Verify scalar sequence is 1 through 16 in memory
        float scalars[16]{};
        std::memcpy(scalars, bytes.data(), 64);
        for (int i = 0; i < 16; ++i) {
            MAT_TEST_APPROX_EQUAL(scalars[i], static_cast<float>(i + 1), 1e-5f, "Matrix column-major order mismatch");
        }

        // Multiply by (1, 0, 0, 1):
        // result = 1 * Col0 + 0 * Col1 + 0 * Col2 + 1 * Col3 = (1+13, 2+14, 3+15, 4+16) = (14, 16, 18, 20)
        glm::vec4 v(1.0f, 0.0f, 0.0f, 1.0f);
        glm::vec4 res = engValues.Model * v;
        MAT_TEST_APPROX_EQUAL(res.x, 14.0f, 1e-5f, "Matrix mult X mismatch");
        MAT_TEST_APPROX_EQUAL(res.y, 16.0f, 1e-5f, "Matrix mult Y mismatch");
        MAT_TEST_APPROX_EQUAL(res.z, 18.0f, 1e-5f, "Matrix mult Z mismatch");
        MAT_TEST_APPROX_EQUAL(res.w, 20.0f, 1e-5f, "Matrix mult W mismatch");

        return true;
    }

    // Vertex input plan fixture (Section 15.5)
    bool TestVertexInputPlanFixture() {
        std::vector<RHI::RHIVertexAttribute> attrs;
        attrs.push_back(RHI::RHIVertexAttribute{ "POSITION", 0, 0, 3 });
        attrs.push_back(RHI::RHIVertexAttribute{ "NORMAL", 0, 1, 3 });
        attrs.push_back(RHI::RHIVertexAttribute{ "TEXCOORD", 0, 2, 2 });

        auto planResult = BuildMaterialVertexInputPlan(attrs);
        MAT_TEST_CHECK(std::holds_alternative<VertexInputPlan>(planResult), "Vertex plan failed");

        const auto& plan = std::get<VertexInputPlan>(planResult);
        MAT_TEST_CHECK(plan.Stride == 64, "Expected stride 64");
        MAT_TEST_CHECK(plan.Attributes.size() == 3, "Expected 3 attributes");

        // POSITION at offset 0
        MAT_TEST_CHECK(plan.Attributes[0].Offset == 0, "Position offset mismatch");
        MAT_TEST_CHECK(plan.Attributes[0].BackendLocation == 0, "Position location mismatch");

        // NORMAL at offset 12
        MAT_TEST_CHECK(plan.Attributes[1].Offset == 12, "Normal offset mismatch");
        MAT_TEST_CHECK(plan.Attributes[1].BackendLocation == 1, "Normal location mismatch");

        // TEXCOORD at offset 40, backend location 2
        MAT_TEST_CHECK(plan.Attributes[2].Offset == 40, "TexCoord offset mismatch");
        MAT_TEST_CHECK(plan.Attributes[2].BackendLocation == 2, "TexCoord location mismatch");

        // Unsupported semantic check
        std::vector<RHI::RHIVertexAttribute> invalidAttrs = attrs;
        invalidAttrs.push_back(RHI::RHIVertexAttribute{ "BLENDINDICES", 0, 3, 4 });
        auto invalidResult = BuildMaterialVertexInputPlan(invalidAttrs);
        MAT_TEST_CHECK(std::holds_alternative<MaterialBindingError>(invalidResult), "Expected UnsupportedSemantic");
        const auto& err = std::get<MaterialBindingError>(invalidResult);
        MAT_TEST_CHECK(err.Code == "UnsupportedSemantic", "Error code must be UnsupportedSemantic");

        return true;
    }

    bool TestCustomShaderInterfaceRecognition() {
        // 1. Canonical interface
        {
            RHI::RHIShaderReflection refl;
            RHI::RHIShaderConstantBuffer objCb;
            objCb.Name = "EngineObject";
            objCb.BindPoint = 0; objCb.Space = 0; objCb.Size = 96;
            objCb.Variables.push_back({ "u_Model", 0, 64 });
            objCb.Variables.push_back({ "u_BaseColor", 64, 16 });
            objCb.Variables.push_back({ "u_BaseUV", 80, 16 });
            refl.ConstantBuffers.push_back(objCb);

            RHI::RHIShaderConstantBuffer scnCb;
            scnCb.Name = "EngineScene";
            scnCb.BindPoint = 1; scnCb.Space = 0; scnCb.Size = 80;
            scnCb.Variables.push_back({ "u_ViewProjection", 0, 64 });
            scnCb.Variables.push_back({ "u_CameraPosition", 64, 16 });
            refl.ConstantBuffers.push_back(scnCb);

            MAT_TEST_CHECK(CustomShaderInterface::DetectInterface(refl) == CustomInterfaceType::Canonical, "Must detect Canonical");
            auto val = CustomShaderInterface::Validate(refl);
            MAT_TEST_CHECK(val.IsValid, "Canonical interface validation must succeed");
        }

        // 2. Legacy template interface
        {
            RHI::RHIShaderReflection refl;
            RHI::RHIShaderConstantBuffer objCb;
            objCb.Name = "ObjectConstants";
            objCb.BindPoint = 0; objCb.Space = 0; objCb.Size = 96;
            objCb.Variables.push_back({ "u_Model", 0, 64 });
            objCb.Variables.push_back({ "u_BaseColor", 64, 16 });
            objCb.Variables.push_back({ "u_Params", 80, 16 });
            refl.ConstantBuffers.push_back(objCb);

            MAT_TEST_CHECK(CustomShaderInterface::DetectInterface(refl) == CustomInterfaceType::LegacyTemplate, "Must detect LegacyTemplate");
            auto val = CustomShaderInterface::Validate(refl);
            MAT_TEST_CHECK(val.IsValid, "Legacy template interface validation must succeed");
        }

        // 3. Unknown interface
        {
            RHI::RHIShaderReflection refl;
            RHI::RHIShaderConstantBuffer ranCb;
            ranCb.Name = "RandomBuffer";
            ranCb.BindPoint = 0; ranCb.Space = 0; ranCb.Size = 64;
            refl.ConstantBuffers.push_back(ranCb);

            MAT_TEST_CHECK(CustomShaderInterface::DetectInterface(refl) == CustomInterfaceType::Unknown, "Must detect Unknown");
            auto val = CustomShaderInterface::Validate(refl);
            MAT_TEST_CHECK(!val.IsValid, "Unknown interface validation must fail");
        }

        // 4. Canonical template text check
        {
            std::string templ = CustomShaderInterface::GetCanonicalTemplate("MyTestShader");
            MAT_TEST_CHECK(templ.find("EngineObject") != std::string::npos, "Template must contain EngineObject");
            MAT_TEST_CHECK(templ.find("EngineScene") != std::string::npos, "Template must contain EngineScene");
            MAT_TEST_CHECK(templ.find("MaterialParameters") != std::string::npos, "Template must contain MaterialParameters");
            MAT_TEST_CHECK(templ.find("space1") != std::string::npos, "Template must use space1 for textures");
        }

        return true;
    }

    bool TestVulkanBindingCollisionDetection() {
        // Collision case: b0 at space 0, and t0 at space 0
        {
            RHI::RHIShaderReflection refl;
            RHI::RHIShaderConstantBuffer cb;
            cb.Name = "ObjectConstants";
            cb.BindPoint = 0;
            cb.Space = 0;
            refl.ConstantBuffers.push_back(cb);

            RHI::RHIShaderResourceBinding res;
            res.Name = "u_BaseMap";
            res.Type = RHI::RHIResourceType::Texture;
            res.BindPoint = 0;
            res.Space = 0; // collision!
            refl.Resources.push_back(res);

            std::string details;
            MAT_TEST_CHECK(CustomShaderInterface::HasVulkanBindingCollision(refl, details), "Must detect collision when cb and tex share space and register");
            MAT_TEST_CHECK(!details.empty(), "Collision details must be non-empty");
        }

        // Clean canonical case: b0 at space 0, and t0 at space 1
        {
            RHI::RHIShaderReflection refl;
            RHI::RHIShaderConstantBuffer cb;
            cb.Name = "EngineObject";
            cb.BindPoint = 0;
            cb.Space = 0;
            refl.ConstantBuffers.push_back(cb);

            RHI::RHIShaderResourceBinding res;
            res.Name = "u_BaseMap";
            res.Type = RHI::RHIResourceType::Texture;
            res.BindPoint = 0;
            res.Space = 1; // Different space -> No collision!
            refl.Resources.push_back(res);

            std::string details;
            MAT_TEST_CHECK(!CustomShaderInterface::HasVulkanBindingCollision(refl, details), "Canonical interface with separate spaces must not collide");
        }

        return true;
    }

} // namespace

bool RunMaterialBindingPlanUnitTests() {
    {
        const std::string legacy="// register(t9, space0)\ncbuffer ObjectConstants { float4x4 u_Model; };\nTexture2D Base:register(t0, space0);\nTexture2D Other:register(t1);\nSamplerState S:register(s0, space0);\n";
        auto normalized=CustomShaderInterface::NormalizeLegacyBindings(legacy);
        MAT_TEST_CHECK(normalized.find("// register(t9, space0)")!=std::string::npos,"Legacy adapter leaves comments intact");
        MAT_TEST_CHECK(normalized.find("Base:register(t0, space1)")!=std::string::npos && normalized.find("Other:register(t1, space1)")!=std::string::npos,"Legacy textures receive a distinct namespace");
        MAT_TEST_CHECK(normalized.find("S:register(s0, space0)")!=std::string::npos,"Sampler binding remains consistent with device offsets");
    }
    {
        RHI::RHIShaderReflection reflection;
        RHI::RHIShaderConstantBuffer buffer{"MaterialParameters",2,0,32,{}};
        buffer.Variables.push_back({"Tint",0,16,RHI::RHIShaderScalarType::Float,1,4});
        buffer.Variables.push_back({"Enabled",16,4,RHI::RHIShaderScalarType::UInt});
        buffer.Variables.push_back({"Reserved",20,4,RHI::RHIShaderScalarType::Float});
        reflection.ConstantBuffers.push_back(buffer);
        auto planned=BuildMaterialBindingPlan(reflection,ValidatedShaderInterface{},{});
        MAT_TEST_CHECK(std::holds_alternative<MaterialBindingPlan>(planned),"Typed reflection creates a complete plan without guessed property types");
        auto plan=std::get<MaterialBindingPlan>(planned);
        ShaderPropertyInstance tint,enabled;
        tint.Desc.Name="Tint"; tint.Desc.Type=ShaderPropertyType::Color; tint.Vec4Val={2,0.25f,1,0.75f};
        enabled.Desc.Name="Enabled"; enabled.Desc.Type=ShaderPropertyType::Int; enabled.IntVal=1;
        std::vector<ShaderPropertyInstance> values{tint,enabled};
        auto resolved=ResolveMaterialProperties(plan,values);
        auto packed=PackMaterialUniforms(plan,EngineMaterialValues{},std::get<ResolvedMaterialProperties>(resolved));
        MAT_TEST_CHECK(std::holds_alternative<PackedMaterialResultData>(packed),"Production-style reflected values pack successfully");
        const auto& bytes=std::get<PackedMaterialResultData>(packed).Buffers[0].Bytes;
        float red=0; uint32_t enabledValue=0;
        std::memcpy(&red,bytes.data(),4); std::memcpy(&enabledValue,bytes.data()+16,4);
        MAT_TEST_CHECK(red==2 && enabledValue==1,"Tint HDR value and uint enabled survive the complete binding path");
        MAT_TEST_CHECK(plan.Uniforms.size()==2,"Reserved fields are not editable");
        auto duplicate=reflection.ConstantBuffers[0]; duplicate.Name="Other"; duplicate.BindPoint=3;
        reflection.ConstantBuffers.push_back(duplicate);
        MAT_TEST_CHECK(std::holds_alternative<MaterialBindingError>(BuildMaterialBindingPlan(reflection,{},{})),"Ambiguous user names are rejected");
        reflection.ConstantBuffers.pop_back();
        reflection.Resources.push_back({"Output",RHI::RHIResourceType::StorageTexture,0,1,0,RHI::RHIShaderStage::Fragment});
        MAT_TEST_CHECK(std::holds_alternative<MaterialBindingError>(BuildMaterialBindingPlan(reflection,{},{})),"Unsupported storage resources are rejected");
    }
    std::cout << "  - Running TestExact64BytePackingFixture ...\n";
    if (!TestExact64BytePackingFixture()) return false;

    std::cout << "  - Running TestIsolatedFloat3SentinelStorage ...\n";
    if (!TestIsolatedFloat3SentinelStorage()) return false;

    std::cout << "  - Running TestPackingNegativeCases ...\n";
    if (!TestPackingNegativeCases()) return false;

    std::cout << "  - Running TestMatrixConvention ...\n";
    if (!TestMatrixConvention()) return false;

    std::cout << "  - Running TestVertexInputPlanFixture ...\n";
    if (!TestVertexInputPlanFixture()) return false;

    std::cout << "  - Running TestCustomShaderInterfaceRecognition ...\n";
    if (!TestCustomShaderInterfaceRecognition()) return false;

    std::cout << "  - Running TestVulkanBindingCollisionDetection ...\n";
    if (!TestVulkanBindingCollisionDetection()) return false;

    return true;
}
