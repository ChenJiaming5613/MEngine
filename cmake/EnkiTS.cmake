# enkiTS task scheduler (Raptor chapter 3): CPU work of asset loading and, later, parallel command recording.
set(MENGINE_ENKITS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/enkiTS")

add_library(enkiTS STATIC "${MENGINE_ENKITS_DIR}/src/TaskScheduler.cpp")
target_include_directories(enkiTS SYSTEM PUBLIC "${MENGINE_ENKITS_DIR}/src")
find_package(Threads REQUIRED)
target_link_libraries(enkiTS PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(enkiTS PRIVATE /W0)
else()
    target_compile_options(enkiTS PRIVATE -w)
endif()
