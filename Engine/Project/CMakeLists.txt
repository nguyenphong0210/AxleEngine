add_library(AxleProject STATIC
    src/Project.cpp
    src/ProjectSerializer.cpp
)
add_library(Axle::Project ALIAS AxleProject)

target_include_directories(AxleProject PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(AxleProject
    PUBLIC
        Axle::Core
)
