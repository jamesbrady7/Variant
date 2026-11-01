#include "variant_python.h"

#include "vt/normalize.h"
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

/// @brief 扩展槽里没法转换时统一的报错文案。
void setUnconvertibleTypeError(const char* typeName)
{
    PyErr_Format(PyExc_TypeError,
                 "vt::Variant holds '%s' but no Python converter is registered "
                 "(custom types need vpy::register*Type; containers register "
                 "automatically when passed to Variant::fromValue, or call "
                 "vt::registerContainerAdapters<T>)",
                 typeName ? typeName : "<unknown>");
}

/**
 * @brief 把 `visitValue` 的事件流直接构造成 Python 对象。
 *
 * 与 `vt::detail::VariantSink` 的区别：映射键保留真实类型
 * （`map<int,int>` -> `{7: 2}`，而不是 `{"7": 2}`），因为 Python dict
 * 支持任意 hashable 键；Variant 只有字符串键，所以那条路必须先字符串化。
 *
 * 键不可哈希时**响亮失败**（保留 Python 的 TypeError），不悄悄退回字符串键。
 */
class PySink final : public vt::detail::ValueSink {
public:
    PySink() = default;
    ~PySink() override { reset(); }

    /// @brief 取出结果（新引用）；失败返回 nullptr 并保留 PyErr。
    PyObject* take()
    {
        if (m_failed) {
            if (!PyErr_Occurred())
                PyErr_SetString(PyExc_RuntimeError, "vt: Python conversion failed");
            reset();
            return nullptr;
        }
        if (!m_root) {
            reset();
            Py_INCREF(Py_None);
            return Py_None;   // 空事件流视作 Null
        }
        PyObject* result = m_root;
        m_root = nullptr;
        reset();
        return result;
    }

    /// @brief Null -> `None`。
    void onNull() override
    {
        if (m_failed)
            return;
        Py_INCREF(Py_None);
        deliver(Py_None);
    }

    /// @brief bool -> `bool`（先于 int 判断，Python 里 bool 是 int 子类）。
    void onBool(bool value) override { deliverNew(PyBool_FromLong(value ? 1 : 0)); }
    /// @brief 整数 -> `int`。
    void onInt(std::int64_t value) override { deliverNew(PyLong_FromLongLong(value)); }
    /// @brief 浮点 -> `float`。
    void onDouble(double value) override { deliverNew(PyFloat_FromDouble(value)); }

    /// @brief UTF-8 字符串 -> `str`（非法 UTF-8 由 Python 报错）。
    void onString(std::string_view value) override
    {
        deliverNew(PyUnicode_DecodeUTF8(value.data(), static_cast<Py_ssize_t>(value.size()),
                                        "strict"));
    }

    /// @brief 已成型的 Variant 沿用既有 `toPython`（含共享所有权等 hook 行为）。
    void onVariant(const vt::Variant& value) override
    {
        if (m_failed)
            return;
        deliverNew(toPython(value));
    }

    /// @brief 扩展槽叶子：hook -> 遍历函数 -> TypeError（见定义）。
    void onExtension(const std::any& any) override;

    /// @brief 序列开始：按已知长度预分配 list。
    void onListBegin(std::size_t size) override
    {
        if (m_failed)
            return;
        PyObject* list = PyList_New(static_cast<Py_ssize_t>(size));
        if (!list) {
            m_failed = true;
            return;
        }
        m_stack.push_back(Frame{list, false, nullptr, 0});
    }

    /// @brief 序列结束：把 list 交给父容器 / 作为根值。
    void onListEnd() override { closeFrame(); }

    /// @brief 映射开始：永远返回 `Typed`，键按原类型给（Python dict 不挑键类型）。
    vt::detail::MapKeyForm onMapBegin(std::size_t, bool) override
    {
        if (m_failed)
            return vt::detail::MapKeyForm::Typed;
        PyObject* dict = PyDict_New();
        if (!dict) {
            m_failed = true;
            return vt::detail::MapKeyForm::Typed;
        }
        m_stack.push_back(Frame{dict, true, nullptr, 0});
        return vt::detail::MapKeyForm::Typed;
    }

