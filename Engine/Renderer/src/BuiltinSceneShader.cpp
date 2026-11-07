#include <Axle/BuiltinSceneShader.hpp>
#include "SceneShaderSource.hpp"
namespace Axle {
    std::string_view GetBuiltinSceneVertexShader(){return BuiltinSceneSource::Vertex;}
    std::string_view GetBuiltinSceneFragmentShader(){return BuiltinSceneSource::Fragment;}
}
