#pragma once

#include "codec.h"
#include "container_traits.h"
#include "export.h"
#include "variant.h"
#include "variant_error.h"
#include "variant_list.h"
#include "variant_map.h"

#include <any>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace vt {

// 前置声明：denormalize 的递归元素转换走同一个入口（定义见文件末尾）。
template <class T>
std::optional<T> getAs(const Variant& value);

namespace detail {

/**
 * @brief 映射键的表示方式。
 *
 * - `Text`：键可无损写成文本（`map_key_traits<Key>::convertible` 为真），
 *   visitor 以字符串事件发出键；
 * - `Typed`：键不能字符串化，visitor 按原类型递归发出键。
 */
enum class MapKeyForm { Text, Typed };

/**
 * @brief 值遍历的接收端（Sink）。
 *
 * `visitValue` 只做一份编译期递归，把值拆成与目标表示无关的事件流：
 * - 序列 / pair：`onListBegin` -> 元素 -> `onListEnd`；
 * - 映射：`onMapBegin` -> 每个条目 `onEntryBegin` -> 键 -> 值 -> `onEntryEnd` -> `onMapEnd`；
 * - 叶子：`onNull/onBool/onInt/onDouble/onString/onVariant/onExtension`。
 *
 * 两个实现：
 * - `VariantSink`：重建 Variant 正规形（JSON / 二进制 / codec 用）；
 * - `PySink`（binding）：直接产出 `PyObject*`，映射键保留真实类型。
 */
struct ValueSink {
    virtual ~ValueSink() = default;

    /// @brief 空值。
    virtual void onNull() = 0;
    /// @brief bool 叶子。
    /// @param [in] bool 布尔值
    virtual void onBool(bool value) = 0;
    /// @brief 整数叶子。
    /// @param [in] std::int64_t 整数值（越界规则与 `Variant` 一致）
    virtual void onInt(std::int64_t value) = 0;
    /// @brief 浮点叶子。
    /// @param [in] double 浮点值
    virtual void onDouble(double value) = 0;
    /// @brief UTF-8 字符串叶子。
    /// @param [in] std::string_view 字符串视图（不保证以 '\0' 结尾）
    virtual void onString(std::string_view value) = 0;
    /// @brief 已经成型的 `Variant`（不拆解，由 sink 决定原样保留还是继续转换）。
    /// @param [in] Variant 值
    virtual void onVariant(const Variant& value) = 0;
    /// @brief 扩展槽叶子（自定义类型）。
    /// @param [in] std::any 扩展值
    virtual void onExtension(const std::any& value) = 0;

    /// @brief 序列开始。
    /// @param [in] size 元素个数
    virtual void onListBegin(std::size_t size) = 0;
    /// @brief 序列结束。
    virtual void onListEnd() = 0;

