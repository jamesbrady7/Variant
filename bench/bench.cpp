// vt::Variant vs QVariant 基准。
// 目的不是"赢"，是量清楚封装成本的边界：哪种操作代价相当、哪种明显更贵。
#include "vt/vt.h"

#include <QVariant>
#include <QVariantMap>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace vt::literals;

volatile std::int64_t g_sink = 0;

template <class Fn>
double nsPerOp(std::size_t iters, Fn&& fn)
{
    const auto t0 = std::chrono::steady_clock::now();
    fn(iters);
    const auto t1 = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / static_cast<double>(iters);
}

template <class Fn>
double msPerOp(std::size_t reps, Fn&& fn)
{
    const auto t0 = std::chrono::steady_clock::now();
    fn(reps);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return ms / static_cast<double>(reps);
}

void row(const char* name, double vt, double qt)
{
    const double ratio = vt > 0.0 ? qt / vt : 0.0;
    std::printf("%-36s %12.2f %12.2f %9.2fx\n", name, vt, qt, ratio);
}

void section(const char* title)
{
    std::printf("\n== %s ==\n", title);
    std::printf("%-36s %12s %12s %10s\n", "scenario", "vt(ns/op)", "qt(ns/op)", "qt/vt");
}

void benchSizes()
{
    std::printf("sizeof(Variant)      = %zu\n", sizeof(vt::Variant));
    std::printf("sizeof(QVariant)     = %zu\n", sizeof(QVariant));
    std::printf("sizeof(VariantMap)   = %zu\n", sizeof(vt::VariantMap));
    std::printf("sizeof(QVariantMap)  = %zu\n", sizeof(QVariantMap));
    std::printf("sizeof(std::any)     = %zu\n", sizeof(std::any));
    std::printf("sizeof(std::string)  = %zu\n", sizeof(std::string));
    std::printf("sizeof(std::shared_ptr<void>) = %zu\n", sizeof(std::shared_ptr<void>));
}

void benchScalars()
{
    section("scalar copy + read");
    const std::size_t N = 3'000'000;

    row("copy int64 + asInt",
        nsPerOp(N, [&](std::size_t n) {
            vt::Variant src(std::int64_t(42));
            for (std::size_t i = 0; i < n; ++i) {
                vt::Variant c = src;
                g_sink += c.asInt();
            }
        }),
        nsPerOp(N, [&](std::size_t n) {
            QVariant src(42);
            for (std::size_t i = 0; i < n; ++i) {
                QVariant c = src;
                g_sink += c.toLongLong();
            }
        }));

    row("copy double + asDouble",
        nsPerOp(N, [&](std::size_t n) {
            vt::Variant src(3.1415926);
            for (std::size_t i = 0; i < n; ++i) {
                vt::Variant c = src;
                g_sink += static_cast<std::int64_t>(c.asDouble());
            }
        }),
        nsPerOp(N, [&](std::size_t n) {
            QVariant src(3.1415926);
            for (std::size_t i = 0; i < n; ++i) {
                QVariant c = src;
                g_sink += static_cast<std::int64_t>(c.toDouble());
            }
        }));

    const std::string shortText(8, 'x');
    const std::string longText(128, 'x');
    const QString qShort = QString::fromStdString(shortText);
    const QString qLong = QString::fromStdString(longText);

    row("copy 8-char string + read",
        nsPerOp(2'000'000, [&](std::size_t n) {
            vt::Variant src(shortText);
            for (std::size_t i = 0; i < n; ++i) {
                vt::Variant c = src;
                g_sink += static_cast<std::int64_t>(c.asString().size());
            }
        }),
        nsPerOp(2'000'000, [&](std::size_t n) {
            QVariant src(qShort);
            for (std::size_t i = 0; i < n; ++i) {
                QVariant c = src;
                g_sink += static_cast<std::int64_t>(c.toString().size());
            }
        }));

    row("copy 128-char string + read",
        nsPerOp(1'000'000, [&](std::size_t n) {
            vt::Variant src(longText);
            for (std::size_t i = 0; i < n; ++i) {
                vt::Variant c = src;
                g_sink += static_cast<std::int64_t>(c.asString().size());
            }
        }),
        nsPerOp(1'000'000, [&](std::size_t n) {
            QVariant src(qLong);
            for (std::size_t i = 0; i < n; ++i) {
                QVariant c = src;
                g_sink += static_cast<std::int64_t>(c.toString().size());
            }
        }));
}

