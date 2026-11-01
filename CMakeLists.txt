cmake_minimum_required(VERSION 3.22)
project(Variant VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 所有可执行文件 / DLL 输出到同一个 bin/<Config>，互相找得到。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(VT_BUILD_TESTS "Build unit tests" ON)
option(VT_BUILD_BENCH "Build QVariant comparison benchmark" ON)
option(VT_BUILD_PYTHON "Build shiboken bindings and Python demo" ON)

# 必须在任何 add_subdirectory 之前：子目录（exe/tests）都要注册 CTest 用例。
if(VT_BUILD_TESTS)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# Python + shiboken（只有 VT_BUILD_PYTHON 才需要）
#
# 换机器只需 -DVT_PYTHON_ROOT=<Python 根目录>（含 python.exe / include / libs）。
# 不传时用 find_package(Python3) 自动探测；库名与 DLL 名由版本自动推导，
# 不再写死 python39。
# ---------------------------------------------------------------------------
set(VT_PYTHON_ROOT "" CACHE PATH "Python root (contains python.exe, include/, libs/)")

if(VT_BUILD_PYTHON)
    if(NOT VT_PYTHON_ROOT)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            get_filename_component(VT_PYTHON_ROOT "${Python3_EXECUTABLE}" DIRECTORY)
            message(STATUS "VT_PYTHON_ROOT auto-detected: ${VT_PYTHON_ROOT}")
        endif()
    endif()
    if(NOT VT_PYTHON_ROOT)
        message(FATAL_ERROR
            "VT_BUILD_PYTHON=ON but Python was not found. "
            "Pass -DVT_PYTHON_ROOT=<python root> or set PATH.")
    endif()

    if(WIN32)
        set(VT_PYTHON_EXECUTABLE "${VT_PYTHON_ROOT}/python.exe")
    else()
        set(VT_PYTHON_EXECUTABLE "${VT_PYTHON_ROOT}/bin/python3")
    endif()

    # pythonXY：从解释器本身取，避免手写版本号。
    execute_process(
        COMMAND "${VT_PYTHON_EXECUTABLE}" -c
                "import sys; print(f'{sys.version_info.major}{sys.version_info.minor}')"
        OUTPUT_VARIABLE VT_PYTHON_ABI
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE VT_PYTHON_ABI_RESULT)
    if(NOT VT_PYTHON_ABI_RESULT EQUAL 0 OR VT_PYTHON_ABI STREQUAL "")
        message(FATAL_ERROR "Cannot run ${VT_PYTHON_EXECUTABLE} to detect the Python version")
    endif()

    # 这些路径始终由 VT_PYTHON_ROOT 推导（不用 CACHE：换 Python 根目录时
    # 缓存的旧版本路径必须跟着变，否则会链接到上一个解释器的库）。
    set(VT_PYTHON_SITELIB "${VT_PYTHON_ROOT}/Lib/site-packages")
    if(WIN32)
        set(VT_PYTHON_LIBRARY "${VT_PYTHON_ROOT}/libs/python${VT_PYTHON_ABI}.lib")
        set(VT_PYTHON_DLL "${VT_PYTHON_ROOT}/python${VT_PYTHON_ABI}.dll")
        # Windows 官方 Python 不带 pythonXY_d.lib；Debug 配置复用 release 库。
        set(Python_LIBRARY_DEBUG "${VT_PYTHON_LIBRARY}"
            CACHE FILEPATH "Official Windows Python has no debug library" FORCE)
    else()
        set(VT_PYTHON_LIBRARY "${Python3_LIBRARIES}")
        set(VT_PYTHON_DLL "")
    endif()

    # Shiboken6Tools 内部 find_dependency(Python ...)，先把它钉到同一个解释器。
    set(Python_EXECUTABLE "${VT_PYTHON_EXECUTABLE}" CACHE FILEPATH "Python for shiboken" FORCE)
    set(Python_ROOT_DIR "${VT_PYTHON_ROOT}" CACHE PATH "Python root for shiboken" FORCE)

    list(APPEND CMAKE_PREFIX_PATH
        "${VT_PYTHON_SITELIB}/shiboken6/lib/cmake"
        "${VT_PYTHON_SITELIB}/shiboken6_generator/lib/cmake"
    )
    find_package(Shiboken6 REQUIRED)
    find_package(Shiboken6Tools REQUIRED)
endif()

# ---------------------------------------------------------------------------
# 可选 Qt5：只有 benchmark 需要。传 -DVT_QT_PREFIX=<Qt 根目录>，或自行提供
# CMAKE_PREFIX_PATH；都找不到时 bench 目标自动跳过。
# ---------------------------------------------------------------------------
set(VT_QT_PREFIX "" CACHE PATH "Qt5 prefix (optional, e.g. E:/Qt/5.15.2/msvc2019_64)")
if(VT_QT_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${VT_QT_PREFIX}")
endif()

add_subdirectory(lib)
add_subdirectory(demo)

if(VT_BUILD_PYTHON)
    add_subdirectory(binding)
    add_subdirectory(exe)
endif()

if(VT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(VT_BUILD_BENCH)
    add_subdirectory(bench)
endif()
