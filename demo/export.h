#pragma once

#if defined(_WIN32)
#  if defined(VT_DEMO_EXPORT)
#    define VT_DEMO_API __declspec(dllexport)
#  else
#    define VT_DEMO_API __declspec(dllimport)
#  endif
#else
#  define VT_DEMO_API
#endif
