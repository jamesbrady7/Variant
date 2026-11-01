// 序列化测试：JSON / 二进制 / codec 注册表 / 错误路径 / 随机往返。
#include "vt/vt.h"

#include <cmath>
#include <cstdio>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <limits>
#include <map>
#include <string_view>
#include <random>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(...) do { ++g_checks; if (!(__VA_ARGS__)) { ++g_failures; std::printf("[FAIL] %s:%d: %s%c", __FILE__, __LINE__, #__VA_ARGS__, 10); } } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!((a) == (b))) {                                                   \
            ++g_failures;                                                      \
            std::printf("[FAIL] %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
        }                                                                      \
    } while (0)

struct Point2 {
    int x = 0;
    int y = 0;
};

struct PNode {
    int id = 0;
    std::string name;
};

bool expectJsonError(std::string_view text, vt::SerializeErrorCode code,
                     vt::JsonReadOptions options = {})
{
    vt::Variant value;
    vt::SerializeError error;
    const bool ok = vt::json::tryRead(text, value, &error, options);
    if (ok) {
        std::printf("[FAIL] expected JSON error %s for: %s\n",
                    vt::toString(code), std::string(text).c_str());
        return false;
    }
    if (error.code != code) {
        std::printf("[FAIL] expected %s, got %s (%s)\n", vt::toString(code),
                    vt::toString(error.code), error.message.c_str());
        return false;
    }
    return true;
}

void registerCodecs()
{
    CHECK(vt::registerValueCodec<Point2>(
        "Point2",
        [](const Point2& p) { return vt::VariantMap{{"x", p.x}, {"y", p.y}}; },
        [](const vt::Variant& plain, Point2& out) {
            if (!plain.isMap())
                return false;
            auto x = plain.getAt<int>("x");
            auto y = plain.getAt<int>("y");
            if (!x || !y)
                return false;
            out = Point2{*x, *y};
            return true;
        }));

    CHECK(vt::registerPointerCodec<PNode>(
        "PNode",
        [](const PNode* node) {
            if (!node)
                return vt::Variant();
            return vt::Variant(vt::VariantMap{{"id", node->id}, {"name", node->name}});
        },
        [](const vt::Variant& plain, PNode*& out) {
            if (!plain.isMap())
                return false;
            auto id = plain.getAt<int>("id");
            auto name = plain.getAt<std::string>("name");
            if (!id || !name)
                return false;
            out = new PNode{*id, *name};
            return true;
        }));

    // 名字冲突应失败
    CHECK(!vt::registerValueCodec<Point2>("Point2Conflict",
                                          [](const Point2&) { return vt::Variant(); },
                                          [](const vt::Variant&, Point2&) { return false; }));
}

void testAdapters()
{
    // vector<int>：扩展槽与普通 List 都能读
    vt::Variant ivec = vt::Variant::fromValue(std::vector<int>{1, 2, 3});
    CHECK(ivec.isExtension());
    auto ivecBack = vt::getAs<std::vector<int>>(ivec);
    CHECK(ivecBack.has_value() && (*ivecBack)[2] == 3);

    vt::Variant ilist = vt::Variant(vt::VariantList{1, 2, 3});
    auto fromList = vt::getAs<std::vector<int>>(ilist);
    CHECK(fromList.has_value() && (*fromList)[1] == 2);
    CHECK(!vt::getAs<std::vector<int>>(vt::Variant(vt::VariantList{1, "x"})).has_value());

    std::vector<int> out;
    CHECK(vt::getAs(vt::Variant(vt::VariantList{4, 5}), out));
    CHECK(out.size() == 2 && out[0] == 4);

    // vector<string>
    vt::Variant svec = vt::Variant::fromValue(std::vector<std::string>{"a", "b"});
    CHECK(vt::getAs<std::vector<std::string>>(svec).has_value());
    CHECK(vt::getAs<std::vector<std::string>>(vt::Variant(vt::VariantList{"a", "b"})).has_value());

    // map<string,string>
    std::unordered_map<std::string, std::string> strMap{{"k", "v"}};
    vt::Variant smap = vt::Variant::fromValue(strMap);
    CHECK(smap.isExtension());
    CHECK(vt::getAs<std::unordered_map<std::string, std::string>>(smap).has_value());
    CHECK(vt::getAs<std::unordered_map<std::string, std::string>>(
              vt::Variant(vt::VariantMap{{"k", "v"}}))
              .has_value());

    // map<int,int>：扩展、字符串键 Map、数组对 都能读
    std::unordered_map<int, int> intMap{{1, 2}, {3, 4}};
    vt::Variant imap = vt::Variant::fromValue(intMap);
    CHECK(imap.isExtension());
    auto imapBack = vt::getAs<std::unordered_map<int, int>>(imap);
    CHECK(imapBack.has_value() && (*imapBack)[3] == 4);

    auto fromStringKeys = vt::getAs<std::unordered_map<int, int>>(
        vt::Variant(vt::VariantMap{{"1", 2}, {"3", 4}}));
    CHECK(fromStringKeys.has_value() && (*fromStringKeys)[1] == 2);

    auto fromPairs = vt::getAs<std::unordered_map<int, int>>(
        vt::Variant(vt::VariantList{vt::VariantList{1, 2}, vt::VariantList{3, 4}}));
    CHECK(fromPairs.has_value() && (*fromPairs)[3] == 4);

    CHECK(!vt::getAs<std::unordered_map<int, int>>(
               vt::Variant(vt::VariantMap{{"x", 2}}))
               .has_value());

    // vector<Variant> / map<string,Variant> 规范化成原生 List / Map
    vt::Variant vv = vt::Variant::fromValue(std::vector<vt::Variant>{1, "x"});
    CHECK(vv.isList());
    CHECK_EQ(vv.asList().size(), std::size_t(2));
    CHECK(vt::getAs<std::vector<vt::Variant>>(vv).has_value());

    std::unordered_map<std::string, vt::Variant> rawMap{{"a", 1}, {"b", "x"}};
    vt::Variant mv = vt::Variant::fromValue(rawMap);
    CHECK(mv.isMap());
    CHECK_EQ(mv.asMap().size(), std::size_t(2));
    auto mvBack = vt::getAs<std::unordered_map<std::string, vt::Variant>>(mv);
    CHECK(mvBack.has_value() && (*mvBack)["a"].asInt() == 1);

    // 普通扩展类型
    vt::Variant point = vt::Variant::fromValue(Point2{5, 6});
    CHECK(vt::getAs<Point2>(point).has_value());
    CHECK(!vt::getAs<Point2>(vt::Variant(vt::VariantList{})).has_value());
}

