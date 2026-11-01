#include "vt/debug.h"

#include "vt/variant_list.h"
#include "vt/variant_map.h"

#include <iomanip>
#include <sstream>

namespace vt {

namespace {

void appendEscaped(std::string& out, std::string_view text)
{
    out.push_back('"');
    for (char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out.push_back(c); break;
        }
    }
    out.push_back('"');
}

void appendVariant(std::string& out, const Variant& value)
{
    switch (value.type()) {
    case Variant::Type::Null:
        out += "null";
        break;
    case Variant::Type::Bool:
        out += value.asBool() ? "true" : "false";
        break;
    case Variant::Type::Int:
        out += std::to_string(value.asInt());
        break;
    case Variant::Type::Double: {
        std::ostringstream oss;
        oss << std::setprecision(17) << value.asDouble();
        out += oss.str();
        break;
    }
    case Variant::Type::String:
        appendEscaped(out, value.asString());
        break;
    case Variant::Type::Map: {
        out.push_back('{');
        bool first = true;
        for (const auto& kv : value.asMap()) {
            if (!first)
                out += ", ";
            first = false;
            appendEscaped(out, kv.first);
            out += ": ";
            appendVariant(out, kv.second);
        }
        out.push_back('}');
        break;
    }
    case Variant::Type::List: {
        out.push_back('[');
        bool first = true;
        for (const auto& item : value.asList()) {
            if (!first)
                out += ", ";
            first = false;
            appendVariant(out, item);
        }
        out.push_back(']');
        break;
    }
    case Variant::Type::Extension:
        out += "<";
        out += value.typeName();
        out += ">";
        break;
    }
}

} // namespace

std::string describe(const Variant& value)
{
    std::string out;
    appendVariant(out, value);
    return out;
}

std::string describe(const VariantMap& map)
{
    return describe(Variant(map));
}

std::string describe(const VariantList& list)
{
    return describe(Variant(list));
}

} // namespace vt
