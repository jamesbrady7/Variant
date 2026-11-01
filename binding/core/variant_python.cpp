#include "variant_python.h"

#include "vt/variant_list.h"
#include "vt/variant_map.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <utility>

namespace vpy {

namespace {

std::vector<Hook> g_hooks;

const char* shortTypeName(const char* fullName)
{
    const char* dot = std::strrchr(fullName, '.');
    return dot ? dot + 1 : fullName;
}

const Hook* findHookByType(const std::type_info& ti)
{
    const std::type_index index(ti);
    for (const Hook& hook : g_hooks) {
        if (hook.cppType == index)
            return &hook;
    }
    return nullptr;
}

const Hook* findHookForObject(PyObject* py)
{
    if (!py)
        return nullptr;

    // 先精确匹配短类名，再退回 isinstance（覆盖 Python 子类）。
    const char* name = shortTypeName(Py_TYPE(py)->tp_name);
    for (const Hook& hook : g_hooks) {
        if (!hook.pyType || !hook.store)
            continue;   // 自定义 toPython hook 没有绑定的 Python 类型
        if (hook.className == name) {
            const int rc = PyObject_IsInstance(py, reinterpret_cast<PyObject*>(hook.pyType));
            if (rc == 1)
                return &hook;
            if (rc < 0)
                PyErr_Clear();
        }
    }
    for (const Hook& hook : g_hooks) {
        if (!hook.pyType || !hook.store)
            continue;
        const int rc = PyObject_IsInstance(py, reinterpret_cast<PyObject*>(hook.pyType));
        if (rc == 1)
            return &hook;
        if (rc < 0)
            PyErr_Clear();
    }
    return nullptr;
}

} // namespace

bool addHook(Hook hook)
{
    for (const Hook& existing : g_hooks) {
        if (existing.cppType == hook.cppType)
            return existing.className == hook.className;
    }
    g_hooks.push_back(std::move(hook));
    return true;
}

void clearHooks()
{
    g_hooks.clear();
}

bool addCustomHook(std::type_index type, std::string name, CustomToPythonFn toPython)
{
    if (!toPython)
        return false;
    Hook hook{
        type,
        std::move(name),
        nullptr,
        [toPython = std::move(toPython)](const std::any& any, const char*) {
            return toPython(any);
        },
        nullptr};
    return addHook(std::move(hook));
}

namespace detail {

PyTypeObject* resolveType(const char* moduleName, const char* className)
{
    PyObject* module = PyImport_ImportModule(moduleName);
    if (!module) {
        PyErr_Clear();
        return nullptr;
    }

    PyTypeObject* found = nullptr;
    Shiboken::Module::TypeInitStruct* types = Shiboken::Module::getTypes(module);
    if (types) {
        for (int i = 0; types[i].fullName; ++i) {
            if (std::strcmp(shortTypeName(types[i].fullName), className) == 0) {
                found = types[i].type;
                break;
            }
        }
    }
    Py_DECREF(module);

    if (!found) {
        PyErr_Format(PyExc_RuntimeError,
                     "shiboken type '%s' not found in module '%s'", className, moduleName);
        return nullptr;
    }
    Py_INCREF(found);   // 类型对象活到解释器结束
    return found;
}

} // namespace detail

bool isRegisteredObject(PyObject* py)
{
    return findHookForObject(py) != nullptr;
}

PyObject* mapToPython(const vt::VariantMap& map)
{
    PyObject* dict = PyDict_New();
    if (!dict)
        return nullptr;

    for (const auto& kv : map) {
        const std::string& key = kv.first;
        PyObject* pyKey = PyUnicode_DecodeUTF8(key.data(), static_cast<Py_ssize_t>(key.size()), "strict");
        if (!pyKey) {
            Py_DECREF(dict);
            return nullptr;
        }
        PyObject* pyValue = toPython(kv.second);
        if (!pyValue) {
            Py_DECREF(pyKey);
            Py_DECREF(dict);
            return nullptr;
        }
        const int rc = PyDict_SetItem(dict, pyKey, pyValue);
        Py_DECREF(pyKey);
        Py_DECREF(pyValue);
        if (rc != 0) {
            Py_DECREF(dict);
            return nullptr;
        }
    }
    return dict;
}

PyObject* listToPython(const vt::VariantList& list)
{
    const Py_ssize_t count = static_cast<Py_ssize_t>(list.size());
    PyObject* out = PyList_New(count);
    if (!out)
        return nullptr;

    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* item = toPython(list[static_cast<std::size_t>(i)]);
        if (!item) {
            Py_DECREF(out);
            return nullptr;
        }
        PyList_SET_ITEM(out, i, item);   // 窃取引用
    }
    return out;
}

bool canConvertToPython(const vt::Variant& value)
{
    switch (value.type()) {
    case vt::Variant::Type::Null:
    case vt::Variant::Type::Bool:
    case vt::Variant::Type::Int:
    case vt::Variant::Type::Double:
    case vt::Variant::Type::String:
        return true;
    case vt::Variant::Type::Map:
        for (const auto& kv : value.asMap()) {
            if (!canConvertToPython(kv.second))
                return false;
        }
        return true;
    case vt::Variant::Type::List:
        for (const auto& item : value.asList()) {
            if (!canConvertToPython(item))
                return false;
        }
        return true;
    case vt::Variant::Type::Extension: {
        const std::any* any = value.extensionAny();
        return any && findHookByType(any->type()) != nullptr;
    }
    }
    return false;
}

