#pragma once

#include "export.h"

#include <memory>
#include <string>
#include <vector>

/**
 * @brief 演示“注册对象类型”的类。
 *
 * 注册成 object-type：Variant 里存 `DemoNode*`，跨语言保持对象身份。
 * 故意放在全局命名空间：shiboken 的转换器名就是类名，注册表按名字精确匹配。
 */
class VT_DEMO_API DemoNode {
public:
    /// @brief 构造空节点（id=0，name 为空）。
    DemoNode();

    /**
     * @brief 构造节点。
     * @param [in] int 节点 id
     * @param [in] std::string 节点名
     */
    DemoNode(int id, const std::string& name);

    /// @brief 节点 id。
    int id() const { return m_id; }

    /**
     * @brief 设置节点 id。
     * @param [in] int 新 id
     */
    void setId(int id) { m_id = id; }

    /// @brief 节点名。
    std::string name() const { return m_name; }

    /**
     * @brief 设置节点名。
     * @param [in] std::string 新名字
     */
    void setName(const std::string& name) { m_name = name; }

    /**
     * @brief 追加一个标签。
     * @param [in] std::string 标签
     */
    void addTag(const std::string& tag) { m_tags.push_back(tag); }

    /// @brief 所有标签用逗号连接。
    std::string tagsJoined() const;

private:
    int m_id = 0;                    ///< 节点 id
    std::string m_name;              ///< 节点名
    std::vector<std::string> m_tags; ///< 标签列表
};

/**
 * @brief 演示“注册值类型”的类。
 *
 * 注册成 value-type：Variant 里存 `DemoColor` 值，跨语言是拷贝语义。
 */
class VT_DEMO_API DemoColor {
public:
    /// @brief 构造黑色。
    DemoColor() = default;

    /**
     * @brief 构造颜色。
     * @param [in] int 红
     * @param [in] int 绿
     * @param [in] int 蓝
     */
    DemoColor(int r, int g, int b) : m_r(r), m_g(g), m_b(b) {}

    /// @brief 红色分量。
    int r() const { return m_r; }
    /// @brief 绿色分量。
    int g() const { return m_g; }
    /// @brief 蓝色分量。
    int b() const { return m_b; }

    /**
     * @brief 设置红色分量。
     * @param [in] int 新值
     */
    void setR(int r) { m_r = r; }

    /**
     * @brief 设置绿色分量。
     * @param [in] int 新值
     */
    void setG(int g) { m_g = g; }

    /**
     * @brief 设置蓝色分量。
     * @param [in] int 新值
     */
    void setB(int b) { m_b = b; }

    /// @brief 生成 "rgb(r,g,b)" 文本。
    std::string toString() const;

private:
    int m_r = 0;  ///< 红
    int m_g = 0;  ///< 绿
    int m_b = 0;  ///< 蓝
};


/**
 * @brief 演示“共享所有权”类型。
 *
 * Variant 里存 `std::shared_ptr<SharedNode>`；Python 侧仍然是 SharedNode。
 * 生命周期由 shared_ptr 控制块 + Python 侧内部 capsule 共同决定。
 */
class VT_DEMO_API SharedNode {
public:
    /// @brief 构造空节点。
    SharedNode();

    /**
     * @brief 构造节点。
     * @param [in] int id
     * @param [in] std::string 名字
     */
    SharedNode(int id, const std::string& name);

    /// @brief 析构（会减少 aliveCount）。
    ~SharedNode();

    /// @brief id。
    int id() const { return m_id; }

    /// @brief 名字。
    std::string name() const { return m_name; }

    /**
     * @brief 设置名字。
     * @param [in] std::string 新名字
     */
    void setName(const std::string& name) { m_name = name; }

    /// @brief 当前存活的 SharedNode 数量（测试用）。
    static int aliveCount();

private:
    int m_id = 0;        ///< id
    std::string m_name;  ///< 名字
};

/**
 * @brief 插件求值请求（值类型）。
 *
 * 作为虚函数 `PluginBase::evaluate` 的输入，在 C++ 与 Python 之间按值传递；
 * 注册成 value-type 后，shiboken 会为它生成拷贝语义的转换器。
 */
class VT_DEMO_API PluginRequest {
public:
    /// @brief 构造默认请求（operation="scale"，value=0，scale=1）。
    PluginRequest() = default;

    /**
     * @brief 构造请求。
     * @param [in] std::string 操作名（"scale" / "offset"）
     * @param [in] int 输入值
     * @param [in] int 缩放/偏移量
     */
    PluginRequest(std::string operation, int value, int scale);

    /// @brief 操作名。
    std::string operation() const { return m_operation; }

    /// @brief 输入值。
    int value() const { return m_value; }

    /// @brief 缩放/偏移量。
    int scale() const { return m_scale; }

