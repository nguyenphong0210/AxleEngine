#include <Axle/MaterialGraph.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Package.hpp>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " #value "\n"; return false; } } while (0)

namespace {
    using namespace Axle;
    std::string Fixture;
    std::filesystem::path Workspace;

    std::string& StringProperty(MaterialGraphNode& node, const std::string& name, bool attribute = false) {
        auto& properties = attribute ? node.Attributes : node.Properties;
        auto found = std::find_if(properties.begin(), properties.end(), [&](const auto& p){return p.Name == name;});
        if (found == properties.end()) throw std::runtime_error("Test property not found: " + name);
        return std::get<std::string>(found->Value);
    }

    MaterialGraphDocument Sample() {
        MaterialGraphDocument doc; MaterialGraphDiagnostic error;
        if (!MaterialGraph::Parse(Fixture, doc, &error)) throw std::runtime_error(error.Code + ": " + error.Message);
        return doc;
    }

    std::string Serialized(const MaterialGraphDocument& doc) {
        std::string source; MaterialGraphDiagnostic error;
        if (!MaterialGraph::Serialize(doc, source, &error)) throw std::runtime_error(error.Code + ": " + error.Message);
        return source;
    }

    std::string Key(const MaterialGraphDocument& doc) {
        MaterialGraphIR ir; MaterialGraphDiagnostic error;
        if (!MaterialGraph::Validate(doc, ir, &error)) throw std::runtime_error(error.Code + ": " + error.Message);
        return ir.SemanticKey;
    }

    std::string Replace(std::string source, const std::string& before, const std::string& after) {
        const auto start = source.find(before);
        if (start == std::string::npos) throw std::runtime_error("Test replacement not found: " + before);
        source.replace(start, before.size(), after); return source;
    }

    bool ParseError(const std::string& source, const std::string& code) {
        auto live = Sample(); const auto previous = Serialized(live);
        MaterialGraphDiagnostic error;
        CHECK(!MaterialGraph::Parse(source, live, &error));
        CHECK(error.Code == code); CHECK(!error.Message.empty());
        CHECK(error.Source.Line > 0 && error.Source.Column > 0);
        CHECK(Serialized(live) == previous);
        return true;
    }

    bool ValidationError(const std::function<void(MaterialGraphDocument&)>& edit, const std::string& code) {
        auto doc = Sample(); edit(doc);
        MaterialGraphIR ir; ir.SemanticKey = "previous IR"; ir.OutputNode = 4321;
        MaterialGraphDiagnostic error;
        CHECK(!MaterialGraph::Validate(doc, ir, &error)); CHECK(error.Code == code);
        CHECK(ir.SemanticKey == "previous IR" && ir.OutputNode == 4321);
        std::string source = "previous source";
        CHECK(!MaterialGraph::Serialize(doc, source, &error)); CHECK(source == "previous source");
        return true;
    }

    bool ApprovedExample() {
        auto doc = Sample();
        CHECK(doc.Identity == 1234567890123456789ULL && doc.Name == "M_RustMetal");
        CHECK(doc.Parameters.size() == 5 && doc.Nodes.size() == 12 && doc.Connections.size() == 12);
        CHECK(doc.Nodes[0].Source.Line > 1 && doc.Parameters[0].Source.Column > 1);
        CHECK(doc.Nodes[0].EditorPosition[0] == -800 && doc.Nodes[0].EditorPosition[1] == -200);
        CHECK(doc.Parameters[3].Default.Components[0] == 0.90f && doc.Parameters[4].Default.Components[0] == 0.25f);
        CHECK(StringProperty(doc.Nodes[3], "texturePath") == "textures/metal_base_d.png");
        CHECK(doc.Nodes.back().ID == 999 && doc.Nodes.back().Inputs.size() == 6);
        MaterialGraphIR ir; CHECK(MaterialGraph::Validate(doc, ir));
        CHECK(ir.Nodes.size() == 12 && ir.Parameters.size() == 5 && ir.OutputNode == 999);
        CHECK(ir.Nodes.back().ID == 999);
        auto source = Serialized(doc); MaterialGraphDocument second;
        CHECK(MaterialGraph::Parse(source, second)); CHECK(Serialized(second) == source); CHECK(Key(second) == ir.SemanticKey);
        CHECK(std::count(source.begin(), source.end(), '#') == 1);
        auto fresh = MaterialGraph::Create("Fresh"); CHECK(fresh.Identity != 0);
        CHECK(fresh.Nodes.size() == 1 && MaterialGraph::Validate(fresh, ir));
        return true;
    }

