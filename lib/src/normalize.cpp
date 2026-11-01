#include "vt/normalize.h"

#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace vt {
namespace detail {

namespace {

std::mutex& normalizerMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::type_index, NormalizerFn>& normalizers()
{
    static std::unordered_map<std::type_index, NormalizerFn> table;
    return table;
}

} // namespace

void registerNormalizer(std::type_index type, NormalizerFn fn)
{
    if (!fn)
        return;
    std::lock_guard<std::mutex> lock(normalizerMutex());
    normalizers().emplace(type, fn);   // 先登记者优先，不覆盖已有实现
}

NormalizerFn findNormalizer(std::type_index type)
{
    std::lock_guard<std::mutex> lock(normalizerMutex());
    const auto it = normalizers().find(type);
    return it == normalizers().end() ? nullptr : it->second;
}

void clearNormalizers()
{
    std::lock_guard<std::mutex> lock(normalizerMutex());
    normalizers().clear();
}

} // namespace detail
} // namespace vt