    /**
     * @brief 设置操作名。
     * @param [in] std::string 新操作名
     */
    void setOperation(const std::string& operation) { m_operation = operation; }

    /**
     * @brief 设置输入值。
     * @param [in] int 新输入值
     */
    void setValue(int value) { m_value = value; }

    /**
     * @brief 设置缩放/偏移量。
     * @param [in] int 新量
     */
    void setScale(int scale) { m_scale = scale; }

private:
    std::string m_operation = "scale";  ///< 操作名
    int m_value = 0;                    ///< 输入值
    int m_scale = 1;                    ///< 缩放/偏移量
};

/**
 * @brief 插件求值结果（值类型）。
 *
 * 作为虚函数 `PluginBase::evaluate` 的返回值；Python 重写时可以先拿基类结果，
 * 再构造一个新的 PluginResult 返回给 C++。
 */
class VT_DEMO_API PluginResult {
public:
    /// @brief 构造空结果（value=0，trace 为空）。
    PluginResult() = default;

    /**
     * @brief 构造结果。
     * @param [in] int 结果值
     * @param [in] std::string 执行轨迹
     */
    PluginResult(int value, std::string trace);

    /// @brief 结果值。
    int value() const { return m_value; }

    /// @brief 执行轨迹（用于观察 base -> python 的调用链）。
    std::string trace() const { return m_trace; }

    /**
     * @brief 设置结果值。
     * @param [in] int 新值
     */
    void setValue(int value) { m_value = value; }

    /**
     * @brief 设置执行轨迹。
     * @param [in] std::string 新轨迹
     */
    void setTrace(const std::string& trace) { m_trace = trace; }

private:
    int m_value = 0;     ///< 结果值
    std::string m_trace; ///< 执行轨迹
};

/**
 * @brief 演示"Python 继承 C++ 并重写虚函数"的基类。
 *
 * 注册为共享类型：Variant 里存 `std::shared_ptr<PluginBase>`，Python 侧看到
 * `PluginBase` 或其 Python 子类。C++ 通过基类指针调用虚函数时，若对象是
 * Python 子类实例，会派发到 Python 的重写实现（shiboken 生成的 wrapper 负责转发）。
 *
 * 虚析构 + 存活计数用于验证"两侧都不持有后对象正常释放"。
 */
class VT_DEMO_API PluginBase {
public:
    /// @brief 构造空插件（id=0，name 为空）。
    PluginBase();

    /**
     * @brief 构造插件。
     * @param [in] int 插件 id
     * @param [in] std::string 插件名
     */
    PluginBase(int id, const std::string& name);

    /// @brief 虚析构（Python 子类析构时递减存活计数）。
    virtual ~PluginBase();

    /// @brief 插件 id。
    int id() const { return m_id; }

    /// @brief 插件名。
    std::string name() const { return m_name; }

    /**
     * @brief 设置插件名。
     * @param [in] std::string 新名字
     */
    void setName(const std::string& name) { m_name = name; }

    /**
     * @brief 可被 Python 重写的计算。
     *
     * 基类实现返回 `value * 2`；Python 子类可以调用 `super().compute(...)` 后加工。
     * @param [in] int 输入值
     * @return int 计算结果
     */
    virtual int compute(int value) const;

    /**
     * @brief 可被 Python 重写的描述文本。
     *
     * 基类实现返回 `"plugin(" + name + ")"`。
     * @return std::string 描述
     */
    virtual std::string describe() const;

    /**
     * @brief 可被 Python 重写的求值；输入输出都是自定义值类型。
     *
     * 基类实现：
     * - operation == "scale"  -> value * scale
     * - operation == "offset" -> value + scale
     * trace 记录 `base:<operation>(<value>,<scale>)`。
     * Python 子类可以调用 `super().evaluate(request)` 后加工返回值。
     * @param [in] PluginRequest 求值请求
     * @return PluginResult 求值结果
     */
    virtual PluginResult evaluate(const PluginRequest& request) const;

    /// @brief 当前存活的 PluginBase 数量（测试用，含 Python 子类）。
    static int aliveCount();

private:
    int m_id = 0;        ///< id
    std::string m_name;  ///< 名字
};

/**
 * @brief 创建一个 shared_ptr 管理的 SharedNode。
 * @param [in] int id
 * @param [in] std::string 名字
 * @return std::shared_ptr<SharedNode> 共享节点
 */
VT_DEMO_API std::shared_ptr<SharedNode> makeSharedNode(int id, const std::string& name);

/**
 * @brief 创建一个由 C++ 静态容器持有、只借给 Python 的 SharedNode。
 *
 * 用于验证“借用对象不能进入共享模型”的错误路径。
 * @param [in] int id
 * @param [in] std::string 名字
 * @return SharedNode* 借用指针
 */
VT_DEMO_API SharedNode* borrowSharedNode(int id, const std::string& name);
