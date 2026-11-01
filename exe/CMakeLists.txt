add_executable(variant_demo_app main.cpp)
add_dependencies(variant_demo_app variant_binding variant_core variant_demo)

target_link_libraries(variant_demo_app PRIVATE
    variant_core
    variant_demo
    "${VT_PYTHON_ROOT}/libs/python39.lib"
)

target_include_directories(variant_demo_app PRIVATE
    ${VT_PYTHON_ROOT}/include
    ${CMAKE_SOURCE_DIR}/lib/include
)

target_compile_definitions(variant_demo_app PRIVATE
    VT_PYTHON_HOME="${VT_PYTHON_ROOT}"
    VT_SCRIPT_DIR="${CMAKE_SOURCE_DIR}/py_scripts"
)

target_compile_options(variant_demo_app PRIVATE
    /utf-8
    "$<$<CONFIG:Debug>:/UPy_LIMITED_API>"
)

# 把 .pyd 与运行期 DLL 复制到 exe 旁边。
add_custom_command(TARGET variant_demo_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:variant_binding>" "$<TARGET_FILE_DIR:variant_demo_app>/variant_binding.pyd"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:variant_core>" "$<TARGET_FILE_DIR:variant_demo_app>/variant_core.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:variant_demo>" "$<TARGET_FILE_DIR:variant_demo_app>/variant_demo.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VT_PYTHON_ROOT}/python39.dll" "$<TARGET_FILE_DIR:variant_demo_app>/python39.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VT_PYTHON_ROOT}/python3.dll" "$<TARGET_FILE_DIR:variant_demo_app>/python3.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VT_PYTHON_ROOT}/Lib/site-packages/shiboken6/shiboken6.abi3.dll"
        "$<TARGET_FILE_DIR:variant_demo_app>/shiboken6.abi3.dll"
    COMMENT "Copying pyd and runtime DLLs next to variant_demo_app"
)