    bool LineWrappingAndEscapes() {
        auto base = Sample(); const auto key = Key(base);
        const auto start = Fixture.find("    Parameters {");
        const auto end = Fixture.find("    Graph {", start);
        const std::string prefix = Fixture.substr(0, start), suffix = Fixture.substr(end);
        for (const auto& declaration : std::vector<std::string>{
            "Parameter { default: 0.65, name: \"RustAmount\", type: \"Float\" }",
            "Parameter { name: \"RustAmount\", type: \"Float\",\n default: 0.65 }",
            "Parameter {\nname: \"RustAmount\"\ntype: \"Float\"\ndefault: 0.65\n}"}) {
            const std::string parameters = "Parameters {\n"
                "Parameter {name:\"BaseColorTint\",type:\"Float3\",default:[1,1,1]}\n"
                "Parameter {name:\"UV_Tiling\",type:\"Float2\",default:[2,2]}\n" + declaration +
                "\nParameter {name:\"BaseMetallic\",type:\"Float\",default:0.9}\n"
                "Parameter {name:\"BaseRoughness\",type:\"Float\",default:0.25}\n}\n";
            MaterialGraphDocument doc; CHECK(MaterialGraph::Parse(prefix + parameters + suffix, doc)); CHECK(Key(doc) == key);
        }
        auto doc = Sample(); doc.Name = "Material \"quoted\", [brackets] // /* literal */\nline\t\\end";
        const auto unusual = std::string("parameter \"x\", \\ /\n");
        const auto oldName = doc.Parameters[0].Name; doc.Parameters[0].Name = unusual;
        for (auto& node : doc.Nodes) if (!node.Attributes.empty() && StringProperty(node, "parameter", true) == oldName) StringProperty(node, "parameter", true) = unusual;
        MaterialGraphDocument roundtrip;
        CHECK(MaterialGraph::Parse(Serialized(doc), roundtrip));
        CHECK(roundtrip.Name == doc.Name && roundtrip.Parameters[0].Name == unusual);
        CHECK(Key(roundtrip) == Key(doc));
        CHECK(ValidationError([](auto& d){d.Name += '\0';}, "MG_STRING"));
        return true;
    }