    /// @brief 映射结束：把 dict 交给父容器 / 作为根值。
    void onMapEnd() override { closeFrame(); }

    /// @brief 条目开始：无状态（dict 帧用 `key == nullptr` 判断"下一个是键"）。
    void onEntryBegin() override {}

    /// @brief 条目结束：释放没用上的键（事件流不完整时才会出现）。
    void onEntryEnd() override
    {
        // 正常情况下键已被值消费；残留说明事件流不完整，直接释放。
        if (!m_stack.empty() && m_stack.back().key) {
            Py_DECREF(m_stack.back().key);
            m_stack.back().key = nullptr;
        }
    }

private:
    struct Frame {
        PyObject* container = nullptr;   ///< 正在构建的 list / dict（新引用）
        bool isDict = false;             ///< true = dict 帧，否则 list 帧
        PyObject* key = nullptr;         ///< dict 的待写入键（新引用）
        Py_ssize_t index = 0;            ///< list 的写入位置
    };

    /// @brief 接收 Python API 返回的新引用；nullptr 表示失败（PyErr 已设置）。
    /// @param [in] PyObject* 新引用（可能为 nullptr）
    void deliverNew(PyObject* obj)
    {
        if (m_failed) {
            Py_XDECREF(obj);
            return;
        }
        if (!obj) {
            m_failed = true;
            return;
        }
        deliver(obj);
    }

    /// @brief 把 obj（接管所有权）放进当前容器 / 作为根值。
    ///
    /// dict 帧里第一个到达的是键（先做可哈希检查），第二个才是值。
    /// @param [in] PyObject* 新引用
    void deliver(PyObject* obj)
    {
        if (m_stack.empty()) {
            Py_XDECREF(m_root);
            m_root = obj;
            return;
        }
        Frame& frame = m_stack.back();
        if (!frame.isDict) {
            // 事件流声称的长度就是 PyList_New 的长度；越界写入是内存破坏，
            // 所以这里宁可响亮失败。正常遍历不会触发（契约由 visitValue 保证）。
            if (frame.index >= PyList_GET_SIZE(frame.container)) {
                Py_DECREF(obj);
                m_failed = true;
                PyErr_SetString(PyExc_RuntimeError,
                                "vt: list event stream exceeded its declared size");
                return;
            }
            PyList_SET_ITEM(frame.container, frame.index++, obj);   // 窃取引用
            return;
        }
        if (!frame.key) {
            if (PyObject_Hash(obj) == -1) {
                // 不可哈希的键（例如某些 shiboken wrapper）：如实报错，
                // 不退回字符串键——那会把类型错误藏进数据里。
                Py_DECREF(obj);
                m_failed = true;
                return;
            }
            frame.key = obj;
            return;
        }
        const int rc = PyDict_SetItem(frame.container, frame.key, obj);
        Py_DECREF(frame.key);
        Py_DECREF(obj);
        frame.key = nullptr;
        if (rc != 0)
            m_failed = true;
    }

    /// @brief 关闭栈顶帧，把容器交给父容器 / 作为根值。
    void closeFrame()
    {
        if (m_failed || m_stack.empty())
            return;
        const Frame frame = m_stack.back();
        m_stack.pop_back();
        Py_XDECREF(frame.key);   // 不完整的 dict 条目

        // PyList_New 的长度是遍历时声明的：没填满就交出去会让 Python
        // 读到 NULL 槽位。这与"超出声明长度"一样属于事件流违约，必须报错。
        if (!frame.isDict && frame.index != PyList_GET_SIZE(frame.container)) {
            Py_DECREF(frame.container);
            m_failed = true;
            PyErr_SetString(PyExc_RuntimeError,
                            "vt: list event stream did not fill its declared size");
            return;
        }
        deliver(frame.container);
    }

