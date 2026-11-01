#pragma once

// 前置声明：变体、容器、路径、错误。
// 递归类型必须先有名字，才能在 Variant 的存储里用 shared_ptr 间接持有。

namespace vt {

class Variant;
class VariantMap;
class VariantList;
class Path;
class VariantError;
class VariantDocument;

// 扩展槽的堆对象（定义在 variant.cpp）。std::any 在 MSVC 下是 64 字节，
// 直接做 variant alternative 会把整个 Variant 撑到 72 字节；隔一层共享指针后
// 尺寸由 std::string(32) 决定。
struct VariantExt;

} // namespace vt