void testBuiltinSerialization()
{
    const vt::Variant ivec = vt::Variant::fromValue(std::vector<int>{1, 2, 3});
    CHECK_EQ(vt::json::write(ivec), std::string("[1,2,3]"));

    const vt::Variant plainBack = vt::json::read("[1,2,3]");
    CHECK(plainBack.isList());
    CHECK(vt::getAs<std::vector<int>>(plainBack).has_value());

    // typed 模式：保留 $type，读回仍是扩展
    {
        vt::JsonWriteOptions typed;
        typed.extensions = vt::ExtensionPolicy::EnvelopeAll;
        const std::string text = vt::json::write(ivec, typed);
        CHECK(text.find("\"$type\":\"std::vector<int>\"") != std::string::npos);
        const vt::Variant typedBack = vt::json::read(text);
        CHECK(typedBack.isExtension());
        CHECK(vt::getAs<std::vector<int>>(typedBack).has_value());
    }

    std::unordered_map<std::string, std::string> strMap{{"b", "2"}, {"a", "1"}};
    CHECK_EQ(vt::json::write(vt::Variant::fromValue(strMap)),
             std::string("{\"a\":\"1\",\"b\":\"2\"}"));

    std::unordered_map<int, int> intMap{{3, 4}, {1, 2}};
    CHECK_EQ(vt::json::write(vt::Variant::fromValue(intMap)),
             std::string("{\"1\":2,\"3\":4}"));

    // 二进制保留类型，读回是扩展
    const vt::Variant binBack = vt::binary::read(vt::binary::write(ivec));
    CHECK(binBack.isExtension());
    CHECK(vt::getAs<std::vector<int>>(binBack).has_value());

    // Reject 允许 plain 内建容器
    vt::JsonWriteOptions reject;
    reject.extensions = vt::ExtensionPolicy::Reject;
    CHECK_EQ(vt::json::write(ivec, reject), std::string("[1,2,3]"));
}

void testNestedSerialization()
{
    // 容器 codec 由 fromValue 自动登记，且是 plain：JSON 写出普通嵌套数组。
    const std::vector<std::vector<int>> grid{{1, 2}, {3}};
    const vt::Variant value = vt::Variant::fromValue(grid);
    CHECK_EQ(vt::json::write(value), std::string("[[1,2],[3]]"));

    const vt::Variant jsonBack = vt::json::read(vt::json::write(value));
    CHECK(jsonBack.isList());
    auto fromJson = vt::getAs<std::vector<std::vector<int>>>(jsonBack);
    CHECK(fromJson.has_value() && (*fromJson)[1][0] == 3);

    // 二进制：类型名由模板自动生成，读回仍是紧凑扩展槽。
    const vt::Variant binBack = vt::binary::read(vt::binary::write(value));
    CHECK(binBack.isExtension());
    CHECK((binBack.is<std::vector<std::vector<int>>>()));
    auto fromBinary = vt::getAs<std::vector<std::vector<int>>>(binBack);
    CHECK(fromBinary.has_value() && (*fromBinary)[0][1] == 2);

    // 整数键：写进 JSON 是十进制字符串键的对象。
    const std::map<int, std::vector<std::string>> byId{{7, {"a"}}, {8, {"b"}}};
    const vt::Variant mapValue = vt::Variant::fromValue(byId);
    CHECK_EQ(vt::json::write(mapValue), std::string("{\"7\":[\"a\"],\"8\":[\"b\"]}"));
    auto mapBack = vt::getAs<std::map<int, std::vector<std::string>>>(
        vt::json::read(vt::json::write(mapValue)));
    CHECK(mapBack.has_value() && (*mapBack)[8][0] == "b");

    // 只读进程的编译期锚点：显式登记后，二进制按名字恢复。
    vt::registerContainerAdapters<std::vector<std::pair<int, std::string>>>();
    const std::vector<std::pair<int, std::string>> pairs{{1, "x"}};
    const vt::Variant pairBack =
        vt::binary::read(vt::binary::write(vt::Variant::fromValue(pairs)));
    auto restored = vt::getAs<std::vector<std::pair<int, std::string>>>(pairBack);
    CHECK(restored.has_value() && (*restored)[0].second == "x");
}

