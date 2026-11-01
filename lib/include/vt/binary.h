#pragma once

#include "export.h"
#include "serialize.h"
#include "variant.h"

#include <iosfwd>
#include <string>
#include <string_view>

namespace vt::binary {

/**
 * @brief 把 Variant 序列化为二进制。
 *
 * 格式：`'V' 'T' 'B' 0x01` + 递归标签；扩展类型始终带类型名。
 * @param [in] Variant 值
 * @param [in] BinaryWriteOptions 写出选项
 * @return std::string 二进制字节
 * @throws SerializeException 序列化失败
 */
VT_API std::string write(const Variant& value, const BinaryWriteOptions& options = {});

/**
 * @brief 把 Variant 序列化为二进制（不抛）。
 * @param [in] Variant 值
 * @param [out] std::string 输出字节（仅成功时写入）
 * @param [out] SerializeError 错误信息
 * @param [in] BinaryWriteOptions 写出选项
 * @return bool 是否成功
 */
VT_API bool tryWrite(const Variant& value,
                     std::string& out,
                     SerializeError* error = nullptr,
                     const BinaryWriteOptions& options = {});

/**
 * @brief 从二进制反序列化为 Variant。
 * @param [in] std::string_view 二进制字节
 * @param [in] BinaryReadOptions 读入选项
 * @return Variant 解析结果
 * @throws SerializeException 解析失败
 */
VT_API Variant read(std::string_view bytes, const BinaryReadOptions& options = {});

/**
 * @brief 从二进制反序列化为 Variant（不抛）。
 * @param [in] std::string_view 二进制字节
 * @param [out] Variant 解析结果（仅成功时写入）
 * @param [out] SerializeError 错误信息
 * @param [in] BinaryReadOptions 读入选项
 * @return bool 是否成功
 */
VT_API bool tryRead(std::string_view bytes,
                    Variant& out,
                    SerializeError* error = nullptr,
                    const BinaryReadOptions& options = {});

/**
 * @brief 把 Variant 写入输出流。
 * @param [in] std::ostream 输出流
 * @param [in] Variant 值
 * @param [out] SerializeError 错误信息（含 IoError）
 * @param [in] BinaryWriteOptions 写出选项
 * @return bool 是否成功
 */
VT_API bool writeTo(std::ostream& output,
                    const Variant& value,
                    SerializeError* error = nullptr,
                    const BinaryWriteOptions& options = {});

/**
 * @brief 从输入流读取二进制并解析为 Variant。
 * @param [in] std::istream 输入流
 * @param [out] Variant 解析结果
 * @param [out] SerializeError 错误信息（含 IoError）
 * @param [in] BinaryReadOptions 读入选项
 * @return bool 是否成功
 */
VT_API bool readFrom(std::istream& input,
                     Variant& out,
                     SerializeError* error = nullptr,
                     const BinaryReadOptions& options = {});

/**
 * @brief 把 Variant 写入文件（二进制模式）。
 * @param [in] std::string 文件路径
 * @param [in] Variant 值
 * @param [in] BinaryWriteOptions 写出选项
 * @throws SerializeException 序列化或文件写入失败
 */
VT_API void writeFile(const std::string& path,
                      const Variant& value,
                      const BinaryWriteOptions& options = {});

/**
 * @brief 把 Variant 写入文件（不抛）。
 * @param [in] std::string 文件路径
 * @param [in] Variant 值
 * @param [out] SerializeError 错误信息
 * @param [in] BinaryWriteOptions 写出选项
 * @return bool 是否成功
 */
VT_API bool tryWriteFile(const std::string& path,
                         const Variant& value,
                         SerializeError* error = nullptr,
                         const BinaryWriteOptions& options = {});

/**
 * @brief 从文件读取二进制并解析为 Variant。
 * @param [in] std::string 文件路径
 * @param [in] BinaryReadOptions 读入选项
 * @return Variant 解析结果
 * @throws SerializeException 文件读取或解析失败
 */
VT_API Variant readFile(const std::string& path, const BinaryReadOptions& options = {});

/**
 * @brief 从文件读取二进制并解析为 Variant（不抛）。
 * @param [in] std::string 文件路径
 * @param [out] Variant 解析结果
 * @param [out] SerializeError 错误信息
 * @param [in] BinaryReadOptions 读入选项
 * @return bool 是否成功
 */
VT_API bool tryReadFile(const std::string& path,
                        Variant& out,
                        SerializeError* error = nullptr,
                        const BinaryReadOptions& options = {});

} // namespace vt::binary
