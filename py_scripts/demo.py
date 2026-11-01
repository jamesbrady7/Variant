"""vt::Variant <-> Python 往返演示（真实签名 API）。

覆盖：
  1. C++ 构造的 Variant 树 -> Python 原生 dict/list/标量
  2. 真实签名 echo/echoMap/echoVector/echoUnorderedMap 的双向转换
  3. 注册对象类型（DemoNode）：跨语言保持同一个对象身份
  4. 注册值类型（DemoColor）：跨语言按值拷贝
  5. 标准容器：不单独注册容器，靠 shiboken 内置转换器 + 元素规则递归
  6. 错误路径：不可转换的 Python 类型必须响亮失败

没有 PyDocument：Python 侧不再有 get/set 门面，函数签名直接收/返 dict/list。
"""

import os
import sys

# 支持直接 `python py_scripts/demo.py`（C++ runner 会自己加路径，这里兜底）。
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "..", "build", "bin", "Release"))

import variant_binding as vb
from variant_binding import DemoColor, DemoNode

_checks = 0


def check(cond, message):
    global _checks
    _checks += 1
    if not cond:
        raise AssertionError(message)
    print("  ok -", message)


def expect_type_error(fn, message):
    try:
        fn()
    except TypeError as exc:
        print("  expected TypeError:", exc)
        check(True, message)
    else:
        raise AssertionError(message + "（没有抛 TypeError）")


def run():
    vb.initTypes()

    print("=== 1. C++ -> Python：Variant 变成原生 dict/list ===")
    sample = vb.makeSample()
    check(isinstance(sample, dict), "makeSample() 返回 dict")
    check(sample["name"] == "variant-demo", "string 往返")
    check(sample["version"] == 3, "int 往返")
    check(sample["enabled"] is True, "bool 往返（不是 int）")
    check(abs(sample["ratio"] - 0.75) < 1e-12, "double 往返")
    check(sample["nothing"] is None, "Null -> None")
    check(sample["root"]["subtree_1"]["subsubtree_1"] == [1, 2, 3], "嵌套 list")
    check(sample["items"][1]["label"] == "second", "list 里的 dict")

    print("\n=== 2. 真实签名 echo：dict/list 双向 ===")
    payload = {
        "name": "cfg",
        "nested": {"flag": True, "nums": [1, 2.5, None, "text"]},
        "deep": [[{"k": "v"}], {}],
    }
    echoed = vb.echo(payload)
    check(echoed == payload, "echo(dict) 原样往返")
    check(vb.echo(True) is True, "bool 保持 bool")
    check(vb.echo(7) == 7, "int 保持 int")
    check(vb.echo(None) is None, "None 保持 None")
    check(vb.echoMap(payload) == payload, "echoMap(VariantMap) 往返")
    check(vb.echoMap({}) == {}, "空 dict 往返")
    check(vb.echoList([1, "x", None]) == [1, "x", None], "echoList(VariantList) 往返")
    check(vb.echoList((1, 2)) == [1, 2], "tuple 读入后变 list")
    check(vb.echoList([]) == [], "空 list 往返")
    expect_type_error(lambda: vb.echoList({"a": 1}), "VariantList 拒绝 dict")

    print("\n=== 3. 注册对象类型：身份保持 ===")
    node = DemoNode(7, "alpha")
    node.addTag("t1")
    node.addTag("t2")
    check(vb.echo(node) is node, "echo(node) 取出同一个 Python 对象（is）")

    nested = {"first": node, "all": [node, {"inner": node}], "n": None}
    back = vb.echo(nested)
    check(back["first"] is node, "dict 值身份保持")
    check(back["all"][0] is node, "list 元素身份保持")
    check(back["all"][1]["inner"] is node, "嵌套 dict 里的身份保持")
    check(back["n"] is None, "None 保持")

    back["first"].setName("renamed")
    check(node.name() == "renamed", "通过取回对象修改原对象可见")

    print("\n=== 4. 注册值类型：按值拷贝 ===")
    color = DemoColor(10, 20, 30)
    got = vb.echo(color)
    check(isinstance(got, DemoColor), "取回的是 DemoColor")
    check(got.r() == 10 and got.g() == 20 and got.b() == 30, "颜色值正确")
    got.setR(99)
    check(color.r() == 10, "值类型是拷贝语义：改副本不影响原对象")

    wrapped = vb.echoMap({"color": color, "node": node})
    check(isinstance(wrapped["color"], DemoColor), "dict 里的值类型保持类型")
    check(wrapped["node"] is node, "dict 里的对象类型保持身份")

    print("\n=== 5. 标准容器：不注册容器，靠内置转换器递归 ===")
    check(vb.echoVector([1, "x", None, [2], {"k": 3}]) == [1, "x", None, [2], {"k": 3}],
          "vector<Variant> 双向")
    vector_back = vb.echoVector([node, {"inner": node}])
    check(vector_back[0] is node and vector_back[1]["inner"] is node,
          "vector<Variant> 里的扩展对象身份保持")

    map_back = vb.echoUnorderedMap({"node": node, "list": [1, node]})
    check(map_back["node"] is node, "unordered_map<string,Variant> 对象身份")
    check(map_back["list"][1] is node, "map 内嵌 list 的对象身份")

    list_back = vb.echoList([node, {"inner": node}])
    check(list_back[0] is node and list_back[1]["inner"] is node,
          "VariantList 里的扩展对象身份保持")

    builtins = vb.makeBuiltinSample()
    check(builtins["ints"] == [1, 2, 3], "vector<int> -> list")
    check(builtins["strings"] == ["a", "b"], "vector<string> -> list")
    check(builtins["strmap"] == {"x": "y"}, "map<string,string> -> dict")
    # 非字符串键按十进制字符串写出（与 JSON 的 int 键格式一致）。
    check(builtins["intmap"] == {"1": 2, "3": 4}, "map<int,int> -> dict（键为十进制字符串）")
    check(vb.sumInts([4, 5, 6]) == 15, "Python list -> vector<int> 参数")
    check(vb.intMapSize({1: 2, 3: 4, 5: 6}) == 3, "Python dict[int,int] -> unordered_map 参数")

    nested = vb.makeNestedSample()
    check(nested["grid"] == [[1, 2], [3, 4, 5]], "vector<vector<int>> -> 嵌套 list")
    check(nested["rows"] == [{"x": 1}, {"y": 2, "z": 3}],
          "vector<map<string,int>> -> list[dict]")
    check(nested["by_id"] == {"7": ["a", "b"], "8": ["c"]},
          "map<int,vector<string>> -> dict[str, list]")
    check(nested["pairs"] == [[1, "one"], [2, "two"]], "vector<pair<int,string>> -> 二元 list")

    print("\n=== 6. 错误路径：必须响亮失败 ===")
    expect_type_error(lambda: vb.echo({1, 2}), "set 不是可转换类型")
    expect_type_error(lambda: vb.echo(object()), "任意 object 不可转换")

    print("\n=== 7. 调试输出 ===")
    print("  describeValue(sample) =", vb.describeValue(sample))
    print("  describeValue(node)   =", vb.describeValue({"node": node}))
    print(f"\nDemo done: {_checks} checks passed.")


if __name__ == "__main__":
    run()
