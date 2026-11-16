> 这份笔记只讲两件事：容器怎么跨过类型擦除（注册表 + Sink），以及 Python 边界怎么用它们。
> 主线一句话：**编译期（模板）负责递归，运行期（typeid 查表）只负责把递归接到边界上。**

# 1 容器的注册与解析

```c++
// adapter.cpp
void registerBuiltinCodecs()
{
    static std::once_flag once;
    std::call_once(once, [] {
        // 用户如果先注册了同类型 codec，就保留用户的实现。
        detail::ensureContainerAdapters<std::vector<int>>();
        detail::ensureContainerAdapters<std::vector<std::string>>();
        detail::ensureContainerAdapters<std::unordered_map<std::string, std::string>>();
        detail::ensureContainerAdapters<std::unordered_map<int, int>>();
    });
}

/**
 * @brief 保证容器类型 T 的遍历函数与 codec 已登记（每个类型只做一次）。
 * @param [in] T 容器类型
 */
template <class T>
void ensureContainerAdapters()
{
    using U = std::decay_t<T>;
    if constexpr (!is_container_v<U>) {
        return;
    } else {
        static std::once_flag once;
        std::call_once(once, [] { installContainerAdapters<U>(); });
    }
}
```

注册函数内部干的事情是：

```c++
using NormalizerFn = void (*)(const std::any&, ValueSink&);

/**
 * @brief 为一个容器类型登记遍历函数（+ 自动 codec，若用户尚未注册）。
 *
 * 遍历函数里做的只有一件事：`visitValue(*value, sink)`——递归在编译期展开，
 * 运行期只按 `typeid` 找到这个入口。
 *
 * 由 `Variant::fromValue` 在第一次见到该类型时自动调用；只读进程（例如
 * 只读 JSON/二进制文件、从不构造该类型）需要显式调用
 * `vt::registerContainerAdapters<T>()` 作为编译期锚点。
 * @param [in] T 容器类型
 */
template <class T>
void installContainerAdapters()
{
    // 注册这条 lambda 的真正原因：**跨过类型擦除**。
    // - 模板 visitValue<T> 需要编译期知道 T；但到 Python 边界时手里只有一个
    //   `const std::any*`（运行期只剩 typeid），编译器已经不知道 T 是什么了。
    // - `Variant::fromValue<T>` 是全库唯一"既知道 T、又能留下运行期痕迹"的地方，
    //   所以在这里把 `typeid(T) -> 函数指针` 记下来；边界按 typeid 查表调用，
    //   相当于把编译期的那份递归"接"到运行期。
    // - 不注册不会影响普通的 T <-> Variant 转换（normalizeToVariant / getAs 都是
    //   模板，自己就能展开）；受影响的是"从 std::any 出发"的两个边界：
    //   vpy::toPython（查表喂 PySink）与 vpy::canConvertToPython（喂 ConvertCheckSink）。
    // - 注册只登记"入口"，写成什么由 Sink 决定——同一个入口可以喂不同 Sink，
    //   这正是 JSON（VariantSink）和 Python（PySink）共用一份递归的方式。
    registerNormalizer(std::type_index(typeid(T)), [](const std::any& any, ValueSink& sink) {
        const T* value = std::any_cast<T>(&any);
        if (!value) {
            throw VariantError(VariantErrorCode::TypeMismatch,
                               "normalizer: std::any does not hold the expected container type");
        }
        visitValue(*value, sink);
    });

    // 这里登记的是**序列化用的 codec**：`typeid(T) + 稳定名字 -> encode/decode`。
    // - encode：T -> "只含内建类型的 Variant"（内部就是 normalizeToVariant）
    // - decode：该平凡 Variant -> T（内部就是 denormalizeFromVariant）
    // - 第三个参数 true = plain codec：JSON 默认直接写数组/对象，不加 $type 信封
    // 注意：codec 表与上面的 normalizer 表是**两张独立的表**，服务两个不同边界
    // （CodecRegistry 管存盘，NormalizerRegistry 管 Python），只是恰好一起登记。
    
    // 用户先注册的 codec 优先；容器 codec 一律是 plain（JSON 写成普通数组/对象）。
    if (CodecRegistry::instance().find(std::type_index(typeid(T))) == nullptr) {
        registerValueCodec<T>(
            stableTypeName<T>(),
            [](const T& value) { return normalizeToVariant(value); },
            [](const Variant& plain, T& out) { return denormalizeFromVariant(plain, out); },
            true);
    }
}
```

所以在调用 `installContainerAdapters<T>` 之后：

1. **Python 边界**：`Variant` 扩展槽里若装着 `T`，`vpy::toPython` 能按 `typeid(T)`
   查到入口，用 `PySink` 把事件流写成 dict/list；查不到就抛 TypeError（响亮失败）。
   两点容易误解的地方：
   - 正常情况下你**不需要手动调**：`Variant::fromValue<T>` 第一次见到容器类型就会
     自动注册；手动调 `vt::registerContainerAdapters<T>()` 只服务"只读进程"
     （只读 JSON/二进制、从不 `fromValue`）。
   - 真正会漏登记的只有一条路：直接 `Variant::fromValue(std::any(vec))` 绕过模板——
     这时 Python 边界会失败，但不会是静默丢值。
2. **序列化边界**：JSON/二进制在写扩展槽时按 `typeid` 查 `CodecRegistry`，
   **没有 codec 直接失败**（`UnknownType`），不存在"不注册也能序列化"。
   编译期递归本身确实不依赖表，可序列化 API 手里只有一个 `Variant`，它必须靠表
   才能把 `std::any` 接回编译期。不注册 codec 时，你仍可以在 C++ 里自己调
   `normalizeToVariant(value)` 拿到平凡 Variant——那是另一回事，不是序列化 API。
