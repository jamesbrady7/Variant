#include "vt/json.h"

#include "utf8.h"
#include "vt/adapters.h"
#include "vt/codec.h"
#include "vt/variant_list.h"
#include "vt/variant_map.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
#include <iterator>
#include <ostream>
#include <utility>
#include <vector>

namespace vt::json {

namespace {


bool ioFail(SerializeError& error, std::string message)
{
    error.code = SerializeErrorCode::IoError;
    error.message = std::move(message);
    error.offset = 0;
    error.line = 0;
    error.column = 0;
    return false;
}

bool readStreamAll(std::istream& input, std::string& out, SerializeError& error)
{
    if (!input)
        return ioFail(error, "input stream is not readable");
    out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad())
        return ioFail(error, "stream read error");
    return true;
}

bool writeStreamAll(std::ostream& output, std::string_view data, SerializeError& error)
{
    if (!output.good())
        return ioFail(error, "output stream is not writable");
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!output)
        return ioFail(error, "stream write error");
    return true;
}

// 路径统一按 UTF-8 解释：Windows 上 u8path 会转成宽字符，
// 否则 MSVC 的窄字符 fstream 按 ANSI 代码页打开，中文路径直接失败。
bool utf8Path(const std::string& path, std::filesystem::path& out)
{
    try {
        out = std::filesystem::u8path(path);
        return true;
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool readFileAll(const std::string& path, std::string& out, SerializeError& error)
{
    if (path.empty())
        return ioFail(error, "empty file path");
    std::filesystem::path fsPath;
    if (!utf8Path(path, fsPath))
        return ioFail(error, "invalid UTF-8 file path '" + path + "'");
    std::ifstream file(fsPath, std::ios::binary);
    if (!file)
        return ioFail(error, "cannot open '" + path + "' for reading");
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (file.bad())
        return ioFail(error, "error while reading '" + path + "'");
    return true;
}

bool writeFileAll(const std::string& path, std::string_view data, SerializeError& error)
{
    if (path.empty())
        return ioFail(error, "empty file path");
    std::filesystem::path fsPath;
    if (!utf8Path(path, fsPath))
        return ioFail(error, "invalid UTF-8 file path '" + path + "'");
    std::ofstream file(fsPath, std::ios::binary | std::ios::trunc);
    if (!file)
        return ioFail(error, "cannot open '" + path + "' for writing");
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    file.flush();
    if (!file)
        return ioFail(error, "error while writing '" + path + "'");
    file.close();
    if (file.fail())
        return ioFail(error, "error while closing '" + path + "'");
    return true;
}

bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

// ---------------------------------------------------------------- Writer

struct JsonWriter {
    std::string out;
    const JsonWriteOptions& options;
    SerializeError error;

    explicit JsonWriter(const JsonWriteOptions& opt) : options(opt) {}

    bool fail(SerializeErrorCode code, std::string message)
    {
        error.code = code;
        error.message = std::move(message);
        return false;
    }

    void newlineIndent(int depth)
    {
        if (!options.pretty)
            return;
        out.push_back('\n');
        out.append(static_cast<std::size_t>(depth) * 2, ' ');
    }

    bool writeValue(const Variant& value, int depth)
    {
        if (depth > options.maxDepth)
            return fail(SerializeErrorCode::DepthExceeded, "maximum nesting depth exceeded");

        switch (value.type()) {
        case Variant::Type::Null:
            out += "null";
            return true;
        case Variant::Type::Bool:
            out += value.asBool() ? "true" : "false";
            return true;
        case Variant::Type::Int: {
            char buffer[32];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.asInt());
            if (result.ec != std::errc())
                return fail(SerializeErrorCode::InvalidNumber, "integer formatting failed");
            out.append(buffer, result.ptr);
            return true;
        }
        case Variant::Type::Double:
            return writeDouble(value.asDouble());
        case Variant::Type::String:
            return writeString(value.asStringView());
        case Variant::Type::List:
            return writeList(value.asList(), depth);
        case Variant::Type::Map:
            return writeMap(value.asMap(), depth);
        case Variant::Type::Extension:
            return writeExtension(value, depth);
        }
        return fail(SerializeErrorCode::UnsupportedValue, "unknown variant type");
    }

    bool writeDouble(double value)
    {
        if (!std::isfinite(value)) {
            if (options.nonFinite == NonFinitePolicy::Null) {
                out += "null";
                return true;
            }
            return fail(SerializeErrorCode::UnsupportedValue,
                        "NaN/Infinity cannot be represented in JSON");
        }

        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                          std::chars_format::general);
        if (result.ec != std::errc())
            return fail(SerializeErrorCode::InvalidNumber, "double formatting failed");

        const char* begin = buffer;
        const char* end = result.ptr;
        out.append(begin, end);
        // 保证读回时仍是 Double：整值补 ".0"
        bool hasMarker = false;
        for (const char* p = begin; p < end; ++p) {
            if (*p == '.' || *p == 'e' || *p == 'E') {
                hasMarker = true;
                break;
            }
        }
        if (!hasMarker)
            out += ".0";
        return true;
    }

    bool writeString(std::string_view text)
    {
        if (!detail::isValidUtf8(text))
            return fail(SerializeErrorCode::InvalidUtf8, "string is not valid UTF-8");

        out.push_back('"');
        for (char c : text) {
            const auto u = static_cast<unsigned char>(c);
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (u < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(u >> 4) & 0xF]);
                    out.push_back(hex[u & 0xF]);
                } else {
                    out.push_back(c);
                }
                break;
            }
        }
        out.push_back('"');
        return true;
    }

    bool writeList(const VariantList& list, int depth)
    {
        out.push_back('[');
        bool first = true;
        for (const Variant& item : list) {
            if (!first)
                out.push_back(',');
            newlineIndent(depth + 1);
            first = false;
            if (!writeValue(item, depth + 1))
                return false;
        }
        if (!first)
            newlineIndent(depth);
        out.push_back(']');
        return true;
    }

    bool writeMap(const VariantMap& map, int depth)
    {
        out.push_back('{');

        if (options.sortKeys) {
            std::vector<const VariantMap::value_type*> items;
            items.reserve(map.size());
            for (const auto& kv : map)
                items.push_back(&kv);
            std::sort(items.begin(), items.end(),
                      [](const VariantMap::value_type* a, const VariantMap::value_type* b) {
                          return a->first < b->first;
                      });

            bool first = true;
            for (const auto* kv : items) {
                if (!first)
                    out.push_back(',');
                newlineIndent(depth + 1);
                first = false;
                if (!writeString(kv->first))
                    return false;
                out += options.pretty ? ": " : ":";
                if (!writeValue(kv->second, depth + 1))
                    return false;
            }
            if (!first)
                newlineIndent(depth);
        } else {
            bool first = true;
            for (const auto& kv : map) {
                if (!first)
                    out.push_back(',');
                newlineIndent(depth + 1);
                first = false;
                if (!writeString(kv.first))
                    return false;
                out += options.pretty ? ": " : ":";
                if (!writeValue(kv.second, depth + 1))
                    return false;
            }
            if (!first)
                newlineIndent(depth);
        }

        out.push_back('}');
        return true;
    }

    bool writeExtension(const Variant& value, int depth)
    {
        const std::any* any = value.extensionAny();
        if (!any)
            return fail(SerializeErrorCode::EncodeFailed, "extension slot is empty");

        const CodecRegistry::Entry* codec = CodecRegistry::instance().find(any->type());
        if (!codec) {
            return fail(SerializeErrorCode::UnknownType,
                        "no codec registered for '" + value.typeName() + "'");
        }

        if (options.extensions == ExtensionPolicy::Reject && !codec->plain) {
            return fail(SerializeErrorCode::UnsupportedValue,
                        "extension value '" + codec->name + "' is not allowed in plain JSON mode");
        }

        Variant plain;
        try {
            plain = codec->encode(*any);
        } catch (const std::exception& e) {
            return fail(SerializeErrorCode::EncodeFailed, e.what());
        }

        // 策略优先级：Flatten > Reject（plain codec）> Auto（plain codec）> 信封
        if (options.extensions == ExtensionPolicy::Flatten
            || options.extensions == ExtensionPolicy::Reject
            || (options.extensions == ExtensionPolicy::Auto && codec->plain)) {
            return writeValue(plain, depth);
        }

        VariantMap envelope;
        envelope["$vt"] = std::string("1");
        envelope["$type"] = codec->name;
        envelope["$data"] = plain;
        return writeMap(envelope, depth);
    }
};

