#include "vt/adapters.h"

#include "vt/codec.h"

#include <mutex>
#include <string>
#include <utility>

namespace vt {

// ---------------------------------------------------------------- fromValue

Variant Variant::fromValue(std::vector<Variant> value)
{
    return Variant(VariantList(std::move(value)));
}

Variant Variant::fromValue(std::unordered_map<std::string, Variant> value)
{
    return Variant(VariantMap(std::move(value)));
}

Variant Variant::fromValue(std::vector<int> value)
{
    return Variant::fromValue(std::any(std::move(value)));
}

Variant Variant::fromValue(std::vector<std::string> value)
{
    return Variant::fromValue(std::any(std::move(value)));
}

Variant Variant::fromValue(std::unordered_map<std::string, std::string> value)
{
    return Variant::fromValue(std::any(std::move(value)));
}

Variant Variant::fromValue(std::unordered_map<int, int> value)
{
    return Variant::fromValue(std::any(std::move(value)));
}

// ---------------------------------------------------------------- 内建 codec

namespace {

template <class T>
bool hasCodec()
{
    return CodecRegistry::instance().find(std::type_index(typeid(T))) != nullptr;
}

Variant encodeIntVector(const std::vector<int>& values)
{
    VariantList list;
    list.reserve(values.size());
    for (int value : values)
        list.push_back(value);
    return Variant(std::move(list));
}

bool decodeIntVector(const Variant& plain, std::vector<int>& out)
{
    if (!plain.isList())
        return false;
    out.clear();
    out.reserve(plain.asList().size());
    for (const Variant& item : plain.asList()) {
        auto number = item.get<int>();
        if (!number)
            return false;
        out.push_back(*number);
    }
    return true;
}

Variant encodeStringVector(const std::vector<std::string>& values)
{
    VariantList list;
    list.reserve(values.size());
    for (const std::string& value : values)
        list.push_back(value);
    return Variant(std::move(list));
}

bool decodeStringVector(const Variant& plain, std::vector<std::string>& out)
{
    if (!plain.isList())
        return false;
    out.clear();
    out.reserve(plain.asList().size());
    for (const Variant& item : plain.asList()) {
        if (!item.isString())
            return false;
        out.push_back(item.asString());
    }
    return true;
}

Variant encodeStringMap(const std::unordered_map<std::string, std::string>& values)
{
    VariantMap map;
    map.reserve(values.size());
    for (const auto& kv : values)
        map[kv.first] = kv.second;
    return Variant(std::move(map));
}

bool decodeStringMap(const Variant& plain, std::unordered_map<std::string, std::string>& out)
{
    if (!plain.isMap())
        return false;
    out.clear();
    out.reserve(plain.asMap().size());
    for (const auto& kv : plain.asMap()) {
        if (!kv.second.isString())
            return false;
        out.emplace(kv.first, kv.second.asString());
    }
    return true;
}

Variant encodeIntMap(const std::unordered_map<int, int>& values)
{
    VariantMap map;
    map.reserve(values.size());
    for (const auto& kv : values)
        map[std::to_string(kv.first)] = kv.second;
    return Variant(std::move(map));
}

bool decodeIntMap(const Variant& plain, std::unordered_map<int, int>& out)
{
    out.clear();
    if (plain.isMap()) {
        out.reserve(plain.asMap().size());
        for (const auto& kv : plain.asMap()) {
            auto key = detail::parseIntKey(kv.first);
            auto number = kv.second.get<int>();
            if (!key || !number)
                return false;
            out.insert_or_assign(*key, *number);
        }
        return true;
    }
    if (plain.isList()) {
        out.reserve(plain.asList().size());
        for (const Variant& item : plain.asList()) {
            if (!item.isList() || item.asList().size() != 2)
                return false;
            auto key = item.asList()[0].get<int>();
            auto number = item.asList()[1].get<int>();
            if (!key || !number)
                return false;
            out.insert_or_assign(*key, *number);
        }
        return true;
    }
    return false;
}

} // namespace

void registerBuiltinCodecs()
{
    static std::once_flag once;
    std::call_once(once, [] {
        // 用户如果先注册了同类型 codec，就保留用户的实现。
        if (!hasCodec<std::vector<int>>())
            registerValueCodec<std::vector<int>>("std::vector<int>", &encodeIntVector,
                                                 &decodeIntVector, true);
        if (!hasCodec<std::vector<std::string>>())
            registerValueCodec<std::vector<std::string>>(
                "std::vector<std::string>", &encodeStringVector, &decodeStringVector, true);
        if (!hasCodec<std::unordered_map<std::string, std::string>>())
            registerValueCodec<std::unordered_map<std::string, std::string>>(
                "std::unordered_map<std::string, std::string>", &encodeStringMap,
                &decodeStringMap, true);
        if (!hasCodec<std::unordered_map<int, int>>())
            registerValueCodec<std::unordered_map<int, int>>(
                "std::unordered_map<int, int>", &encodeIntMap, &decodeIntMap, true);
    });
}

} // namespace vt
