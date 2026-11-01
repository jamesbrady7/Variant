#include "vt/binary.h"

#include "utf8.h"
#include "vt/adapters.h"
#include "vt/codec.h"
#include "vt/variant_list.h"
#include "vt/variant_map.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
#include <iterator>
#include <ostream>
#include <utility>
#include <vector>

namespace vt::binary {

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

constexpr unsigned char kTagFalse = 0x01;
constexpr unsigned char kTagTrue = 0x02;
constexpr unsigned char kTagInt = 0x03;
constexpr unsigned char kTagDouble = 0x04;
constexpr unsigned char kTagString = 0x05;
constexpr unsigned char kTagList = 0x06;
constexpr unsigned char kTagMap = 0x07;
constexpr unsigned char kTagExtension = 0x08;
constexpr unsigned char kTagNull = 0x00;

constexpr unsigned char kMagic0 = 'V';
constexpr unsigned char kMagic1 = 'T';
constexpr unsigned char kMagic2 = 'B';
constexpr unsigned char kVersion = 0x01;

void appendVarint(std::string& out, std::uint64_t value)
{
    while (value >= 0x80) {
        out.push_back(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<char>(value));
}

void appendZigZag(std::string& out, std::int64_t value)
{
    const std::uint64_t u = static_cast<std::uint64_t>(value);
    const std::uint64_t zz = (u << 1) ^ (value < 0 ? ~std::uint64_t(0) : std::uint64_t(0));
    appendVarint(out, zz);
}

void appendDouble(std::string& out, double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<char>((bits >> (8 * i)) & 0xFF));
}

// ---------------------------------------------------------------- Writer

struct Writer {
    std::string out;
    const BinaryWriteOptions& options;
    SerializeError error;

    explicit Writer(const BinaryWriteOptions& opt) : options(opt) {}

    bool fail(SerializeErrorCode code, std::string message)
    {
        error.code = code;
        error.message = std::move(message);
        return false;
    }

