#pragma once

#include "export.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <deque>
#include <limits>
#include <list>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vt {
namespace detail {

/**
 * @brief 容器的“形状”。
 *
 * 归一化 / 反归一化 / `getAs` / Python 转换共用这一张表：
 * - `Sequence`：按顺序遍历，表示成 List；
 * - `Map`：键值遍历，键可字符串化时表示成 Map，否则表示成 `[[k,v], ...]`；
 * - `Pair`：表示成两元素 List；
 * - `Leaf`：不是容器，原样进扩展槽。
 */
enum class ContainerShape {
    Leaf,      ///< 非容器（默认）
    Sequence,  ///< 序列容器
    Map,       ///< 映射容器
    Pair       ///< std::pair
};

/**
 * @brief 容器形状特化点。
 *
 * 默认是 `Leaf`：**不靠 begin/end 猜测**，避免把可迭代的业务类型误拆成容器。
 * 支持新的容器形状时在这里加一个特化即可，归一化/序列化/Python 三条路径自动获得。
 *
 * 特化需要提供：
 * - `shape`；
 * - `value_type`（Sequence）；
 * - `key_type` / `mapped_type`（Map）；
 * - `first_type` / `second_type`（Pair）；
 * - `type_name`（稳定名字前缀，用于二进制序列化）；
 * - `fixed_size`（固定长度容器为长度，否则 0）。
 */
template <class T>
struct container_traits {
    static constexpr ContainerShape shape = ContainerShape::Leaf;
};

template <class T, class Alloc>
struct container_traits<std::vector<T, Alloc>> {
    using value_type = T;
    static constexpr ContainerShape shape = ContainerShape::Sequence;
    static constexpr const char* type_name = "std::vector";
    static constexpr std::size_t fixed_size = 0;
};

template <class T, class Alloc>
struct container_traits<std::list<T, Alloc>> {
    using value_type = T;
    static constexpr ContainerShape shape = ContainerShape::Sequence;
    static constexpr const char* type_name = "std::list";
    static constexpr std::size_t fixed_size = 0;
};

template <class T, class Alloc>
struct container_traits<std::deque<T, Alloc>> {
    using value_type = T;
    static constexpr ContainerShape shape = ContainerShape::Sequence;
    static constexpr const char* type_name = "std::deque";
    static constexpr std::size_t fixed_size = 0;
};

template <class T, class Compare, class Alloc>
struct container_traits<std::set<T, Compare, Alloc>> {
    using value_type = T;
    static constexpr ContainerShape shape = ContainerShape::Sequence;
    static constexpr const char* type_name = "std::set";
    static constexpr std::size_t fixed_size = 0;
};

template <class T, class Hash, class Equal, class Alloc>
struct container_traits<std::unordered_set<T, Hash, Equal, Alloc>> {
    using value_type = T;
    static constexpr ContainerShape shape = ContainerShape::Sequence;
    static constexpr const char* type_name = "std::unordered_set";
    static constexpr std::size_t fixed_size = 0;
};

template <class T, std::size_t N>
struct container_traits<std::array<T, N>> {
    using value_type = T;
    static constexpr ContainerShape shape = ContainerShape::Sequence;
    static constexpr const char* type_name = "std::array";
    static constexpr std::size_t fixed_size = N;
};

template <class Key, class Value, class Compare, class Alloc>
struct container_traits<std::map<Key, Value, Compare, Alloc>> {
    using key_type = Key;
    using mapped_type = Value;
    static constexpr ContainerShape shape = ContainerShape::Map;
    static constexpr const char* type_name = "std::map";
    static constexpr std::size_t fixed_size = 0;
};

template <class Key, class Value, class Hash, class Equal, class Alloc>
struct container_traits<std::unordered_map<Key, Value, Hash, Equal, Alloc>> {
    using key_type = Key;
    using mapped_type = Value;
    static constexpr ContainerShape shape = ContainerShape::Map;
    static constexpr const char* type_name = "std::unordered_map";
    static constexpr std::size_t fixed_size = 0;
};

template <class First, class Second>
struct container_traits<std::pair<First, Second>> {
    using first_type = First;
    using second_type = Second;
    static constexpr ContainerShape shape = ContainerShape::Pair;
    static constexpr const char* type_name = "std::pair";
    static constexpr std::size_t fixed_size = 2;
};

/// @brief 去引用后的容器形状。
template <class T>
inline constexpr ContainerShape container_shape_v = container_traits<std::decay_t<T>>::shape;

/// @brief 是否为支持的容器形状。
template <class T>
inline constexpr bool is_container_v = container_shape_v<T> != ContainerShape::Leaf;

/**
 * @brief 映射键的字符串化 / 还原策略。
 *
 * `convertible == false` 的键类型不改写成 Map，而是退化成 `[[k,v], ...]`，
 * 保证类型不丢失（例如自定义结构体做键）。
 */
template <class Key, class Enable = void>
struct map_key_traits {
    static constexpr bool convertible = false;
};

template <>
struct map_key_traits<std::string> {
    static constexpr bool convertible = true;

    static std::string toKey(const std::string& value) { return value; }

    static bool fromKey(std::string_view text, std::string& out)
    {
        out.assign(text.data(), text.size());
        return true;
    }
};

template <class Key>
struct map_key_traits<Key, std::enable_if_t<std::is_integral_v<Key> && !std::is_same_v<Key, bool>>> {
    static constexpr bool convertible = true;

    static std::string toKey(Key value) { return std::to_string(value); }

    static bool fromKey(std::string_view text, Key& out)
    {
        if (text.empty())
            return false;
        Key value{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc() || result.ptr != text.data() + text.size())
            return false;
        out = value;
        return true;
    }
};

template <class Key>
struct map_key_traits<Key, std::enable_if_t<std::is_enum_v<Key>>> {
    static constexpr bool convertible = true;

    using Underlying = std::underlying_type_t<Key>;

    static std::string toKey(Key value)
    {
        return std::to_string(static_cast<Underlying>(value));
    }

    static bool fromKey(std::string_view text, Key& out)
    {
        Underlying raw{};
        if (!map_key_traits<Underlying>::fromKey(text, raw))
            return false;
        out = static_cast<Key>(raw);
        return true;
    }
};

template <class Key>
struct map_key_traits<Key, std::enable_if_t<std::is_floating_point_v<Key>>> {
    static constexpr bool convertible = true;

    static std::string toKey(Key value)
    {
        char buffer[64] = {};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                          std::chars_format::general,
                                          std::numeric_limits<Key>::max_digits10);
        if (result.ec != std::errc())
            return {};
        return std::string(buffer, result.ptr);
    }

    static bool fromKey(std::string_view text, Key& out)
    {
        if (text.empty())
            return false;
        Key value{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value,
                                            std::chars_format::general);
        if (result.ec != std::errc() || result.ptr != text.data() + text.size())
            return false;
        out = value;
        return true;
    }
};

/// @brief 容器是否有 `reserve(size_type)`（vector/deque/map/list 有，set/array 无）。
template <class T, class = void>
struct has_reserve : std::false_type {};

template <class T>
struct has_reserve<T, std::void_t<decltype(std::declval<T&>().reserve(std::size_t{}))>>
    : std::true_type {};

template <class T>
inline constexpr bool has_reserve_v = has_reserve<T>::value;

} // namespace detail
} // namespace vt