    /**
     * @brief 映射开始。
     * @param [in] size 条目数
     * @param [in] keyConvertible 键类型是否可无损字符串化
     * @return MapKeyForm 本次遍历采用的键形态（`Text` 只在 keyConvertible 时有效）
     */
    virtual MapKeyForm onMapBegin(std::size_t size, bool keyConvertible) = 0;
    /// @brief 映射结束。
    virtual void onMapEnd() = 0;
    /// @brief 一个键值条目开始。
    virtual void onEntryBegin() = 0;
    /// @brief 一个键值条目结束。
    virtual void onEntryEnd() = 0;
};

/**
 * @brief 遍历函数：把扩展槽里的容器按 `ValueSink` 事件流写出去。
 *
 * 由 `installContainerAdapters<T>` 在 `Variant::fromValue<T>` 首次见到容器
 * 类型时登记；`std::any` 里必须是登记时的那个 `T`，否则抛 `TypeMismatch`。
 */
using NormalizerFn = void (*)(const std::any&, ValueSink&);

/**
 * @brief 登记一个容器类型的遍历函数（Python 桥与 codec 在扩展槽上使用）。
 * @param [in] std::type_index 类型索引
 * @param [in] NormalizerFn 遍历函数
 */
VT_API void registerNormalizer(std::type_index type, NormalizerFn fn);

/**
 * @brief 按类型查找遍历函数。
 * @param [in] std::type_index 类型索引
 * @return NormalizerFn 未登记返回 nullptr
 */
VT_API NormalizerFn findNormalizer(std::type_index type);

/// @brief 清空遍历函数表（主要用于测试）。
VT_API void clearNormalizers();

template <class T>
bool denormalizeFromVariant(const Variant& value, T& out);

// ------------------------------------------------------------ 稳定类型名

template <class T>
constexpr const char* fundamentalTypeName() noexcept
{
    if constexpr (std::is_same_v<T, bool>)
        return "bool";
    else if constexpr (std::is_same_v<T, char>)
        return "char";
    else if constexpr (std::is_same_v<T, signed char>)
        return "signed char";
    else if constexpr (std::is_same_v<T, unsigned char>)
        return "unsigned char";
    else if constexpr (std::is_same_v<T, wchar_t>)
        return "wchar_t";
    else if constexpr (std::is_same_v<T, char16_t>)
        return "char16_t";
    else if constexpr (std::is_same_v<T, char32_t>)
        return "char32_t";
    else if constexpr (std::is_same_v<T, short>)
        return "short";
    else if constexpr (std::is_same_v<T, unsigned short>)
        return "unsigned short";
    else if constexpr (std::is_same_v<T, int>)
        return "int";
    else if constexpr (std::is_same_v<T, unsigned int>)
        return "unsigned int";
    else if constexpr (std::is_same_v<T, long>)
        return "long";
    else if constexpr (std::is_same_v<T, unsigned long>)
        return "unsigned long";
    else if constexpr (std::is_same_v<T, long long>)
        return "long long";
    else if constexpr (std::is_same_v<T, unsigned long long>)
        return "unsigned long long";
    else if constexpr (std::is_same_v<T, float>)
        return "float";
    else if constexpr (std::is_same_v<T, double>)
        return "double";
    else
        return "long double";
}

/**
 * @brief 容器类型的稳定名字，用于二进制序列化的 `$type`。
 *
 * 内建/算术类型用固定写法，未支持的类型回退到 `typeid(T).name()`
 * （同一构建内稳定；跨编译器不保证）。
 */
template <class T>
std::string stableTypeName();

template <class T>
std::string stableTypeName()
{
    using U = std::decay_t<T>;
    using Traits = container_traits<U>;

    if constexpr (std::is_same_v<U, std::string>) {
        return "std::string";
    } else if constexpr (std::is_arithmetic_v<U>) {
        return fundamentalTypeName<U>();
    } else if constexpr (Traits::shape == ContainerShape::Sequence) {
        std::string name = Traits::type_name;
        name += '<';
        name += stableTypeName<typename Traits::value_type>();
        if constexpr (Traits::fixed_size > 0) {
            name += ", ";
            name += std::to_string(Traits::fixed_size);
        }
        name += '>';
        return name;
    } else if constexpr (Traits::shape == ContainerShape::Map) {
        std::string name = Traits::type_name;
        name += '<';
        name += stableTypeName<typename Traits::key_type>();
        name += ", ";
        name += stableTypeName<typename Traits::mapped_type>();
        name += '>';
        return name;
    } else if constexpr (Traits::shape == ContainerShape::Pair) {
        std::string name = Traits::type_name;
        name += '<';
        name += stableTypeName<typename Traits::first_type>();
        name += ", ";
        name += stableTypeName<typename Traits::second_type>();
        name += '>';
        return name;
    } else {
        return typeid(U).name();
    }
}

// ------------------------------------------------------------ 值遍历

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
 * @param [in] T 容器或叶子值
 * @param [in] ValueSink 接收端
 */
template <class T>
void visitValue(const T& value, ValueSink& sink)
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
        // 与 `Variant` 的整型构造共用同一条规则：无符号越界抛 OutOfRange。
        const Variant scalar{value};
        sink.onInt(scalar.asInt());
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

// ------------------------------------------------------------ 归一化

/**
 * @brief 递归归一化：把任意支持的容器变成只含内建类型的 Variant。
 *
 * 是 `visitValue` + `VariantSink` 的一行包装：
 * - 序列容器 -> List（元素递归）；
 * - 映射容器 -> 键可字符串化时是 Map（键写成十进制/文本），否则是 `[[k,v], ...]`；
 * - `std::pair` -> 两元素 List；
 * - 其他类型（叶子）-> `Variant::fromValue`，标量进热槽、其余进扩展槽。
 * @param [in] T 容器或叶子值
 * @return Variant 归一化结果
 */
template <class T>
Variant normalizeToVariant(const T& value)
{
    VariantSink sink;
    visitValue(value, sink);
    return sink.take();
}

// ------------------------------------------------------------ 反归一化

/**
 * @brief 递归反归一化：把普通 List/Map 还原成目标容器。
 * @param [in] Variant 归一化表示
 * @param [out] T 目标值（仅成功时写入）
 * @return bool 是否成功
 */
template <class T>
bool denormalizeFromVariant(const Variant& value, T& out)
{
    using U = std::decay_t<T>;
    using Traits = container_traits<U>;

    if constexpr (std::is_same_v<U, Variant>) {
        out = value;
        return true;
    } else if constexpr (std::is_same_v<U, VariantMap>) {
        if (!value.isMap())
            return false;
        out = value.asMap();
        return true;
    } else if constexpr (std::is_same_v<U, VariantList>) {
        if (!value.isList())
            return false;
        out = value.asList();
        return true;
    } else if constexpr (Traits::shape == ContainerShape::Sequence) {
        if (!value.isList())
            return false;
        const VariantList& items = value.asList();
        if constexpr (Traits::fixed_size > 0) {
            if (items.size() != Traits::fixed_size)
                return false;
        }
        U result{};
        if constexpr (Traits::fixed_size == 0 && has_reserve_v<U>)
            result.reserve(items.size());
        std::size_t index = 0;
        for (const Variant& item : items) {
            auto element = getAs<typename Traits::value_type>(item);
            if (!element)
                return false;
            if constexpr (Traits::fixed_size > 0)
                result[index++] = std::move(*element);
            else
                result.insert(result.end(), std::move(*element));
        }
        out = std::move(result);
        return true;
    } else if constexpr (Traits::shape == ContainerShape::Map) {
        using Key = typename Traits::key_type;
        using Mapped = typename Traits::mapped_type;

        U result;
        if (value.isMap()) {
            if constexpr (!map_key_traits<Key>::convertible) {
                return false;   // 键无法从字符串还原：只能用 [[k,v], ...] 形式
            } else {
                if constexpr (has_reserve_v<U>)
                    result.reserve(value.asMap().size());
                for (const auto& kv : value.asMap()) {
                    Key key{};
                    if (!map_key_traits<Key>::fromKey(kv.first, key))
                        return false;
                    auto mapped = getAs<Mapped>(kv.second);
                    if (!mapped)
                        return false;
                    result.insert_or_assign(std::move(key), std::move(*mapped));
                }
                out = std::move(result);
                return true;
            }
        }
        if (value.isList()) {
            if constexpr (has_reserve_v<U>)
                result.reserve(value.asList().size());
            for (const Variant& item : value.asList()) {
                if (!item.isList() || item.asList().size() != 2)
                    return false;
                auto key = getAs<Key>(item.asList()[0]);
                auto mapped = getAs<Mapped>(item.asList()[1]);
                if (!key || !mapped)
                    return false;
                result.insert_or_assign(std::move(*key), std::move(*mapped));
            }
            out = std::move(result);
            return true;
        }
        return false;
    } else if constexpr (Traits::shape == ContainerShape::Pair) {
        if (!value.isList() || value.asList().size() != 2)
            return false;
        auto first = getAs<typename Traits::first_type>(value.asList()[0]);
        auto second = getAs<typename Traits::second_type>(value.asList()[1]);
        if (!first || !second)
            return false;
        out = U(std::move(*first), std::move(*second));
        return true;
    } else {
        auto result = value.get<U>();
        if (!result)
            return false;
        out = std::move(*result);
        return true;
    }
}

// ------------------------------------------------------------ 注册

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
    registerNormalizer(std::type_index(typeid(T)), [](const std::any& any, ValueSink& sink) {
        const T* value = std::any_cast<T>(&any);
        if (!value) {
            throw VariantError(VariantErrorCode::TypeMismatch,
                               "normalizer: std::any does not hold the expected container type");
        }
        visitValue(*value, sink);
    });

