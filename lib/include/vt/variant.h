#pragma once

#include "container_traits.h"
#include "export.h"
#include "path.h"
#include "variant_error.h"
#include "variant_fwd.h"

#include <any>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace vt {

namespace detail {

/**
 * @brief `fromValue` 的容器分支：登记归一化函数/自动 codec 后放进扩展槽。
 *
 * 定义在 `vt/normalize.h`（需要 VariantMap / VariantList 完整类型）：
 * 对容器类型调用 `Variant::fromValue` 的 TU 需要包含 `vt/normalize.h`
 * 或 `vt/adapters.h` / `vt/vt.h`。
 */
template <class T>
Variant extendValue(T&& value);

} // namespace detail

/**
 * @brief 值语义的类型擦除容器。
 *
 * 内部是 `std::variant` 的 8 个槽：
 * - 7 个内建热槽：Null / Bool / Int / Double / String / Map / List；
 * - 第 8 槽 `std::any` 承载运行时注册/未注册的扩展类型。
 *
 * map/list 经 `shared_ptr` 间接持有：递归类型不能直接作为 variant alternative，
 * 同时借引用计数实现写时复制（COW）——拷贝 O(1)，首次可变访问时按需克隆。
 */
class VT_API Variant {
public:
    /// 存储槽类型；数值顺序必须与 Storage 的 alternative 顺序一致。
    enum class Type : unsigned char {
        Null = 0,  ///< 空值
        Bool,      ///< bool
        Int,       ///< 统一为 int64_t
        Double,    ///< double
        String,    ///< std::string（UTF-8 约定）
        Map,       ///< VariantMap
        List,      ///< VariantList
        Extension  ///< 扩展槽（std::any）
    };

    /// @brief 构造空值。
    Variant() = default;

    /**
     * @brief 从 nullptr 构造空值。
     * @param [in] std::nullptr_t 空指针
     */
    Variant(std::nullptr_t) noexcept {}

    /**
     * @brief 从 bool 构造。
     * @param [in] bool 布尔值
     */
    Variant(bool v) noexcept : m_storage(v) {}

    /**
     * @brief 从 64 位整数构造。
     * @param [in] std::int64_t 整数值
     */
    Variant(std::int64_t v) noexcept : m_storage(v) {}

    /**
     * @brief 从 double 构造。
     * @param [in] double 浮点值
     */
    Variant(double v) noexcept : m_storage(v) {}

    /**
     * @brief 从 C 字符串构造 String；空指针得到 Null。
     * @param [in] const char* UTF-8 字符串（可为 nullptr）
     */
    Variant(const char* v);

    /**
     * @brief 从 string_view 构造 String（拷贝内容）。
     * @param [in] std::string_view 字符串视图
     */
    Variant(std::string_view v) : m_storage(std::string(v)) {}

    /**
     * @brief 从 std::string 构造 String（移动）。
     * @param [in] std::string 字符串
     */
    Variant(std::string v) : m_storage(std::move(v)) {}

    /**
     * @brief 从任意整型构造 Int。
     *
     * 所有整型（含 char/short/unsigned）都落到 Int 槽；bool 被上方精确匹配拿走。
     * 无符号值超出 int64 可表示范围时抛异常，**不静默回绕**。
     * @param [in] T 整型
     * @throws VariantError 无符号值大于 INT64_MAX（OutOfRange）
     */
    template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    Variant(T v) : Variant(toInt64(v)) {}

    /**
     * @brief 从 VariantMap 构造 Map（定义在 .cpp，因类型在此不完整）。
     * @param [in] VariantMap 映射表
     */
    Variant(VariantMap value);

    /**
     * @brief 从 VariantList 构造 List（定义在 .cpp）。
     * @param [in] VariantList 列表
     */
    Variant(VariantList value);

    /// 拷贝构造：标量/字符串深拷贝，map/list/扩展槽共享（COW）。
    Variant(const Variant&) = default;
    /// 移动构造。
    Variant(Variant&&) = default;
    /// 拷贝赋值。
    Variant& operator=(const Variant&) = default;
    /// 移动赋值。
    Variant& operator=(Variant&&) = default;
    /// 析构。
    ~Variant() = default;

