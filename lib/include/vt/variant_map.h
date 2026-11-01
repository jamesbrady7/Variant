#pragma once

#include "export.h"
#include "path.h"
#include "variant.h"

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace vt {

/**
 * @brief `string -> Variant` 的哈希表。
 *
 * 访问契约与 Variant 一致：字符串参数是**字面键**，`Path` 参数是**路径**。
 * - `m["a.b"]`      -> 键 "a.b"
 * - `m["a.b"_path]` -> 路径 a -> b
 *
 * 方法名完全相同，只靠参数类型区分，不存在 literal / path 两套命名。
 *
 * @note 遍历顺序不稳定（unordered_map）；JSON 输出默认按键排序。
 */
class VT_API VariantMap {
public:
    using Storage = std::unordered_map<std::string, Variant>;  ///< 底层存储
    using iterator = Storage::iterator;                        ///< 可写迭代器
    using const_iterator = Storage::const_iterator;            ///< 只读迭代器
    using value_type = Storage::value_type;                    ///< 元素类型

    /// @brief 构造空 map。
    VariantMap() = default;

    /**
     * @brief 从初始化列表构造。
     * @param [in] std::initializer_list 键值对列表
     */
    VariantMap(std::initializer_list<std::pair<std::string, Variant>> init);

    /**
     * @brief 接管一个已有的 unordered_map。
     * @param [in] Storage 底层存储（按值传入，可移动）
     */
    explicit VariantMap(Storage data);

    // ---- 字面键（std::map 习惯）----
    // 只保留 string_view 入口：字面量 / std::string 都能转换，
    // 也避免 m[0] 被解释成空指针常量。

    /**
     * @brief 以字面键访问子节点；缺失时自动创建 Null。
     * @param [in] std::string_view 键
     * @return Variant& 子节点引用
     */
    Variant& operator[](std::string_view key);

    /**
     * @brief 以字面键只读访问；缺失返回 Null 拷贝。
     * @param [in] std::string_view 键
     * @return Variant 子节点拷贝
     */
    Variant operator[](std::string_view key) const;

    /**
     * @brief 按字面键查找（只读）。
     *
     * 内部构造 std::string（C++17 的 unordered_map 没有异构查找），不标 noexcept。
     * @param [in] std::string_view 键
     * @return const Variant* 缺失返回 nullptr
     */
    const Variant* find(std::string_view key) const;

    /**
     * @brief 按字面键查找（只读，无临时分配）。
     * @param [in] std::string 键
     * @return const Variant* 缺失返回 nullptr
     */
    const Variant* find(const std::string& key) const noexcept;

    /**
     * @brief 按字面键查找（只读，C 字符串）。
     * @param [in] const char* 键
     * @return const Variant* 缺失返回 nullptr
     */
    const Variant* find(const char* key) const;

    /**
     * @brief 按字面键查找（可写）。
     * @param [in] std::string_view 键
     * @return Variant* 缺失返回 nullptr
     */
    Variant* find(std::string_view key);

    /**
     * @brief 按字面键查找（可写，无临时分配）。
     * @param [in] std::string 键
     * @return Variant* 缺失返回 nullptr
     */
    Variant* find(const std::string& key) noexcept;

    /**
     * @brief 按字面键查找（可写，C 字符串）。
     * @param [in] const char* 键
     * @return Variant* 缺失返回 nullptr
     */
    Variant* find(const char* key);

    /**
     * @brief 按字面键严格取值。
     * @param [in] std::string_view 键
     * @return const Variant& 子节点
     * @throws VariantError 键不存在（KeyNotFound）
     */
    const Variant& at(std::string_view key) const;

    /**
     * @brief 按字面键严格取可写引用。
     * @param [in] std::string_view 键
     * @return Variant& 子节点
     * @throws VariantError 键不存在（KeyNotFound）
     */
    Variant& at(std::string_view key);

    /**
     * @brief 是否含有某个字面键。
     * @param [in] std::string_view 键
     * @return bool 是否存在
     */
    bool contains(std::string_view key) const;

    /**
     * @brief 删除字面键。
     * @param [in] std::string_view 键
     * @return bool 是否删除了元素
     */
    bool erase(std::string_view key);

    /**
     * @brief 以字面键写入；失败返回 false（不抛）。
     * @param [in] std::string_view 键
     * @param [in] Variant 值
     * @return bool 是否成功
     */
    bool set(std::string_view key, Variant value) noexcept;

    /**
     * @brief 按字面键取值。
     * @param [in] std::string_view 键
     * @return std::optional<T> 缺失或类型不符返回 nullopt
     */
    template <class T>
    std::optional<T> getAt(std::string_view key) const {
        if (const Variant* p = find(key)) return p->get<T>();
        return std::nullopt;
    }

    // ---- 路径（显式 Path）----

    /**
     * @brief 按路径写入；自动创建中间节点。
     * @param [in] Path 路径
     * @return Variant& 目标节点引用
     * @throws VariantError 路径为空 / 类型冲突 / 下标越界
     */
    Variant& operator[](const Path& path);

    /**
     * @brief 按路径只读访问；缺失返回 Null 拷贝。
     * @param [in] Path 路径
     * @return Variant 目标节点拷贝
     */
    Variant operator[](const Path& path) const;

    /**
     * @brief 按路径查找（只读）。
     * @param [in] Path 路径
     * @return const Variant* 缺失返回 nullptr
     */
    const Variant* find(const Path& path) const noexcept;

    /**
     * @brief 按路径查找（可写）。
     * @param [in] Path 路径
     * @return Variant* 缺失返回 nullptr
     */
    Variant* find(const Path& path) noexcept;

    /**
     * @brief 按路径严格取值。
     * @param [in] Path 路径
     * @return const Variant& 目标节点
     * @throws VariantError 路径不存在 / 类型冲突 / 越界
     */
    const Variant& at(const Path& path) const;

    /**
     * @brief 按路径严格取可写引用。
     * @param [in] Path 路径
     * @return Variant& 目标节点
     * @throws VariantError 路径不存在 / 类型冲突 / 越界
     */
    Variant& at(const Path& path);

    /**
     * @brief 路径是否存在。
     * @param [in] Path 路径
     * @return bool 是否存在
     */
    bool contains(const Path& path) const noexcept;

    /**
     * @brief 按路径写入；失败返回 false（不抛）。
     * @param [in] Path 路径
     * @param [in] Variant 值
     * @return bool 是否成功
     */
    bool set(const Path& path, Variant value) noexcept;

    /**
     * @brief 按路径取值。
     * @param [in] Path 路径
     * @return std::optional<T> 缺失或类型不符返回 nullopt
     */
    template <class T>
    std::optional<T> getAt(const Path& path) const {
        if (const Variant* p = find(path)) return p->get<T>();
        return std::nullopt;
    }

    // ---- 普通容器接口 ----

    /// @brief 元素个数。
    std::size_t size() const noexcept { return m_data.size(); }
    /// @brief 是否为空。
    bool empty() const noexcept { return m_data.empty(); }
    /// @brief 清空所有元素。
    void clear() noexcept { m_data.clear(); }
    /**
     * @brief 预留桶数量，避免反复 rehash。
     * @param [in] std::size_t 预期元素数
     */
    void reserve(std::size_t n) { m_data.reserve(n); }

    /// @brief 可写迭代器起点。
    iterator begin() noexcept { return m_data.begin(); }
    /// @brief 可写迭代器终点。
    iterator end() noexcept { return m_data.end(); }
    /// @brief 只读迭代器起点。
    const_iterator begin() const noexcept { return m_data.begin(); }
    /// @brief 只读迭代器终点。
    const_iterator end() const noexcept { return m_data.end(); }

    /**
     * @brief 内容相等比较（与顺序无关）。
     * @param [in] VariantMap 另一个 map
     * @return bool 是否相等
     */
    bool operator==(const VariantMap& other) const;

    /**
     * @brief 内容不等比较。
     * @param [in] VariantMap 另一个 map
     * @return bool 是否不等
     */
    bool operator!=(const VariantMap& other) const { return !(*this == other); }

private:
    /// @brief 路径解析：第一段必须是键。
    Variant& resolveCreate(const Path& path);

    /// @brief 路径查找：第一段必须是键。
    const Variant* resolveFind(const Path& path) const noexcept;

    Storage m_data;  ///< 实际存储
};

} // namespace vt
