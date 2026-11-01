// 内嵌 Python 的 C++ runner：把 .pyd 目录与 py_scripts 加进 sys.path，
// 导入绑定模块、注册类型，再执行 Python 演示脚本。
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
    // 控制台已设 UTF-8；再让 Python 的 stdout/stderr 也用 UTF-8，避免中文乱码。
    _putenv_s("PYTHONIOENCODING", "utf-8");
    Py_Initialize();

    insertSysPath(exeDir);
    insertSysPath(VT_SCRIPT_DIR);
}

} // namespace

int main()
{
    const std::string exeDir = executableDir();
    initPython(exeDir);

    PyRun_SimpleString(
        "import variant_binding as vb\n"
        "if not vb.initTypes():\n"
        "    raise RuntimeError('initTypes() failed')\n"
        "import demo\n"
        "demo.run()\n");

    if (PyErr_Occurred())
        PyErr_Print();

    Py_Finalize();
    return 0;
}
