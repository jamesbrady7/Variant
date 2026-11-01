#include "vt/variant_map.h"

#include "vt/variant_list.h"

#include <utility>

namespace vt {

VariantMap::VariantMap(std::initializer_list<std::pair<std::string, Variant>> init)
{
    m_data.reserve(init.size());
    for (const auto& kv : init)
        m_data.emplace(kv.first, kv.second);
}

VariantMap::VariantMap(Storage data)
    : m_data(std::move(data))
{
}

// ---------------------------------------------------------------- 字面键

Variant& VariantMap::operator[](std::string_view key)
{
    return m_data[std::string(key)];
}

Variant VariantMap::operator[](std::string_view key) const
{
    if (const Variant* p = find(key))
        return *p;
    return Variant();
}

const Variant* VariantMap::find(std::string_view key) const noexcept
{
    return find(std::string(key));
}

const Variant* VariantMap::find(const std::string& key) const noexcept
{
    const auto it = m_data.find(key);
    return it == m_data.end() ? nullptr : &it->second;
}

const Variant* VariantMap::find(const char* key) const noexcept
{
    return find(std::string_view(key ? key : ""));
}

Variant* VariantMap::find(std::string_view key) noexcept
{
    return const_cast<Variant*>(static_cast<const VariantMap&>(*this).find(key));
}

Variant* VariantMap::find(const std::string& key) noexcept
{
    return const_cast<Variant*>(static_cast<const VariantMap&>(*this).find(key));
}

Variant* VariantMap::find(const char* key) noexcept
{
    return const_cast<Variant*>(static_cast<const VariantMap&>(*this).find(key));
}

const Variant& VariantMap::at(std::string_view key) const
{
    const Variant* found = find(key);
    if (!found) {
        throw VariantError(VariantErrorCode::KeyNotFound,
                           "key \"" + std::string(key) + "\" not found");
    }
    return *found;
}

Variant& VariantMap::at(std::string_view key)
{
    return const_cast<Variant&>(static_cast<const VariantMap&>(*this).at(key));
}

bool VariantMap::contains(std::string_view key) const noexcept
{
    return find(key) != nullptr;
}

bool VariantMap::erase(std::string_view key)
{
    return m_data.erase(std::string(key)) != 0;
}

bool VariantMap::set(std::string_view key, Variant value) noexcept
{
    try {
        (*this)[key] = std::move(value);
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------- 路径

Variant& VariantMap::operator[](const Path& path)
{
    return resolveCreate(path);
}

Variant VariantMap::operator[](const Path& path) const
{
    if (const Variant* p = resolveFind(path))
        return *p;
    return Variant();
}

const Variant* VariantMap::find(const Path& path) const noexcept
{
    return resolveFind(path);
}

Variant* VariantMap::find(const Path& path) noexcept
{
    return const_cast<Variant*>(resolveFind(path));
}

bool VariantMap::contains(const Path& path) const noexcept
{
    return find(path) != nullptr;
}

bool VariantMap::set(const Path& path, Variant value) noexcept
{
    try {
        resolveCreate(path) = std::move(value);
        return true;
    } catch (...) {
        return false;
    }
}

Variant& VariantMap::resolveCreate(const Path& path)
{
    if (path.empty()) {
        throw VariantError(VariantErrorCode::EmptyPath,
                           "empty path cannot address a map entry");
    }
    const auto& first = path.segments().front();
    if (first.isIndex) {
        throw VariantError(VariantErrorCode::NotAMap,
                           "segment 0: map root cannot be indexed",
                           path.source(), 0, first.offset);
    }
    Variant& value = (*this)[first.key];
    if (path.size() == 1)
        return value;
    return value.create(path.slice(1));
}

const Variant* VariantMap::resolveFind(const Path& path) const noexcept
{
    if (path.empty())
        return nullptr;
    const auto& first = path.segments().front();
    if (first.isIndex)
        return nullptr;
    const Variant* value = find(first.key);
    if (!value)
        return nullptr;
    if (path.size() == 1)
        return value;
    return value->find(path.slice(1));
}

const Variant& VariantMap::at(const Path& path) const
{
    if (path.empty()) {
        throw VariantError(VariantErrorCode::EmptyPath,
                           "empty path cannot address a map entry");
    }
    const auto& first = path.segments().front();
    if (first.isIndex) {
        throw VariantError(VariantErrorCode::NotAMap,
                           "segment 0: map root cannot be indexed",
                           path.source(), 0, first.offset);
    }
    const Variant* value = find(first.key);
    if (!value) {
        throw VariantError(VariantErrorCode::KeyNotFound,
                           "segment 0: key \"" + first.key + "\" not found",
                           path.source(), 0, first.offset);
    }
    if (path.size() == 1)
        return *value;
    return value->at(path.slice(1));
}

Variant& VariantMap::at(const Path& path)
{
    return const_cast<Variant&>(static_cast<const VariantMap&>(*this).at(path));
}

bool VariantMap::operator==(const VariantMap& other) const
{
    return m_data == other.m_data;
}

} // namespace vt
