#include "vt/path.h"

#include <cctype>
#include <limits>
#include <utility>

namespace vt {

namespace {

bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t';
}

bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

bool needsQuoting(std::string_view key) noexcept
{
    if (key.empty())
        return true;
    for (char c : key) {
        if (c == '.' || c == '[' || c == ']' || c == '"' || c == '\\' || isSpace(c))
            return true;
    }
    return false;
}

void appendQuoted(std::string& out, std::string_view key)
{
    out.push_back('"');
    for (char c : key) {
        if (c == '"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
}

} // namespace

Path::Path(std::string_view text)
{
    *this = parse(text);
}

Path Path::parse(std::string_view text)
{
    Path path;
    path.m_source.assign(text);
    if (text.empty())
        return path;

    std::size_t i = 0;
    const std::size_t n = text.size();

    auto fail = [&](const char* what, std::size_t offset) {
        throw VariantError(VariantErrorCode::SyntaxError,
                           std::string(what) + " at offset " + std::to_string(offset),
                           path.m_source, path.m_segments.size(), offset);
    };

    while (i < n) {
        Segment seg;
        seg.offset = i;

        if (text[i] == '[') {
            const std::size_t open = i;
            ++i;
            while (i < n && isSpace(text[i]))
                ++i;

            if (i < n && text[i] == '"') {
                // ["quoted.key"]
                ++i;
                std::string key;
                bool closed = false;
                while (i < n) {
                    const char c = text[i];
                    if (c == '\\') {
                        if (i + 1 >= n)
                            fail("dangling escape", i);
                        key.push_back(text[i + 1]);
                        i += 2;
                    } else if (c == '"') {
                        ++i;
                        closed = true;
                        break;
                    } else {
                        key.push_back(c);
                        ++i;
                    }
                }
                if (!closed)
                    fail("unterminated quoted key", open);
                while (i < n && isSpace(text[i]))
                    ++i;
                if (i >= n || text[i] != ']')
                    fail("expected ']' after quoted key", i);
                ++i;
                seg.isIndex = false;
                seg.key = std::move(key);
            } else {
                // [123]
                std::size_t value = 0;
                bool any = false;
                while (i < n && isDigit(text[i])) {
                    const unsigned digit = static_cast<unsigned>(text[i] - '0');
                    if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10)
                        fail("index overflow", i);
                    value = value * 10 + digit;
                    any = true;
                    ++i;
                }
                if (!any)
                    fail("expected index or quoted key after '['", i);
                while (i < n && isSpace(text[i]))
                    ++i;
                if (i >= n || text[i] != ']')
                    fail("expected ']' after index", i);
                ++i;
                seg.isIndex = true;
                seg.index = value;
            }
        } else {
            // plain key
            const std::size_t start = i;
            while (i < n && text[i] != '.' && text[i] != '[' && text[i] != ']')
                ++i;
            if (i == start)
                fail("empty key", i);
            seg.key.assign(text.substr(start, i - start));
        }

        path.m_segments.push_back(std::move(seg));

        if (i < n) {
            if (text[i] == '.') {
                ++i;
                if (i >= n)
                    fail("trailing '.'", i);
                if (text[i] == '.' || text[i] == '[' || text[i] == ']')
                    fail("expected key after '.'", i);
            } else if (text[i] == '[') {
                // 下一段是下标，继续
            } else {
                fail("unexpected character", i);
            }
        }
    }
    return path;
}

bool Path::tryParse(std::string_view text, Path& out, std::string* error) noexcept
{
    try {
        out = parse(text);
        return true;
    } catch (const std::exception& e) {
        if (error)
            *error = e.what();
        return false;
    } catch (...) {
        if (error)
            *error = "unknown parse failure";
        return false;
    }
}

Path Path::slice(std::size_t from) const
{
    Path out;
    out.m_source = m_source;
    if (from < m_segments.size())
        out.m_segments.assign(m_segments.begin() + static_cast<std::ptrdiff_t>(from), m_segments.end());
    return out;
}

std::string Path::toString() const
{
    std::string out;
    for (std::size_t i = 0; i < m_segments.size(); ++i) {
        const Segment& seg = m_segments[i];
        if (seg.isIndex) {
            out.push_back('[');
            out += std::to_string(seg.index);
            out.push_back(']');
        } else {
            if (needsQuoting(seg.key)) {
                // 带方括号的键自带分隔：a["x.y"].z / a[0]["x.y"]
                out.push_back('[');
                appendQuoted(out, seg.key);
                out.push_back(']');
            } else {
                // 普通键之前必须有分隔（a.b / a[0].b）
                if (i > 0)
                    out.push_back('.');
                out += seg.key;
            }
        }
    }
    return out;
}

} // namespace vt