void testJsonBuiltins()
{
    CHECK_EQ(vt::json::write(vt::Variant()), std::string("null"));
    CHECK_EQ(vt::json::write(vt::Variant(true)), std::string("true"));
    CHECK_EQ(vt::json::write(vt::Variant(false)), std::string("false"));
    CHECK_EQ(vt::json::write(vt::Variant(42)), std::string("42"));
    CHECK_EQ(vt::json::write(vt::Variant(1.0)), std::string("1.0"));
    CHECK_EQ(vt::json::write(vt::Variant(1.5)), std::string("1.5"));
    CHECK_EQ(vt::json::write(vt::Variant(std::string("a\"b\n"))),
             std::string("\"a\\\"b\\n\""));

    const vt::VariantMap map{{"b", 2}, {"a", 1}};
    CHECK_EQ(vt::json::write(vt::Variant(map)), std::string("{\"a\":1,\"b\":2}"));

    const vt::VariantList list{1, "x", true, vt::Variant()};
    CHECK_EQ(vt::json::write(vt::Variant(list)), std::string("[1,\"x\",true,null]"));

    const std::vector<vt::Variant> values{
        vt::Variant(),
        vt::Variant(true),
        vt::Variant(false),
        vt::Variant(std::int64_t(0)),
        vt::Variant(std::numeric_limits<std::int64_t>::min()),
        vt::Variant(std::numeric_limits<std::int64_t>::max()),
        vt::Variant(0.0),
        vt::Variant(-0.0),
        vt::Variant(0.1),
        vt::Variant(1e-300),
        vt::Variant(1.7976931348623157e308),
        vt::Variant(std::string("")),
        vt::Variant(std::string("中文 😀")),
        vt::Variant(std::string("line\n\t\r\b\f")),
        vt::Variant(vt::VariantList{}),
        vt::Variant(vt::VariantMap{}),
        vt::Variant(vt::VariantList{1, vt::VariantMap{{"a", vt::VariantList{true, "x"}}}}),
    };

    for (const vt::Variant& value : values) {
        const std::string text = vt::json::write(value);
        const vt::Variant back = vt::json::read(text);
        CHECK(back == value);
        if (value.isDouble())
            CHECK(back.isDouble());   // 1.0 不能读回 Int
        if (value.isInt())
            CHECK(back.isInt());
    }

    CHECK(vt::json::read("\"\\uD83D\\uDE00\"").asString() == std::string("😀"));
    CHECK(vt::json::read("1 \n\t ").asInt() == 1);   // 尾随空白允许
}

void testJsonPretty()
{
    vt::JsonWriteOptions options;
    options.pretty = true;
    const std::string text = vt::json::write(vt::Variant(vt::VariantMap{{"a", 1}}), options);
    CHECK_EQ(text, std::string("{\n  \"a\": 1\n}"));

    const vt::Variant back = vt::json::read(text);
    CHECK(back.isMap());
    CHECK_EQ(back.at("a").asInt(), 1);
}

void testJsonErrors()
{
    CHECK(expectJsonError("", vt::SerializeErrorCode::UnexpectedEnd));
    CHECK(expectJsonError("{", vt::SerializeErrorCode::UnexpectedEnd));
    CHECK(expectJsonError("[1,]", vt::SerializeErrorCode::Syntax));
    CHECK(expectJsonError("{\"a\":1,}", vt::SerializeErrorCode::Syntax));
    CHECK(expectJsonError("01", vt::SerializeErrorCode::InvalidNumber));
    CHECK(expectJsonError("+1", vt::SerializeErrorCode::Syntax));
    CHECK(expectJsonError(".5", vt::SerializeErrorCode::Syntax));
    CHECK(expectJsonError("1.", vt::SerializeErrorCode::InvalidNumber));
    CHECK(expectJsonError("1e", vt::SerializeErrorCode::InvalidNumber));
    CHECK(expectJsonError("\"\\uD800\"", vt::SerializeErrorCode::InvalidUtf8));
    CHECK(expectJsonError("\"\\uDC00\"", vt::SerializeErrorCode::InvalidUtf8));
    CHECK(expectJsonError(std::string("\"\xC0\xAF\""), vt::SerializeErrorCode::InvalidUtf8));
    CHECK(expectJsonError("1 x", vt::SerializeErrorCode::TrailingData));

    std::string deep;
    deep.reserve(700);
    for (int i = 0; i < 300; ++i)
        deep.push_back('[');
    for (int i = 0; i < 300; ++i)
        deep.push_back(']');
    CHECK(expectJsonError(deep, vt::SerializeErrorCode::DepthExceeded));

    // 重复键：后者覆盖前者
    const vt::Variant duplicate = vt::json::read("{\"a\":1,\"a\":2}");
    CHECK_EQ(duplicate.at("a").asInt(), 2);

    // 超过 int64 的整数退化为 double
    const vt::Variant big = vt::json::read("9223372036854775808");
    CHECK(big.isDouble());
}