struct QueryData {
    std::vector<std::string> keys;
    std::vector<QString> qkeys;
    vt::Variant vtMap;
    QVariantMap qtMap;
};

QueryData makeQueryData(std::size_t count)
{
    QueryData data;
    data.keys.reserve(count);
    data.qkeys.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        data.keys.push_back("key" + std::to_string(i));
        data.qkeys.push_back(QString::fromStdString(data.keys.back()));
    }

    vt::VariantMap m;
    m.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        m[data.keys[i]] = std::int64_t(i);
    data.vtMap = vt::Variant(std::move(m));

    for (std::size_t i = 0; i < count; ++i)
        data.qtMap.insert(data.qkeys[i], static_cast<qlonglong>(i));

    return data;
}

void benchMapBuild()
{
    section("map build (10k keys), ms per build");
    const std::size_t K = 10'000;
    const QueryData data = makeQueryData(K);

    const double vtBuild = msPerOp(20, [&](std::size_t reps) {
        for (std::size_t r = 0; r < reps; ++r) {
            vt::VariantMap m;
            m.reserve(K);
            for (std::size_t i = 0; i < K; ++i)
                m[data.keys[i]] = std::int64_t(i);
            vt::Variant v(std::move(m));
            g_sink += static_cast<std::int64_t>(v.asMap().size());
        }
    });

    const double qtBuild = msPerOp(20, [&](std::size_t reps) {
        for (std::size_t r = 0; r < reps; ++r) {
            QVariantMap m;
            for (std::size_t i = 0; i < K; ++i)
                m.insert(data.qkeys[i], static_cast<qlonglong>(i));
            QVariant v(m);
            g_sink += v.toMap().size();
        }
    });

    std::printf("%-36s %12.3f %12.3f %9.2fx\n", "build 10k-key map (ms)", vtBuild, qtBuild,
                vtBuild > 0 ? qtBuild / vtBuild : 0.0);
}

void benchMapLookup()
{
    section("map lookup");
    const std::size_t K = 10'000;
    const std::size_t N = 5'000'000;
    const QueryData data = makeQueryData(K);
    const vt::VariantMap& vtm = data.vtMap.asMap();

    row("hash lookup (10k-key map)",
        nsPerOp(N, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i) {
                const vt::Variant* v = vtm.find(data.keys[i % K]);
                g_sink += v ? v->asInt() : 0;
            }
        }),
        nsPerOp(N, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                g_sink += data.qtMap.value(data.qkeys[i % K]).toLongLong();
        }));

    row("copy 10k-key map (COW)",
        nsPerOp(N, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i) {
                vt::Variant c = data.vtMap;
                const vt::Variant& cc = c;   // 只读必须走 const：非 const asMap() 会 detach
                g_sink += static_cast<std::int64_t>(cc.asMap().size());
            }
        }),
        nsPerOp(N, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i) {
                QVariant c = QVariant(data.qtMap);
                g_sink += c.toMap().size();
            }
        }));
}

