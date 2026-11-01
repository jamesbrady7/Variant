# C++ 语义与接口设计笔记

> 本文记录本项目在设计与讨论中反复用到、也最容易踩坑的 C++ 知识点。
> 偏"为什么这样设计"，实现细节见 [设计说明](设计说明.md)。

---

## 1. 值类别、移动与参数传递

### 1.1 三个基本事实

1. **按值参数是一个新对象**：函数调用时用实参去初始化它。
2. 用**左值**初始化 → 拷贝构造；用**右值**（xvalue/prvalue）初始化 → 移动构造。
3. **`std::move` 本身不移动任何东西**，它只是 `static_cast<T&&>`，把表达式标记成"可以被搬走"。

### 1.2 `Variant v = std::move(map);` 到底发生了什么

```cpp
vt::VariantMap map;
vt::Variant v = std::move(map);
```

```cpp
// 1) std::move(map) 只是类型转换，得到 VariantMap&&（xvalue）
// 2) 构造 Variant 的参数：
VariantMap value = std::move(map);           // 移动构造，map 被搬空
// 3) 进入 Variant 的构造函数：
make_shared<VariantMap>(std::move(value));   // 再移动一次，进入堆对象
```

成本（`unordered_map`）：

```
一次 make_shared 分配 + 两次 O(1) 指针偷取，零元素拷贝
```

对比传左值 `Variant v = map;`：一次分配 + 一次 **O(n)** 整表深拷贝。

### 1.3 sink（接收）参数惯用法

```cpp
void set(std::string s) { member_ = std::move(s); }   // s 是 sink 参数
```

| 调用 | 实际发生 |
| --- | --- |
| `set(lvalue)` | 拷贝进参数 + 移动进成员 |
| `set(std::move(x))` | 移动进参数 + 移动进成员 |
| `set(std::string("x"))` | C++17 保证省略，直接构造进参数 + 移动进成员 |

**一个签名同时支持拷贝与移动，由调用方决定成本。** 这是"按值"最大的价值。

### 1.4 那为什么还需要 `T&&`？

分两种，规则完全不同：

**非模板 `T&&`（右值引用）**

- 省掉"移动进参数"这一跳：右值直接移动进成员；
- 只接受右值，拒绝隐式转换（`void f(std::string&&)` 不接受 `"literal"`）；
- 当**移动本身很贵**时才有明显收益（例如 `std::array<char, N>` 的移动是逐元素拷贝，移动两次就是两倍）。

**模板 `T&&`（转发引用）**

- 目的不是省移动，而是**保持实参的值类别、const 和具体类型**：

```cpp
template <class... Args>
T* create(Args&&... args) {
    return new T(std::forward<Args>(args)...);   // 原样转发，一次都不搬
}
```

- 按值模板参数会丢掉引用/const，还可能发生**切片**（派生类 → 基类）；
- `std::make_shared`、`emplace_back` 都靠它做到"在目标位置直接构造"。

### 1.5 决策表

| 意图 | 写法 |
| --- | --- |
| 接收一个值并拥有它，移动便宜（string/vector/map） | **按值** `void f(T v)` |
| 接收一个值，但移动贵/可能抛 | `void f(const T&)` + `void f(T&&)` |
| 泛型转发，保持值类别与类型 | `template<class T> void f(T&&)` + `std::forward<T>` |
| 只读借用 | `void f(const T&)`、`std::string_view` |
| 禁止隐式转换 | `void f(T&&)` 或 `explicit` 构造 |

### 1.6 常见误区

1. **有名字的右值引用是左值**：

   ```cpp
   void f(T&& x) {
       g(x);              // 调左值重载
       g(std::move(x));   // 正确
   }
   ```

2. **`const T&&` 不会移动**：移动构造需要非 const 的 `T&&`；`std::move(constObj)` 仍然深拷贝。
3. **移动后的对象是 valid-but-unspecified**：可以析构/重新赋值，但不要读它的内容。
4. **C++17 的保证省略**：`f(T{})` 的临时对象直接构造进参数，这一步不产生移动；但 `f(std::move(x))` 是真实移动。