3. **反序列化的类型找回**分两种，不要混：
   - 二进制把类型名写进了值里，读回时按名字查 `CodecRegistry` 还原 `std::any`；
   - JSON 默认 plain（没有类型名），读回来就是普通 list/map；想要原类型只能在
     调用点写 `getAs<T>(v)`（编译期），或者写的时候用信封策略。
   所以"字符串 -> 真实类型"并不需要 if-else 大表：要么按 `typeid`/名字查 codec 表，
   要么按模板参数展开 `getAs<T>`，两条路都不写类型 switch、都能由用户扩展。

注：

1. codec 只回答"这个 `std::any`（类型已定）怎么变成平凡 Variant、怎么变回来"，
   既不负责 Python，也不负责类型发现；
2. **写出**：扩展槽没有 codec -> JSON 和二进制都失败（`UnknownType`）。
   `ExtensionPolicy`（Auto / EnvelopeAll / Flatten / Reject）只决定"**有** codec 时怎么写"：
   plain 是否加 `$type` 信封、非 plain 是否允许平铺成普通数据——它不能把
   "没有 codec"变成成功；
3. **读入**：
   - 二进制：类型名写在值里，查不到 codec -> `UnknownType` 失败；
   - JSON：只有带 `$type` 的信封才去查 codec，查不到同样 **`UnknownType` 失败**，
     **不会退回来一份"解码了一半的平凡数据"**。想拿平凡数据只有两条路：
     ① 写出时用 plain（读回来天然是 list/map，再用 `getAs<T>` 还原）；
     ② 读取时把 `detectExtensions` 关掉，信封会被当成普通 map 读进来
     （`$vt/$type/$data` 三个键原样保留）——但那已经不是"解码"了。



# 2 函数解析

```c++
/** 这个是解析容器的关键，再次强调，注册容器并不是注册容器的解析方法，其实容器就算不注册也可以解析，其依赖的是 container_traits 和 Sink 事件流 **/

/**
 * @brief 编译期递归：把容器 / 叶子拆成 `ValueSink` 事件流。
 *
 * 规则与 `container_traits` 一致，与目标表示无关：
 * - `Variant` 原样交给 sink（`onVariant`）；
 * - Sequence / Pair -> List 事件；
 * - Map -> Map 事件（键形态由 sink 在 `onMapBegin` 决定）；
 * - 标量 / 字符串 -> 对应叶子事件；
 * - 其他类型 -> `onExtension`。
 *
 * 叶子不做任何猜测：`container_traits` 没有特化的类型一律原样存放。
 *
 * `Sink` 是模板参数而不是 `ValueSink&`：`normalizeToVariant` 传具体类型
 * `VariantSink` 时整棵递归可以内联（实测把 `vector<int>` 的遍历成本从
 * 手写循环的 1.8x 拉回 1.0x 附近）；只有按 `typeid` 查表得到的入口
 * （`NormalizerFn`）才用基类引用、走虚调用。
 *
 * @param [in] T 容器或叶子值
 * @param [in] Sink 接收端（`ValueSink` 或其派生类型）
 */
template <class T, class Sink>
void visitValue(const T& value, Sink& sink)
{
    using U = std::decay_t<T>;
    using Traits = container_traits<U>;

    if constexpr (std::is_same_v<U, Variant>) {
        sink.onVariant(value);
    } else if constexpr (Traits::shape == ContainerShape::Sequence) {
        sink.onListBegin(value.size());
        for (const auto& item : value)
            visitValue(item, sink);
        sink.onListEnd();
    } else if constexpr (Traits::shape == ContainerShape::Map) {
        using Key = typename Traits::key_type;
        constexpr bool convertible = map_key_traits<Key>::convertible;

        const MapKeyForm wanted = sink.onMapBegin(value.size(), convertible);
        const MapKeyForm form =
            (convertible && wanted == MapKeyForm::Text) ? MapKeyForm::Text : MapKeyForm::Typed;
        for (const auto& kv : value) {
            sink.onEntryBegin();
            if constexpr (convertible) {
                if (form == MapKeyForm::Text)
                    sink.onString(map_key_traits<Key>::toKey(kv.first));
                else
                    visitValue(kv.first, sink);
            } else {
                visitValue(kv.first, sink);
            }
            visitValue(kv.second, sink);
            sink.onEntryEnd();
        }
        sink.onMapEnd();
    } else if constexpr (Traits::shape == ContainerShape::Pair) {
        sink.onListBegin(2);
        visitValue(value.first, sink);
        visitValue(value.second, sink);
        sink.onListEnd();
    } else if constexpr (std::is_same_v<U, bool>) {
        sink.onBool(value);
    } else if constexpr (std::is_integral_v<U>) {
        // 与 `Variant` 的整型构造同一条规则（无符号越界抛 OutOfRange），
        // 但**不构造临时 Variant**——平坦容器里这是每元素一次的热路径，
        // 多一次构造 + asInt 会让遍历比手写循环慢近一倍（bench 实测）。
        if constexpr (std::is_unsigned_v<U>) {
            if (value > static_cast<U>(std::numeric_limits<std::int64_t>::max()))
                throw VariantError(VariantErrorCode::OutOfRange,
                                   "unsigned integer exceeds int64 range");
        }
        sink.onInt(static_cast<std::int64_t>(value));
    } else if constexpr (std::is_floating_point_v<U>) {
        sink.onDouble(static_cast<double>(value));
    } else if constexpr (std::is_same_v<U, std::string>) {
        sink.onString(value);
    } else if constexpr (std::is_same_v<U, std::string_view>) {
        sink.onString(value);
    } else if constexpr (std::is_same_v<U, const char*> || std::is_same_v<U, char*>) {
        if (value == nullptr)
            sink.onNull();
        else
            sink.onString(value);
    } else if constexpr (std::is_same_v<U, std::nullptr_t>) {
        sink.onNull();
    } else {
        sink.onExtension(std::any(value));
    }
}
```

