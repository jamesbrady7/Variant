#pragma once

#include "export.h"
#include "vt/variant.h"

#include <string>

/**
 * @brief 构造一棵示例 Variant 树（标量/字符串/嵌套 map/list）。
 * @return vt::Variant 示例根值
 */
VT_DEMO_API vt::Variant makeSample();

/**
 * @brief 构造一棵包含内建容器的示例树。
 *
 * 含 `vector<int>`、`vector<string>`、`map<string,string>`、`map<int,int>`。
 * @return vt::Variant 示例根值
 */
VT_DEMO_API vt::Variant makeBuiltinSample();

/**
 * @brief 原样返回传入的 Variant（用于跨语言往返测试）。
 * @param [in] vt::Variant 值
 * @return vt::Variant 同一个值
 */
VT_DEMO_API vt::Variant echo(const vt::Variant& value);

/**
 * @brief 用 describe() 生成调试文本。
 * @param [in] vt::Variant 值
 * @return std::string 调试文本
 */
VT_DEMO_API std::string describeValue(const vt::Variant& value);
