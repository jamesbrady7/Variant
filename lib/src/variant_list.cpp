#include "vt/variant_list.h"

#include <string>
#include <utility>

namespace vt {

Variant& VariantList::at(std::size_t index)
{
    if (index >= m_data.size()) {
        throw VariantError(VariantErrorCode::IndexOutOfBounds,
                           "index " + std::to_string(index) + " out of range [0," +
                               std::to_string(m_data.size()) + ")");
    }
    return m_data[index];
}

const Variant& VariantList::at(std::size_t index) const
{
    if (index >= m_data.size()) {
        throw VariantError(VariantErrorCode::IndexOutOfBounds,
                           "index " + std::to_string(index) + " out of range [0," +
                               std::to_string(m_data.size()) + ")");
    }
    return m_data[index];
}

// ---------------------------------------------------------------- 路径

Variant& VariantList::operator[](const Path& path)
{
    return resolveCreate(path);
}

Variant VariantList::operator[](const Path& path) const
{
    if (const Variant* p = resolveFind(path))
        return *p;
    return Variant();
}

const Variant* VariantList::find(const Path& path) const noexcept
{
    return resolveFind(path);
}

bool VariantList::contains(const Path& path) const noexcept
{
    return find(path) != nullptr;
}

Variant& VariantList::at(const Path& path)
{
    return const_cast<Variant&>(static_cast<const VariantList&>(*this).at(path));
}

const Variant& VariantList::at(const Path& path) const
{
    if (path.empty()) {
        throw VariantError(VariantErrorCode::EmptyPath,
                           "empty path cannot address a list entry");
    }
    const auto& first = path.segments().front();
    if (!first.isIndex) {
        throw VariantError(VariantErrorCode::NotAList,
                           "segment 0: list root requires an index",
                           path.source(), 0, first.offset);
    }
    if (first.index >= m_data.size()) {
        throw VariantError(VariantErrorCode::IndexOutOfBounds,
                           "segment 0: index " + std::to_string(first.index) +
                               " out of range [0," + std::to_string(m_data.size()) + ")",
                           path.source(), 0, first.offset);
    }
    if (path.size() == 1)
        return m_data[first.index];
    return m_data[first.index].at(path.slice(1));
}

bool VariantList::set(const Path& path, Variant value) noexcept
{
    try {
        resolveCreate(path) = std::move(value);
        return true;
    } catch (...) {
        return false;
    }
}

Variant& VariantList::resolveCreate(const Path& path)
{
    if (path.empty()) {
        throw VariantError(VariantErrorCode::EmptyPath,
                           "empty path cannot address a list entry");
    }
    const auto& first = path.segments().front();
    if (!first.isIndex) {
        throw VariantError(VariantErrorCode::NotAList,
                           "segment 0: list root requires an index",
                           path.source(), 0, first.offset);
    }

    Variant* value = nullptr;
    if (first.index < m_data.size()) {
        value = &m_data[first.index];
    } else if (first.index == m_data.size()) {
        m_data.emplace_back();
        value = &m_data.back();
    } else {
        throw VariantError(VariantErrorCode::IndexOutOfBounds,
                           "segment 0: index " + std::to_string(first.index) +
                               " out of range [0," + std::to_string(m_data.size()) + "]",
                           path.source(), 0, first.offset);
    }

    if (path.size() == 1)
        return *value;
    return value->create(path.slice(1));
}

const Variant* VariantList::resolveFind(const Path& path) const noexcept
{
    if (path.empty())
        return nullptr;
    const auto& segments = path.segments();
    if (!segments.front().isIndex)
        return nullptr;
    if (segments.front().index >= m_data.size())
        return nullptr;

    // 逐段前进，不做 Path::slice（切片会复制剩余段，而每段都是分配）。
    const Variant* cur = &m_data[segments.front().index];
    for (std::size_t i = 1; i < segments.size(); ++i) {
        const Path::Segment& seg = segments[i];
        if (seg.isIndex) {
            if (!cur->isList())
                return nullptr;
            const VariantList& list = cur->asList();
            if (seg.index >= list.size())
                return nullptr;
            cur = &list[seg.index];
        } else {
            cur = cur->find(seg.key);
        }
    }
    return cur;
}

} // namespace vt