无需多言，只需注意，当提取到一个类型属于容器之后，其会调用 onXXXBegin/End，我们主要看一下 Sink 的实现：

```c++
/**
 * @brief 把事件流重建为 Variant 正规形（JSON / 二进制 / codec 的编码路径）。
 *
 * 行为与改造前逐字节一致：
 * - List / Pair -> List；
 * - 可字符串化的 Map 键 -> Map（键写十进制/文本）；
 * - 不可字符串化的 Map 键 -> `[[k,v], ...]`。
 */
class VariantSink final : public ValueSink {
public:
    /// @brief 取出构建结果（调用时遍历必须已结束）。
    /// @return Variant 归一化结果
    Variant take() { return std::move(m_root); }

    /// @brief Null -> 空 Variant。
    void onNull() override { emit(Variant()); }
    /// @brief bool -> Bool 槽。
    void onBool(bool value) override { emit(Variant(value)); }
    /// @brief 整数 -> Int 槽。
    void onInt(std::int64_t value) override { emit(Variant(value)); }
    /// @brief 浮点 -> Double 槽。
    void onDouble(double value) override { emit(Variant(value)); }

    /// @brief 字符串 -> String 槽；Text 形态映射里条目开头的那个是键。
    void onString(std::string_view value) override
    {
        // Text 形态的映射键以字符串事件发出；条目里的第一个字符串就是键。
        if (Frame* frame = textMapFrame()) {
            if (!frame->haveKey) {
                frame->key.assign(value.data(), value.size());
                frame->haveKey = true;
                return;
            }
        }
        emit(Variant(std::string(value)));
    }

    /// @brief 已成型的 Variant 原样保留（与改造前 `normalizeToVariant(Variant)` 一致）。
    void onVariant(const Variant& value) override { emit(value); }
    /// @brief 扩展槽叶子 -> 扩展槽 Variant（容器仍紧凑存放，不在此展开）。
    void onExtension(const std::any& value) override { emit(Variant::fromValue(value)); }

    /// @brief 序列开始：压入一个 List 帧。
    void onListBegin(std::size_t size) override
    {
        Frame frame;
        frame.items.reserve(size);
        m_frames.push_back(std::move(frame));
    }

    /// @brief 序列结束：弹出帧并作为 VariantList 交给父容器。
    void onListEnd() override
    {
        Frame frame = popFrame();
        emit(Variant(std::move(frame.items)));
    }

    /**
     * @brief 映射开始：按键能否字符串化选择帧形态。
     * @param [in] size 条目数
     * @param [in] keyConvertible 键类型是否可无损字符串化
     * @return MapKeyForm 实际采用的形态（不可转换时只能是 `Typed`）
     */
    MapKeyForm onMapBegin(std::size_t size, bool keyConvertible) override
    {
        Frame frame;
        frame.isMap = true;
        frame.form = keyConvertible ? MapKeyForm::Text : MapKeyForm::Typed;
        if (frame.form == MapKeyForm::Text)
            frame.map.reserve(size);
        else
            frame.items.reserve(size);
        m_frames.push_back(std::move(frame));
        return m_frames.back().form;
    }

    /// @brief 映射结束：Text -> VariantMap，Typed -> `[[k,v], ...]`。
    void onMapEnd() override
    {
        Frame frame = popFrame();
        if (frame.form == MapKeyForm::Text)
            emit(Variant(std::move(frame.map)));
        else
            emit(Variant(std::move(frame.items)));
    }

    /// @brief 条目开始：无状态（Text 帧靠 `haveKey` 区分键与值）。
    void onEntryBegin() override {}

    /// @brief 条目结束：Typed 帧把刚追加的键、值折叠成一个二元 List。
    void onEntryEnd() override
    {
        if (!m_frames.empty())
            m_frames.back().foldTypedEntryIfNeeded();
    }

private:
    struct Frame {
        bool isMap = false;
        MapKeyForm form = MapKeyForm::Text;
        VariantList items;   ///< 序列元素，或 Typed 形态映射的待折叠条目
        VariantMap map;      ///< Text 形态映射
        std::string key;     ///< Text 形态映射的当前键
        bool haveKey = false;

        /// Typed 形态：一个条目恰好追加两个值（键、值；容器值闭合时也只追加一次）。
        void foldTypedEntryIfNeeded()
        {
            if (!isMap || form != MapKeyForm::Typed || items.size() < 2)
                return;
            const std::size_t count = items.size();
            VariantList pair;
            pair.reserve(2);
            pair.push_back(std::move(items[count - 2]));
            pair.push_back(std::move(items[count - 1]));
            items.resize(count - 2);
            items.push_back(Variant(std::move(pair)));
        }
    };

    Frame* textMapFrame()
    {
        if (m_frames.empty())
            return nullptr;
        Frame& frame = m_frames.back();
        return (frame.isMap && frame.form == MapKeyForm::Text) ? &frame : nullptr;
    }

    Frame popFrame()
    {
        Frame frame = std::move(m_frames.back());
        m_frames.pop_back();
        return frame;
    }

    void emit(Variant value)
    {
        if (m_frames.empty()) {
            m_root = std::move(value);
            return;
        }
        Frame& frame = m_frames.back();
        if (frame.isMap && frame.form == MapKeyForm::Text) {
            frame.map[frame.key] = std::move(value);
            frame.key.clear();
            frame.haveKey = false;
        } else {
            frame.items.push_back(std::move(value));
        }
    }

    std::vector<Frame> m_frames;
    Variant m_root;
};
```

