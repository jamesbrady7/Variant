#pragma once

// vt::Variant <-> Python 原生对象（None/bool/int/float/str/list/dict）的转换，
// 以及注册表：把 shiboken 绑定的 C++ 类型注册进来后，放进 Variant 即可跨语言往返。
//
// 关键机制（已用探针验证）：
//   - Shiboken::Module::getTypes(module) 可在运行时按 "pkg.Class" 枚举 PyTypeObject*
//   - SpecificConverter("Class") 可按字符串名取转换器，toCpp/toPython 双向转换
//   - 对象类型 toPython 走 pointerToPython，返回原 wrapper（身份不变）

// shiboken 在部分配置下会传播 Py_LIMITED_API（届时 PyTypeObject 字段不可见、
// PyList_SET_ITEM / PyUnicode_AsUTF8AndSize 等宏缺失）。我们链接的是完整
// python39.dll，所以在本 TU 内先抹掉它，再包含 Python 头。
#if defined(Py_LIMITED_API)
#undef Py_LIMITED_API
#endif

#include <Python.h>
#include <shiboken.h>
#include <sbkconverter.h>

#include <any>
#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vt/type_registry.h"
#include "vt/variant.h"

namespace vpy {

/// 一条 Python 转换记录。
struct Hook {
    std::type_index cppType;  ///< Variant 扩展槽里的动态类型
    std::string className;    ///< shiboken 转换器名 / Python 短类名
    PyTypeObject* pyType = nullptr;  ///< 绑定的 Python 类型；自定义 hook 为 nullptr
    std::function<PyObject*(const std::any&, const char*)> toPython;  ///< C++ -> Python
    std::function<bool(PyObject*, std::any&, const char*)> store;     ///< Python -> C++
};

namespace detail {

/**
 * @brief 运行时解析 shiboken 绑定类型。
 *
 * import 模块并枚举 `TypeInitStruct`，按 fullName 后缀匹配类名。
 * @param [in] const char* 模块名（如 "variant_binding"）
 * @param [in] const char* 类名（如 "DemoNode"）
 * @return PyTypeObject* 类型对象（已 INCREF）；找不到返回 nullptr
 */
PyTypeObject* resolveType(const char* moduleName, const char* className);

/**
 * @brief 对象类型：std::any 中的 T* -> Python wrapper。
 * @param [in] std::any 扩展槽内容
 * @param [in] const char* shiboken 转换器名
 * @return PyObject* Python 对象；失败返回 nullptr
 */
template <class T>
PyObject* pointerToPython(const std::any& value, const char* name)
{
    if (T* const* pp = std::any_cast<T*>(&value)) {
        if (!*pp)
            Py_RETURN_NONE;
        Shiboken::Conversions::SpecificConverter converter(name);
        if (!converter.isValid()) {
            PyErr_Format(PyExc_TypeError, "no shiboken converter named '%s'", name);
            return nullptr;
        }
        return converter.toPython(pp);   // pp 指向存储的 T*，正是转换器要的形态
    }
    PyErr_Format(PyExc_TypeError, "std::any does not hold %s*", name);
    return nullptr;
}

/**
 * @brief 对象类型：Python wrapper -> std::any 中的 T*。
 * @param [in] PyObject* Python 对象
 * @param [out] std::any 写入 T*
 * @param [in] const char* shiboken 转换器名
 * @return bool 是否成功
 */
template <class T>
bool pointerStore(PyObject* py, std::any& out, const char* name)
{
    Shiboken::Conversions::SpecificConverter converter(name);
    if (!converter.isValid()) {
        PyErr_Format(PyExc_TypeError, "no shiboken converter named '%s'", name);
        return false;
    }
    T* p = nullptr;
    converter.toCpp(py, &p);
    if (PyErr_Occurred())
        return false;
    out = p;
    return true;
}

/**
 * @brief 值类型：std::any 中的 T -> Python 对象（拷贝）。
 * @param [in] std::any 扩展槽内容
 * @param [in] const char* shiboken 转换器名
 * @return PyObject* Python 对象；失败返回 nullptr
 */
template <class T>
PyObject* valueToPython(const std::any& value, const char* name)
{
    if (const T* p = std::any_cast<T>(&value)) {
        Shiboken::Conversions::SpecificConverter converter(name);
        if (!converter.isValid()) {
            PyErr_Format(PyExc_TypeError, "no shiboken converter named '%s'", name);
            return nullptr;
        }
        return converter.toPython(p);
    }
    PyErr_Format(PyExc_TypeError, "std::any does not hold %s", name);
    return nullptr;
}

/**
 * @brief 值类型：Python 对象 -> std::any 中的 T。
 * @param [in] PyObject* Python 对象
 * @param [out] std::any 写入 T
 * @param [in] const char* shiboken 转换器名
 * @return bool 是否成功
 */
template <class T>
bool valueStore(PyObject* py, std::any& out, const char* name)
{
    Shiboken::Conversions::SpecificConverter converter(name);
    if (!converter.isValid()) {
        PyErr_Format(PyExc_TypeError, "no shiboken converter named '%s'", name);
        return false;
    }
    T v{};   // 注册的 value-type 需要可默认构造
    converter.toCpp(py, &v);
    if (PyErr_Occurred())
        return false;
    out = std::move(v);
    return true;
}

// ---------------------------------------------------------------- shared_ptr
// 内部 Python 属性：值为 PyCapsule，里面是 new std::shared_ptr<T>(...)。
inline constexpr const char* kSharedOwnerAttr = "_vt_shared_owner";
inline constexpr const char* kSharedOwnerCapsule = "vt::shared_owner";

/// capsule 析构：释放里面保存的 shared_ptr 拷贝。
template <class T>
void sharedCapsuleDestructor(PyObject* capsule)
{
    auto* holder = static_cast<std::shared_ptr<T>*>(
        PyCapsule_GetPointer(capsule, kSharedOwnerCapsule));
    if (holder)
        delete holder;
    else
        PyErr_Clear();
}

/**
 * @brief Python 创建的对象被收养时使用的 deleter。
 *
 * 它不 delete T（T 仍由 Python wrapper 拥有），而是持有一份 wrapper 的强引用：
 * shared_ptr 存在期间 wrapper 不会被 GC，因此 T 也不会被提前删除；
 * 最后一个 shared_ptr 释放时 DECREF wrapper，wrapper 归零时才由 Python 删除 T。
 */
template <class T>
struct PythonKeepAliveDeleter {
    mutable PyObject* owner = nullptr;

