#include "vt/document.h"

#include <utility>

vt::Variant VariantDocument::get(const std::string& path) const
{
    vt::Path parsed;
    if (!vt::Path::tryParse(path, parsed))
        return vt::Variant();
    return get(parsed);
}

vt::Variant VariantDocument::get(const vt::Path& path) const
{
    if (const vt::Variant* found = m_root.find(path))
        return *found;
    return vt::Variant();
}

bool VariantDocument::set(const std::string& path, vt::Variant value)
{
    vt::Path parsed;
    if (!vt::Path::tryParse(path, parsed, &m_lastError))
        return false;
    return set(parsed, std::move(value));
}

bool VariantDocument::set(const vt::Path& path, vt::Variant value)
{
    try {
        m_root.create(path) = std::move(value);
        m_lastError.clear();
        return true;
    } catch (const vt::VariantError& e) {
        m_lastError = e.what();
        return false;
    } catch (const std::exception& e) {
        m_lastError = e.what();
        return false;
    } catch (...) {
        m_lastError = "unknown error";
        return false;
    }
}

bool VariantDocument::has(const std::string& path) const noexcept
{
    vt::Path parsed;
    if (!vt::Path::tryParse(path, parsed))
        return false;
    return has(parsed);
}

bool VariantDocument::has(const vt::Path& path) const noexcept
{
    return m_root.find(path) != nullptr;
}