void benchNestedPath()
{
    section("nested access (5 levels: a.b.c.d.e)");
    const std::size_t N = 2'000'000;

    vt::Variant vtRoot;
    vtRoot["a.b.c.d.e"_path] = std::int64_t(7);
    const vt::Path prepared = vt::Path::parse("a.b.c.d.e");

    QVariantMap e; e.insert(QStringLiteral("e"), 7);
    QVariantMap d; d.insert(QStringLiteral("d"), e);
    QVariantMap c; c.insert(QStringLiteral("c"), d);
    QVariantMap b; b.insert(QStringLiteral("b"), c);
    QVariantMap a; a.insert(QStringLiteral("a"), b);
    const QVariant qtRoot(a);

    row("prepared Path + at",
        nsPerOp(N, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                g_sink += vtRoot.at(prepared).asInt();
        }),
        nsPerOp(N / 4, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i) {
                g_sink += qtRoot.toMap()
                              .value(QStringLiteral("a"))
                              .toMap()
                              .value(QStringLiteral("b"))
                              .toMap()
                              .value(QStringLiteral("c"))
                              .toMap()
                              .value(QStringLiteral("d"))
                              .toMap()
                              .value(QStringLiteral("e"))
                              .toLongLong();
            }
        }));

    row("parse path string every call",
        nsPerOp(N, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                g_sink += vtRoot.getAt<std::int64_t>("a.b.c.d.e"_path).value_or(0);
        }),
        0.0);
}

// ------------------------------------------------------------ 值遍历

/// @brief 手写等价基线：直接构建 List（改造前 normalizeToVariant 的做法）。
vt::Variant directVectorInt(const std::vector<int>& values)
{
    vt::VariantList list;
    list.reserve(values.size());
    for (int value : values)
        list.push_back(vt::Variant(value));
    return vt::Variant(std::move(list));
}

/// @brief 手写等价基线：嵌套 vector<vector<int>>。
vt::Variant directNestedInt(const std::vector<std::vector<int>>& outer)
{
    vt::VariantList list;
    list.reserve(outer.size());
    for (const auto& inner : outer) {
        vt::VariantList items;
        items.reserve(inner.size());
        for (int value : inner)
            items.push_back(vt::Variant(value));
        list.push_back(vt::Variant(std::move(items)));
    }
    return vt::Variant(std::move(list));
}

/// @brief 手写等价基线：map<string,int> -> Map（键原样）。
vt::Variant directStringMap(const std::map<std::string, int>& values)
{
    vt::VariantMap map;
    map.reserve(values.size());
    for (const auto& kv : values)
        map[kv.first] = vt::Variant(kv.second);
    return vt::Variant(std::move(map));
}

/// @brief 手写等价基线：map<int,int> -> Map（键写十进制文本）。
vt::Variant directIntMap(const std::map<int, int>& values)
{
    vt::VariantMap map;
    map.reserve(values.size());
    for (const auto& kv : values)
        map[std::to_string(kv.first)] = vt::Variant(kv.second);
    return vt::Variant(std::move(map));
}

void scanVariant(const vt::Variant& value)
{
    g_sink += static_cast<std::int64_t>(value.isList() ? value.asList().size()
                                                       : value.asMap().size());
}

