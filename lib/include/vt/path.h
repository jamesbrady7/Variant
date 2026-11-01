#pragma once

#include "export.h"
#include "variant_error.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace vt {

/**
 * @brief 预编译路径：把 `"root.subtree[0].name"` 解析成段列表，热循环复用。
 *
 * 语法：
 * - 普通键：到 `.` 或 `[` 为止；
 * - `[0]`：下标；
 * - `["a.b"]`：带引号的键，支持 `\"` 与 `\\` 转义；
 * - `""`：空路径，表示根自身。
 *
 * 设计要点：路径是一个**类型**，而不是字符串约定：
 * - `m["a.b"]`    -> 字面键 `"a.b"`；
 * - `m["a.b"_path]` -> 路径 a -> b。
 */
class VT_API Path {
public:
    /// 路径中的一段：键段或下标段。
    struct Segment {
        bool isIndex = false;      ///< true 表示下标段
        std::string key;           ///< 键段内容（isIndex=false 时有效）
        std::size_t index = 0;     ///< 下标值（isIndex=true 时有效）
        std::size_t offset = 0;    ///< 该段在原始路径串中的字节偏移
    };

    /// @brief 构造空路径（表示根自身）。
    Path() = default;

    /**
     * @brief 从字符串解析路径。
     *
     * 构造函数是 `explicit` 的，避免字符串被隐式当成路径。
     * @param [in] std::string_view 路径文本
     * @throws VariantError 语法错误（SyntaxError）
     */
    explicit Path(std::string_view text);

    /**
     * @brief 解析路径（静态版本）。
     * @param [in] std::string_view 路径文本
     * @return Path 解析结果
     * @throws VariantError 语法错误（SyntaxError），message 带偏移
     */
    static Path parse(std::string_view text);

    /**
     * @brief 不抛异常的解析。
     * @param [in] std::string_view 路径文本
     * @param [out] Path 解析结果（失败时保持不变）
     * @param [out] std::string 可选错误信息
     * @return bool 是否解析成功
     */
    static bool tryParse(std::string_view text, Path& out, std::string* error = nullptr) noexcept;

    /// @brief 是否为空路径（表示根自身）。
    bool empty() const noexcept { return m_segments.empty(); }

    /// @brief 段数。
    std::size_t size() const noexcept { return m_segments.size(); }

    /// @brief 只读段列表。
    const std::vector<Segment>& segments() const noexcept { return m_segments; }

    /// @brief 原始路径文本（用于错误信息）。
    const std::string& source() const noexcept { return m_source; }

    /**
     * @brief 去掉前 n 段，返回新路径。
     *
     * 供容器把剩余路径转发给子 Variant。
     * @param [in] std::size_t 起始段下标
     * @return Path 切片后的路径
     */
    Path slice(std::size_t from) const;

    /**
     * @brief 重新拼成规范字符串（必要时补引号）。
     * @return std::string 规范路径文本
     */
    std::string toString() const;

private:
    std::vector<Segment> m_segments;  ///< 解析后的段
    std::string m_source;             ///< 原始文本
};

inline namespace literals {

/**
 * @brief 路径字面量：`"root.subtree[0]"_path`。
 * @param [in] const char* 文本
 * @param [in] std::size_t 长度
 * @return Path 解析结果（语法错误时抛出）
 */
inline Path operator""_path(const char* text, std::size_t length)
{
    return Path::parse(std::string_view(text, length));
}

} // namespace literals

} // namespace vt