    // ---------------------------------------------------------------- 类型

    /**
     * @brief 当前存储槽类型。
     * @return Type 类型枚举
     */
    Type type() const noexcept { return static_cast<Type>(m_storage.index()); }

    /// @brief 是否 Null。
    bool isNull() const noexcept { return type() == Type::Null; }
    /// @brief 是否 Bool。
    bool isBool() const noexcept { return type() == Type::Bool; }
    /// @brief 是否 Int。
    bool isInt() const noexcept { return type() == Type::Int; }
    /// @brief 是否 Double。
    bool isDouble() const noexcept { return type() == Type::Double; }
    /// @brief 是否 String。
    bool isString() const noexcept { return type() == Type::String; }
    /// @brief 是否 Map。
    bool isMap() const noexcept { return type() == Type::Map; }
    /// @brief 是否 List。
    bool isList() const noexcept { return type() == Type::List; }
    /// @brief 是否扩展槽。
    bool isExtension() const noexcept { return type() == Type::Extension; }

    /**
     * @brief 类型的静态名字。
     * @param [in] Type 类型枚举
     * @return const char* 内建类型名（"null"/"bool"/...）
     */
    static const char* typeName(Type t) noexcept;

    /**
     * @brief 当前值的类型名。
     *
     * 扩展类型返回注册名；未注册的类型回退到 `typeid(T).name()`。
     * @return std::string 类型名
     */
    std::string typeName() const;

    /**
     * @brief 扩展槽的动态类型。
     * @return const std::type_info& 非扩展时返回 `typeid(void)`
     */
    const std::type_info& extensionType() const noexcept;

    /**
     * @brief 取扩展槽的原始 std::any（可写；共享时会 detach）。
     *
     * 不标 noexcept：共享时 detach 会分配内存（bad_alloc 应能被上层看到，
     * 而不是直接 terminate）。
     * @return std::any* 非扩展时返回 nullptr
     */
    std::any* extensionAny();

    /**
     * @brief 取扩展槽的原始 std::any（只读，不 detach）。
     * @return const std::any* 非扩展时返回 nullptr
     */
    const std::any* extensionAny() const noexcept;

    // ---------------------------------------------------------- 严格/宽松取值

    /**
     * @brief 严格取 bool。
     * @return bool 布尔值
     * @throws VariantError 类型不符（TypeMismatch）
     */
    bool asBool() const;

    /**
     * @brief 严格取 int64。
     * @return std::int64_t 整数值
     * @throws VariantError 类型不符
     */
    std::int64_t asInt() const;

    /**
     * @brief 取 double；Int 可隐式转换。
     * @return double 浮点值
     * @throws VariantError 类型不符
     */
    double asDouble() const;

    /**
     * @brief 取 String 的拷贝。
     * @return std::string 字符串
     * @throws VariantError 类型不符
     */
    std::string asString() const;

    /**
     * @brief 取 String 的只读视图（零拷贝）。
     * @return std::string_view 指向内部字符串
     * @throws VariantError 类型不符
     */
    std::string_view asStringView() const;

    /**
     * @brief 取 String 视图；类型不符返回默认值。
     * @param [in] std::string_view 默认值
     * @return std::string_view 字符串视图
     */
    std::string_view asStringViewOr(std::string_view def = {}) const noexcept;

    /**
     * @brief 取 Map 的可写引用（必要时 COW detach）。
     * @return VariantMap& 映射表
     * @throws VariantError 不是 Map
     */
    VariantMap& asMap();

    /**
     * @brief 取 Map 的只读引用（不 detach）。
     * @return const VariantMap& 映射表
     * @throws VariantError 不是 Map
     */
    const VariantMap& asMap() const;

    /**
     * @brief 取 List 的可写引用（必要时 COW detach）。
     * @return VariantList& 列表
     * @throws VariantError 不是 List
     */
    VariantList& asList();

    /**
     * @brief 取 List 的只读引用（不 detach）。
     * @return const VariantList& 列表
     * @throws VariantError 不是 List
     */
    const VariantList& asList() const;

