#include "vt/type_registry.h"

#include <utility>

namespace vt {

TypeRegistry& TypeRegistry::instance()
{
    static TypeRegistry registry;
    return registry;
}

bool TypeRegistry::registerType(std::type_index type, std::string name)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto byType = m_byType.find(type);
    if (byType != m_byType.end())
        return byType->second.name == name;   // 同类型重复注册：名字一致才算成功

    if (m_byName.find(name) != m_byName.end())
        return false;                          // 名字被别的类型占用

    Entry entry{type, std::move(name)};
    m_byName.emplace(entry.name, type);
    m_byType.emplace(type, std::move(entry));
    return true;
}

const TypeRegistry::Entry* TypeRegistry::find(std::type_index type) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_byType.find(type);
    return it == m_byType.end() ? nullptr : &it->second;
}

const TypeRegistry::Entry* TypeRegistry::findByName(std::string_view name) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_byName.find(std::string(name));
    if (it == m_byName.end())
        return nullptr;
    const auto byType = m_byType.find(it->second);
    return byType == m_byType.end() ? nullptr : &byType->second;
}

bool TypeRegistry::isRegistered(std::type_index type) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_byType.find(type) != m_byType.end();
}

std::vector<TypeRegistry::Entry> TypeRegistry::entries() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Entry> out;
    out.reserve(m_byType.size());
    for (const auto& kv : m_byType)
        out.push_back(kv.second);
    return out;
}

void TypeRegistry::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_byType.clear();
    m_byName.clear();
}

std::size_t TypeRegistry::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_byType.size();
}

} // namespace vt
