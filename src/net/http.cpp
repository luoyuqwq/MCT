#include "net/http.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "util/fs.hpp"
#include "util/hash.hpp"
#include "util/log.hpp"
#include "util/str.hpp"
#include "version.hpp"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <unistd.h>
#endif

namespace mct::net {

namespace {

struct UrlParts {
  std::string scheme;
  std::string host;
  std::string path;
  uint16_t port = 80;
  bool secure = false;
};

bool parse_url(const std::string& url, UrlParts& out) {
  size_t pos = url.find("://");
  if (pos == std::string::npos) return false;
  out.scheme = str::lower(url.substr(0, pos));
  out.secure = out.scheme == "https";
  out.port = out.secure ? 443 : 80;
  size_t start = pos + 3;
  size_t slash = url.find('/', start);
  std::string hostport =
      url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
  out.path = slash == std::string::npos ? "/" : url.substr(slash);
  if (hostport.empty()) return false;
  size_t colon = hostport.rfind(':');
  if (colon != std::string::npos && hostport.find(']') == std::string::npos) {
    out.host = hostport.substr(0, colon);
    int port = std::atoi(hostport.c_str() + colon + 1);
    if (port > 0 && port < 65536) out.port = static_cast<uint16_t>(port);
  } else {
    out.host = hostport;
  }
  return !out.host.empty();
}

#ifdef _WIN32

std::string win_error_text(DWORD code) {
  wchar_t* buffer = nullptr;
  DWORD n = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer),
      0, nullptr);
  std::string text;
  if (n && buffer) {
    text = str::narrow(std::wstring(buffer, n));
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();
  }
  LocalFree(buffer);
  if (text.empty()) text = "错误码 " + std::to_string(code);
  return text;
}

struct WinHandle {
  HINTERNET h = nullptr;
  WinHandle() = default;
  explicit WinHandle(HINTERNET x) : h(x) {}
  ~WinHandle() {
    if (h) WinHttpCloseHandle(h);
  }
  WinHandle(const WinHandle&) = delete;
  WinHandle& operator=(const WinHandle&) = delete;
  bool valid() const { return h != nullptr; }
  operator HINTERNET() const { return h; }
};

#endif

}  // namespace

std::string HttpResult::header(const std::string& name) const {
  auto it = headers.find(str::lower(name));
  return it == headers.end() ? std::string() : it->second;
}

std::string HttpClient::join_url(const std::string& base, const std::string& path) {
  if (path.empty()) return base;
  if (str::starts_with(path, "http://") || str::starts_with(path, "https://")) return path;
  if (base.empty()) return path;
  if (base.back() == '/' && path.front() == '/') return base + path.substr(1);
  if (base.back() != '/' && path.front() != '/') return base + "/" + path;
  return base + path;
}

#ifdef _WIN32

