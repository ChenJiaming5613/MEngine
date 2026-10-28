add_executable(IblBaker
    main.cpp
    Bake.hpp
    BrdfLut.cpp
    Cubemap.cpp
    Cubemap.hpp
    Irradiance.cpp
    Prefilter.cpp
    SelfTest.cpp
    SelfTest.hpp
    UeBrdf.hpp
)
target_link_libraries(IblBaker PRIVATE MEngineAsset)
MEngineSetWarnings(IblBaker)
