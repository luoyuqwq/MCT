// 界面与业务流程
#pragma once

#include <string>
#include <vector>

#include "auth/accounts.hpp"
#include "core/config.hpp"
#include "launch/java.hpp"
#include "meta/installer.hpp"
#include "net/http.hpp"
#include "ui/term.hpp"
#include "util/hash_cache.hpp"

namespace mct {

class App {
 public:
  App(Paths paths, Config config);
  int run();

 private:
  int main_screen();
  void accounts_screen();
  void offline_login();
  void microsoft_login();
  void skin_screen();
  void settings_screen();
  void launch_screen();
  void download_game_screen();
  void mods_screen(const std::string& preset_instance);
  void install_version(const std::string& mc, meta::LoaderKind loader,
                       const std::string& loader_version);
  bool choose_loader_version(const std::string& mc, meta::LoaderKind loader,
                             meta::Mirror& mirror, std::string& version);
  void launch_instance(const std::string& instance);
  bool ensure_account(auth::Account& account);
  bool ensure_java(const std::string& instance);
  bool install_java_runtime(int major);
  bool refresh_microsoft_account(const std::string& uuid);
  std::string pick_instance(const std::string& title, const std::string& subtitle);
  void change_game_dir();
  meta::Mirror make_mirror();
  void save_config();
  void save_accounts();
  void save_hash_cache();
  std::vector<std::string> installed_instances();

  ui::Terminal term_;
  Paths paths_;
  Config config_;
  auth::AccountStore accounts_;
  net::HttpClient http_;
  HashCache hash_cache_;
  std::string flash_;
  std::vector<launch::JavaInfo> java_cache_;
};

}  // namespace mct
