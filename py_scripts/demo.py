"""vt::Variant <-> Python 往返演示。

覆盖：
  1. C++ 构造的 Variant 树 -> Python 原生 dict/list/标量
  2. Python dict/list -> C++ Variant -> 再回 Python（含路径访问）
  3. 注册对象类型（DemoNode）：跨语言保持同一个对象身份
  4. 注册值类型（DemoColor）：跨语言按值拷贝
  5. 错误路径：类型冲突、不可转换的 Python 类型
"""

import os
import sys

# 支持直接 `python py_scripts/demo.py`（C++ runner 会自己加路径，这里兜底）。
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "..", "build", "bin", "Release"))

import variant_binding as vb
from variant_binding import (
    DemoColor,
    DemoNode,
    PyDocument,
    makeSample,
    pyDescribe,
    pyEcho,
)

class Document:
    """把 PyDocument + pyDocGet/pyDocSet 包成 doc.get/set 的顺手形式。"""

    def __init__(self):
        self._impl = PyDocument()

    def get(self, path):
        return vb.pyDocGet(self._impl, path)

    def set(self, path, value):
        return vb.pyDocSet(self._impl, path, value)

    def has(self, path):
        return self._impl.has(path)

    def lastError(self):
        return self._impl.lastError()

    def clear(self):
        self._impl.clear()


_checks = 0


def check(cond, message):
    global _checks
    _checks += 1
    if not cond:
        raise AssertionError(message)
    print("  ok -", message)


def run():
    print("=== 1. C++ -> Python：Variant 变成原生 dict/list ===")
    sample = makeSample()
    check(isinstance(sample, dict), "makeSample() 返回 dict")
    check(sample["name"] == "variant-demo", "string 往返")
    check(sample["version"] == 3, "int 往返")
    check(sample["enabled"] is True, "bool 往返（不是 int）")
    check(abs(sample["ratio"] - 0.75) < 1e-12, "double 往返")
    check(sample["nothing"] is None, "Null -> None")
    check(sample["root"]["subtree_1"]["subsubtree_1"] == [1, 2, 3], "嵌套 list")
    check(sample["items"][1]["label"] == "second", "list 里的 dict")

    print("\n=== 2. Python -> C++ -> Python：路径访问 ===")
    doc = Document()
    check(doc.set("root.subtree_1.subsubtree_1", [10, 20, 30]), "写入嵌套 list")
    check(doc.get("root.subtree_1.subsubtree_1[1]") == 20, "路径读取 list 元素")
    check(doc.get("root.subtree_1") == {"subsubtree_1": [10, 20, 30]}, "读取子树")

    payload = {
        "name": "cfg",
        "nested": {"flag": True, "nums": [1, 2.5, None, "text"]},
    }
    check(doc.set("cfg", payload), "写入 Python dict")
    check(doc.get("cfg") == payload, "dict 原样往返")
    check(doc.get("cfg.nested.nums[3]") == "text", "路径穿过 dict/list")
    check(doc.has("cfg.nested.flag"), "has() 支持路径")
    check(not doc.has("cfg.nested.missing"), "has() 缺失返回 False")

    print("\n=== 3. 注册对象类型：身份保持 ===")
    node = DemoNode(7, "alpha")
    node.addTag("t1")
    node.addTag("t2")
    check(doc.set("nodes.first", node), "把 DemoNode 放进 Variant")
    back = doc.get("nodes.first")
    check(back is node, "取出的是同一个 Python 对象（is）")
    back.setName("renamed")
    check(node.name() == "renamed", "通过取回对象修改原对象可见")

    check(doc.set("nodes.all", [node, {"inner": node}]), "对象嵌进 list/dict")
    all_nodes = doc.get("nodes.all")
    check(all_nodes[0] is node, "list 元素身份保持")
    check(all_nodes[1]["inner"] is node, "嵌套 dict 里的身份保持")

    echoed = pyEcho({"a": [1, node, None], "n": DemoNode(0, "x")})
    check(echoed["a"][1] is node, "echo() 后对象身份保持")
    check(echoed["a"][2] is None, "echo() 后 None 保持")

    print("\n=== 4. 注册值类型：按值拷贝 ===")
    color = DemoColor(10, 20, 30)
    check(doc.set("color", color), "把 DemoColor 放进 Variant")
    got = doc.get("color")
    check(isinstance(got, DemoColor), "取回的是 DemoColor")
    check(got.r() == 10 and got.g() == 20 and got.b() == 30, "颜色值正确")
    got.setR(99)
    check(color.r() == 10, "值类型是拷贝语义：改副本不影响原对象")
    print("  pyDescribe(color):", pyDescribe({"color": color}))

    print("\n=== 5. 错误路径 ===")
    check(doc.set("cfg.nested", 5), "dict 节点可以被标量替换")
    check(not doc.set("cfg.nested.deep", 1), "对 int 继续下钻应失败")
    print("  lastError:", doc.lastError())

    try:
        pyEcho({1, 2})  # set 不是可转换类型
        raise AssertionError("set 应该被拒绝")
    except TypeError as e:
        print("  expected TypeError:", e)

    # 边界预检：不抛异常，直接回答"这棵树能否转成 Variant/Python"
    check(vb.canConvertToPython({"a": [1, node, None]}), "已注册对象嵌套在 dict 里可转换")
    check(not vb.canConvertToPython({"a": {1, 2}}), "嵌套 set 不可转换")
    check(not vb.canConvertToPython(object()), "任意 Python 对象不可转换")

    with_error = Document()
    check(not with_error.set("bad[3]", 1), "越界下标返回 False")
    print("  lastError:", with_error.lastError())

    print("\n=== 6. 内建容器适配 ===")
    builtins = vb.makeBuiltinSample()
    check(builtins["ints"] == [1, 2, 3], "vector<int> -> list")
    check(builtins["strings"] == ["a", "b"], "vector<string> -> list")
    check(builtins["strmap"] == {"x": "y"}, "map<string,string> -> dict")
    check(builtins["intmap"] == {1: 2, 3: 4}, "map<int,int> -> dict[int,int]")
    check(vb.sumInts([4, 5, 6]) == 15, "Python list -> getAs<vector<int>>")
    check(vb.intMapSize({1: 2, 3: 4, 5: 6}) == 3, "Python dict[int,int] -> getAs<map<int,int>>")
    check(vb.canConvertToPython(builtins), "内建容器整棵树可转换")

    print("\n=== 7. 调试输出 ===")
    print("  pyDescribe(sample) =", pyDescribe(sample))
    print(f"\nDemo done: {_checks} checks passed.")


if __name__ == "__main__":
    vb.initTypes()
    run()
