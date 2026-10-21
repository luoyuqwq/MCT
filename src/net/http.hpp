// HTTP 客户端：Windows 使用 WinHTTP（系统 TLS），其他平台回退到 curl 命令行。
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mct::net {

struct HttpHeader {
  std::string name;
  std::string value;
};

struct HttpResult {
  long status = 0;
  std::string body;
  std::map<std::string, std::string> headers;  // 键为小写
  std::string error;

  bool ok() const { return error.empty() && status >= 200 && status < 300; }
  std::string header(const std::string& name) const;
};

// 返回 false 表示取消下载
using ProgressFn = std::function<bool(uint64_t received, uint64_t total)>;

class HttpClient {
 public:
  HttpClient();
  ~HttpClient();
  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;

  void set_user_agent(const std::string& ua);
  void set_timeout_ms(int connect_ms, int receive_ms);
  void set_proxy(const std::string& proxy);

  HttpResult get(const std::string& url);
  HttpResult get_to_file(const std::string& url, const std::string& path,
                         const ProgressFn& progress = nullptr);
  HttpResult post(const std::string& url, const std::string& body,
                  const std::string& content_type = "application/json",
                  const std::vector<HttpHeader>& headers = {});
  HttpResult put(const std::string& url, const std::string& body, const std::string& content_type,
                 const std::vector<HttpHeader>& headers = {});
  HttpResult del(const std::string& url, const std::vector<HttpHeader>& headers = {});

  HttpResult request(const std::string& method, const std::string& url, const std::string& body,
                     const std::vector<HttpHeader>& headers, const ProgressFn& progress,
                     const std::string& save_path);

  // 便捷：把相对 URL 拼接为绝对 URL
  static std::string join_url(const std::string& base, const std::string& path);

 private:
  std::string user_agent_;
  std::string proxy_;
  int connect_timeout_ms_ = 15000;
  int receive_timeout_ms_ = 60000;
  void* session_ = nullptr;
};

// 对 429 / 5xx 自动退避重试（读取 Retry-After）
HttpResult get_with_retry(HttpClient& http, const std::string& url, int attempts = 3);

}  // namespace mct::net
