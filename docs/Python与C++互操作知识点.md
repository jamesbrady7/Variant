# Python 与 C++ 互操作知识点

> 面向读 `binding/core/`、`exe/`、`tests/` 里 Python 相关代码的人。
> 目标不是复述 CPython 文档，而是把**本仓库真正用到的那些概念**讲清楚：
> 它是什么、为什么需要、代码里长什么样、写错会怎样。
>
> 流程性说明（谁先调谁、三级回退、收养的完整时序）在
> [关键逻辑梳理-byCYX 第 3 章](关键逻辑梳理-byCYX.md)；本文只讲**知识点**，
> 两者配合读：先在这里建立概念，再去那边看流程。

## 目录

1. [CPython 对象模型](#1-cpython-对象模型)
2. [shiboken：绑定生成器与运行时](#2-shiboken绑定生成器与运行时)
3. [PyCapsule：把 C++ 数据挂到 Python 对象上](#3-pycapsule把-c-数据挂到-python-对象上)
4. [shared_ptr 与 Python 生命周期的交点](#4-shared_ptr-与-python-生命周期的交点)
5. [数值与容器语义速查](#5-数值与容器语义速查)
6. [常见坑清单](#6-常见坑清单)
7. [本仓库代码地图](#7-本仓库代码地图)
8. [自己动手验证](#8-自己动手验证)
9. [术语表](#9-术语表)

---

# 1 CPython 对象模型

## 1.1 一切从 `PyObject` 开始

CPython 的 C API 里，**任何 Python 值都是 `PyObject*`**。它的定义只有两个字段：

```c
typedef struct _object {
    Py_ssize_t ob_refcnt;      // 引用计数
    PyTypeObject *ob_type;     // 指向“类型对象”
} PyObject;
```

变长对象（`int`、`bytes`、`tuple`）在此基础上多一个 `ob_size`（`PyVarObject`）。
除此之外没有别的：Python 的“对象”到了 C 层就是一个带引用计数的指针。

两条直接推论：

1. **所有权靠引用计数**：谁 `INCREF` 谁负责 `DECREF`，归零时调用类型的
   `tp_dealloc` 析构。
2. **行为靠 `ob_type` 分派**：`len(x)`、`x[k]`、`x == y` 最终都变成
   `Py_TYPE(x)` 里某个函数指针的调用。

```cpp
PyObject* py = /* ... */;
const char* typeName = Py_TYPE(py)->tp_name;   // 例如 "variant_binding.DemoNode"
```

## 1.2 `PyTypeObject`：类型对象

### 它是什么

`PyTypeObject` 是 CPython 描述“一个类型”的 C 结构体。它同时承担两件事：

- **元数据**：类型叫什么、实例占多少字节、有哪些基类、是不是可子类化；
- **虚函数表**：析构、哈希、比较、下标、迭代、属性读写、调用……

Python 层看到的 `type(x)` 返回的就是它。执行 `class Foo: ...` 时，CPython
在堆上创建一个 `PyTypeObject`（heap type）；内建类型（`int`、`str`）则是
编译进解释器的静态对象（static type）。类型对象自己的类型是**元类**，
通常是 `type`。

### 常用字段

| 字段 | 含义 | 本仓库是否用到 |
| --- | --- | --- |
| `tp_name` | 类型名，通常 `"module.Class"` | ✅ 匹配 hook、报错文案 |
| `tp_basicsize` / `tp_itemsize` | 实例大小 | ❌ |
| `tp_dealloc` | 引用归零时的析构 | ❌（shiboken 生成） |
| `tp_hash` / `tp_richcompare` | 能不能当 dict 键、怎么比较 | 间接（键可哈希性） |
| `tp_flags` | `BASETYPE` / `HEAPTYPE` / `IMMUTABLETYPE`… | ❌ |
| `tp_base` / `tp_bases` / `tp_mro` | 继承关系 | 间接（`isinstance`） |
| `tp_dict` | 类字典（方法、属性） | ❌ |
| `tp_new` / `tp_init` | 构造 | ❌（shiboken 生成） |
| `tp_as_number` / `tp_as_sequence` / `tp_as_mapping` | 数值/序列/映射协议槽 | ❌ |

### 相关 API

| API | 用途 | 注意 |
| --- | --- | --- |
| `Py_TYPE(obj)` | 取 `ob_type` | 不检查空指针 |
| `PyObject_TypeCheck(obj, type)` | 只看 `Py_TYPE(obj)`：等于 `type`，或在其 C 层 MRO 里；不读 `__class__`、不执行 Python 代码 | 快、可内联；不会失败 |
| `PyObject_IsInstance(obj, cls)` | 等价于 Python 的 `isinstance()`：可能调用元类的 `__instancecheck__`、读 `__class__`、识别 ABC 虚拟子类 | 返回 1/0/**-1**；-1 表示异常已设置 |
| `PyType_Ready(&t)` | 初始化静态类型 | 模块初始化时调 |
| `PyType_FromSpec(&spec)` | 用 `PyType_Spec` 建堆类型（稳定 ABI 推荐） | 新式 C 扩展 |

### 两者到底差在哪

`PyObject_TypeCheck` 的语义等价于：

```c
Py_TYPE(obj) == type || PyType_IsSubtype(Py_TYPE(obj), type)
```

它只沿 `tp_mro`（C 层继承图）走，**不执行任何 Python 代码，也没有失败模式**。
所以 `class Dog(Animal)` 的实例能命中；`True` 对 `int` 也能命中（bool 的 MRO
里有 int）。

`PyObject_IsInstance` 是 `isinstance()` 的 C 实现，多做了三件事：

1. 先找 `type(cls).__instancecheck__`，有就调用——自定义元类可以完全改写判定；
2. 默认实现里，C 层判断失败后会再读 `obj.__class__`，与 `Py_TYPE(obj)` 不同就
   递归判定（`unittest.mock` 就是靠这个“装成”别的类）；
3. 支持 ABC 虚拟子类：`isinstance([], collections.abc.Sequence)` 为真，
   尽管 `list` 的 C 层 MRO 里没有 `Sequence`。

| 场景 | `PyObject_TypeCheck` | `PyObject_IsInstance` |
| --- | --- | --- |
| `class Dog(Animal)` 的实例对 `Animal` | 1 | 1 |
| `True` 对 `int` | 1 | 1 |
| `[]` 对 `collections.abc.Sequence` | 0 | 1 |
| 元类重写了 `__instancecheck__` | 0 | 按重写结果 |
| `Mock(spec=Foo)` 对 `Foo` | 0 | 1 |
| 出错 | 不可能 | 返回 -1，异常已设置 |

选择规则：只关心“C++ wrapper 到底是什么类型”时用 `PyObject_TypeCheck`
（快、无错误路径）；要问“Python 眼里它算不算这个类”时才用
`PyObject_IsInstance`，并且必须处理 -1。

### 静态类型的最小示意

```c
typedef struct {
    PyObject_HEAD
    int value;
} Tiny;

static void tiny_dealloc(PyObject* self)
{
    Py_TYPE(self)->tp_free(self);       // 先释放成员，再交还内存
}

static PyTypeObject TinyType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "tiny.Tiny",        // 注意：带模块前缀
    .tp_basicsize = sizeof(Tiny),
    .tp_dealloc   = tiny_dealloc,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_new       = PyType_GenericNew,
    .tp_doc       = "minimal example",
};

PyMODINIT_FUNC PyInit_tiny(void)
{
    PyType_Ready(&TinyType);
    // ... PyModule_AddObject ...
}
```

这段只是示意，不在本仓库编译；本仓库**不手写任何 C++ 类的 `PyTypeObject`**，
因为 shiboken 已经为每个绑定类生成了一个（见第 2 章）。我们只“查”和“用”。

### 本仓库的用法

```cpp
// binding/core/variant_python.h
struct Hook {
    ...
    PyTypeObject* pyType = nullptr;   // 绑定的 Python 类型；自定义 hook 为 nullptr
};

// binding/core/variant_python.cpp — findHookForObject
const int rc = PyObject_IsInstance(py, reinterpret_cast<PyObject*>(hook.pyType));
if (rc == 1) return &hook;            // 命中（含 Python 子类）
if (rc < 0)  PyErr_Clear();           // 类型检查自己炸了：跳过这个 hook
```

为什么可以 `reinterpret_cast`：CPython 保证 `PyTypeObject` 以 `PyObject`
头开始（`PyVarObject` 也兼容），而所有公开 API 的参数都是 `PyObject*`。
**注意**：开了 `Py_LIMITED_API` 后结构体是不透明的，连 `tp_name` 都看不见，
所以 `variant_python.h` 在包含 `Python.h` 之前做了：

```cpp
#if defined(Py_LIMITED_API)
#undef Py_LIMITED_API
#endif
```

我们链接的是完整版 `pythonXY.dll`，并不承诺 stable ABI。

## 1.3 `tp_name` 与“短类名”

### `tp_name` 的格式

- 内建类型：`"int"`、`"str"`、`"dict"` —— 没有模块前缀；
- 用 `class` 定义的堆类型：**`"模块名.类名"`**，如 `"mymod.MyClass"`；
- 嵌套类：`"mymod.Outer.Inner"`（`__qualname__` 带点）；
- 函数里定义的类：`"mymod.f.<locals>.C"`。

`tp_name` 是 C 层的名字，不等于 Python 的 `__name__`（后者只是类名），
也不完全等于 `__qualname__`（后者不带模块）。shiboken 的类型表里
`fullName` 也是 `"模块名.类名"` 这个风格。

### 短类名

**短类名 = 去掉最后一个 `.` 之前的所有内容。**

```cpp
const char* shortTypeName(const char* fullName)
{
    const char* dot = std::strrchr(fullName, '.');
    return dot ? dot + 1 : fullName;
}
```

| 来源 | 完整名 | 短类名 |
| --- | --- | --- |
| 内建 | `int` | `int` |
| shiboken `getTypes().fullName` | `variant_binding.DemoNode` | `DemoNode` |
| Python 类 | `mymod.MyClass` | `MyClass` |
| 嵌套类 | `mymod.Outer.Inner` | `Inner` |
| 局部类 | `mymod.f.<locals>.C` | `C` |

### 为什么需要它

因为**两个注册表用的名字不同**：

1. `Shiboken::Conversions::SpecificConverter(name)` 按**短类名**查转换器
   （生成代码里登记的就是 `"DemoNode"`）；
2. `Shiboken::Module::getTypes(module)` 枚举出来的 `fullName` 是
   **`"module.Class"`**；
3. Python 对象的 `tp_name` 也是 `"module.Class"`。

注册时拿到的参数是 `(moduleName, className)`，要把这三处对上，就得把 2、3
归一成短类名再和 1 比较。

### 风险

- **短类名不唯一**：两个模块都导出 `DemoNode` 时转换器表会冲突（谁先注册/谁后
  覆盖取决于加载顺序），所以自定义类型的类名要全局唯一；本仓库统一用
  `variant_binding` 下的名字。
- **嵌套类的短名会丢外层**：`Outer.Inner` 和另一个模块的 `Inner` 无法靠名字区分。
- **名字只能用来“查表”，不能用来“判定类型”**：真正的判定是
  `pyType` + `PyObject_IsInstance`。名字匹配只是快路径。

### 两段实际代码

```cpp
// 注册时：从模块类型表里按短类名找 PyTypeObject*
PyObject* module = PyImport_ImportModule(moduleName);
Shiboken::Module::TypeInitStruct* types = Shiboken::Module::getTypes(module);
for (int i = 0; types && types[i].fullName; ++i) {
    if (std::strcmp(shortTypeName(types[i].fullName), className) == 0) {
        found = types[i].type;
        break;
    }
}
```

```cpp
// Python -> C++ 时：先按短类名精确匹配，再退回 isinstance（覆盖子类）
const char* name = shortTypeName(Py_TYPE(py)->tp_name);
for (const Hook& hook : g_hooks) {          // 第一轮：名字 + isinstance
    if (hook.className == name &&
        PyObject_IsInstance(py, reinterpret_cast<PyObject*>(hook.pyType)) == 1)
        return &hook;
}
for (const Hook& hook : g_hooks) {          // 第二轮：纯 isinstance
    if (PyObject_IsInstance(py, reinterpret_cast<PyObject*>(hook.pyType)) == 1)
        return &hook;
}
```

第二轮的典型命中场景：`class MyNode(DemoNode)`。它的 `tp_name` 是
`"__main__.MyNode"`，短类名 `MyNode` 和注册的 `DemoNode` 不相等，
但 `isinstance(obj, DemoNode)` 为真。

## 1.4 引用计数契约

C API 的每个函数都会声明自己返回的引用是哪种：

| 契约 | 含义 | 例子 |
| --- | --- | --- |
| **new reference（新引用）** | 调用方拿到一份所有权，用完必须 `Py_DECREF` | `PyImport_ImportModule`、`PyObject_GetAttrString`、`PyLong_FromLongLong`、`PyDict_New`、`PyCapsule_New`、`converter.toPython` |
| **borrowed reference（借用）** | 不增加计数，调用方**不得** DECREF | `Py_TYPE`、`PySys_GetObject("path")`、`PyDict_Next` 取出的键/值、`Py_None` |
| **stolen reference（窃取）** | 调用方把所有权交出去，**不得**再 DECREF | `PyList_SET_ITEM`、`PyTuple_SET_ITEM`、`PyModule_AddObject`（成功后） |

不窃取的容器写入 API：`PyDict_SetItem`、`PyList_Append`、`PyList_Insert`、
`PyObject_SetAttrString` —— 它们只增加容器里那一份引用，调用方仍持有自己的。

### 仓库里的实例

```cpp
// 1) 新引用 -> 用完 DECREF
PyObject* module = PyImport_ImportModule(moduleName);   // new
...
Py_DECREF(module);

// 2) 类型表里的指针是 borrowed，但我们承诺“返回给调用方的 PyTypeObject*
//    是已 INCREF 的”，所以补一次
Py_INCREF(found);
return found;

// 3) GETATTR 是 new；无论成败都要收尾
PyObject* existing = PyObject_GetAttrString(wrapper, kSharedOwnerAttr);
if (existing) {
    const bool valid = PyCapsule_IsValid(existing, kSharedOwnerCapsule) != 0;
    Py_DECREF(existing);          // 查完就还
    ...
}
PyErr_Clear();                    // 属性不存在：清掉 AttributeError 再继续

// 4) CAPSULE 是 new；SetAttr 不窃取 -> 挂上后自己 DECREF
PyObject* capsule = PyCapsule_New(holder, kSharedOwnerCapsule, dtor);
PyObject_SetAttrString(wrapper, kSharedOwnerAttr, capsule);  // 不窃取
Py_DECREF(capsule);               // 属性表持有一份，我们这份还掉

// 5) LIST_SET_ITEM 窃取 -> 不能再 DECREF
PyList_SET_ITEM(frame.container, frame.index++, obj);
```

### 失败路径的铁律

**函数返回 `nullptr` / `false` 之前，必须把本函数创建的所有新引用还掉。**
这是 C 扩展里最常见的内存泄漏来源。本仓库的写法：

- `mapToPython` / `listToPython`：任何一步失败，先 `Py_DECREF` 已建好的容器和键；
- `PySink`：失败时置 `m_failed`，后续事件变成空操作，析构函数统一 `reset()`
  释放未闭合的容器与待写入的键；
- `resolveType`：`import` 失败先 `PyErr_Clear()` 再返回 `nullptr`（否则残留的
  异常会污染下一次 C API 调用）。

## 1.5 错误指示器与返回值约定

CPython 的异常不是 C++ 异常，而是**每线程一个的“错误指示器”**：

- 出错的函数按返回类型返回哨兵值：指针类返回 `NULL`，整数类返回 `-1`；
- 同时设置错误指示器（`PyErr_SetString` / `PyErr_Format` / 底层 API 自己设）；
- 调用方用 `PyErr_Occurred()` 判断“有没有挂异常”；
- 把 `NULL` 一路返回给 Python 解释器，解释器看到错误指示器就抛异常。

### `-1` 的二义性

```cpp
const long long v = PyLong_AsLongLong(py);
if (v == -1 && PyErr_Occurred()) {
    // 真的是错：溢出或不是 int
}
```

- `PyLong_AsLongLong(-1)` 返回 `-1` 且**不设异常**是合法的；
- 超范围时返回 `-1` 并设 `OverflowError`。

所以不能只判 `v == -1`。仓库里的 `pythonIntToVariant` 正是靠这个区分，
先试有符号、失败再试无符号：

```cpp
bool pythonIntToVariant(PyObject* py, vt::Variant& out)
{
    const long long v = PyLong_AsLongLong(py);
    if (!(v == -1 && PyErr_Occurred())) {
        out = vt::Variant(static_cast<std::int64_t>(v));
        return true;
    }
    PyErr_Clear();                                  // 关键：先清掉上一次的 OverflowError

    const unsigned long long u = PyLong_AsUnsignedLongLong(py);
    if (u == static_cast<unsigned long long>(-1) && PyErr_Occurred()) {
        PyErr_Clear();
        PyErr_SetString(PyExc_OverflowError,
                        "integer does not fit in 64 bits (signed or unsigned)");
        return false;
    }
    out = vt::Variant(static_cast<std::uint64_t>(u));
    return true;
}
```

要点：

1. `PyErr_Clear()` 不是“吞异常”，而是**为下一次尝试准备干净状态**；
2. 最终失败要重新设置一个**对用户有意义**的异常（这里统一成 `OverflowError`）；
3. 这个 helper 同时被“值分支”和“dict 键分支”调用——键分支曾经漏掉无符号
   回退，表现是 `{2**63: 1}` 抛 `OverflowError`，抽成共用函数后修复。

### 常用异常

| C 名字 | Python 异常 | 本仓库使用场景 |
| --- | --- | --- |
| `PyExc_TypeError` | `TypeError` | 类型不匹配、没有转换器、键不可哈希 |
| `PyExc_OverflowError` | `OverflowError` | 整数超出 64 位 |
| `PyExc_RuntimeError` | `RuntimeError` | C++ 异常透传、`resolveType` 找不到类型 |

### 不要“静默清空”

`PyErr_Clear()` 会丢掉异常。本仓库只在两种情况下用它：

1. 有明确的回退路径（int 的有符号→无符号；属性不存在→走收养）；
2. 类型检查探针失败（`PyObject_IsInstance` 返回 -1）——不能因为某个 hook
   的类型检查坏了就让整次转换失败。

其余情况一律把错误留给调用方。

### 嵌入式运行器的特例

```cpp
const int rc = PyRun_SimpleString("...");
return rc == 0 ? 0 : 1;
```

`PyRun_SimpleString` 出错时会**先打印 traceback、再清空错误指示器**，所以
返回后 `PyErr_Occurred()` 已经为假。判断成败只能看返回值，不能看错误指示器。

## 1.6 GIL

GIL（Global Interpreter Lock）是 CPython 的全局锁：**任何 Python C API 调用
都必须在持有 GIL 的线程里执行**。否则轻则计数错乱，重则崩溃。

### 本仓库的三类位置

| 位置 | GIL 状态 | 说明 |
| --- | --- | --- |
| `vpy::toPython` / `fromPython` 等入口 | 已持有 | 它们由 Python 调用（shiboken 包装函数或绑定模块内部），调用时天然持锁 |
| `PythonKeepAliveDeleter::release()` | **不确定** | 最后一个 `shared_ptr` 可能在任意 C++ 线程析构，必须自己取锁 |
| `exe/main.cpp` | 主线程持有 | `Py_Initialize()` 之后主线程持 GIL，`Py_Finalize()` 结束 |

deleter 的写法：

```cpp
void release() const noexcept
{
    if (!owner) return;
    PyObject* object = owner;
    owner = nullptr;

    if (!Py_IsInitialized())
        return;                         // 解释器已 finalize：不能再碰 Python

    PyGILState_STATE gil = PyGILState_Ensure();
    Py_DECREF(object);
    PyGILState_Release(gil);
}
```

`PyGILState_Ensure()` 会：当前线程已持 GIL 就返回“已持有”，否则阻塞获取；
`Release()` 恢复原状。它可以在任意线程调用，是 C++ 回调里最常用的入口。

`Py_IsInitialized()` 的判断同样重要：进程退出阶段 Python 可能已经
`Py_Finalize()`，此时再 DECREF 就是访问已释放的解释器状态。

### 本仓库的边界

没有对 free-threading（3.13 `--disable-gil`）做适配，所有假设都建立在
“有 GIL”之上。也没有把长耗时 C++ 计算主动释放 GIL（`Py_BEGIN_ALLOW_THREADS`）——
当前转换都是内存操作，不做这种优化。

## 1.7 内建类型速查

### None

单例。返回 `None` 用 `Py_RETURN_NONE`（等于 `Py_INCREF(Py_None); return Py_None;`）。

### bool

**`bool` 是 `int` 的子类**，`PyLong_Check(True)` 为真。所以判定顺序必须是
`PyBool_Check` 在前：

```cpp
if (PyBool_Check(py)) {                 // 先判 bool
    out = vt::Variant(py == Py_True);   // Py_True / Py_False 是单例
    return true;
}
if (PyLong_Check(py)) {                 // 再判 int
    return pythonIntToVariant(py, out);
}
```

反过来（C++ -> Python）没有这个问题：Variant 的 Bool 和 Int 是两个槽，
直接 `PyBool_FromLong` / `PyLong_FromLongLong`。

### int

- Python 的 `int` 是**任意精度**（`PyLongObject`），没有 64 位上限；
- C++ 侧只有 64 位，因此边界转换必然可能失败：
  `PyLong_AsLongLong`（有符号）、`PyLong_AsUnsignedLongLong`（无符号，负数直接
  `OverflowError`）、`PyLong_FromLongLong` / `PyLong_FromUnsignedLongLong`；
- 超出 64 位：本仓库统一报 `OverflowError`；
- `sys.maxsize` 只是“平台指针宽度能表示的最大值”，不等于 Python int 的上限。

### float

`PyFloat_Check` / `PyFloat_AsDouble` / `PyFloat_FromDouble`。Python 的 `float`
就是 C 的 `double`（IEEE 754 双精度），没有单独的 float32。

### str

- CPython 内部不是 UTF-8，而是“按最大码点选 1/2/4 字节”的紧凑表示；
- 传给 C++ 时用 `PyUnicode_AsUTF8AndSize(py, &size)`：返回**缓存的 UTF-8 视图**，
  指针在对象存活期间有效，长度可能包含内嵌 `\0`，所以要同时用 `size`，
  不能只靠 `strlen`；
- C++ -> Python 用 `PyUnicode_DecodeUTF8(data, size, "strict")`：非法 UTF-8
  直接报错，不静默替换。

### list / tuple

- `PyList_New(n)` 创建的列表**每个槽是未初始化的 `NULL`**，必须逐个填满；
- `PyList_SET_ITEM` 窃取引用、不做边界检查（下标必须已经分配过）；
- `PyList_Append` / `PyList_Insert` 不窃取，且会检查边界；
- `tuple` 在 Python -> C++ 方向被当作 list 接收（`PyTuple_Check` 同一个分支）。

### dict

- 键必须可哈希（有 `tp_hash`）且可比较（有 `tp_richcompare`）；
- `PyObject_Hash(obj)` 失败返回 `-1` 并设 `TypeError`——本仓库在 `PySink`
  写 dict 前显式调用它，让“不可哈希的键”当场响亮失败：

```cpp
if (PyObject_Hash(obj) == -1) {
    Py_DECREF(obj);
    m_failed = true;        // 保留 Python 的 TypeError，不退回字符串键
    return;
}
```

- `PyDict_SetItem` 不窃取键/值，失败返回 `-1`；
- `PyDict_Next(dict, &pos, &key, &value)` 取出的都是**借用引用**，且遍历期间
  不能修改 dict；
- Python 3.7+ 保证插入顺序，但 `VariantMap` 是 `std::map`（有序），所以
  C++ -> Python 的顺序由 C++ 容器决定，不是 Python 的插入顺序。

### 通用序列

`PySequence_Size` / `PySequence_GetItem` 对 list/tuple 都适用；
`GetItem` 返回新引用。本仓库在 Python -> C++ 的 list/tuple 分支里使用它们，
避免为两种类型写两套循环。

---

# 2 shiboken：绑定生成器与运行时

## 2.1 它是什么

shiboken（本仓库用 shiboken6）不是 Python 的一部分，而是一套
**“C++ ↔ Python 绑定生成器 + 运行时库”**：

```
C++ 头文件 + typesystem.xml
        │  shiboken6-generator
        ▼
生成的 wrapper C++ 源码（每个类一个 PyTypeObject、一组转换器、一个方法表）
        │  与 shiboken6 运行时库一起编译
        ▼
variant_binding.pyd  ← Python 里 import 的就是它
```

分工：

- **生成器**（构建期）：读头文件和 typesystem 规则，生成包装代码；
- **运行时**（`shiboken6` 库）：提供 `Shiboken::Conversions`、
  `Shiboken::Object`、`Shiboken::Module` 等 C++ API，被包装代码和我们的代码调用。

本仓库的 typesystem 在 `binding/core/`，绑定目标名是 `variant_binding`。

## 2.2 wrapper 对象

C++ 对象 `T` 到了 Python 侧不是 `T` 本身，而是一个 **wrapper**（类型为
`SbkObject` 的 Python 对象）：

```
Python 变量 ──▶ wrapper(SbkObject)
                    ├─ 指向 C++ T 的裸指针
                    ├─ 所有权标志：owning / non-owning
                    └─ 额外属性表（我们的 capsule 就挂在这里）
```

- **owning**：wrapper 销毁时 `delete T`（Python 里 `new` 出来的对象）；
- **non-owning**：只借用，不删（C++ 交给 Python 看的对象）。

判定 API：

```cpp
auto* sbk = reinterpret_cast<SbkObject*>(wrapper);
bool owns = Shiboken::Object::hasOwnership(sbk);
bool valid = Shiboken::Object::isValid(py);   // wrapper 是否还指向有效对象
```

本仓库在 `sharedToPython` / `sharedStore` 里用这两个判断来拒绝“所有权冲突”
和“借用对象入库”。

## 2.3 转换器（converter）

每个绑定类型都注册了一个转换器，**按短类名索引**：

```cpp
Shiboken::Conversions::SpecificConverter converter("DemoNode");
if (!converter.isValid()) { /* 这个名字没被任何绑定模块导出过 */ }

// C++ -> Python
PyObject* py = converter.toPython(&cppValue);   // 值类型：传 const T*
PyObject* py = converter.toPython(&ptr);        // 指针类型：传 T* const*

// Python -> C++
T value;   converter.toCpp(py, &value);         // 值类型：写入 T
T* ptr = nullptr;
converter.toCpp(py, &ptr);                      // 指针类型：写入 T*
```

关键语义：

- `toPython` 返回**新引用**；
- `toCpp` **按转换器登记的类型**解释对象，不看 Python 的运行时类型；
- 两者失败时返回 `nullptr` / 设置错误指示器（`toCpp` 是 `void`，靠
  `PyErr_Occurred()` 判断）。

本仓库的薄封装：

```cpp
template <class T>
PyObject* pointerToPython(const std::any& value, const char* name)
{
    if (T* const* pp = std::any_cast<T*>(&value)) {
        if (!*pp) Py_RETURN_NONE;
        SpecificConverter converter(name);
        if (!converter.isValid()) {
            PyErr_Format(PyExc_TypeError, "no shiboken converter named '%s'", name);
            return nullptr;
        }
        return converter.toPython(pp);   // pp 是 T* const*，正是指针转换器要的形态
    }
    PyErr_Format(PyExc_TypeError, "std::any does not hold %s*", name);
    return nullptr;
}
```

**为什么注册名必须和 shiboken 的类名完全一致**：转换器表就是按这个字符串查的。
写成 `"demo.DemoNode"` 会 `isValid() == false`；写成 `"Demonode"` 也一样。
本仓库的注册入口把模块名和类名分开传，就是为了同时满足
“枚举类型表（fullName）”和“查转换器（短名）”两种需求。

## 2.4 `Module::getTypes` 与 `resolveType`

shiboken 生成的模块里有一张类型表，每项是：

```cpp
struct TypeInitStruct {
    const char* fullName;   // "variant_binding.DemoNode"
    PyTypeObject* type;
};
// 以 fullName == nullptr 结束
```

`Shiboken::Module::getTypes(module)` 返回这张表的指针。本仓库的
`resolveType(moduleName, className)`：

1. `PyImport_ImportModule(moduleName)` 拿到模块（新引用）；
2. 遍历类型表，用 `shortTypeName(fullName) == className` 匹配；
3. `Py_DECREF(module)`；
4. 对找到的 `PyTypeObject*` 做 `Py_INCREF`，把“借用”升级成“新引用”；
5. 找不到就设 `RuntimeError` 返回 `nullptr`。

只在**注册时执行一次**，结果存进 `Hook::pyType`，之后 Python -> C++ 的
每次转换都只做 `isinstance`。

## 2.5 所有权 API

| API | 作用 | 本仓库使用 |
| --- | --- | --- |
| `Shiboken::Object::hasOwnership(SbkObject*)` | 这个 wrapper 是否拥有 C++ 对象 | ✅ 拒绝所有权冲突 / 只收养 owning 对象 |
| `Shiboken::Object::isValid(PyObject*)` | wrapper 是否仍指向有效对象 | ✅ 收养前检查 |
| `Shiboken::Object::setCppOwnership` / `setPythonOwnership` | 改写所有权标志 | ❌（由共享/收养机制间接决定） |

## 2.6 版本与构建约束

- `variant_binding.pyd` 与 **Python 次版本**和 **shiboken6 版本**绑定：
  换 Python 3.10 → 3.11 必须重编；
- 绑定目标**不能 Debug**：MSVC 的 `_DEBUG` 会让 `pyconfig.h` 链接
  `pythonXY_d.lib`（官方 Windows 发行版没有），并打开 `Py_DEBUG` 引用
  未导出的调试符号；
- 生成物默认落在 `build/binding/<cfg>/`，而 runner / demo.py 从
  `build/bin/<cfg>/` 加载——所以 CMake 里加了 POST_BUILD 复制；
  排查时先打印 `vb.__file__`，别只看构建日志。

## 2.7 我们为什么不用自己写 `PyTypeObject`

对 C++ 类手写静态类型意味着要自己处理：

- 每个方法的参数解析、返回值包装；
- 继承、虚函数、Python 子类；
- 所有权（谁 delete）；
- 类型转换（`int`/`str` 参数、默认参数、重载）。

这些 shiboken 已经生成好了。**我们的角色是“使用已有的类型对象”**：
拿 `PyTypeObject*` 做 `isinstance`，拿转换器做双向转换，不重复造轮子。

---

# 3 PyCapsule：把 C++ 数据挂到 Python 对象上

## 3.1 它是什么

PyCapsule 是 CPython 提供的**不透明指针盒子**：

```c
PyObject* PyCapsule_New(void* pointer, const char* name,
                        PyCapsule_Destructor destructor);
int       PyCapsule_IsValid(PyObject* capsule, const char* name);
void*     PyCapsule_GetPointer(PyObject* capsule, const char* name);
```

它只装三样东西：一个 `void*`、一个名字字符串、一个可选的析构函数。
没有属性、没有方法，Python 代码拿不到里面的指针（只能看到
`<capsule object "vt::shared_owner">` 这样的 repr）。

## 3.2 三条必须知道的行为

1. **名字做精确匹配**：`IsValid` / `GetPointer` 的名字必须和创建时一致，
   防止 A 模块把 B 模块的盒子当自己的用。
2. **析构函数在胶囊被回收时调用**，参数是胶囊对象本身；此时仍可
   `GetPointer` 取出指针做清理。析构函数里不能抛 C++ 异常。
3. **它就是普通 Python 对象**，遵守引用计数：挂到属性上、放进容器都会 INCREF；
   最后一个引用消失时调用析构函数。

## 3.3 本仓库的用法

`sharedToPython` 把一份 `shared_ptr<T>` 拷贝放到堆上，再装进胶囊，
挂到 wrapper 的 `_vt_shared_owner` 属性：

```cpp
auto* holder = new std::shared_ptr<T>(sp);      // 拷贝一份，强引用 +1
PyObject* capsule = PyCapsule_New(holder, "vt::shared_owner",
                                  &sharedCapsuleDestructor<T>);
PyObject_SetAttrString(wrapper, "_vt_shared_owner", capsule);  // 不窃取
Py_DECREF(capsule);                             // 属性表持有唯一一份
```

胶囊的析构函数：

```cpp
template <class T>
void sharedCapsuleDestructor(PyObject* capsule)
{
    auto* holder = static_cast<std::shared_ptr<T>*>(
        PyCapsule_GetPointer(capsule, kSharedOwnerCapsule));
    delete holder;                              // shared_ptr 析构，强引用 -1
}
```

结构：

```
Python 看到的                          真实结构
SharedNode 实例 ── raw ──▶ T
      │
      └─ "_vt_shared_owner" ──▶ capsule ── void* ──▶ shared_ptr<T> ──▶ 控制块
```

生命周期链：

- wrapper 活着 ⇒ 属性活着 ⇒ 胶囊活着 ⇒ 那份 `shared_ptr` 拷贝活着
  ⇒ C++ 侧计数不归零 ⇒ `T` 不会被提前析构；
- wrapper 被回收 ⇒ 属性表释放 ⇒ 胶囊析构 ⇒ `delete holder` ⇒ 计数 -1
  ⇒ 归零时由**最后一个持有者**析构 `T`。

## 3.4 为什么用胶囊，而不是别的

| 方案 | 问题 |
| --- | --- |
| 塞进 Python 普通属性 | Python 属性只能是 `PyObject*`，放不下 C++ 的 `shared_ptr` |
| 手写一个新的 Python 类型 | 要注册类型、处理继承/回收，为一个内部字段不值得 |
| shiboken 的 `smart-pointer-type` | Python 侧会看到 `SharedNodePtr` 包装类型，`isinstance(x, SharedNode)` 失败 |
| **PyCapsule** | 标准、轻量、不暴露给用户；Python 侧仍是普通 `T` |

胶囊**本身不拥有对象**：它只是“把一段 C++ 数据挂到 Python 对象上”的容器；
“拥有”来自里面那份 `shared_ptr` 拷贝。

## 3.5 注意点

- 名字是“类型安全开关”，但只防名字，不防内容：如果别人在同名属性上塞一个
  名字相同、payload 不是 `shared_ptr<T>` 的胶囊，`IsValid` 会放行，
  属于调用方违约；
- 胶囊不可 pickle，也不应该暴露给业务代码；
- 胶囊丢了但 C++ 侧还有 `shared_ptr`：对象不会死（wrapper 是非 owning 的），
  只是 Python 侧无法再通过它延长生命周期——这正是预期行为。

---

# 4 shared_ptr 与 Python 生命周期的交点

这一章只列概念，完整时序见
[关键逻辑梳理 3.4–3.7](关键逻辑梳理-byCYX.md)。

## 4.1 控制块

`std::shared_ptr<T>` 由“指向 T 的指针 + 指向控制块的指针”组成。控制块里有：

- 强引用计数；
- 弱引用计数；
- **deleter**（计数归零时执行什么）；
- 分配器（可选）。

关键点：**只有共享同一个控制块的 shared_ptr 才共享计数**。对同一个 `T*`
分别构造两次 `shared_ptr<T>(raw)`，会得到两个控制块，最后 delete 两次。

## 4.2 wrapper 的 owning / non-owning

- Python 里 `T()` 创建的对象：wrapper owning，`T` 的生死由 Python 管；
- C++ 交给 Python 看的对象：wrapper non-owning，Python 不负责 delete；
- 两者混用会出现“双方都 delete”或“双方都不 delete”。

`Shiboken::Object::hasOwnership()` 就是读这个标志。

## 4.3 两个方向的所有权桥

### C++ -> Python：capsule 续命

见第 3 章。C++ 的 `shared_ptr` 拷贝挂在 wrapper 上，保证 C++ 计数不归零。

### Python -> C++：收养（adoption）

Python 创建的对象归 Python 所有。C++ 想要一个“能延长寿命的共享句柄”，
但不能接管所有权（否则 Python 的 wrapper 晚点析构会双删）。做法：

```cpp
std::shared_ptr<T> sp(raw, PythonKeepAliveDeleter<T>(py));  // deleter 不 delete T
registerAdopted<T>(raw, sp);                                // raw -> weak_ptr 记表
out = std::move(sp);
```

- deleter 构造时 `Py_XINCREF(wrapper)`，最后一个 shared_ptr 消失时
  `Py_DECREF(wrapper)`；
- 因此只要 C++ 还持有引用，wrapper 就不会被回收，`T` 也不会被删；
- 登记表用 `weak_ptr`，表本身不续命；**清理是惰性的**：只有 `findAdopted(raw)`
  按同一地址来查、且 `weak_ptr` 已过期时才 `erase`；同一地址被新对象复用时
  `registerAdopted` 直接覆盖旧条目；进程退出随静态对象一起释放。没有全局扫描，
  也没有容量上限——大量“只收养一次、地址不再复用”的对象会留下 map 节点和
  共享控制块（每条约几十字节），属于已知的惰性残留，换地址复用时自然收敛。

结果：对象寿命 = Python 引用与 C++ `shared_ptr` 里**更长的那个**。

注意 `weak_ptr::expired()` 的含义是“**C++ 侧已没有强引用**”，不是“T 已经死了”：
`releasePayload()` 之后 C++ 的 `shared_ptr` 归零，但 Python 变量可能还拿着 wrapper，
T 仍然活着；此时表项已经过期，下次再收养会建新的控制块。

## 4.4 四种场景

| 场景 | 对象来源 | wrapper 拥有 T？ | 处理 |
| --- | --- | --- | --- |
| A | C++ 工厂返回 `shared_ptr` | 否 | 造 wrapper + 挂 capsule |
| B | 同一个值再转一次 | —— | 已有 capsule → 返回同一个 wrapper |
| C | Python 创建、已被收养 | 是 | 直接返回 wrapper，**不再挂 capsule** |
| D | Python 创建、从未收养，却有 C++ `shared_ptr` | 是 | 所有权冲突 → `TypeError` |

场景 C 不挂 capsule 的原因：capsule 持有 `shared_ptr`，而收养的 deleter 又持有
wrapper，会形成 `wrapper → capsule → shared_ptr → deleter → wrapper` 的环，
永远释放不掉。

## 4.5 谁负责 `delete T`

`PythonKeepAliveDeleter` 不 `delete T`，**只用于“Python 创建的对象”那条路**；
C++ 创建的对象走的是普通控制块，deleter 仍然是 `delete T`。

| 对象由谁创建 | 控制块里的 deleter | 谁 `delete T` |
| --- | --- | --- |
| C++ 工厂（`make_shared` / `shared_ptr<T>(new T)`） | 默认 `delete`（或原来的自定义 deleter） | 最后一个强引用：Variant、capsule、C++ 局部拷贝中的最后一个 |
| Python `T(...)` | `PythonKeepAliveDeleter`（只 DECREF wrapper） | Python wrapper 的 `tp_dealloc`（wrapper 引用归零时） |

C++ 路径的要点：

1. `converter.toPython(&raw)` 造出的 wrapper 是 **non-owning** 的，Python 回收它
   不会 `delete T`；
2. `sharedToPython` 用 `new std::shared_ptr<T>(sp)` 把**同一个控制块**的拷贝放进
   capsule，**不替换 deleter**——C++ 原来怎么删，之后还是怎么删；
3. wrapper 被回收 → 属性表释放 → capsule 析构 → `delete holder` → 强计数 -1；
4. 最后一个强引用（可能在 C++ 局部变量、Variant 或 capsule 里）归零时，
   控制块执行原来的 deleter → `delete T`。

所以两条路的区别不是“删不删”，而是**谁持有最后一份强引用**：C++ 路径由控制块删，
收养路径由 Python wrapper 删。两边都只删一次。

---

# 5 数值与容器语义速查

## 5.1 整数

| 方向 | 规则 |
| --- | --- |
| C++ -> Python | `Int` → `PyLong_FromLongLong`；`UInt` → `PyLong_FromUnsignedLongLong`；Python 侧都是 `int`，符号位不可见 |
| Python -> C++ | 先试 `long long`，失败再试 `unsigned long long`；都失败 → `OverflowError` |
| 边界 | `[INT64_MIN, INT64_MAX]` 进 `Int`；`(INT64_MAX, UINT64_MAX]` 进 `UInt`；超出 64 位报错 |

Python 的 `int` 本身没有 64 位限制：

```python
2 ** 1000          # 合法，约 160 字节
```

所以“C++ 能收多少”是 C++ 侧的限制，不是 Python 的限制。

## 5.2 bool

Python 里 `True == 1`、`hash(True) == hash(1)`，且 `bool` 是 `int` 子类。
C++ 侧 `Variant` 的 Bool / Int 是两个槽，不会混淆；Python -> C++ 靠
`PyBool_Check` 先判。

## 5.3 容器键

| 方向 | 行为 |
| --- | --- |
| C++ -> Python（`PySink`） | **保留真实键类型**：`map<int,int>` → `{7: 2}` |
| C++ -> 序列化（`VariantSink`） | 键统一写成字符串（JSON 只能这样） |
| Python -> C++（`fromPython`） | `VariantMap` 只有字符串键，`int` 键转十进制字符串；其他键类型 `TypeError` |
| 不可哈希键 | `PyObject_Hash` 失败 → 保留 `TypeError`，不退回字符串键 |

所以 `{7: "a"}` 进 C++ 再出来是 `{"7": "a"}`；要拿回 `int` 键，在 C++ 侧用
`getAs<map<int,string>>`。这是**有意的方向不对称**：只有“C++ 知道类型”的
那一侧能保留键类型。

---

# 6 常见坑清单

| 现象 | 根因 | 修法 |
| --- | --- | --- |
| 随机崩溃 / 计数异常 | 把 borrowed 引用当 new 引用 DECREF，或反之 | 查 API 文档的引用契约；拿不准就查 CPython 头文件注释 |
| 出错路径内存泄漏 | `return nullptr` 前没 DECREF 已建对象 | 每个失败分支逐项回收；容器类写统一 `reset()` |
| 明明返回了 NULL，Python 却抛了上一次的异常 | 残留错误指示器 | 调可能失败的 API 前先 `PyErr_Clear()` |
| `PyLong_AsLongLong` 把合法值 -1 当成错误 | 没判 `PyErr_Occurred()` | `v == -1 && PyErr_Occurred()` |
| `True` 被当成整数 1 | `PyLong_Check` 在 `PyBool_Check` 之前 | bool 分支必须前置 |
| 找不到转换器 | 注册名带了模块前缀 / 大小写不一致 | 用短类名，和 shiboken 生成名逐字符一致 |
| Python 子类对象转换失败 | 只按 `tp_name` 精确匹配 | 加 `isinstance` 兜底 |
| 线程里 DECREF 崩溃 | 没持 GIL | `PyGILState_Ensure/Release`；先判 `Py_IsInitialized` |
| Python 退出时崩 | `Py_Finalize` 之后还有 deleter 在跑 | 同上；必要时放弃引用而不是硬用 |
| 编译报 `tp_name` 未定义 / `PyList_SET_ITEM` 缺失 | 开了 `Py_LIMITED_API` | 包含 `Python.h` 前 `#undef`，或改用稳定 ABI 的替代 API |
| 改了 binding 却看不到变化 | 加载了旧 `.pyd` | 打印 `vb.__file__`；确认 POST_BUILD 复制到 `build/bin/<cfg>` |
| 绑定在 Debug 下链接失败 | `_DEBUG` → `pythonXY_d.lib` / `Py_DEBUG` | binding 只用 Release / RelWithDebInfo |

---

# 7 本仓库代码地图

| 文件 | 职责 | 关键点 |
| --- | --- | --- |
| `binding/core/variant_python.h` | Hook 表、四种注册、转换器薄封装、共享/收养、`PyCapsule` 辅助 | `pointerToPython` / `valueToPython` / `sharedToPython` / `sharedStore` / `PythonKeepAliveDeleter` |
| `binding/core/variant_python.cpp` | `toPython` / `fromPython` 分派、`PySink`、`resolveType`、`findHookForObject`、`pythonIntToVariant` | 分派顺序、引用计数收尾、dict 键字符串化 |
| `binding/core/glue/variant_convert.cpp` | typesystem 用 `// @snippet` 抽取的转换片段 | **不直接编译**，由 shiboken 生成代码引用 |
| `exe/main.cpp` | 内嵌 Python 的 runner | `Py_Initialize` / `PyRun_SimpleString` / `Py_Finalize`；按返回值判成败 |
| `py_scripts/demo.py` | Python 侧往返演示与断言 | 键类型、整数边界、共享/收养行为 |
| `tests/shared_ownership_test.py` | 共享所有权场景 | wrapper/capsule/adoption 的生命周期 |
| `tests/`（C++ 侧） | 嵌入式跑 Python 断言 | 退出码 = 失败数 |

`vpy::` 的公开入口：

```cpp
bool  canConvertToPython(const vt::Variant&);   // 只预检类型，不构造对象
PyObject* toPython(const vt::Variant&);         // 新引用
bool  fromPython(PyObject*, vt::Variant&);      // 写 out
PyObject* mapToPython(const vt::VariantMap&);
PyObject* listToPython(const vt::VariantList&);
```

---

# 8 自己动手验证

## 8.1 看真实的类型名

```python
import variant_binding as vb

print(vb.__file__)                    # 确认加载的是新 pyd
print(vb.DemoNode.__name__)           # DemoNode
print(vb.DemoNode.__module__)         # variant_binding
print(vb.DemoNode.__qualname__)       # DemoNode
print(vb.DemoNode.__flags__)          # 位标志，可看 HEAPTYPE/BASETYPE
```

C++ 侧想看同一个名字：

```cpp
PyObject* py = /* ... */;
std::printf("tp_name = %s\n", Py_TYPE(py)->tp_name);
```

## 8.2 验证胶囊确实在

```python
node = vb.makeSharedNode(7)           # 走 C++ shared_ptr -> Python
owner = getattr(node, "_vt_shared_owner")
print(owner)                          # <capsule object "vt::shared_owner" at ...>
print(type(owner))                    # <class 'PyCapsule'>
```

## 8.3 验证引用契约导致的 bug 长什么样

把某处 `Py_DECREF` 注释掉，再跑 `py_scripts/demo.py`：短时间看不出问题，
长时间/大批量转换后内存持续上涨——引用计数错误通常不是立刻崩，而是泄漏。
反过来多 DECREF 一次，表现为随机崩溃或 `Fatal Python error`。

## 8.4 验证 GIL 缺失

在 C++ 线程里直接 `Py_DECREF`（不走 `PyGILState_Ensure`）跑多次，
通常很快出现 `Fatal Python error: PyEval_SaveThread: ...` 或计数错乱。
这也是为什么 deleter 里必须取 GIL。

---

# 9 术语表

| 术语 | 含义 |
| --- | --- |
| `PyObject` | 所有 Python 值的 C 表示：引用计数 + 类型指针 |
| `PyTypeObject` | 类型对象，描述布局与行为；`type(x)` 返回它 |
| `tp_name` | 类型的 C 名字，通常 `"module.Class"` |
| 短类名 | `tp_name` 去掉最后一个 `.` 前的部分，如 `DemoNode` |
| new reference | 调用方获得所有权，必须 DECREF |
| borrowed reference | 只借用，不得 DECREF |
| stolen reference | 调用方把所有权交给被调函数，不得再 DECREF |
| 错误指示器 | 每线程的异常状态；`PyErr_Occurred()` 查询 |
| GIL | CPython 全局解释器锁；C API 调用必须持有 |
| shiboken | C++ ↔ Python 绑定生成器 + 运行时库 |
| wrapper / `SbkObject` | shiboken 生成的 Python 对象壳，内含 C++ 指针与所有权标志 |
| converter | shiboken 的类型转换器，按短类名注册 |
| owning / non-owning | wrapper 是否负责 delete 背后的 C++ 对象 |
| PyCapsule | 装 `void*` + 名字 + 析构函数的 Python 对象 |
| 收养（adoption） | Python 拥有对象时，C++ 取得一个不负责 delete、只负责续命的 `shared_ptr` |
| 控制块 | `shared_ptr` 的计数与 deleter 所在的内存块 |
| 值遍历（visitValue） | 把任意容器变成事件流的编译期递归（本文不展开，见设计说明） |
