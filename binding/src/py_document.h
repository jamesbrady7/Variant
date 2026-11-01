#pragma once

// 绑定专用门面：Python 侧只看到 PyDocument，签名里不出现 vt::Variant。
// 原因（实证）：
//   1. shiboken6 的 Python 签名解析器无法识别 "vt.Variant"，参数不匹配时 fatal；
//   2. 自动绑定的方法不支持 PyObject* 参数/返回值（会生成空转换）。
// 因此 class 只保留 shiboken 能自动处理的签名，跨语言值通过自由函数 + PyObject 直通。

// 必须在包含 Python.h 之前抹掉 Py_LIMITED_API，否则 shiboken 头里的
// PyBufferProcs 等符号不可见。
#if defined(Py_LIMITED_API)
#undef Py_LIMITED_API
#endif

#include <Python.h>

#include <string>

#include "vt/document.h"

/**
 * @brief Python 侧文档门面。
 *
 * 只暴露 shiboken 能安全自动绑定的操作；`get/set` 由自由函数以 PyObject 直通实现。
 */
class PyDocument {
public:
    /// @brief 构造空文档。
    PyDocument() = default;

    /// @brief 析构。
    ~PyDocument() = default;

    PyDocument(const PyDocument&) = delete;             ///< 禁止拷贝
    PyDocument& operator=(const PyDocument&) = delete;  ///< 禁止拷贝赋值

    /**
     * @brief 路径是否存在。
     * @param [in] std::string 路径
     * @return bool 是否存在
     */
    bool has(const std::string& path) const;

    /**
     * @brief 最近一次失败原因。
     * @return std::string 错误文本
     */
    std::string lastError() const;

    /// @brief 清空文档与错误信息。
    void clear();

private:
    friend PyObject* pyDocGet(const PyDocument* doc, const std::string& path);
    friend bool pyDocSet(PyDocument* doc, const std::string& path, PyObject* value);

    VariantDocument m_doc;          ///< 底层文档
    std::string m_conversionError;  ///< 值转换失败时的错误文本
};

/**
 * @brief 按路径读取；返回 Python 原生对象。
 * @param [in] const PyDocument* 文档
 * @param [in] std::string 路径
 * @return PyObject* 新引用；失败返回 nullptr 并设置 Python 异常
 */
PyObject* pyDocGet(const PyDocument* doc, const std::string& path);

/**
 * @brief 按路径写入；value 由 C++ 侧递归转换。
 * @param [in] PyDocument* 文档
 * @param [in] std::string 路径
 * @param [in] PyObject* Python 值
 * @return bool 是否成功；失败原因见 lastError()
 */
bool pyDocSet(PyDocument* doc, const std::string& path, PyObject* value);