理解几个关键点：

1. **Frame 只表示"还没闭合的容器"**（列表 / 映射）；叶子值（bool/int/double/string）
   不会成为 Frame，它们由 `emit` 直接写进当前帧或根。所以栈深度 = 当前未闭合的
   容器层数，不是值的层数。
2. `emit` 的含义是"把一个**已经完成**的值交给上一层"：
   - 栈空 -> 它就是整棵结果（`m_root`）；
   - 在 list 帧里 -> `push_back`；
   - 在 Text 映射帧里 -> 用 `frame.key` 作键写进 `VariantMap`，然后清空键。
   容器闭合时（`onListEnd` / `onMapEnd`）先把自己变成一个 Variant，再 `emit`
   给父帧——嵌套就是靠"闭合顺序"自底向上拼起来的。
3. Text 与 Typed 两种映射帧，区别只在**键**：
   - **Text**：键已由 visitor 用 `map_key_traits<Key>::toKey` 写成文本，sink 存成
     字符串键（JSON 只能这样）；
   - **Typed**：键按原类型逐个发事件（`map<int,...>` 到 PySink 那边就是 int 键），
     VariantSink 拿到的是 `[[k,v], ...]`，反归一化时按 `key_type` 的
     `fromKey` / `getAs` 还原。
   选择权在 Sink（`onMapBegin` 返回值），visitor 只保证"不可字符串化的键不会走 Text"。
4. 遍历结束后 `m_frames` 必为空、`m_root` 就是结果；`take()` 只是把根值移出来。
5. 再次强调：注册表不是"解析方法"。`visitValue` 编译期就能递归，注册表只是让
   **运行期只剩 `std::any`** 的那条边界（Python / 序列化）能重新找回编译期的入口。

# 3 Python 转换

代码都在 `binding/core/variant_python.{h,cpp}`。核心只有两个方向：
`vpy::toPython(Variant) -> PyObject*` 和 `vpy::fromPython(PyObject*, Variant&) -> bool`，
其余都是它们的分派分支与注册进来（Hook / normalizer / codec）的叶子处理。

## 3.1 分派顺序（顺序本身就是语义）

**C++ -> Python（`toPython`）**

| Variant 槽 | 结果 | 说明 |
| --- | --- | --- |
| Null | `None` | |
| Bool | `bool` | C++ 侧槽位分开，不存在 Python 那种"bool 是 int 子类"的问题 |
| Int | `int` | `PyLong_FromLongLong` |
| Double | `float` | |
| String | `str` | `PyUnicode_DecodeUTF8(..., "strict")`，非法 UTF-8 直接报错 |
| Map | `dict` | 键按字符串解码，值递归 |
| List | `list` | 元素递归 |
| Extension | hook / PySink / TypeError | 见 3.3 |

**Python -> C++（`fromPython`）**，顺序非常关键：

1. `None` -> Null；
2. `bool` -> Bool（**必须放在 int 之前**：Python 里 `bool` 是 `int` 的子类，
   否则 `True` 会变成整数 1，丢掉类型）；
3. `int` -> Int（`PyLong_AsLongLong`，溢出会失败）；
4. `float` -> Double；
5. `str` -> String（`PyUnicode_AsUTF8AndSize` 取 UTF-8 字节）；
6. `list` / `tuple` -> VariantList（tuple 也当列表收）；
7. `dict` -> VariantMap：键只接受 `str` 或非 bool 的 `int`，
   **int 键转成十进制字符串**，其它键类型直接 TypeError；
8. 以上都不匹配 -> 查 Hook 表（`findHookForObject`），调用 `hook->store` 写入 `std::any`；
9. 仍然没有 -> TypeError。

注意这一层**不是镜像**：C++ -> Python 的 `map<int,...>` 是 int 键，但 Python -> C++
的 dict 键一律变成字符串（`VariantMap` 只有字符串键）。所以 `{7: "a"}` 进 C++ 再出来
是 `{"7": "a"}`，要拿回原类型得在 C++ 侧写 `getAs<map<int,string>>`。

## 3.2 Hook 表

```cpp
struct Hook {
    std::type_index cppType;   // 扩展槽里 std::any 的动态类型
    std::string className;     // shiboken 转换器名 / Python 短类名
    PyTypeObject* pyType;      // 绑定的 Python 类型（自定义 hook 为 nullptr）
    std::function<PyObject*(const std::any&, const char*)> toPython;
    std::function<bool(PyObject*, std::any&, const char*)> store;
};
```

四条注册入口共用这一张表：

| 注册 API | `std::any` 里存什么 | toPython | store |
| --- | --- | --- | --- |
| `registerPointerType<T>` | `T*`（裸指针，所有权不归这个库管） | `pointerToPython`：按类名取转换器，`toPython(&ptr)` | `pointerStore`：`toCpp` 取回 `T*` |
| `registerValueType<T>` | `T`（按值） | `valueToPython`：`toPython(&value)`，Python 侧是按值拷贝 | `valueStore`：默认构造 + `toCpp` 写回 |
| `registerSharedType<T>` | `std::shared_ptr<T>` | `sharedToPython`（3.5） | `sharedStore`（3.6） |
| `addCustomHook` | 任意 | 用户 lambda | 没有（只能单向） |

几个支撑 API：

- `Shiboken::Conversions::SpecificConverter(name)`：**按类名字符串**取 shiboken 的
  类型转换器。这就是为什么注册时的 `className` 必须与 shiboken 生成的类型名完全一致
  （`DemoNode` 而不是 `demo.DemoNode`）。`isValid()` 为 false 表示这个名字没被任何
  绑定模块导出过。
