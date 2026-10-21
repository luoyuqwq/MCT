#include "auth/microsoft.hpp"

#include <algorithm>
#include <chrono>

#include "util/hash.hpp"
#include "util/json.hpp"
#include "util/log.hpp"
#include "util/str.hpp"

namespace mct::auth {

namespace {

const char* kLiveToken = "https://login.live.com/oauth20_token.srf";
const char* kLiveDeviceCode = "https://login.live.com/oauth20_connect.srf";

const char* kXboxAuthUrl = "https://user.auth.xboxlive.com/user/authenticate";
const char* kXstsUrl = "https://xsts.auth.xboxlive.com/xsts/authorize";
const char* kMinecraftLoginUrl =
    "https://api.minecraftservices.com/authentication/login_with_xbox";
const char* kProfileUrl = "https://api.minecraftservices.com/minecraft/profile";
const char* kEntitlementsUrl = "https://api.minecraftservices.com/entitlements/mcstore";

std::string form_body(const std::vector<std::pair<std::string, std::string>>& fields) {
  std::vector<std::string> parts;
  parts.reserve(fields.size());
  for (const auto& kv : fields) parts.push_back(kv.first + "=" + str::percent_encode(kv.second));
  return str::join(parts, "&");
}

int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string describe_error(const net::HttpResult& result) {
  std::string message = result.error.empty() ? str::fmt("HTTP %ld", result.status)
                                             : result.error + str::fmt(" (HTTP %ld)", result.status);
  if (!result.body.empty()) {
    std::string err;
    Json j = Json::parse(result.body, &err);
    if (err.empty() && j.is_object()) {
      if (j.has("error_description")) message += " " + j.get_str("error_description");
      if (j.has("error")) message += " (" + j.get_str("error") + ")";
      if (j.has("XErr")) message += " XErr=" + std::to_string(j.get_int("XErr"));
      if (j.has("Message")) message += " " + j.get_str("Message");
    } else {
      message += " " + str::ellipsize(result.body, 200);
    }
  }
  return message;
}

}  // namespace

const char* DevicePoll::state_name() const {
  switch (state) {
    case State::Pending: return "pending";
    case State::SlowDown: return "slow_down";
    case State::Authorized: return "authorized";
    case State::Declined: return "declined";
    case State::Expired: return "expired";
    case State::Error: return "error";
  }
  return "error";
}

std::string MicrosoftAuth::builtin_client_id() { return "00000000402b5328"; }

MicrosoftAuth::MicrosoftAuth(net::HttpClient& http)
    : http_(http), client_id_(builtin_client_id()) {}

std::string MicrosoftAuth::token_url() const { return kLiveToken; }

std::string MicrosoftAuth::device_code_url() const { return kLiveDeviceCode; }

bool MicrosoftAuth::request_device_code(DeviceCode& out, std::string* error) {
  const std::string body = "client_id=" + str::percent_encode(client_id_) +
                           "&scope=" + str::percent_encode(scope()) +
                           "&response_type=device_code";
  net::HttpResult result = http_.post(device_code_url(), body,
                                      "application/x-www-form-urlencoded");
  if (!result.ok()) {
    if (error) *error = "获取设备码失败: " + describe_error(result);
    return false;
  }
  std::string err;
  Json j = Json::parse(result.body, &err);
  if (!err.empty()) {
    if (error) *error = "设备码响应解析失败: " + err;
    return false;
  }
  out.device_code = j.get_str("device_code");
  out.user_code = j.get_str("user_code");
  out.verification_uri = j.get_str("verification_uri");
  if (out.verification_uri.empty()) out.verification_uri = "https://www.microsoft.com/link";
  out.verification_uri_complete = j.get_str("verification_uri_complete");
  out.message = j.get_str("message");
  out.interval = static_cast<int>(j.get_int("interval", 5));
  out.expires_in = static_cast<int>(j.get_int("expires_in", 900));
  if (out.device_code.empty()) {
    if (error) *error = "设备码响应缺少 device_code";
    return false;
  }
  log::info(str::fmt("设备码已获取：user_code=%s interval=%d expires=%ds", out.user_code.c_str(),
                     out.interval, out.expires_in));
  return true;
}

DevicePoll MicrosoftAuth::poll_device_code(const DeviceCode& code) {
  DevicePoll out;
  const std::string body =
      form_body({{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
                 {"client_id", client_id_},
                 {"device_code", code.device_code}});
  net::HttpResult result = http_.post(token_url(), body, "application/x-www-form-urlencoded",
                                      {{"Accept", "application/json"}});
  out.status = result.status;
  std::string parse_error;
  Json j = Json::parse(result.body, &parse_error);
  if (!parse_error.empty()) {
    out.state = DevicePoll::State::Error;
    out.message = "轮询响应解析失败（HTTP " + std::to_string(result.status) + "）";
    log::warn("设备码轮询失败: " + out.message + " body=" + str::ellipsize(result.body, 200));
    return out;
  }
  if (result.ok() && j.has("access_token")) {
    out.state = DevicePoll::State::Authorized;
    out.access_token = j.get_str("access_token");
    out.refresh_token = j.get_str("refresh_token");
    out.expires_at = now_ms() + j.get_int("expires_in", 3600) * 1000;
    out.message = "授权成功";
    log::info("设备码轮询：授权成功");
    return out;
  }
  const std::string error_name = j.get_str("error");
  out.message = j.get_str("error_description", error_name);
  if (error_name == "authorization_pending")
    out.state = DevicePoll::State::Pending;
  else if (error_name == "slow_down")
    out.state = DevicePoll::State::SlowDown;
  else if (error_name == "authorization_declined" || error_name == "access_denied")
    out.state = DevicePoll::State::Declined;
  else if (error_name == "expired_token")
    out.state = DevicePoll::State::Expired;
  else
    out.state = DevicePoll::State::Error;
  log::info(str::fmt("设备码轮询：HTTP %ld error=%s msg=%s", result.status, error_name.c_str(),
                     str::ellipsize(out.message, 160).c_str()));
  return out;
}

bool MicrosoftAuth::wait_for_token(const DeviceCode& code, std::string& ms_access,
                                   std::string& refresh_token, int64_t& expires_at,
                                   const WaitFn& wait, std::string* error,
                                   const DevicePollFn& on_poll) {
  int interval = std::max(2, code.interval);
  int elapsed = 0;
  while (elapsed < code.expires_in) {
    DevicePoll poll = poll_device_code(code);
    if (on_poll) on_poll(poll);
    switch (poll.state) {
      case DevicePoll::State::Authorized:
        ms_access = poll.access_token;
        refresh_token = poll.refresh_token;
        expires_at = poll.expires_at;
        return true;
      case DevicePoll::State::Pending:
        break;
      case DevicePoll::State::SlowDown:
        interval += 5;
        break;
      case DevicePoll::State::Declined:
        if (error) *error = "用户拒绝了授权";
        return false;
      case DevicePoll::State::Expired:
        if (error) *error = "设备码已过期，请重新登录";
        return false;
      case DevicePoll::State::Error:
        if (error) *error = "设备码轮询失败: " + poll.message;
        return false;
    }
    elapsed += interval;
    if (elapsed >= code.expires_in) break;
    if (wait && !wait(interval)) {
      if (error) *error = "登录已取消";
      return false;
    }
  }
  if (error) {
    *error = "登录超时：设备码已失效。请确认在 microsoft.com/link 输入的是屏幕上显示的代码，" +
             std::string("并且使用的是拥有 Minecraft 的微软账号。");
  }
  return false;
}

bool MicrosoftAuth::token_request(
    const std::vector<std::pair<std::string, std::string>>& fields, std::string& ms_access,
    std::string& refresh_token, int64_t& expires_at, std::string* error) {
  net::HttpResult result = http_.post(token_url(), form_body(fields),
                                      "application/x-www-form-urlencoded",
                                      {{"Accept", "application/json"}});
  if (!result.ok()) {
    if (error) *error = "获取令牌失败: " + describe_error(result);
    return false;
  }
  std::string err;
  Json j = Json::parse(result.body, &err);
  if (!err.empty() || !j.has("access_token")) {
    if (error) *error = "令牌响应解析失败";
    return false;
  }
  ms_access = j.get_str("access_token");
  refresh_token = j.get_str("refresh_token");
  expires_at = now_ms() + j.get_int("expires_in", 3600) * 1000;
  return true;
}

bool MicrosoftAuth::refresh(const std::string& refresh_token, std::string& ms_access,
                            std::string& new_refresh, int64_t& expires_at, std::string* error) {
  if (refresh_token.empty()) {
    if (error) *error = "缺少 refresh_token";
    return false;
  }
  const std::vector<std::pair<std::string, std::string>> fields = {
      {"grant_type", "refresh_token"},
      {"client_id", client_id_},
      {"refresh_token", refresh_token},
  };
  return token_request(fields, ms_access, new_refresh, expires_at, error);
}

bool MicrosoftAuth::xbox_user_token(const std::string& ms_access, std::string& token,
                                    std::string& uhs, std::string* error) {
  Json body = Json::object();
  Json props = Json::object();
  props["AuthMethod"] = "RPS";
  props["SiteName"] = "user.auth.xboxlive.com";
  props["RpsTicket"] = "d=" + ms_access;
  body["Properties"] = props;
  body["RelyingParty"] = "http://auth.xboxlive.com";
  body["TokenType"] = "JWT";
  net::HttpResult result = http_.post(kXboxAuthUrl, body.dump(), "application/json",
                                      {{"Accept", "application/json"},
                                       {"x-xbl-contract-version", "1"}});
  if (!result.ok()) {
    if (error) *error = "Xbox Live 认证失败: " + describe_error(result);
    return false;
  }
  std::string err;
  Json j = Json::parse(result.body, &err);
  if (!err.empty()) {
    if (error) *error = "Xbox Live 响应解析失败";
    return false;
  }
  token = j.get_str("Token");
  const Json& xui = j["DisplayClaims"]["xui"];
  if (xui.size() > 0) uhs = xui[0].get_str("uhs");
  if (token.empty() || uhs.empty()) {
    if (error) *error = "Xbox Live 响应缺少 Token/uhs";
    return false;
  }
  return true;
}

bool MicrosoftAuth::xsts_token(const std::string& xbl_token, std::string& token, std::string& uhs,
                               std::string* error) {
  Json body = Json::object();
  Json props = Json::object();
  props["SandboxId"] = "RETAIL";
  Json tokens = Json::array();
  tokens.push_back(xbl_token);
  props["UserTokens"] = tokens;
  body["Properties"] = props;
  body["RelyingParty"] = "rp://api.minecraftservices.com/";
  body["TokenType"] = "JWT";
  net::HttpResult result = http_.post(kXstsUrl, body.dump(), "application/json",
                                      {{"Accept", "application/json"},
                                       {"x-xbl-contract-version", "1"}});
  if (!result.ok()) {
    std::string extra = describe_error(result);
    if (str::contains(extra, "2148916233"))
      extra += "\n该微软账号还没有创建 Xbox 档案，请先在 xbox.com 登录一次并创建档案";
    else if (str::contains(extra, "2148916238"))
      extra += "\n这是儿童账号，需要加入家庭组后才能使用";
    else if (str::contains(extra, "2148916235"))
      extra += "\n该账号所在地区不支持 Xbox Live";
    if (error) *error = "XSTS 授权失败: " + extra;
    return false;
  }
  std::string err;
  Json j = Json::parse(result.body, &err);
  if (!err.empty()) {
    if (error) *error = "XSTS 响应解析失败";
    return false;
  }
  token = j.get_str("Token");
  const Json& xui = j["DisplayClaims"]["xui"];
  if (xui.size() > 0) uhs = xui[0].get_str("uhs");
  if (token.empty() || uhs.empty()) {
    if (error) *error = "XSTS 响应缺少 Token/uhs";
    return false;
  }
  return true;
}

bool MicrosoftAuth::login_minecraft(const std::string& ms_access, Account& out,
                                    std::string* error) {
  std::string xbl, uhs;
  if (!xbox_user_token(ms_access, xbl, uhs, error)) return false;
  std::string xsts, xsts_uhs;
  if (!xsts_token(xbl, xsts, xsts_uhs, error)) return false;
  if (!xsts_uhs.empty()) uhs = xsts_uhs;

  Json body = Json::object();
  body["identityToken"] = "XBL3.0 x=" + uhs + ";" + xsts;
  net::HttpResult result = http_.post(kMinecraftLoginUrl, body.dump(), "application/json",
                                      {{"Accept", "application/json"}});
  if (!result.ok()) {
    if (error) *error = "Minecraft 登录失败: " + describe_error(result);
    return false;
  }
  std::string err;
  Json j = Json::parse(result.body, &err);
  if (!err.empty()) {
    if (error) *error = "Minecraft 登录响应解析失败";
    return false;
  }
  out.access_token = j.get_str("access_token");
  if (out.access_token.empty()) {
    if (error) *error = "Minecraft 登录响应缺少 access_token";
    return false;
  }
  out.type = AccountType::Microsoft;
  out.expires_at = now_ms() + j.get_int("expires_in", 86400) * 1000;

  // 确认拥有游戏
  net::HttpResult ent = http_.get(kEntitlementsUrl);
  if (ent.ok()) {
    std::string e2;
    Json items = Json::parse(ent.body, &e2);
    if (e2.empty() && items["items"].size() == 0) {
      if (error) *error = "该微软账号没有 Minecraft: Java Edition 的所有权";
      return false;
    }
  }

  std::string uuid, name, skin_url, skin_model;
  if (!fetch_profile(out.access_token, uuid, name, skin_url, skin_model, error)) return false;
  out.uuid = uuid;
  out.name = name;
  out.skin_url = skin_url;
  out.skin_model = skin_model.empty() ? "classic" : skin_model;
  return true;
}

bool MicrosoftAuth::fetch_profile(const std::string& mc_token, std::string& uuid, std::string& name,
                                  std::string& skin_url, std::string& skin_model,
                                  std::string* error) {
  net::HttpResult result =
      http_.request("GET", kProfileUrl, "", {{"Authorization", "Bearer " + mc_token}}, nullptr, "");
  if (result.status == 404) {
    if (error) *error = "该账号尚未创建 Minecraft 档案（请先启动一次正版游戏）";
    return false;
  }
  if (!result.ok()) {
    if (error) *error = "获取玩家档案失败: " + describe_error(result);
    return false;
  }
  std::string err;
  Json j = Json::parse(result.body, &err);
  if (!err.empty()) {
    if (error) *error = "档案响应解析失败";
    return false;
  }
  uuid = hash::uuid_with_dashes(j.get_str("id"));
  name = j.get_str("name");
  const Json& skins = j["skins"];
  skin_url.clear();
  skin_model.clear();
  if (skins.size() > 0) {
    skin_url = skins[0].get_str("url");
    skin_model = skins[0].get_str("variant", "classic");
  }
  if (uuid.empty() || name.empty()) {
    if (error) *error = "档案响应缺少 id/name";
    return false;
  }
  return true;
}

}  // namespace mct::auth
