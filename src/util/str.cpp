#include "util/str.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mct::str {

namespace {
bool is_space(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
bool is_wide(uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
         (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
         (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
         (cp >= 0xA960 && cp <= 0xA97F) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE10 && cp <= 0xFE19) ||
         (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
         (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
         (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD);
}
bool is_combining(uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
         (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
         (cp >= 0xFE20 && cp <= 0xFE2F) || cp == 0x200B || cp == 0xFEFF;
}
}  // namespace

std::string ltrim(std::string s) {
  size_t i = 0;
  while (i < s.size() && is_space(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}

std::string rtrim(std::string s) {
  size_t i = s.size();
  while (i > 0 && is_space(static_cast<unsigned char>(s[i - 1]))) --i;
  s.resize(i);
  return s;
}

std::string trim(std::string s) { return ltrim(rtrim(std::move(s))); }

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

bool starts_with(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

bool contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

bool icontains(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) return true;
  std::string h = lower(std::string(haystack));
  std::string n = lower(std::string(needle));
  return h.find(n) != std::string::npos;
}

std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == sep) {
      out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

std::vector<std::string> split_str(std::string_view s, std::string_view sep) {
  std::vector<std::string> out;
  if (sep.empty()) {
    out.emplace_back(s);
    return out;
  }
  size_t pos = 0;
  while (true) {
    size_t next = s.find(sep, pos);
    if (next == std::string_view::npos) {
      out.emplace_back(s.substr(pos));
      break;
    }
    out.emplace_back(s.substr(pos, next - pos));
    pos = next + sep.size();
  }
  return out;
}

std::vector<std::string> split_ws(std::string_view s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && is_space(static_cast<unsigned char>(s[i]))) ++i;
    size_t start = i;
    while (i < s.size() && !is_space(static_cast<unsigned char>(s[i]))) ++i;
    if (i > start) out.emplace_back(s.substr(start, i - start));
  }
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out.append(sep);
    out += parts[i];
  }
  return out;
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
  if (from.empty()) return s;
  std::string out;
  out.reserve(s.size());
  size_t pos = 0;
  while (true) {
    size_t next = s.find(from, pos);
    if (next == std::string::npos) {
      out.append(s, pos, std::string::npos);
      break;
    }
    out.append(s, pos, next - pos);
    out.append(to);
    pos = next + from.size();
  }
  return out;
}

std::string fmt(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  va_list ap2;
  va_copy(ap2, ap);
  int n = std::vsnprintf(nullptr, 0, format, ap2);
  va_end(ap2);
  std::string out;
  if (n > 0) {
    out.resize(static_cast<size_t>(n) + 1);
    std::vsnprintf(&out[0], out.size(), format, ap);
    out.resize(static_cast<size_t>(n));
  }
  va_end(ap);
  return out;
}

uint32_t decode_utf8(std::string_view s, size_t& i) {
  if (i >= s.size()) return 0;
  unsigned char c = static_cast<unsigned char>(s[i]);
  if (c < 0x80) {
    ++i;
    return c;
  }
  int extra = 0;
  uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0) {
    extra = 1;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    extra = 2;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    extra = 3;
    cp = c & 0x07;
  } else {
    ++i;
    return 0xFFFD;
  }
  if (i + static_cast<size_t>(extra) >= s.size()) {
    ++i;
    return 0xFFFD;
  }
  for (int k = 1; k <= extra; ++k) {
    unsigned char cc = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
    if ((cc & 0xC0) != 0x80) {
      ++i;
      return 0xFFFD;
    }
    cp = (cp << 6) | (cc & 0x3F);
  }
  i += static_cast<size_t>(extra) + 1;
  return cp;
}

size_t utf8_length(std::string_view s) {
  size_t n = 0;
  size_t i = 0;
  while (i < s.size()) {
    decode_utf8(s, i);
    ++n;
  }
  return n;
}

std::string from_codepoint(uint32_t cp) {
  std::string out;
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
  return out;
}

int char_width(uint32_t cp) {
  if (cp == 0) return 0;
  if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;
  if (is_combining(cp)) return 0;
  if (cp == 0x2026) return 1;  // 省略号按半角处理
  return is_wide(cp) ? 2 : 1;
}

int display_width(std::string_view s) {
  int width = 0;
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    // 跳过 ANSI 转义序列，避免把颜色码算进宽度
    if (c == 0x1B) {
      size_t j = i + 1;
      if (j < s.size() && s[j] == '[') {
        ++j;
        while (j < s.size() && !(s[j] >= '@' && s[j] <= '~')) ++j;
        i = j < s.size() ? j + 1 : s.size();
        continue;
      }
      i += 1;
      continue;
    }
    uint32_t cp = decode_utf8(s, i);
    width += char_width(cp);
  }
  return width;
}

std::string pad(std::string_view s, int width, bool align_right) {
  int w = display_width(s);
  if (w >= width) return std::string(s);
  std::string spaces(static_cast<size_t>(width - w), ' ');
  return align_right ? spaces + std::string(s) : std::string(s) + spaces;
}

std::string ellipsize(std::string_view s, int width) {
  if (width <= 0) return std::string();
  if (display_width(s) <= width) return std::string(s);
  std::string out;
  int w = 0;
  size_t i = 0;
  while (i < s.size()) {
    size_t before = i;
    uint32_t cp = decode_utf8(s, i);
    int cw = char_width(cp);
    if (w + cw > width - 1) break;
    out.append(s.substr(before, i - before));
    w += cw;
  }
  out += "…";
  return out;
}

std::vector<std::string> wrap(std::string_view s, int width) {
  std::vector<std::string> lines;
  if (width <= 0) {
    lines.emplace_back(s);
    return lines;
  }
  for (const std::string& paragraph : split(s, '\n')) {
    std::string cur;
    int w = 0;
    size_t i = 0;
    while (i < paragraph.size()) {
      size_t before = i;
      uint32_t cp = decode_utf8(paragraph, i);
      int cw = char_width(cp);
      if (w + cw > width) {
        lines.emplace_back(cur);
        cur.clear();
        w = 0;
        if (cw > width) continue;  // 单个超宽字符直接丢弃
      }
      cur.append(paragraph.substr(before, i - before));
      w += cw;
    }
    lines.emplace_back(cur);
  }
  return lines;
}

#ifdef _WIN32
std::wstring widen(std::string_view s) {
  if (s.empty()) return std::wstring();
  int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                 static_cast<int>(s.size()), nullptr, 0);
  if (need <= 0)
    need = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (need <= 0) return std::wstring();
  std::wstring out(static_cast<size_t>(need), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], need);
  return out;
}