### 1.7 本项目为什么用按值

`Variant(VariantMap value)`、`Variant(VariantList value)`、`Variant::fromValue(...)`：

- 容器移动 = 偷指针，O(1)，两次移动可忽略；
- 一个签名覆盖拷贝与移动，接口简单；
- `make_shared` 本来就要分配，移动成本不是瓶颈。

如果将来某个成员是 `std::array` 这类"移动也很贵"的类型，再改成 `const T&` + `T&&` 两个重载。

---

## 2. `std::string_view`

### 2.1 本质

```cpp
struct string_view { const char* data; std::size_t size; };
```

- **不拥有**数据、不分配、不释放；
- 拷贝 O(1)（两个机器字）；
- 只读视图（底层字符串仍可被修改）；
- **不保证 `'\0'` 结尾**，可以包含 `'\0'`；
- 不延长底层数据的生命周期。

### 2.2 生命周期规则（最重要）

```cpp
std::string_view bad() {
    std::string local = "hello";
    return local;                       // 悬空
}

std::string_view sv = std::string("hi"); // 分号处临时对象销毁，sv 悬空

std::string s = "hello";
std::string_view view = s;              // 只要 s 活着且不重分配就有效
s += " world";                          // 若触发重分配，view 悬空
```

原则：

- **参数**可以放心用 `string_view`（调用方保证实参活过本次调用）；
- **返回值/成员变量/异步回调捕获**不要用，除非能证明数据活得更久；
- 需要 C 接口（`c_str()`、`printf("%s")`）或需要修改时，用 `std::string`。

### 2.3 转换成本

| 转换 | 成本 |
| --- | --- |
| `std::string` → `string_view` | O(1)，隐式 |
| `const char*` → `string_view` | O(n) `strlen`，隐式 |
| 字符串字面量 → `string_view` | O(1)，长度编译期已知 |
| `string_view` → `std::string` | O(n) 分配 + 拷贝，显式 |

### 2.4 在本项目中的位置

| 位置 | 用法 | 安全性 |
| --- | --- | --- |
| 键参数 `operator[]/find/at` | 避免调用边界分配 | 调用期间有效即可 |
| `Path(std::string_view)` / `Path::parse` | 把文本**拷贝进 Path 自己的 `std::string`** | Path 不依赖调用方寿命 |
| `Variant::asStringView()` | 指向 Variant 内部字符串 | 仅在 Variant 存活且未修改时有效 |
| `"a.b"_path` | 指向字面量静态存储 | 永久有效 |

注意：C++17 的 `unordered_map` **没有异构查找**（`is_transparent` 是 C++20），
所以键参数进了哈希表查找时仍会构造一次 `std::string`；热路径另有
`const std::string&` 重载来避免这次构造。

---

## 3. 键 / 路径的类型区分

### 3.1 访问契约

```cpp
using namespace vt::literals;

m["a.b"];          // 字符串 = 字面键 "a.b"
m["a.b"_path];     // Path   = 路径 a -> b
m[0];              // 下标
```

Variant / VariantMap / VariantList 共用同一套方法名，靠**参数类型**区分语义。
早期用 `literal()/findLiteral()` 两套名字的方案已否决：

- 方法名要记，容易用错；
- 对 map 用户来说，`m["a.b"]` 的直觉必须是键；
- 猜字符串内容（有没有点号）会导致"数据一变语义就变"。

### 3.2 `operator[]` 的重载陷阱（MSVC C2593）

如果同时提供：

```cpp
Variant& operator[](std::size_t);
Variant& operator[](const char*);
```

那么 `v[0]` 会二义——字面量 `0` 同时是整型和**空指针常量**，两个候选的
标准转换等级相同。因此 `operator[]` 只保留：

```cpp
operator[](std::string_view)   // 键
operator[](std::size_t)        // 下标
operator[](const Path&)        // 路径
```

字符串字面量和 `std::string` 都会隐式转成 `string_view`，功能不受影响。
`find/at/contains/getAt` 没有数字重载冲突，可以保留 `const std::string&`
热路径重载。

### 3.3 `Path` 的构造函数为什么是 `explicit`

