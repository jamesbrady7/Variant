#pragma once

#include "export.h"
#include "variant.h"

#include <string>

namespace vt {

/**
 * @brief 生成调试/日志用的 JSON 风格文本。
 *
 * 不是严格 JSON：扩展类型写成 `<TypeName>`，map 顺序不保证。
 * 需要可解析的 JSON 请用 `vt::json::write`。
 * @param [in] Variant 值
 * @return std::string 文本
 */
VT_API std::string describe(const Variant& value);

/**
 * @brief 生成 map 的调试文本。
 * @param [in] VariantMap 映射表
 * @return std::string 文本
 */
VT_API std::string describe(const VariantMap& map);

/**
 * @brief 生成 list 的调试文本。
 * @param [in] VariantList 列表
 * @return std::string 文本
 */
VT_API std::string describe(const VariantList& list);

} // namespace vt
