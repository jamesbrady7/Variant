#pragma once

#include "export.h"
#include "variant.h"

#include <any>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace vt {

/**
 * @brief 扩展类型的序列化编解码表（与 Python 的 vpy Hook 表无关）。
 *
 * 一个类型只需要注册一次：
 * - `encode` 把它变成“只含内建类型的 Variant”；
 * - `decode` 再从该表示还原。
 *
 * JSON 和二进制共用这张表。所有方法线程安全（内部互斥锁）。
 */
class VT_API CodecRegistry {
public:
    /// 一条 codec 记录。
    struct Entry {
        std::type_index type;                                   ///< C++ 动态类型
        std::string name;                                       ///< 序列化协议名
        std::function<Variant(const std::any&)> encode;         ///< T -> 普通 Variant
        std::function<bool(const Variant&, std::any&)> decode;  ///< 普通 Variant -> T
        bool plain = false;  ///< true 表示“表示本身就是普通 List/Map”，JSON 默认不写信封
    };

    /**
     * @brief 全局单例。
     * @return CodecRegistry& 注册表
     */
    static CodecRegistry& instance();

    /**
     * @brief 注册一个 codec。
     * @param [in] std::type_index 类型索引
     * @param [in] std::string 协议名
     * @param [in] std::function encode 函数
     * @param [in] std::function decode 函数
     * @param [in] bool 是否为 plain codec
     * @return bool 成功；类型/名字冲突返回 false
     */
    bool registerCodec(std::type_index type,
                       std::string name,
                       std::function<Variant(const std::any&)> encode,
                       std::function<bool(const Variant&, std::any&)> decode,
                       bool plain = false);

    /**
     * @brief 按类型查找 codec。
     * @param [in] std::type_index 类型索引
     * @return const Entry* 未注册返回 nullptr
     */
    const Entry* find(std::type_index type) const;

    /**
     * @brief 按协议名查找 codec。
     * @param [in] std::string_view 协议名
     * @return const Entry* 未注册返回 nullptr
     */
    const Entry* findByName(std::string_view name) const;

    /**
     * @brief 类型是否已注册 codec。
     * @param [in] std::type_index 类型索引
     * @return bool 是否已注册
     */
    bool isRegistered(std::type_index type) const;

    /// @brief 已注册 codec 数量。
    std::size_t size() const;

    /// @brief 清空注册表（主要用于测试）。
    void clear();

private:
    CodecRegistry() = default;
    mutable std::mutex m_mutex;                                  ///< 保护两张表
    std::unordered_map<std::type_index, Entry> m_byType;         ///< 类型 -> 记录
    std::unordered_map<std::string, std::type_index> m_byName;   ///< 名字 -> 类型
};

/**
 * @brief 注册值类型的 codec（Variant 扩展槽里存的是 T）。
 * @param [in] std::string 协议名
 * @param [in] std::function T -> Variant
 * @param [in] std::function Variant -> T（失败返回 false）
 * @param [in] bool 是否为 plain codec
 * @return bool 是否注册成功
 */
template <class T>
bool registerValueCodec(std::string name,
                        std::function<Variant(const T&)> encode,
                        std::function<bool(const Variant&, T&)> decode,
                        bool plain = false)
{
    auto encodeAny = [encode = std::move(encode)](const std::any& any) -> Variant {
        const T* value = std::any_cast<T>(&any);
        if (!value)
            throw std::runtime_error("codec: std::any does not hold the registered value type");
        return encode(*value);
    };
    auto decodeAny = [decode = std::move(decode)](const Variant& plain, std::any& out) -> bool {
        T value{};
        if (!decode(plain, value))
            return false;
        out = std::move(value);
        return true;
    };
    return CodecRegistry::instance().registerCodec(
        std::type_index(typeid(T)), std::move(name), std::move(encodeAny), std::move(decodeAny),
        plain);
}

/**
 * @brief 注册对象类型的 codec（Variant 扩展槽里存的是 T*）。
 *
 * @warning Variant 不拥有 T*；decode 返回的指针所有权由调用方/Variant 持有者约定。
 * @param [in] std::string 协议名
 * @param [in] std::function T* -> Variant
 * @param [in] std::function Variant -> T*（失败返回 false）
 * @param [in] bool 是否为 plain codec
 * @return bool 是否注册成功
 */
template <class T>
bool registerPointerCodec(std::string name,
                          std::function<Variant(const T*)> encode,
                          std::function<bool(const Variant&, T*&)> decode,
                          bool plain = false)
{
    auto encodeAny = [encode = std::move(encode)](const std::any& any) -> Variant {
        T* const* value = std::any_cast<T*>(&any);
        if (!value)
            throw std::runtime_error("codec: std::any does not hold the registered pointer type");
        return encode(*value);
    };
    auto decodeAny = [decode = std::move(decode)](const Variant& plain, std::any& out) -> bool {
        T* value = nullptr;
        if (!decode(plain, value))
            return false;
        out = value;
        return true;
    };
    return CodecRegistry::instance().registerCodec(
        std::type_index(typeid(T*)), std::move(name), std::move(encodeAny), std::move(decodeAny),
        plain);
}

} // namespace vt