// ---------------------------------------------------------------- Parser

struct JsonParser {
    std::string_view text;
    const char* p;
    const char* end;
    const JsonReadOptions& options;
    SerializeError error;

    JsonParser(std::string_view input, const JsonReadOptions& opt)
        : text(input)
        , p(input.data())
        , end(input.data() + input.size())
        , options(opt)
    {
    }

    bool fail(SerializeErrorCode code, std::string message, const char* at = nullptr)
    {
        error.code = code;
        error.message = std::move(message);
        const char* pos = at ? at : p;
        error.offset = static_cast<std::size_t>(pos - text.data());
        computeLineColumn(error.offset);
        return false;
    }

    void computeLineColumn(std::size_t offset)
    {
        error.line = 1;
        error.column = 1;
        const std::size_t limit = std::min(offset, text.size());
        for (std::size_t i = 0; i < limit; ++i) {
            if (text[i] == '\n') {
                ++error.line;
                error.column = 1;
            } else {
                ++error.column;
            }
        }
    }

    void skipWs()
    {
        while (p < end && isSpace(*p))
            ++p;
    }

    bool consume(char c)
    {
        if (p < end && *p == c) {
            ++p;
            return true;
        }
        return false;
    }

    bool expect(char c, const char* what)
    {
        if (p >= end)
            return fail(SerializeErrorCode::UnexpectedEnd, "unexpected end of input");
        if (*p != c)
            return fail(SerializeErrorCode::Syntax, std::string("expected ") + what);
        ++p;
        return true;
    }

