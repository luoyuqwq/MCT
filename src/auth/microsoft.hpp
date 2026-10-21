// 微软正版登录：仅保留设备码（Device Code）流程
// 使用公开的 Minecraft 客户端 ID（00000000402b5328，Live 端点），无需注册 Azure 应用
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "auth/accounts.hpp"
#include "net/http.hpp"

namespace mct::auth {

struct DeviceCode {
  std::string device_code;
  std::string user_code;
  std::string verification_uri;
  std::string verification_uri_complete;
  std::string message;
  int interval = 5;
  int expires_in = 900;
};

struct DevicePoll {
  enum class State { Pending, SlowDown, Authorized, Declined, Expired, Error };
  State state = State::Error;
  long status = 0;
  std::string message;
  std::string access_token;
  std::string refresh_token;
  int64_t expires_at = 0;
  const char* state_name() const;
};

// wait(秒) 返回 false 表示用户取消
using WaitFn = std::function<bool(int seconds)>;
using DevicePollFn = std::function<void(const DevicePoll& poll)>;

class MicrosoftAuth {
 public:
  explicit MicrosoftAuth(net::HttpClient& http);

  // 内置公开客户端 ID（官方/社区启动器通用）
  static std::string builtin_client_id();

  bool request_device_code(DeviceCode& out, std::string* error);
  DevicePoll poll_device_code(const DeviceCode& code);
  bool wait_for_token(const DeviceCode& code, std::string& ms_access, std::string& refresh_token,
                      int64_t& expires_at, const WaitFn& wait, std::string* error,
                      const DevicePollFn& on_poll = nullptr);

  bool refresh(const std::string& refresh_token, std::string& ms_access, std::string& new_refresh,
               int64_t& expires_at, std::string* error);

  // 微软 token -> Minecraft token + 档案
  bool login_minecraft(const std::string& ms_access, Account& out, std::string* error);

  bool fetch_profile(const std::string& mc_token, std::string& uuid, std::string& name,
                     std::string& skin_url, std::string& skin_model, std::string* error);

 private:
  bool xbox_user_token(const std::string& ms_access, std::string& token, std::string& uhs,
                       std::string* error);
  bool xsts_token(const std::string& xbl_token, std::string& token, std::string& uhs,
                  std::string* error);
  bool token_request(const std::vector<std::pair<std::string, std::string>>& fields,
                     std::string& ms_access, std::string& refresh_token, int64_t& expires_at,
                     std::string* error);
  std::string token_url() const;
  std::string device_code_url() const;
  std::string scope() const { return "XboxLive.signin offline_access"; }

  net::HttpClient& http_;
  std::string client_id_;
};

}  // namespace mct::auth
