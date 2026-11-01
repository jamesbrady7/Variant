# ---------------------------------------------------------------------------
# variant_python_core：生产内核（Variant/VariantMap/VariantList <-> Python）
#   只依赖 variant_core + libshiboken，不含任何 demo 类型。
#   部署清单见 binding/core/README.md。
# ---------------------------------------------------------------------------
add_library(variant_python_core STATIC
    core/variant_python.cpp
    core/container_hooks.cpp
)

target_include_directories(variant_python_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/core
    PRIVATE ${VT_PYTHON_ROOT}/include
)
target_link_libraries(variant_python_core PUBLIC variant_core Shiboken6::libshiboken)
target_compile_options(variant_python_core PRIVATE
    /utf-8
    "$<$<CONFIG:Debug>:/UPy_LIMITED_API>"
)

# ---------------------------------------------------------------------------
# variant_python_bridge：演示专用（DemoNode/DemoColor/SharedNode/PluginBase
#   注册 + 共享所有权工厂胶水）。生产环境不要链这个目标。
# ---------------------------------------------------------------------------
add_library(variant_python_bridge STATIC
    demo/demo_registration.cpp
    demo/shared_bridge.cpp
)

target_include_directories(variant_python_bridge
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/demo
            ${CMAKE_CURRENT_SOURCE_DIR}/core
    PRIVATE ${VT_PYTHON_ROOT}/include
)
target_link_libraries(variant_python_bridge PUBLIC variant_python_core variant_demo)
target_compile_options(variant_python_bridge PRIVATE
    /utf-8
    "$<$<CONFIG:Debug>:/UPy_LIMITED_API>"
)

# shiboken 生成的 wrapper 列表（类名小写 + _wrapper.cpp）。
set(VARIANT_GEN
    ${CMAKE_CURRENT_BINARY_DIR}/variant_binding/variant_binding_module_wrapper.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/variant_binding/demonode_wrapper.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/variant_binding/democolor_wrapper.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/variant_binding/sharednode_wrapper.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/variant_binding/pluginbase_wrapper.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/variant_binding/pluginrequest_wrapper.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/variant_binding/pluginresult_wrapper.cpp)

shiboken_generator_create_binding(
    EXTENSION_TARGET variant_binding
    LIBRARY_TARGET variant_core
    TYPESYSTEM_FILE ${CMAKE_CURRENT_SOURCE_DIR}/variant_bindings.xml
    HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/core/container_hooks.h
        ${CMAKE_CURRENT_SOURCE_DIR}/demo/shared_bridge.h
        ${CMAKE_SOURCE_DIR}/demo/demo_types.h
        ${CMAKE_SOURCE_DIR}/demo/variant_demo_api.h
    GENERATED_SOURCES ${VARIANT_GEN}
    SHIBOKEN_EXTRA_OPTIONS
        -I${CMAKE_SOURCE_DIR}/lib/include
        -I${CMAKE_SOURCE_DIR}/demo
        -I${CMAKE_CURRENT_SOURCE_DIR}/core
        -I${CMAKE_CURRENT_SOURCE_DIR}/demo
        -I${VT_PYTHON_ROOT}/include
)

target_link_libraries(variant_binding PRIVATE variant_demo variant_python_bridge)

# 生成的 wrapper 按名字链接 python39.lib，需要显式给出库目录。
if(WIN32)
    target_link_directories(variant_binding PRIVATE "${VT_PYTHON_ROOT}/libs")
    target_link_libraries(variant_binding PRIVATE "${VT_PYTHON_ROOT}/libs/python39.lib")
endif()
target_include_directories(variant_binding PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/demo
)
target_compile_options(variant_binding PRIVATE
    /utf-8
    "$<$<CONFIG:Debug>:/UPy_LIMITED_API>"
)

# 生成的 wrapper 不一定 include 业务头，强制包含（与 ShiboKenTest-GLM 同法）。
if(MSVC)
    target_compile_options(variant_binding PRIVATE
        "/FI${CMAKE_SOURCE_DIR}/lib/include/vt/variant.h"
        "/FI${CMAKE_SOURCE_DIR}/binding/core/variant_python.h"
        "/FI${CMAKE_SOURCE_DIR}/binding/core/container_hooks.h"
        "/FI${CMAKE_SOURCE_DIR}/binding/demo/shared_bridge.h"
        "/FI${CMAKE_SOURCE_DIR}/demo/demo_types.h"
        "/FI${CMAKE_SOURCE_DIR}/demo/variant_demo_api.h"
    )
endif()
