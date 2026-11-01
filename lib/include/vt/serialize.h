#pragma once

#include "serialize_error.h"

#include <cstddef>

namespace vt {

/**
 * @brief 扩展类型在 JSON 里的呈现策略。
 */
enum class ExtensionPolicy {
    Auto,         ///< plain codec 写普通数据，其余写信封（默认）
    EnvelopeAll,  ///< 所有扩展都写 `$type` 信封（精确类型）
    Flatten,      ///< 所有扩展都只写普通表示（有损）
    Reject        ///< 非 plain 扩展报错；plain 扩展写普通表示（纯数据模式）
};

/**
 * @brief NaN/Inf 在 JSON 里没有标准表示的策略。
 */
enum class NonFinitePolicy {
    Error,  ///< 报 UnsupportedValue
    Null    ///< 写成 null（读回是 Null，类型丢失）
};

/**
 * @brief JSON 写出选项。
 */
struct JsonWriteOptions {
    bool pretty = false;                                    ///< 缩进输出
    bool sortKeys = true;                                   ///< map 按键排序，输出稳定
    ExtensionPolicy extensions = ExtensionPolicy::Auto;      ///< 扩展类型策略
    NonFinitePolicy nonFinite = NonFinitePolicy::Error;      ///< NaN/Inf 策略
    int maxDepth = 256;                                      ///< 最大嵌套深度
};

/**
 * @brief JSON 读入选项。
 */
struct JsonReadOptions {
    int maxDepth = 256;          ///< 最大嵌套深度
    bool detectExtensions = true; ///< 是否识别 `$vt/$type/$data` 信封
};

/**
 * @brief 二进制写出选项。
 */
struct BinaryWriteOptions {
    bool sortKeys = true;  ///< map 按键排序，输出稳定
    int maxDepth = 256;    ///< 最大嵌套深度
};

/**
 * @brief 二进制读入选项。
 */
struct BinaryReadOptions {
    int maxDepth = 256;                              ///< 最大嵌套深度
    std::size_t maxStringBytes = 256u * 1024u * 1024u;  ///< 单字符串上限（默认 256 MiB）
    std::size_t maxElements = 16u * 1024u * 1024u;      ///< 单容器元素上限
};

} // namespace vt
