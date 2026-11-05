add_library(AxleCore STATIC
    src/IllegalCoreDependency.cpp
)
target_link_libraries(AxleCore
    PUBLIC
        Axle::Scene
        Axle::UI
)
