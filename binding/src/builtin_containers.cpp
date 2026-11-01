#include "builtin_containers.h"

#include "variant_python.h"
#include "vt/adapters.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace {

PyObject* intVectorToPython(const std::vector<int>& values)
{
    const Py_ssize_t count = static_cast<Py_ssize_t>(values.size());
    PyObject* list = PyList_New(count);
    if (!list)
        return nullptr;
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* item = PyLong_FromLong(values[static_cast<std::size_t>(i)]);
        if (!item) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, i, item);
    }
    return list;
}

PyObject* stringVectorToPython(const std::vector<std::string>& values)
{
    const Py_ssize_t count = static_cast<Py_ssize_t>(values.size());
    PyObject* list = PyList_New(count);
    if (!list)
        return nullptr;
    for (Py_ssize_t i = 0; i < count; ++i) {
        const std::string& text = values[static_cast<std::size_t>(i)];
        PyObject* item = PyUnicode_DecodeUTF8(text.data(), static_cast<Py_ssize_t>(text.size()),
                                              "strict");
        if (!item) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, i, item);
    }
    return list;
}

PyObject* stringMapToPython(const std::unordered_map<std::string, std::string>& values)
{
    PyObject* dict = PyDict_New();
    if (!dict)
        return nullptr;
    for (const auto& kv : values) {
        PyObject* key = PyUnicode_DecodeUTF8(kv.first.data(),
                                             static_cast<Py_ssize_t>(kv.first.size()), "strict");
        PyObject* value = PyUnicode_DecodeUTF8(kv.second.data(),
                                               static_cast<Py_ssize_t>(kv.second.size()), "strict");
        if (!key || !value) {
            Py_XDECREF(key);
            Py_XDECREF(value);
            Py_DECREF(dict);
            return nullptr;
        }
        const int rc = PyDict_SetItem(dict, key, value);
        Py_DECREF(key);
        Py_DECREF(value);
        if (rc != 0) {
            Py_DECREF(dict);
            return nullptr;
        }
    }
    return dict;
}

PyObject* intMapToPython(const std::unordered_map<int, int>& values)
{
    PyObject* dict = PyDict_New();
    if (!dict)
        return nullptr;
    for (const auto& kv : values) {
        PyObject* key = PyLong_FromLong(kv.first);
        PyObject* value = PyLong_FromLong(kv.second);
        if (!key || !value) {
            Py_XDECREF(key);
            Py_XDECREF(value);
            Py_DECREF(dict);
            return nullptr;
        }
        const int rc = PyDict_SetItem(dict, key, value);
        Py_DECREF(key);
        Py_DECREF(value);
        if (rc != 0) {
            Py_DECREF(dict);
            return nullptr;
        }
    }
    return dict;
}

} // namespace

namespace vpy {

bool registerBuiltinContainerHooks()
{
    bool ok = true;
    ok = registerCustomToPython<std::vector<int>>("std::vector<int>", intVectorToPython) && ok;
    ok = registerCustomToPython<std::vector<std::string>>("std::vector<std::string>",
                                                          stringVectorToPython) && ok;
    ok = registerCustomToPython<std::unordered_map<std::string, std::string>>(
             "std::unordered_map<std::string, std::string>", stringMapToPython) && ok;
    ok = registerCustomToPython<std::unordered_map<int, int>>(
             "std::unordered_map<int, int>", intMapToPython) && ok;
    return ok;
}

} // namespace vpy

int sumInts(PyObject* value)
{
    vt::Variant converted;
    if (!vpy::fromPython(value, converted))
        return -1;

    auto numbers = vt::getAs<std::vector<int>>(converted);
    if (!numbers) {
        PyErr_SetString(PyExc_TypeError, "sumInts expects a sequence of integers");
        return -1;
    }

    long long total = 0;
    for (int number : *numbers)
        total += number;
    return static_cast<int>(total);
}

int intMapSize(PyObject* value)
{
    vt::Variant converted;
    if (!vpy::fromPython(value, converted))
        return -1;

    auto numbers = vt::getAs<std::unordered_map<int, int>>(converted);
    if (!numbers) {
        PyErr_SetString(PyExc_TypeError, "intMapSize expects a dict with integer keys/values");
        return -1;
    }
    return static_cast<int>(numbers->size());
}
