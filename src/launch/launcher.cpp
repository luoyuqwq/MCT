#include "launch/launcher.hpp"

#include <algorithm>

#include "launch/java.hpp"
#include "meta/version.hpp"
#include "util/fs.hpp"
#include "util/log.hpp"
#include "util/process.hpp"
#include "util/str.hpp"
#include "version.hpp"

namespace mct::launch {

namespace {

std::string classpath_separator() {
#ifdef _WIN32
  return ";";
#else
  return ":";
#endif
}

std::string console_java(const std::string& path) {
  std::string lower = str::lower(path);
  if (str::ends_with(lower, "javaw.exe")) {
    std::string candidate = path.substr(0, path.size() - 9) + "java.exe";
    if (fs::is_file(candidate)) return candidate;
  }
  return path;
}

}  // namespace

GameLauncher::GameLauncher(const Paths& paths, const Config& config)
    : paths_(paths), config_(config) {}

std::string GameLauncher::resolve_jar(const std::string& instance) const {
  std::string id = instance;
  for (int depth = 0; depth < 6; ++depth) {
    if (fs::is_file(paths_.instance_jar(id))) return paths_.instance_jar(id);
    Json raw;
    std::string error;
    if (!Json::parse_file(paths_.instance_json(id), raw, &error)) break;
    std::string parent = raw.get_str("inheritsFrom");
    if (parent.empty()) break;
    id = parent;
  }
  return paths_.instance_jar(id);
}

bool GameLauncher::build_command(const std::string& instance, const auth::Account& account,
                                 std::vector<std::string>& argv, std::string* error,
                                 const std::function<void(const std::string&)>& status) {
  meta::VersionMeta meta;
  if (!meta::load_version_meta(paths_, instance, meta, error)) return false;

  if (status) status("查找 Java " + std::to_string(meta.java_major) + "+…");
  JavaInfo java;
  if (!find_java(paths_, config_, meta.java_major, java, error)) return false;
  std::string java_path = console_java(java.path);

  const std::string game_dir =
      paths_.instance_game_dir(instance, config_.version_isolation);
  fs::ensure_dir(game_dir);
  const std::string natives = paths_.instance_natives(instance);
  if (!fs::is_dir(natives)) {
    if (error) *error = "缺少 natives 目录，实例可能没有安装完整，请重新安装该版本";
    return false;
  }
  std::string jar = resolve_jar(instance);
  if (!fs::is_file(jar)) {
    if (error) *error = "缺少客户端 " + fs::filename(jar) + "，请重新安装该版本";
    return false;
  }

  std::vector<std::string> classpath_entries;
  for (const auto& lib : meta.libraries) {
    if (lib.is_native) continue;
    classpath_entries.push_back(fs::join(paths_.libraries_dir(), lib.download.path));
  }
  classpath_entries.push_back(jar);
  const std::string classpath = str::join(classpath_entries, classpath_separator());

  const std::string token = account.access_token.empty() ? "0" : account.access_token;
  const std::string uuid = account.uuid_plain();
  std::string log_config;
  if (!meta.logging_file.path.empty())
    log_config = fs::join(paths_.cache_dir(), "log_configs", meta.logging_file.path);

  std::map<std::string, std::string> subs;
  std::string assets_root = paths_.assets_dir();
  // 旧版本（1.7.10 及更早）的资源索引带 virtual 标记，需要把 resources 目录指到虚拟资源树
  {
    Json asset_index;
    if (Json::parse_file(fs::join(assets_root, "indexes", meta.assets_id + ".json"), asset_index,
                         nullptr) &&
        asset_index.get_bool("virtual", false)) {
      assets_root = fs::join(assets_root, "virtual", meta.assets_id);
    }
  }
  subs["auth_player_name"] = account.name;
  subs["version_name"] = instance;
  subs["game_directory"] = game_dir;
  subs["assets_root"] = assets_root;
  subs["game_assets"] = assets_root;
  subs["assets_index_name"] = meta.assets_id;
  subs["auth_uuid"] = uuid;
  subs["auth_access_token"] = token;
  subs["auth_session"] = "token:" + token + ":" + uuid;
  subs["user_type"] = account.offline() ? "legacy" : "msa";
  subs["version_type"] = meta.type;
  subs["natives_directory"] = natives;
  subs["launcher_name"] = MCT_NAME;
  subs["launcher_version"] = MCT_VERSION;
  subs["classpath"] = classpath;
  subs["classpath_separator"] = classpath_separator();
  subs["library_directory"] = paths_.libraries_dir();
  subs["auth_xuid"] = "";
  subs["clientid"] = "";
  subs["user_properties"] = "{}";
  subs["resolution_width"] = std::to_string(config_.window_width);
  subs["resolution_height"] = std::to_string(config_.window_height);
  subs["path"] = log_config;

  std::vector<std::string> jvm;
  jvm.push_back("-Xmx" + std::to_string(config_.max_memory_mb) + "m");
  if (config_.min_memory_mb > 0)
    jvm.push_back("-Xms" + std::to_string(config_.min_memory_mb) + "m");
  for (const auto& extra : str::split_ws(config_.extra_jvm_args)) jvm.push_back(extra);
  if (!meta.logging_argument.empty() && !log_config.empty()) {
    jvm.push_back(meta::substitute_vars(meta.logging_argument, subs));
  }

  meta::OsInfo os = meta::current_os();
  std::vector<std::string> jvm_from_json = meta::build_args(meta.jvm_args, os, subs);
  bool has_classpath = false;
  bool has_library_path = false;
  for (const auto& arg : jvm_from_json) {
    if (arg == "-cp" || arg == "-classpath") has_classpath = true;
    if (str::starts_with(arg, "-Djava.library.path=")) has_library_path = true;
  }
  if (jvm_from_json.empty()) {
    jvm.push_back("-Djava.library.path=" + natives);
    jvm.push_back("-cp");
    jvm.push_back(classpath);
  } else {
    jvm.insert(jvm.end(), jvm_from_json.begin(), jvm_from_json.end());
    if (!has_library_path) jvm.push_back("-Djava.library.path=" + natives);
    if (!has_classpath) {
      jvm.push_back("-cp");
      jvm.push_back(classpath);
    }
  }

  std::vector<std::string> game = meta::build_args(meta.game_args, os, subs);
  if (game.empty() && !meta.legacy_game_args.empty()) {
    for (const auto& token_text : str::split_ws(meta.legacy_game_args)) {
      game.push_back(meta::substitute_vars(token_text, subs));
    }
  }
  if (game.empty()) {
    if (error) *error = "版本 JSON 中没有可用的游戏参数";
    return false;
  }

  argv.clear();
  argv.push_back(java_path);
  argv.insert(argv.end(), jvm.begin(), jvm.end());
  argv.push_back(meta.main_class);
  argv.insert(argv.end(), game.begin(), game.end());
  log::info("启动命令: " + process::join_args(argv));
  return true;
}

LaunchResult GameLauncher::run(const std::string& instance, const auth::Account& account,
                               const std::function<void(const std::string&)>& status) {
  LaunchResult result;
  std::vector<std::string> argv;
  if (!build_command(instance, account, argv, &result.error, status)) return result;
  const std::string game_dir =
      paths_.instance_game_dir(instance, config_.version_isolation);
  int exit_code = 0;
  if (!process::spawn_and_wait(process::join_args(argv), game_dir, &exit_code, &result.error))
    return result;
  result.ok = true;
  result.exit_code = exit_code;
  return result;
}

}  // namespace mct::launch
