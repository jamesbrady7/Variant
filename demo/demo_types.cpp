#include "demo_types.h"

#include <atomic>
#include <memory>
#include <sstream>
#include <vector>

DemoNode::DemoNode() = default;

DemoNode::DemoNode(int id, const std::string& name)
    : m_id(id)
    , m_name(name)
{
}

std::string DemoNode::tagsJoined() const
{
    std::string out;
    for (std::size_t i = 0; i < m_tags.size(); ++i) {
        if (i)
            out += ",";
        out += m_tags[i];
    }
    return out;
}

std::string DemoColor::toString() const
{
    std::ostringstream oss;
    oss << "rgb(" << m_r << "," << m_g << "," << m_b << ")";
    return oss.str();
}


namespace {
std::atomic<int> g_sharedAlive{0};
}

SharedNode::SharedNode()
{
    ++g_sharedAlive;
}

SharedNode::SharedNode(int id, const std::string& name)
    : m_id(id)
    , m_name(name)
{
    ++g_sharedAlive;
}

SharedNode::~SharedNode()
{
    --g_sharedAlive;
}

int SharedNode::aliveCount()
{
    return g_sharedAlive.load();
}

std::shared_ptr<SharedNode> makeSharedNode(int id, const std::string& name)
{
    return std::make_shared<SharedNode>(id, name);
}

PluginRequest::PluginRequest(std::string operation, int value, int scale)
    : m_operation(std::move(operation))
    , m_value(value)
    , m_scale(scale)
{
}

PluginResult::PluginResult(int value, std::string trace)
    : m_value(value)
    , m_trace(std::move(trace))
{
}

namespace {
std::atomic<int> g_pluginAlive{0};
}

PluginBase::PluginBase()
{
    ++g_pluginAlive;
}

PluginBase::PluginBase(int id, const std::string& name)
    : m_id(id)
    , m_name(name)
{
    ++g_pluginAlive;
}

PluginBase::~PluginBase()
{
    --g_pluginAlive;
}

int PluginBase::compute(int value) const
{
    return value * 2;
}

std::string PluginBase::describe() const
{
    return "plugin(" + m_name + ")";
}

PluginResult PluginBase::evaluate(const PluginRequest& request) const
{
    int value = request.value();
    if (request.operation() == "scale")
        value *= request.scale();
    else if (request.operation() == "offset")
        value += request.scale();

    const std::string trace = "base:" + request.operation()
        + "(" + std::to_string(request.value()) + ","
        + std::to_string(request.scale()) + ")";
    return PluginResult(value, trace);
}

vt::VariantMap PluginBase::transform(const vt::VariantMap& input) const
{
    vt::VariantMap out = input;
    out["plugin_id"] = m_id;
    out["plugin_name"] = m_name;
    out["stage"] = std::string("base");
    out["value"] = input.getAt<int>("value").value_or(0) * 2;
    return out;
}

int PluginBase::aliveCount()
{
    return g_pluginAlive.load();
}

SharedNode* borrowSharedNode(int id, const std::string& name)
{
    static std::vector<std::unique_ptr<SharedNode>> held;
    held.push_back(std::make_unique<SharedNode>(id, name));
    return held.back().get();
}
