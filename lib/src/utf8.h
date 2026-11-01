#pragma once

// 内部 UTF-8 工具：JSON 与二进制共用。
// 严格校验：拒绝 overlong、代理区码点、> U+10FFFF、非法续字节。

#include <cstdint>
#include <string>
#include <string_view>

namespace vt {
namespace detail {

/**
 * @brief 解码一个 UTF-8 码点。
 * @param [in,out] const char* 当前指针（成功后前移）
 * @param [in] const char* 缓冲区末尾
 * @param [out] std::uint32_t 解码出的码点
 * @return bool 是否合法
 */
inline bool decodeUtf8(const char*& p, const char* end, std::uint32_t& cp) noexcept
{
    if (p >= end)
        return false;
    const auto b0 = static_cast<unsigned char>(*p);
    if (b0 < 0x80) {
        cp = b0;
        ++p;
        return true;
    }

    int extra = 0;
    std::uint32_t value = 0;
    std::uint32_t minValue = 0;

    if ((b0 & 0xE0) == 0xC0) {
        extra = 1;
        value = b0 & 0x1F;
        minValue = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
        extra = 2;
        value = b0 & 0x0F;
        minValue = 0x800;
    } else if ((b0 & 0xF8) == 0xF0) {
        extra = 3;
        value = b0 & 0x07;
        minValue = 0x10000;
    } else {
        return false;
    }

    if (end - p <= extra)
        return false;
    for (int i = 1; i <= extra; ++i) {
        const auto b = static_cast<unsigned char>(p[i]);
        if ((b & 0xC0) != 0x80)
            return false;
        value = (value << 6) | (b & 0x3F);
    }

    if (value < minValue)
        return false;                       // overlong
    if (value >= 0xD800 && value <= 0xDFFF)
        return false;                       // surrogate
    if (value > 0x10FFFF)
        return false;

    p += extra + 1;
    cp = value;
    return true;
}

/**
 * @brief 把一个码点编码成 UTF-8 并追加。
 * @param [out] std::string 输出字符串
 * @param [in] std::uint32_t 码点
 */
inline void appendUtf8(std::string& out, std::uint32_t cp)
{
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/**
 * @brief 判断整段文本是否为合法 UTF-8。
 * @param [in] std::string_view 文本
 * @return bool 是否合法
 */
inline bool isValidUtf8(std::string_view text) noexcept
{
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end) {
        std::uint32_t cp = 0;
        if (!decodeUtf8(p, end, cp))
            return false;
    }
    return true;
}

} // namespace detail
} // namespace vt
