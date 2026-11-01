"""共享所有权（std::shared_ptr + capsule/收养）测试。

由 C++ runner `vt_shared_tests` 启动；也可以单独运行：
    python py_scripts/shared_ownership_test.py
"""

import gc
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "..", "build", "bin", "Release"))

import variant_binding as vb

_checks = 0


def check(cond, message):
    global _checks
    _checks += 1
    if not cond:
        raise AssertionError(message)
    print("  ok -", message)


def alive():
    return vb.SharedNode.aliveCount()


def run():
    vb.initTypes()
    baseline = alive()
    print("baseline alive =", baseline)

    print("\n=== 1. Python 创建的对象：收养 + 身份 ===")
    node = vb.SharedNode(101, "py-node")
    check(alive() == baseline + 1, "Python 构造后计数 +1")

    doc = vb.PyDocument()
    check(vb.pyDocSet(doc, "root.node", node), "放进 Variant 成功")
    back = vb.pyDocGet(doc, "root.node")
    check(back is node, "取回的是同一个 Python 对象（收养保留身份）")
    check(alive() == baseline + 1, "Variant 持有期间对象不重复创建")

    back.setName("renamed")
    check(node.name() == "renamed", "通过取回对象修改原对象可见")

    del node
    gc.collect()
    check(alive() == baseline + 1, "删除 Python 变量后 Variant 仍保活")
    check(back.name() == "renamed", "Variant 保留期间 wrapper 仍可用")

    del back
    gc.collect()
    check(alive() == baseline + 1, "删除 wrapper 后 Variant 仍保活")
    again = vb.pyDocGet(doc, "root.node")
    check(again.name() == "renamed", "再次读取仍得到原对象")

    doc.clear()
    del again
    gc.collect()
    check(alive() == baseline, "Variant 释放后对象析构")

    print("\n=== 2. 同一个对象多次放入 Variant ===")
    node = vb.SharedNode(102, "multi")
    doc = vb.PyDocument()
    check(vb.pyDocSet(doc, "a", node), "写入 a")
    check(vb.pyDocSet(doc, "b", node), "写入 b")
    check(alive() == baseline + 1, "同一对象只应有一个实例")
    del node
    gc.collect()
    a = vb.pyDocGet(doc, "a")
    b = vb.pyDocGet(doc, "b")
    check(a is b, "两个路径取回同一个对象")
    check(alive() == baseline + 1, "多次收养不重复计数")
    doc.clear()
    del a, b
    gc.collect()
    check(alive() == baseline, "释放后回到 baseline")

    print("\n=== 3. 嵌套在 dict/list 中 ===")
    node = vb.SharedNode(103, "nested")
    doc = vb.PyDocument()
    payload = {"items": [node, {"inner": node}], "scalar": 1}
    check(vb.pyDocSet(doc, "tree", payload), "嵌套 dict/list 写入")
    got = vb.pyDocGet(doc, "tree")
    check(got["items"][0] is node, "list 元素身份保持")
    check(got["items"][1]["inner"] is node, "嵌套 dict 身份保持")
    del node
    gc.collect()
    check(got["items"][0].name() == "nested", "删除 Python 变量后嵌套对象仍可用")
    check(alive() == baseline + 1, "嵌套引用只计一份")
    doc.clear()
    del got, payload
    gc.collect()
    check(alive() == baseline, "清空后回到 baseline")

    print("\n=== 4. C++ 工厂创建的 shared_ptr 对象 ===")
    cpp_node = vb.pyMakeSharedNode(201, "cpp-node")
    check(isinstance(cpp_node, vb.SharedNode), "工厂返回真正的 SharedNode")
    check(alive() == baseline + 1, "C++ 工厂创建计数 +1")

    doc = vb.PyDocument()
    check(vb.pyDocSet(doc, "c", cpp_node), "工厂对象放进 Variant")
    same = vb.pyDocGet(doc, "c")
    check(same is cpp_node, "C++ shared_ptr 路径保持 wrapper 身份")

    del cpp_node
    gc.collect()
    check(alive() == baseline + 1 and same.name() == "cpp-node",
          "删除 Python 变量后 Variant 仍保活")
    del same
    gc.collect()
    check(alive() == baseline + 1, "删除 wrapper 后 Variant 仍保活")
    again = vb.pyDocGet(doc, "c")
    check(again.name() == "cpp-node", "再次读取仍可用")
    doc.clear()
    del again
    gc.collect()
    check(alive() == baseline, "Variant 释放后析构")

    # 不进 Variant 的工厂对象，删除即析构
    temp = vb.pyMakeSharedNode(202, "temp")
    check(alive() == baseline + 1, "临时工厂对象存活")
    del temp
    gc.collect()
    check(alive() == baseline, "删除临时工厂对象后析构")

    print("\n=== 5. 借用对象必须被拒绝 ===")
    borrowed = vb.pyBorrowSharedNode(301, "borrowed")
    check(isinstance(borrowed, vb.SharedNode), "借用对象也是 SharedNode")
    check(borrowed.name() == "borrowed", "借用对象可正常读取")

    before_borrowed = alive()
    doc = vb.PyDocument()
    ok = vb.pyDocSet(doc, "borrowed", borrowed)
    check(not ok, "借用对象写入 Variant 必须失败")
    check("borrowed" in doc.lastError().lower(), "lastError 说明是借用对象")
    check(borrowed.name() == "borrowed", "失败后借用对象仍可用")
    doc.clear()
    del borrowed
    gc.collect()
    check(alive() == before_borrowed, "借用对象由 C++ 静态容器持有，不会因 Python 释放而析构")

    print(f"\nShared ownership tests: {_checks} checks passed.")
    return _checks


if __name__ == "__main__":
    run()
