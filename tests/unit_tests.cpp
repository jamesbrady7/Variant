// vt::Variant 单元测试。无外部框架：失败计数即退出码（0=全绿）。
#include "vt/vt.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace vt::literals;

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!((a) == (b))) {                                                   \
            ++g_failures;                                                      \
            std::printf("[FAIL] %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
        }                                                                      \
    } while (0)

struct Probe {
    int id = 0;
    std::string name;
};

struct Wrong {};

struct Keyed {
    int id = 0;
};

bool throwsCode(const std::function<void()>& fn, vt::VariantErrorCode expected)
{
    try {
        fn();
    } catch (const vt::VariantError& e) {
        return e.code() == expected;
    } catch (...) {
        return false;
    }
    return false;
}

void testConstruction()
{
    vt::Variant null;
    CHECK(null.isNull());

    vt::Variant b = true;
    CHECK(b.isBool());
    CHECK_EQ(b.asBool(), true);

    vt::Variant i = 42;
    CHECK(i.isInt());
    CHECK_EQ(i.asInt(), 42);

    vt::Variant d = 3.5;
    CHECK(d.isDouble());
    CHECK_EQ(d.asDouble(), 3.5);

    vt::Variant s = "hello";       // const char* 不应退化成 bool
    CHECK(s.isString());
    CHECK_EQ(s.asString(), std::string("hello"));

    vt::Variant sv = std::string_view("view");
    CHECK(sv.isString());
    CHECK_EQ(sv.asString(), std::string("view"));

    vt::Variant fromNullptr = nullptr;
    CHECK(fromNullptr.isNull());

    // 整型窄化 + 范围检查
    vt::Variant big = std::int64_t(1) << 40;
    CHECK(!big.get<int>().has_value());
    CHECK(big.get<std::int64_t>().has_value());

    // 无符号越界：必须响亮失败，不静默回绕成负数
    CHECK(throwsCode([] {
        vt::Variant v(std::uint64_t(1) << 63);
        (void)v;
    }, vt::VariantErrorCode::OutOfRange));
    CHECK(throwsCode([] {
        vt::Variant v = vt::Variant::fromValue(std::numeric_limits<std::uint64_t>::max());
        (void)v;
    }, vt::VariantErrorCode::OutOfRange));

    // 边界值 INT64_MAX 仍然接受
    // 注意：Variant maxUnsigned(std::uint64_t(...)) 会被解析成函数声明（most vexing parse）
    vt::Variant maxUnsigned(static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    CHECK_EQ(maxUnsigned.asInt(), std::numeric_limits<std::int64_t>::max());
    CHECK_EQ(maxUnsigned.get<std::uint64_t>().value_or(0),
             static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
}

void testStringLikeInputs()
{
    // 字符串字面量：const char* 构造 -> String（不是 Bool）
    vt::Variant literal = "hello";
    CHECK(literal.isString());
    CHECK_EQ(literal.asString(), std::string("hello"));

    vt::Variant empty = "";
    CHECK(empty.isString());
    CHECK_EQ(empty.asString(), std::string(""));

    // 空指针 -> Null（不是空字符串）
    const char* nullPtr = nullptr;
    vt::Variant fromNull = nullPtr;
    CHECK(fromNull.isNull());
    CHECK(vt::Variant::fromValue(nullPtr).isNull());
    CHECK(vt::Variant(nullptr).isNull());
    CHECK(vt::Variant::fromValue(nullptr).isNull());

    // char* / string_view / string 都落到 String
    char buffer[] = "buffer";
    char* mutablePtr = buffer;
    CHECK(vt::Variant(mutablePtr).isString());
    CHECK(vt::Variant::fromValue(mutablePtr).isString());
    CHECK(vt::Variant::fromValue(std::string_view("view")).isString());
    CHECK(vt::Variant(std::string_view("view")).isString());

    // 单字符 char 是整数，不是字符串
    vt::Variant character = 'a';
    CHECK(character.isInt());
    CHECK_EQ(character.asInt(), 97);
}

void testContainers()
{
    vt::VariantMap m{{"a", 1}, {"b", "text"}};
    CHECK_EQ(m.size(), std::size_t(2));
    CHECK(m.contains("a"));
    CHECK(!m.contains("missing"));

    vt::VariantList l{1, 2.5, "x", true};
    CHECK_EQ(l.size(), std::size_t(4));
    CHECK_EQ(l.at(0).asInt(), 1);
    CHECK_EQ(l.at(1).asDouble(), 2.5);
    CHECK_EQ(l.at(2).asString(), std::string("x"));
    CHECK(l.at(3).asBool());

    vt::Variant asMap(std::move(m));
    CHECK(asMap.isMap());
    CHECK_EQ(asMap.asMap().at("a").asInt(), 1);

    vt::Variant asList(std::move(l));
    CHECK(asList.isList());
    CHECK_EQ(asList.asList().size(), std::size_t(4));
}

void testMoves()
{
    // 容器类没有手写移动，但隐式移动构造/赋值是存在的（未被拷贝/析构声明抑制）。
    static_assert(std::is_move_constructible_v<vt::VariantMap>);
    static_assert(std::is_move_assignable_v<vt::VariantMap>);
    static_assert(std::is_move_constructible_v<vt::VariantList>);
    static_assert(std::is_move_assignable_v<vt::VariantList>);
    static_assert(std::is_move_constructible_v<vt::Variant>);
    static_assert(std::is_move_assignable_v<vt::Variant>);

    // 容器自身的移动
    vt::VariantMap map;
    map["k"] = 1;
    vt::VariantMap movedMap = std::move(map);
    CHECK_EQ(movedMap.at("k").asInt(), 1);

    vt::VariantList list{1, 2, 3};
    vt::VariantList movedList = std::move(list);
    CHECK_EQ(movedList.size(), std::size_t(3));

    // Variant(VariantMap value) 是按值参数：传右值 -> 移动，不深拷贝
    vt::VariantMap big;
    big["x"] = 42;
    vt::Variant v = std::move(big);
    CHECK(v.isMap());
    CHECK_EQ(v.at("x").asInt(), 42);

    // 工厂同样按值接收
    vt::VariantList items{7, 8};
    vt::Variant fromFactory = vt::Variant::fromValue(std::move(items));
    CHECK(fromFactory.isList());
    CHECK_EQ(fromFactory.asList().size(), std::size_t(2));

    // 传左值则是拷贝（符合值语义）
    vt::VariantMap source;
    source["y"] = 1;
    vt::Variant copied = source;
    copied["y"] = 2;
    CHECK_EQ(source.at("y").asInt(), 1);
    CHECK_EQ(copied.at("y").asInt(), 2);
}

void testKeyVsPathOnVariant()
{
    vt::Variant v;
    v["a.b"] = 1;             // 字符串 = 字面键
    v["a.b"_path] = 2;        // Path = 路径 a -> b

    CHECK_EQ(v.at("a.b").asInt(), 1);          // 字面键 "a.b"
    CHECK_EQ(v.at("a.b"_path).asInt(), 2);     // 路径 a -> b
    CHECK_EQ(v.at("a").asMap().at("b").asInt(), 2);
    CHECK(v.find("a.b") != nullptr);
    CHECK(v.find("a")->isMap());
}

void testPathCreateAndRead()
{
    vt::Variant root;
    root["root.subtree_1.subsubtree_1"_path][0] = 42;
    root["root.subtree_1.subsubtree_1"_path][1] = "second";

    CHECK(root.isMap());
    CHECK(root.find("root.subtree_1.subsubtree_1[0]"_path) != nullptr);
    CHECK_EQ(root.getAt<int>("root.subtree_1.subsubtree_1[0]"_path).value_or(-1), 42);
    CHECK_EQ(root.getAt<std::string>("root.subtree_1.subsubtree_1[1]"_path).value_or(""),
             std::string("second"));

    // 字符串是字面键：整串带点号的键不存在
    CHECK(root.find("root.subtree_1.subsubtree_1[0]") == nullptr);

    // 空路径 = 自身
    CHECK(root.find(vt::Path()) == &root);

    // 预编译路径复用
    const vt::Path p = vt::Path::parse("root.subtree_1.subsubtree_1[0]");
    CHECK_EQ(root.at(p).asInt(), 42);
}

void testPathOnMap()
{
    // VariantMap 自己就是路径访问的根：路径相对于这个 map。
    vt::VariantMap m;
    m["a.b"_path][0] = 5;
    m["a.b"_path][1] = "x";

    CHECK(m.contains("a.b[0]"_path));
    CHECK_EQ(m.getAt<int>("a.b[0]"_path).value_or(-1), 5);
    CHECK_EQ(m.at("a.b[1]"_path).asString(), std::string("x"));
    CHECK(m.find("a.b[2]"_path) == nullptr);

    // 字面键那一侧
    CHECK(!m.contains("a.b"));
    CHECK(m.find("a") != nullptr);

    // const 版本：路径读不修改 map，返回拷贝
    const vt::VariantMap& cm = m;
    CHECK_EQ(cm["a.b[0]"_path].asInt(), 5);
    CHECK(cm.find("missing"_path) == nullptr);

    // 包成 Variant 是【深拷贝】：之后两边互不影响。
    vt::Variant v = m;
    v["a.b"_path][0] = 9;
    CHECK_EQ(m.at("a.b[0]"_path).asInt(), 5);
    CHECK_EQ(v.at("a.b[0]"_path).asInt(), 9);

    // 空路径：Variant 表示自身；VariantMap 没有"自身"这个节点。
    CHECK(m.find(vt::Path()) == nullptr);
    CHECK(throwsCode([&] { m.at(vt::Path()); }, vt::VariantErrorCode::EmptyPath));

    // 异常码与 Variant::at 一致（不是笼统的 KeyNotFound）
    CHECK(throwsCode([&] { m.at("missing.x"_path); }, vt::VariantErrorCode::KeyNotFound));
    CHECK(throwsCode([&] { m.at("a.b.0"_path); }, vt::VariantErrorCode::NotAMap));
    CHECK(throwsCode([&] { m.at("a[0]"_path); }, vt::VariantErrorCode::NotAList));
}

void testLiteralVsPath()
{
    vt::VariantMap m;
    m["a.b"] = 1;          // 字面键 "a.b"
    m["a"]["b"] = 2;       // 字面键 a -> 字面键 b
    m["a.b"_path] = 3;     // 路径 a -> b（覆盖到 m["a"]["b"]）

    CHECK(m.contains("a.b"));
    CHECK(m.contains("a.b"_path));
    CHECK_EQ(m.at("a.b").asInt(), 1);
    CHECK_EQ(m.at("a.b"_path).asInt(), 3);
    CHECK_EQ(m.at("a").asMap().at("b").asInt(), 3);
    CHECK_EQ(m.size(), std::size_t(2));   // 顶层只有 "a.b" 和 "a"

    // 路径语法也能表达字面键："[\"a.b\"]" 明确要求一个键段
    CHECK_EQ(m[R"(["a.b"])"_path].asInt(), 1);

    CHECK(m.erase("a.b"));
    CHECK(!m.contains("a.b"));
    CHECK(m.contains("a.b"_path));         // 路径 a -> b 仍在

    CHECK(m.erase("a"));
    CHECK(!m.contains("a.b"_path));
}

void testPathQuotedKeys()
{
    vt::Variant root;
    root[R"(cfg["a.b"]["c[0]"])"_path] = 7;
    CHECK_EQ(root.getAt<int>(R"(cfg["a.b"]["c[0]"])"_path).value_or(-1), 7);

    const auto& cfg = root.at("cfg");      // 字符串 = 字面键
    CHECK(cfg.isMap());
    CHECK_EQ(cfg.at(R"(["a.b"]["c[0]"])"_path).asInt(), 7);

    // 字面键里带点号：直接用字符串即可
    vt::VariantMap m;
    m["a.b"] = 9;
    CHECK_EQ(m.find("a.b")->asInt(), 9);
    CHECK(m.find("a.b"_path) == nullptr);   // 路径 a -> b 不存在
}

void testPathOnList()
{
    vt::VariantList list;
    list[vt::Path::parse("[0].name")] = "first";
    list[vt::Path::parse("[1]")] = 42;

    CHECK(list.contains(vt::Path::parse("[0].name")));
    CHECK_EQ(list.at(vt::Path::parse("[0].name")).asString(), std::string("first"));
    CHECK_EQ(list.getAt<int>(vt::Path::parse("[1]")).value_or(-1), 42);
    CHECK(list.find(vt::Path::parse("[2]")) == nullptr);

    CHECK(throwsCode([&] { list.at(vt::Path::parse("[5]")); },
                     vt::VariantErrorCode::IndexOutOfBounds));
    CHECK(throwsCode([&] { list.at(vt::Path::parse("name")); },
                     vt::VariantErrorCode::NotAList));
}

void testPathErrors()
{
    vt::Variant root;
    root["a.b"_path] = 1;

    CHECK(throwsCode([&] { root.at("a.c"_path); }, vt::VariantErrorCode::KeyNotFound));
    CHECK(throwsCode([&] { root.at("a.b.c"_path); }, vt::VariantErrorCode::NotAMap));
    CHECK(throwsCode([&] { root.at("a[0]"_path); }, vt::VariantErrorCode::NotAList));
    CHECK(throwsCode([&] { const_cast<const vt::Variant&>(root).at("x..y"_path); },
                     vt::VariantErrorCode::SyntaxError));

    // 非抛出接口
    CHECK(root.find("nope"_path) == nullptr);
    CHECK_EQ(root.getAt<int>("a.b"_path).value_or(-1), 1);       // 存在且类型正确
    CHECK(!root.getAt<std::string>("a.b"_path).has_value());     // 类型不符 -> nullopt
    CHECK_EQ(root.getAtOr<std::string>("a.b"_path, "fallback"), std::string("fallback"));
    CHECK(!root.set("a.b.c"_path, 5));                           // b 是 int，不能继续下钻
    CHECK(root.set("a.b"_path, 2));
    CHECK_EQ(root.at("a.b"_path).asInt(), 2);

    // 下标越界
    vt::Variant list = vt::VariantList{1};
    CHECK(throwsCode([&] { list[3]; }, vt::VariantErrorCode::IndexOutOfBounds));
    CHECK(throwsCode([&] { list.at(3); }, vt::VariantErrorCode::IndexOutOfBounds));
}

void testCow()
{
    vt::Variant original = vt::VariantMap{{"k", 1}, {"nested", vt::VariantMap{{"x", 10}}}};
    vt::Variant copy = original;

    copy["k"] = 2;
    copy["nested.x"_path] = 20;

    CHECK_EQ(original.at("k").asInt(), 1);
    CHECK_EQ(original.at("nested.x"_path).asInt(), 10);
    CHECK_EQ(copy.at("k").asInt(), 2);
    CHECK_EQ(copy.at("nested.x"_path).asInt(), 20);

    // 只读走 const，不 detach、不改内容
    const vt::Variant& constRef = original;
    CHECK_EQ(constRef["k"].asInt(), 1);
}

void testVectorPayload()
{
    // 密集数值的正确形态：一个 Variant 装一个数组，而不是每个元素一个 Variant。
    std::vector<int> data{1, 2, 3};
    vt::Variant v = vt::Variant::fromValue(std::move(data));

    CHECK(v.isExtension());
    CHECK(!v.typeName().empty());

    // 取值（拷贝整个 vector）
    auto copied = v.get<std::vector<int>>();
    CHECK(copied.has_value());
    CHECK_EQ(copied->size(), std::size_t(3));
    CHECK_EQ((*copied)[2], 3);

    // 零拷贝读取：必须走 const 引用，否则非 const extensionAny() 会 detach
    const vt::Variant& cv = v;
    const std::vector<int>* p = cv.extensionPtr<std::vector<int>>();
    CHECK(p != nullptr);
    CHECK_EQ(p->size(), std::size_t(3));

    // Variant 拷贝共享扩展盒；非 const 访问触发 detach（深拷贝 vector）
    vt::Variant shared = v;
    shared.extensionPtr<std::vector<int>>()->push_back(4);
    CHECK_EQ(cv.extensionPtr<std::vector<int>>()->size(), std::size_t(3));
    CHECK_EQ(shared.extensionPtr<std::vector<int>>()->size(), std::size_t(4));
}

void testSharedPointerStorage()
{
    auto sp = std::make_shared<Probe>(Probe{7, "shared"});
    vt::Variant value = vt::Variant::fromValue(sp);
    CHECK(value.isExtension());

    auto back = value.get<std::shared_ptr<Probe>>();
    CHECK(back.has_value());
    CHECK(back->get() == sp.get());
    CHECK((*back)->name == std::string("shared"));

    // Variant 拷贝共享同一个控制块
    vt::Variant copy = value;
    auto back2 = copy.get<std::shared_ptr<Probe>>();
    CHECK(back2.has_value());
    CHECK(back2->get() == sp.get());
    CHECK(sp.use_count() >= 3);   // sp + value + copy（以及临时 optional）
}

void testExtension()
{
    CHECK(vt::TypeRegistry::instance().registerType<Probe>("Probe"));
    CHECK(!vt::TypeRegistry::instance().registerType<Probe>("Probe2"));  // 同类型改名失败
    CHECK(!vt::TypeRegistry::instance().registerType<Wrong>("Probe"));   // 占名失败

    CHECK(vt::TypeRegistry::instance().isRegistered<Probe>());
    CHECK(!vt::TypeRegistry::instance().isRegistered<Wrong>());

    vt::Variant v = vt::Variant::fromValue(Probe{7, "probe"});
    CHECK(v.isExtension());
    CHECK(v.is<Probe>());
    CHECK_EQ(v.typeName(), std::string("Probe"));

    const Probe* p = v.extensionPtr<Probe>();
    CHECK(p != nullptr);
    CHECK_EQ(p->id, 7);

    auto got = v.get<Probe>();
    CHECK(got.has_value());
    CHECK_EQ(got->name, std::string("probe"));

    // 未注册类型仍可存储，typeName 回退到 typeid 名
    vt::Variant raw = vt::Variant::fromValue(Wrong{});
    CHECK(raw.isExtension());
    CHECK(!raw.typeName().empty());
}

void testNestedContainers()
{
    // 1) 外层容器仍是紧凑扩展槽，归一化/反归一化按 traits 自动递归。
    const std::vector<std::vector<int>> grid{{1, 2}, {3}};
    vt::Variant v = vt::Variant::fromValue(grid);
    CHECK(v.isExtension());
    CHECK(v.is<std::vector<std::vector<int>>>());

    auto gridBack = vt::getAs<std::vector<std::vector<int>>>(v);
    CHECK(gridBack.has_value());
    CHECK_EQ(gridBack->size(), std::size_t(2));
    CHECK_EQ((*gridBack)[0][1], 2);
    CHECK_EQ((*gridBack)[1][0], 3);

    // 2) 纯 List 表示（JSON 读回/ Python 传入的形态）也能还原。
    vt::Variant plain = vt::VariantList{
        vt::VariantList{1, 2},
        vt::VariantList{3},
    };
    auto fromPlain = vt::getAs<std::vector<std::vector<int>>>(plain);
    CHECK(fromPlain.has_value());
    CHECK_EQ((*fromPlain)[1][0], 3);
    CHECK(!vt::getAs<std::vector<std::vector<int>>>(
               vt::Variant(vt::VariantList{vt::VariantList{1, "x"}}))
               .has_value());

    // 3) map<string, vector<int>>
    const std::map<std::string, std::vector<int>> named{{"a", {1, 2}}, {"b", {3}}};
    vt::Variant namedVar = vt::Variant::fromValue(named);
    CHECK(namedVar.isExtension());
    auto namedBack = vt::getAs<std::map<std::string, std::vector<int>>>(namedVar);
    CHECK(namedBack.has_value());
    CHECK_EQ((*namedBack)["a"].size(), std::size_t(2));

    // 4) vector<map<string,int>>
    const std::vector<std::map<std::string, int>> rows{{{"x", 1}}, {{"y", 2}}};
    auto rowsBack = vt::getAs<std::vector<std::map<std::string, int>>>(
        vt::Variant::fromValue(rows));
    CHECK(rowsBack.has_value());
    CHECK_EQ((*rowsBack)[1]["y"], 2);

    // 5) 整数键按十进制字符串写进 Map，读回按 key_type 解析。
    const std::map<int, std::vector<std::string>> byId{{7, {"a"}}, {8, {"b"}}};
    vt::Variant byIdVar = vt::Variant::fromValue(byId);
    CHECK(byIdVar.isExtension());
    auto byIdBack = vt::getAs<std::map<int, std::vector<std::string>>>(byIdVar);
    CHECK(byIdBack.has_value());
    CHECK_EQ((*byIdBack)[8][0], std::string("b"));

    // 6) pair / set / array
    const std::pair<int, std::string> pairValue{4, "four"};
    vt::Variant pairVar = vt::Variant::fromValue(pairValue);
    CHECK(pairVar.isExtension());
    auto pairBack = vt::getAs<std::pair<int, std::string>>(pairVar);
    CHECK(pairBack.has_value() && pairBack->first == 4 && pairBack->second == "four");

    const std::vector<std::pair<int, std::string>> pairList{{1, "a"}, {2, "b"}};
    auto pairListBack = vt::getAs<std::vector<std::pair<int, std::string>>>(
        vt::Variant::fromValue(pairList));
    CHECK(pairListBack.has_value());
    CHECK_EQ((*pairListBack)[1].second, std::string("b"));

    const std::set<int> ids{3, 1, 2};
    auto idsBack = vt::getAs<std::set<int>>(vt::Variant::fromValue(ids));
    CHECK(idsBack.has_value());
    CHECK_EQ(*idsBack->begin(), 1);

    using Triple = std::array<int, 3>;
    using Pair2 = std::array<int, 2>;
    const Triple triple{1, 2, 3};
    CHECK(vt::getAs<Triple>(vt::Variant::fromValue(triple)).has_value());
    CHECK(!vt::getAs<Pair2>(vt::Variant::fromValue(triple)).has_value());

    // 7) 归一化表已自动登记（Python 桥在扩展槽转换时用它）。
    CHECK(vt::detail::findNormalizer(std::type_index(typeid(std::vector<std::vector<int>>)))
          != nullptr);
    CHECK(vt::detail::findNormalizer(std::type_index(typeid(std::vector<std::map<std::string, int>>)))
          != nullptr);
    // 内层类型不单独登记：归一化递归在编译期完成，运行时只需外层入口。
    CHECK(vt::detail::findNormalizer(std::type_index(typeid(std::map<std::string, int>)))
          == nullptr);
    CHECK(vt::detail::findNormalizer(std::type_index(typeid(Probe))) == nullptr);

    // 7.5) 指针键 map：键不可字符串化 -> 自动退化成 [[k,v], ...]，键保留身份。
    Keyed first{1};
    Keyed second{2};
    const std::map<Keyed*, int> pointerMap{{&first, 10}, {&second, 20}};
    vt::Variant pointerVar = vt::Variant::fromValue(pointerMap);
    CHECK(pointerVar.isExtension());
    const vt::Variant normalized = vt::detail::normalizeToVariant(pointerMap);
    CHECK(normalized.isList());                       // 不是 Map，键类型没丢
    CHECK_EQ(normalized.asList().size(), std::size_t(2));
    CHECK(normalized.asList()[0].isList());           // [键, 值]
    auto pointerBack = vt::getAs<std::map<Keyed*, int>>(pointerVar);
    CHECK(pointerBack.has_value());
    CHECK_EQ(pointerBack->at(&second), 20);           // 指针原样还原

    // 8) 归一化只用于转换，不改写存储：扩展槽里仍是原对象。
    CHECK(vt::detail::normalizeToVariant(grid).isList());
    CHECK_EQ(vt::detail::normalizeToVariant(grid).asList().size(), std::size_t(2));

    // 9) vector<bool> 的元素是代理类型（_Bit_reference）：必须按 bool 事件发出，
    //    不能当成"未知叶子"塞进扩展槽（那样 getAs / JSON 都会失败）。
    using Bits = std::vector<bool>;
    const Bits bits{true, false, true};
    vt::Variant bitsVar = vt::Variant::fromValue(bits);
    CHECK(vt::detail::normalizeToVariant(bits).isList());
    CHECK_EQ(vt::detail::normalizeToVariant(bits).asList().size(), std::size_t(3));
    auto bitsBack = vt::getAs<Bits>(bitsVar);
    CHECK(bitsBack.has_value());
    CHECK_EQ((*bitsBack)[2], true);
}

void testDocument()
{
    VariantDocument doc;
    CHECK(doc.set("root.items[0]", 10));
    CHECK(doc.set("root.items[1]", "twenty"));
    CHECK(doc.set("root.name", "demo"));

    CHECK_EQ(doc.get("root.items[0]").asInt(), 10);
    CHECK_EQ(doc.get("root.name").asString(), std::string("demo"));
    CHECK(doc.get("root.missing").isNull());
    CHECK(doc.has("root.items[0]"));
    CHECK(!doc.has("root.missing"));

    // Path 重载：省掉每次解析
    CHECK_EQ(doc.get(vt::Path::parse("root.items[0]")).asInt(), 10);
    CHECK(doc.set(vt::Path::parse("root.viaPath"), 1));
    CHECK(doc.has(vt::Path::parse("root.viaPath")));

    CHECK(!doc.set("root.name.child", 1));   // name 是 string，不能再往下
    CHECK(!doc.lastError().empty());
    CHECK(doc.set("root.ok", true));
    CHECK(doc.lastError().empty());
}

void testEqualityAndDescribe()
{
    vt::Variant a = vt::VariantMap{{"x", 1}, {"y", vt::VariantList{true, "s"}}};
    vt::Variant b = vt::VariantMap{{"y", vt::VariantList{true, "s"}}, {"x", 1}};
    CHECK(a == b);
    b["x"] = 2;
    CHECK(a != b);

    vt::VariantList list{1, "x", true};
    CHECK_EQ(vt::describe(list), std::string("[1, \"x\", true]"));

    vt::Variant str = "a\"b";
    CHECK_EQ(vt::describe(str), std::string("\"a\\\"b\""));
}

void testPathRoundTrip()
{
    const char* samples[] = {
        "a.b.c",
        "a[0].b",
        "a[0][1]",
        R"(a["x.y"].z)",
        R"(["root key"][2])",
    };
    for (const char* text : samples) {
        const vt::Path path = vt::Path::parse(text);
        const vt::Path again = vt::Path::parse(path.toString());
        CHECK_EQ(again.segments().size(), path.segments().size());
    }
}

/// 记录事件流的测试用 Sink：验证"编译器算出来的键形态"确实交给了 sink。
class TraceSink final : public vt::detail::ValueSink {
public:
    explicit TraceSink(vt::detail::MapKeyForm wanted) : m_wanted(wanted) {}

    void onNull() override { m_trace += "null;"; }
    void onBool(bool) override { m_trace += "bool;"; }
    void onInt(std::int64_t value) override { m_trace += "int:" + std::to_string(value) + ";"; }
    void onDouble(double) override { m_trace += "double;"; }
    void onString(std::string_view value) override
    {
        m_trace += "str:";
        m_trace += std::string(value);
        m_trace += ";";
    }
    void onVariant(const vt::Variant&) override { m_trace += "variant;"; }
    void onExtension(const std::any&) override { m_trace += "ext;"; }

    void onListBegin(std::size_t) override { m_trace += "["; }
    void onListEnd() override { m_trace += "];"; }

    vt::detail::MapKeyForm onMapBegin(std::size_t, bool keyConvertible) override
    {
        m_convertible = keyConvertible;
        m_trace += "{";
        return m_wanted;
    }
    void onMapEnd() override { m_trace += "};"; }
    void onEntryBegin() override { m_trace += "entry("; }
    void onEntryEnd() override { m_trace += ");"; }

    const std::string& trace() const { return m_trace; }
    bool lastConvertible() const { return m_convertible; }

private:
    vt::detail::MapKeyForm m_wanted;
    std::string m_trace;
    bool m_convertible = false;
};

void testValueSink()
{
    // 1) 键可字符串化，但边界要 Typed（Python）：键按原类型发事件。
    {
        TraceSink sink(vt::detail::MapKeyForm::Typed);
        vt::detail::visitValue(std::map<int, std::string>{{7, "seven"}}, sink);
        CHECK_EQ(sink.trace(), std::string("{entry(int:7;str:seven;);};"));
        CHECK(sink.lastConvertible());
    }

    // 2) 同一个值，边界要 Text（序列化）：键是字符串事件。
    {
        TraceSink sink(vt::detail::MapKeyForm::Text);
        vt::detail::visitValue(std::map<int, std::string>{{7, "seven"}}, sink);
        CHECK_EQ(sink.trace(), std::string("{entry(str:7;str:seven;);};"));
    }

    // 3) 键不可字符串化：Text 请求无效，visitor 强制 Typed。
    {
        TraceSink sink(vt::detail::MapKeyForm::Text);
        vt::detail::visitValue(std::map<std::vector<int>, int>{{{1}, 2}}, sink);
        CHECK_EQ(sink.trace(), std::string("{entry([int:1;];int:2;);};"));
        CHECK(!sink.lastConvertible());
    }

    // 4) normalizeToVariant 仍是 VariantSink 的包装（序列化侧 Text 键）。
    const vt::Variant normalized = vt::detail::normalizeToVariant(std::map<int, int>{{7, 8}});
    CHECK(normalized.isMap());
    CHECK_EQ(normalized.asMap().at("7").asInt(), 8);
}

} // namespace

int main()
{
    // 崩溃时不要吞掉日志（块缓冲会在 abort 时丢失全部输出）。
    std::setvbuf(stdout, nullptr, _IONBF, 0);

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

    RUN(testConstruction);
    RUN(testStringLikeInputs);
    RUN(testContainers);
    RUN(testMoves);
    RUN(testKeyVsPathOnVariant);
    RUN(testPathCreateAndRead);
    RUN(testPathOnMap);
    RUN(testLiteralVsPath);
    RUN(testPathQuotedKeys);
    RUN(testPathOnList);
    RUN(testPathErrors);
    RUN(testCow);
    RUN(testVectorPayload);
    RUN(testSharedPointerStorage);
    RUN(testExtension);
    RUN(testNestedContainers);
    RUN(testValueSink);
    RUN(testDocument);
    RUN(testEqualityAndDescribe);
    RUN(testPathRoundTrip);
#undef RUN

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