    /**
     * @brief 取 bool；类型不符返回默认值。
     * @param [in] bool 默认值
     * @return bool 布尔值
     */
    bool asBoolOr(bool def) const noexcept { return isBool() ? std::get<bool>(m_storage) : def; }

    /**
     * @brief 取 int64；类型不符返回默认值。
     * @param [in] std::int64_t 默认值
     * @return std::int64_t 整数值
     */
    std::int64_t asIntOr(std::int64_t def) const noexcept { return isInt() ? std::get<std::int64_t>(m_storage) : def; }

    /**
     * @brief 取 double；类型不符返回默认值。
     * @param [in] double 默认值
     * @return double 浮点值
     */
    double asDoubleOr(double def) const noexcept;

    /**
     * @brief 取 String 拷贝；类型不符返回默认值。
     * @param [in] std::string 默认值
     * @return std::string 字符串
     */
    std::string asStringOr(std::string def) const;

    // ---------------------------------------------------------- 泛型取值

    /**
     * @brief 判断是否可按下述类型读取。
     *
     * 标量按槽判断；浮点类型接受 Int；其他类型要求扩展槽动态类型完全一致。
     * @param [in] T 目标类型
     * @return bool 是否匹配
     */
    template <class T>
    bool is() const noexcept {
        using U = std::decay_t<T>;
        if constexpr (std::is_same_v<U, bool>) {
            return isBool();
        } else if constexpr (std::is_integral_v<U>) {
            return isInt();
        } else if constexpr (std::is_floating_point_v<U>) {
            return isDouble() || isInt();
        } else if constexpr (std::is_same_v<U, std::string>) {
            return isString();
        } else {
            return isExtension() && extensionType() == typeid(U);
        }
    }

