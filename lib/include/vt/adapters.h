#pragma once

// 适配层入口：标准容器 <-> Variant。
//
// 实现全部在 normalize.h：`getAs` 按 container_traits 递归还原嵌套容器，
// 不再逐类型硬编码；容器类型第一次被 `Variant::fromValue` 见到时会自动登记
// 归一化函数与 codec，因此嵌套容器（如 vector<vector<int>>）无需手写转换。

#include "export.h"
#include "normalize.h"

namespace vt {

/**
 * @brief 注册内建容器的序列化 codec。
 *
 * JSON/二进制入口会自动调用；通常不需要手动调用。用户如果先注册了同类型
 * codec，则保留用户的实现。
 */
VT_API void registerBuiltinCodecs();

} // namespace vt
