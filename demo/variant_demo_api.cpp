#include "variant_demo_api.h"

#include "vt/adapters.h"
#include "vt/debug.h"
#include "vt/variant_list.h"
#include "vt/variant_map.h"

#include <string>
#include <unordered_map>
#include <vector>

using namespace vt::literals;

vt::Variant makeSample()
{
    vt::Variant root;
    root["name"] = "variant-demo";
    root["version"] = 3;
    root["enabled"] = true;
    root["ratio"] = 0.75;
    root["root.subtree_1.subsubtree_1"_path] = vt::VariantList{1, 2, 3};
    root["items"][0]["id"] = 1;
    root["items"][0]["label"] = "first";
    root["items"][1]["id"] = 2;
    root["items"][1]["label"] = "second";
    root["config.window.width"_path] = 1280;
    root["config.window.height"_path] = 720;
    root["nothing"] = vt::Variant();
    return root;
}

vt::Variant makeBuiltinSample()
{
    vt::VariantMap root;
    root["ints"] = vt::Variant::fromValue(std::vector<int>{1, 2, 3});
    root["strings"] = vt::Variant::fromValue(std::vector<std::string>{"a", "b"});
    root["strmap"] = vt::Variant::fromValue(
        std::unordered_map<std::string, std::string>{{"x", "y"}});
    root["intmap"] = vt::Variant::fromValue(
        std::unordered_map<int, int>{{1, 2}, {3, 4}});
    return vt::Variant(std::move(root));
}

vt::Variant echo(const vt::Variant& value)
{
    return value;
}

std::string describeValue(const vt::Variant& value)
{
    return vt::describe(value);
}
