#pragma once

#include "export.h"

#include <mutex>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vt {

/**
 * @brief 扩展类型的名字表。
 *
 * `std::any` 本身不需要注册也能存值；注册的意义是：
 * - 给类型一个稳定名字（describe / 日志 / 序列化）；
 * - 让 Python 桥（以及 codec 表）按类型或名字查找。
 *
 * 同类型/同名重复注册：名字一致返回 true；冲突返回 false 并保留首次注册。
 * 所有方法线程安全（内部互斥锁）。
 */
class VT_API TypeRegistry {
public:
    /// 一条注册记录。
    struct Entry {
        std::type_index type;  ///< C++ 动态类型
        std::string name;      ///< 稳定名字
    };

    /**
     * @brief 全局单例。
     * @return TypeRegistry& 注册表
     */
    static TypeRegistry& instance();

    /**
     * @brief 注册一个类型名。
     * @param [in] std::type_index 类型索引
     * @param [in] std::string 名字
     * @return bool 成功；同名或同类型冲突返回 false
     */
    bool registerType(std::type_index type, std::string name);

    /**
     * @brief 注册一个类型名（模板版）。
     * @param [in] std::string 名字
     * @return bool 成功
     */
    template <class T>
    bool registerType(std::string name) {
        return registerType(std::type_index(typeid(T)), std::move(name));
    }

    /**
     * @brief 按类型查找。
     * @param [in] std::type_index 类型索引
     * @return const Entry* 未注册返回 nullptr
     */
    const Entry* find(std::type_index type) const;

    /**
     * @brief 按名字查找。
     * @param [in] std::string_view 名字
     * @return const Entry* 未注册返回 nullptr
     */
    const Entry* findByName(std::string_view name) const;

    /**
     * @brief 类型是否已注册。
     * @param [in] std::type_index 类型索引
     * @return bool 是否已注册
     */
    bool isRegistered(std::type_index type) const;

    /**
     * @brief 类型是否已注册（模板版）。
     * @param [in] T 类型
     * @return bool 是否已注册
     */
    template <class T>
    bool isRegistered() const {
        return isRegistered(std::type_index(typeid(T)));
    }

    /**
     * @brief 导出全部注册记录（快照）。
     * @return std::vector<Entry> 记录列表
     */
    std::vector<Entry> entries() const;

    /// @brief 清空注册表。
    void clear();

    /// @brief 已注册类型数量。
    std::size_t size() const;

private:
    TypeRegistry() = default;
    mutable std::mutex m_mutex;                                   ///< 保护两张表
    std::unordered_map<std::type_index, Entry> m_byType;          ///< 类型 -> 记录
    std::unordered_map<std::string, std::type_index> m_byName;    ///< 名字 -> 类型
};

} // namespace vt
