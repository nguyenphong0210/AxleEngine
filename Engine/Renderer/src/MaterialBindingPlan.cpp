#include <Axle/MaterialBindingPlan.hpp>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace Axle {

    namespace {
        std::string ToLower(std::string str) {
            std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return str;
        }

        bool IsEngineUniformName(const std::string& name, EngineUniform& outUniform, UniformKind& outKind) {
            std::string lower = ToLower(name);
            if (lower == "u_model" || lower == "model") {
                outUniform = EngineUniform::Model;
                outKind = UniformKind::Matrix4x4;
                return true;
            }
            if (lower == "u_viewprojection" || lower == "viewprojection") {
                outUniform = EngineUniform::ViewProjection;
                outKind = UniformKind::Matrix4x4;
                return true;
            }
            if (lower == "u_view" || lower == "view") {
                outUniform = EngineUniform::View;
                outKind = UniformKind::Matrix4x4;
                return true;
            }
            if (lower == "u_projection" || lower == "projection") {
                outUniform = EngineUniform::Projection;
                outKind = UniformKind::Matrix4x4;
                return true;
            }
            if (lower == "u_cameraposition" || lower == "cameraposition") {
                outUniform = EngineUniform::CameraPosition;
                outKind = UniformKind::Float4;
                return true;
            }
            if (lower == "u_sundirection" || lower == "sundirection") {
                outUniform = EngineUniform::SunDirection;
                outKind = UniformKind::Float4;
                return true;
            }
            if (lower == "u_suncolor" || lower == "suncolor") {
                outUniform = EngineUniform::SunColor;
                outKind = UniformKind::Float4;
                return true;
            }
            if (lower == "u_basecolor" || lower == "basecolor") {
                outUniform = EngineUniform::BaseColor;
                outKind = UniformKind::Float4;
                return true;
            }
            if (lower == "u_baseuv" || lower == "baseuv") {
                outUniform = EngineUniform::BaseUVTransform;
                outKind = UniformKind::Float4;
                return true;
            }
            if (lower == "u_params" || lower == "params") {
                outUniform = EngineUniform::LegacySurfaceParams;
                outKind = UniformKind::Float4;
                return true;
            }
            return false;
        }

        UniformKind DeclaredKind(ShaderPropertyType declaredType) {
            switch (declaredType) {
                case ShaderPropertyType::Float:     return UniformKind::Float32;
                case ShaderPropertyType::Float2:    return UniformKind::Float2;
                case ShaderPropertyType::Float3:    return UniformKind::Float3;
                case ShaderPropertyType::Float4:    return UniformKind::Float4;
                case ShaderPropertyType::Color:     return UniformKind::Float4;
                case ShaderPropertyType::Int:       return UniformKind::Int32;
                case ShaderPropertyType::Bool:      return UniformKind::Bool32;
                default: break;
            }
            return UniformKind::Float32;
        }
    }

    bool IsEngineUniform(const std::string& name) {
        EngineUniform engUnif;
        UniformKind engKind;
        return IsEngineUniformName(name, engUnif, engKind);
    }

    uint32_t GetUniformKindSize(UniformKind kind) {
        switch (kind) {
            case UniformKind::Float32:   return 4;
            case UniformKind::Float2:    return 8;
            case UniformKind::Float3:    return 12;
            case UniformKind::Float4:    return 16;
            case UniformKind::Int32:     return 4;
            case UniformKind::Bool32:    return 4;
            case UniformKind::Matrix4x4: return 64;
        }
        return 4;
    }

    BindingPlanResult BuildMaterialBindingPlan(
        const RHI::RHIShaderReflection& mergedReflection,
        const ValidatedShaderInterface& shaderInterface,
        std::span<const ShaderPropertyDesc> declaredUserProperties)
    {
        if (shaderInterface.InterfaceRevision < 1) {
            return MaterialBindingError{ "InvalidInterfaceRevision", "", "", "Shader interface revision must be >= 1" };
        }

        // Check for user properties colliding with reserved engine uniforms
        for (const auto& prop : declaredUserProperties) {
            EngineUniform engUnif;
            UniformKind engKind;
            if (IsEngineUniformName(prop.Name, engUnif, engKind)) {
                return MaterialBindingError{
                    "EngineUniformCollision",
                    prop.ConstantBufferName,
                    prop.Name,
                    "Declared property name '" + prop.Name + "' collides with reserved engine uniform"
                };
            }
        }

        MaterialBindingPlan plan;
        plan.InterfaceRevision = shaderInterface.InterfaceRevision;
        std::unordered_set<std::string> propertyNames;
        std::unordered_set<uint64_t> bufferAddresses;
        for (const auto& resource : mergedReflection.Resources) {
            if (resource.BindCount != 1 ||
                (resource.Type != RHI::RHIResourceType::ConstantBuffer &&
                 resource.Type != RHI::RHIResourceType::Texture && resource.Type != RHI::RHIResourceType::Sampler) ||
                (resource.Type == RHI::RHIResourceType::Texture && resource.TextureDimension != 0 && resource.TextureDimension != 2))
                return MaterialBindingError{"UnsupportedResource",resource.Name,"","Only individual cbuffers, Texture2D and ordinary samplers are supported."};
        }

        // Process Constant Buffers
        for (const auto& cb : mergedReflection.ConstantBuffers) {
            if (!cb.Size || cb.Size > 65536 || !bufferAddresses.insert((uint64_t(cb.Space)<<32)|cb.BindPoint).second)
                return MaterialBindingError{"InvalidBuffer",cb.Name,"","Invalid size or duplicate cbuffer binding."};
            ConstantBufferPlan bufPlan;
            bufPlan.Name = cb.Name;
            bufPlan.Address.RegisterIndex = cb.BindPoint;
            bufPlan.Address.RegisterSpace = cb.Space;
            bufPlan.LogicalSize = cb.Size;

            size_t bufferIndex = plan.Buffers.size();
            plan.Buffers.push_back(bufPlan);

            if (ToLower(cb.Name) == "objectconstants") {
                if (!shaderInterface.AllowLegacyTemplate)
                    return MaterialBindingError{"LegacyDisabled",cb.Name,"","Legacy shader interface is disabled."};
                plan.IsLegacyTemplate = true;
            }

            uint32_t previousEnd = 0;
            for (const auto& var : cb.Variables) {
                if (!var.Size || var.Offset < previousEnd || var.Offset > cb.Size || var.Size > cb.Size-var.Offset || var.Elements)
                    return MaterialBindingError{"InvalidVariable",cb.Name,var.Name,"Overlapping, out-of-bounds or array variable."};
                previousEnd = var.Offset+var.Size;
                if (ToLower(var.Name) == "reserved") continue;
                EngineUniform engUnif;
                UniformKind engKind;
                bool isEngine = IsEngineUniformName(var.Name, engUnif, engKind);

                UniformBinding binding;
                binding.BufferIndex = bufferIndex;
                binding.Offset = var.Offset;
                binding.ReflectedSize = var.Size;

                if (isEngine) {
                    if (var.RowMajor || var.Size != GetUniformKindSize(engKind) ||
                        (var.ScalarType != RHI::RHIShaderScalarType::Unknown && var.ScalarType != RHI::RHIShaderScalarType::Float))
                        return MaterialBindingError{"EngineUniformType",cb.Name,var.Name,"Engine uniform type/layout does not match the custom interface."};
                    binding.Kind = engKind;
                    binding.Source = engUnif;
                } else {
                    if (!propertyNames.insert(var.Name).second)
                        return MaterialBindingError{"AmbiguousProperty",cb.Name,var.Name,"Editable names must be unique across constant buffers."};
                    const ShaderPropertyDesc* declared = nullptr;
                    for (const auto& p : declaredUserProperties) {
                        if (p.Name == var.Name && (p.ConstantBufferName.empty() || p.ConstantBufferName == cb.Name)) {
                            declared = &p;
                            break;
                        }
                    }
                    using Scalar = RHI::RHIShaderScalarType;
                    if (var.ScalarType == Scalar::Float && var.Rows == 1 && var.Columns >= 1 && var.Columns <= 4)
                        binding.Kind = static_cast<UniformKind>(static_cast<int>(UniformKind::Float32)+var.Columns-1);
                    else if ((var.ScalarType == Scalar::Int || var.ScalarType == Scalar::UInt) && var.Rows == 1 && var.Columns == 1)
                        binding.Kind = UniformKind::Int32;
                    else if (var.ScalarType == Scalar::Bool && var.Rows == 1 && var.Columns == 1)
                        binding.Kind = UniformKind::Bool32;
                    else if (var.ScalarType == Scalar::Unknown && declared && declared->Type != ShaderPropertyType::Texture2D)
                        binding.Kind = DeclaredKind(declared->Type); // Compatibility for synthetic/older reflection.
                    else return MaterialBindingError{"UnsupportedType",cb.Name,var.Name,"Missing or unsupported scalar/vector type metadata."};
                    if (GetUniformKindSize(binding.Kind) != var.Size)
                        return MaterialBindingError{"ReflectedStorageMismatch",cb.Name,var.Name,"Declared value width differs from reflection."};
                    ShaderPropertyDesc schema = declared ? *declared : ShaderPropertyDesc{};
                    schema.Name=var.Name; schema.ConstantBufferName=cb.Name; schema.Offset=var.Offset; schema.Size=var.Size;
                    if (!declared || DeclaredKind(schema.Type) != binding.Kind) {
                        switch(binding.Kind) {
                            case UniformKind::Float32: schema.Type=ShaderPropertyType::Float; break;
                            case UniformKind::Float2: schema.Type=ShaderPropertyType::Float2; break;
                            case UniformKind::Float3: schema.Type=ShaderPropertyType::Float3; break;
                            case UniformKind::Float4: schema.Type=ShaderPropertyType::Float4; break;
                            case UniformKind::Int32: schema.Type=ShaderPropertyType::Int; break;
                            case UniformKind::Bool32: schema.Type=ShaderPropertyType::Bool; break;
                            default: break;
                        }
                    }
                    plan.PropertySchema.push_back(schema);
                    binding.Source = PropertyKey{ cb.Name, var.Name };
                }

                plan.Uniforms.push_back(binding);
            }
        }

        // Process Textures
        for (const auto& res : mergedReflection.Resources) {
            if (res.Type == RHI::RHIResourceType::Texture) {
                TextureBinding tb;
                tb.ResourceName = res.Name;
                tb.Address.RegisterIndex = res.BindPoint;
                tb.Address.RegisterSpace = res.Space;

                std::string lower = ToLower(res.Name);
                if (lower == "u_basemap" || lower == "basemap" || lower == "albedomap") {
                    tb.Role = TextureRole::Color;
                } else if (lower == "u_normalmap") {
                    tb.Role = TextureRole::TangentNormal;
                } else {
                    tb.Role = TextureRole::LinearData;
                }

                plan.Textures.push_back(tb);
                if (lower != "u_basemap" && lower != "basemap" && lower != "albedomap") {
                    ShaderPropertyDesc schema;
                    for (const auto& desc : declaredUserProperties) if (desc.Name == res.Name) schema=desc;
                    schema.Name=res.Name; schema.Type=ShaderPropertyType::Texture2D;
                    plan.PropertySchema.push_back(schema);
                }
            }
        }

        // Process Vertex Inputs
        if (!mergedReflection.VertexInputs.empty()) {
            auto vPlanResult = BuildMaterialVertexInputPlan(mergedReflection.VertexInputs);
            if (std::holds_alternative<MaterialBindingError>(vPlanResult)) {
                return std::get<MaterialBindingError>(vPlanResult);
            }
            plan.VertexInput = std::get<VertexInputPlan>(vPlanResult);
        }

        return plan;
    }

    ResolvedPropertyResult ResolveMaterialProperties(
        const MaterialBindingPlan& plan,
        std::span<const ShaderPropertyInstance> savedValues,
        std::span<const ShaderPropertyDesc> declaredUserProperties)
    {
        ResolvedMaterialProperties result;
        if (declaredUserProperties.empty()) declaredUserProperties=plan.PropertySchema;

        for (const auto& unif : plan.Uniforms) {
            if (!std::holds_alternative<PropertyKey>(unif.Source)) continue;
            const auto& key = std::get<PropertyKey>(unif.Source);

            const ShaderPropertyInstance* foundInstance = nullptr;
            for (const auto& inst : savedValues) {
                if (inst.Desc.Name == key.VariableName &&
                    (inst.Desc.ConstantBufferName.empty() || inst.Desc.ConstantBufferName == key.BufferName)) {
                    foundInstance = &inst;
                    break;
                }
            }

            const ShaderPropertyDesc* foundDesc = nullptr;
            for (const auto& desc : declaredUserProperties) {
                if (desc.Name == key.VariableName) {
                    foundDesc = &desc;
                    break;
                }
            }

            ResolvedMaterialProperty prop;
            prop.Key = key;
            prop.Kind = unif.Kind;
            if (foundInstance && (foundInstance->Desc.Type == ShaderPropertyType::Texture2D || DeclaredKind(foundInstance->Desc.Type) != unif.Kind))
                foundInstance=nullptr;

            if (foundInstance) {
                switch (unif.Kind) {
                    case UniformKind::Float32:
                        prop.Value = foundInstance->FloatVal;
                        break;
                    case UniformKind::Float2:
                        prop.Value = glm::vec2(foundInstance->Vec4Val.x, foundInstance->Vec4Val.y);
                        break;
                    case UniformKind::Float3:
                        prop.Value = glm::vec3(foundInstance->Vec4Val.x, foundInstance->Vec4Val.y, foundInstance->Vec4Val.z);
                        break;
                    case UniformKind::Float4:
                        prop.Value = foundInstance->Vec4Val;
                        break;
                    case UniformKind::Int32:
                        if (foundInstance->Desc.Type == ShaderPropertyType::Float) {
                            // Incompatible type: use default without bitcasting
                            prop.Value = foundDesc ? foundDesc->DefaultInt : 0;
                        } else {
                            prop.Value = foundInstance->IntVal;
                        }
                        break;
                    case UniformKind::Bool32:
                        prop.Value = static_cast<uint32_t>(foundInstance->BoolVal ? 1 : 0);
                        break;
                    case UniformKind::Matrix4x4:
                        prop.Value = glm::vec4(0.0f);
                        break;
                }
            } else if (foundDesc) {
                switch (unif.Kind) {
                    case UniformKind::Float32:
                        prop.Value = foundDesc->DefaultFloat;
                        break;
                    case UniformKind::Float2:
                        prop.Value = glm::vec2(foundDesc->DefaultVec4.x, foundDesc->DefaultVec4.y);
                        break;
                    case UniformKind::Float3:
                        prop.Value = glm::vec3(foundDesc->DefaultVec4.x, foundDesc->DefaultVec4.y, foundDesc->DefaultVec4.z);
                        break;
                    case UniformKind::Float4:
                        prop.Value = foundDesc->DefaultVec4;
                        break;
                    case UniformKind::Int32:
                        prop.Value = foundDesc->DefaultInt;
                        break;
                    case UniformKind::Bool32:
                        prop.Value = static_cast<uint32_t>(foundDesc->DefaultBool ? 1 : 0);
                        break;
                    case UniformKind::Matrix4x4:
                        prop.Value = glm::vec4(0.0f);
                        break;
                }
            } else {
                switch (unif.Kind) {
                    case UniformKind::Float32: prop.Value = 0.0f; break;
                    case UniformKind::Float2:  prop.Value = glm::vec2(0.0f); break;
                    case UniformKind::Float3:  prop.Value = glm::vec3(0.0f); break;
                    case UniformKind::Float4:  prop.Value = glm::vec4(0.0f); break;
                    case UniformKind::Int32:   prop.Value = 0; break;
                    case UniformKind::Bool32:  prop.Value = 0u; break;
                    case UniformKind::Matrix4x4: prop.Value = glm::vec4(0.0f); break;
                }
            }

            result.Properties.push_back(prop);
        }

        return result;
    }

    PackedMaterialResult PackMaterialUniforms(
        const MaterialBindingPlan& plan,
        const EngineMaterialValues& engineValues,
        const ResolvedMaterialProperties& properties)
    {
        PackedMaterialResultData result;

        // Allocate CPU byte buffers initialized to zero
        for (const auto& buf : plan.Buffers) {
            PackedBufferData pbd;
            pbd.Name = buf.Name;
            pbd.Address = buf.Address;
            pbd.Bytes.resize(buf.LogicalSize, 0);
            result.Buffers.push_back(pbd);
        }

        for (const auto& unif : plan.Uniforms) {
            if (unif.BufferIndex >= result.Buffers.size()) {
                return MaterialBindingError{ "InvalidBufferIndex", "", "", "Buffer index out of range" };
            }

            auto& targetBuf = result.Buffers[unif.BufferIndex];
            uint32_t bytesToWrite = GetUniformKindSize(unif.Kind);

            // Safe bounds check without overflow
            if (unif.Offset > targetBuf.Bytes.size() || bytesToWrite > targetBuf.Bytes.size() - unif.Offset) {
                return MaterialBindingError{
                    "OutOfBounds",
                    targetBuf.Name,
                    "",
                    "Uniform offset or size out of buffer bounds"
                };
            }

            if (unif.ReflectedSize > 0 && bytesToWrite > unif.ReflectedSize) {
                return MaterialBindingError{
                    "ReflectedStorageMismatch",
                    targetBuf.Name,
                    "",
                    "Bytes to write exceeds reflected variable size"
                };
            }

            uint8_t* dest = targetBuf.Bytes.data() + unif.Offset;

            if (std::holds_alternative<EngineUniform>(unif.Source)) {
                EngineUniform eu = std::get<EngineUniform>(unif.Source);
                switch (eu) {
                    case EngineUniform::Model:
                        std::memcpy(dest, &engineValues.Model, sizeof(glm::mat4));
                        break;
                    case EngineUniform::ViewProjection:
                        std::memcpy(dest, &engineValues.ViewProjection, sizeof(glm::mat4));
                        break;
                    case EngineUniform::View:
                        std::memcpy(dest, &engineValues.View, sizeof(glm::mat4));
                        break;
                    case EngineUniform::Projection:
                        std::memcpy(dest, &engineValues.Projection, sizeof(glm::mat4));
                        break;
                    case EngineUniform::CameraPosition:
                        std::memcpy(dest, &engineValues.CameraPosition, sizeof(glm::vec4));
                        break;
                    case EngineUniform::SunDirection:
                        std::memcpy(dest, &engineValues.SunDirection, sizeof(glm::vec4));
                        break;
                    case EngineUniform::SunColor:
                        std::memcpy(dest, &engineValues.SunColor, sizeof(glm::vec4));
                        break;
                    case EngineUniform::BaseColor:
                        std::memcpy(dest, &engineValues.BaseColor, sizeof(glm::vec4));
                        break;
                    case EngineUniform::BaseUVTransform:
                        std::memcpy(dest, &engineValues.BaseUVTransform, sizeof(glm::vec4));
                        break;
                    case EngineUniform::LegacySurfaceParams:
                        std::memcpy(dest, &engineValues.LegacySurfaceParams, sizeof(glm::vec4));
                        break;
                }
            } else {
                const auto& key = std::get<PropertyKey>(unif.Source);
                const ResolvedMaterialProperty* resolved = nullptr;
                for (const auto& r : properties.Properties) {
                    if (r.Key.VariableName == key.VariableName &&
                        (key.BufferName.empty() || r.Key.BufferName == key.BufferName)) {
                        resolved = &r;
                        break;
                    }
                }

                if (resolved) {
                    switch (unif.Kind) {
                        case UniformKind::Float32: {
                            float v = std::holds_alternative<float>(resolved->Value) ? std::get<float>(resolved->Value) : 0.0f;
                            std::memcpy(dest, &v, sizeof(float));
                            break;
                        }
                        case UniformKind::Float2: {
                            glm::vec2 v = std::holds_alternative<glm::vec2>(resolved->Value) ? std::get<glm::vec2>(resolved->Value) : glm::vec2(0.0f);
                            const float components[]{v.x,v.y};
                            std::memcpy(dest, components, sizeof(components));
                            break;
                        }
                        case UniformKind::Float3: {
                            // Exactly 12 bytes - leaving subsequent bytes intact
                            glm::vec3 v = std::holds_alternative<glm::vec3>(resolved->Value) ? std::get<glm::vec3>(resolved->Value) : glm::vec3(0.0f);
                            const float components[]{v.x,v.y,v.z};
                            std::memcpy(dest, components, sizeof(components));
                            break;
                        }
                        case UniformKind::Float4: {
                            glm::vec4 v = std::holds_alternative<glm::vec4>(resolved->Value) ? std::get<glm::vec4>(resolved->Value) : glm::vec4(0.0f);
                            std::memcpy(dest, &v, sizeof(glm::vec4));
                            break;
                        }
                        case UniformKind::Int32: {
                            int32_t v = std::holds_alternative<int32_t>(resolved->Value) ? std::get<int32_t>(resolved->Value) : 0;
                            std::memcpy(dest, &v, sizeof(int32_t));
                            break;
                        }
                        case UniformKind::Bool32: {
                            uint32_t v = std::holds_alternative<uint32_t>(resolved->Value) ? std::get<uint32_t>(resolved->Value) : 0u;
                            std::memcpy(dest, &v, sizeof(uint32_t));
                            break;
                        }
                        case UniformKind::Matrix4x4:
                            break;
                    }
                }
            }
        }

        return result;
    }

    VertexInputPlanResult BuildMaterialVertexInputPlan(
        std::span<const RHI::RHIVertexAttribute> reflectedInputs)
    {
        VertexInputPlan plan;
        plan.Stride = static_cast<uint32_t>(sizeof(RHI::RHIVertex3D)); // 64 bytes

        for (const auto& attr : reflectedInputs) {
            std::string upper = attr.SemanticName;
            std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return (char)std::toupper(c); });

            VertexAttributeBinding binding;
            binding.SemanticName = attr.SemanticName;
            binding.SemanticIndex = attr.SemanticIndex;
            binding.InputSlot = 0;
            binding.BackendLocation = attr.Register;

            if (upper == "POSITION") {
                binding.Offset = offsetof(RHI::RHIVertex3D, Position);
                binding.Format = nri::Format::RGB32_SFLOAT;
            } else if (upper == "NORMAL") {
                binding.Offset = offsetof(RHI::RHIVertex3D, Normal);
                binding.Format = nri::Format::RGB32_SFLOAT;
            } else if (upper == "TANGENT") {
                binding.Offset = offsetof(RHI::RHIVertex3D, Tangent);
                binding.Format = nri::Format::RGBA32_SFLOAT;
            } else if (upper == "TEXCOORD") {
                binding.Offset = offsetof(RHI::RHIVertex3D, TexCoord);
                binding.Format = nri::Format::RG32_SFLOAT;
            } else if (upper == "COLOR") {
                binding.Offset = offsetof(RHI::RHIVertex3D, Color);
                binding.Format = nri::Format::RGBA32_SFLOAT;
            } else {
                return MaterialBindingError{
                    "UnsupportedSemantic",
                    "",
                    attr.SemanticName,
                    "Unsupported vertex attribute semantic '" + attr.SemanticName + "' in custom material shader"
                };
            }

            plan.Attributes.push_back(binding);
        }

        return plan;
    }

    TextureRequestResult ResolveMaterialTextureRequests(
        const MaterialBindingPlan& plan,
        const EffectiveMaterialValues& materialValues)
    {
        TextureRequestResultData result;

        for (const auto& tb : plan.Textures) {
            TextureRequest req;
            req.ResourceName = tb.ResourceName;
            req.Address = tb.Address;
            req.Role = tb.Role;

            std::string lower = ToLower(tb.ResourceName);
            if (lower == "u_basemap" || lower == "basemap" || lower == "albedomap") {
                req.ResolvedPath = materialValues.UseBaseMap ? materialValues.BaseMapPath : "";
            } else {
                // Look up in custom properties
                for (const auto& p : materialValues.Properties) {
                    if (p.Desc.Name == tb.ResourceName && p.Desc.Type == ShaderPropertyType::Texture2D) {
                        req.ResolvedPath = p.TexturePath;
                        break;
                    }
                }
            }

            result.Requests.push_back(req);
        }

        return result;
    }

} // namespace Axle
