// vt::Variant vs QVariant 基准。
// 目的不是"赢"，是量清楚封装成本的边界：哪种操作代价相当、哪种明显更贵。
#include "vt/vt.h"

#include <QVariant>
#include <QVariantMap>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdio>
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
    benchDetach();

    std::printf("\nsink=%lld\n", static_cast<long long>(g_sink));
    std::printf("notes:\n");
    std::printf(" - QVariantMap is an ordered QMap; vt::VariantMap is std::unordered_map.\n");
    std::printf(" - QString is implicitly shared; vt uses std::string (deep copy).\n");
    std::printf(" - ratio = qt/vt; >1 means vt is faster.\n");
    return 0;
}
