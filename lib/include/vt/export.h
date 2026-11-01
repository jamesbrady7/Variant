#pragma once

// 导出宏。构建 shared 库时由 CMake 定义 VT_CORE_EXPORT。
#if defined(_WIN32)
#  if defined(VT_CORE_EXPORT)
#    define VT_API __declspec(dllexport)
#  else
#    define VT_API __declspec(dllimport)
#  endif
#else
#  define VT_API
#endif