void testJsonExtensions()
{
    const vt::Variant point = vt::Variant::fromValue(Point2{3, 4});
    const std::string text = vt::json::write(point);
    CHECK(text.find("\"$type\":\"Point2\"") != std::string::npos);

    const vt::Variant back = vt::json::read(text);
    CHECK(back.isExtension());
    auto decoded = back.get<Point2>();
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->x, 3);
    CHECK_EQ(decoded->y, 4);

    // 嵌套在 map/list 中
    vt::Variant root = vt::VariantMap{
        {"points", vt::VariantList{point, vt::Variant::fromValue(Point2{5, 6})}},
        {"meta", vt::VariantMap{{"owner", point}}},
    };
    const vt::Variant rt = vt::json::read(vt::json::write(root));
    const vt::Variant* points = root.find("points");
    CHECK(points != nullptr);
    const vt::Variant& first = rt.at("points").asList().at(0);
    CHECK(first.isExtension());
    CHECK_EQ(first.get<Point2>()->x, 3);

    // vector<int>：默认 plain 输出，读回是 List，用 getAs 恢复
    const vt::Variant vec = vt::Variant::fromValue(std::vector<int>{7, 8, 9});
    const std::string vecText = vt::json::write(vec);
    CHECK_EQ(vecText, std::string("[7,8,9]"));
    const vt::Variant vecBack = vt::json::read(vecText);
    CHECK(vecBack.isList());
    auto values = vt::getAs<std::vector<int>>(vecBack);
    CHECK(values.has_value());
    CHECK(values->size() == 3 && (*values)[2] == 9);

    // EnvelopeAll：保留类型信封，读回仍是扩展
    {
        vt::JsonWriteOptions typed;
        typed.extensions = vt::ExtensionPolicy::EnvelopeAll;
        const std::string typedText = vt::json::write(vec, typed);
        CHECK(typedText.find("\"$type\":\"std::vector<int>\"") != std::string::npos);
        const vt::Variant typedBack = vt::json::read(typedText);
        CHECK(typedBack.isExtension());
        CHECK(vt::getAs<std::vector<int>>(typedBack).has_value());
    }

    // 指针类型（所有权由测试自己管理）
    PNode* original = new PNode{11, "alpha"};
    const vt::Variant node = vt::Variant::fromValue(original);
    vt::Variant nodeBack = vt::json::read(vt::json::write(node));
    CHECK(nodeBack.isExtension());
    PNode* decodedNode = *nodeBack.extensionPtr<PNode*>();
    CHECK(decodedNode != nullptr);
    CHECK_EQ(decodedNode->id, 11);
    CHECK_EQ(decodedNode->name, std::string("alpha"));
    delete decodedNode;
    delete original;

    // Reject 策略
    {
        vt::JsonWriteOptions options;
        options.extensions = vt::ExtensionPolicy::Reject;
        std::string out;
        vt::SerializeError error;
        CHECK(!vt::json::tryWrite(point, out, &error, options));
        CHECK(error.code == vt::SerializeErrorCode::UnsupportedValue);
    }

    // Flatten 策略（有损）
    {
        vt::JsonWriteOptions options;
        options.extensions = vt::ExtensionPolicy::Flatten;
        const std::string flat = vt::json::write(point, options);
        CHECK(flat.find("$type") == std::string::npos);
        const vt::Variant flatBack = vt::json::read(flat);
        CHECK(flatBack.isMap());
        CHECK_EQ(flatBack.at("x").asInt(), 3);
    }

    // 未知类型
    CHECK(expectJsonError("{\"$vt\":\"1\",\"$type\":\"Nope\",\"$data\":1}",
                          vt::SerializeErrorCode::UnknownType));

    // 关闭信封识别：按普通 map 读取
    {
        vt::JsonReadOptions options;
        options.detectExtensions = false;
        const vt::Variant plain = vt::json::read(text, options);
        CHECK(plain.isMap());
        CHECK_EQ(plain.asMap().size(), std::size_t(3));
    }
}