    PythonKeepAliveDeleter() = default;
    explicit PythonKeepAliveDeleter(PyObject* py) : owner(py) { Py_XINCREF(owner); }
    PythonKeepAliveDeleter(const PythonKeepAliveDeleter& other) : owner(other.owner) { Py_XINCREF(owner); }
    PythonKeepAliveDeleter(PythonKeepAliveDeleter&& other) noexcept : owner(other.owner) { other.owner = nullptr; }

    PythonKeepAliveDeleter& operator=(const PythonKeepAliveDeleter& other)
    {
        if (this != &other) {
            release();
            owner = other.owner;
            Py_XINCREF(owner);
        }
        return *this;
    }
    PythonKeepAliveDeleter& operator=(PythonKeepAliveDeleter&& other) noexcept
    {
        if (this != &other) {
            release();
            owner = other.owner;
            other.owner = nullptr;
        }
        return *this;
    }
    ~PythonKeepAliveDeleter() { release(); }

    void operator()(T*) const noexcept { release(); }

    void release() const noexcept
    {
        if (!owner)
            return;
        PyObject* object = owner;
        owner = nullptr;
        if (!Py_IsInitialized())
            return;   // 解释器已结束：放弃引用，避免在 finalize 之后碰 Python
        PyGILState_STATE gil = PyGILState_Ensure();
        Py_DECREF(object);
        PyGILState_Release(gil);
    }
};

/// 已收养对象的 raw -> weak_ptr 表（每个 T 一张，避免重复建控制块）。
template <class T>
std::mutex& adoptedMutex()
{
    static std::mutex mutex;
    return mutex;
}

template <class T>
std::unordered_map<T*, std::weak_ptr<T>>& adoptedRegistry()
{
    static std::unordered_map<T*, std::weak_ptr<T>> registry;
    return registry;
}

template <class T>
std::shared_ptr<T> findAdopted(T* raw)
{
    if (!raw)
        return {};
    std::lock_guard<std::mutex> lock(adoptedMutex<T>());
    auto& registry = adoptedRegistry<T>();
    const auto it = registry.find(raw);
    if (it == registry.end())
        return {};
    if (auto sp = it->second.lock())
        return sp;
    registry.erase(it);
    return {};
}

template <class T>
bool isAdopted(T* raw)
{
    return static_cast<bool>(findAdopted<T>(raw));
}

template <class T>
void registerAdopted(T* raw, const std::shared_ptr<T>& sp)
{
    std::lock_guard<std::mutex> lock(adoptedMutex<T>());
    adoptedRegistry<T>()[raw] = sp;
}

/**
 * @brief std::shared_ptr<T> -> Python 对象（真正的 T wrapper + capsule）。
 */
template <class T>
PyObject* sharedToPython(const std::any& value, const char* name)
{
    const auto* pp = std::any_cast<std::shared_ptr<T>>(&value);
    if (!pp) {
        PyErr_Format(PyExc_TypeError, "std::any does not hold std::shared_ptr<%s>", name);
        return nullptr;
    }
    const std::shared_ptr<T>& sp = *pp;
    if (!sp)
        Py_RETURN_NONE;

    T* raw = sp.get();
    Shiboken::Conversions::SpecificConverter converter(name);
    if (!converter.isValid()) {
        PyErr_Format(PyExc_TypeError, "no shiboken converter named '%s'", name);
        return nullptr;
    }
    PyObject* wrapper = converter.toPython(&raw);
    if (!wrapper)
        return nullptr;
    auto* sbk = reinterpret_cast<SbkObject*>(wrapper);

    // 已经挂过 capsule：C++ shared_ptr 路径，直接返回同一个 wrapper。
    PyObject* existing = PyObject_GetAttrString(wrapper, kSharedOwnerAttr);
    if (existing) {
        const bool valid = PyCapsule_IsValid(existing, kSharedOwnerCapsule) != 0;
        Py_DECREF(existing);
        if (!valid) {
            Py_DECREF(wrapper);
            PyErr_Format(PyExc_TypeError, "%s has an invalid shared-owner capsule", name);
            return nullptr;
        }
        return wrapper;
    }
    PyErr_Clear();

    // 已被收养的 Python 对象：不能再挂 capsule（会形成 wrapper<->shared_ptr 环）。
    if (isAdopted<T>(raw))
        return wrapper;

    // C++ shared_ptr 路径：Python wrapper 必须是非 owning，否则所有权冲突。
    if (Shiboken::Object::hasOwnership(sbk)) {
        Py_DECREF(wrapper);
        PyErr_Format(PyExc_TypeError,
                     "%s is Python-owned but has no shared owner; create it via a shared factory",
                     name);
        return nullptr;
    }

    auto* holder = new std::shared_ptr<T>(sp);
    PyObject* capsule = PyCapsule_New(holder, kSharedOwnerCapsule, &detail::sharedCapsuleDestructor<T>);
    if (!capsule) {
        delete holder;
        Py_DECREF(wrapper);
        return nullptr;
    }
    if (PyObject_SetAttrString(wrapper, kSharedOwnerAttr, capsule) != 0) {
        Py_DECREF(capsule);
        Py_DECREF(wrapper);
        return nullptr;
    }
    Py_DECREF(capsule);
    return wrapper;
}

/**
 * @brief Python 对象 -> std::shared_ptr<T>。
 *
 * 有 capsule 直接用；没有 capsule 时只收养 Python 独占的对象，借用对象报错。
 */
template <class T>
bool sharedStore(PyObject* py, std::any& out, const char* name)
{
    if (!py) {
        PyErr_SetString(PyExc_TypeError, "null Python object");
        return false;
    }

    // 1) 已挂 capsule：copy shared_ptr。
    PyObject* capsule = PyObject_GetAttrString(py, kSharedOwnerAttr);
    if (capsule) {
        auto* holder = static_cast<std::shared_ptr<T>*>(
            PyCapsule_IsValid(capsule, kSharedOwnerCapsule)
                ? PyCapsule_GetPointer(capsule, kSharedOwnerCapsule)
                : nullptr);
        if (!holder) {
            Py_DECREF(capsule);
            PyErr_Format(PyExc_TypeError, "%s has an invalid shared-owner capsule", name);
            return false;
        }
        out = *holder;
        Py_DECREF(capsule);
        return true;
    }
    PyErr_Clear();

    // 2) 基本合法性。
    auto* sbk = reinterpret_cast<SbkObject*>(py);
    if (!Shiboken::Object::isValid(py)) {
        PyErr_Format(PyExc_TypeError, "%s is not a valid Python object", name);
        return false;
    }

    // 3) 取裸指针。
    Shiboken::Conversions::SpecificConverter converter(name);
    if (!converter.isValid()) {
        PyErr_Format(PyExc_TypeError, "no shiboken converter named '%s'", name);
        return false;
    }
    T* raw = nullptr;
    converter.toCpp(py, &raw);
    if (!raw || PyErr_Occurred())
        return false;

    // 4) 已收养过：复用同一个控制块。
    if (auto adopted = findAdopted<T>(raw)) {
        out = std::move(adopted);
        return true;
    }

    // 5) 没有 capsule 也没有收养记录：只有 Python 独占所有权才允许收养。
    if (!Shiboken::Object::hasOwnership(sbk)) {
        PyErr_Format(PyExc_TypeError,
                     "%s is registered as a shared type, but this object is borrowed "
                     "from C++; create it via a shared factory", name);
        return false;
    }

    // 6) 收养：shared_ptr 的 deleter 持有 wrapper 引用，Python 继续拥有 T。
    std::shared_ptr<T> sp(raw, detail::PythonKeepAliveDeleter<T>(py));
    registerAdopted<T>(raw, sp);
    out = std::move(sp);
    return true;
}

} // namespace detail

/**
 * @brief 向 Hook 表添加一条记录。
 * @param [in] Hook 记录
 * @return bool 成功；同类型同名重复注册返回 true
 */
bool addHook(Hook hook);

/// @brief 清空 Hook 表（主要用于测试）。
void clearHooks();

/**
 * @brief 注册一个对象类型（Variant 里存 T*）。
 *
 * 一次调用同时：
 * 1. 在核心 TypeRegistry 登记稳定名字；
 * 2. 在 Python Hook 表登记双向转换器。
 * 名字冲突返回 false，且不注册 Hook。
 * @param [in] const char* 模块名
 * @param [in] const char* 类名
 * @return bool 是否成功
 */
template <class T>
bool registerPointerType(const char* moduleName, const char* className)
{
    if (!vt::TypeRegistry::instance().registerType<T*>(className))
        return false;
    PyTypeObject* pyType = detail::resolveType(moduleName, className);
    if (!pyType)
        return false;
    return addHook(Hook{std::type_index(typeid(T*)), className, pyType,
                        &detail::pointerToPython<T>, &detail::pointerStore<T>});
}

/**
 * @brief 注册一个值类型（Variant 里存 T）。
 * @param [in] const char* 模块名
 * @param [in] const char* 类名
 * @return bool 是否成功
 */
template <class T>
bool registerValueType(const char* moduleName, const char* className)
{
    if (!vt::TypeRegistry::instance().registerType<T>(className))
        return false;
    PyTypeObject* pyType = detail::resolveType(moduleName, className);
    if (!pyType)
        return false;
    return addHook(Hook{std::type_index(typeid(T)), className, pyType,
                        &detail::valueToPython<T>, &detail::valueStore<T>});
}

/**
 * @brief XML 转换规则的 check 谓词：只有已注册的绑定对象才走对象分支。
 * @param [in] PyObject* Python 对象
 * @return bool 是否属于某个已注册类型
 */
bool isRegisteredObject(PyObject* py);

/// 自定义 C++ -> Python 转换函数类型（不依赖 shiboken）。
using CustomToPythonFn = std::function<PyObject*(const std::any&)>;

/**
 * @brief 注册一条自定义 C++ -> Python hook。
 * @param [in] std::type_index 类型索引
 * @param [in] std::string 名字
 * @param [in] CustomToPythonFn 转换函数
 * @return bool 是否成功
 */
bool addCustomHook(std::type_index type, std::string name, CustomToPythonFn toPython);

/**
 * @brief 注册值类型的自定义 C++ -> Python 转换（模板版）。
 * @param [in] const char* 名字
 * @param [in] std::function T -> PyObject*
 * @return bool 是否成功
 */
template <class T>
bool registerCustomToPython(const char* name, std::function<PyObject*(const T&)> toPython)
{
    vt::TypeRegistry::instance().registerType<T>(name);   // best effort
    auto fn = [toPython = std::move(toPython)](const std::any& any) -> PyObject* {
        const T* value = std::any_cast<T>(&any);
        if (!value) {
            PyErr_SetString(PyExc_TypeError, "vt::Variant holds a different extension type");
            return nullptr;
        }
        return toPython(*value);
    };
    return addCustomHook(std::type_index(typeid(T)), name, std::move(fn));
}

/**
 * @brief 注册共享所有权类型（Variant 存 std::shared_ptr<T>，Python 仍是 T）。
 *
 * 有 capsule 的对象直接共享；Python 独占的对象会被收养；借用对象报错。
 * @param [in] const char* 模块名
 * @param [in] const char* 类名
 * @return bool 是否成功
 */
template <class T>
bool registerSharedType(const char* moduleName, const char* className)
{
    PyTypeObject* pyType = detail::resolveType(moduleName, className);
    if (!pyType)
        return false;

    vt::TypeRegistry::instance().registerType<std::shared_ptr<T>>(
        std::string("std::shared_ptr<") + className + ">");

    return addHook(Hook{std::type_index(typeid(std::shared_ptr<T>)), className, pyType,
                        &detail::sharedToPython<T>, &detail::sharedStore<T>});
}

/**
 * @brief 边界预检：这棵 Variant 树能否整体转成 Python。
 *
 * 任一扩展槽没有注册转换器就返回 false（递归检查 map/list）。
 * @param [in] vt::Variant 值
 * @return bool 是否可转换
 */
bool canConvertToPython(const vt::Variant& value);

/**
 * @brief 主转换入口：Variant -> Python 原生对象。
 * @param [in] vt::Variant 值
 * @return PyObject* 新引用；失败返回 nullptr 并设置 Python 异常
 */
PyObject* toPython(const vt::Variant& value);

/**
 * @brief 主转换入口：Python 原生对象 -> Variant。
 * @param [in] PyObject* Python 对象
 * @param [out] vt::Variant 输出值
 * @return bool 是否成功
 */
bool fromPython(PyObject* py, vt::Variant& out);

/**
 * @brief VariantMap -> Python dict。
 * @param [in] vt::VariantMap 映射表
 * @return PyObject* 新引用；失败返回 nullptr
 */
PyObject* mapToPython(const vt::VariantMap& map);

/**
 * @brief VariantList -> Python list。
 * @param [in] vt::VariantList 列表
 * @return PyObject* 新引用；失败返回 nullptr
 */
PyObject* listToPython(const vt::VariantList& list);

/**
 * @brief 注册演示类型（DemoNode / DemoColor + 内建容器 hook）。
 * @return bool 是否全部成功
 */
bool registerDemoTypes();

} // namespace vpy