void benchNormalize()
{
    std::printf("\n== value traversal: visitValue + VariantSink vs hand-written loop ==\n");
    std::printf("%-40s %12s %12s %12s\n", "scenario", "visit(ms)", "direct(ms)", "visit/direct");
    std::printf("  （direct = 改造前 normalizeToVariant 的写法；visit 走事件流，"
                "VariantSink 已模板化内联）\n");

    // 1) vector<int>：叶子最密，最能看出每元素一次虚调用的代价。
    {
        const std::vector<int> values = [] {
            std::vector<int> v;
            v.reserve(200'000);
            for (int i = 0; i < 200'000; ++i)
                v.push_back(i);
            return v;
        }();
        const std::size_t reps = 10;
        const double visitMs = msPerOp(reps, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                scanVariant(vt::detail::normalizeToVariant(values));
        });
        const double directMs = msPerOp(reps, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                scanVariant(directVectorInt(values));
        });
        std::printf("%-40s %12.2f %12.2f %11.2fx\n", "vector<int> 200k",
                    visitMs, directMs, directMs > 0 ? visitMs / directMs : 0.0);
    }

    // 2) 嵌套：每次进入内层容器多两次事件。
    {
        std::vector<std::vector<int>> outer;
        outer.reserve(1'000);
        for (int i = 0; i < 1'000; ++i) {
            std::vector<int> inner;
            inner.reserve(100);
            for (int j = 0; j < 100; ++j)
                inner.push_back(j);
            outer.push_back(std::move(inner));
        }
        const std::size_t reps = 10;
        const double visitMs = msPerOp(reps, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                scanVariant(vt::detail::normalizeToVariant(outer));
        });
        const double directMs = msPerOp(reps, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                scanVariant(directNestedInt(outer));
        });
        std::printf("%-40s %12.2f %12.2f %11.2fx\n", "vector<vector<int>> 1k x 100",
                    visitMs, directMs, directMs > 0 ? visitMs / directMs : 0.0);
    }

    // 3) map<string,int>：Key 事件 + 条目事件。
    {
        std::map<std::string, int> values;
        for (int i = 0; i < 20'000; ++i)
            values.emplace("key_" + std::to_string(i), i);
        const std::size_t reps = 20;
        const double visitMs = msPerOp(reps, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                scanVariant(vt::detail::normalizeToVariant(values));
        });
        const double directMs = msPerOp(reps, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                scanVariant(directStringMap(values));
        });
        std::printf("%-40s %12.2f %12.2f %11.2fx\n", "map<string,int> 20k",
                    visitMs, directMs, directMs > 0 ? visitMs / directMs : 0.0);
    }

    // 4) map<int,int>：多一步 toKey（十进制文本），衡量键字符串化的成本。
    {
        std::map<int, int> values;
        for (int i = 0; i < 20'000; ++i)
            values.emplace(i, i);
        const std::size_t reps = 20;
        const double visitMs = msPerOp(reps, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                scanVariant(vt::detail::normalizeToVariant(values));
        });
        const double directMs = msPerOp(reps, [&](std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                scanVariant(directIntMap(values));
        });
        std::printf("%-40s %12.2f %12.2f %11.2fx\n", "map<int,int> 20k (text keys)",
                    visitMs, directMs, directMs > 0 ? visitMs / directMs : 0.0);
    }

    std::printf("  note: Python 侧（PySink）不在此基准内；它省掉中间 VariantList，\n");
    std::printf("        100 万 int 少约 40 MB 的临时 Variant，但没有单独测量。\n");
}

void benchDetach()
{
    section("copy + mutate one key (COW detach), 1k-key map");
    const std::size_t K = 1'000;
    const std::size_t N = 2'000;   // 每次都要克隆 1k 个节点，不再放大
    const QueryData data = makeQueryData(K);

    QVariantMap qtSource;
    for (std::size_t i = 0; i < K; ++i)
        qtSource.insert(data.qkeys[i], static_cast<qlonglong>(i));

    const double vtNs = nsPerOp(N, [&](std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            vt::Variant c = data.vtMap;
            c.asMap()[data.keys[0]] = std::int64_t(-1);
            const vt::Variant& cc = c;
            g_sink += cc.asMap().size() == 0 ? 0 : 1;
        }
    });

    const double qtNs = nsPerOp(N, [&](std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            QVariant c(qtSource);
            QVariantMap m = c.toMap();
            m[data.qkeys[0]] = static_cast<qlonglong>(-1);
            g_sink += m.size() == 0 ? 0 : 1;
        }
    });

    std::printf("%-36s %12.2f %12.2f %9.2fx  (vt: detach clones 1k map)\n",
                "copy + mutate key", vtNs, qtNs, vtNs > 0 ? qtNs / vtNs : 0.0);
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("vt::Variant vs QVariant benchmark (MSVC Release)\n");
    benchSizes();
    benchScalars();
    benchMapBuild();
    benchMapLookup();
    benchNestedPath();
    benchNormalize();
    benchDetach();

    std::printf("\nsink=%lld\n", static_cast<long long>(g_sink));
    std::printf("notes:\n");
    std::printf(" - QVariantMap is an ordered QMap; vt::VariantMap is std::unordered_map.\n");
    std::printf(" - QString is implicitly shared; vt uses std::string (deep copy).\n");
    std::printf(" - ratio = qt/vt; >1 means vt is faster.\n");
    return 0;
}