    bool DSLRegression() {
        AssetDocument doc; std::string error;
        const std::string source = R"(Material {
            n: 42, enabled: true, next: -0.25
            path: "string, [a] // keep /* keep */ \"quote\" \\"
            vectors: [[1, 2, 3], [4, 5, 6]]
            strings: ["a,]", "b[//", "c"]
            Child { id: 999, value: 0.5, label: "inline" }
            // ignored closing tokens: } ]
            values: [1, /* } ] */ 2, 3]
        })";
        CHECK(AssetDSL::ParseSource(source, doc, &error));
        CHECK(doc.Root->GetUInt("n") == 42 && doc.Root->GetBool("enabled"));
        CHECK(doc.Root->GetFloat("next") == -0.25f);
        CHECK(doc.Root->GetString("path") == "string, [a] // keep /* keep */ \"quote\" \\");
        CHECK(doc.Root->Children[0]->GetUInt("id") == 999 && doc.Root->Children[0]->GetFloat("value") == 0.5f);
        CHECK(doc.Root->GetProperty("vectors")->AsVec3Array().size() == 2);
        CHECK(doc.Root->GetProperty("values")->AsFloatArray() == std::vector<float>({1,2,3}));
        CHECK(doc.Root->GetProperty("strings")->AsStringArray() == std::vector<std::string>({"a,]", "b[//", "c"}));
        const std::vector<std::string> escapedStrings={"a,b", "quote\"", "back\\slash", "line\nend", ""};
        doc.Root->SetStringArray("escaped", escapedStrings);
        CHECK(doc.Root->GetProperty("escaped")->AsStringArray()==escapedStrings);
        CHECK(AssetDSL::StripComments("1/*comment*/2").find("1  ") == 0);
        const auto serialized = AssetDSL::SerializeDocument(doc); AssetDocument again;
        CHECK(AssetDSL::ParseSource(serialized, again)); CHECK(again.Root->GetString("path") == doc.Root->GetString("path"));
        // Existing supported adjacent properties also remain parseable.
        CHECK(AssetDSL::ParseSource("Material { name: \"m\" roughness: 0.9 }", again));
        CHECK(again.Root->GetFloat("roughness") == 0.9f);
        return true;
    }

    bool SyntaxFailures() {
        AssetDocument live; CHECK(AssetDSL::ParseSource("Material { name: \"previous\" }", live));
        auto previousRoot = live.Root;
        for (const auto& source : std::vector<std::string>{
            "", "Material", "Material {", "Material { n: }", "Material { n: , x: 2 }", "Material { n: [1,2 }",
            "Material { n: [1,] }", "Material { n: [1 2] }", "Material { n: \"open }",
            "Material { n: \"bad\\q\" }", "Material { n: \"bad\nline\" }", "Material { /* open",
            "Material { n: 1, n: 2 }", "Material { n 42 }", "Material { @ }", "Material {} trailing", "Material {} Material {}",
            "Material , { name: \"m\" }", "Material { name, : \"m\" }", "Material { Child, {} }"}) {
            std::string error; AssetSourceLocation location;
            CHECK(!AssetDSL::ParseSource(source, live, &error, &location));
            CHECK(!error.empty() && location.Line > 0 && location.Column > 0); CHECK(live.Root == previousRoot);
        }
        std::string error; AssetSourceLocation location;
        CHECK(!AssetDSL::ParseSource("# identity\nMaterial {\n /* comment */\n @\n}", live, &error, &location));
        CHECK(location.Line == 4 && location.Column == 2 && location.Offset == 38);
        std::string nested = "Material {";
        for (int i=0; i<135; ++i) nested += " Child {";
        nested += std::string(136, '}');
        CHECK(!AssetDSL::ParseSource(nested, live, &error)); CHECK(error.find("128") != std::string::npos);
        CHECK(ParseError(Fixture + "extra", "MG_SYNTAX"));
        return true;
    }

    bool SchemaFailures() {
        CHECK(ParseError(Replace(Fixture, "schemaVersion: 1", "schemaVersion: 2"), "MG_VERSION"));
        CHECK(ParseError(Replace(Fixture, "schemaVersion: 1", "schemaVersion: 4294967297"), "MG_VERSION"));
        CHECK(ParseError(Replace(Fixture, "schemaVersion: 1", "schemaVersion: -1"), "MG_NUMBER"));
        CHECK(ParseError(Replace(Fixture, "schemaVersion: 1", "schemaVersion: 1.0"), "MG_NUMBER"));
        CHECK(ParseError(Replace(Fixture, "schemaVersion: 1", "schemaVersion: 18446744073709551616"), "MG_NUMBER"));
        CHECK(ParseError(Replace(Fixture, "Material {", "Texture {"), "MG_ROOT"));
        CHECK(ParseError(Replace(Fixture, "type: \"Graph\"", "type: \"Unknown\""), "MG_ROOT"));
        CHECK(ParseError(Replace(Fixture, "depthWrite: true", "depthWrite: 1"), "MG_BOOL"));
        CHECK(ParseError(Replace(Fixture, "schemaVersion: 1", "uuid: 42\nschemaVersion: 1"), "MG_FIELD"));
        CHECK(ParseError(Replace(Fixture, "    Parameters {", "    Parameters {}\n    Parameters {"), "MG_BLOCK"));
        CHECK(ParseError(Replace(Fixture, "Connections {", "Connections {}\nConnections {"), "MG_BLOCK"));
        CHECK(ParseError(Replace(Fixture, "name: \"BaseColorTint\"", "id: 1, name: \"BaseColorTint\""), "MG_FIELD"));
        CHECK(ParseError(Replace(Fixture, "parameter: \"UV_Tiling\"", "paramId: 1"), "MG_FIELD"));
        CHECK(ParseError(Replace(Fixture, "default: 0.65", "default: 0.65suffix"), "MG_NUMBER"));
        CHECK(ParseError(Replace(Fixture, "default: 0.65", "default: 1e99"), "MG_NUMBER"));
        CHECK(ParseError(Replace(Fixture, "default: 0.65", "default: NaN"), "MG_NUMBER"));
        CHECK(ParseError(Replace(Fixture, "default: [2.0, 2.0]", "default: [2.0]"), "MG_VALUE"));
        CHECK(ParseError(Replace(Fixture, "from: \"1.uv\"", "from: \"1.uv.extra\""), "MG_ENDPOINT"));
        CHECK(ParseError(Replace(Fixture, "# AxleFileIdentity: 1", "# AxleFileIdentity: 99"), "MG_IDENTITY"));
        CHECK(ParseError(Replace(Fixture, "# AxleFileIdentity: 1", "# AxleFileIdentity: -4294967295"), "MG_IDENTITY"));
        CHECK(ParseError(Replace(Fixture, "1234567890123456789", "0"), "MG_IDENTITY"));
        CHECK(ParseError(Replace(Fixture, "1234567890123456789", "18446744073709551616"), "MG_IDENTITY"));
        CHECK(ParseError(Fixture.substr(Fixture.find('\n')+1), "MG_IDENTITY"));
        return true;
    }

    bool GraphFailures() {
        CHECK(ValidationError([](auto& d){d.Nodes[0].ID = 0;}, "MG_NODE_ID"));
        CHECK(ValidationError([](auto& d){d.Nodes[1].ID = d.Nodes[0].ID;}, "MG_NODE_ID"));
        CHECK(ValidationError([](auto& d){d.Parameters[1].Name = d.Parameters[0].Name;}, "MG_PARAMETER"));
        CHECK(ValidationError([](auto& d){d.Parameters[1].Name.clear();}, "MG_PARAMETER"));
        CHECK(ValidationError([](auto& d){StringProperty(d.Nodes[1], "parameter", true) = "uv_tiling";}, "MG_PARAMETER"));
        CHECK(ValidationError([](auto& d){d.Nodes[1].Outputs[0].Type = MaterialValueType::Float;}, "MG_PIN"));
        CHECK(ValidationError([](auto& d){d.Nodes[0].Type = "InventedNode";}, "MG_NODE"));
        CHECK(ValidationError([](auto& d){d.Nodes[0].Attributes.push_back({"parameter", std::string("UV_Tiling"), {}});}, "MG_NODE"));
        CHECK(ValidationError([](auto& d){d.Nodes[0].Outputs[0].Name = "invented";}, "MG_PIN"));
        CHECK(ValidationError([](auto& d){d.Nodes[0].Inputs = d.Nodes[0].Outputs; d.Nodes[0].Outputs.clear();}, "MG_PIN"));
        CHECK(ValidationError([](auto& d){d.Nodes[0].Outputs[0].Default = MaterialValue{MaterialValueType::Float2, {1,1}};}, "MG_PIN"));
        CHECK(ValidationError([](auto& d){d.Nodes.back().Inputs[0].Default = MaterialValue{MaterialValueType::Float, {1}};}, "MG_VALUE"));
        CHECK(ValidationError([](auto& d){d.Parameters[0].Default.Components[0] = std::numeric_limits<float>::infinity();}, "MG_VALUE"));
        CHECK(ValidationError([](auto& d){d.Nodes.back().Inputs[3].Default.reset();}, "MG_INPUT"));
        CHECK(ValidationError([](auto& d){d.Nodes.pop_back();}, "MG_OUTPUT"));
        CHECK(ValidationError([](auto& d){auto node=d.Nodes.back();node.ID=1000;d.Nodes.push_back(node);}, "MG_OUTPUT"));
        CHECK(ValidationError([](auto& d){d.Connections[0].From.NodeID=99999;}, "MG_ENDPOINT"));
        CHECK(ValidationError([](auto& d){d.Connections[0].From.Pin="a";}, "MG_ENDPOINT"));
        CHECK(ValidationError([](auto& d){std::swap(d.Connections[0].From,d.Connections[0].To);}, "MG_ENDPOINT"));
        CHECK(ValidationError([](auto& d){d.Connections.push_back(d.Connections[0]);}, "MG_CONNECTION"));
        CHECK(ValidationError([](auto& d){d.Connections[0].From={3,"result"};}, "MG_CYCLE"));
        CHECK(ValidationError([](auto& d){
            auto a=d.Nodes[2], b=a; a.ID=80; b.ID=81; d.Nodes.push_back(a); d.Nodes.push_back(b);
            d.Connections.push_back({{80,"result"},{81,"a"},{}}); d.Connections.push_back({{81,"result"},{80,"a"},{}});
        }, "MG_CYCLE"));
        CHECK(ValidationError([](auto& d){d.Nodes[0].EditorPosition[0]=std::numeric_limits<float>::quiet_NaN();}, "MG_VALUE"));
        CHECK(ValidationError([](auto& d){d.ShadingModel="Unknown";}, "MG_SURFACE"));
        CHECK(ValidationError([](auto& d){d.SurfaceType="Transparent";}, "MG_SURFACE"));
        CHECK(ValidationError([](auto& d){d.CullMode="Unknown";}, "MG_SURFACE"));
        return true;
    }

    bool DefaultsAndUnusedData() {
        auto doc=Sample(); doc.Parameters.push_back({"Unused", {MaterialValueType::Float,{0.75f}}, {}});
        auto unused=doc.Nodes[0]; unused.ID=500; doc.Nodes.push_back(unused);
        // Roughness now comes from its authored default; its ParameterRef becomes unused.
        doc.Connections.pop_back();
        MaterialGraphIR ir; CHECK(MaterialGraph::Validate(doc, ir));
        CHECK(ir.Nodes.size()==11 && ir.Parameters.size()==4);
        CHECK(std::none_of(ir.Nodes.begin(),ir.Nodes.end(),[](const auto& n){return n.ID==11 || n.ID==500;}));
        MaterialGraphDocument again; CHECK(MaterialGraph::Parse(Serialized(doc),again));
        CHECK(again.Nodes.size()==13 && again.Parameters.size()==6 && again.Connections.size()==11);
        CHECK(again.Nodes.back().EditorPosition==unused.EditorPosition);
        auto key=Key(doc); doc.Parameters.back().Default.Components[0]=0.33f;
        doc.Nodes.back().EditorPosition={1000,2000}; CHECK(Key(doc)==key);
        // Connected inputs ignore their authored defaults in IR, but keep them on disk.
        auto& connected=doc.Nodes[8].Inputs[0]; connected.Default->Components={0.125f,0.25f,0.5f};
        CHECK(Key(doc)==key); CHECK(MaterialGraph::Parse(Serialized(doc),again));
        CHECK(again.Nodes[8].Inputs[0].Default==connected.Default);
        connected.Default.reset(); CHECK(Key(doc)==key); CHECK(MaterialGraph::Parse(Serialized(doc),again));
        CHECK(!again.Nodes[8].Inputs[0].Default);
        return true;
    }

    bool ConversionsAndPolymorphism() {
        auto doc=Sample(); doc.Connections[9].From={10,"value"};
        MaterialGraphIR ir; CHECK(MaterialGraph::Validate(doc,ir));
        const auto& output=ir.Nodes.back();
        auto base=std::find_if(output.Inputs.begin(),output.Inputs.end(),[](const auto& p){return p.Name=="BaseColor";});
        CHECK(base!=output.Inputs.end() && base->Conversion==MaterialInputConversion::ScalarSplat && !base->Default);
        CHECK(ValidationError([](auto& d){d.Connections[9].From={1,"uv"};},"MG_CONNECTION_TYPE"));
        CHECK(ValidationError([](auto& d){d.Nodes[6].Inputs[2].Type=MaterialValueType::Float3;},"MG_PIN"));
        for (auto type:{MaterialValueType::Float,MaterialValueType::Float2,MaterialValueType::Float3,MaterialValueType::Float4}) {
            auto d=MaterialGraph::Create("Polymorphic");
            MaterialGraphNode n; n.ID=20; n.Type="Multiply";
            const auto width=static_cast<size_t>(type)+1;
            n.Inputs={{"a",type,MaterialValue{type,std::vector<float>(width,1)},{}},
                      {"b",MaterialValueType::Float,MaterialValue{MaterialValueType::Float,{2}}, {}}};
            n.Outputs={{"result",type,{},{}}}; d.Nodes.push_back(n);
            CHECK(MaterialGraph::Validate(d,ir)); CHECK(MaterialGraph::Parse(Serialized(d),doc));
        }
        CHECK(ValidationError([](auto& d){d.Nodes[2].Outputs[0].Type=MaterialValueType::Float4;},"MG_PIN"));
        return true;
    }

    bool ResourceContracts() {
        for (const auto& path:std::vector<std::string>{"", "../escape.png", "textures/../escape.png", "./t.png", "/root/t.png", "C:/t.png",
                                                     "textures\\t.png", "textures//t.png", "textures/", "textures/./t.png", "tex\nture.png"})
            CHECK(ValidationError([&](auto& d){StringProperty(d.Nodes[3], "texturePath")=path;},"MG_RESOURCE_PATH"));
        CHECK(ValidationError([](auto& d){StringProperty(d.Nodes[3], "colorSpace")="Gamma";},"MG_RESOURCE"));
        CHECK(ValidationError([](auto& d){StringProperty(d.Nodes[3], "sampler")="Unspecified";},"MG_RESOURCE"));
        CHECK(ValidationError([](auto& d){d.Nodes[3].Properties.clear();},"MG_RESOURCE_PATH"));
        // Metadata validation does not require texture files to exist before C12-04.
        auto d=Sample(); StringProperty(d.Nodes[3], "texturePath")="textures/missing.png";
        StringProperty(d.Nodes[3], "colorSpace")="Linear"; StringProperty(d.Nodes[3], "sampler")="PointClamp";
        MaterialGraphIR ir; CHECK(MaterialGraph::Validate(d,ir));
        return true;
    }

    bool CanonicalIR() {
        auto doc=Sample(); const auto key=Key(doc);
        std::reverse(doc.Nodes.begin(),doc.Nodes.end()); std::reverse(doc.Parameters.begin(),doc.Parameters.end());
        std::reverse(doc.Connections.begin(),doc.Connections.end());
        for(auto& n:doc.Nodes) {
            n.EditorPosition={42,84}; std::reverse(n.Inputs.begin(),n.Inputs.end()); std::reverse(n.Outputs.begin(),n.Outputs.end());
        }
        doc.Name="Renamed display name"; doc.Identity=987654321; CHECK(Key(doc)==key);
        doc.Parameters[0].Default.Components[0]+=0.1f; CHECK(Key(doc)!=key);
        doc=Sample(); StringProperty(doc.Nodes[3], "colorSpace")="Linear"; CHECK(Key(doc)!=key);
        doc=Sample(); doc.Nodes.back().Inputs[3].Default->Components={0,1,0}; CHECK(Key(doc)!=key);
        doc=Sample(); doc.CullMode="None"; CHECK(Key(doc)!=key);
        return true;
    }

    bool RegisteredNodeExtension() {
        MaterialNodeRegistry registry = MaterialNodeRegistry::Builtins();
        MaterialNodeDefinition definition;
        definition.Type="test.ColorOffset"; definition.Revision=7;
        definition.Signatures={{{{"color", MaterialValueType::Float3, MaterialValue{MaterialValueType::Float3,{0,0,0}}}},
                                {{"result", MaterialValueType::Float3, {}}}}};
        MaterialNodePropertyDefinition amount; amount.Name="amount";
        amount.Kind=MaterialPropertyKind::Number; amount.NumberType=MaterialValueType::Float3;
        MaterialNodePropertyDefinition enabled; enabled.Name="enabled"; enabled.Kind=MaterialPropertyKind::Boolean;
        MaterialNodePropertyDefinition label; label.Name="label"; label.Required=false; label.Default=std::string("unnamed");
        MaterialNodePropertyDefinition mode; mode.Name="mode"; mode.AllowedStrings={"Add", "Subtract"};
        definition.Properties={amount,enabled,label}; definition.Attributes={mode};
        MaterialGraphDiagnostic error;
        CHECK(registry.Register(definition,&error));
        CHECK(!MaterialNodeRegistry::Builtins().Find("test.ColorOffset"));
        CHECK(!registry.Register(definition,&error) && error.Code=="MG_DEFINITION");
        CHECK(registry.Find("test.ColorOffset")->Revision==7);

        auto doc=Sample();
        MaterialGraphNode extension; extension.ID=200; extension.Type=definition.Type;
        extension.Attributes={{"mode",std::string("Add"),{}}};
        extension.Properties={{"amount",MaterialValue{MaterialValueType::Float3,{0.1f,0.2f,0.3f}},{}},{"enabled",true,{}}};
        extension.Inputs={{"color",MaterialValueType::Float3,MaterialValue{MaterialValueType::Float3,{0,0,0}}, {}}};
        extension.Outputs={{"result",MaterialValueType::Float3,{},{}}};
        doc.Nodes.push_back(extension);
        const auto previousSource=doc.Connections[9].From;
        doc.Connections[9].From={200,"result"}; doc.Connections.push_back({previousSource,{200,"color"},{}});
        MaterialGraphIR ir; CHECK(MaterialGraph::Validate(doc,ir,&error,registry));
        const auto custom=std::find_if(ir.Nodes.begin(),ir.Nodes.end(),[](const auto& n){return n.ID==200;});
        CHECK(custom!=ir.Nodes.end() && custom->DefinitionRevision==7 && custom->Properties.size()==3);
        CHECK(custom->Attributes[0].first=="mode" && std::get<std::string>(custom->Attributes[0].second)=="Add");
        CHECK(doc.Nodes.back().Properties.size()==2); // Optional schema defaults do not rewrite authoring data.
        const auto originalKey=ir.SemanticKey;
        std::string text; CHECK(MaterialGraph::Serialize(doc,text,&error,registry));
        MaterialGraphDocument roundtrip; CHECK(MaterialGraph::Parse(text,roundtrip,&error,registry));
        CHECK(roundtrip.Nodes.back().Attributes.size()==1 && roundtrip.Nodes.back().Properties.size()==2);
        CHECK(MaterialGraph::Validate(roundtrip,ir,&error,registry) && ir.SemanticKey==originalKey);
        CHECK(!MaterialGraph::Parse(text,roundtrip,&error) && error.Code=="MG_NODE");
        const auto path=Workspace/"CustomGraph.axleasset";
        CHECK(MaterialGraph::Save(path,doc,&error,registry)); CHECK(MaterialGraph::Load(path,roundtrip,&error,registry));

        // Every path receives the explicit registry; unknown nodes cannot bypass validation.
        std::string protectedText="previous";
        CHECK(!MaterialGraph::Serialize(doc,protectedText,&error) && protectedText=="previous");
        const auto bytes=FileSystem::ReadBinaryFile(path);
        CHECK(!MaterialGraph::Save(path,doc,&error) && FileSystem::ReadBinaryFile(path)==bytes);
        auto invalid=doc; invalid.Nodes.back().Properties[0].Value=MaterialValue{MaterialValueType::Float,{1}};
        CHECK(!MaterialGraph::Validate(invalid,ir,&error,registry) && error.Code=="MG_PROPERTY");
        invalid=doc; invalid.Nodes.back().Attributes[0].Value=std::string("Invented");
        CHECK(!MaterialGraph::Validate(invalid,ir,&error,registry) && error.Code=="MG_PROPERTY");
        invalid=doc; invalid.Nodes.back().Properties.pop_back();
        CHECK(!MaterialGraph::Validate(invalid,ir,&error,registry) && error.Field=="enabled");
        invalid=doc; invalid.Nodes.back().Properties.push_back(invalid.Nodes.back().Properties.front());
        CHECK(!MaterialGraph::Validate(invalid,ir,&error,registry) && error.Code=="MG_PROPERTY");

        // Registry copies own schemas; changing a node implementation revision changes IR identity.
        MaterialNodeRegistry next=MaterialNodeRegistry::Builtins(); definition.Revision=8;
        CHECK(next.Register(definition)); CHECK(MaterialGraph::Validate(doc,ir,&error,next)); CHECK(ir.SemanticKey!=originalKey);
        CHECK(MaterialGraph::Validate(doc,ir,&error,registry) && ir.SemanticKey==originalKey);

        // Parameter references are definition metadata, not a ParameterRef name check.
        MaterialNodeDefinition alias; alias.Type="test.ParameterAlias"; alias.ParameterAttribute="key";
        MaterialNodePropertyDefinition key; key.Name="key"; alias.Attributes={key};
        alias.Signatures={{{},{{"value",std::nullopt,{}}}}}; CHECK(registry.Register(alias));
        auto parameterDoc=Sample(); parameterDoc.Nodes[1].Type=alias.Type; parameterDoc.Nodes[1].Attributes[0].Name="key";
        CHECK(MaterialGraph::Validate(parameterDoc,ir,&error,registry) && ir.Parameters.size()==5);
        CHECK(MaterialGraph::Serialize(parameterDoc,text,&error,registry)); CHECK(MaterialGraph::Parse(text,roundtrip,&error,registry));
        return true;
    }

    bool RegistryRejectsInvalidDefinitions() {
        MaterialNodeDefinition valid; valid.Type="test.Valid"; valid.Signatures={{{},{{"value",MaterialValueType::Float,{}}}}};
        MaterialNodeRegistry registry;
        const std::vector<std::function<void(MaterialNodeDefinition&)>> edits={
            [](auto& d){d.Type.clear();}, [](auto& d){d.Revision=0;}, [](auto& d){d.Signatures.clear();},
            [](auto& d){d.Signatures[0].Outputs[0].Type.reset();},
            [](auto& d){d.Signatures[0].Outputs[0].Name="dot.pin";},
            [](auto& d){d.Signatures[0].Outputs.push_back(d.Signatures[0].Outputs[0]);},
            [](auto& d){d.Signatures.push_back(d.Signatures[0]);},
            [](auto& d){
                d.Signatures[0].Outputs.push_back({"other",MaterialValueType::Float2,{}});
                auto reordered=d.Signatures[0];std::reverse(reordered.Outputs.begin(),reordered.Outputs.end());d.Signatures.push_back(reordered);
            },
            [](auto& d){
                MaterialNodePropertyDefinition key;key.Name="key";d.Attributes={key};d.ParameterAttribute="key";
                auto wildcard=d.Signatures[0];wildcard.Outputs[0].Type.reset();d.Signatures.push_back(wildcard);
            },
            [](auto& d){d.ParameterAttribute="missing";},
            [](auto& d){d.Role=MaterialNodeRole::SurfaceOutput;},
            [](auto& d){MaterialNodePropertyDefinition p;p.Name="type";d.Attributes.push_back(p);},
            [](auto& d){MaterialNodePropertyDefinition p;p.Name="value";p.Default=std::string("bad required default");d.Properties.push_back(p);},
            [](auto& d){MaterialNodePropertyDefinition p;p.Name="value";p.Kind=MaterialPropertyKind::Boolean;p.AllowedStrings={"no"};d.Properties.push_back(p);},
            [](auto& d){MaterialNodePropertyDefinition p;p.Name="value";p.Kind=MaterialPropertyKind::Boolean;p.Required=false;p.Default=std::string("wrong kind");d.Properties.push_back(p);}
        };
        for(const auto& edit:edits) {
            auto bad=valid; edit(bad); MaterialGraphDiagnostic error;
            CHECK(!registry.Register(bad,&error) && error.Code=="MG_DEFINITION"); CHECK(!registry.Find("test.Valid"));
        }
        CHECK(registry.Register(valid)); CHECK(registry.Find("test.Valid"));
        return true;
    }

    struct CommaPunctuation:std::numpunct<char> { char do_decimal_point() const override { return ','; } };
    bool NumericRoundTrips() {
        auto doc=Sample();
        const auto previous=std::locale();
        struct Restore { std::locale Previous; ~Restore(){std::locale::global(Previous);} } restore{previous};
        std::locale::global(std::locale(previous,new CommaPunctuation));
        for (float value:{std::numeric_limits<float>::max(),std::numeric_limits<float>::min(),
                         std::numeric_limits<float>::denorm_min(),-0.0f,0.123456791f}) {
            doc.Parameters[4].Default.Components[0]=value;
            doc.Nodes[0].EditorPosition={value,value};
            const auto source=Serialized(doc); MaterialGraphDocument again;
            CHECK(MaterialGraph::Parse(source,again)); CHECK(again.Parameters[4].Default.Components[0]==value);
            CHECK(again.Nodes[0].EditorPosition[0]==value); CHECK(Key(again)==Key(doc));
        }
        doc=Sample(); const auto max=std::numeric_limits<uint64_t>::max(); doc.Identity=max; doc.Nodes.back().ID=max;
        for(auto& connection:doc.Connections) if(connection.To.NodeID==999) connection.To.NodeID=max;
        MaterialGraphDocument again; CHECK(MaterialGraph::Parse(Serialized(doc),again));
        CHECK(again.Identity==max && again.Nodes.back().ID==max);
        return true;
    }

    bool PersistenceAndRollback() {
        const auto path=Workspace/std::filesystem::path(L"Material_ki\u1ec3m_tra.axleasset");
        auto doc=Sample(); MaterialGraphDiagnostic error;
        CHECK(MaterialGraph::Save(path,doc,&error));
        uint64_t identity=0; CHECK(FileSystem::ReadFileIdentity(path,identity) && identity==doc.Identity);
        doc.Name="Updated"; CHECK(MaterialGraph::Save(path,doc));
        const auto bytes=FileSystem::ReadBinaryFile(path);
        auto different=MaterialGraph::Create("Different asset"); CHECK(!MaterialGraph::Save(path,different,&error));
        CHECK(error.Code=="MG_IO" && FileSystem::ReadBinaryFile(path)==bytes);
        auto invalid=doc; invalid.Connections[0].From.Pin="missing";
        CHECK(!MaterialGraph::Save(path,invalid,&error)); CHECK(FileSystem::ReadBinaryFile(path)==bytes);
        MaterialGraphDocument loaded; CHECK(MaterialGraph::Load(path,loaded)); CHECK(Serialized(loaded)==Serialized(doc));
        const auto live=Serialized(loaded);
        CHECK(!MaterialGraph::Load(Workspace/"missing.axleasset",loaded,&error)); CHECK(error.Code=="MG_IO"); CHECK(Serialized(loaded)==live);
        auto badPath=Workspace/"bad.axleasset";
        CHECK(FileSystem::WriteTextFileAtomically(badPath,"# AxleFileIdentity: 99 123\nMaterial {}\n"));
        CHECK(!MaterialGraph::Load(badPath,loaded,&error)); CHECK(error.Code=="MG_IDENTITY" && Serialized(loaded)==live);
        CHECK(!MaterialGraph::Save(badPath,doc,&error)); CHECK(FileSystem::ReadTextFile(badPath).find("99 123")!=std::string::npos);
        auto directory=Workspace/"directory.axleasset"; std::filesystem::create_directories(directory);
        CHECK(!MaterialGraph::Save(directory,doc,&error)); CHECK(std::filesystem::is_directory(directory));
#ifdef _WIN32
        HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        CHECK(lock!=INVALID_HANDLE_VALUE);
        const bool saved=MaterialGraph::Save(path,doc,&error); CloseHandle(lock);
        CHECK(!saved && error.Code=="MG_IO" && FileSystem::ReadBinaryFile(path)==bytes);
#endif
        for (const auto& entry:std::filesystem::directory_iterator(Workspace)) {
            const auto extension=entry.path().extension();
            CHECK(extension!=".meta" && extension!=".uid" && extension!=".import" && extension!=".axleregistry");
        }
        return true;
    }

    bool PackagedLoad() {
        auto doc=Sample(); auto text=Serialized(doc);
        const auto package=Workspace/"Graph.axlepak";
        PackageWriter writer; CHECK(writer.Begin(package));
        CHECK(writer.AddData("materials/Graph.axleasset",text.data(),text.size(),Axle::UUID(doc.Identity)));
        CHECK(writer.Finish());
        FileSystem::SetAssetDirectory(Workspace/"absent-assets"); CHECK(FileSystem::MountPackage(package));
        struct Unmount { ~Unmount(){FileSystem::UnmountPackage();} } unmount;
        MaterialGraphDocument loaded; CHECK(MaterialGraph::Load("materials/Graph.axleasset",loaded));
        CHECK(loaded.Identity==doc.Identity && Key(loaded)==Key(doc));
        return true;
    }
}

