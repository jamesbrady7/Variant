add_executable(vt_tests unit_tests.cpp)
target_link_libraries(vt_tests PRIVATE variant_core)

if(MSVC)
    target_compile_options(vt_tests PRIVATE /utf-8 /W4 /permissive-)
endif()

if(MSVC)
    target_compile_options(vt_tests PRIVATE /wd4251 /wd4275)
endif()

add_executable(vt_serial_tests serialization_tests.cpp)
target_link_libraries(vt_serial_tests PRIVATE variant_core)

if(MSVC)
    target_compile_options(vt_serial_tests PRIVATE /utf-8 /W4 /permissive- /wd4251 /wd4275)
endif()

# 内嵌 Python 的目标：把 .pyd 与运行期 DLL 复制到 exe 旁边。
function(vt_copy_python_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:variant_binding>" "$<TARGET_FILE_DIR:${target}>/variant_binding.pyd"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:variant_core>" "$<TARGET_FILE_DIR:${target}>/variant_core.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:variant_demo>" "$<TARGET_FILE_DIR:${target}>/variant_demo.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VT_PYTHON_ROOT}/python39.dll" "$<TARGET_FILE_DIR:${target}>/python39.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VT_PYTHON_ROOT}/python3.dll" "$<TARGET_FILE_DIR:${target}>/python3.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VT_PYTHON_ROOT}/Lib/site-packages/shiboken6/shiboken6.abi3.dll"
            "$<TARGET_FILE_DIR:${target}>/shiboken6.abi3.dll"
        COMMENT "Copying pyd and runtime DLLs next to ${target}")
endfunction()

# 共享所有权测试：C++ runner 启动 Python 脚本
if(TARGET variant_binding)
    add_executable(vt_shared_tests shared_ownership_test.cpp)
    add_dependencies(vt_shared_tests variant_binding variant_core variant_demo)
    target_link_libraries(vt_shared_tests PRIVATE
        variant_core
        variant_demo
        "${VT_PYTHON_ROOT}/libs/python39.lib")
    target_include_directories(vt_shared_tests PRIVATE
        "${VT_PYTHON_ROOT}/include"
        ${CMAKE_SOURCE_DIR}/lib/include)
    target_compile_definitions(vt_shared_tests PRIVATE
        VT_PYTHON_HOME="${VT_PYTHON_ROOT}"
        VT_SCRIPT_DIR="${CMAKE_SOURCE_DIR}/py_scripts")
    target_compile_options(vt_shared_tests PRIVATE
        /utf-8
        "$<$<CONFIG:Debug>:/UPy_LIMITED_API>")

    vt_copy_python_runtime(vt_shared_tests)

    # Python 继承 C++ 多态类 + VariantMap 接收 shared_ptr + 虚函数派发。
    # 需要直接调用 vpy::fromPython，因此链接 variant_python_bridge。
    add_executable(vt_plugin_override_test plugin_override_test.cpp)
    add_dependencies(vt_plugin_override_test variant_binding variant_core variant_demo)
    target_link_libraries(vt_plugin_override_test PRIVATE
        variant_core
        variant_demo
        variant_python_bridge
        "${VT_PYTHON_ROOT}/libs/python39.lib")
    target_include_directories(vt_plugin_override_test PRIVATE
        "${VT_PYTHON_ROOT}/include")
    target_compile_definitions(vt_plugin_override_test PRIVATE
        VT_PYTHON_HOME="${VT_PYTHON_ROOT}"
        VT_SCRIPT_DIR="${CMAKE_SOURCE_DIR}/py_scripts")
    target_compile_options(vt_plugin_override_test PRIVATE
        /utf-8
        "$<$<CONFIG:Debug>:/UPy_LIMITED_API>")
    vt_copy_python_runtime(vt_plugin_override_test)
endif()