```cpp
vt::Path p(std::string_view);      // explicit
```

- 防止 `root[text]` 被悄悄当成路径；
- 运行时字符串转路径必须显式写 `vt::Path::parse(text)` 或 `vt::Path(text)`，
  调用点自解释；
- 也不会让 `"a.b"` 在重载解析时同时匹配 `string_view` 与 `Path`。

### 3.4 运行时字符串当路径

```cpp
const vt::Path p = vt::Path::parse(runtimeText);   // 失败抛 VariantError
vt::Path p2;
std::string err;
if (vt::Path::tryParse(runtimeText, p2, &err)) { /* 用 p2 */ }
```

`_path` 只适用于字面量；运行时字符串用 `parse/tryParse`。要复用就提前构造 `Path`。

---

## 4. 类型注册

### 4.1 存储层是开放的

```cpp
struct Probe { int id; };
vt::Variant v = vt::Variant::fromValue(Probe{7});
v.get<Probe>();          // 不需要注册
```

`Variant` 的扩展槽就是 `std::any`；注册不是存储门槛。

### 4.2 注册只服务边界

| 表 | 用途 |
| --- | --- |
| `vt::TypeRegistry` | 稳定名字（describe / 日志 / 将来的序列化） |
| `vpy` Hook 表 | C++ ↔ Python 转换器 |

`vpy::registerPointerType<T>` / `registerValueType<T>` **一次调用同时写两张表**。
名字被另一种类型占用时返回 `false`，并且不安装 Hook。

### 4.3 边界预检

```cpp
vpy::canConvertToPython(tree);   // C++ 侧预检：递归检查 map/list/扩展槽
```

未注册类型跨界仍然会**响亮失败**（`TypeError` 带类型名），预检只是让调用方
能提前判断，而不是在深层元素上才炸。Python 侧不暴露预检函数。

---

## 5. C++17 边界

本项目所有设计均可在 C++17 内实现。

| 特性 | 版本 | 用途 |
| --- | --- | --- |
| `std::variant` / `std::any` / `std::optional` | C++17 | 存储与取值 |
| `std::string_view` | C++17 | 借用字符串 |
| `if constexpr` | C++17 | 类型分支 |
| `std::in_place_type` | C++17 | 构造 variant 槽位 |
| 用户定义字面量 `_path` | C++11 | 路径字面量 |
| `operator[]` 参数类型重载 | C++98 | 键 / 路径区分 |
| `std::shared_ptr` / `use_count` | C++11 | COW |

刻意避开、改用 C++17 替代的 C++20 特性：

| C++20 | 本项目的 C++17 做法 |
| --- | --- |
| `unordered_map::contains` / 透明查找 | `find() != end()`；显式 `const std::string&` 重载 |
| `std::span` | `(pointer, size)` |
| `std::format` | `std::ostringstream` / `snprintf` |
| `<=>` | 手写 `operator==` |
| `std::in_range` | 手写 min/max 检查 |
| `constexpr` 动态分配 | 路径在运行时解析 |

唯一会想要 C++20 的地方：让 `"a.b"_path` 在编译期完成语法校验。
C++17 下是运行时解析，热循环请预编译 `vt::Path`。

---

## 6. 其他工程实证

### 6.1 COW 与"非 const 读取"陷阱

`Variant` 的 map/list 是写时复制。**只读必须走 const 引用**：

```cpp
const vt::Variant& ro = value;
ro.asMap().size();       // 不 detach
value.asMap().size();    // 非 const，可能克隆整张表
```

benchmark 初版在 500 万次只读里用了非 const 的 `asMap().size()`，
每次都克隆 10k 条 map，直接跑了 2 分钟。

### 6.2 MSVC 的 `std::any` 是 64 字节

直接把 `std::any` 作为 `std::variant` 的 alternative，会让
`sizeof(vt::Variant)` 变成 72。套一层 `shared_ptr<VariantExt>` 后降到 40
（此时由 `std::string` 的 32 字节决定）。

### 6.3 `std::string` 与 `QString` 的拷贝语义不同

- `QString` 隐式共享：拷贝 O(1)；
- `std::string` 深拷贝（短串有 SSO）。