    /**
     * @brief 尝试按 T 读取。
     *
     * 整型做范围检查；浮点接受 Int；扩展槽用 any_cast。
     * @param [in] T 目标类型
     * @return std::optional<T> 失败返回 nullopt
     */
    template <class T>
    std::optional<T> get() const {
        using U = std::decay_t<T>;
        if constexpr (std::is_same_v<U, bool>) {
            if (isBool()) return std::get<bool>(m_storage);
        } else if constexpr (std::is_integral_v<U>) {
            if (isInt()) {
                const auto v = std::get<std::int64_t>(m_storage);
                if constexpr (std::is_signed_v<U>) {
                    if (v < static_cast<std::int64_t>(std::numeric_limits<U>::min()) ||
                        v > static_cast<std::int64_t>(std::numeric_limits<U>::max()))
                        return std::nullopt;
                } else {
                    if (v < 0 ||
                        static_cast<std::uint64_t>(v) > static_cast<std::uint64_t>(std::numeric_limits<U>::max()))
                        return std::nullopt;
                }
                return static_cast<U>(v);
            }
        } else if constexpr (std::is_floating_point_v<U>) {
            if (isDouble()) return static_cast<U>(std::get<double>(m_storage));
            if (isInt()) return static_cast<U>(std::get<std::int64_t>(m_storage));
        } else if constexpr (std::is_same_v<U, std::string>) {
            if (isString()) return std::get<std::string>(m_storage);
        } else {
            if (const std::any* any = extensionAny()) {
                if (const auto* p = std::any_cast<U>(any))
                    return *p;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief 按 T 读取，失败返回默认值。
     * @param [in] T 默认值
     * @return T 读到的值或默认值
     */
    template <class T>
    T getOr(T def) const {
        auto v = get<T>();
        return v ? *v : std::move(def);
    }

    /**
     * @brief 扩展槽中 T 的只读指针。
     * @param [in] T 目标类型
     * @return const T* 非扩展或类型不符时 nullptr
     */
    template <class T>
    const T* extensionPtr() const noexcept {
        if (const std::any* any = extensionAny())
            return std::any_cast<T>(any);
        return nullptr;
    }

    /**
     * @brief 扩展槽中 T 的可写指针（共享时会 detach；可能分配内存）。
     * @param [in] T 目标类型
     * @return T* 非扩展或类型不符时 nullptr
     */
    template <class T>
    T* extensionPtr() {
        if (std::any* any = extensionAny())
            return std::any_cast<T>(any);
        return nullptr;
    }

    // ---------------------------------------------------------- 构造

    /**
     * @brief 用任意 std::any 构造扩展值。
     * @param [in] std::any 任意值
     * @return Variant 扩展槽 Variant
     */
    static Variant fromValue(std::any value);

    /**
     * @brief 原样返回 Variant。
     * @param [in] Variant 值
     * @return Variant 同一个值
     */
    static Variant fromValue(Variant v) { return v; }

    /**
     * @brief 从 VariantMap 构造 Map。
     * @param [in] VariantMap 映射表
     * @return Variant Map 值
     */
    static Variant fromValue(VariantMap value);

    /**
     * @brief 从 VariantList 构造 List。
     * @param [in] VariantList 列表
     * @return Variant List 值
     */
    static Variant fromValue(VariantList value);

    /**
     * @brief 规范化 vector<Variant> 为原生 List。
     * @param [in] std::vector<Variant> 列表
     * @return Variant List 值
     */
    static Variant fromValue(std::vector<Variant> value);

    /**
     * @brief 规范化 map<string,Variant> 为原生 Map。
     * @param [in] std::unordered_map<std::string,Variant> 映射表
     * @return Variant Map 值
     */
    static Variant fromValue(std::unordered_map<std::string, Variant> value);

    /**
     * @brief 把 vector<int> 放进扩展槽（紧凑存储）。
     * @param [in] std::vector<int> 整数数组
     * @return Variant 扩展值
     */
    static Variant fromValue(std::vector<int> value);

    /**
     * @brief 把 vector<string> 放进扩展槽。
     * @param [in] std::vector<std::string> 字符串数组
     * @return Variant 扩展值
     */
    static Variant fromValue(std::vector<std::string> value);

    /**
     * @brief 把 map<string,string> 放进扩展槽。
     * @param [in] std::unordered_map<std::string,std::string> 字符串映射
     * @return Variant 扩展值
     */
    static Variant fromValue(std::unordered_map<std::string, std::string> value);

    /**
     * @brief 把 map<int,int> 放进扩展槽。
     * @param [in] std::unordered_map<int,int> 整数映射
     * @return Variant 扩展值
     */
    static Variant fromValue(std::unordered_map<int, int> value);

    /**
     * @brief 通用构造：内建类型进对应槽，其他类型进扩展槽。
     * @param [in] T 任意可拷贝类型
     * @return Variant 值
     */
    template <class T, std::enable_if_t<
        !std::is_same_v<std::decay_t<T>, Variant> &&
        !std::is_same_v<std::decay_t<T>, VariantMap> &&
        !std::is_same_v<std::decay_t<T>, VariantList>, int> = 0>
    static Variant fromValue(T&& value) {
        using U = std::decay_t<T>;
        if constexpr (std::is_same_v<U, bool>) {
            return Variant(value);
        } else if constexpr (std::is_integral_v<U>) {
            return Variant(value);   // 范围检查在整型构造里（无符号越界抛 OutOfRange）
        } else if constexpr (std::is_floating_point_v<U>) {
            return Variant(static_cast<double>(value));
        } else if constexpr (std::is_same_v<U, std::string>) {
            return Variant(std::move(value));
        } else if constexpr (std::is_same_v<U, std::string_view>) {
            return Variant(std::string(value));   // 拷贝，避免存一个可能悬空的 view
        } else if constexpr (std::is_same_v<U, const char*> || std::is_same_v<U, char*>) {
            return Variant(value);
        } else if constexpr (std::is_same_v<U, std::nullptr_t>) {
            return Variant();
        } else if constexpr (detail::is_container_v<U>) {
            // 容器：紧凑存进扩展槽，同时登记归一化函数与自动 codec，
            // 之后的 Python 转换 / 嵌套序列化 / getAs 都能递归下去。
            return detail::extendValue(std::forward<T>(value));
        } else {
            return fromValue(std::any(std::forward<T>(value)));
        }
    }

    // ============================================================
    // 访问契约（重要）：
    //   字符串 = 字面键；Path  = 路径。语义由【参数类型】决定，
    //   不猜字符串内容，也不用不同的方法名区分。
    //     v["a.b"]        -> 子键 "a.b"
    //     v["a.b"_path]   -> 路径 a -> b
    // ============================================================

    /**
     * @brief 以字面键访问子节点；Null 自动变 Map，缺失键自动创建。
     * @param [in] std::string_view 键
     * @return Variant& 子节点引用
     * @throws VariantError 当前值不是 Map / Null
     */
    Variant& operator[](std::string_view key);

    /**
     * @brief 以字面键只读访问；缺失或类型不符返回 Null 拷贝。
     * @param [in] std::string_view 键
     * @return Variant 子节点拷贝
     */
    Variant operator[](std::string_view key) const;

    /**
     * @brief 以列表下标访问；Null 自动变 List，index == size 时追加。
     * @param [in] std::size_t 下标
     * @return Variant& 元素引用
     * @throws VariantError 当前值不是 List / 下标越界
     */
    Variant& operator[](std::size_t index);

    /**
     * @brief 以列表下标只读访问；越界返回 Null 拷贝。
     * @param [in] std::size_t 下标
     * @return Variant 元素拷贝
     */
    Variant operator[](std::size_t index) const;

    /**
     * @brief 按字面键查找（不抛）。
     *
     * 内部需要构造 std::string（C++17 的 unordered_map 没有异构查找），
     * 因此不标 noexcept。
     * @param [in] std::string_view 键
     * @return const Variant* 缺失或类型不符返回 nullptr
     */
    const Variant* find(std::string_view key) const;

    /**
     * @brief 按字面键查找（不抛，无临时分配）。
     * @param [in] std::string 键
     * @return const Variant* 缺失或类型不符返回 nullptr
     */
    const Variant* find(const std::string& key) const noexcept;

    /**
     * @brief 按字面键查找（不抛，C 字符串）。
     * @param [in] const char* 键
     * @return const Variant* 缺失或类型不符返回 nullptr
     */
    const Variant* find(const char* key) const;

    /**
     * @brief 按字面键严格取值。
     * @param [in] std::string_view 键
     * @return const Variant& 子节点
     * @throws VariantError 不是 Map / 键不存在
     */
    const Variant& at(std::string_view key) const;

    /**
     * @brief 按字面键严格取可写引用。
     * @param [in] std::string_view 键
     * @return Variant& 子节点
     * @throws VariantError 不是 Map / 键不存在
     */
    Variant& at(std::string_view key);

    /**
     * @brief 按列表下标严格取值。
     * @param [in] std::size_t 下标
     * @return const Variant& 元素
     * @throws VariantError 不是 List / 下标越界
     */
    const Variant& at(std::size_t index) const;

    /**
     * @brief 按列表下标严格取可写引用。
     * @param [in] std::size_t 下标
     * @return Variant& 元素
     * @throws VariantError 不是 List / 下标越界
     */
    Variant& at(std::size_t index);

    /**
     * @brief 是否含有某个字面键。
     * @param [in] std::string_view 键
     * @return bool 是否存在
     */
    bool contains(std::string_view key) const;

    /**
     * @brief 以字面键写入；失败返回 false（不抛）。
     * @param [in] std::string_view 键
     * @param [in] Variant 值
     * @return bool 是否成功
     */
    bool set(std::string_view key, Variant value) noexcept;

    /**
     * @brief 按字面键取值。
     * @param [in] std::string_view 键
     * @return std::optional<T> 缺失或类型不符返回 nullopt
     */
    template <class T>
    std::optional<T> getAt(std::string_view key) const {
        if (const Variant* p = find(key)) return p->get<T>();
        return std::nullopt;
    }

    /**
     * @brief 按字面键取值，失败返回默认值。
     * @param [in] std::string_view 键
     * @param [in] T 默认值
     * @return T 值或默认值
     */
    template <class T>
    T getAtOr(std::string_view key, T def) const {
        auto v = getAt<T>(key);
        return v ? *v : std::move(def);
    }

    // ---- 路径（显式 Path 类型）----
    // 空路径 = 自身。

    /**
     * @brief 按路径写入；自动创建中间 Map/List。
     * @param [in] Path 路径
     * @return Variant& 目标节点引用
     * @throws VariantError 路径类型冲突 / 下标越界
     */
    Variant& operator[](const Path& path);

    /**
     * @brief 按路径只读访问；缺失返回 Null 拷贝。
     * @param [in] Path 路径
     * @return Variant 目标节点拷贝
     */
    Variant operator[](const Path& path) const;

    /**
     * @brief 按路径查找（不抛）。
     * @param [in] Path 路径
     * @return const Variant* 缺失返回 nullptr；空路径返回自身
     */
    const Variant* find(const Path& path) const noexcept;

    /**
     * @brief 按路径严格取值。
     * @param [in] Path 路径
     * @return const Variant& 目标节点
     * @throws VariantError 路径不存在 / 类型冲突 / 越界
     */
    const Variant& at(const Path& path) const;

    /**
     * @brief 按路径严格取可写引用。
     * @param [in] Path 路径
     * @return Variant& 目标节点
     * @throws VariantError 路径不存在 / 类型冲突 / 越界
     */
    Variant& at(const Path& path);

    /**
     * @brief 按路径创建并返回目标节点（供容器转发剩余路径）。
     * @param [in] Path 路径
     * @return Variant& 目标节点引用
     * @throws VariantError 类型冲突 / 下标越界
     */
    Variant& create(const Path& path);

    /**
     * @brief 按路径写入；失败返回 false（不抛）。
     * @param [in] Path 路径
     * @param [in] Variant 值
     * @return bool 是否成功
     */
    bool set(const Path& path, Variant value) noexcept;

    /**
     * @brief 按路径取值。
     * @param [in] Path 路径
     * @return std::optional<T> 缺失或类型不符返回 nullopt
     */
    template <class T>
    std::optional<T> getAt(const Path& path) const {
        if (const Variant* p = find(path)) return p->get<T>();
        return std::nullopt;
    }

    /**
     * @brief 按路径取值，失败返回默认值。
     * @param [in] Path 路径
     * @param [in] T 默认值
     * @return T 值或默认值
     */
    template <class T>
    T getAtOr(const Path& path, T def) const {
        auto v = getAt<T>(path);
        return v ? *v : std::move(def);
    }

    // ---------------------------------------------------------- 其他

    /**
     * @brief 值相等比较。
     *
     * 标量/字符串/容器递归比较。扩展类型：注册了 codec 时按 codec 编码出的
     * 普通 Variant 比较（因此 `vector<int>` 等紧凑容器是内容比较，代价是一次
     * 编码）；未注册 codec 或编码失败时只比较动态类型（std::any 没有通用
     * 相等语义，这样至少保证 `v == v`）。
     * @param [in] Variant 另一个值
     * @return bool 是否相等
     */
    bool operator==(const Variant& other) const;

    /**
     * @brief 值不等比较。
     * @param [in] Variant 另一个值
     * @return bool 是否不等
     */
    bool operator!=(const Variant& other) const { return !(*this == other); }

private:
    /// 整型统一的收口点：无符号越界抛 OutOfRange，避免静默回绕成负数。
    template <class T>
    static std::int64_t toInt64(T value)
    {
        if constexpr (std::is_unsigned_v<T>) {
            if (value > static_cast<T>(std::numeric_limits<std::int64_t>::max()))
                throw VariantError(VariantErrorCode::OutOfRange,
                                   "unsigned integer exceeds int64 range");
        }
        return static_cast<std::int64_t>(value);
    }

    struct NullTag {};  ///< Null 槽的占位类型

    /// 八个存储槽；顺序必须与 Type 枚举一致。
    using Storage = std::variant<
        NullTag,                          ///< Type::Null
        bool,                             ///< Type::Bool
        std::int64_t,                     ///< Type::Int
        double,                           ///< Type::Double
        std::string,                      ///< Type::String
        std::shared_ptr<VariantMap>,      ///< Type::Map（共享句柄，COW）
        std::shared_ptr<VariantList>,     ///< Type::List（共享句柄，COW）
        std::shared_ptr<VariantExt>       ///< Type::Extension（内部 std::any）
    >;

    Storage m_storage{};  ///< 实际存储
};

} // namespace vt
