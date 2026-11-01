cmake_minimum_required(VERSION 3.18)
project(Variant VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 所有可执行文件 / DLL 输出到同一个 bin/<Config>，互相找得到。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(VT_BUILD_TESTS "Build unit tests" ON)
option(VT_BUILD_BENCH "Build QVariant comparison benchmark" ON)
option(VT_BUILD_PYTHON "Build shiboken bindings and Python demo" ON)

# 本机 Python / shiboken6（见知识库）
set(VT_PYTHON_ROOT "C:/Users/Zz/AppData/Local/Programs/Python/Python39" CACHE PATH "Python 3.9 root")

if(VT_BUILD_PYTHON)
    if(WIN32)
        set(Python_ROOT_DIR "${VT_PYTHON_ROOT}" CACHE PATH "Python root" FORCE)
        set(Python_LIBRARY_DEBUG "${VT_PYTHON_ROOT}/libs/python39.lib"
            CACHE FILEPATH "Override debug Python library" FORCE)
    endif()
    list(APPEND CMAKE_PREFIX_PATH
        "${VT_PYTHON_ROOT}/Lib/site-packages/shiboken6/lib/cmake"
        "${VT_PYTHON_ROOT}/Lib/site-packages/shiboken6_generator/lib/cmake"
    )
    find_package(Shiboken6 REQUIRED)
    find_package(Shiboken6Tools REQUIRED)
    if(WIN32)
        set(Python_LIBRARY_DEBUG "${VT_PYTHON_ROOT}/libs/python39.lib"
            CACHE FILEPATH "Override debug Python library (post-find)" FORCE)
    endif()
endif()

# 本机 Qt5（见知识库）。没有 Qt 就跳过 bench。
if(EXISTS "D:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5")
    list(APPEND CMAKE_PREFIX_PATH "D:/Qt/5.15.2/msvc2019_64")
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
