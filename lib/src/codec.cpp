#include "vt/codec.h"

#include "vt/type_registry.h"

#include <utility>

namespace vt {

CodecRegistry& CodecRegistry::instance()
{
    static CodecRegistry registry;
    return registry;
}

bool CodecRegistry::registerCodec(std::type_index type,
                                  std::string name,
                                  std::function<Variant(const std::any&)> encode,
                                  std::function<bool(const Variant&, std::any&)> decode,
                                  bool plain)
{
    if (!encode || !decode)
        return false;

    // 名字尽量同步到核心名字表（describe/日志）。
    // 若该类型已经有一个不同的名字（例如先注册给了 Python 桥），不让它阻断 codec：
    // codec 名是"序列化协议名"，可以和展示名不同。
    TypeRegistry::instance().registerType(type, name);

    std::lock_guard<std::mutex> lock(m_mutex);

    const auto byType = m_byType.find(type);
    if (byType != m_byType.end())
        return byType->second.name == name;

    if (m_byName.find(name) != m_byName.end())
        return false;

    Entry entry{type, std::move(name), std::move(encode), std::move(decode), plain};
    m_byName.emplace(entry.name, type);
    m_byType.emplace(type, std::move(entry));
    return true;
}

const CodecRegistry::Entry* CodecRegistry::find(std::type_index type) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_byType.find(type);
    return it == m_byType.end() ? nullptr : &it->second;
}

const CodecRegistry::Entry* CodecRegistry::findByName(std::string_view name) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_byName.find(std::string(name));
    if (it == m_byName.end())
        return nullptr;
    const auto byType = m_byType.find(it->second);
    return byType == m_byType.end() ? nullptr : &byType->second;
}

bool CodecRegistry::isRegistered(std::type_index type) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_byType.find(type) != m_byType.end();
}

std::size_t CodecRegistry::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_byType.size();
}

void CodecRegistry::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_byType.clear();
    m_byName.clear();
}

} // namespace vt