所以长字符串拷贝的基准里 Qt 更快（约 0.45x），这是语义差异，不是实现缺陷。

### 6.4 shiboken 的三条硬限制

详见 [设计说明 第 5 章](设计说明.md)：typesystem 不接受 `ns::Class`、
命名空间 primitive-type 进入 Python 签名后会 fatal、自动绑定方法不支持
`PyObject*` 形参/返回值。本项目因此使用 `PyObject` 直通 + C++ 手工递归转换。

### 6.5 密集数值：一个 Variant 装一个数组

不要把每个数值都塞进一个 Variant。把整个数组作为一个扩展值：

```cpp
std::vector<int> data = ...;
vt::Variant v = vt::Variant::fromValue(std::move(data));   // 一次移动，零元素拷贝

const vt::Variant& cv = v;
const std::vector<int>* p = cv.extensionPtr<std::vector<int>>();   // 零拷贝读取
```

内存对比（100 万个 `short`）：

| 形态 | 估算 |
| --- | ---: |
| `std::vector<short>` | ~2 MB |
| 一个 Variant 装 `vector<short>` | ~2 MB + 40 B |
| 100 万个独立 Variant（列表） | ~40 MB |
| 100 万个 VariantMap 条目 | ~100 MB（键 + 节点开销） |

三个注意点：

1. **只读要用 const 引用**：非 const 的 `extensionPtr<T>()` 会触发扩展盒 detach
   （深拷贝整个数组）。
2. **`get<std::vector<int>>()` 是整份拷贝**；热路径用 `extensionPtr`。
3. **Variant 拷贝共享扩展盒**（O(1)），但之后任何非 const 写都会 clone 整个数组。
   如果本来就想共享可变大数组，直接存 `std::shared_ptr<std::vector<int>>`；
   想共享只读则存 `std::shared_ptr<const std::vector<int>>`。

跨 Python 分两种情况：**函数签名**里直接写 `std::vector<int>` 时，shiboken 内置容器
转换器自动生效；但把 `vector<int>` 放进 Variant **扩展槽**再跨界，需要给扩展槽类型
注册 hook（`registerCustomToPython`，本项目已在 `registerDemoTypes` 里注册）。
密集数组如果要交给 Python，转换后仍建议用 `array('i')` / numpy / `bytes`，
而不是 Python `list`（每个 Python int 约 28 字节，会把省下的内存全部吃回去）。

### 6.6 整型统一收口：无符号越界必须响亮失败

Variant 只有 Int 槽（`int64`）。所有整型构造都经过同一个 `toInt64` 收口点：
无符号值大于 `INT64_MAX` 时抛 `VariantError(OutOfRange)`，**不做 `static_cast` 回绕**
（回绕成负数比报错更难查）。要存完整 64 位无符号范围，就存 `std::any(uint64_t)` 扩展值。

踩坑：`vt::Variant v(std::uint64_t(x));` 是 **most vexing parse** —— 编译器把它解析成
"声明一个返回 Variant、参数为 `std::uint64_t` 的函数"。MSVC 报 C2751/C2059，指向
`numeric_limits<__int64>::max`，看起来像库的错。用 `static_cast<std::uint64_t>(x)`
或花括号 `{...}` 消除歧义。

---

## 7. 一页速查

```cpp
// 键 vs 路径
m["a.b"];                         // 键
m["a.b"_path];                    // 路径
m.at("a.b"_path);                 // 路径，缺失抛
m.find("a.b");                    // 键，缺失 nullptr
m.getAt<int>("a.b"_path);         // optional

// 运行时字符串当路径
vt::Path p = vt::Path::parse(text);

// 移动
vt::Variant v = std::move(map);   // 移动进去，零元素拷贝
vt::Variant c = map;              // 拷贝（值语义）

// 只读
const vt::Variant& ro = v;
ro.asMap().size();                // 不触发 COW detach

// 注册
vpy::registerPointerType<MyNode>("variant_binding", "MyNode");  // 名字 + 转换器
vpy::canConvertToPython(tree);    // 跨界预检
```
