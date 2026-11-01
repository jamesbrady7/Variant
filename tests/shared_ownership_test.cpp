// 共享所有权测试 runner：内嵌 Python，加载 binding，执行 shared_ownership_test.py。
#include <Python.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

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

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const std::string exeDir = executableDir();
    initPython(exeDir);

    // PyRun_SimpleString 在 Python 抛异常时会自己打印 traceback 并返回 -1，
    // 事后 PyErr_Occurred() 已经是 false —— 必须用返回值判断成败，
    // 否则脚本里的断言失败会被静默吞掉、退出码仍是 0。
    const int rc = PyRun_SimpleString(
        "import shared_ownership_test as test\n"
        "test.run()\n");

    Py_Finalize();
    return rc == 0 ? 0 : 1;
}