- `converter.toPython(const void* cpp)`：C++ -> Python，返回**新引用**。
  指针类型要传"指向指针的指针"（`T* const*`），值类型传 `const T*`。
- `converter.toCpp(PyObject*, void* out)`：Python -> C++，按转换器的类型信息写入
  `out`；它**按注册名**解释对象，不看 Python 的运行时类型。
- `resolveType(moduleName, className)`：import 模块后用
  `Shiboken::Module::getTypes()` 枚举类型表，按短类名匹配拿 `PyTypeObject*`。
  这一步只在注册时做一次。
- `findHookForObject(py)`（Python -> C++ 方向）：先按 `tp_name` 的短类名精确匹配
  并 `PyObject_IsInstance`，再退化为遍历所有带 `pyType` 的 hook 做 `isinstance`
  ——后者覆盖"Python 子类对象"这种情况。

## 3.3 扩展槽 -> Python 的三级回退

`toPython` 的 Extension 分支（`PySink::onExtension` 里同样）：

1. **hook**：有注册的类型，直接调用其 `toPython`（对象/值/共享/自定义）。
2. **normalizer**：容器类型。查 `NormalizerRegistry`，把 `ValueSink&`（这里是
   `PySink`）交给登记的遍历函数，事件流直接写成 dict/list。
   - 键保留真实类型：`map<int,int>` -> `{7: 2}`；
   - 键不可哈希（如 `map<vector<int>,int>` 的 list 键）-> `PyObject_Hash` 失败 ->
     保留 Python 的 `TypeError`，**不退回字符串键**。
3. **都没有** -> `TypeError`（"no Python converter is registered..."）。

`PySink` 的骨架：`std::vector<Frame>` 模拟"未闭合容器"的栈，`Frame`
= `{容器对象, isDict, 待写入键, list 下标}`；`deliver()` 负责把新对象放进栈顶容器
（dict 帧里第一个到达的是键——先 `PyObject_Hash` 检查，第二个才是值）。
失败时置 `m_failed`，后续事件全部变成空操作，析构 / `take()` 里统一 `reset()`
释放未完成的容器与键，避免引用计数泄漏。

`vpy::canConvertToPython` 走的是另一个 Sink（`ConvertCheckSink`）：它只沿事件流
判断"叶子有没有转换器"，**不构造 Python 对象**，因此也无法预判"键可不可哈希"。
把它当"类型是否注册"的预检，不要当"一定能转换成功"的闸门。

## 3.4 预备：wrapper、控制块、PyCapsule

后面两步（3.5 / 3.6）全都在回答"谁 delete 这个 C++ 对象"，先把四个词说清楚。

**wrapper**：shiboken 给每个绑定的 C++ 对象生成的 Python 对象壳。Python 里看到的
`SharedNode` 实例就是 wrapper；它内部存着指向 C++ `T` 的指针，并记录自己
**拥不拥有**这个 T。*owning* = wrapper 销毁时会 delete T（Python 里 new 出来的
对象）；*non-owning* = 只借用、不删（C++ 交给 Python 看的对象）。

**控制块**：`std::shared_ptr` 除了指向 T，还指向一块记录强/弱引用计数的堆内存。
只有共享同一个控制块的两个 shared_ptr 才共享计数；对同一个 `T*` 各自
`std::shared_ptr<T>(raw)` 一次，会得到两套计数，最后各删一次。

**deleter**：控制块里存的"计数归零时执行什么"。默认是 `delete ptr`；这里会换成
"DECREF 一个 Python wrapper"。

**PyCapsule**：CPython 提供的"不透明白盒子"对象，见 3.4.1。

### 3.4.1 PyCapsule 是什么

一句话：**它只装三样东西——一个 `void*` 指针、一个名字字符串、一个可选的析构函数。**
它没有类型、没有属性，Python 代码看不见里面的指针；唯一用途是在两个 C 扩展之间
安全传递一个指针，并约定"什么时候释放"。

```c
PyObject* PyCapsule_New(void* pointer, const char* name, PyCapsule_Destructor dtor);
int       PyCapsule_IsValid(PyObject* capsule, const char* name);
void*     PyCapsule_GetPointer(PyObject* capsule, const char* name);
```

三个必须知道的行为：

1. **名字是精确匹配的**。`IsValid` / `GetPointer` 要求传同一个字符串，防止 A 模块
   把 B 模块的盒子当成自己的（名字不对直接失败）。
2. **析构函数在胶囊被 Python 回收时调用**，参数是胶囊对象本身；此时仍可
   `GetPointer` 取出指针做清理。
3. **胶囊就是普通 Python 对象**，遵守引用计数：挂到属性上、塞进容器都会 INCREF；
   谁最后 DECREF 它，它才被回收。

本项目里，胶囊装的是一个**堆上的 `shared_ptr<T>` 拷贝**：

```cpp
auto* holder = new std::shared_ptr<T>(sp);   // 拷贝一份 shared_ptr（计数 +1）
PyObject* capsule = PyCapsule_New(holder, "vt::shared_owner", &sharedCapsuleDestructor<T>);
PyObject_SetAttrString(wrapper, "_vt_shared_owner", capsule);  // 挂到 wrapper 的属性表
Py_DECREF(capsule);                                            // 属性已持有一份引用
```

结构长这样：

```
Python 看到的                        真实结构（C++ 侧）
SharedNode 实例 ── raw ──▶ T
      │
      └─ 属性 "_vt_shared_owner" ──▶ capsule ── void* ──▶ shared_ptr<T> ──▶ 控制块(计数)
```

于是生命周期是一条链：