void testBinaryBuiltins()
{
    const std::vector<vt::Variant> values{
        vt::Variant(),
        vt::Variant(true),
        vt::Variant(false),
        vt::Variant(std::int64_t(0)),
        vt::Variant(std::numeric_limits<std::int64_t>::min()),
        vt::Variant(std::numeric_limits<std::int64_t>::max()),
        vt::Variant(0.0),
        vt::Variant(-0.0),
        vt::Variant(0.1),
        vt::Variant(std::string("")),
        vt::Variant(std::string("中文 😀")),
        vt::Variant(vt::VariantList{}),
        vt::Variant(vt::VariantMap{}),
        vt::Variant(vt::VariantList{1, vt::VariantMap{{"a", vt::VariantList{true, "x"}}}}),
    };

    for (const vt::Variant& value : values) {
        const std::string blob = vt::binary::write(value);
        CHECK(blob.size() >= 4);
        CHECK_EQ(blob[0], 'V');
        CHECK_EQ(blob[1], 'T');
        CHECK_EQ(blob[2], 'B');
        CHECK_EQ(blob[3], '\x01');
        const vt::Variant back = vt::binary::read(blob);
        CHECK(back == value);
        if (value.isDouble())
            CHECK(back.isDouble());
    }

    // 非法 UTF-8 不能写出
    {
        vt::Variant bad = vt::Variant(std::string("\xC0\xAF"));
        std::string out;
        vt::SerializeError error;
        CHECK(!vt::binary::tryWrite(bad, out, &error));
        CHECK(error.code == vt::SerializeErrorCode::InvalidUtf8);
    }
}

void testBinaryErrors()
{
    const std::string blob = vt::binary::write(vt::Variant(vt::VariantMap{{"k", 1}}));

    // 截断：任何前缀都必须失败
    for (std::size_t i = 0; i < blob.size(); ++i) {
        vt::Variant value;
        vt::SerializeError error;
        CHECK(!vt::binary::tryRead(std::string_view(blob.data(), i), value, &error));
    }

    // 坏 magic
    {
        std::string bad = blob;
        bad[0] = 'X';
        vt::Variant value;
        vt::SerializeError error;
        CHECK(!vt::binary::tryRead(bad, value, &error));
        CHECK(error.code == vt::SerializeErrorCode::Syntax);
    }

    // 版本不匹配
    {
        std::string bad = blob;
        bad[3] = 0x02;
        vt::Variant value;
        vt::SerializeError error;
        CHECK(!vt::binary::tryRead(bad, value, &error));
        CHECK(error.code == vt::SerializeErrorCode::VersionMismatch);
    }

    // 尾随数据
    {
        std::string trailing = blob;
        trailing.push_back('\0');
        vt::Variant value;
        vt::SerializeError error;
        CHECK(!vt::binary::tryRead(trailing, value, &error));
        CHECK(error.code == vt::SerializeErrorCode::TrailingData);
    }

    // 长度炸弹：巨大 string 长度，不能分配
    {
        std::string bomb;
        bomb.push_back('V');
        bomb.push_back('T');
        bomb.push_back('B');
        bomb.push_back('\x01');
        bomb.push_back('\x05');            // string tag
        for (int i = 0; i < 9; ++i)
            bomb.push_back('\xFF');
        bomb.push_back('\x01');            // varint 巨大长度
        vt::Variant value;
        vt::SerializeError error;
        CHECK(!vt::binary::tryRead(bomb, value, &error));
        CHECK(error.code == vt::SerializeErrorCode::BadLength);
    }

    // 写入深度
    {
        vt::Variant nested = vt::Variant(vt::VariantList{});
        for (int i = 0; i < 300; ++i)
            nested = vt::Variant(vt::VariantList{std::move(nested)});
        vt::BinaryWriteOptions options;
        options.maxDepth = 256;
        std::string out;
        vt::SerializeError error;
        CHECK(!vt::binary::tryWrite(nested, out, &error, options));
        CHECK(error.code == vt::SerializeErrorCode::DepthExceeded);
    }
}

void testBinaryExtensions()
{
    const vt::Variant vec = vt::Variant::fromValue(std::vector<int>{1, 2, 3});
    const vt::Variant back = vt::binary::read(vt::binary::write(vec));
    CHECK(back.isExtension());
    auto values = back.get<std::vector<int>>();
    CHECK(values.has_value());
    CHECK(values->size() == 3 && (*values)[0] == 1);

    // 未知类型名
    std::string blob = vt::binary::write(vt::Variant());
    // 手写：header + extension tag + name "Nope" + null
    blob.assign("\x56\x54\x42\x01", 4);
    blob.push_back('\x08');        // extension
    blob.push_back(4);             // name length
    blob.append("Nope", 4);
    blob.push_back('\x00');        // null payload
    vt::Variant value;
    vt::SerializeError error;
    CHECK(!vt::binary::tryRead(blob, value, &error));
    CHECK(error.code == vt::SerializeErrorCode::UnknownType);
}

