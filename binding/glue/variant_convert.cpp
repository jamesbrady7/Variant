// shiboken conversion-rule 胶水（不参与直接编译，由 // @snippet 抽取注入）。
// 真正的递归转换在 variant_python.cpp 里，用 C++ 实现一次即可。

// @snippet variant-to-python
return vpy::toPython(%in);
// @snippet variant-to-python

// @snippet variant-from-python
vpy::fromPython(%in, %out);
// @snippet variant-from-python
