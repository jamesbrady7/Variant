#pragma once

// 内建容器在 Variant **扩展槽**里的 C++ -> Python 转换（不依赖 shiboken
// SpecificConverter）。注意：这里只服务 `vt::Variant::fromValue(std::vector<int>)`
// 这类扩展值；函数签名里的 std::vector<T> 由 shiboken 内置容器转换器处理，
// 与这个文件无关。

namespace vpy {

/**
 * @brief 注册内建容器的 C++ -> Python hook。
 *
 * 覆盖 vector<int>、vector<string>、map<string,string>、map<int,int>。
 * @return bool 是否全部注册成功
 */
bool registerBuiltinContainerHooks();

} // namespace vpy