    bool parseLiteral(const char* literal, std::size_t length)
    {
        if (static_cast<std::size_t>(end - p) < length)
            return fail(SerializeErrorCode::UnexpectedEnd, "unexpected end of input");
        if (std::memcmp(p, literal, length) != 0)
            return fail(SerializeErrorCode::Syntax, "invalid literal");
        p += length;
        return true;
    }

    bool parseValue(Variant& out, int depth)
    {
        if (depth > options.maxDepth)
            return fail(SerializeErrorCode::DepthExceeded, "maximum nesting depth exceeded");
        if (p >= end)
            return fail(SerializeErrorCode::UnexpectedEnd, "unexpected end of input");

        switch (*p) {
        case '{':
            return parseObject(out, depth);
        case '[':
            return parseArray(out, depth);
        case '"': {
            std::string value;
            if (!parseString(value))
                return false;
            out = Variant(std::move(value));
            return true;
        }
        case 't':
            if (!parseLiteral("true", 4))
                return false;
            out = Variant(true);
            return true;
        case 'f':
            if (!parseLiteral("false", 5))
                return false;
            out = Variant(false);
            return true;
        case 'n':
            if (!parseLiteral("null", 4))
                return false;
            out = Variant();
            return true;
        default:
            if (*p == '-' || isDigit(*p))
                return parseNumber(out);
            return fail(SerializeErrorCode::Syntax, "unexpected character");
        }
    }

