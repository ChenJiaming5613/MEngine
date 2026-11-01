add_executable(IblBaker
    main.cpp
    Bake.h
    BrdfLut.cpp
    Cubemap.cpp
    Cubemap.h
    Irradiance.cpp
    Ktx1Cube.cpp
    Ktx1Cube.h
    Prefilter.cpp
    SelfTest.cpp
    SelfTest.h
    UeBrdf.h
)
target_link_libraries(IblBaker PRIVATE MEngineAsset)
MEngineSetWarnings(IblBaker)
