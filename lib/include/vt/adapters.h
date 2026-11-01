#pragma once

#include "export.h"
#include "variant.h"
#include "variant_list.h"
#include "variant_map.h"

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vt {

/**
 * @brief 注册内建容器的序列化 codec。
 *
 * JSON/二进制入口会自动调用；通常不需要手动调用。用户如果先注册了同类型
 * codec，则保留用户的实现。
 */
VT_API void registerBuiltinCodecs();

namespace detail {

/**
 * @brief 把十进制字符串解析为 int（内部使用）。
 * @param [in] std::string_view 键文本
 * @return std::optional<int> 非法或越界返回 nullopt
 */
inline std::optional<int> parseIntKey(std::string_view key)
{
    if (key.empty())
        return std::nullopt;
    int value = 0;
    const auto result = std::from_chars(key.data(), key.data() + key.size(), value);
    if (result.ec != std::errc() || result.ptr != key.data() + key.size())
        return std::nullopt;
    return value;
}

} // namespace detail

/**
 * @brief 按目标 C++ 类型取回值。
 *
 * - 扩展槽里正好是 T -> any_cast；
 * - 普通 VariantList / VariantMap -> 逐元素校验后转换。
 *
 * 主要支持内建容器（vector<int>、vector<string>、map<string,string>、
 * map<int,int>、vector<Variant>、map<string,Variant>）；其他类型只支持
 * 扩展槽精确匹配。
 *
 * @param [in] Variant 值
 * @return std::optional<T> 失败返回 nullopt
 */
template <class T>
std::optional<T> getAs(const Variant& value)
{
    using U = std::decay_t<T>;

    if constexpr (std::is_same_v<U, std::vector<int>>) {
        if (value.isExtension())
            return value.get<U>();
        if (!value.isList())
            return std::nullopt;
        std::vector<int> out;
        out.reserve(value.asList().size());
        for (const Variant& item : value.asList()) {
            auto number = item.get<int>();
            if (!number)
                return std::nullopt;
            out.push_back(*number);
        }
        return out;
    } else if constexpr (std::is_same_v<U, std::vector<std::string>>) {
        if (value.isExtension())
            return value.get<U>();
        if (!value.isList())
            return std::nullopt;
        std::vector<std::string> out;
        out.reserve(value.asList().size());
        for (const Variant& item : value.asList()) {
            if (!item.isString())
                return std::nullopt;
            out.push_back(item.asString());
        }
        return out;
    } else if constexpr (std::is_same_v<U, std::unordered_map<std::string, std::string>>) {
        if (value.isExtension())
            return value.get<U>();
        if (!value.isMap())
            return std::nullopt;
        U out;
        out.reserve(value.asMap().size());
        for (const auto& kv : value.asMap()) {
            if (!kv.second.isString())
                return std::nullopt;
            out.emplace(kv.first, kv.second.asString());
        }
        return out;
    } else if constexpr (std::is_same_v<U, std::unordered_map<int, int>>) {
        if (value.isExtension())
            return value.get<U>();
        U out;
        if (value.isMap()) {
            out.reserve(value.asMap().size());
            for (const auto& kv : value.asMap()) {
                auto key = detail::parseIntKey(kv.first);
                auto number = kv.second.get<int>();
                if (!key || !number)
                    return std::nullopt;
                out.insert_or_assign(*key, *number);
            }
            return out;
        }
        if (value.isList()) {
            // 也接受 [[k, v], ...] 表示
            out.reserve(value.asList().size());
            for (const Variant& item : value.asList()) {
                if (!item.isList() || item.asList().size() != 2)
                    return std::nullopt;
                auto key = item.asList()[0].get<int>();
                auto number = item.asList()[1].get<int>();
                if (!key || !number)
                    return std::nullopt;
                out.insert_or_assign(*key, *number);
            }
            return out;
        }
        return std::nullopt;
    } else if constexpr (std::is_same_v<U, std::vector<Variant>>) {
        if (value.isExtension())
            return value.get<U>();
        if (!value.isList())
            return std::nullopt;
        return U(value.asList().begin(), value.asList().end());
    } else if constexpr (std::is_same_v<U, std::unordered_map<std::string, Variant>>) {
        if (value.isExtension())
            return value.get<U>();
        if (!value.isMap())
            return std::nullopt;
        U out;
        out.reserve(value.asMap().size());
        for (const auto& kv : value.asMap())
            out.emplace(kv.first, kv.second);
        return out;
    } else {
        if (!value.isExtension())
            return std::nullopt;
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

} // namespace vt