PyObject* toPython(const vt::Variant& value)
{
    try {
        switch (value.type()) {
        case vt::Variant::Type::Null:
            Py_RETURN_NONE;
        case vt::Variant::Type::Bool:
            return PyBool_FromLong(value.asBool() ? 1 : 0);
        case vt::Variant::Type::Int:
            return PyLong_FromLongLong(value.asInt());
        case vt::Variant::Type::Double:
            return PyFloat_FromDouble(value.asDouble());
        case vt::Variant::Type::String: {
            const std::string_view text = value.asStringView();
            return PyUnicode_DecodeUTF8(text.data(), static_cast<Py_ssize_t>(text.size()), "strict");
        }
        case vt::Variant::Type::Map:
            return mapToPython(value.asMap());
        case vt::Variant::Type::List:
            return listToPython(value.asList());
        case vt::Variant::Type::Extension: {
            const std::any* any = value.extensionAny();
            if (!any)
                Py_RETURN_NONE;
            const Hook* hook = findHookByType(any->type());
            if (!hook || !hook->toPython) {
                PyErr_Format(PyExc_TypeError,
                             "vt::Variant holds '%s' but no Python converter is registered",
                             value.typeName().c_str());
                return nullptr;
            }
            return hook->toPython(*any, hook->className.c_str());
        }
        }
    } catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
    Py_RETURN_NONE;
}

bool fromPython(PyObject* py, vt::Variant& out)
{
    if (!py) {
        PyErr_SetString(PyExc_TypeError, "null Python object");
        return false;
    }

    try {
        if (py == Py_None) {
            out = vt::Variant();
            return true;
        }
        // bool 必须先于 int：Python 里 bool 是 int 的子类。
        if (PyBool_Check(py)) {
            out = vt::Variant(py == Py_True);
            return true;
        }
        if (PyLong_Check(py)) {
            const long long v = PyLong_AsLongLong(py);
            if (v == -1 && PyErr_Occurred())
                return false;
            out = vt::Variant(static_cast<std::int64_t>(v));
            return true;
        }
        if (PyFloat_Check(py)) {
            const double d = PyFloat_AsDouble(py);
            if (PyErr_Occurred())
                return false;
            out = vt::Variant(d);
            return true;
        }
        if (PyUnicode_Check(py)) {
            Py_ssize_t length = 0;
            const char* utf8 = PyUnicode_AsUTF8AndSize(py, &length);
            if (!utf8)
                return false;
            out = vt::Variant(std::string(utf8, static_cast<std::size_t>(length)));
            return true;
        }
        if (PyList_Check(py) || PyTuple_Check(py)) {
            const Py_ssize_t count = PySequence_Size(py);
            if (count < 0)
                return false;
            vt::VariantList list;
            list.reserve(static_cast<std::size_t>(count));
            for (Py_ssize_t i = 0; i < count; ++i) {
                PyObject* item = PySequence_GetItem(py, i);   // new ref
                if (!item)
                    return false;
                vt::Variant element;
                const bool ok = fromPython(item, element);
                Py_DECREF(item);
                if (!ok)
                    return false;
                list.push_back(std::move(element));
            }
            out = vt::Variant(std::move(list));
            return true;
        }
        if (PyDict_Check(py)) {
            vt::VariantMap map;
            PyObject* key = nullptr;
            PyObject* value = nullptr;
            Py_ssize_t pos = 0;
            while (PyDict_Next(py, &pos, &key, &value)) {
                std::string keyText;
                if (PyUnicode_Check(key)) {
                    Py_ssize_t length = 0;
                    const char* utf8 = PyUnicode_AsUTF8AndSize(key, &length);
                    if (!utf8)
                        return false;
                    keyText.assign(utf8, static_cast<std::size_t>(length));
                } else if (PyLong_Check(key) && !PyBool_Check(key)) {
                    // int 键统一转成十进制字符串；getAs<map<int,int>> 会再解析回来
                    const long long numeric = PyLong_AsLongLong(key);
                    if (numeric == -1 && PyErr_Occurred())
                        return false;
                    keyText = std::to_string(numeric);
                } else {
                    PyErr_SetString(PyExc_TypeError, "VariantMap keys must be str or int");
                    return false;
                }
                vt::Variant element;
                if (!fromPython(value, element))
                    return false;
                map[std::move(keyText)] = std::move(element);
            }
            out = vt::Variant(std::move(map));
            return true;
        }
        if (const Hook* hook = findHookForObject(py)) {
            std::any any;
            if (!hook->store(py, any, hook->className.c_str()))
                return false;
            out = vt::Variant::fromValue(std::move(any));
            return true;
        }
        PyErr_Format(PyExc_TypeError,
                     "cannot convert Python object of type '%s' to vt::Variant", Py_TYPE(py)->tp_name);
        return false;
    } catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return false;
    }
}

} // namespace vpy
