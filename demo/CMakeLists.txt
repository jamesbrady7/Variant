add_library(variant_demo SHARED
    demo_types.cpp
    variant_demo_api.cpp
)

target_include_directories(variant_demo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(variant_demo PUBLIC variant_core)
target_compile_definitions(variant_demo PRIVATE VT_DEMO_EXPORT)

if(MSVC)
    target_compile_options(variant_demo PRIVATE /utf-8 /W4 /permissive- /wd4251 /wd4275)
endif()