    bool parseNumber(Variant& out)
    {
        const char* start = p;
        if (p < end && *p == '-')
            ++p;

        if (p >= end)
            return fail(SerializeErrorCode::UnexpectedEnd, "unexpected end of number");
        if (*p == '0') {
            ++p;
            if (p < end && isDigit(*p))
                return fail(SerializeErrorCode::InvalidNumber, "leading zero is not allowed", start);
        } else if (*p >= '1' && *p <= '9') {
            while (p < end && isDigit(*p))
                ++p;
        } else {
            return fail(SerializeErrorCode::InvalidNumber, "invalid number", start);
        }

        bool isDouble = false;
        if (p < end && *p == '.') {
            isDouble = true;
            ++p;
            if (p >= end || !isDigit(*p))
                return fail(SerializeErrorCode::InvalidNumber, "expected digit after '.'", start);
            while (p < end && isDigit(*p))
                ++p;
        }

        if (p < end && (*p == 'e' || *p == 'E')) {
            isDouble = true;
            ++p;
            if (p < end && (*p == '+' || *p == '-'))
                ++p;
            if (p >= end || !isDigit(*p))
                return fail(SerializeErrorCode::InvalidNumber, "expected exponent digits", start);
            while (p < end && isDigit(*p))
                ++p;
        }

        const char* tokenEnd = p;
        if (!isDouble) {
            std::int64_t integer = 0;
            const auto result = std::from_chars(start, tokenEnd, integer);
            if (result.ec == std::errc() && result.ptr == tokenEnd) {
                out = Variant(integer);
                return true;
            }
            if (result.ec != std::errc::result_out_of_range)
                return fail(SerializeErrorCode::InvalidNumber, "invalid integer", start);
            // 超出 int64：退化到 double
        }

        double value = 0.0;
        const auto result = std::from_chars(start, tokenEnd, value);
        if (result.ec != std::errc() || result.ptr != tokenEnd || !std::isfinite(value))
            return fail(SerializeErrorCode::InvalidNumber, "invalid number", start);

        out = Variant(value);
        return true;
    }

