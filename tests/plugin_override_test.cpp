// Python 继承 C++ PluginBase 并重写虚函数的端到端演示。
//
// 验证链路：
//   1. PluginBase 同时注册了落盘 codec + Python 共享类型；
//      PluginRequest / PluginResult 注册为值类型（Python <-> C++ 按值拷贝）；
//   2. Python 子类实例经 dict 返回，C++ 用 VariantMap 接收，
//      条目里是 std::shared_ptr<PluginBase>；
//   3. C++ 构造 PluginRequest 调用虚函数 evaluate()，派发到 Python 重写：
//        C++ request -> Python 重写 -> super().evaluate() 回 C++ 基类
//                    -> PluginResult 回 Python 加工 -> 再回 C++ 读取
//   4. Python / C++ 两侧都不再持有时，对象正常析构。
#include "demo_types.h"
#include "variant_python.h"
#include "vt/vt.h"

// shiboken 可能传播 Py_LIMITED_API；本 TU 需要 PyList_Insert 等完整 API。
#if defined(Py_LIMITED_API)
#undef Py_LIMITED_API
#endif
#include <Python.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("[FAIL] %s\n", what);
    } else {
        std::printf("  ok - %s\n", what);
    }
}

std::string executableDir()
{
#ifdef _WIN32
    char buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    std::string path(buffer, length);
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
#else
    return ".";
#endif
}

void insertSysPath(const std::string& dir)
{
    PyObject* sysPath = PySys_GetObject("path");   // borrowed
    if (!sysPath)
        return;
    PyObject* entry = PyUnicode_DecodeFSDefault(dir.c_str());
    if (!entry)
        return;
    PyList_Insert(sysPath, 0, entry);
    Py_DECREF(entry);
}

void initPython(const std::string& exeDir)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    _putenv_s("PYTHONHOME", VT_PYTHON_HOME);
    _putenv_s("PYTHONIOENCODING", "utf-8");
    Py_Initialize();

    insertSysPath(exeDir);
    insertSysPath(VT_SCRIPT_DIR);
}

void collectPythonGarbage()
{
    PyRun_SimpleString("import gc; gc.collect()");
}

