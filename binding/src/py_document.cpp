#include "py_document.h"

#include "variant_python.h"

#include <utility>

namespace {

// 把当前挂起的 Python 异常转成文本并清掉，避免异常延续到 shiboken 的错误处理。
std::string takePyError()
{
    PyObject* type = nullptr;
    PyObject* value = nullptr;
    PyObject* trace = nullptr;
    PyErr_Fetch(&type, &value, &trace);

    std::string message = "conversion failed";
    if (value) {
        if (PyObject* text = PyObject_Str(value)) {
            if (const char* utf8 = PyUnicode_AsUTF8(text))
                message = utf8;
            Py_DECREF(text);
        }
    }
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(trace);
    return message;
}

} // namespace

bool PyDocument::has(const std::string& path) const
{
    return m_doc.has(path);
}

std::string PyDocument::lastError() const
{
    return m_conversionError.empty() ? m_doc.lastError() : m_conversionError;
}

void PyDocument::clear()
{
    m_doc.clear();
    m_conversionError.clear();
}

PyObject* pyDocGet(const PyDocument* doc, const std::string& path)
{
    return vpy::toPython(doc->m_doc.get(path));
}

bool pyDocSet(PyDocument* doc, const std::string& path, PyObject* value)
{
    doc->m_conversionError.clear();

    vt::Variant converted;
    if (!vpy::fromPython(value, converted)) {
        doc->m_conversionError = takePyError();
        return false;
    }
    return doc->m_doc.set(path, std::move(converted));
}
