#pragma once

// 内建容器的 Python 转换（不依赖 shiboken SpecificConverter）。
#if defined(Py_LIMITED_API)
#undef Py_LIMITED_API
#endif

#include <Python.h>

#include "vt/variant.h"

namespace vpy {

/**
 * @brief 注册内建容器的 C++ -> Python hook。
 *
 * 覆盖 vector<int>、vector<string>、map<string,string>、map<int,int>。
 * @return bool 是否全部注册成功
 */
bool registerBuiltinContainerHooks();

} // namespace vpy

/**
 * @brief 演示/测试：Python list -> Variant -> getAs<vector<int>> 求和。
 * @param [in] PyObject* Python 序列
 * @return int 和；失败返回 -1 并设置 Python 异常
 */
int sumInts(PyObject* value);

/**
 * @brief 演示/测试：Python dict[int,int] -> Variant -> getAs<map<int,int>> 取大小。
 * @param [in] PyObject* Python 字典
 * @return int 元素个数；失败返回 -1 并设置 Python 异常
 */
int intMapSize(PyObject* value);