void printResult(const char* who, const PluginRequest& request, const PluginResult& result)
{
    std::printf("      %s.evaluate(%s, value=%d, scale=%d) -> value=%d, trace=\"%s\"\n",
                who,
                request.operation().c_str(),
                request.value(),
                request.scale(),
                result.value(),
                result.trace().c_str());
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const std::string exeDir = executableDir();
    initPython(exeDir);

    // ---------------------------------------------------------- 1. 注册
    std::printf("=== 1. registration ===\n");
    if (!vpy::registerDemoTypes()) {
        std::printf("[FAIL] registerDemoTypes()\n");
        PyErr_Print();
        Py_Finalize();
        return 1;
    }
    check(true, "PluginBase: Python shared type + persistence codec");
    check(true, "PluginRequest / PluginResult: Python value types");

    const int baseline = PluginBase::aliveCount();
    check(baseline == 0, "baseline PluginBase alive == 0");

    // ---------------------------------------------------------- 2. C++ 基类行为
    std::printf("\n=== 2. C++ base implementation ===\n");
    {
        const PluginBase basePlugin(1, "plain-cpp");
        const PluginRequest scale("scale", 7, 2);
        const PluginRequest offset("offset", 10, 5);

        const PluginResult r1 = basePlugin.evaluate(scale);
        const PluginResult r2 = basePlugin.evaluate(offset);
        printResult("base", scale, r1);
        printResult("base", offset, r2);

        check(r1.value() == 14 && r1.trace() == std::string("base:scale(7,2)"),
              "base: scale -> 7*2=14, trace=base:scale(7,2)");
        check(r2.value() == 15 && r2.trace() == std::string("base:offset(10,5)"),
              "base: offset -> 10+5=15, trace=base:offset(10,5)");
    }

    // ---------------------------------------------------------- 3. 落盘
    std::printf("\n=== 3. persistence (codec) ===\n");
    {
        auto plugin = std::make_shared<PluginBase>(42, "persisted");
        const vt::Variant encoded = vt::Variant::fromValue(plugin);
        const std::string text = vt::json::write(encoded);
        std::printf("      json: %s\n", text.c_str());

        auto restored = vt::getAs<std::shared_ptr<PluginBase>>(vt::json::read(text));
        check(restored.has_value(), "JSON round-trip returns shared_ptr<PluginBase>");
        if (restored) {
            const auto& p = *restored;
            check(p->id() == 42 && p->name() == std::string("persisted"),
                  "codec: base fields preserved");
            const PluginResult r = p->evaluate(PluginRequest("scale", 3, 4));
            check(r.value() == 12 && r.trace() == std::string("base:scale(3,4)"),
                  "codec: decoded object is a plain C++ PluginBase");
        }
    }
    check(PluginBase::aliveCount() == baseline, "codec: temporary objects released");

    // ---------------------------------------------------------- 4. Python 子类
    std::printf("\n=== 4. Python subclass via VariantMap ===\n");
    PyObject* module = PyImport_ImportModule("plugin_override_test");
    if (!module) {
        std::printf("[FAIL] import plugin_override_test\n");
        PyErr_Print();
        Py_Finalize();
        return 1;
    }

    PyObject* makePayload = PyObject_GetAttrString(module, "make_payload");
    if (!makePayload || !PyCallable_Check(makePayload)) {
        std::printf("[FAIL] plugin_override_test.make_payload not found\n");
        PyErr_Print();
        Py_DECREF(module);
        Py_Finalize();
        return 1;
    }

    PyObject* payload = PyObject_CallNoArgs(makePayload);
    Py_DECREF(makePayload);
    if (!payload) {
        std::printf("[FAIL] make_payload() raised\n");
        PyErr_Print();
        Py_DECREF(module);
        Py_Finalize();
        return 1;
    }

    // Python 返回 dict -> C++ VariantMap；其中 plugin 被收养为 shared_ptr。
    vt::Variant map;
    const bool converted = vpy::fromPython(payload, map);
    Py_DECREF(payload);   // 之后 Python 侧不再持有该对象
    if (!converted) {
        std::printf("[FAIL] fromPython(dict) -> VariantMap\n");
        PyErr_Print();
        Py_DECREF(module);
        Py_Finalize();
        return 1;
    }
    collectPythonGarbage();

    check(map.isMap(), "payload converted to VariantMap");
    check(map.at("class_name").asString() == std::string("PyPlugin"),
          "VariantMap class_name == PyPlugin");
    check(map.at("is_subclass").asBool(),
          "issubclass(PyPlugin, PluginBase) == True");
    check(PluginBase::aliveCount() == baseline + 1,
          "adopted Python subclass alive while C++ holds it");

    auto plugin = vt::getAs<std::shared_ptr<PluginBase>>(map.at("plugin"));
    check(plugin.has_value(), "VariantMap holds shared_ptr<PluginBase>");
    if (plugin) {
        const auto& p = *plugin;

        std::printf("      base fields via C++: id=%d, name=%s\n", p->id(), p->name().c_str());
        check(p->id() == 7 && p->name() == std::string("py-plugin"),
              "base fields readable through shared_ptr<PluginBase>");

        // 自定义输入/输出类型：双向跨语言。
        const PluginRequest scale("scale", 7, 2);
        const PluginRequest offset("offset", 10, 5);

        const PluginResult r1 = p->evaluate(scale);
        const PluginResult r2 = p->evaluate(offset);
        printResult("py", scale, r1);
        printResult("py", offset, r2);

        check(r1.value() == 42 && r1.trace()
                  == std::string("base:scale(7,2) -> python(x3, call=1)"),
              "request->Python->super()->Python->C++: 14*3=42, base trace kept");
        check(r2.value() == 45 && r2.trace()
                  == std::string("base:offset(10,5) -> python(x3, call=2)"),
              "second call keeps Python state: 15*3=45, call=2");

        // 同一对象上的原始类型虚函数也能派发。
        check(p->compute(5) == 30,
              "primitive virtual compute(5) -> Python 5*2*3=30");
        check(p->describe() == std::string("python:plugin(py-plugin)"),
              "virtual describe() -> Python prefix + base text");
    }

    // ---------------------------------------------------------- 5. 释放
    std::printf("\n=== 5. lifetime ===\n");
    std::printf("      alive before release: %d\n", PluginBase::aliveCount());
    map = vt::Variant();     // 释放 VariantMap 里的 shared_ptr
    plugin = std::nullopt;   // 释放本地副本
    collectPythonGarbage();

    check(PluginBase::aliveCount() == baseline,
          "object released when neither Python nor C++ holds it");
    std::printf("      alive after release: %d\n", PluginBase::aliveCount());

    Py_DECREF(module);
    Py_Finalize();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
