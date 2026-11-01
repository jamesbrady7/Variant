# Variant —— 不依赖 QVariant 的类型擦除容器

纯 C++17 的 `vt::Variant` + 友好的路径访问 + 可运行时注册的扩展类型 + shiboken
双向转换 + 自包含的 JSON / 二进制序列化。核心库不依赖 Qt、不依赖 Python、不依赖第三方库。

```cpp
using namespace vt::literals;

vt::Variant root;
root["root.subtree_1.subsubtree_1"_path][0] = 42;   // Path = 路径
root["config.window.width"_path] = 1280;

root["a.b"] = 1;                                    // 字符串 = 字面键 "a.b"

const vt::Variant* p = root.find("root.subtree_1"_path);       // 不抛，缺失返回 nullptr
auto n = root.getAt<int>("root.subtree_1.subsubtree_1[0]"_path); // std::optional<int>
root.at("missing.key"_path);                                     // 抛结构化 VariantError
```

**访问契约只有一条**：字符串是字面键，`Path` 是路径。两者靠类型区分，
不猜字符串内容，也没有 `literal()` 之类的第二套方法名。

## 组成

| 目录 | 内容 |
| --- | --- |
| `lib/` | `variant_core`：Variant / VariantMap / VariantList / Path / VariantDocument / TypeRegistry / JSON / 二进制 / Codec |
| `demo/` | `variant_demo`：DemoNode（对象）/ DemoColor（值）/ SharedNode（共享）/ PluginBase（多态基类）+ 真实签名演示函数 |
| `tests/` | 223 项核心单测 + 1821 项序列化检查 + 33 项共享所有权 + 31 项继承/虚函数检查 |
| `bench/` | 与 QVariant 的对比基准（Qt5 Core，可选） |
| `binding/core/` | **生产内核**：Variant/VariantMap/VariantList ↔ Python 转换、注册入口、typesystem CORE 段（见 `binding/core/README.md`） |
| `binding/demo/` | 演示专用注册与共享工厂胶水（不随生产部署） |
| `exe/` | 内嵌 Python 的 C++ runner |
| `py_scripts/` | Python 往返演示 |
| `docs/` | [使用手册](docs/使用手册.md)、[设计说明](docs/设计说明.md)、[C++ 语义与接口笔记](docs/C++知识点整理.md)、[升级说明](docs/升级说明.md) |

## 构建（Windows / MSVC 2019 / 本机 Qt5 与 Python 3.9）

```bash
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release
```

可选开关：`-DVT_BUILD_TESTS=OFF`、`-DVT_BUILD_BENCH=OFF`、`-DVT_BUILD_PYTHON=OFF`。

## 运行

```bash
build/bin/Release/vt_tests.exe          # 核心单测，退出码 = 失败数
build/bin/Release/vt_serial_tests.exe   # JSON / 二进制 / codec 测试
build/bin/Release/vt_shared_tests.exe   # 共享所有权（C++ runner 启动 Python 测试）
build/bin/Release/vt_plugin_override_test.exe  # Python 继承 C++ + 虚函数 VariantMap 往返 + 释放验证（31 项）
build/bin/Release/variant_demo_app.exe  # C++ + Python 往返演示（50 项断言）

# benchmark 需要 Qt5 的 DLL
PATH="/d/Qt/5.15.2/msvc2019_64/bin:$PATH" build/bin/Release/vt_bench.exe

# Python 侧也可以直接跑
python py_scripts/demo.py
```

> 绑定目标只支持 **Release**：Windows 官方 Python 发行版不带 `python39_d.lib`，
> Debug 下 `_DEBUG` 会让 Python 头文件启用 `Py_DEBUG`，引用 release 库中不存在的
> `_Py_RefTotal` 等符号。core / tests / bench 的 Debug 配置不受影响。

## 关键设计（详见 docs）

- `std::variant` 提供 7 个内建热槽，`std::any` 作为第 8 槽承载运行时注册类型；
  any 外再套一层 `shared_ptr`，把 MSVC 下 64 字节的 any 从对象布局里挪走
  （`sizeof(Variant)` 72 → 40）。
- map/list 用 `shared_ptr` 间接持有并实现写时复制；**只读请用 `const` 引用**，
  非 const 的 `asMap()` 会 detach。
- 路径可预编译成 `vt::Path`，热循环里复用（39ns vs 281ns / 次）。
- Python 侧不出现 `Variant` 类：C++ 函数签名直接写 `vt::Variant` / `vt::VariantMap`，
  Python 侧自动对应原生 `None/bool/int/float/str/list/dict`；标准容器
  （`vector<Variant>`、`unordered_map<string,Variant>` 等）不逐个注册，
  由 shiboken 内置容器转换器 + 元素规则递归完成。
- C++ 侧存扩展类型**不需要注册**（就是 `std::any`）；注册只服务于边界：
  `registerPointerType/registerValueType` 一行同时登记稳定名字和 Python 转换器。
  C++ 侧可用 `vpy::canConvertToPython(tree)` 预检整棵树。
- 内建容器：`vector<Variant>` / `map<string,Variant>` 规范化为原生 List/Map；其余容器整体走紧凑扩展槽。`vt/container_traits.h` 覆盖 `vector/list/deque/set/unordered_set/array/map/unordered_map/pair`，`Variant::fromValue` 首次见到类型时自动登记遍历函数与 plain codec——**任意嵌套（如 `vector<vector<int>>`）无需注册**即可跨语言、序列化、`getAs<T>` 往返。
- 一份编译期递归 + 两个 Sink：`VariantSink` 产出 Variant 正规形（JSON/二进制），`PySink` 直接产出 `PyObject*`。因此 **C++ → Python 保留真实键类型**（`map<int,int>` 是 `{7: 2}`），JSON 侧键仍是字符串；反向 dict → C++ 仍字符串化，由 `getAs<T>` 解析回来。
- 共享所有权：C++ 侧存 `std::shared_ptr<T>`，Python 侧仍是 `T`；内部用 PyCapsule/keep-alive deleter 管生命周期，借用对象在边界被拒绝。详见使用手册 4.8。
- 序列化：`vt::json::write/read`、`vt::binary::write/read`，以及流/文件接口 `writeTo/readFrom`、`writeFile/readFile`；扩展类型通过
  `registerValueCodec/registerPointerCodec` 注册一次，JSON 和二进制共用；
  JSON 用 `{"$vt":"1","$type":"...","$data":...}` 信封恢复类型，未注册类型**报错而不是静默降级**。
