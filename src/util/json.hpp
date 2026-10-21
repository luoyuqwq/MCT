// 轻量 JSON，支持解析/序列化/Minecraft 元数据常用访问方式。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mct {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Json();
  Json(std::nullptr_t);
  Json(bool v);
  Json(int v);
  Json(long long v);
  Json(double v);
  Json(const char* v);
  Json(std::string v);

  static Json object();
  static Json array();
  static Json parse(const std::string& text, std::string* error = nullptr);
  static bool parse_file(const std::string& path, Json& out, std::string* error = nullptr);

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  bool as_bool(bool fallback = false) const;
  double as_number(double fallback = 0.0) const;
  long long as_int(long long fallback = 0) const;
  std::string as_string(std::string fallback = std::string()) const;

  // 对象访问：不存在时返回只读空值
  bool has(std::string_view key) const;
  const Json& operator[](std::string_view key) const;
  const Json& operator[](size_t index) const;
  Json& operator[](std::string_view key);
  Json& operator[](size_t index);

  // 便捷读取
  std::string get_str(std::string_view key, std::string fallback = std::string()) const;
  long long get_int(std::string_view key, long long fallback = 0) const;
  double get_num(std::string_view key, double fallback = 0.0) const;
  bool get_bool(std::string_view key, bool fallback = false) const;
  const Json& get(std::string_view key) const { return (*this)[key]; }

  const std::vector<Json>& items() const { return array_; }
  std::vector<Json>& items() { return array_; }
  const std::vector<std::pair<std::string, Json>>& members() const { return object_; }
  std::vector<std::pair<std::string, Json>>& members() { return object_; }
  size_t size() const;
  bool empty() const { return size() == 0; }

  void push_back(Json value);
  void set(std::string key, Json value);
  void erase(std::string_view key);
  void clear();

  std::string dump(int indent = -1) const;
  void dump_to(std::string& out, int indent, int level) const;

  bool operator==(const Json& other) const;
  bool operator!=(const Json& other) const { return !(*this == other); }

 private:
  void ensure_object();
  void dump_string(std::string& out, std::string_view s) const;

  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<Json> array_;
  std::vector<std::pair<std::string, Json>> object_;
};

}  // namespace mct
