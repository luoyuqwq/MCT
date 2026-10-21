#include "auth/accounts.hpp"

#include <chrono>

#include "util/fs.hpp"
#include "util/hash.hpp"
#include "util/log.hpp"
#include "util/str.hpp"

namespace mct::auth {

bool Account::token_valid() const {
  if (offline()) return true;
  if (access_token.empty()) return false;
  if (expires_at <= 0) return true;
  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  return expires_at > now + 60 * 1000;
}

std::string Account::uuid_plain() const { return hash::uuid_without_dashes(uuid); }

Json Account::to_json() const {
  Json j = Json::object();
  j["type"] = offline() ? "offline" : "microsoft";
  j["name"] = name;
  j["uuid"] = uuid;
  j["access_token"] = access_token;
  j["refresh_token"] = refresh_token;
  j["expires_at"] = expires_at;
  j["skin_model"] = skin_model;
  j["skin_url"] = skin_url;
  return j;
}

Account Account::from_json(const Json& j) {
  Account a;
  a.type = j.get_str("type", "offline") == "microsoft" ? AccountType::Microsoft
                                                       : AccountType::Offline;
  a.name = j.get_str("name");
  a.uuid = j.get_str("uuid");
  a.access_token = j.get_str("access_token");
  a.refresh_token = j.get_str("refresh_token");
  a.expires_at = j.get_int("expires_at", 0);
  a.skin_model = j.get_str("skin_model", "classic");
  a.skin_url = j.get_str("skin_url");
  if (a.uuid.empty()) a.uuid = hash::offline_uuid(a.name);
  return a;
}

bool AccountStore::load(const std::string& path) {
  accounts_.clear();
  current_uuid_.clear();
  std::string text;
  if (!fs::read_file(path, text)) return false;
  std::string error;
  Json j = Json::parse(text, &error);
  if (!error.empty() || !j.is_object()) {
    log::warn("账号文件解析失败: " + error);
    return false;
  }
  current_uuid_ = j.get_str("current");
  const Json& list = j["accounts"];
  for (size_t i = 0; i < list.size(); ++i) {
    accounts_.push_back(Account::from_json(list[i]));
  }
  if (current() == nullptr && !accounts_.empty()) current_uuid_ = accounts_.front().uuid;
  return true;
}

bool AccountStore::save(const std::string& path) const {
  Json j = Json::object();
  j["current"] = current_uuid_;
  Json list = Json::array();
  for (const auto& a : accounts_) list.push_back(a.to_json());
  j["accounts"] = list;
  return fs::write_file_atomic(path, j.dump(2) + "\n");
}

Account* AccountStore::find(const std::string& uuid) {
  for (auto& a : accounts_) {
    if (a.uuid == uuid) return &a;
  }
  return nullptr;
}

const Account* AccountStore::find(const std::string& uuid) const {
  for (const auto& a : accounts_) {
    if (a.uuid == uuid) return &a;
  }
  return nullptr;
}

Account* AccountStore::current() { return current_uuid_.empty() ? nullptr : find(current_uuid_); }
const Account* AccountStore::current() const {
  return current_uuid_.empty() ? nullptr : find(current_uuid_);
}

void AccountStore::upsert(const Account& account) {
  for (auto& a : accounts_) {
    if (a.uuid == account.uuid) {
      a = account;
      return;
    }
  }
  accounts_.push_back(account);
}

void AccountStore::remove(const std::string& uuid) {
  for (auto it = accounts_.begin(); it != accounts_.end(); ++it) {
    if (it->uuid == uuid) {
      accounts_.erase(it);
      break;
    }
  }
  if (current_uuid_ == uuid) {
    current_uuid_ = accounts_.empty() ? std::string() : accounts_.front().uuid;
  }
}

}  // namespace mct::auth
