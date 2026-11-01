#pragma once

// 共享所有权的绑定胶水：只被 typesystem 的 inject-code 调用，
// Python 用户看到的是真实签名 `makeSharedNode` / `borrowSharedNode`。
#if defined(Py_LIMITED_API)
#undef Py_LIMITED_API
#endif

#include <Python.h>

#include <string>

/**
 * @brief 创建 shared_ptr 管理的 SharedNode，返回带 capsule 的 Python wrapper。
 * @param [in] int id
 * @param [in] std::string 名字
 * @return PyObject* 新引用；失败返回 nullptr
 */
PyObject* makeSharedNodeToPython(int id, const std::string& name);

/**
 * @brief 返回一个由 C++ 静态容器持有、只借给 Python 的 SharedNode（无 capsule）。
 *
 * 用于验证“借用对象不能进入共享模型”。
 * @param [in] int id
 * @param [in] std::string 名字
 * @return PyObject* 新引用；失败返回 nullptr
 */
PyObject* borrowSharedNodeToPython(int id, const std::string& name);
