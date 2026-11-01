#include "vt/serialize_error.h"

#include <utility>

namespace vt {

const char* toString(SerializeErrorCode code) noexcept
{
    switch (code) {
    case SerializeErrorCode::None:             return "None";
    case SerializeErrorCode::Syntax:           return "Syntax";
    case SerializeErrorCode::UnexpectedEnd:    return "UnexpectedEnd";
    case SerializeErrorCode::InvalidUtf8:      return "InvalidUtf8";
    case SerializeErrorCode::InvalidNumber:    return "InvalidNumber";
    case SerializeErrorCode::DepthExceeded:    return "DepthExceeded";
    case SerializeErrorCode::UnknownType:      return "UnknownType";
    case SerializeErrorCode::UnsupportedValue: return "UnsupportedValue";
    case SerializeErrorCode::BadLength:        return "BadLength";
    case SerializeErrorCode::VersionMismatch:  return "VersionMismatch";
    case SerializeErrorCode::TrailingData:     return "TrailingData";
    case SerializeErrorCode::EncodeFailed:     return "EncodeFailed";
    case SerializeErrorCode::IoError:          return "IoError";
    }
    return "Unknown";
}

std::string SerializeError::toString() const
{
    std::string out = "[";
    out += vt::toString(code);
    out += "] ";
    out += message;
    if (offset != 0 || code != SerializeErrorCode::None) {
        out += " (offset=";
        out += std::to_string(offset);
        if (line > 0) {
            out += ", line=";
            out += std::to_string(line);
            out += ", column=";
            out += std::to_string(column);
        }
        out += ")";
    }
    return out;
}

SerializeException::SerializeException(SerializeError info)
    : std::runtime_error(info.toString())
    , m_info(std::move(info))
{
}

} // namespace vt
