// UTF-8 字符串工具：裁剪、分割、格式化、终端显示宽度、宽窄字符转换。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mct::str {

std::string trim(std::string s);
std::string ltrim(std::string s);
std::string rtrim(std::string s);
std::string lower(std::string s);
std::string upper(std::string s);

bool starts_with(std::string_view s, std::string_view prefix);
bool ends_with(std::string_view s, std::string_view suffix);
bool iequals(std::string_view a, std::string_view b);
bool icontains(std::string_view haystack, std::string_view needle);
bool contains(std::string_view haystack, std::string_view needle);

std::vector<std::string> split(std::string_view s, char sep);
std::vector<std::string> split_str(std::string_view s, std::string_view sep);
std::vector<std::string> split_ws(std::string_view s);
std::string join(const std::vector<std::string>& parts, std::string_view sep);
std::string replace_all(std::string s, std::string_view from, std::string_view to);

// printf 风格格式化，返回 std::string。
std::string fmt(const char* format, ...);

// ---------------- UTF-8 ----------------
// 从 s[i] 解出一个码点并前进 i；非法字节返回 0xFFFD 且只前进 1 字节。
uint32_t decode_utf8(std::string_view s, size_t& i);
size_t utf8_length(std::string_view s);
std::string from_codepoint(uint32_t cp);

// 终端列宽：CJK 全角算 2，组合字符算 0，ANSI 转义序列算 0。
int char_width(uint32_t cp);
int display_width(std::string_view s);
std::string pad(std::string_view s, int width, bool align_right = false);
std::string ellipsize(std::string_view s, int width);
std::vector<std::string> wrap(std::string_view s, int width);

#ifdef _WIN32
std::wstring widen(std::string_view s);
std::string narrow(std::wstring_view s);
#endif

// URL 编码（RFC 3986，保留 -_.~ 与字母数字）
std::string percent_encode(std::string_view s);
std::string url_decode(std::string_view s);

// 人类可读
std::string human_size(uint64_t bytes);
std::string human_count(uint64_t n);
std::string human_duration(double seconds);

}  // namespace mct::str
