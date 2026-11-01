#pragma once

#include "export.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace vt {

/// 路径访问失败的分类；比一个 bool 多出的信息是“为什么失败”。
enum class VariantErrorCode {
    SyntaxError,        ///< 路径串本身不合法
    EmptyPath,          ///< 空路径用在了容器上
    NotAMap,            ///< 期望 map，实际是别的类型
    NotAList,           ///< 期望 list，实际是别的类型
    KeyNotFound,        ///< map 里没有这个键
    IndexOutOfBounds,   ///< list 下标越界
    TypeMismatch,       ///< 值存在但类型不对（如想取 int 却存了 string）
    OutOfRange,         ///< 数值超出可表示范围（如 uint64 > INT64_MAX）
};

/**
 * @brief 错误码转可读字符串。
 * @param [in] VariantErrorCode 错误码
 * @return const char* 名称（"KeyNotFound" 等）
 */
VT_API const char* toString(VariantErrorCode code) noexcept;

/**
 * @brief 结构化路径异常。
 *
 * 除了 message，还携带：错误码、原始路径、第几段、该段在路径串里的字符偏移。
 */
class VT_API VariantError : public std::runtime_error {
public:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);  ///< “无此信息”

    /**
     * @brief 构造异常。
     * @param [in] VariantErrorCode 错误码
     * @param [in] std::string 描述
     * @param [in] std::string 原始路径（可选）
     * @param [in] std::size_t 出错段下标（可选）
     * @param [in] std::size_t 该段的字符偏移（可选）
     */
    VariantError(VariantErrorCode code,
                 std::string message,
                 std::string path = {},
                 std::size_t segment = npos,
                 std::size_t offset = npos);

    /// @brief 错误码。
    VariantErrorCode code() const noexcept { return m_code; }

    /// @brief 原始路径文本。
    const std::string& path() const noexcept { return m_path; }

    /// @brief 出错段下标（可能为 npos）。
    std::size_t segment() const noexcept { return m_segment; }

    /// @brief 出错段在路径串中的字符偏移（可能为 npos）。
    std::size_t offset() const noexcept { return m_offset; }

private:
    VariantErrorCode m_code;  ///< 错误分类
    std::string m_path;       ///< 原始路径
    std::size_t m_segment;    ///< 段下标
    std::size_t m_offset;     ///< 字符偏移
};

} // namespace vt