- wrapper 活着 ⇒ 属性活着 ⇒ capsule 活着 ⇒ 那份 shared_ptr 拷贝活着
  ⇒ C++ 侧计数不会归零 ⇒ T 不会被提前析构；
- wrapper 被回收 ⇒ 属性表释放 ⇒ capsule 析构 ⇒ `delete holder` ⇒ 那份 shared_ptr
  析构（计数 −1）⇒ 归零时由**最后一个持有者**负责析构 T。

为什么不直接把指针放进 Python 属性？因为属性只能是 Python 对象，放不下 C++ 的
`shared_ptr`；capsule 就是 CPython 给 C 扩展准备的标准"指针盒子"。
为什么不用 shiboken 的 `smart-pointer-type`？那会让 Python 侧看到 `SharedNodePtr`
这种包装类型（`isinstance(x, SharedNode)` 失败），而 capsule 方案下 Python 只看到
普通的 `SharedNode`。

> 记忆点：**capsule 本身不拥有对象**，它只是"把一段 C++ 数据挂到 Python 对象上"的
> 容器；这里的"拥有"是里面那份 shared_ptr 拷贝带来的。

补充两个日常会遇到的细节：

- 在 Python 里 `getattr(node, "_vt_shared_owner")` 确实能看到一个
  `<capsule object "vt::shared_owner">`，但拿不到里面的指针，也不能 pickle；
  对用户来说它就是一团不透明的东西。
- 胶囊的名字是**类型安全的开关**：别人（或别的库）若在对象上塞一个同名但 payload
  不是 `shared_ptr<T>` 的东西，`IsValid` 能挡住；名字对上但类型不对，属于调用方违约。

## 3.5 `sharedToPython`：`std::shared_ptr<T>` -> Python

目标：Python 侧只看到普通的 `T`（没有句柄类型），但生命周期由 C++ 的 shared_ptr
与 Python 的 wrapper 共同保证。先看四种场景，再看步骤：

| 场景 | 对象从哪来 | wrapper 拥有 T？ | 处理 |
| --- | --- | --- | --- |
| A | C++ 工厂返回 `shared_ptr` | 否 | 造 wrapper + 挂 capsule |
| B | 同一个值再转一次 | —— | 已有 capsule → 返回同一个 wrapper |
| C | Python 创建、已被收养 | 是 | `isAdopted` → 直接返回 wrapper，不挂 capsule |
| D | Python 创建、从未收养，却有 C++ shared_ptr | 是 | 所有权冲突 → TypeError |

步骤：

```cpp
const std::shared_ptr<T>& sp = *std::any_cast<std::shared_ptr<T>>(&value);
if (!sp) Py_RETURN_NONE;                  // 空 shared_ptr -> None
T* raw = sp.get();
PyObject* wrapper = converter.toPython(&raw);   // 1. 造/取 wrapper
```

1. `converter.toPython(&raw)` 得到 **wrapper**（shiboken 的对象壳）。C++ 侧持有
   所有权，正常情况下这个 wrapper 是 **non-owning**（`hasOwnership == false`），
   不会 delete T；如果拿回来的其实是 Python 创建的 owning wrapper，会在第 4 步被
   当作所有权冲突拦下。
2. **已经有 capsule**：说明这个 wrapper 之前就是由 C++ 的 shared_ptr 路径转出来的，
   直接返回同一个 wrapper（保持 `is` 身份）。
3. **`isAdopted(raw)`**：这个对象原本是 Python 创建的、被收养过。此时**不能再挂
   capsule**——capsule 持有 `shared_ptr`，而收养用的 `shared_ptr` 的 deleter 又持有
   wrapper 的强引用，两者会形成 `wrapper -> capsule -> shared_ptr -> deleter -> wrapper`
   的环，永远释放不掉。
4. **`hasOwnership(wrapper)` 为真**：Python 独占这个对象，但 C++ 这边却有一个
   不是通过"收养"得到的 shared_ptr —— 这是所有权冲突（Python 会在 wrapper 死亡时
   delete T，C++ 的 shared_ptr 也会），没有安全做法，直接 TypeError。
5. 正常路径：`new std::shared_ptr<T>(sp)` 放进 `PyCapsule`（名字
   `vt::shared_owner`，析构函数 `sharedCapsuleDestructor` 负责 delete 这个拷贝），
   用 `PyObject_SetAttrString(wrapper, "_vt_shared_owner", capsule)` 挂到 wrapper 上，
   然后 `Py_DECREF(capsule)`（属性持有了唯一引用）。**wrapper 销毁时 -> 属性释放 ->
   capsule 析构 -> shared_ptr 拷贝释放 -> 引用计数减一**，C++ 侧最后一个 shared_ptr
   归零时对象才真正析构。

（capsule 本身是什么见 3.4.1；这里只用它三件事：取属性 → 校验名字 → 取指针。）

## 3.6 `sharedStore`：Python -> `std::shared_ptr<T>`

### 3.6.1 收养（adoption）是什么

**一句话**：Python 创建的对象本来就归 Python 所有（wrapper 析构时 delete T）。
"收养"= C++ 说"我承认它归你管，但我需要一个能**延长**它寿命的共享句柄"——
于是造一个**不负责 delete 的 `shared_ptr`**：它的 deleter 只是抓紧 wrapper，
最后一个 shared_ptr 消失时松手；T 什么时候删，仍然由 Python 决定。

```
收养前：  Python 变量 ──▶ wrapper(owning) ──▶ T
收养后：  Python 变量 ──▶ wrapper(owning) ──▶ T
                              ▲
          C++ shared_ptr ── deleter 持有 wrapper 的强引用 ──┘
```