    // 用户先注册的 codec 优先；容器 codec 一律是 plain（JSON 写成普通数组/对象）。
    if (CodecRegistry::instance().find(std::type_index(typeid(T))) == nullptr) {
        registerValueCodec<T>(
            stableTypeName<T>(),
            [](const T& value) { return normalizeToVariant(value); },
            [](const Variant& plain, T& out) { return denormalizeFromVariant(plain, out); },
            true);
    }
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

/**
 * @brief `Variant::fromValue` 的容器分支：登记适配后放进扩展槽（紧凑存储）。
 * @param [in] T 容器值
 * @return Variant 扩展值
 */
template <class T>
Variant extendValue(T&& value)
{
    using U = std::decay_t<T>;
    if constexpr (is_container_v<U>)
        ensureContainerAdapters<U>();
    return Variant::fromValue(std::any(std::forward<T>(value)));
}

} // namespace detail

/**
 * @brief 按目标类型取回值（递归支持嵌套容器）。
 *
 * - 扩展槽里正好是 T（紧凑存储）-> any_cast；
 * - 普通 List / Map 表示 -> 按 `container_traits` 逐层校验后还原；
 * - 标量 / 字符串 -> 对应热槽；
 * - 其他类型 -> 扩展槽精确匹配。
 *
 * @param [in] Variant 值
 * @return std::optional<T> 失败返回 nullopt
 */
