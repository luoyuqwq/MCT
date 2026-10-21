// 配置与目录布局
#pragma once

#include <string>

#include "util/json.hpp"

namespace mct {

enum class GameSource { Official = 0, Bmclapi = 1, Custom = 2 };
enum class ModSource { Domestic = 0, CurseForge = 1, Modrinth = 2 };

struct Config {
  GameSource game_source = GameSource::Bmclapi;
  ModSource mod_source = ModSource::Domestic;
  std::string custom_mirror;  // 自定义站点根地址，例如 https://mc.example.com/
  std::string custom_mirror_name = "自定义站点";
  bool version_isolation = true;
  int download_threads = 16;
  std::string java_path;  // 空 = 自动探测
  std::string game_dir;   // 空 = <home>/game
  int max_memory_mb = 4096;
  int min_memory_mb = 512;
  std::string extra_jvm_args;
  std::string curseforge_api_key;
  std::string last_version;
  bool show_snapshots = false;
  std::string proxy;
  int window_width = 854;
  int window_height = 480;
  bool fullscreen = false;
  bool download_assets = true;

  static Config load(const std::string& path, bool* exists = nullptr);
  bool save(const std::string& path) const;
  Json to_json() const;
  void from_json(const Json& j);

  std::string game_source_label() const;
  std::string mod_source_label() const;
  bool has_custom_mirror() const { return !custom_mirror.empty(); }
};

// 目录布局：
//   <home>/config.json          配置
//   <home>/accounts.json        账号
//   <home>/cache/               版本清单缓存
//   <home>/logs/mct.log         日志
//   <home>/game/                游戏根目录（.minecraft 等价物）
//   <home>/game/versions/<id>/  版本/实例目录
struct Paths {
  std::string home;
  std::string game_root;  // 空 = <home>/game

  static Paths resolve(const std::string& override_home);

  std::string config_file() const;
  std::string accounts_file() const;
  std::string cache_dir() const;
  std::string logs_dir() const;
  std::string log_file() const;
  std::string game_dir() const;
  std::string versions_dir() const;
  std::string libraries_dir() const;
  std::string assets_dir() const;
  std::string java_dir() const;
  std::string instance_dir(const std::string& id) const;
  std::string instance_json(const std::string& id) const;
  std::string instance_jar(const std::string& id) const;
  std::string instance_natives(const std::string& id) const;
  std::string instance_game_dir(const std::string& id, bool isolated) const;
  std::string instance_mods_dir(const std::string& id, bool isolated) const;
  std::string shared_mods_dir() const;
};

}  // namespace mct