结果：对象的寿命 = **Python 的引用** 与 **C++ 的 shared_ptr** 里更长的那个。
哪边先撒手都不会提前析构，两边都撒手才析构。

为什么需要它：`vb.holdPayload({"node": node})` 这类调用里，Python 侧的对象要存进
Variant，而共享类型在 Variant 里必须表现为 `shared_ptr<T>`。可这个对象是 Python
`new` 出来的、wrapper 拥有它。三种处理方式只有一种安全：

| 做法 | 结果 |
| --- | --- |
| 拒绝 Python 对象入库 | 太苛刻：Python 用户没法把自己的对象传进来 |
| 把所有权转给 C++（`shared_ptr<T>(raw)` 直接接管） | Python 那边还拿着 wrapper，C++ 一析构就是悬空；wrapper 晚点析构又会 delete → 双删 |
| **收养**（本方案） | 所有权不动，C++ 只拿"可续命的引用"，双方都不会多删一次 |

### 3.6.2 收养具体做了什么

```cpp
std::shared_ptr<T> sp(raw, PythonKeepAliveDeleter<T>(py));  // deleter 不 delete T
registerAdopted<T>(raw, sp);                                // raw -> weak_ptr 记表
out = std::move(sp);                                        // 放进 std::any
```

- **deleter**：构造时 `Py_XINCREF(wrapper)`，最后一个 shared_ptr 消失时
  `Py_DECREF(wrapper)`。所以只要 C++ 还持有引用，wrapper 的引用计数就不为 0，
  Python 不会回收它，T 也就不会被删。
- **登记表**：每个 T 一张 `T* -> weak_ptr<T>`（配一把互斥锁）。用 weak_ptr 是
  因为表本身不能续命；`findAdopted` 查到过期条目会顺手删掉。

生命周期走一遍（就是 `shared_ownership_test.py` 场景 1）：

1. `node = vb.SharedNode(101)` → wrapper owning，alive = 1；
2. `vb.holdPayload({"node": node})` → `fromPython` 走到 `sharedStore` → 收养：
   shared_ptr 的 deleter 持有 wrapper（wrapper 引用 +1）；
3. `del node; gc.collect()` → Python 变量没了，但 wrapper 被 deleter 抓着 → alive 仍 1；
4. `vb.heldPayload()` → 同一个 `T*` → shiboken 返回**同一个 wrapper** → `is` 成立；
5. `vb.releasePayload()` → Variant 里的 shared_ptr 析构 → deleter DECREF wrapper
   → 引用归零 → Python 删除 T → alive 回到 baseline。

### 3.6.3 收养的调用点（整个仓库）

| 位置 | 调用 | 作用 |
| --- | --- | --- |
| `registerSharedType<T>` | 把 `sharedStore` 装进 `Hook.store` | 注册时决定"这个类型的 Python→C++ 走收养逻辑" |
| `vpy::fromPython` | 命中 Hook 后 `hook->store(py, any, name)` | 所有入口的总闸：Variant 参数、dict/list 元素、VariantMap 值 |
| `sharedStore` 步骤 4 | `findAdopted<T>(raw)` | 同一个对象再次入库：复用同一控制块 |
| `sharedStore` 步骤 6 | `registerAdopted<T>(raw, sp)` | 首次收养：登记 `raw -> weak_ptr` |
| `sharedToPython` | `isAdopted<T>(raw)` | C++→Python 时判断"这是 Python 拥有的对象"，**不挂 capsule** |
| 测试 | `py_scripts/shared_ownership_test.py` 场景 1/2/3；`tests/plugin_override_test.cpp` | 收养 / 多次收养 / Python 子类被收养 |

### 3.6.4 为什么一定要这张表

按重要程度排：

1. **`sharedToPython` 需要知道"它被收养过"**：被收养的对象是 owning wrapper，
   当前代码里如果没有这条判断，就会掉进"所有权冲突"分支误报错；而如果为了绕过
   误报去挂 capsule，则会形成 `wrapper → capsule → shared_ptr → deleter → wrapper`
   的环，永远释放不掉。判断依据就是这张表里的 `isAdopted`。
2. **同一对象只有一个所有者家族**：多次转换必须落在同一个控制块上，否则
   `strong_count` / `weak_ptr` / `enable_shared_from_this` 都会变成"两套账"。
3. **身份保持**：多次取回拿到同一个 wrapper（这条也有 shiboken 的 `T* -> wrapper`
   映射帮忙，两者叠加）。

代价（如实记录）：

- 借用对象不能收养（步骤 5 直接报错），这是有意的 fail-fast，不是缺陷；
- 表项只在"同一地址再次被查询"时清理；地址长期不复用时会积累已过期的空条目
  （weak_ptr 很小，但 map 节点占内存），需要的话可以定期 sweep；
- 每 T 一把互斥锁，共享类型每次转换都会加锁。

### 3.6.5 决策树与逐段拆解

这一段在回答**一个**问题：这个 Python 对象转成 `shared_ptr` 时，控制块从哪来？

```
这个 Python 对象是…
├─ 带 capsule            → 复用 capsule 里那份 shared_ptr（拷贝，计数 +1）   [步骤 1]
├─ 收养表里有这个 T*     → 复用收养时建的控制块                              [步骤 4]
├─ Python 独占所有权     → 新建控制块，deleter = 只 DECREF wrapper           [步骤 6]
└─ 其余（C++ 借用）      → 报错：既不能收养，也不能假共享                    [步骤 5]
```

逐段对应：

```cpp
// 1) 已挂 capsule：直接复制共享句柄
PyObject* capsule = PyObject_GetAttrString(py, kSharedOwnerAttr);   // 新引用
if (capsule) { ... out = *holder; Py_DECREF(capsule); return true; }
PyErr_Clear();
```

