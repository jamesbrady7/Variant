#pragma once

// 内建容器的 Python 转换（不依赖 shiboken SpecificConverter）。
#include <unordered_map>
#include <vector>

namespace vpy {

/**
 * @brief 注册内建容器的 C++ -> Python hook。
 *
 * 覆盖 vector<int>、vector<string>、map<string,string>、map<int,int>。
 * @return bool 是否全部注册成功
 */
bool registerBuiltinContainerHooks();

} // namespace vpy

/**
 * @brief 演示/测试：`vector<int>` 的真实签名（Python list 自动转换）。
 * @param [in] std::vector<int> 整数序列
 * @return int 和
 */
int sumInts(const std::vector<int>& values);

/**
 * @brief 演示/测试：`unordered_map<int,int>` 的真实签名（Python dict 自动转换）。
 * @param [in] std::unordered_map<int,int> 整数映射表
 * @return int 元素个数
 */
int intMapSize(const std::unordered_map<int, int>& values);
