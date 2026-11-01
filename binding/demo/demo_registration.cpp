#include "variant_python.h"

#include "demo_types.h"

#include "vt/codec.h"
#include "vt/variant_map.h"

#include <memory>

namespace vpy {

// 演示「一次注册，之后任意嵌套放进 Variant 都能跨语言往返」。
// 新增类型只需要在这里加一行（对象类型用 registerPointerType，
// 值类型用 registerValueType），转换规则不用改 XML。
bool registerDemoTypes()
{
    static bool registered = false;
    if (registered)
        return true;

    // 标准容器不需要注册：Variant::fromValue 遇到容器类型时会自动登记
    // 归一化函数（toPython 的扩展槽回退会用它递归展开成 list/dict）。
    bool ok = true;

    // 一行注册同时完成：核心名字表 + Python 转换器（见 registerPointerType 实现）。
    ok = registerPointerType<DemoNode>("variant_binding", "DemoNode") && ok;
    ok = registerValueType<DemoColor>("variant_binding", "DemoColor") && ok;

    // 共享所有权：Variant 存 shared_ptr<SharedNode>，Python 仍是 SharedNode。
    ok = registerSharedType<SharedNode>("variant_binding", "SharedNode") && ok;

    // 多态基类：Python 可继承并重写虚函数；Variant 存 shared_ptr<PluginBase>。
    // 落盘 codec 只写基类字段（Python 子类的额外状态不参与持久化）。
    ok = registerSharedType<PluginBase>("variant_binding", "PluginBase") && ok;

    // 虚函数 evaluate() 的输入/输出：自定义值类型，Python <-> C++ 按值往返。
    ok = registerValueType<PluginRequest>("variant_binding", "PluginRequest") && ok;
    ok = registerValueType<PluginResult>("variant_binding", "PluginResult") && ok;
    ok = vt::registerValueCodec<std::shared_ptr<PluginBase>>(
             "PluginBase",
             [](const std::shared_ptr<PluginBase>& plugin) {
                 if (!plugin)
                     return vt::Variant();
                 return vt::Variant(vt::VariantMap{{"id", plugin->id()},
                                                   {"name", plugin->name()}});
             },
             [](const vt::Variant& plain, std::shared_ptr<PluginBase>& out) {
                 auto id = plain.getAt<int>("id");
                 auto name = plain.getAt<std::string>("name");
                 if (!id || !name)
                     return false;
                 out = std::make_shared<PluginBase>(*id, *name);
                 return true;
             }) && ok;

    // 序列化 codec（按值编码；读回会变成新的 shared_ptr，不保留共享关系）。
    ok = vt::registerValueCodec<std::shared_ptr<SharedNode>>(
             "SharedNode",
             [](const std::shared_ptr<SharedNode>& node) {
                 if (!node)
                     return vt::Variant();
                 return vt::Variant(vt::VariantMap{{"id", node->id()}, {"name", node->name()}});
             },
             [](const vt::Variant& plain, std::shared_ptr<SharedNode>& out) {
                 auto id = plain.getAt<int>("id");
                 auto name = plain.getAt<std::string>("name");
                 if (!id || !name)
                     return false;
                 out = makeSharedNode(*id, *name);
                 return true;
             }) && ok;

    registered = ok;
    return ok;
}

} // namespace vpy
