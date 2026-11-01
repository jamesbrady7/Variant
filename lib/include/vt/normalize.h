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
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace vt {

// 前置声明：denormalize 的递归元素转换走同一个入口（定义见文件末尾）。
template <class T>
std::optional<T> getAs(const Variant& value);

namespace detail {

/// 归一化函数：扩展槽里的容器 -> 只含内建类型的 Variant。
using NormalizerFn = Variant (*)(const std::any&);

/**
 * @brief 登记一个扩展类型的归一化函数（Python 桥在扩展槽转换失败时使用）。
 * @param [in] std::type_index 类型索引
 * @param [in] NormalizerFn 归一化函数
 */
VT_API void registerNormalizer(std::type_index type, NormalizerFn fn);

/**
 * @brief 按类型查找归一化函数。
 * @param [in] std::type_index 类型索引
 * @return NormalizerFn 未登记返回 nullptr
 */
VT_API NormalizerFn findNormalizer(std::type_index type);

/// @brief 清空归一化表（主要用于测试）。
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

// ------------------------------------------------------------ 归一化

/**
 * @brief 递归归一化：把任意支持的容器变成只含内建类型的 Variant。
 *
 * - 序列容器 -> List（元素递归）；
 * - 映射容器 -> 键可字符串化时是 Map（键写成十进制/文本），否则是 `[[k,v], ...]`；
 * - `std::pair` -> 两元素 List；
 * - 其他类型（叶子）-> `Variant::fromValue`，标量进热槽、其余进扩展槽。
 *
 * 叶子不做任何猜测：`container_traits` 没有特化的类型一律原样存放。
 * @param [in] T 容器或叶子值
 * @return Variant 归一化结果
 */
template <class T>
Variant normalizeToVariant(const T& value);

template <class T>
Variant normalizeMapToVariant(const T& value)
{
    using Traits = container_traits<std::decay_t<T>>;
    using Key = typename Traits::key_type;

    if constexpr (map_key_traits<Key>::convertible) {
        VariantMap map;
        map.reserve(value.size());
        for (const auto& kv : value)
            map[map_key_traits<Key>::toKey(kv.first)] = normalizeToVariant(kv.second);
        return Variant(std::move(map));
    } else {
        VariantList pairs;
        pairs.reserve(value.size());
        for (const auto& kv : value) {
            VariantList pair;
            pair.push_back(normalizeToVariant(kv.first));
            pair.push_back(normalizeToVariant(kv.second));
            pairs.push_back(std::move(pair));
        }
        return Variant(std::move(pairs));
    }
}

template <class T>
Variant normalizeToVariant(const T& value)
{
    using U = std::decay_t<T>;
    using Traits = container_traits<U>;

    if constexpr (std::is_same_v<U, Variant>) {
        return value;
    } else if constexpr (Traits::shape == ContainerShape::Sequence) {
        VariantList list;
        if constexpr (has_reserve_v<VariantList>)
            list.reserve(value.size());
        for (const auto& item : value)
            list.push_back(normalizeToVariant(item));
        return Variant(std::move(list));
    } else if constexpr (Traits::shape == ContainerShape::Map) {
        return normalizeMapToVariant(value);
    } else if constexpr (Traits::shape == ContainerShape::Pair) {
        VariantList list;
        list.push_back(normalizeToVariant(value.first));
        list.push_back(normalizeToVariant(value.second));
        return Variant(std::move(list));
    } else {
        return Variant::fromValue(value);
    }
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
 * @brief 为一个容器类型登记归一化函数（+ 自动 codec，若用户尚未注册）。
 *
 * 由 `Variant::fromValue` 在第一次见到该类型时自动调用；只读进程（例如
 * 只读 JSON/二进制文件、从不构造该类型）需要显式调用
 * `vt::registerContainerAdapters<T>()` 作为编译期锚点。
 */
template <class T>
void installContainerAdapters()
{
    registerNormalizer(std::type_index(typeid(T)), [](const std::any& any) -> Variant {
        const T* value = std::any_cast<T>(&any);
        if (!value) {
            throw VariantError(VariantErrorCode::TypeMismatch,
                               "normalizer: std::any does not hold the expected container type");
        }
        return normalizeToVariant(*value);
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
 * @brief 保证容器类型 T 的归一化函数与 codec 已登记（每个类型只做一次）。
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
 * @brief 显式登记容器 T 的归一化函数与自动 codec。
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
