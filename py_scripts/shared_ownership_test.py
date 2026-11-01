"""共享所有权（std::shared_ptr + capsule/收养）测试。

由 C++ runner `vt_shared_tests` 启动；也可以单独运行：
    python py_scripts/shared_ownership_test.py

Python 侧只使用真实签名：
    vb.holdPayload(dict) / vb.heldPayload() / vb.releasePayload()
C++ 侧静态持有 payload，替代已删除的 PyDocument。
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


def expect_type_error(fn, message):
    try:
        fn()
    except TypeError as exc:
        print("  expected TypeError:", exc)
        check(True, message)
    else:
        raise AssertionError(message + "（没有抛 TypeError）")


def run():
    # ---------------------------------------------------------------- 0. 注册顺序
    print("=== 0. 注册顺序：原生数据无需注册，扩展对象必须响亮失败 ===")
    native = {"a": [1, None, "x"], "nested": {"flag": True}}
    check(vb.echoMap(native) == native, "未 initTypes() 时原生 dict 仍可往返")
    expect_type_error(
        lambda: vb.echo(vb.DemoNode(1, "unregistered")),
        "未注册的扩展对象转 Variant 必须抛 TypeError")

    vb.initTypes()
    node_for_order = vb.DemoNode(2, "registered")
    check(vb.echo(node_for_order) is node_for_order,
          "initTypes() 后同一个扩展对象身份往返")
    del node_for_order
    gc.collect()

    baseline = alive()
    print("baseline alive =", baseline)

    # ---------------------------------------------------------------- 1. 收养
    print("\n=== 1. Python 创建的对象：收养 + 身份 ===")
    node = vb.SharedNode(101, "py-node")
    check(alive() == baseline + 1, "Python 构造后计数 +1")

    vb.holdPayload({"root": {"node": node}})
    back = vb.heldPayload()["root"]["node"]
    check(back is node, "取回的是同一个 Python 对象（收养保留身份）")
    check(alive() == baseline + 1, "Variant 持有期间对象不重复创建")

    back.setName("renamed")
    check(node.name() == "renamed", "通过取回对象修改原对象可见")

    del node
    gc.collect()
    check(alive() == baseline + 1, "删除 Python 变量后 C++ 仍保活")
    check(back.name() == "renamed", "C++ 保留期间 wrapper 仍可用")

    del back
    gc.collect()
    check(alive() == baseline + 1, "删除 wrapper 后 C++ 仍保活")
    again = vb.heldPayload()["root"]["node"]
    check(again.name() == "renamed", "再次读取仍得到原对象")

    vb.releasePayload()
    del again
    gc.collect()
    check(alive() == baseline, "releasePayload() 后对象析构")

    # ---------------------------------------------------------------- 2. 同一对象多次
    print("\n=== 2. 同一个对象多次放入 Variant ===")
    node = vb.SharedNode(102, "multi")
    vb.holdPayload({"a": node, "b": node, "deep": [node, {"inner": node}]})
    check(alive() == baseline + 1, "同一对象只应有一个实例")
    del node
    gc.collect()

    payload = vb.heldPayload()
    check(payload["a"] is payload["b"], "两个路径取回同一个对象")
    check(payload["deep"][0] is payload["a"], "list 元素身份保持")
    check(payload["deep"][1]["inner"] is payload["a"], "嵌套 dict 身份保持")
    check(alive() == baseline + 1, "多次收养不重复计数")
    vb.releasePayload()
    del payload
    gc.collect()
    check(alive() == baseline, "释放后回到 baseline")

    # ---------------------------------------------------------------- 3. C++ 工厂
    print("\n=== 3. C++ 工厂创建的 shared_ptr 对象 ===")
    cpp_node = vb.makeSharedNode(201, "cpp-node")
    check(isinstance(cpp_node, vb.SharedNode), "工厂返回真正的 SharedNode")
    check(alive() == baseline + 1, "C++ 工厂创建计数 +1")

    vb.holdPayload({"c": cpp_node})
    same = vb.heldPayload()["c"]
    check(same is cpp_node, "C++ shared_ptr 路径保持 wrapper 身份")

    del cpp_node
    gc.collect()
    check(alive() == baseline + 1 and same.name() == "cpp-node",
          "删除 Python 变量后 C++ 仍保活")
    del same
    gc.collect()
    check(alive() == baseline + 1, "删除 wrapper 后 C++ 仍保活")
    again = vb.heldPayload()["c"]
    check(again.name() == "cpp-node", "再次读取仍可用")
    vb.releasePayload()
    del again
    gc.collect()
    check(alive() == baseline, "释放后析构")

    # 不进 Variant 的工厂对象，删除即析构
    temp = vb.makeSharedNode(202, "temp")
    check(alive() == baseline + 1, "临时工厂对象存活")
    del temp
    gc.collect()
    check(alive() == baseline, "删除临时工厂对象后析构")

    # ---------------------------------------------------------------- 4. 借用拒绝
    print("\n=== 4. 借用对象必须被拒绝 ===")
    borrowed = vb.borrowSharedNode(301, "borrowed")
    check(isinstance(borrowed, vb.SharedNode), "借用对象也是 SharedNode")
    check(borrowed.name() == "borrowed", "借用对象可正常读取")

    before_borrowed = alive()
    vb.holdPayload({"ok": 1})
    expect_type_error(
        lambda: vb.holdPayload({"borrowed": borrowed}),
        "借用对象写入 VariantMap 必须失败")
    check(vb.heldPayload() == {"ok": 1},
          "失败不产生半成品：C++ 仍持有上一次的 payload")
    vb.releasePayload()
    check(borrowed.name() == "borrowed", "失败后借用对象仍可用")
    del borrowed
    gc.collect()
    check(alive() == before_borrowed,
          "借用对象由 C++ 静态容器持有，不会因 Python 释放而析构")

    print(f"\nShared ownership tests: {_checks} checks passed.")
    return _checks


if __name__ == "__main__":
    run()