HttpClient::HttpClient() : user_agent_(std::string(MCT_NAME) + "/" + MCT_VERSION + " (Windows)") {
  std::wstring ua = str::widen(user_agent_);
  DWORD access = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
  access = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
#endif
  session_ = WinHttpOpen(ua.c_str(), access, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (session_) {
    WinHttpSetTimeouts(static_cast<HINTERNET>(session_), 15000, connect_timeout_ms_,
                       receive_timeout_ms_, receive_timeout_ms_);
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(static_cast<HINTERNET>(session_), WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                     sizeof(policy));
  }
}

HttpClient::~HttpClient() {
  if (session_) WinHttpCloseHandle(static_cast<HINTERNET>(session_));
}

void HttpClient::set_user_agent(const std::string& ua) { user_agent_ = ua; }
void HttpClient::set_timeout_ms(int connect_ms, int receive_ms) {
  connect_timeout_ms_ = connect_ms;
  receive_timeout_ms_ = receive_ms;
  if (session_) {
    WinHttpSetTimeouts(static_cast<HINTERNET>(session_), connect_ms, connect_ms, receive_ms,
                       receive_ms);
  }
}
void HttpClient::set_proxy(const std::string& proxy) { proxy_ = proxy; }

HttpResult HttpClient::request(const std::string& method, const std::string& url,
                               const std::string& body, const std::vector<HttpHeader>& headers,
                               const ProgressFn& progress, const std::string& save_path) {
  HttpResult result;
  UrlParts parts;
  if (!parse_url(url, parts)) {
    result.error = "URL 格式错误: " + url;
    return result;
  }
  if (!session_) {
    result.error = "WinHTTP 初始化失败";
    return result;
  }

  WinHandle conn(WinHttpConnect(static_cast<HINTERNET>(session_), str::widen(parts.host).c_str(),
                                parts.port, 0));
  if (!conn.valid()) {
    result.error = "无法连接 " + parts.host + ": " + win_error_text(GetLastError());
    return result;
  }

  std::wstring wmethod = str::widen(method);
  std::wstring wpath = str::widen(parts.path);
  WinHandle req(WinHttpOpenRequest(conn, wmethod.c_str(), wpath.c_str(), nullptr,
                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   parts.secure ? WINHTTP_FLAG_SECURE : 0));
  if (!req.valid()) {
    result.error = "创建请求失败: " + win_error_text(GetLastError());
    return result;
  }

  WinHttpSetTimeouts(req, connect_timeout_ms_, connect_timeout_ms_, receive_timeout_ms_,
                     receive_timeout_ms_);

  std::wstring header_block;
  for (const auto& h : headers) header_block += str::widen(h.name + ": " + h.value) + L"\r\n";
  if (!header_block.empty()) {
    WinHttpAddRequestHeaders(req, header_block.c_str(), static_cast<DWORD>(-1),
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  }

  LPVOID body_ptr = body.empty() ? WINHTTP_NO_REQUEST_DATA
                                 : const_cast<char*>(body.data());
  DWORD body_len = static_cast<DWORD>(body.size());
  if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body_ptr, body_len, body_len, 0)) {
    result.error = "发送请求失败: " + win_error_text(GetLastError()) + " (" + url + ")";
    return result;
  }
  if (!WinHttpReceiveResponse(req, nullptr)) {
    result.error = "接收响应失败: " + win_error_text(GetLastError()) + " (" + url + ")";
    return result;
  }

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                          WINHTTP_NO_HEADER_INDEX)) {
    result.status = static_cast<long>(status);
  }

  // 响应头
  DWORD raw_size = 0;
  WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                      &raw_size, WINHTTP_NO_HEADER_INDEX);
  if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && raw_size > 0) {
    std::wstring raw(raw_size / sizeof(wchar_t) + 1, L'\0');
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                            &raw[0], &raw_size, WINHTTP_NO_HEADER_INDEX)) {
      std::string text = str::narrow(raw);
      for (const auto& line : str::split_str(text, "\r\n")) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = str::lower(str::trim(line.substr(0, colon)));
        std::string value = str::trim(line.substr(colon + 1));
        if (!key.empty()) result.headers[key] = value;
      }
    }
  }

  uint64_t total = 0;
  {
    std::string cl = result.header("content-length");
    if (!cl.empty()) total = std::strtoull(cl.c_str(), nullptr, 10);
  }

  const bool want_file = !save_path.empty();
  const bool success_status = result.status >= 200 && result.status < 300;
  HANDLE file = INVALID_HANDLE_VALUE;
  if (want_file && success_status) {
    std::string dir = fs::parent(save_path);
    if (!dir.empty()) fs::ensure_dir(dir);
    file = CreateFileW(str::widen(save_path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      result.error = "无法写入文件: " + save_path;
      return result;
    }
  }

  std::vector<char> buffer(64 * 1024);
  uint64_t received = 0;
  bool cancelled = false;
  while (true) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(req, &available)) {
      result.error = "读取响应失败: " + win_error_text(GetLastError());
      break;
    }
    if (available == 0) break;
    DWORD want = static_cast<DWORD>(std::min<size_t>(available, buffer.size()));
    DWORD got = 0;
    if (!WinHttpReadData(req, buffer.data(), want, &got)) {
      result.error = "读取响应数据失败: " + win_error_text(GetLastError());
      break;
    }
    if (got == 0) break;
    received += got;
    if (file != INVALID_HANDLE_VALUE) {
      DWORD written = 0;
      if (!WriteFile(file, buffer.data(), got, &written, nullptr) || written != got) {
        result.error = "写入文件失败: " + save_path;
        break;
      }
    } else {
      result.body.append(buffer.data(), got);
    }
    if (progress && !progress(received, total)) {
      cancelled = true;
      result.error = "已取消";
      break;
    }
  }

  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  if (want_file && (!result.error.empty() || !success_status)) {
    DeleteFileW(str::widen(save_path).c_str());
  }
  if (!cancelled && result.error.empty() && !success_status && result.body.empty() && want_file) {
    result.error = str::fmt("HTTP %ld", result.status);
  }
  return result;
}

#else  // POSIX：调用 curl 命令行