    bool parseString(std::string& out)
    {
        if (!consume('"'))
            return fail(SerializeErrorCode::Syntax, "expected string");

        out.clear();
        while (true) {
            if (p >= end)
                return fail(SerializeErrorCode::UnexpectedEnd, "unterminated string");

            const auto c = static_cast<unsigned char>(*p);
            if (c == '"') {
                ++p;
                return true;
            }
            if (c < 0x20)
                return fail(SerializeErrorCode::Syntax, "control character in string");
            if (c == '\\') {
                ++p;
                if (p >= end)
                    return fail(SerializeErrorCode::UnexpectedEnd, "unterminated escape");
                const char escape = *p++;
                switch (escape) {
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!parseHex4(cp))
                        return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // 高代理：必须跟一个低代理
                        if (end - p < 2 || p[0] != '\\' || p[1] != 'u')
                            return fail(SerializeErrorCode::InvalidUtf8, "lone high surrogate");
                        p += 2;
                        std::uint32_t low = 0;
                        if (!parseHex4(low))
                            return false;
                        if (low < 0xDC00 || low > 0xDFFF)
                            return fail(SerializeErrorCode::InvalidUtf8, "invalid low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail(SerializeErrorCode::InvalidUtf8, "lone low surrogate");
                    }
                    detail::appendUtf8(out, cp);
                    break;
                }
                default:
                    return fail(SerializeErrorCode::Syntax, "invalid escape sequence");
                }
            } else if (c < 0x80) {
                out.push_back(static_cast<char>(c));
                ++p;
            } else {
                const char* sequence = p;
                std::uint32_t cp = 0;
                if (!detail::decodeUtf8(p, end, cp))
                    return fail(SerializeErrorCode::InvalidUtf8, "invalid UTF-8 sequence", sequence);
                out.append(sequence, static_cast<std::size_t>(p - sequence));
            }
        }
    }

    bool parseHex4(std::uint32_t& value)
    {
        if (end - p < 4)
            return fail(SerializeErrorCode::UnexpectedEnd, "incomplete \\u escape");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = *p++;
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else
                return fail(SerializeErrorCode::Syntax, "invalid hex digit");
        }
        return true;
    }

    bool parseArray(Variant& out, int depth)
    {
        ++p;   // '['
        VariantList list;
        skipWs();
        if (p < end && *p == ']') {
            ++p;
            out = Variant(std::move(list));
            return true;
        }

        while (true) {
            skipWs();
            if (p >= end)
                return fail(SerializeErrorCode::UnexpectedEnd, "unexpected end of array");
            if (*p == ']')
                return fail(SerializeErrorCode::Syntax, "trailing comma in array");

            Variant item;
            if (!parseValue(item, depth + 1))
                return false;
            list.push_back(std::move(item));

            skipWs();
            if (p >= end)
                return fail(SerializeErrorCode::UnexpectedEnd, "unexpected end of array");
            if (*p == ',') {
                ++p;
                continue;
            }
            if (*p == ']') {
                ++p;
                out = Variant(std::move(list));
                return true;
            }
            return fail(SerializeErrorCode::Syntax, "expected ',' or ']'");
        }
    }

    bool parseObject(Variant& out, int depth)
    {
        ++p;   // '{'
        VariantMap map;
        skipWs();
        if (p < end && *p == '}') {
            ++p;
            out = Variant(std::move(map));
            return true;
        }

        while (true) {
            skipWs();
            if (p >= end)
                return fail(SerializeErrorCode::UnexpectedEnd, "unexpected end of object");
            if (*p != '"')
                return fail(SerializeErrorCode::Syntax, "object key must be a string");

            std::string key;
            if (!parseString(key))
                return false;

            skipWs();
            if (!expect(':', "':'"))
                return false;
            skipWs();

            Variant value;
            if (!parseValue(value, depth + 1))
                return false;
            map[std::move(key)] = std::move(value);

            skipWs();
            if (p >= end)
                return fail(SerializeErrorCode::UnexpectedEnd, "unexpected end of object");
            if (*p == ',') {
                ++p;
                continue;
            }
            if (*p == '}') {
                ++p;
                break;
            }
            return fail(SerializeErrorCode::Syntax, "expected ',' or '}'");
        }

        bool decoded = false;
        if (options.detectExtensions) {
            if (!tryDecodeEnvelope(map, out, decoded))
                return false;
        }
        if (!decoded)
            out = Variant(std::move(map));
        return true;
    }

    bool tryDecodeEnvelope(const VariantMap& map, Variant& out, bool& decoded)
    {
        decoded = false;
        if (map.size() != 3)
            return true;   // 不是信封，按普通 map 处理

        const Variant* vt = map.find("$vt");
        const Variant* type = map.find("$type");
        const Variant* data = map.find("$data");
        if (!vt || !type || !data)
            return true;
        if (!vt->isString())
            return true;   // 三个键都在但 $vt 不是字符串：按普通 map 处理
        // 与二进制头一致：认得出的信封但版本不支持，必须响亮报错，
        // 不能悄悄退化成普通 map（否则类型信息无声丢失）。
        if (vt->asString() != "1")
            return fail(SerializeErrorCode::VersionMismatch,
                        "unsupported JSON envelope version '" + vt->asString() + "'");
        if (!type->isString())
            return true;

        const std::string& name = type->asString();
        const CodecRegistry::Entry* codec = CodecRegistry::instance().findByName(name);
        if (!codec) {
            return fail(SerializeErrorCode::UnknownType,
                        "no codec registered for type name '" + name + "'");
        }

        std::any any;
        try {
            if (!codec->decode(*data, any))
                return fail(SerializeErrorCode::UnsupportedValue,
                            "codec failed to decode type '" + name + "'");
        } catch (const std::exception& e) {
            return fail(SerializeErrorCode::UnsupportedValue, e.what());
        }

        out = Variant::fromValue(std::move(any));
        decoded = true;
        return true;
    }
};

} // namespace

