#include "shared_bridge.h"

#include "demo_types.h"
#include "variant_python.h"

#include "vt/variant.h"

#include <memory>
#include <string>

PyObject* pyMakeSharedNode(int id, const std::string& name)
{
    std::shared_ptr<SharedNode> node = makeSharedNode(id, name);
    return vpy::toPython(vt::Variant::fromValue(std::move(node)));
}

PyObject* pyBorrowSharedNode(int id, const std::string& name)
{
    SharedNode* raw = borrowSharedNode(id, name);
    if (!raw) {
        PyErr_SetString(PyExc_RuntimeError, "borrowSharedNode returned null");
        return nullptr;
    }

    Shiboken::Conversions::SpecificConverter converter("SharedNode");
    if (!converter.isValid()) {
        PyErr_SetString(PyExc_RuntimeError, "no shiboken converter named 'SharedNode'");
        return nullptr;
    }

    PyObject* wrapper = converter.toPython(&raw);
    if (!wrapper)
        return nullptr;

    // 保证这是"借用"：C++ 静态容器拥有对象，Python 不负责 delete。
    auto* sbk = reinterpret_cast<SbkObject*>(wrapper);
    if (Shiboken::Object::hasOwnership(sbk))
        Shiboken::Object::releaseOwnership(sbk);
    return wrapper;
}
