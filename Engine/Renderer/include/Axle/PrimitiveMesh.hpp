#pragma once
#include <Axle/Types.hpp>
#include <Axle/VertexArray.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace Axle {

    struct MeshInfo {
        std::shared_ptr<VertexArray> VAO;
        glm::vec3 HalfExtent{ 0.5f, 0.5f, 0.5f };
        glm::vec3 BoundingSize{ 1.0f, 1.0f, 1.0f };
    };

    class PrimitiveMesh {
    public:
        // Unit cube: size 1.0 x 1.0 x 1.0 (from -0.5 to +0.5)
        static std::shared_ptr<VertexArray> CreateCube();

        // Unit sphere: radius 1.0 centered at origin
        static std::shared_ptr<VertexArray> CreateSphere(u32 stacks = 24, u32 sectors = 32);

        // Unit cylinder: radius 1.0, half-height 1.0 (height 2.0 along Y from -1.0 to +1.0)
        static std::shared_ptr<VertexArray> CreateCylinder(u32 segments = 32);

        // Unit cone: base radius 1.0 at Y = -1.0, apex at Y = +1.0 (height 2.0 along Y)
        static std::shared_ptr<VertexArray> CreateCone(u32 segments = 32);

        // Unit horizontal plane: centered at origin on XZ plane from -0.5 to +0.5 (size 1.0 x 1.0 at Y = 0)
        static std::shared_ptr<VertexArray> CreatePlane(u32 resolutionX = 1, u32 resolutionZ = 1);

        // Load Wavefront .obj 3D model from disk
        static std::shared_ptr<VertexArray> LoadOBJ(const std::string& filepath);
        static MeshInfo LoadOBJEx(const std::string& filepath, bool centerGeometry = true);
    };

}
