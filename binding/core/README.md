# binding/core —— 生产内核

这里只放 **Variant ↔ Python 的转换核心**，不含任何 demo 类型或演示函数。
生产部署拷贝本目录即可；`binding/demo/`、`py_scripts/`、`demo/`、`tests/` 都不要带。

## 需要拷贝的文件

| 文件 | 作用 |
| --- | --- |
| `variant_python.h` / `variant_python.cpp` | Hook 表、`toPython/fromPython`、`registerPointerType/ValueType/SharedType`、共享所有权（capsule/收养）、`PySink` |
| `glue/variant_convert.cpp` | 由 typesystem 用 `// @snippet` 抽取的转换片段，不直接编译 |
| `variant_bindings.xml` 的 CORE 段 | 见下面"typesystem" |

> **标准容器不需要任何注册**：`Variant::fromValue` 遇到 `vector/map/set/pair/array`
> （及其任意嵌套）时，会按 `vt/container_traits.h` 自动登记遍历函数（编译期递归 +
> `ValueSink` 事件流），`toPython` 的扩展槽分支用 `PySink` 把事件直接写成原生
> list/dict。旧的 `container_hooks.*`（逐类型手写 hook）已删除，内嵌容器不再需要
> 单独适配。Python 侧映射键保留真实类型（`map<int,int>` -> `{7: 2}`），
> 只有序列化侧才把键写成文本。

## typesystem

`binding/variant_bindings.xml` 顶部有两条标记：

```xml
<!-- ============================== CORE BEGIN ============================== -->
...vt::Variant / vt::VariantMap / vt::VariantList 的 primitive-type 与转换规则...
<!-- =============================== CORE END =============================== -->
```

把这一段复制到你自己的 `package` 里（`package` 名改成你的模块名），
并确保 `glue/variant_convert.cpp` 的相对路径正确。demo 段的函数与类型全部不要。

> 为什么不能拆成两个 XML：shiboken 一个模块只接受一个 typesystem 文件，
> 所以内核段与 demo 段共用一个文件，靠注释标记分区。
>
> **实测**：只保留 CORE 段时，VariantMap/VariantList 规则里
> `%CONVERTTOPYTHON[std::unordered_map<...>]` / `%CONVERTTOPYTHON[std::vector<...>]`
> 会强制生成对应的内置容器转换器；签名里**不需要**再出现这些容器
> （验证方式：临时删掉 `echoVector`，binding 仍能生成并编译）。

## CMake

```cmake
add_library(variant_python_core STATIC
    <copy>/core/variant_python.cpp)
target_include_directories(variant_python_core
    PUBLIC  <copy>/core
    PRIVATE ${VT_PYTHON_ROOT}/include)
target_link_libraries(variant_python_core
    PUBLIC variant_core Shiboken6::libshiboken)
target_compile_options(variant_python_core PRIVATE /utf-8
    "$<$<CONFIG:Debug>:/UPy_LIMITED_API>")
```

生成的 wrapper 需要：

- `target_include_directories(... PRIVATE <copy>/core)`（`#include "variant_python.h"`）；
- 强制包含（MSVC）：`/FI<copy>/core/variant_python.h`；
- 链接 `variant_python_core`。

## 注册入口

生产环境写自己的注册函数，不要链 `variant_python_bridge`：

```cpp
bool registerMyTypes()
{
    bool ok = vpy::registerPointerType<MyNode>("my_module", "MyNode");
    ok = vpy::registerValueType<MyColor>("my_module", "MyColor") && ok;
    ok = vpy::registerSharedType<MyShared>("my_module", "MyShared") && ok;
    return ok;
}
```

只让标准容器跨语言、不注册任何自定义类型时，这个函数可以整个省略。

## 硬约束

- 必须在包含 `Python.h` 之前 `#undef Py_LIMITED_API`（`variant_python.h` 已处理）；
- 绑定目标只能 **Release / RelWithDebInfo** 构建（官方 Python 无 `python39_d.lib`）；
- 自定义 primitive-type 的 target-to-native 必须留 `check="true"` 兜底分支，
  否则参数不匹配会触发 shiboken 的 `seterror_argument did not receive a result` fatal；
- 绑定类（`object-type`/`value-type`）必须放在全局命名空间；
  `primitive-type` 条目名可以带命名空间。
