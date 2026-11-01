#pragma once

#include "export.h"
#include "variant.h"

#include <string>

/**
 * @brief 路径式文档：把一棵 Variant 树包成 get/set/has，失败返回 false / Null。
 *
 * C++ 侧想要“失败即异常”时直接用 `Variant::at`；这个类是给需要
 * “不抛 + lastError()”风格的调用方准备的便利包装。注意它的字符串参数
 * 一律按**路径**解释（这是文档 API 自己的契约，与 Variant 的
 * “字符串 = 字面键”不同）。
 *
 * @note 当前 Python 绑定使用真实签名（`vt::Variant` 作为 primitive-type），
 *       并不经过这个类；不要把它当成绑定层入口。
 * @note 类放在全局命名空间：shiboken6 的 typesystem 已不支持 `ns::Class`
 *       形式（错误 GD60709C2）；将来若要绑定它，类必须是全局类型。
 */
class VT_API VariantDocument {
public:
    /// @brief 构造空文档。
    VariantDocument() = default;

    /**
     * @brief 用一棵 Variant 树构造文档。
     * @param [in] vt::Variant 根值
     */
    explicit VariantDocument(vt::Variant root)
        : m_root(std::move(root))
    {
    }

    /**
     * @brief 按路径读取（字符串路径）。
     *
     * 文档接口的字符串一律按**路径**解释（这是文档 API，不是 map 的键接口）。
     * 参数用 std::string 而非 string_view：shiboken 对 std::string 的绑定更成熟。
     * @param [in] std::string 路径
     * @return vt::Variant 节点拷贝；缺失返回 Null
     */
    vt::Variant get(const std::string& path) const;

    /**
     * @brief 按预编译路径读取。
     * @param [in] vt::Path 路径
     * @return vt::Variant 节点拷贝；缺失返回 Null
     */
    vt::Variant get(const vt::Path& path) const;

    /**
     * @brief 按路径写入（字符串路径）；失败不抛。
     * @param [in] std::string 路径
     * @param [in] vt::Variant 值
     * @return bool 是否成功；失败原因见 lastError()
     */
    bool set(const std::string& path, vt::Variant value);

    /**
     * @brief 按预编译路径写入；失败不抛。
     * @param [in] vt::Path 路径
     * @param [in] vt::Variant 值
     * @return bool 是否成功
     */
    bool set(const vt::Path& path, vt::Variant value);

    /**
     * @brief 路径是否存在（字符串路径）。
     * @param [in] std::string 路径
     * @return bool 是否存在
     */
    bool has(const std::string& path) const noexcept;

    /**
     * @brief 路径是否存在（预编译路径）。
     * @param [in] vt::Path 路径
     * @return bool 是否存在
     */
    bool has(const vt::Path& path) const noexcept;

    /// @brief 根值（可写）。
    vt::Variant& root() noexcept { return m_root; }

    /// @brief 根值（只读）。
    const vt::Variant& root() const noexcept { return m_root; }

    /// @brief 清空文档与错误信息。
    void clear() noexcept
    {
        m_root = vt::Variant();
        m_lastError.clear();
    }

    /// @brief 最近一次失败原因。
    const std::string& lastError() const noexcept { return m_lastError; }

private:
    vt::Variant m_root;       ///< 根值
    std::string m_lastError;  ///< 最近一次错误文本
};
