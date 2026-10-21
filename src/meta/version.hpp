// Minecraft 版本元数据：规则、库、参数、版本继承
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "util/json.hpp"

namespace mct::meta {

struct OsInfo {
  std::string name;     // windows / osx / linux
  std::string version;  // 10.0
  std::string arch;     // x86_64 / x86
};
OsInfo current_os();

struct FeatureSet {
  bool demo_user = false;
  bool custom_resolution = false;
};

bool rules_allow(const Json& rules, const OsInfo& os, const FeatureSet& features = FeatureSet{});

struct DownloadInfo {
  std::string url;
  std::string path;
  std::string sha1;
  uint64_t size = 0;
  bool valid() const { return !url.empty(); }
};

struct Library {
  std::string name;
  std::string group;
  std::string artifact;
  std::string version;
  std::string classifier;
  std::string extension = "jar";
  std::string natives_key;
  bool is_native = false;
  DownloadInfo download;
  std::vector<std::string> extract_exclude;

  static bool parse(const Json& j, const OsInfo& os, Library& out);
  std::string relative_path() const;
  std::string fallback_url() const;
};

struct VersionMeta {
  std::string id;
  std::string type;
  std::string main_class;
  std::string assets_id;
  bool virtual_assets = false;
  bool map_to_resources = false;
  DownloadInfo client;
  DownloadInfo asset_index;
  DownloadInfo logging_file;
  std::string logging_argument;
  int java_major = 8;
  std::vector<Library> libraries;
  Json jvm_args;
  Json game_args;
  std::string legacy_game_args;
  Json raw;

  static bool parse(const Json& j, VersionMeta& out, std::string* error);
  std::vector<std::string> classpath_libraries() const;
};

// 把加载器 profile 合并进原版 json（子优先）
Json merge_version_json(const Json& base, const Json& addon);
bool load_version_json(const Paths& paths, const std::string& id, Json& out, std::string* error);
bool load_version_meta(const Paths& paths, const std::string& id, VersionMeta& out,
                       std::string* error);

std::vector<std::string> build_args(const Json& arg_array, const OsInfo& os,
                                    const std::map<std::string, std::string>& subs,
                                    const FeatureSet& features = FeatureSet{});

std::string substitute_vars(const std::string& text,
                            const std::map<std::string, std::string>& subs);

bool instance_exists(const Paths& paths, const std::string& id);
std::vector<std::string> list_instances(const Paths& paths);
Json load_instance_record(const Paths& paths, const std::string& id);

}  // namespace mct::meta
