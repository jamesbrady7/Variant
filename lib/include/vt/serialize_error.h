#pragma once

#include "export.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace vt {

/// 序列化/反序列化错误分类。
enum class SerializeErrorCode {
    None = 0,        ///< 无错误
    Syntax,          ///< JSON/二进制语法错误
    UnexpectedEnd,   ///< 数据提前结束
    InvalidUtf8,     ///< 非法 UTF-8
    InvalidNumber,   ///< 数字格式/范围错误
    DepthExceeded,   ///< 嵌套过深
    UnknownType,     ///< 扩展类型没有注册 codec
    UnsupportedValue,///< NaN/Inf 等无法表示的值
    BadLength,       ///< 长度字段非法/超出剩余数据
    VersionMismatch, ///< 版本不匹配（JSON 信封 / 二进制头）
    TrailingData,    ///< 根值之后还有多余数据
    EncodeFailed,    ///< codec 编码失败
    IoError,         ///< 流/文件读写失败
};

/**
 * @brief 错误码转可读字符串。
 * @param [in] SerializeErrorCode 错误码
 * @return const char* 名称
 */
VT_API const char* toString(SerializeErrorCode code) noexcept;

/// 结构化错误信息。
struct VT_API SerializeError {
    SerializeErrorCode code = SerializeErrorCode::None;  ///< 错误分类
    std::string message;                                 ///< 描述
    std::size_t offset = 0;                              ///< 字节偏移
    int line = 0;                                        ///< 1-based 行号（仅 JSON）
    int column = 0;                                      ///< 1-based 列号（仅 JSON）

    /// @brief 是否没有错误。
    bool ok() const noexcept { return code == SerializeErrorCode::None; }

    /**
     * @brief 生成带错误码/偏移的可读文本。
     * @return std::string 文本
     */
    std::string toString() const;
};

/**
 * @brief 序列化异常；`write/read/writeFile/readFile` 失败时抛出。
 *
 * `what()` 是格式化后的错误文本，`info()` 保留结构化字段。
 */
class VT_API SerializeException : public std::runtime_error {
public:
    /**
     * @brief 用错误信息构造异常。
     * @param [in] SerializeError 错误信息
     */
    explicit SerializeException(SerializeError info);

    /// @brief 结构化错误信息。
    const SerializeError& info() const noexcept { return m_info; }

private:
    SerializeError m_info;  ///< 原始错误
};

} // namespace vt