    /// @brief 释放栈里所有未完成的容器、待写入键与根值（失败路径与析构共用）。
    void reset()
    {
        for (Frame& frame : m_stack) {
            Py_XDECREF(frame.container);
            Py_XDECREF(frame.key);
        }
        m_stack.clear();
        Py_XDECREF(m_root);
        m_root = nullptr;
    }

    std::vector<Frame> m_stack;
    PyObject* m_root = nullptr;
    bool m_failed = false;
};

void PySink::onExtension(const std::any& any)
{
    if (m_failed)
        return;

    if (const Hook* hook = findHookByType(any.type()); hook && hook->toPython) {
        deliverNew(hook->toPython(any, hook->className.c_str()));
        return;
    }

    // 容器（vector/map/set/pair/array 及其嵌套）：自动登记的归一化函数
    // 把扩展槽展开成事件流，键按原类型交给本 sink。
    if (vt::detail::NormalizerFn normalizer = vt::detail::findNormalizer(any.type())) {
        try {
            normalizer(any, *this);
        } catch (const std::exception& e) {
            PyErr_SetString(PyExc_RuntimeError, e.what());
            m_failed = true;
        } catch (...) {
            PyErr_SetString(PyExc_RuntimeError, "vt: container normalization failed");
            m_failed = true;
        }
        return;
    }

    setUnconvertibleTypeError(vt::Variant::fromValue(std::any(any)).typeName().c_str());
    m_failed = true;
}

/**
 * @brief `canConvertToPython` 的遍历器：只判断可转换性，不构造 Python 对象。
 *
 * 注意：映射键是否可哈希无法在不构造对象的前提下判断，这里按"可哈希"处理
 * （真正的失败会在 `toPython` 里响亮报错）。
 */
class ConvertCheckSink final : public vt::detail::ValueSink {
public:
    bool ok() const { return m_ok; }

    void onNull() override {}
    void onBool(bool) override {}
    void onInt(std::int64_t) override {}
    void onDouble(double) override {}
    void onString(std::string_view) override {}

    void onVariant(const vt::Variant& value) override
    {
        if (m_ok && !canConvertToPython(value))
            m_ok = false;
    }

    void onExtension(const std::any& any) override
    {
        if (!m_ok)
            return;
        if (findHookByType(any.type()))
            return;
        if (vt::detail::NormalizerFn normalizer = vt::detail::findNormalizer(any.type())) {
            try {
                normalizer(any, *this);
            } catch (...) {
                m_ok = false;
            }
            return;
        }
        m_ok = false;
    }

    void onListBegin(std::size_t) override {}
    void onListEnd() override {}
    vt::detail::MapKeyForm onMapBegin(std::size_t, bool) override
    {
        return vt::detail::MapKeyForm::Typed;
    }
    void onMapEnd() override {}
    void onEntryBegin() override {}
    void onEntryEnd() override {}

private:
    bool m_ok = true;
};

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
        if (!any)
            return true;
        // 容器类型：自动登记的归一化函数把扩展槽展开成事件流，
        // 由 ConvertCheckSink 逐叶子判断（不构造 Python 对象）。
        ConvertCheckSink sink;
        sink.onExtension(*any);
        return sink.ok();
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
            // 容器类型（vector/map/set/pair/array 及其嵌套）无需注册 hook：
            // fromValue 时自动登记的归一化函数把扩展槽展开成事件流。
            if ((!hook || !hook->toPython) &&
                !vt::detail::findNormalizer(any->type())) {
                setUnconvertibleTypeError(value.typeName().c_str());
                return nullptr;
            }
            // 统一交给 PySink：hook / 归一化都在同一处，
            // 容器映射的键保留真实类型（map<int,int> -> {7: 2}）。
            PySink sink;
            sink.onExtension(*any);
            return sink.take();
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
