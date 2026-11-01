find_package(Qt5 COMPONENTS Core QUIET)
if(NOT Qt5_FOUND)
    message(STATUS "Qt5 not found - vt_bench skipped")
    return()
endif()

add_executable(vt_bench bench.cpp)
target_link_libraries(vt_bench PRIVATE variant_core Qt5::Core)

if(MSVC)
    target_compile_options(vt_bench PRIVATE /utf-8 /W4 /permissive-)
endif()

if(MSVC)
    target_compile_options(vt_bench PRIVATE /wd4251 /wd4275)
endif()
