#pragma once

#include "export.h"
#include "vt/variant.h"
#include "vt/variant_list.h"
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
 * @brief 原样返回 VariantList（验证 VariantList 直接作为函数签名）。
 * @param [in] vt::VariantList 列表
 * @return vt::VariantList 同一个列表
 */
VT_DEMO_API vt::VariantList echoList(const vt::VariantList& value);

/**
 * @brief 演示/测试：`vector<int>` 的真实签名（Python list 自动转换）。
 * @param [in] std::vector<int> 整数序列
 * @return int 和
 */
VT_DEMO_API int sumInts(const std::vector<int>& values);

/**
 * @brief 演示/测试：`unordered_map<int,int>` 的真实签名（Python dict 自动转换）。
 * @param [in] std::unordered_map<int,int> 整数映射表
 * @return int 元素个数
 */
VT_DEMO_API int intMapSize(const std::unordered_map<int, int>& values);

/**
 * @brief 用 describe() 生成调试文本。
 * @param [in] vt::Variant 值
 * @return std::string 调试文本
 */
VT_DEMO_API std::string describeValue(const vt::Variant& value);

// ---- 测试辅助：由 C++ 静态持有 payload，验证跨语言生命周期 ----
// 只服务共享所有权测试；不是通用文档 API（PyDocument 已删除）。

/**
 * @brief 让 C++ 静态变量持有这份 VariantMap。
 * @param [in] vt::VariantMap 映射表
 */
VT_DEMO_API void holdPayload(const vt::VariantMap& payload);

/**
 * @brief 取回 C++ 当前持有的 VariantMap（拷贝）。
 * @return vt::VariantMap 映射表
 */
VT_DEMO_API vt::VariantMap heldPayload();

/// @brief 释放 C++ 静态变量持有的 payload。
VT_DEMO_API void releasePayload();