vt::Variant randomValue(std::mt19937& rng, int depth)
{
    int kind = static_cast<int>(rng() % 8);
    if (depth >= 3)
        kind = static_cast<int>(rng() % 5);

    switch (kind) {
    case 0:
        return vt::Variant();
    case 1:
        return vt::Variant((rng() & 1) != 0);
    case 2:
        return vt::Variant(static_cast<std::int64_t>(rng()) - 2147483648LL);
    case 3: {
        const double value = static_cast<double>(static_cast<int>(rng() % 2000) - 1000) / 8.0;
        return vt::Variant(value);
    }
    case 4: {
        static const char* pool[] = {"", "alpha", "中文", "a\"b", "line\n", "😀"};
        return vt::Variant(std::string(pool[rng() % 6]));
    }
    case 5: {
        vt::VariantList list;
        const std::size_t count = rng() % 4;
        for (std::size_t i = 0; i < count; ++i)
            list.push_back(randomValue(rng, depth + 1));
        return vt::Variant(std::move(list));
    }
    case 6: {
        vt::VariantMap map;
        const std::size_t count = rng() % 3;
        for (std::size_t i = 0; i < count; ++i)
            map["k" + std::to_string(i)] = randomValue(rng, depth + 1);
        return vt::Variant(std::move(map));
    }
    default:
        return vt::Variant(static_cast<std::int64_t>(rng()));
    }
}

void testStreamIO()
{
    const vt::Variant value = vt::VariantMap{
        {"a", 1},
        {"b", vt::VariantList{true, "x"}},
    };

    std::ostringstream jsonOut;
    CHECK(vt::json::writeTo(jsonOut, value));
    std::istringstream jsonIn(jsonOut.str());
    vt::Variant back;
    CHECK(vt::json::readFrom(jsonIn, back));
    CHECK(back == value);

    std::ostringstream binOut;
    CHECK(vt::binary::writeTo(binOut, value));
    std::istringstream binIn(binOut.str());
    CHECK(vt::binary::readFrom(binIn, back));
    CHECK(back == value);

    std::istringstream empty("");
    vt::SerializeError error;
    CHECK(!vt::json::readFrom(empty, back, &error));
    CHECK(error.code == vt::SerializeErrorCode::UnexpectedEnd);
}

void testFileIO()
{
    const std::string jsonPath = "vt_serial_test.json";
    const std::string binPath = "vt_serial_test.bin";
    const vt::Variant value = vt::VariantMap{
        {"name", "demo"},
        {"ids", vt::Variant::fromValue(std::vector<int>{1, 2, 3})},
        {"nested", vt::VariantList{true, 2.5, vt::Variant()}},
    };

    vt::SerializeError error;
    CHECK(vt::json::tryWriteFile(jsonPath, value, &error));
    vt::Variant back;
    CHECK(vt::json::tryReadFile(jsonPath, back, &error));
    CHECK(back.at("name").asString() == std::string("demo"));
    CHECK(vt::getAs<std::vector<int>>(back.at("ids")).has_value());

    CHECK(vt::binary::tryWriteFile(binPath, value, &error));
    CHECK(vt::binary::tryReadFile(binPath, back, &error));
    CHECK(back.at("ids").isExtension());
    CHECK(vt::getAs<std::vector<int>>(back.at("ids")).has_value());

    // 抛出版本：JSON 与二进制
    vt::json::writeFile("vt_serial_test_throw.json", value);
    const vt::Variant thrownBack = vt::json::readFile("vt_serial_test_throw.json");
    CHECK(thrownBack.at("name").asString() == std::string("demo"));

    vt::binary::writeFile("vt_serial_test_bin_throw.bin", value);
    const vt::Variant binThrown = vt::binary::readFile("vt_serial_test_bin_throw.bin");
    CHECK(binThrown.at("ids").isExtension());

    // 文件不存在
    CHECK(!vt::json::tryReadFile("vt_no_such_file_xyz.json", back, &error));
    CHECK(error.code == vt::SerializeErrorCode::IoError);

    // 目录不存在
    CHECK(!vt::json::tryWriteFile("vt_no_such_dir/out.json", value, &error));
    CHECK(error.code == vt::SerializeErrorCode::IoError);

    // 序列化失败时不应创建文件
    struct Unregistered {};
    const std::string noFile = "vt_should_not_exist.json";
    std::remove(noFile.c_str());
    CHECK(!vt::json::tryWriteFile(noFile, vt::Variant::fromValue(Unregistered{}), &error));
    CHECK(error.code == vt::SerializeErrorCode::UnknownType);
    std::ifstream check(noFile, std::ios::binary);
    CHECK(!check.good());

    std::remove(jsonPath.c_str());
    std::remove(binPath.c_str());
    std::remove("vt_serial_test_throw.json");
    std::remove("vt_serial_test_bin_throw.bin");
}

