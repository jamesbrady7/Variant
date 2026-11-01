#pragma once

#include "export.h"
#include "vt/variant.h"
#include "vt/variant_map.h"

#include <string>
#include <unordered_map>
#include <vector>

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
 * @brief 原样返回 VariantMap（验证 VariantMap 直接作为函数签名）。
 * @param [in] vt::VariantMap 映射表
 * @return vt::VariantMap 同一个映射表
 */
VT_DEMO_API vt::VariantMap echoMap(const vt::VariantMap& value);

/**
 * @brief 原样返回 vector<Variant>（验证 shiboken 内置容器递归）。
 * @param [in] std::vector<vt::Variant> 列表
 * @return std::vector<vt::Variant> 同一个列表
 */
VT_DEMO_API std::vector<vt::Variant> echoVector(const std::vector<vt::Variant>& value);

/**
 * @brief 原样返回 unordered_map<string, Variant>（验证内置 map 容器递归）。
 * @param [in] std::unordered_map 映射表
 * @return std::unordered_map 同一个映射表
 */
VT_DEMO_API std::unordered_map<std::string, vt::Variant> echoUnorderedMap(
    const std::unordered_map<std::string, vt::Variant>& value);

/**
 * @brief 用 describe() 生成调试文本。
 * @param [in] vt::Variant 值
 * @return std::string 调试文本
 */
VT_DEMO_API std::string describeValue(const vt::Variant& value);