bool tryWrite(const Variant& value,
              std::string& out,
              SerializeError* error,
              const JsonWriteOptions& options)
{
    registerBuiltinCodecs();
    JsonWriter writer(options);
    writer.out.reserve(256);

    if (!writer.writeValue(value, 0)) {
        if (error)
            *error = writer.error;
        return false;
    }

    out = std::move(writer.out);
    if (error)
        *error = SerializeError{};
    return true;
}

std::string write(const Variant& value, const JsonWriteOptions& options)
{
    std::string out;
    SerializeError error;
    if (!tryWrite(value, out, &error, options))
        throw SerializeException(std::move(error));
    return out;
}

bool tryRead(std::string_view text,
             Variant& out,
             SerializeError* error,
             const JsonReadOptions& options)
{
    registerBuiltinCodecs();
    JsonParser parser(text, options);
    parser.skipWs();
    if (parser.p >= parser.end) {
        parser.fail(SerializeErrorCode::UnexpectedEnd, "empty input");
        if (error)
            *error = parser.error;
        return false;
    }

    Variant value;
    if (!parser.parseValue(value, 0)) {
        if (error)
            *error = parser.error;
        return false;
    }

    parser.skipWs();
    if (parser.p != parser.end) {
        parser.fail(SerializeErrorCode::TrailingData, "trailing characters after root value");
        if (error)
            *error = parser.error;
        return false;
    }

    out = std::move(value);
    if (error)
        *error = SerializeError{};
    return true;
}

Variant read(std::string_view text, const JsonReadOptions& options)
{
    Variant value;
    SerializeError error;
    if (!tryRead(text, value, &error, options))
        throw SerializeException(std::move(error));
    return value;
}

bool writeTo(std::ostream& output,
             const Variant& value,
             SerializeError* error,
             const JsonWriteOptions& options)
{
    std::string data;
    SerializeError local;
    if (!tryWrite(value, data, &local, options)) {
        if (error)
            *error = std::move(local);
        return false;
    }
    if (!writeStreamAll(output, data, local)) {
        if (error)
            *error = std::move(local);
        return false;
    }
    if (error)
        *error = SerializeError{};
    return true;
}

bool readFrom(std::istream& input,
              Variant& out,
              SerializeError* error,
              const JsonReadOptions& options)
{
    std::string data;
    SerializeError local;
    if (!readStreamAll(input, data, local)) {
        if (error)
            *error = std::move(local);
        return false;
    }
    return tryRead(data, out, error, options);
}

bool tryWriteFile(const std::string& path,
                  const Variant& value,
                  SerializeError* error,
                  const JsonWriteOptions& options)
{
    std::string data;
    SerializeError local;
    if (!tryWrite(value, data, &local, options)) {
        if (error)
            *error = std::move(local);
        return false;
    }
    if (!writeFileAll(path, data, local)) {
        if (error)
            *error = std::move(local);
        return false;
    }
    if (error)
        *error = SerializeError{};
    return true;
}

void writeFile(const std::string& path, const Variant& value, const JsonWriteOptions& options)
{
    SerializeError error;
    if (!tryWriteFile(path, value, &error, options))
        throw SerializeException(std::move(error));
}

bool tryReadFile(const std::string& path,
                 Variant& out,
                 SerializeError* error,
                 const JsonReadOptions& options)
{
    std::string data;
    SerializeError local;
    if (!readFileAll(path, data, local)) {
        if (error)
            *error = std::move(local);
        return false;
    }
    return tryRead(data, out, error, options);
}

Variant readFile(const std::string& path, const JsonReadOptions& options)
{
    Variant value;
    SerializeError error;
    if (!tryReadFile(path, value, &error, options))
        throw SerializeException(std::move(error));
    return value;
}

} // namespace vt::json