template <class T>
std::optional<T> getAs(const Variant& value)
{
    using U = std::decay_t<T>;

    if constexpr (std::is_same_v<U, Variant>) {
        return value;
    } else if constexpr (std::is_same_v<U, VariantMap>) {
        if (!value.isMap())
            return std::nullopt;
        return value.asMap();
    } else if constexpr (std::is_same_v<U, VariantList>) {
        if (!value.isList())
            return std::nullopt;
        return value.asList();
    } else if constexpr (detail::is_container_v<U>) {
        // 紧凑存储快路径：扩展槽里就是 T。
        if (const U* exact = value.extensionPtr<U>())
            return *exact;
        U out{};
        if (!detail::denormalizeFromVariant(value, out))
            return std::nullopt;
        return out;
    } else {
        return value.get<U>();
    }
}

/**
 * @brief 按目标类型取回值（写入式，不抛）。
 * @param [in] Variant 值
 * @param [out] T 输出值（仅成功时写入）
 * @return bool 是否成功
 */
template <class T>
bool getAs(const Variant& value, T& out)
{
    auto result = getAs<T>(value);
    if (!result)
        return false;
    out = std::move(*result);
    return true;
}

/**
 * @brief 显式登记容器 T 的遍历函数与自动 codec。
 *
 * 只读进程的编译期锚点：从 JSON/二进制读回 `vector<vector<int>>` 等容器时，
 * 若进程内从未 `fromValue` 过该类型，需要先调用它。
 * @param [in] T 容器类型
 */
template <class T>
void registerContainerAdapters()
{
    detail::ensureContainerAdapters<T>();
}

} // namespace vt