std::string narrow(std::wstring_view s) {
  if (s.empty()) return std::string();
  int need = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                 nullptr, nullptr);
  if (need <= 0) return std::string();
  std::string out(static_cast<size_t>(need), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], need, nullptr,
                      nullptr);
  return out;
}
#endif

std::string percent_encode(std::string_view s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0xF];
    }
  }
  return out;
}

std::string url_decode(std::string_view s) {
  auto hex_value = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      int hi = hex_value(s[i + 1]);
      int lo = hex_value(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out += static_cast<char>((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    if (s[i] == '+')
      out += ' ';
    else
      out += s[i];
  }
  return out;
}

std::string human_size(uint64_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1024.0 && u < 4) {
    v /= 1024.0;
    ++u;
  }
  if (u == 0)
    return str::fmt("%llu B", static_cast<unsigned long long>(bytes));
  return str::fmt("%.1f %s", v, units[u]);
}

std::string human_count(uint64_t n) {
  if (n >= 100000000ULL) return str::fmt("%.1f 亿", static_cast<double>(n) / 1e8);
  if (n >= 10000ULL) return str::fmt("%.1f 万", static_cast<double>(n) / 1e4);
  return str::fmt("%llu", static_cast<unsigned long long>(n));
}

std::string human_duration(double seconds) {
  if (seconds < 0) seconds = 0;
  if (seconds < 60) return str::fmt("%.0f 秒", seconds);
  int total = static_cast<int>(seconds);
  int h = total / 3600;
  int m = (total % 3600) / 60;
  int s = total % 60;
  if (h > 0) return str::fmt("%d 小时 %d 分", h, m);
  return str::fmt("%d 分 %d 秒", m, s);
}

}  // namespace mct::str