    bool writeValue(const Variant& value, int depth)
    {
        if (depth > options.maxDepth)
            return fail(SerializeErrorCode::DepthExceeded, "maximum nesting depth exceeded");

        switch (value.type()) {
        case Variant::Type::Null:
            out.push_back(static_cast<char>(kTagNull));
            return true;
        case Variant::Type::Bool:
            out.push_back(static_cast<char>(value.asBool() ? kTagTrue : kTagFalse));
            return true;
        case Variant::Type::Int:
            out.push_back(static_cast<char>(kTagInt));
            appendZigZag(out, value.asInt());
            return true;
        case Variant::Type::Double:
            out.push_back(static_cast<char>(kTagDouble));
            appendDouble(out, value.asDouble());
            return true;
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

    bool writeRawString(std::string_view text)
    {
        if (!detail::isValidUtf8(text))
            return fail(SerializeErrorCode::InvalidUtf8, "string is not valid UTF-8");
        appendVarint(out, text.size());
        out.append(text.data(), text.size());
        return true;
    }

    bool writeString(std::string_view text)
    {
        out.push_back(static_cast<char>(kTagString));
        return writeRawString(text);
    }

    bool writeList(const VariantList& list, int depth)
    {
        out.push_back(static_cast<char>(kTagList));
        appendVarint(out, list.size());
        for (const Variant& item : list) {
            if (!writeValue(item, depth + 1))
                return false;
        }
        return true;
    }

    bool writeMap(const VariantMap& map, int depth)
    {
        out.push_back(static_cast<char>(kTagMap));
        appendVarint(out, map.size());

        if (options.sortKeys) {
            std::vector<const VariantMap::value_type*> items;
            items.reserve(map.size());
            for (const auto& kv : map)
                items.push_back(&kv);
            std::sort(items.begin(), items.end(),
                      [](const VariantMap::value_type* a, const VariantMap::value_type* b) {
                          return a->first < b->first;
                      });
            for (const auto* kv : items) {
                if (!writeRawString(kv->first))
                    return false;
                if (!writeValue(kv->second, depth + 1))
                    return false;
            }
        } else {
            for (const auto& kv : map) {
                if (!writeRawString(kv.first))
                    return false;
                if (!writeValue(kv.second, depth + 1))
                    return false;
            }
        }
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

        Variant plain;
        try {
            plain = codec->encode(*any);
        } catch (const std::exception& e) {
            return fail(SerializeErrorCode::EncodeFailed, e.what());
        }

        out.push_back(static_cast<char>(kTagExtension));
        if (!writeRawString(codec->name))
            return false;
        return writeValue(plain, depth + 1);
    }
};

// ---------------------------------------------------------------- Reader

struct Reader {
    const unsigned char* begin = nullptr;
    const unsigned char* p = nullptr;
    const unsigned char* end = nullptr;
    const BinaryReadOptions& options;
    SerializeError error;

    explicit Reader(const BinaryReadOptions& opt) : options(opt) {}

    bool fail(SerializeErrorCode code, std::string message)
    {
        error.code = code;
        error.message = std::move(message);
        error.offset = static_cast<std::size_t>(p - begin);
        return false;
    }

    std::size_t remaining() const { return static_cast<std::size_t>(end - p); }

    bool readByte(unsigned char& value)
    {
        if (p >= end)
            return fail(SerializeErrorCode::UnexpectedEnd, "unexpected end of binary data");
        value = *p++;
        return true;
    }

    bool readVarint(std::uint64_t& value)
    {
        value = 0;
        for (int i = 0; i < 10; ++i) {
            unsigned char byte = 0;
            if (!readByte(byte))
                return false;
            if (i == 9 && (byte & 0xFE) != 0)
                return fail(SerializeErrorCode::BadLength, "varint overflow");
            value |= static_cast<std::uint64_t>(byte & 0x7F) << (7 * i);
            if ((byte & 0x80) == 0)
                return true;
        }
        return fail(SerializeErrorCode::BadLength, "varint too long");
    }

    bool readString(std::string& out)
    {
        std::uint64_t length = 0;
        if (!readVarint(length))
            return false;
        if (length > options.maxStringBytes)
            return fail(SerializeErrorCode::BadLength, "string length exceeds limit");
        if (length > remaining())
            return fail(SerializeErrorCode::UnexpectedEnd, "string length exceeds remaining data");
        out.assign(reinterpret_cast<const char*>(p), static_cast<std::size_t>(length));
        p += static_cast<std::size_t>(length);
        if (!detail::isValidUtf8(out))
            return fail(SerializeErrorCode::InvalidUtf8, "string is not valid UTF-8");
        return true;
    }

    bool readValue(Variant& out, int depth)
    {
        if (depth > options.maxDepth)
            return fail(SerializeErrorCode::DepthExceeded, "maximum nesting depth exceeded");

        unsigned char tag = 0;
        if (!readByte(tag))
            return false;

        switch (tag) {
        case kTagNull:
            out = Variant();
            return true;
        case kTagFalse:
            out = Variant(false);
            return true;
        case kTagTrue:
            out = Variant(true);
            return true;
        case kTagInt: {
            std::uint64_t zz = 0;
            if (!readVarint(zz))
                return false;
            const std::int64_t value =
                static_cast<std::int64_t>((zz >> 1) ^ (~(zz & 1) + 1));
            out = Variant(value);
            return true;
        }
        case kTagDouble: {
            if (remaining() < 8)
                return fail(SerializeErrorCode::UnexpectedEnd, "truncated double");
            std::uint64_t bits = 0;
            for (int i = 0; i < 8; ++i)
                bits |= static_cast<std::uint64_t>(*p++) << (8 * i);
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(value));
            out = Variant(value);
            return true;
        }
        case kTagString: {
            std::string text;
            if (!readString(text))
                return false;
            out = Variant(std::move(text));
            return true;
        }
        case kTagList:
            return readList(out, depth);
        case kTagMap:
            return readMap(out, depth);
        case kTagExtension:
            return readExtension(out, depth);
        default:
            return fail(SerializeErrorCode::Syntax, "unknown binary tag");
        }
    }

    bool readList(Variant& out, int depth)
    {
        std::uint64_t count = 0;
        if (!readVarint(count))
            return false;
        if (count > options.maxElements)
            return fail(SerializeErrorCode::BadLength, "list element count exceeds limit");
        if (count > remaining())   // 每个值至少 1 字节
            return fail(SerializeErrorCode::BadLength, "list element count exceeds remaining data");

        VariantList list;
        list.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            Variant item;
            if (!readValue(item, depth + 1))
                return false;
            list.push_back(std::move(item));
        }
        out = Variant(std::move(list));
        return true;
    }

