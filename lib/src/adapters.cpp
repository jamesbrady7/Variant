#include "vt/adapters.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vt {

// ---------------------------------------------------------------- fromValue

Variant Variant::fromValue(std::vector<Variant> value)
{
    return Variant(VariantList(std::move(value)));
}

Variant Variant::fromValue(std::unordered_map<std::string, Variant> value)
{
    return Variant(VariantMap(std::move(value)));
}

// 四个内建容器保持"紧凑存储"：值本身留在扩展槽里（100 万 int 约 4 MB，
// 而不是逐元素 Variant 的 40 MB）。归一化函数与 codec 由 extendValue 在
// 第一次见到类型时登记，Python 转换 / 序列化都复用同一套递归。

Variant Variant::fromValue(std::vector<int> value)
{
    return detail::extendValue(std::move(value));
}

Variant Variant::fromValue(std::vector<std::string> value)
{
    return detail::extendValue(std::move(value));
}

Variant Variant::fromValue(std::unordered_map<std::string, std::string> value)
{
    return detail::extendValue(std::move(value));
}

Variant Variant::fromValue(std::unordered_map<int, int> value)
{
    return detail::extendValue(std::move(value));
}

// ---------------------------------------------------------------- 内建 codec

void registerBuiltinCodecs()
{
    static std::once_flag once;
    std::call_once(once, [] {
        // 用户如果先注册了同类型 codec，就保留用户的实现。
        detail::ensureContainerAdapters<std::vector<int>>();
        detail::ensureContainerAdapters<std::vector<std::string>>();
        detail::ensureContainerAdapters<std::unordered_map<std::string, std::string>>();
        detail::ensureContainerAdapters<std::unordered_map<int, int>>();
    });
}

} // namespace vt
