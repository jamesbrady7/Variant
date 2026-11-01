add_library(variant_core SHARED
    src/variant.cpp
    src/variant_map.cpp
    src/variant_list.cpp
    src/path.cpp
    src/type_registry.cpp
    src/document.cpp
    src/debug.cpp
    src/serialize_error.cpp
    src/codec.cpp
    src/binary.cpp
    src/json.cpp
    src/adapters.cpp
)

target_include_directories(variant_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(variant_core PRIVATE VT_CORE_EXPORT)

if(MSVC)
    target_compile_options(variant_core PRIVATE /utf-8 /W4 /permissive-)
endif()

if(MSVC)
    target_compile_options(variant_core PRIVATE /wd4251 /wd4275)
endif()
