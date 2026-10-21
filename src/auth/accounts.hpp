// 账号存储：离线账号与微软账号
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "util/json.hpp"

namespace mct::auth {

enum class AccountType { Offline, Microsoft };

struct Account {
  AccountType type = AccountType::Offline;
  std::string name;
  std::string uuid;  // 带连字符
  std::string access_token;
  std::string refresh_token;
  int64_t expires_at = 0;  // Unix 毫秒；0 表示不过期（离线）
  std::string skin_model = "classic";
  std::string skin_url;

  bool offline() const { return type == AccountType::Offline; }
  std::string type_label() const { return offline() ? "离线" : "微软正版"; }
  bool token_valid() const;
  std::string uuid_plain() const;
  Json to_json() const;
  static Account from_json(const Json& j);
};

class AccountStore {
 public:
  bool load(const std::string& path);
  bool save(const std::string& path) const;

  std::vector<Account>& accounts() { return accounts_; }
  const std::vector<Account>& accounts() const { return accounts_; }
  Account* find(const std::string& uuid);
  const Account* find(const std::string& uuid) const;
  Account* current();
  const Account* current() const;
  void set_current(const std::string& uuid) { current_uuid_ = uuid; }
  std::string current_uuid() const { return current_uuid_; }
  void upsert(const Account& account);
  void remove(const std::string& uuid);
  bool empty() const { return accounts_.empty(); }

 private:
  std::vector<Account> accounts_;
  std::string current_uuid_;
};

}  // namespace mct::auth