int main(int argc,char** argv) {
    if(argc!=3) {std::cerr<<"Expected fixture and writable build workspace\n";return 2;}
    Fixture=Axle::FileSystem::ReadTextFile(std::filesystem::u8path(argv[1]));
    Workspace=std::filesystem::u8path(argv[2])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(Workspace);
    Axle::FileSystem::SetAssetDirectory(Workspace);
    const std::vector<std::pair<const char*,std::function<bool()>>> cases={
        {"approved example",ApprovedExample},{"line wrapping and escapes",LineWrappingAndEscapes},
        {"AssetDSL consumers",DSLRegression},{"syntax failure rollback",SyntaxFailures},{"schema rejection",SchemaFailures},
        {"graph rejection and IR rollback",GraphFailures},{"defaults and unused preservation",DefaultsAndUnusedData},
        {"conversions and polymorphism",ConversionsAndPolymorphism},{"resource metadata",ResourceContracts},
        {"canonical IR",CanonicalIR},{"numeric precision and locale",NumericRoundTrips},
        {"registered node extension",RegisteredNodeExtension},{"invalid node definitions",RegistryRejectsInvalidDefinitions},
        {"identity-safe persistence rollback",PersistenceAndRollback},{"packaged native load",PackagedLoad}};
    unsigned failures=0;
    for(const auto& [name,run]:cases) {
        try {if(run()) std::cout<<"PASS "<<name<<"\n";else {++failures;std::cerr<<"FAIL "<<name<<"\n";}}
        catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<"\n";}
    }
    std::cout<<cases.size()-failures<<"/"<<cases.size()<<" passed\n";
    return failures?1:0;
}
