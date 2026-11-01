// shiboken conversion-rule 胶水（不参与直接编译，由 // @snippet 抽取注入）。
// 真正的递归转换在 variant_python.cpp 里，用 C++ 实现一次即可。

// @snippet variant-to-python
return vpy::toPython(%in);
// @snippet variant-to-python

// @snippet variant-from-python
vpy::fromPython(%in, %out);
// @snippet variant-from-python

// VariantMap：不手写逐元素递归，委托给 shiboken 内置的
// std::unordered_map<std::string, vt::Variant> 容器转换器；
// 元素再回调 vt::Variant 的转换规则，天然递归。

// @snippet variantmap-to-python
std::unordered_map<std::string, vt::Variant> map;
map.reserve(%in.size());
for (const auto& kv : %in)
    map.emplace(kv.first, kv.second);
return %CONVERTTOPYTHON[std::unordered_map<std::string, vt::Variant>](map);
// @snippet variantmap-to-python

// @snippet variantmap-from-python
std::unordered_map<std::string, vt::Variant> map =
    %CONVERTTOCPP[std::unordered_map<std::string, vt::Variant>](%in);
%out = vt::VariantMap(std::move(map));
// @snippet variantmap-from-python

// @snippet variantmap-from-python-catchall
if (!PyDict_Check(%in)) {
    PyErr_Format(PyExc_TypeError, "vt::VariantMap requires a dict, got %s",
                 Py_TYPE(%in)->tp_name);
    return;
}
std::unordered_map<std::string, vt::Variant> map =
    %CONVERTTOCPP[std::unordered_map<std::string, vt::Variant>](%in);
%out = vt::VariantMap(std::move(map));
// @snippet variantmap-from-python-catchall
