#include "vt/variant.h"

#include "vt/codec.h"
#include "vt/type_registry.h"
#include "vt/variant_list.h"
#include "vt/variant_map.h"

#include <algorithm>
#include <typeindex>
#include <utility>

namespace vt {

// 扩展槽的堆对象。用共享指针隔一层，避免 std::any(64B) 把 Variant 撑大。
struct VariantExt {
    std::any value;
};

// ---------------------------------------------------------------- VariantError

const char* toString(VariantErrorCode code) noexcept
{
    switch (code) {
    case VariantErrorCode::SyntaxError:      return "SyntaxError";
    case VariantErrorCode::EmptyPath:        return "EmptyPath";
    case VariantErrorCode::NotAMap:          return "NotAMap";
    case VariantErrorCode::NotAList:         return "NotAList";
    case VariantErrorCode::KeyNotFound:      return "KeyNotFound";
    case VariantErrorCode::IndexOutOfBounds: return "IndexOutOfBounds";
    case VariantErrorCode::TypeMismatch:     return "TypeMismatch";
    case VariantErrorCode::OutOfRange:       return "OutOfRange";
    }
    return "Unknown";
}

namespace {

std::string buildErrorMessage(VariantErrorCode code,
                              const std::string& message,
                              const std::string& path,
                              std::size_t segment,
                              std::size_t offset)
{
    std::string out = "[";
    out += toString(code);
    out += "] ";
    out += message;
    if (!path.empty()) {
        out += " (path=\"";
        out += path;
        out += "\", segment=";
        out += std::to_string(segment);
        out += ", offset=";
        out += std::to_string(offset);
        out += ")";
    }
    return out;
}

const char* builtinTypeName(Variant::Type t) noexcept
{
    switch (t) {
    case Variant::Type::Null:      return "null";
    case Variant::Type::Bool:      return "bool";
    case Variant::Type::Int:       return "int";
    case Variant::Type::Double:    return "double";
    case Variant::Type::String:    return "string";
    case Variant::Type::Map:       return "map";
    case Variant::Type::List:      return "list";
    case Variant::Type::Extension: return "extension";
    }
    return "unknown";
}

/// 扩展值比较的递归深度（防"编码结果里又包含同一类型"的 codec 自引用）。
thread_local int g_extensionCompareDepth = 0;

/**
 * @brief 比较两个扩展槽。
 *
 * 同一动态类型时：注册过 codec 就按 codec 编码出的普通 Variant 递归比较
 * （因此 `vector<int>` 等紧凑容器是内容比较）；没有 codec 则退化为只比较
 * 动态类型——`std::any` 没有通用相等语义，这是有意保留的旧约定。
 */
bool extensionEquals(const std::any& a, const std::any& b)
{
    if (a.type() != b.type())
        return false;

    const CodecRegistry::Entry* codec =
        CodecRegistry::instance().find(std::type_index(a.type()));
    if (!codec || !codec->encode)
        return true;   // 无 codec：只保证类型相同

    if (g_extensionCompareDepth >= 64)
        return true;   // 自引用型 codec：截断为类型比较，避免无限递归
    ++g_extensionCompareDepth;
    struct DepthGuard {
        ~DepthGuard() { --g_extensionCompareDepth; }
    } guard;

    try {
        const Variant left = codec->encode(a);
        const Variant right = codec->encode(b);
        return left == right;   // 递归回到 Variant::operator==
    } catch (...) {
        // codec 编不出来 = 这个值没有可比较的普通表示：退化为类型比较，
        // 与"未注册 codec"同一约定。返回 false 会破坏自反性（v == v 变假）。
        return true;
    }
}

} // namespace

VariantError::VariantError(VariantErrorCode code,
                           std::string message,
                           std::string path,
                           std::size_t segment,
                           std::size_t offset)
    : std::runtime_error(buildErrorMessage(code, message, path, segment, offset))
    , m_code(code)
    , m_path(std::move(path))
    , m_segment(segment)
    , m_offset(offset)
{
}

// ---------------------------------------------------------------- 构造

Variant::Variant(const char* v)
{
    if (v)
        m_storage = std::string(v);
}

Variant::Variant(VariantMap value)
    : m_storage(std::in_place_type<std::shared_ptr<VariantMap>>,
                std::make_shared<VariantMap>(std::move(value)))
{
}

Variant::Variant(VariantList value)
    : m_storage(std::in_place_type<std::shared_ptr<VariantList>>,
                std::make_shared<VariantList>(std::move(value)))
{
}

Variant Variant::fromValue(VariantMap value)
{
    return Variant(std::move(value));
}

Variant Variant::fromValue(VariantList value)
{
    return Variant(std::move(value));
}

Variant Variant::fromValue(std::any value)
{
    Variant v;
    v.m_storage.emplace<std::shared_ptr<VariantExt>>(
        std::make_shared<VariantExt>(VariantExt{std::move(value)}));
    return v;
}

// ---------------------------------------------------------------- 类型名

const char* Variant::typeName(Type t) noexcept
{
    return builtinTypeName(t);
}

std::string Variant::typeName() const
{
    if (isExtension()) {
        const auto& ti = extensionType();
        if (const TypeRegistry::Entry* e = TypeRegistry::instance().find(std::type_index(ti)))
            return e->name;
        return ti.name();
    }
    return builtinTypeName(type());
}

const std::type_info& Variant::extensionType() const noexcept
{
    if (!isExtension())
        return typeid(void);
    return std::get<std::shared_ptr<VariantExt>>(m_storage)->value.type();
}

std::any* Variant::extensionAny()
{
    auto* ptr = std::get_if<std::shared_ptr<VariantExt>>(&m_storage);
    if (!ptr || !*ptr)
        return nullptr;
    if (ptr->use_count() != 1)
        *ptr = std::make_shared<VariantExt>(**ptr);   // COW detach
    return &(*ptr)->value;
}

const std::any* Variant::extensionAny() const noexcept
{
    const auto* ptr = std::get_if<std::shared_ptr<VariantExt>>(&m_storage);
    if (!ptr || !*ptr)
        return nullptr;
    return &(*ptr)->value;
}

// ---------------------------------------------------------------- 取值

bool Variant::asBool() const
{
    if (!isBool())
        throw VariantError(VariantErrorCode::TypeMismatch,
                           std::string("expected bool, got ") + typeName());
    return std::get<bool>(m_storage);
}

std::int64_t Variant::asInt() const
{
    if (!isInt())
        throw VariantError(VariantErrorCode::TypeMismatch,
                           std::string("expected int, got ") + typeName());
    return std::get<std::int64_t>(m_storage);
}

double Variant::asDouble() const
{
    if (isDouble())
        return std::get<double>(m_storage);
    if (isInt())
        return static_cast<double>(std::get<std::int64_t>(m_storage));
    throw VariantError(VariantErrorCode::TypeMismatch,
                       std::string("expected double or int, got ") + typeName());
}

std::string Variant::asString() const
{
    if (!isString())
        throw VariantError(VariantErrorCode::TypeMismatch,
                           std::string("expected string, got ") + typeName());
    return std::get<std::string>(m_storage);
}

std::string_view Variant::asStringView() const
{
    if (!isString())
        throw VariantError(VariantErrorCode::TypeMismatch,
                           std::string("expected string, got ") + typeName());
    return std::get<std::string>(m_storage);
}

std::string_view Variant::asStringViewOr(std::string_view def) const noexcept
{
    if (!isString())
        return def;
    return std::get<std::string>(m_storage);
}

double Variant::asDoubleOr(double def) const noexcept
{
    if (isDouble())
        return std::get<double>(m_storage);
    if (isInt())
        return static_cast<double>(std::get<std::int64_t>(m_storage));
    return def;
}

std::string Variant::asStringOr(std::string def) const
{
    if (isString())
        return std::get<std::string>(m_storage);
    return def;
}

VariantMap& Variant::asMap()
{
    auto* ptr = std::get_if<std::shared_ptr<VariantMap>>(&m_storage);
    if (!ptr || !*ptr)
        throw VariantError(VariantErrorCode::NotAMap,
                           std::string("expected map, got ") + typeName());
    if (ptr->use_count() != 1)
        *ptr = std::make_shared<VariantMap>(**ptr);   // COW detach
    return **ptr;
}

const VariantMap& Variant::asMap() const
{
    const auto* ptr = std::get_if<std::shared_ptr<VariantMap>>(&m_storage);
    if (!ptr || !*ptr)
        throw VariantError(VariantErrorCode::NotAMap,
                           std::string("expected map, got ") + typeName());
    return **ptr;
}

VariantList& Variant::asList()
{
    auto* ptr = std::get_if<std::shared_ptr<VariantList>>(&m_storage);
    if (!ptr || !*ptr)
        throw VariantError(VariantErrorCode::NotAList,
                           std::string("expected list, got ") + typeName());
    if (ptr->use_count() != 1)
        *ptr = std::make_shared<VariantList>(**ptr);   // COW detach
    return **ptr;
}

const VariantList& Variant::asList() const
{
    const auto* ptr = std::get_if<std::shared_ptr<VariantList>>(&m_storage);
    if (!ptr || !*ptr)
        throw VariantError(VariantErrorCode::NotAList,
                           std::string("expected list, got ") + typeName());
    return **ptr;
}

// ---------------------------------------------------------------- 路径查找

const Variant* Variant::find(const Path& path) const noexcept
{
    const Variant* cur = this;
    for (const auto& seg : path.segments()) {
        if (seg.isIndex) {
            if (!cur->isList())
                return nullptr;
            const auto& ptr = std::get<std::shared_ptr<VariantList>>(cur->m_storage);
            if (seg.index >= ptr->size())
                return nullptr;
            cur = &(*ptr)[seg.index];
        } else {
            if (!cur->isMap())
                return nullptr;
            const auto& ptr = std::get<std::shared_ptr<VariantMap>>(cur->m_storage);
            const Variant* next = ptr->find(seg.key);
            if (!next)
                return nullptr;
            cur = next;
        }
    }
    return cur;
}

const Variant* Variant::find(const std::string& key) const noexcept
{
    if (!isMap())
        return nullptr;
    const auto& ptr = std::get<std::shared_ptr<VariantMap>>(m_storage);
    return ptr->find(key);
}

const Variant* Variant::find(std::string_view key) const
{
    return find(std::string(key));
}

const Variant* Variant::find(const char* key) const
{
    return find(std::string_view(key ? key : ""));
}

bool Variant::contains(std::string_view key) const
{
    return find(key) != nullptr;
}

const Variant& Variant::at(const Path& path) const
{
    const Variant* cur = this;
    for (std::size_t i = 0; i < path.segments().size(); ++i) {
        const auto& seg = path.segments()[i];
        if (seg.isIndex) {
            if (!cur->isList()) {
                throw VariantError(VariantErrorCode::NotAList,
                                   std::string("segment ") + std::to_string(i) + ": expected list, got " + cur->typeName(),
                                   path.source(), i, seg.offset);
            }
            const auto& ptr = std::get<std::shared_ptr<VariantList>>(cur->m_storage);
            if (seg.index >= ptr->size()) {
                throw VariantError(VariantErrorCode::IndexOutOfBounds,
                                   "segment " + std::to_string(i) + ": index " + std::to_string(seg.index) +
                                       " out of range [0," + std::to_string(ptr->size()) + ")",
                                   path.source(), i, seg.offset);
            }
            cur = &(*ptr)[seg.index];
        } else {
            if (!cur->isMap()) {
                throw VariantError(VariantErrorCode::NotAMap,
                                   std::string("segment ") + std::to_string(i) + ": expected map, got " + cur->typeName(),
                                   path.source(), i, seg.offset);
            }
            const auto& ptr = std::get<std::shared_ptr<VariantMap>>(cur->m_storage);
            const Variant* next = ptr->find(seg.key);
            if (!next) {
                throw VariantError(VariantErrorCode::KeyNotFound,
                                   "segment " + std::to_string(i) + ": key \"" + seg.key + "\" not found",
                                   path.source(), i, seg.offset);
            }
            cur = next;
        }
    }
    return *cur;
}

const Variant& Variant::at(std::string_view key) const
{
    if (!isMap())
        throw VariantError(VariantErrorCode::NotAMap,
                           std::string("expected map, got ") + typeName());
    const Variant* found = find(std::string(key));
    if (!found)
        throw VariantError(VariantErrorCode::KeyNotFound,
                           "key \"" + std::string(key) + "\" not found");
    return *found;
}

const Variant& Variant::at(std::size_t index) const
{
    if (!isList())
        throw VariantError(VariantErrorCode::NotAList,
                           std::string("expected list, got ") + typeName());
    return std::get<std::shared_ptr<VariantList>>(m_storage)->at(index);
}

Variant& Variant::at(const Path& path)
{
    // 可写入口必须逐层 detach：const 版本只读，直接 const_cast 会把写入
    // 落到与副本共享的容器里，破坏写时复制（改副本会污染原值）。
    Variant* cur = this;
    for (std::size_t i = 0; i < path.segments().size(); ++i) {
        const Path::Segment& seg = path.segments()[i];
        if (seg.isIndex) {
            if (!cur->isList()) {
                throw VariantError(VariantErrorCode::NotAList,
                                   std::string("segment ") + std::to_string(i) + ": expected list, got " +
                                       cur->typeName(),
                                   path.source(), i, seg.offset);
            }
            VariantList& list = cur->asList();   // 共享时在这里 detach
            if (seg.index >= list.size()) {
                throw VariantError(VariantErrorCode::IndexOutOfBounds,
                                   "segment " + std::to_string(i) + ": index " +
                                       std::to_string(seg.index) + " out of range [0," +
                                       std::to_string(list.size()) + ")",
                                   path.source(), i, seg.offset);
            }
            cur = &list[seg.index];
        } else {
            if (!cur->isMap()) {
                throw VariantError(VariantErrorCode::NotAMap,
                                   std::string("segment ") + std::to_string(i) + ": expected map, got " +
                                       cur->typeName(),
                                   path.source(), i, seg.offset);
            }
            VariantMap& map = cur->asMap();   // 共享时在这里 detach
            Variant* next = map.find(seg.key);
            if (!next) {
                throw VariantError(VariantErrorCode::KeyNotFound,
                                   "segment " + std::to_string(i) + ": key \"" + seg.key + "\" not found",
                                   path.source(), i, seg.offset);
            }
            cur = next;
        }
    }
    return *cur;
}

Variant& Variant::at(std::string_view key)
{
    // 先查类型、再 detach：失败路径不付出深拷贝。
    if (!isMap()) {
        throw VariantError(VariantErrorCode::NotAMap,
                           std::string("expected map, got ") + typeName());
    }
    return asMap().at(key);
}

Variant& Variant::at(std::size_t index)
{
    if (!isList()) {
        throw VariantError(VariantErrorCode::NotAList,
                           std::string("expected list, got ") + typeName());
    }
    return asList().at(index);
}

Variant& Variant::create(const Path& path)
{
    Variant* cur = this;
    for (std::size_t i = 0; i < path.segments().size(); ++i) {
        const auto& seg = path.segments()[i];
        if (seg.isIndex) {
            if (cur->isNull())
                cur->m_storage = std::make_shared<VariantList>();
            if (!cur->isList()) {
                throw VariantError(VariantErrorCode::NotAList,
                                   std::string("segment ") + std::to_string(i) + ": expected list, got " + cur->typeName(),
                                   path.source(), i, seg.offset);
            }
            auto& ptr = std::get<std::shared_ptr<VariantList>>(cur->m_storage);
            if (ptr.use_count() != 1)
                ptr = std::make_shared<VariantList>(*ptr);
            VariantList& list = *ptr;
            if (seg.index < list.size()) {
                cur = &list[seg.index];
            } else if (seg.index == list.size()) {
                cur = &list.emplace_back();
            } else {
                throw VariantError(VariantErrorCode::IndexOutOfBounds,
                                   "segment " + std::to_string(i) + ": index " + std::to_string(seg.index) +
                                       " out of range [0," + std::to_string(list.size()) + "]",
                                   path.source(), i, seg.offset);
            }
        } else {
            if (cur->isNull())
                cur->m_storage = std::make_shared<VariantMap>();
            if (!cur->isMap()) {
                throw VariantError(VariantErrorCode::NotAMap,
                                   std::string("segment ") + std::to_string(i) + ": expected map, got " + cur->typeName(),
                                   path.source(), i, seg.offset);
            }
            auto& ptr = std::get<std::shared_ptr<VariantMap>>(cur->m_storage);
            if (ptr.use_count() != 1)
                ptr = std::make_shared<VariantMap>(*ptr);
            cur = &(*ptr)[seg.key];
        }
    }
    return *cur;
}

Variant& Variant::operator[](std::string_view key)
{
    if (isNull())
        m_storage = std::make_shared<VariantMap>();
    return asMap()[key];
}

Variant Variant::operator[](std::string_view key) const
{
    if (const Variant* found = find(key))
        return *found;
    return Variant();
}

Variant& Variant::operator[](const Path& path)
{
    return create(path);
}

Variant Variant::operator[](const Path& path) const
{
    if (const Variant* found = find(path))
        return *found;
    return Variant();
}

Variant& Variant::operator[](std::size_t index)
{
    if (isNull())
        m_storage = std::make_shared<VariantList>();
    if (!isList()) {
        throw VariantError(VariantErrorCode::NotAList,
                           std::string("expected list, got ") + typeName());
    }
    auto& ptr = std::get<std::shared_ptr<VariantList>>(m_storage);
    if (ptr.use_count() != 1)
        ptr = std::make_shared<VariantList>(*ptr);
    VariantList& list = *ptr;
    if (index < list.size())
        return list[index];
    if (index == list.size())
        return list.emplace_back();
    throw VariantError(VariantErrorCode::IndexOutOfBounds,
                       "index " + std::to_string(index) + " out of range [0," +
                           std::to_string(list.size()) + "]");
}

Variant Variant::operator[](std::size_t index) const
{
    if (!isList())
        return Variant();
    const auto& list = *std::get<std::shared_ptr<VariantList>>(m_storage);
    if (index >= list.size())
        return Variant();
    return list[index];
}

bool Variant::set(const Path& path, Variant value) noexcept
{
    try {
        create(path) = std::move(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool Variant::set(std::string_view key, Variant value) noexcept
{
    try {
        if (isNull())
            m_storage = std::make_shared<VariantMap>();
        asMap()[key] = std::move(value);
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------- 比较

bool Variant::operator==(const Variant& other) const
{
    if (m_storage.index() != other.m_storage.index())
        return false;
    switch (type()) {
    case Type::Null:      return true;
    case Type::Bool:      return std::get<bool>(m_storage) == std::get<bool>(other.m_storage);
    case Type::Int:       return std::get<std::int64_t>(m_storage) == std::get<std::int64_t>(other.m_storage);
    case Type::Double:    return std::get<double>(m_storage) == std::get<double>(other.m_storage);
    case Type::String:    return std::get<std::string>(m_storage) == std::get<std::string>(other.m_storage);
    case Type::Map:       return asMap() == other.asMap();
    case Type::List:      return asList() == other.asList();
    case Type::Extension: {
        const std::any* a = extensionAny();
        const std::any* b = other.extensionAny();
        return a && b && extensionEquals(*a, *b);
    }
    }
    return false;
}

} // namespace vt