HttpClient::HttpClient() : user_agent_(std::string(MCT_NAME) + "/" + MCT_VERSION + " (POSIX)") {}
HttpClient::~HttpClient() = default;
void HttpClient::set_user_agent(const std::string& ua) { user_agent_ = ua; }
void HttpClient::set_timeout_ms(int connect_ms, int receive_ms) {
  connect_timeout_ms_ = connect_ms;
  receive_timeout_ms_ = receive_ms;
}
void HttpClient::set_proxy(const std::string& proxy) { proxy_ = proxy; }

namespace {
std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}
}  // namespace

HttpResult HttpClient::request(const std::string& method, const std::string& url,
                               const std::string& body, const std::vector<HttpHeader>& headers,
                               const ProgressFn& progress, const std::string& save_path) {
  (void)progress;
  HttpResult result;
  std::string body_file = fs::join(fs::temp_dir(), "mct_http_body_" + std::to_string(::getpid()) +
                                                          "_" + hash::random_hex(4));
  std::string out_file = save_path.empty() ? body_file : save_path;
  std::string status_file = body_file + ".status";
  std::string cmd = "curl -sS -L --connect-timeout " + std::to_string(connect_timeout_ms_ / 1000) +
                    " -m " + std::to_string(receive_timeout_ms_ / 1000) + " -A " +
                    shell_quote(user_agent_);
  if (method != "GET") cmd += " -X " + method;
  for (const auto& h : headers) cmd += " -H " + shell_quote(h.name + ": " + h.value);
  if (!body.empty()) {
    std::string body_path = body_file + ".req";
    fs::write_file(body_path, body);
    cmd += " --data-binary @" + shell_quote(body_path);
  }
  std::string dir = fs::parent(out_file);
  if (!dir.empty()) fs::ensure_dir(dir);
  cmd += " -o " + shell_quote(out_file) + " -w '%{http_code}' " + shell_quote(url) + " > " +
         shell_quote(status_file) + " 2>/dev/null";
  int rc = std::system(cmd.c_str());
  std::string status_text;
  fs::read_file(status_file, status_text);
  result.status = std::atol(status_text.c_str());
  if (save_path.empty()) fs::read_file(body_file, result.body);
  if (rc != 0 || result.status == 0) result.error = "curl 请求失败: " + url;
  fs::remove_file(status_file);
  fs::remove_file(body_file);
  if (!save_path.empty() && !result.ok()) fs::remove_file(save_path);
  return result;
}

#endif

HttpResult HttpClient::get(const std::string& url) {
  return request("GET", url, "", {{"Connection", "close"}}, nullptr, "");
}

HttpResult HttpClient::get_to_file(const std::string& url, const std::string& path,
                                   const ProgressFn& progress) {
  return request("GET", url, "", {}, progress, path);
}

HttpResult HttpClient::post(const std::string& url, const std::string& body,
                            const std::string& content_type,
                            const std::vector<HttpHeader>& headers) {
  std::vector<HttpHeader> all = headers;
  all.push_back({"Content-Type", content_type});
  return request("POST", url, body, all, nullptr, "");
}

HttpResult HttpClient::put(const std::string& url, const std::string& body,
                           const std::string& content_type,
                           const std::vector<HttpHeader>& headers) {
  std::vector<HttpHeader> all = headers;
  all.push_back({"Content-Type", content_type});
  return request("PUT", url, body, all, nullptr, "");
}

HttpResult HttpClient::del(const std::string& url, const std::vector<HttpHeader>& headers) {
  return request("DELETE", url, "", headers, nullptr, "");
}

namespace {
int parse_retry_after(const HttpResult& result, int fallback_seconds) {
  std::string value = result.header("retry-after");
  if (value.empty()) value = result.header("retry-after-short");
  if (!value.empty()) {
    try {
      int seconds = std::stoi(value);
      if (seconds > 0) return std::min(seconds, 120);
    } catch (const std::exception&) {
    }
  }
  return fallback_seconds;
}
}  // namespace

HttpResult get_with_retry(HttpClient& http, const std::string& url, int attempts) {
  HttpResult result;
  for (int attempt = 0; attempt < attempts; ++attempt) {
    result = http.get(url);
    if (result.ok()) return result;
    bool retryable = result.status == 0 || result.status == 408 || result.status == 425 ||
                     result.status == 429 || result.status >= 500;
    if (!retryable || attempt + 1 >= attempts) return result;
    int wait_seconds =
        result.status == 429 ? parse_retry_after(result, 5) : (1 << attempt);
    log::warn(str::fmt("请求失败（HTTP %ld），%d 秒后重试: %s", result.status, wait_seconds,
                       url.c_str()));
    std::this_thread::sleep_for(std::chrono::seconds(wait_seconds));
  }
  return result;
}

}  // namespace mct::net
