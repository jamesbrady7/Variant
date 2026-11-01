#pragma once

#include "export.h"
#include "variant.h"

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <vector>

namespace vt {

/**
 * @brief `Variant` 的数组。
 *
 * 路径接口的根是列表本身：路径第一段必须是下标，例如 `[0].name`。
 * `operator[](size_t)` 与 `std::vector` 一样不做边界检查；需要检查时用 `at()`。
 */
class VT_API VariantList {
public:
    using Storage = std::vector<Variant>;             ///< 底层存储
    using iterator = Storage::iterator;               ///< 可写迭代器
    using const_iterator = Storage::const_iterator;   ///< 只读迭代器

    /// @brief 构造空列表。
    VariantList() = default;

    /**
     * @brief 从初始化列表构造（元素被拷贝）。
     * @param [in] std::initializer_list Variant 列表
     */
    VariantList(std::initializer_list<Variant> init) : m_data(init) {}

    /**
     * @brief 接管一个已有的 vector。
     * @param [in] Storage 底层存储（按值传入，可移动）
     */
    explicit VariantList(Storage data) : m_data(std::move(data)) {}

    /**
     * @brief 下标访问（不检查边界，vector 语义）。
     * @param [in] std::size_t 下标
     * @return Variant& 元素引用
     */
    Variant& operator[](std::size_t index) { return m_data[index]; }

    /**
     * @brief 下标只读访问（不检查边界）。
     * @param [in] std::size_t 下标
     * @return const Variant& 元素引用
     */
    const Variant& operator[](std::size_t index) const { return m_data[index]; }

    /**
     * @brief 下标严格访问。
     * @param [in] std::size_t 下标
     * @return Variant& 元素引用
     * @throws VariantError 越界（IndexOutOfBounds）
     */
    Variant& at(std::size_t index);

    /**
     * @brief 下标严格只读访问。
     * @param [in] std::size_t 下标
     * @return const Variant& 元素引用
     * @throws VariantError 越界（IndexOutOfBounds）
     */
    const Variant& at(std::size_t index) const;

    // ---- 路径（显式 Path；首段必须是下标）----

    /**
     * @brief 按路径写入；自动创建中间节点。
     * @param [in] Path 路径
     * @return Variant& 目标节点引用
     * @throws VariantError 路径为空 / 首段不是下标 / 越界
     */
    Variant& operator[](const Path& path);

    /**
     * @brief 按路径只读访问；缺失返回 Null 拷贝。
     * @param [in] Path 路径
     * @return Variant 目标节点拷贝
     */
    Variant operator[](const Path& path) const;

    /**
     * @brief 按路径查找。
     * @param [in] Path 路径
     * @return const Variant* 缺失返回 nullptr
     */
    const Variant* find(const Path& path) const noexcept;

    /**
     * @brief 按路径严格取可写引用。
     * @param [in] Path 路径
     * @return Variant& 目标节点
     * @throws VariantError 路径不存在 / 类型冲突 / 越界
     */
    Variant& at(const Path& path);

    /**
     * @brief 按路径严格取值。
     * @param [in] Path 路径
     * @return const Variant& 目标节点
     * @throws VariantError 路径不存在 / 类型冲突 / 越界
     */
    const Variant& at(const Path& path) const;

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

    /**
     * @brief 追加元素。
     * @param [in] Variant 值
     */
    void push_back(Variant v) { m_data.push_back(std::move(v)); }

    /**
     * @brief 原地构造元素。
     * @param [in] Args 构造参数
     * @return Variant& 新元素引用
     */
    template <class... Args>
    Variant& emplace_back(Args&&... args) {
        m_data.emplace_back(std::forward<Args>(args)...);
        return m_data.back();
    }

    /// @brief 元素个数。
    std::size_t size() const noexcept { return m_data.size(); }
    /// @brief 是否为空。
    bool empty() const noexcept { return m_data.empty(); }
    /// @brief 清空所有元素。
    void clear() noexcept { m_data.clear(); }
    /**
     * @brief 预留容量。
     * @param [in] std::size_t 预期元素数
     */
    void reserve(std::size_t n) { m_data.reserve(n); }
    /**
     * @brief 改变元素个数（新元素为 Null）。
     * @param [in] std::size_t 新大小
     */
    void resize(std::size_t n) { m_data.resize(n); }

    /// @brief 可写迭代器起点。
    iterator begin() noexcept { return m_data.begin(); }
    /// @brief 可写迭代器终点。
    iterator end() noexcept { return m_data.end(); }
    /// @brief 只读迭代器起点。
    const_iterator begin() const noexcept { return m_data.begin(); }
    /// @brief 只读迭代器终点。
    const_iterator end() const noexcept { return m_data.end(); }

    /**
     * @brief 按下标取值。
     * @param [in] std::size_t 下标
     * @return std::optional<T> 越界或类型不符返回 nullopt
     */
    template <class T>
    std::optional<T> getAt(std::size_t index) const {
        if (index >= m_data.size()) return std::nullopt;
        return m_data[index].get<T>();
    }

    /**
     * @brief 内容相等比较。
     * @param [in] VariantList 另一个列表
     * @return bool 是否相等
     */
    bool operator==(const VariantList& other) const { return m_data == other.m_data; }

    /**
     * @brief 内容不等比较。
     * @param [in] VariantList 另一个列表
     * @return bool 是否不等
     */
    bool operator!=(const VariantList& other) const { return !(*this == other); }

private:
    /// @brief 路径解析：首段必须是下标。
    Variant& resolveCreate(const Path& path);

    /// @brief 路径查找：首段必须是下标。
    const Variant* resolveFind(const Path& path) const noexcept;

    Storage m_data;  ///< 实际存储
};

} // namespace vt