void testJsonIoEdgeCases()
{
    struct Unregistered {};

    // 流：序列化失败不应向流写入任何东西
    {
        std::ostringstream os;
        vt::SerializeError error;
        const bool ok = vt::json::writeTo(os, vt::Variant::fromValue(Unregistered{}), &error);
        CHECK(!ok);
        CHECK(error.code == vt::SerializeErrorCode::UnknownType);
        CHECK(os.str().empty());
    }

    // 流：pretty + EnvelopeAll 选项
    {
        vt::JsonWriteOptions options;
        options.pretty = true;
        options.extensions = vt::ExtensionPolicy::EnvelopeAll;
        const vt::Variant vec = vt::Variant::fromValue(std::vector<int>{1, 2, 3});

        std::ostringstream os;
        CHECK(vt::json::writeTo(os, vec, nullptr, options));
        const std::string text = os.str();
        CHECK(text.find("$type") != std::string::npos);
        CHECK(text.find('\n') != std::string::npos);

        std::istringstream is(text);
        vt::Variant back;
        CHECK(vt::json::readFrom(is, back));
        CHECK(back.isExtension());
        CHECK(vt::getAs<std::vector<int>>(back).has_value());
    }

    // 流：malformed JSON 要带 offset / line
    {
        std::istringstream is("{\"a\":1,}");
        vt::Variant value;
        vt::SerializeError error;
        CHECK(!vt::json::readFrom(is, value, &error));
        CHECK(error.code == vt::SerializeErrorCode::Syntax);
        CHECK(error.offset > 0);
        CHECK(error.line >= 1);
    }

    // 流：尾随数据
    {
        std::istringstream is("1 x");
        vt::Variant value;
        vt::SerializeError error;
        CHECK(!vt::json::readFrom(is, value, &error));
        CHECK(error.code == vt::SerializeErrorCode::TrailingData);
    }

    // 流：关闭信封识别
    {
        const std::string envelope =
            R"({"$vt":"1","$type":"Point2","$data":{"x":1,"y":2}})";
        std::istringstream is(envelope);
        vt::JsonReadOptions options;
        options.detectExtensions = false;
        vt::Variant value;
        CHECK(vt::json::readFrom(is, value, nullptr, options));
        CHECK(value.isMap());
        CHECK_EQ(value.asMap().size(), std::size_t(3));
    }

    // 流：信封版本不支持 -> VersionMismatch（与二进制头行为一致，不能退化成 map）
    {
        std::istringstream is(R"({"$vt":"2","$type":"Point2","$data":{"x":1}})");
        vt::Variant value;
        vt::SerializeError error;
        CHECK(!vt::json::readFrom(is, value, &error));
        CHECK(error.code == vt::SerializeErrorCode::VersionMismatch);
    }

    // 流：三个同名键但 $vt 不是字符串 -> 仍按普通 map 读
    {
        std::istringstream is(R"({"$vt":1,"$type":"x","$data":2})");
        vt::Variant value;
        CHECK(vt::json::readFrom(is, value));
        CHECK(value.isMap());
    }

    // 文件：UTF-8 字节与默认 write 完全一致（无 BOM、无换行转换）
    {
        const std::string path = "vt_serial_utf8.json";
        const vt::Variant value = vt::VariantMap{{"text", std::string("中文 😀")}};
        CHECK(vt::json::tryWriteFile(path, value));

        std::ifstream file(path, std::ios::binary);
        const std::string raw((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
        CHECK(raw == vt::json::write(value));

        const vt::Variant back = vt::json::readFile(path);
        CHECK(back.at("text").asString() == std::string("中文 😀"));
        std::remove(path.c_str());
    }

    // 文件：writeFile 选项 + 读回精确类型
    {
        const std::string path = "vt_serial_pretty.json";
        vt::JsonWriteOptions options;
        options.pretty = true;
        options.extensions = vt::ExtensionPolicy::EnvelopeAll;
        const vt::Variant vec = vt::Variant::fromValue(std::vector<int>{7, 8});

        vt::json::writeFile(path, vec, options);

        std::ifstream file(path, std::ios::binary);
        const std::string raw((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
        CHECK(raw.find("$type") != std::string::npos);
        CHECK(raw.find('\n') != std::string::npos);

        const vt::Variant back = vt::json::readFile(path);
        CHECK(back.isExtension());
        CHECK(vt::getAs<std::vector<int>>(back).has_value());
        std::remove(path.c_str());
    }

    // 文件：覆盖写会截断旧内容
    {
        const std::string path = "vt_serial_overwrite.json";
        vt::json::writeFile(path, vt::VariantMap{{"a", 1}, {"b", 2}});
        vt::json::writeFile(path, vt::VariantMap{{"a", 1}});
        const vt::Variant back = vt::json::readFile(path);
        CHECK_EQ(back.asMap().size(), std::size_t(1));
        std::remove(path.c_str());
    }

    // 文件：空文件 -> UnexpectedEnd
    {
        const std::string path = "vt_serial_empty.json";
        {
            std::ofstream file(path, std::ios::binary);
        }
        vt::Variant value;
        vt::SerializeError error;
        CHECK(!vt::json::tryReadFile(path, value, &error));
        CHECK(error.code == vt::SerializeErrorCode::UnexpectedEnd);
        std::remove(path.c_str());
    }

    // 文件：抛出版本遇到 IO 错误会抛 SerializeException(IoError)
    {
        bool writeThrew = false;
        try {
            vt::json::writeFile("vt_no_such_dir/out.json", vt::Variant(1));
        } catch (const vt::SerializeException& e) {
            writeThrew = e.info().code == vt::SerializeErrorCode::IoError;
        }
        CHECK(writeThrew);

        bool readThrew = false;
        try {
            vt::json::readFile("vt_no_such_file_xyz.json");
        } catch (const vt::SerializeException& e) {
            readThrew = e.info().code == vt::SerializeErrorCode::IoError;
        }
        CHECK(readThrew);
    }

    // 文件：路径是目录 -> 打开失败，报 IoError（不崩溃）
    {
        const std::filesystem::path dir = "vt_serial_dir_probe";
        std::filesystem::create_directory(dir);
        vt::Variant value;
        vt::SerializeError error;
        CHECK(!vt::json::tryWriteFile(dir.string(), vt::Variant(1), &error));
        CHECK(error.code == vt::SerializeErrorCode::IoError);
        CHECK(!vt::json::tryReadFile(dir.string(), value, &error));
        CHECK(error.code == vt::SerializeErrorCode::IoError);
        std::filesystem::remove(dir);
    }

    // 文件：非 ASCII 路径（std::string 按 UTF-8 解释）
    {
        const std::string path = "vt_序列化_测试.json";
        const vt::Variant value = vt::VariantMap{{"名字", std::string("中文")}};
        vt::SerializeError error;
        CHECK(vt::json::tryWriteFile(path, value, &error));
        vt::Variant back;
        CHECK(vt::json::tryReadFile(path, back, &error));
        CHECK(back.at("名字").asString() == std::string("中文"));
        std::filesystem::remove(std::filesystem::u8path(path));
    }

    // 文件：较大负载（2 MiB）完整往返，验证 readFileAll/writeFileAll 无截断
    {
        const std::string path = "vt_serial_large.json";
        const std::string big(2u * 1024u * 1024u, 'x');
        vt::SerializeError error;
        CHECK(vt::json::tryWriteFile(path, vt::Variant(big), &error));
        vt::Variant back;
        CHECK(vt::json::tryReadFile(path, back, &error));
        CHECK(back.isString());
        CHECK(back.asString() == big);
        std::remove(path.c_str());
    }

#ifdef _WIN32
    // 文件：只读文件写入失败 -> IoError（权限错误注入）
    {
        const std::string path = "vt_serial_readonly.json";
        CHECK(vt::json::tryWriteFile(path, vt::Variant(1)));
        std::error_code ec;
        std::filesystem::permissions(std::filesystem::u8path(path),
                                     std::filesystem::perms::owner_read,
                                     std::filesystem::perm_options::replace, ec);
        CHECK(!ec);

        vt::SerializeError error;
        CHECK(!vt::json::tryWriteFile(path, vt::Variant(2), &error));
        CHECK(error.code == vt::SerializeErrorCode::IoError);

        // 恢复写权限后才能删除（Windows 的只读文件删不掉）
        std::filesystem::permissions(
            std::filesystem::u8path(path),
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, ec);
        std::filesystem::remove(std::filesystem::u8path(path), ec);
    }
#endif
}

void testRandomRoundTrip()
{
    std::mt19937 rng(20261031);
    for (int i = 0; i < 500; ++i) {
        const vt::Variant value = randomValue(rng, 0);
        const vt::Variant jsonBack = vt::json::read(vt::json::write(value));
        const vt::Variant binaryBack = vt::binary::read(vt::binary::write(value));
        CHECK(jsonBack == value);
        CHECK(binaryBack == value);
        CHECK(jsonBack == binaryBack);
    }
}

void testRandomBytesNoCrash()
{
    std::mt19937 rng(424242);
    for (int i = 0; i < 2000; ++i) {
        const std::size_t length = rng() % 64;
        std::string bytes;
        bytes.reserve(length);
        for (std::size_t k = 0; k < length; ++k)
            bytes.push_back(static_cast<char>(rng() % 256));

        vt::Variant value;
        vt::SerializeError error;
        if (vt::json::tryRead(bytes, value, &error)) {
            const std::string again = vt::json::write(value);
            vt::Variant reparsed;
            CHECK(vt::json::tryRead(again, reparsed));
        }
        if (vt::binary::tryRead(bytes, value, &error)) {
            const std::string again = vt::binary::write(value);
            vt::Variant reparsed;
            CHECK(vt::binary::tryRead(again, reparsed));
        }
    }
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    registerCodecs();

#define RUN(fn)                                                                 \
    do {                                                                        \
        std::printf("-- %s\n", #fn);                                            \
        try {                                                                   \
            fn();                                                               \
        } catch (const std::exception& e) {                                     \
            ++g_failures;                                                       \
            std::printf("[EXCEPTION] %s: %s\n", #fn, e.what());                 \
        } catch (...) {                                                         \
            ++g_failures;                                                       \
            std::printf("[EXCEPTION] %s: unknown\n", #fn);                      \
        }                                                                       \
    } while (0)

    RUN(testAdapters);
    RUN(testBuiltinSerialization);
    RUN(testNestedSerialization);
    RUN(testJsonBuiltins);
    RUN(testJsonPretty);
    RUN(testJsonErrors);
    RUN(testJsonExtensions);
    RUN(testBinaryBuiltins);
    RUN(testBinaryErrors);
    RUN(testBinaryExtensions);
    RUN(testStreamIO);
    RUN(testFileIO);
    RUN(testJsonIoEdgeCases);
    RUN(testRandomRoundTrip);
    RUN(testRandomBytesNoCrash);
#undef RUN

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
