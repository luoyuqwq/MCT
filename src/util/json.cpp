#include "util/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "util/fs.hpp"
#include "util/str.hpp"

namespace mct {

namespace {

class Parser {
 public:
  explicit Parser(const std::string& text) : s_(text) {}

  bool parse(Json& out, std::string* error) {
    if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF &&
        static_cast<unsigned char>(s_[1]) == 0xBB && static_cast<unsigned char>(s_[2]) == 0xBF) {
      pos_ = 3;
    }
    skip_ws();
    if (!value(out, 0)) {
      if (error) *error = err_;
      return false;
    }
    skip_ws();
    if (pos_ != s_.size()) {
      fail("JSON 结束后仍有多余内容");
      if (error) *error = err_;
      return false;
    }
    return true;
  }

 private:
  bool fail(const std::string& msg) {
    if (err_.empty()) err_ = "JSON 解析失败（第 " + std::to_string(pos_ + 1) + " 字节）：" + msg;
    return false;
  }

  void skip_ws() {
    while (pos_ < s_.size()) {
      char c = s_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool literal(const char* text, size_t len, Json& out, bool value) {
    if (s_.compare(pos_, len, text, len) != 0) return fail("无法识别的字面量");
    pos_ += len;
    out = Json(value);
    return true;
  }

  bool value(Json& out, int depth) {
    if (depth > 200) return fail("嵌套层级过深");
    skip_ws();
    if (pos_ >= s_.size()) return fail("意外的输入结束");
    char c = s_[pos_];
    switch (c) {
      case '{':
        return object(out, depth);
      case '[':
        return array(out, depth);
      case '"': {
        std::string text;
        if (!string(text)) return false;
        out = Json(std::move(text));
        return true;
      }
      case 't':
        return literal("true", 4, out, true);
      case 'f':
        return literal("false", 5, out, false);
      case 'n':
        if (!literal("null", 4, out, false)) return false;
        out = Json();
        return true;
      default:
        return number(out);
    }
  }

  bool object(Json& out, int depth) {
    ++pos_;  // {
    out = Json::object();
    skip_ws();
    if (pos_ < s_.size() && s_[pos_] == '}') {
      ++pos_;
      return true;
    }
    while (true) {
      skip_ws();
      if (pos_ >= s_.size() || s_[pos_] != '"') return fail("对象的键必须是字符串");
      std::string key;
      if (!string(key)) return false;
      skip_ws();
      if (pos_ >= s_.size() || s_[pos_] != ':') return fail("缺少冒号");
      ++pos_;
      Json v;
      if (!value(v, depth + 1)) return false;
      out.set(std::move(key), std::move(v));
      skip_ws();
      if (pos_ >= s_.size()) return fail("对象未闭合");
      if (s_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (s_[pos_] == '}') {
        ++pos_;
        return true;
      }
      return fail("对象中出现意外字符");
    }
  }

  bool array(Json& out, int depth) {
    ++pos_;  // [
    out = Json::array();
    skip_ws();
    if (pos_ < s_.size() && s_[pos_] == ']') {
      ++pos_;
      return true;
    }
    while (true) {
      Json v;
      if (!value(v, depth + 1)) return false;
      out.push_back(std::move(v));
      skip_ws();
      if (pos_ >= s_.size()) return fail("数组未闭合");
      if (s_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (s_[pos_] == ']') {
        ++pos_;
        return true;
      }
      return fail("数组中出现意外字符");
    }
  }

  bool number(Json& out) {
    const char* start = s_.c_str() + pos_;
    char* end = nullptr;
    double d = std::strtod(start, &end);
    if (end == start) return fail("非法的数字");
    pos_ = static_cast<size_t>(end - s_.c_str());
    out = Json(d);
    return true;
  }

  bool string(std::string& out) {
    ++pos_;  // "
    out.clear();
    while (pos_ < s_.size()) {
      unsigned char c = static_cast<unsigned char>(s_[pos_++]);
      if (c == '"') return true;
      if (c == '\\') {
        if (pos_ >= s_.size()) return fail("转义序列不完整");
        char e = s_[pos_++];
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            if (pos_ + 4 > s_.size()) return fail("\\u 转义不完整");
            uint32_t cp = 0;
            for (int k = 0; k < 4; ++k) {
              char h = s_[pos_++];
              cp <<= 4;
              if (h >= '0' && h <= '9')
                cp |= static_cast<uint32_t>(h - '0');
              else if (h >= 'a' && h <= 'f')
                cp |= static_cast<uint32_t>(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F')
                cp |= static_cast<uint32_t>(h - 'A' + 10);
              else
                return fail("\\u 转义含非法字符");
            }
            if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= s_.size() && s_[pos_] == '\\' &&
                s_[pos_ + 1] == 'u') {
              pos_ += 2;
              uint32_t low = 0;
              for (int k = 0; k < 4; ++k) {
                char h = s_[pos_++];
                low <<= 4;
                if (h >= '0' && h <= '9')
                  low |= static_cast<uint32_t>(h - '0');
                else if (h >= 'a' && h <= 'f')
                  low |= static_cast<uint32_t>(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F')
                  low |= static_cast<uint32_t>(h - 'A' + 10);
                else
                  return fail("\\u 转义含非法字符");
              }
              if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
              }
            }
            out += str::from_codepoint(cp);
            break;
          }
          default:
            return fail("未知的转义字符");
        }
        continue;
      }
      if (c < 0x20) return fail("字符串中包含未转义的控制字符");
      out += static_cast<char>(c);
    }
    return fail("字符串未闭合");
  }

  const std::string& s_;
  size_t pos_ = 0;
  std::string err_;
};

}  // namespace

Json::Json() = default;
Json::Json(std::nullptr_t) : type_(Type::Null) {}
Json::Json(bool v) : type_(Type::Bool), bool_(v) {}
Json::Json(int v) : type_(Type::Number), number_(v) {}
Json::Json(long long v) : type_(Type::Number), number_(static_cast<double>(v)) {}
Json::Json(double v) : type_(Type::Number), number_(v) {}
Json::Json(const char* v) : type_(Type::String), string_(v ? v : "") {}
Json::Json(std::string v) : type_(Type::String), string_(std::move(v)) {}

Json Json::object() {
  Json j;
  j.type_ = Type::Object;
  return j;
}

Json Json::array() {
  Json j;
  j.type_ = Type::Array;
  return j;
}

Json Json::parse(const std::string& text, std::string* error) {
  Json out;
  Parser p(text);
  if (!p.parse(out, error)) return Json();
  return out;
}

bool Json::parse_file(const std::string& path, Json& out, std::string* error) {
  std::string text;
  if (!fs::read_file(path, text)) {
    if (error) *error = "无法读取文件: " + path;
    return false;
  }
  std::string err;
  out = Json::parse(text, &err);
  if (!err.empty()) {
    if (error) *error = err + " (" + path + ")";
    return false;
  }
  return true;
}

bool Json::as_bool(bool fallback) const {
  if (type_ == Type::Bool) return bool_;
  if (type_ == Type::Number) return number_ != 0.0;
  return fallback;
}

double Json::as_number(double fallback) const {
  if (type_ == Type::Number) return number_;
  if (type_ == Type::String) {
    char* end = nullptr;
    double d = std::strtod(string_.c_str(), &end);
    if (end != string_.c_str()) return d;
  }
  return fallback;
}

long long Json::as_int(long long fallback) const {
  if (type_ == Type::Number) return static_cast<long long>(number_);
  if (type_ == Type::String) {
    try {
      return std::stoll(string_);
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

std::string Json::as_string(std::string fallback) const {
  if (type_ == Type::String) return string_;
  return fallback;
}

bool Json::has(std::string_view key) const {
  if (type_ != Type::Object) return false;
  for (const auto& kv : object_) {
    if (kv.first == key) return true;
  }
  return false;
}

const Json& Json::operator[](std::string_view key) const {
  static const Json kNull;
  if (type_ != Type::Object) return kNull;
  for (const auto& kv : object_) {
    if (kv.first == key) return kv.second;
  }
  return kNull;
}

const Json& Json::operator[](size_t index) const {
  static const Json kNull;
  if (type_ != Type::Array || index >= array_.size()) return kNull;
  return array_[index];
}

Json& Json::operator[](std::string_view key) {
  ensure_object();
  for (auto& kv : object_) {
    if (kv.first == key) return kv.second;
  }
  object_.emplace_back(std::string(key), Json());
  return object_.back().second;
}

Json& Json::operator[](size_t index) {
  if (type_ != Type::Array) {
    clear();
    type_ = Type::Array;
  }
  if (index >= array_.size()) array_.resize(index + 1);
  return array_[index];
}

std::string Json::get_str(std::string_view key, std::string fallback) const {
  const Json& v = (*this)[key];
  return v.is_string() ? v.string_ : fallback;
}

long long Json::get_int(std::string_view key, long long fallback) const {
  return (*this)[key].as_int(fallback);
}

double Json::get_num(std::string_view key, double fallback) const {
  return (*this)[key].as_number(fallback);
}

bool Json::get_bool(std::string_view key, bool fallback) const {
  return (*this)[key].as_bool(fallback);
}

size_t Json::size() const {
  if (type_ == Type::Array) return array_.size();
  if (type_ == Type::Object) return object_.size();
  return 0;
}

void Json::push_back(Json value) {
  if (type_ != Type::Array) {
    clear();
    type_ = Type::Array;
  }
  array_.push_back(std::move(value));
}

void Json::set(std::string key, Json value) {
  ensure_object();
  for (auto& kv : object_) {
    if (kv.first == key) {
      kv.second = std::move(value);
      return;
    }
  }
  object_.emplace_back(std::move(key), std::move(value));
}

void Json::erase(std::string_view key) {
  if (type_ != Type::Object) return;
  for (auto it = object_.begin(); it != object_.end(); ++it) {
    if (it->first == key) {
      object_.erase(it);
      return;
    }
  }
}

void Json::clear() {
  type_ = Type::Null;
  bool_ = false;
  number_ = 0.0;
  string_.clear();
  array_.clear();
  object_.clear();
}

void Json::ensure_object() {
  if (type_ != Type::Object) {
    clear();
    type_ = Type::Object;
  }
}

void Json::dump_string(std::string& out, std::string_view s) const {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

void Json::dump_to(std::string& out, int indent, int level) const {
  const bool pretty = indent >= 0;
  switch (type_) {
    case Type::Null:
      out += "null";
      break;
    case Type::Bool:
      out += bool_ ? "true" : "false";
      break;
    case Type::Number: {
      if (!std::isfinite(number_)) {
        out += "null";
      } else if (number_ == std::floor(number_) && std::fabs(number_) < 1e15) {
        out += std::to_string(static_cast<long long>(number_));
      } else {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.17g", number_);
        out += buf;
      }
      break;
    }
    case Type::String:
      dump_string(out, string_);
      break;
    case Type::Array: {
      if (array_.empty()) {
        out += "[]";
        break;
      }
      out += '[';
      for (size_t i = 0; i < array_.size(); ++i) {
        if (i) out += ',';
        if (pretty) {
          out += '\n';
          out.append(static_cast<size_t>(indent * (level + 1)), ' ');
        }
        array_[i].dump_to(out, indent, level + 1);
      }
      if (pretty) {
        out += '\n';
        out.append(static_cast<size_t>(indent * level), ' ');
      }
      out += ']';
      break;
    }
    case Type::Object: {
      if (object_.empty()) {
        out += "{}";
        break;
      }
      out += '{';
      for (size_t i = 0; i < object_.size(); ++i) {
        if (i) out += ',';
        if (pretty) {
          out += '\n';
          out.append(static_cast<size_t>(indent * (level + 1)), ' ');
        }
        dump_string(out, object_[i].first);
        out += ':';
        if (pretty) out += ' ';
        object_[i].second.dump_to(out, indent, level + 1);
      }
      if (pretty) {
        out += '\n';
        out.append(static_cast<size_t>(indent * level), ' ');
      }
      out += '}';
      break;
    }
  }
}

std::string Json::dump(int indent) const {
  std::string out;
  out.reserve(256);
  dump_to(out, indent, 0);
  return out;
}

bool Json::operator==(const Json& other) const {
  if (type_ != other.type_) return false;
  switch (type_) {
    case Type::Null: return true;
    case Type::Bool: return bool_ == other.bool_;
    case Type::Number: return number_ == other.number_;
    case Type::String: return string_ == other.string_;
    case Type::Array: return array_ == other.array_;
    case Type::Object: return object_ == other.object_;
  }
  return false;
}

}  // namespace mct
