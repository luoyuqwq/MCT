#include "core/config.hpp"

#include <cstdlib>

#include "util/fs.hpp"
#include "util/log.hpp"
#include "util/str.hpp"

namespace mct {

namespace {
GameSource parse_game_source(const std::string& s) {
  if (s == "official") return GameSource::Official;
  if (s == "custom") return GameSource::Custom;
  return GameSource::Bmclapi;
}
ModSource parse_mod_source(const std::string& s) {
  if (s == "curseforge") return ModSource::CurseForge;
  if (s == "modrinth") return ModSource::Modrinth;
  return ModSource::Domestic;
}
std::string trim_slash(std::string s) {
  while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
  return s;
}
}  // namespace

std::string Config::game_source_label() const {
  switch (game_source) {
    case GameSource::Official: return "官方 Mojang";
    case GameSource::Bmclapi: return "国内镜像 BMCLAPI";
    case GameSource::Custom:
      return custom_mirror_name.empty() ? "自定义站点" : custom_mirror_name + "（自定义）";
  }
  return "未知";
}

std::string Config::mod_source_label() const {
  switch (mod_source) {
    case ModSource::Domestic: return "国内镜像";
    case ModSource::CurseForge: return "CurseForge";
    case ModSource::Modrinth: return "Modrinth";
  }
  return "未知";
}

Json Config::to_json() const {
  Json j = Json::object();
  j["game_source"] = game_source == GameSource::Official
                         ? "official"
                         : (game_source == GameSource::Custom ? "custom" : "bmclapi");
  j["custom_mirror"] = custom_mirror;
  j["custom_mirror_name"] = custom_mirror_name;
  j["mod_source"] = mod_source == ModSource::CurseForge
                        ? "curseforge"
                        : (mod_source == ModSource::Modrinth ? "modrinth" : "domestic");
  j["version_isolation"] = version_isolation;
  j["download_threads"] = download_threads;
  j["java_path"] = java_path;
  j["game_dir"] = game_dir;
  j["max_memory_mb"] = max_memory_mb;
  j["min_memory_mb"] = min_memory_mb;
  j["extra_jvm_args"] = extra_jvm_args;
  j["curseforge_api_key"] = curseforge_api_key;
  j["last_version"] = last_version;
  j["show_snapshots"] = show_snapshots;
  j["proxy"] = proxy;
  j["window_width"] = window_width;
  j["window_height"] = window_height;
  j["fullscreen"] = fullscreen;
  j["download_assets"] = download_assets;
  return j;
}

void Config::from_json(const Json& j) {
  if (!j.is_object()) return;
  game_source = parse_game_source(j.get_str("game_source", "bmclapi"));
  custom_mirror = trim_slash(j.get_str("custom_mirror", custom_mirror));
  custom_mirror_name = j.get_str("custom_mirror_name", custom_mirror_name);
  if (custom_mirror_name.empty()) custom_mirror_name = "自定义站点";
  mod_source = parse_mod_source(j.get_str("mod_source", "domestic"));
  version_isolation = j.get_bool("version_isolation", version_isolation);
  download_threads = static_cast<int>(j.get_int("download_threads", download_threads));
  if (download_threads < 1) download_threads = 1;
  if (download_threads > 64) download_threads = 64;
  java_path = j.get_str("java_path", java_path);
  game_dir = str::trim(j.get_str("game_dir", game_dir));
  max_memory_mb = static_cast<int>(j.get_int("max_memory_mb", max_memory_mb));
  min_memory_mb = static_cast<int>(j.get_int("min_memory_mb", min_memory_mb));
  extra_jvm_args = j.get_str("extra_jvm_args", extra_jvm_args);
  curseforge_api_key = j.get_str("curseforge_api_key", curseforge_api_key);
  last_version = j.get_str("last_version", last_version);
  show_snapshots = j.get_bool("show_snapshots", show_snapshots);
  proxy = j.get_str("proxy", proxy);
  window_width = static_cast<int>(j.get_int("window_width", window_width));
  window_height = static_cast<int>(j.get_int("window_height", window_height));
  fullscreen = j.get_bool("fullscreen", fullscreen);
  download_assets = j.get_bool("download_assets", download_assets);
}

Config Config::load(const std::string& path, bool* exists_flag) {
  Config config;
  std::string text;
  if (!fs::read_file(path, text)) {
    if (exists_flag) *exists_flag = false;
    return config;
  }
  if (exists_flag) *exists_flag = true;
  std::string error;
  Json j = Json::parse(text, &error);
  if (!error.empty()) {
    log::warn("配置文件解析失败，使用默认值: " + error);
    return config;
  }
  config.from_json(j);
  return config;
}

bool Config::save(const std::string& path) const {
  return fs::write_file_atomic(path, to_json().dump(2) + "\n");
}

Paths Paths::resolve(const std::string& override_home) {
  Paths paths;
  std::string home;
  if (!override_home.empty()) {
    home = override_home;
  } else {
    home = fs::get_env("MCT_HOME");
    if (home.empty()) {
#ifdef _WIN32
      home = fs::join(fs::appdata_dir(), "MCT");
#else
      home = fs::join(fs::home_dir(), ".mct");
#endif
    }
  }
  paths.home = fs::normalize(fs::absolute(home));
  return paths;
}

std::string Paths::config_file() const { return fs::join(home, "config.json"); }
std::string Paths::accounts_file() const { return fs::join(home, "accounts.json"); }
std::string Paths::cache_dir() const { return fs::join(home, "cache"); }
std::string Paths::logs_dir() const { return fs::join(home, "logs"); }
std::string Paths::log_file() const { return fs::join(logs_dir(), "mct.log"); }
std::string Paths::game_dir() const {
  return game_root.empty() ? fs::join(home, "game") : game_root;
}
std::string Paths::versions_dir() const { return fs::join(game_dir(), "versions"); }
std::string Paths::libraries_dir() const { return fs::join(game_dir(), "libraries"); }
std::string Paths::assets_dir() const { return fs::join(game_dir(), "assets"); }
std::string Paths::java_dir() const { return fs::join(home, "java"); }
std::string Paths::instance_dir(const std::string& id) const {
  return fs::join(versions_dir(), id);
}
std::string Paths::instance_json(const std::string& id) const {
  return fs::join(instance_dir(id), id + ".json");
}
std::string Paths::instance_jar(const std::string& id) const {
  return fs::join(instance_dir(id), id + ".jar");
}
std::string Paths::instance_natives(const std::string& id) const {
  return fs::join(instance_dir(id), "natives");
}
std::string Paths::instance_game_dir(const std::string& id, bool isolated) const {
  return isolated ? instance_dir(id) : game_dir();
}
std::string Paths::instance_mods_dir(const std::string& id, bool isolated) const {
  return fs::join(instance_game_dir(id, isolated), "mods");
}
std::string Paths::shared_mods_dir() const { return fs::join(game_dir(), "mods"); }

}  // namespace mct