- `PyObject_GetAttrString(py, "_vt_shared_owner")`：取属性，**返回新引用**；
  没有这个属性时返回 nullptr 并设置 `AttributeError`。所以下一行 `PyErr_Clear()`
  是必须的——否则后面的 `PyErr_Occurred()` 检查会被这个"假错误"污染。
- `PyCapsule_IsValid(capsule, "vt::shared_owner")`：确认名字对得上（防止用户/别的库
  塞了一个同名属性）。
- `PyCapsule_GetPointer(...)`：取出 payload，类型就是 `std::shared_ptr<T>*`。
- `out = *holder`：**拷贝**一个 `shared_ptr`（控制块共享，引用计数 +1），
  不是接管指针。于是 Python 和 C++ 现在各持一个强引用。
- 出错（属性存在但不是合法 capsule）时**明确报错**，不静默当没有。

```cpp
// 2)-3) 基本合法性 + 取裸指针
Shiboken::Object::isValid(py);
SpecificConverter converter(name);
converter.toCpp(py, &raw);
```

- `Shiboken::Object::isValid(py)`：这个 Python 对象还是一个**有效的 shiboken wrapper**
  （没有处于"对象已被 delete / 所有权已转移"的状态），避免拿到底层已失效的 C++ 指针。
- `SpecificConverter(name)` + `toCpp`：按注册名取转换器并把 Python 对象解成 `T*`。
  这里只拿到**指针**，不复制对象。
- `if (!raw || PyErr_Occurred())`：转换器可能返回空指针或设置异常，两种情况都失败。

```cpp
// 4) 已收养过：复用同一个控制块
if (auto adopted = findAdopted<T>(raw)) { out = std::move(adopted); return true; }
```

- `findAdopted<T>` 查的是"每个 T 一张"的 `T* -> std::weak_ptr<T>` 表。
  命中且未过期 -> `lock()` 出同一个控制块的 `shared_ptr`。
- **必须复用控制块**：如果对同一个 `T*` 建第二个 `shared_ptr`，就会有两个彼此
  独立的引用计数，各自析构时都会去 delete 同一个对象 -> 双删。
- 表里存 `weak_ptr` 而不是 `shared_ptr`：表本身不能延长对象寿命；过期的条目
  在查询时顺手 `erase`。

```cpp
// 5) 没有 capsule、也没收养过：只有 Python 独占所有权才允许收养
if (!Shiboken::Object::hasOwnership(sbk)) { ... TypeError ... }
```

- `Shiboken::Object::hasOwnership(sbk)`：wrapper 是否**拥有**这个 C++ 对象
  （Python 里 `SharedNode()` 创建的对象为 true；从 C++ 返回、借用给 Python 的
  对象为 false）。
- 借用对象不能收养：C++ 的主人（例如静态容器、QObject parent）随时可能 delete 它，
  我们若建 shared_ptr 就会在 deleter 里再删一次；而"假共享"（只保存裸指针）会悬空。
  两种都是错误，所以这里 **fail-fast**，提示用共享工厂创建。

```cpp
// 6) 收养
std::shared_ptr<T> sp(raw, detail::PythonKeepAliveDeleter<T>(py));
registerAdopted<T>(raw, sp);
out = std::move(sp);
```

- 自定义 deleter `PythonKeepAliveDeleter`：**不 delete T**，只持有一份 wrapper 的强
  引用（构造时 `Py_XINCREF`）。shared_ptr 归零时它 `Py_DECREF(wrapper)`，wrapper
  归零后由 Python 自己 delete T —— 所有权仍然在 Python 手里，C++ 只是"共享使用"。
- `registerAdopted` 写入 `raw -> weak_ptr` 表，保证下一次遇到同一个对象拿到同一个
  控制块。
- deleter 两个细节：
  - `owner` 是 `mutable`：deleter 以 const 方式被调用，但仍要清空自己；
  - `release()` 先看 `Py_IsInitialized()`，再用 `PyGILState_Ensure()` 包住 DECREF：
    C++ 静态变量里的 shared_ptr 可能活到解释器 finalize 之后，那时碰 Python 会崩。

## 3.7 三条所有权路径对照

| Python 侧对象来源 | Variant 里存什么 | 谁 delete T | wrapper 是否 owning |
| --- | --- | --- | --- |
| C++ 工厂返回的 `shared_ptr` | `shared_ptr<T>` + capsule | 最后一个 shared_ptr | 否（capsule 保活） |
| Python `new` 出来再入库 | 收养的 `shared_ptr<T>`（keep-alive deleter） | Python wrapper | 是 |
| C++ 借给 Python（如静态容器） | 不允许入库 | C++ 主人 | 否 |

两条不变式：

- **同一个 `T*` 只能有一个控制块**（收养表 + capsule 保证）；
- **wrapper 与 shared_ptr 不能互相持有**（capsule 和收养二选一，否则成环）。

## 3.8 与 shiboken 内置容器转换器的分工

- **函数签名里**的容器（`std::vector<vt::Variant>`、
  `std::unordered_map<std::string, vt::Variant>`）：由 shiboken 生成的容器转换器
  负责"容器层"，元素再回调 `vt::Variant` 规则（也就是本节的两个入口）。这条路径
  **看不到 PySink**。
- **扩展槽里**的容器（`Variant::fromValue(vector<vector<int>>)` 之类）：签名里没有
  它，只能靠 normalizer + PySink（3.3）。
- 两条路径的键类型行为不同是必然的：签名路径的容器是
  `unordered_map<string, Variant>`，键本来就只能是字符串；只有扩展槽里的
  `map<int,...>` 才知道键的真实类型。