    bool readMap(Variant& out, int depth)
    {
        std::uint64_t count = 0;
        if (!readVarint(count))
            return false;
        if (count > options.maxElements)
            return fail(SerializeErrorCode::BadLength, "map entry count exceeds limit");
        if (count > remaining() / 2)   // 每条至少 key 长度(1) + value 标签(1)
            return fail(SerializeErrorCode::BadLength, "map entry count exceeds remaining data");

        VariantMap map;
        map.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            std::string key;
            if (!readString(key))
                return false;
            Variant value;
            if (!readValue(value, depth + 1))
                return false;
            map[std::move(key)] = std::move(value);
        }
        out = Variant(std::move(map));
        return true;
    }

    bool readExtension(Variant& out, int depth)
    {
        std::string name;
        if (!readString(name))
            return false;

        const CodecRegistry::Entry* codec = CodecRegistry::instance().findByName(name);
        if (!codec) {
            return fail(SerializeErrorCode::UnknownType,
                        "no codec registered for type name '" + name + "'");
        }

        Variant plain;
        if (!readValue(plain, depth + 1))
            return false;

        std::any any;
        try {
            if (!codec->decode(plain, any))
                return fail(SerializeErrorCode::UnsupportedValue,
                            "codec failed to decode type '" + name + "'");
        } catch (const std::exception& e) {
            return fail(SerializeErrorCode::UnsupportedValue, e.what());
        }
        out = Variant::fromValue(std::move(any));
        return true;
    }
};

} // namespace

bool tryWrite(const Variant& value,
              std::string& out,
              SerializeError* error,
              const BinaryWriteOptions& options)
{
    registerBuiltinCodecs();
    Writer writer(options);
    writer.out.reserve(64);

    writer.out.push_back(static_cast<char>(kMagic0));
    writer.out.push_back(static_cast<char>(kMagic1));
    writer.out.push_back(static_cast<char>(kMagic2));
    writer.out.push_back(static_cast<char>(kVersion));

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

std::string write(const Variant& value, const BinaryWriteOptions& options)
{
    std::string out;
    SerializeError error;
    if (!tryWrite(value, out, &error, options))
        throw SerializeException(std::move(error));
    return out;
}

bool tryRead(std::string_view bytes,
             Variant& out,
             SerializeError* error,
             const BinaryReadOptions& options)
{
    registerBuiltinCodecs();
    Reader reader(options);
    reader.begin = reinterpret_cast<const unsigned char*>(bytes.data());
    reader.p = reader.begin;
    reader.end = reader.begin + bytes.size();

    if (bytes.size() < 4) {
        reader.fail(SerializeErrorCode::UnexpectedEnd, "binary data too short");
        if (error)
            *error = reader.error;
        return false;
    }
    if (reader.begin[0] != kMagic0 || reader.begin[1] != kMagic1 || reader.begin[2] != kMagic2) {
        reader.fail(SerializeErrorCode::Syntax, "bad binary magic");
        if (error)
            *error = reader.error;
        return false;
    }
    if (reader.begin[3] != kVersion) {
        reader.fail(SerializeErrorCode::VersionMismatch, "unsupported binary version");
        if (error)
            *error = reader.error;
        return false;
    }
    reader.p += 4;

    Variant value;
    if (!reader.readValue(value, 0)) {
        if (error)
            *error = reader.error;
        return false;
    }
    if (reader.p != reader.end) {
        reader.fail(SerializeErrorCode::TrailingData, "trailing bytes after root value");
        if (error)
            *error = reader.error;
        return false;
    }

    out = std::move(value);
    if (error)
        *error = SerializeError{};
    return true;
}

Variant read(std::string_view bytes, const BinaryReadOptions& options)
{
    Variant value;
    SerializeError error;
    if (!tryRead(bytes, value, &error, options))
        throw SerializeException(std::move(error));
    return value;
}

bool writeTo(std::ostream& output,
             const Variant& value,
             SerializeError* error,
             const BinaryWriteOptions& options)
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
              const BinaryReadOptions& options)
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
                  const BinaryWriteOptions& options)
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

void writeFile(const std::string& path, const Variant& value, const BinaryWriteOptions& options)
{
    SerializeError error;
    if (!tryWriteFile(path, value, &error, options))
        throw SerializeException(std::move(error));
}

bool tryReadFile(const std::string& path,
                 Variant& out,
                 SerializeError* error,
                 const BinaryReadOptions& options)
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

Variant readFile(const std::string& path, const BinaryReadOptions& options)
{
    Variant value;
    SerializeError error;
    if (!tryReadFile(path, value, &error, options))
        throw SerializeException(std::move(error));
    return value;
}

} // namespace vt::binary
