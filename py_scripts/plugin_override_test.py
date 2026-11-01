"""Python 继承 C++ PluginBase 并重写虚函数。

由 C++ runner `vt_plugin_override_test` 调用：
    make_payload() -> {"plugin": PyPlugin(...), "class_name": ..., "is_subclass": ...}

C++ 侧把返回的 dict 转成 VariantMap（plugin 是 shared_ptr<PluginBase>），
然后构造自定义值类型 PluginRequest，调用虚函数 evaluate()：

    C++ PluginRequest ──> Python override ──> super().evaluate() ──> C++ 基类
        └──────────────────────── PluginResult <──────────────────────┘
                          Python 加工后返回给 C++

自定义类型 PluginRequest / PluginResult 都是 value-type，两侧按值拷贝。
"""

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "..", "build", "bin", "Release"))

import variant_binding as vb


class PyPlugin(vb.PluginBase):
    """重写 evaluate/compute/describe，并带 Python 专属状态（factor/calls）。"""

    def __init__(self, plugin_id, name, factor):
        super().__init__(plugin_id, name)
        self.factor = factor
        self.compute_calls = 0
        self.eval_calls = 0
        self.transform_calls = 0

    def compute(self, value):
        self.compute_calls += 1
        return super().compute(value) * self.factor

    def describe(self):
        return "python:" + super().describe()

    def evaluate(self, request):
        # request 是 C++ 构造的 PluginRequest（按值传到 Python）
        self.eval_calls += 1
        base = super().evaluate(request)   # 回到 C++ 基类执行，返回 PluginResult
        return vb.PluginResult(            # 新的 PluginResult 再传回 C++
            base.value() * self.factor,
            "%s -> python(x%d, call=%d)" % (base.trace(), self.factor, self.eval_calls))

    def transform(self, payload):
        # payload 是 C++ 构造的 VariantMap，到 Python 就是普通 dict
        self.transform_calls += 1
        result = super().transform(payload)          # 回 C++ 基类，返回新 dict
        result["stage"] = "python"
        result["factor"] = self.factor
        result["value"] *= self.factor               # 值类型往返后是 int
        result["calls"] = self.transform_calls
        result["input_keys"] = sorted(payload.keys())
        return result


def make_payload():
    """构造 payload；返回后函数内不再保留引用。"""
    plugin = PyPlugin(7, "py-plugin", 3)
    return {
        "plugin": plugin,
        "class_name": type(plugin).__name__,
        "is_subclass": issubclass(type(plugin), vb.PluginBase),
    }


if __name__ == "__main__":
    # 直接运行时的自检（不进 Variant，只验证 Python 侧能覆盖）
    p = PyPlugin(1, "standalone", 2)
    assert p.compute(4) == 16, p.compute(4)
    assert p.describe() == "python:plugin(standalone)"
    r = p.evaluate(vb.PluginRequest("scale", 3, 4))
    assert r.value() == 24, r.value()                 # 3*4=12, *2=24
    assert r.trace() == "base:scale(3,4) -> python(x2, call=1)", r.trace()

    out = p.transform({"value": 5, "tag": "demo"})
    assert out["stage"] == "python", out
    assert out["value"] == 20, out                    # 5*2=10, *2=20
    assert out["plugin_id"] == 1 and out["plugin_name"] == "standalone", out
    assert out["tag"] == "demo", out                  # 输入键原样保留
    print("plugin_override_test.py: Python subclass works")
