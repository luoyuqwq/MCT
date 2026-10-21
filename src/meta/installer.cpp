#include "meta/installer.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <regex>

#include "launch/java.hpp"
#include "util/fs.hpp"
#include "util/hash.hpp"
#include "util/log.hpp"
#include "util/process.hpp"
#include "util/str.hpp"
#include "util/zip.hpp"

namespace mct::meta {

namespace {

const char* kFabricMeta = "https://meta.fabricmc.net";
const char* kQuiltMeta = "https://meta.quiltmc.org";
const char* kForgePromotions =
    "https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json";
const char* kNeoForgeMetadata =
    "https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml";

int64_t unix_now() { return static_cast<int64_t>(std::time(nullptr)); }

std::string sanitize_id(std::string id) {
  for (char& c : id) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
        c == '>' || c == '|') {
      c = '-';
    }
  }
  return id;
}

std::vector<std::string> parse_xml_versions(const std::string& xml) {
  std::vector<std::string> out;
  std::regex re("<version>([^<]+)</version>");
  for (std::sregex_iterator it(xml.begin(), xml.end(), re), end; it != end; ++it) {
    out.push_back((*it)[1].str());
  }
  return out;
}

}  // namespace

std::string loader_label(LoaderKind kind) {
  switch (kind) {
    case LoaderKind::Vanilla: return "原版";
    case LoaderKind::Fabric: return "Fabric";
    case LoaderKind::Quilt: return "Quilt";
    case LoaderKind::Forge: return "Forge";
    case LoaderKind::NeoForge: return "NeoForge";
  }
  return "原版";
}

std::string loader_id(LoaderKind kind) {
  switch (kind) {
    case LoaderKind::Vanilla: return "vanilla";
    case LoaderKind::Fabric: return "fabric";
    case LoaderKind::Quilt: return "quilt";
    case LoaderKind::Forge: return "forge";
    case LoaderKind::NeoForge: return "neoforge";
  }
  return "vanilla";
}

LoaderKind loader_from_id(const std::string& id) {
  if (id == "fabric") return LoaderKind::Fabric;
  if (id == "quilt") return LoaderKind::Quilt;
  if (id == "forge") return LoaderKind::Forge;
  if (id == "neoforge") return LoaderKind::NeoForge;
  return LoaderKind::Vanilla;
}

std::string GameInstaller::instance_id_for(const std::string& version, LoaderKind loader) {
  switch (loader) {
    case LoaderKind::Vanilla: return sanitize_id(version);
    case LoaderKind::Fabric: return sanitize_id(version + "-fabric");
    case LoaderKind::Quilt: return sanitize_id(version + "-quilt");
    case LoaderKind::Forge: return sanitize_id(version + "-forge");
    case LoaderKind::NeoForge: return sanitize_id(version + "-neoforge");
  }
  return sanitize_id(version);
}

bool VersionCatalog::load(net::HttpClient& http, const Mirror& mirror, const Paths& paths,
                          bool force, std::string* error) {
  std::string cache = fs::join(paths.cache_dir(), "version_manifest.json");
  const int64_t max_age = 6 * 3600;
  bool use_cache = false;
  if (!force && fs::is_file(cache)) {
    int64_t age = unix_now() - fs::mtime(cache);
    if (age >= 0 && age < max_age) use_cache = true;
  }
  std::string body;
  if (!use_cache) {
    net::HttpResult result = net::get_with_retry(http, mirror.manifest_url(), 3);
    if (result.ok()) {
      body = result.body;
      fs::write_file_atomic(cache, body);
    } else if (fs::is_file(cache) && fs::read_file(cache, body)) {
      log::warn("版本清单在线获取失败，使用本地缓存: " + result.error);
    } else {
      if (error) {
        *error = "获取版本清单失败: " +
                 (result.error.empty() ? str::fmt("HTTP %ld", result.status) : result.error);
      }
      return false;
    }
  } else if (!fs::read_file(cache, body)) {
    if (error) *error = "读取版本清单缓存失败";
    return false;
  }

  std::string parse_error;
  Json j = Json::parse(body, &parse_error);
  if (!parse_error.empty()) {
    if (error) *error = "版本清单解析失败: " + parse_error;
    return false;
  }
  versions.clear();
  latest_release = j["latest"].get_str("release");
  latest_snapshot = j["latest"].get_str("snapshot");
  for (const Json& v : j["versions"].items()) {
    VersionSummary summary;
    summary.id = v.get_str("id");
    summary.type = v.get_str("type");
    summary.url = v.get_str("url");
    summary.sha1 = v.get_str("sha1");
    summary.release_time = v.get_str("releaseTime");
    if (!summary.id.empty()) versions.push_back(std::move(summary));
  }
  return !versions.empty();
}

const VersionSummary* VersionCatalog::find(const std::string& id) const {
  for (const auto& v : versions) {
    if (v.id == id) return &v;
  }
  return nullptr;
}

GameInstaller::GameInstaller(const Paths& paths, const Config& config, const Mirror& mirror,
                             net::HttpClient& http)
    : paths_(paths), config_(config), mirror_(mirror), http_(http) {}

bool GameInstaller::fetch_version_json(const std::string& id, Json& out, std::string* error) {
  std::string path = paths_.instance_json(id);
  if (fs::is_file(path)) {
    std::string parse_error;
    if (Json::parse_file(path, out, &parse_error) && out.is_object()) return true;
  }
  VersionCatalog catalog;
  if (!catalog.load(http_, mirror_, paths_, false, error)) return false;
  const VersionSummary* summary = catalog.find(id);
  if (!summary) {
    if (error) *error = "版本清单中没有 " + id;
    return false;
  }
  std::vector<std::string> urls;
  std::string mirrored = mirror_.version_json_url(id, summary->url);
  if (!mirrored.empty()) urls.push_back(mirrored);
  if (mirrored != summary->url) urls.push_back(summary->url);
  std::string last_error;
  for (const auto& url : urls) {
    net::HttpResult result = net::get_with_retry(http_, url, 3);
    if (!result.ok()) {
      last_error = result.error.empty() ? str::fmt("HTTP %ld", result.status) : result.error;
      continue;
    }
    std::string parse_error;
    Json json = Json::parse(result.body, &parse_error);
    if (!parse_error.empty() || !json.is_object()) {
      last_error = "版本 JSON 解析失败: " + parse_error;
      continue;
    }
    fs::ensure_dir(paths_.instance_dir(id));
    fs::write_file_atomic(path, json.dump(2));
    out = json;
    return true;
  }
  if (error) *error = "获取版本 " + id + " 的 JSON 失败: " + last_error;
  return false;
}

bool GameInstaller::fetch_loader_profile(const InstallRequest& request, Json& profile,
                                         std::string& loader_version, std::string* error) {
  std::string mc = request.version_id;
  if (request.loader == LoaderKind::Fabric) {
    if (loader_version.empty()) {
      std::string list_url = std::string(kFabricMeta) + "/v2/versions/loader/" + mc;
      net::HttpResult result;
      for (const auto& url : mirror_.candidates(list_url)) {
        result = net::get_with_retry(http_, url, 2);
        if (result.ok()) break;
      }
      if (!result.ok()) {
        if (error) *error = "获取 Fabric 加载器列表失败";
        return false;
      }
      std::string parse_error;
      Json list = Json::parse(result.body, &parse_error);
      if (!parse_error.empty() || list.size() == 0) {
        if (error) *error = "Fabric 加载器列表为空";
        return false;
      }
      for (const Json& item : list.items()) {
        if (item["loader"].get_bool("stable", false)) {
          loader_version = item["loader"].get_str("version");
          break;
        }
      }
      if (loader_version.empty()) loader_version = list[0]["loader"].get_str("version");
    }
    std::string url =
        std::string(kFabricMeta) + "/v2/versions/loader/" + mc + "/" + loader_version + "/profile/json";
    for (const auto& candidate : mirror_.candidates(url)) {
      net::HttpResult result = net::get_with_retry(http_, candidate, 2);
      if (!result.ok()) continue;
      std::string parse_error;
      profile = Json::parse(result.body, &parse_error);
      if (parse_error.empty() && profile.is_object()) return true;
    }
  } else if (request.loader == LoaderKind::Quilt) {
    if (loader_version.empty()) {
      std::string list_url = std::string(kQuiltMeta) + "/v3/versions/loader/" + mc;
      net::HttpResult result;
      for (const auto& url : mirror_.candidates(list_url)) {
        result = net::get_with_retry(http_, url, 2);
        if (result.ok()) break;
      }
      if (result.ok()) {
        std::string parse_error;
        Json list = Json::parse(result.body, &parse_error);
        if (parse_error.empty() && list.size() > 0) {
          loader_version = list[0]["loader"].get_str("version");
        }
      }
      if (loader_version.empty()) {
        if (error) *error = "获取 Quilt 加载器版本失败";
        return false;
      }
    }
    std::string url = std::string(kQuiltMeta) + "/v3/versions/loader/" + mc + "/" + loader_version +
                      "/profile/json";
    for (const auto& candidate : mirror_.candidates(url)) {
      net::HttpResult result = net::get_with_retry(http_, candidate, 2);
      if (!result.ok()) continue;
      std::string parse_error;
      profile = Json::parse(result.body, &parse_error);
      if (parse_error.empty() && profile.is_object()) return true;
    }
  }
  if (error) *error = loader_label(request.loader) + " 配置获取失败";
  return false;
}

bool GameInstaller::loader_versions(const std::string& mc_version, LoaderKind kind,
                                    std::vector<std::string>& versions, std::string* error) {
  versions.clear();
  if (kind == LoaderKind::Fabric || kind == LoaderKind::Quilt) {
    std::string base = kind == LoaderKind::Fabric ? kFabricMeta : kQuiltMeta;
    std::string path = kind == LoaderKind::Fabric ? "/v2/versions/loader/" : "/v3/versions/loader/";
    std::string url = base + path + mc_version;
    net::HttpResult result;
    for (const auto& candidate : mirror_.candidates(url)) {
      result = net::get_with_retry(http_, candidate, 2);
      if (result.ok()) break;
    }
    if (!result.ok()) {
      if (error) *error = "获取加载器版本失败";
      return false;
    }
    std::string parse_error;
    Json list = Json::parse(result.body, &parse_error);
    if (!parse_error.empty()) {
      if (error) *error = "加载器版本解析失败";
      return false;
    }
    for (const Json& item : list.items()) {
      std::string version = item["loader"].get_str("version");
      if (!version.empty()) versions.push_back(version);
    }
    return !versions.empty();
  }
  if (kind == LoaderKind::Forge) {
    net::HttpResult result;
    for (const auto& candidate : mirror_.candidates(kForgePromotions)) {
      result = net::get_with_retry(http_, candidate, 2);
      if (result.ok()) break;
    }
    if (!result.ok()) {
      if (error) *error = "获取 Forge 版本失败";
      return false;
    }
    std::string parse_error;
    Json j = Json::parse(result.body, &parse_error);
    if (!parse_error.empty()) {
      if (error) *error = "Forge 版本解析失败";
      return false;
    }
    std::string recommended = j["promos"].get_str(mc_version + "-recommended");
    std::string latest = j["promos"].get_str(mc_version + "-latest");
    if (!latest.empty()) versions.push_back(latest);
    if (!recommended.empty() && recommended != latest) versions.push_back(recommended);
    if (versions.empty()) {
      if (error) *error = "该版本没有可用的 Forge 构建";
      return false;
    }
    return true;
  }
  if (kind == LoaderKind::NeoForge) {
    net::HttpResult result;
    for (const auto& candidate : mirror_.candidates(kNeoForgeMetadata)) {
      result = net::get_with_retry(http_, candidate, 2);
      if (result.ok()) break;
    }
    if (!result.ok()) {
      if (error) *error = "获取 NeoForge 版本失败";
      return false;
    }
    std::vector<std::string> all = parse_xml_versions(result.body);
    // 1.20.4 -> 20.4.，1.21.1 -> 21.1.
    std::string prefix;
    if (str::starts_with(mc_version, "1.")) prefix = mc_version.substr(2) + ".";
    for (const auto& v : all) {
      if (!prefix.empty() && !str::starts_with(v, prefix)) continue;
      versions.push_back(v);
    }
    std::reverse(versions.begin(), versions.end());
    if (versions.empty()) {
      if (error) *error = "该版本没有可用的 NeoForge 构建（1.20.1 请使用 Forge）";
      return false;
    }
    return true;
  }
  if (error) *error = "原版不需要加载器版本";
  return false;
}

bool GameInstaller::download_file(const std::string& url, const std::string& dest,
                                  std::string* error) {
  fs::ensure_dir(fs::parent(dest));
  net::HttpResult result = http_.get_to_file(url, dest);
  if (!result.ok()) {
    if (error) *error = result.error.empty() ? str::fmt("HTTP %ld", result.status) : result.error;
    return false;
  }
  return true;
}

bool GameInstaller::download_one(const std::string& dest, const std::vector<std::string>& urls,
                                 const std::string& sha1, uint64_t size, const std::string& label,
                                 net::Downloader& downloader, std::string* error) {
  std::vector<net::DownloadTask> tasks;
  net::DownloadTask task;
  task.urls = urls;
  task.dest = dest;
  task.sha1 = sha1;
  task.size = size;
  task.label = label;
  tasks.push_back(std::move(task));
  std::string download_error;
  if (!downloader.run(tasks, &download_error)) {
    if (error) *error = download_error;
    return false;
  }
  return true;
}

bool GameInstaller::install_forge(const InstallRequest& request, net::Downloader& downloader,
                                  const StatusFn& status, std::string& instance_id,
                                  std::string& loader_version, std::string* error) {
  std::string mc = request.version_id;
  const bool neoforge = request.loader == LoaderKind::NeoForge;
  if (loader_version.empty()) {
    std::vector<std::string> versions;
    if (!loader_versions(mc, request.loader, versions, error)) return false;
    loader_version = versions.front();
  }

  std::string url;
  std::string file_name;
  if (!neoforge) {
    std::string coord = mc + "-" + loader_version;
    url = "https://maven.minecraftforge.net/net/minecraftforge/forge/" + coord + "/forge-" + coord +
          "-installer.jar";
    file_name = "forge-" + coord + "-installer.jar";
  } else {
    std::string coord = loader_version;
    if (str::starts_with(loader_version, mc + "-"))
      coord = loader_version;
    else
      coord = mc + "-" + loader_version;
    url = "https://maven.neoforged.net/releases/net/neoforged/neoforge/" + loader_version +
          "/neoforge-" + loader_version + "-installer.jar";
    file_name = "neoforge-" + loader_version + "-installer.jar";
    (void)coord;
  }

  std::string installer_path = fs::join(paths_.cache_dir(), "installers", file_name);
  status("下载 " + loader_label(request.loader) + " 安装器…");
  std::string download_error;
  if (!download_one(installer_path, mirror_.candidates(url), "", 0,
                    loader_label(request.loader) + " 安装器", downloader, &download_error)) {
    if (error) *error = "下载安装器失败: " + download_error;
    return false;
  }

  launch::JavaInfo java;
  std::string java_error;
  Config java_config = config_;
  if (!request.java_path.empty()) java_config.java_path = request.java_path;
  int required = 17;
  if (!launch::find_java(paths_, java_config, required, java, &java_error)) {
    if (error) *error = java_error;
    return false;
  }
  if (java.major < 8) {
    if (error) *error = "运行 Forge 安装器至少需要 Java 8，当前为 " + std::to_string(java.major);
    return false;
  }

  fs::ensure_dir(paths_.game_dir());
  std::string profiles = fs::join(paths_.game_dir(), "launcher_profiles.json");
  if (!fs::is_file(profiles)) {
    fs::write_file_atomic(profiles,
                          "{\"profiles\":{},\"settings\":{},\"version\":3}\n");
  }

  std::vector<std::string> before;
  for (const auto& entry : fs::list_dir(paths_.versions_dir())) {
    if (entry.is_dir) before.push_back(entry.name);
  }

  status("运行 " + loader_label(request.loader) + " 安装器（需要下载依赖，请耐心等待）…");
  std::string cmd = process::join_args(
      {java.path, "-jar", installer_path, "--installClient", paths_.game_dir()});
  log::info("运行安装器: " + cmd);
  process::Result run = process::run(cmd, paths_.game_dir());
  if (!run.started) {
    if (error) *error = run.error;
    return false;
  }
  if (run.exit_code != 0) {
    if (error) {
      *error = loader_label(request.loader) + " 安装器退出码 " + std::to_string(run.exit_code) +
               "\n" + str::ellipsize(run.output, 400);
    }
    return false;
  }

  std::string result_id;
  for (const auto& entry : fs::list_dir(paths_.versions_dir())) {
    if (!entry.is_dir) continue;
    if (std::find(before.begin(), before.end(), entry.name) != before.end()) continue;
    std::string json_path = fs::join(paths_.versions_dir(), entry.name, entry.name + ".json");
    if (!fs::is_file(json_path)) continue;
    Json json;
    std::string parse_error;
    if (!Json::parse_file(json_path, json, &parse_error)) continue;
    if (json.get_str("inheritsFrom") == mc) {
      result_id = entry.name;
      break;
    }
  }
  if (result_id.empty()) {
    if (error) *error = "安装器已运行，但未找到生成的版本目录";
    return false;
  }
  instance_id = result_id;
  Json record;
  std::string record_path = fs::join(paths_.instance_dir(instance_id), "mct.json");
  record = Json::object();
  record["loader"] = loader_id(request.loader);
  record["loader_label"] = loader_label(request.loader);
  record["loader_version"] = loader_version;
  record["base_version"] = mc;
  record["installed_at"] = static_cast<long long>(unix_now());
  record["source"] = mirror_.name();
  fs::write_file_atomic(record_path, record.dump(2));
  return true;
}

bool GameInstaller::install(const InstallRequest& request, net::Downloader& downloader,
                            const StatusFn& status, InstallResult& result) {
  result = InstallResult{};
  const std::string mc = request.version_id;
  status("获取 " + mc + " 的版本元数据…");
  Json base_json;
  if (!fetch_version_json(mc, base_json, &result.error)) return false;
  VersionMeta base_meta;
  if (!VersionMeta::parse(base_json, base_meta, &result.error)) return false;

  const bool forge_like =
      request.loader == LoaderKind::Forge || request.loader == LoaderKind::NeoForge;
  std::string loader_version = request.loader_version;
  std::string instance;

  Json install_json = base_json;
  if (request.loader == LoaderKind::Fabric || request.loader == LoaderKind::Quilt) {
    instance = instance_id_for(mc, request.loader);
    status("获取 " + loader_label(request.loader) + " 加载器配置…");
    Json profile;
    if (!fetch_loader_profile(request, profile, loader_version, &result.error)) return false;
    install_json = merge_version_json(base_json, profile);
    profile["id"] = instance;
    profile["inheritsFrom"] = mc;
    fs::ensure_dir(paths_.instance_dir(instance));
    fs::write_file_atomic(paths_.instance_json(instance), profile.dump(2));
  } else if (forge_like) {
    status("准备 " + loader_label(request.loader) + " 安装器…");
    if (!install_forge(request, downloader, status, instance, loader_version, &result.error))
      return false;
  } else {
    instance = instance_id_for(mc, request.loader);
    fs::ensure_dir(paths_.instance_dir(instance));
    fs::write_file_atomic(paths_.instance_json(instance), base_json.dump(2));
  }
  result.instance_id = instance;

  VersionMeta meta;
  if (request.loader == LoaderKind::Vanilla) {
    meta = base_meta;
  } else if (forge_like) {
    if (!load_version_meta(paths_, instance, meta, &result.error)) return false;
  } else {
    if (!VersionMeta::parse(install_json, meta, &result.error)) return false;
  }

  // ---------- 下载客户端、库、日志配置、资源索引 ----------
  std::vector<net::DownloadTask> tasks;
  auto make_task = [&](const std::string& dest, const std::string& url, const std::string& sha1,
                       uint64_t size, const std::string& label) {
    net::DownloadTask task;
    task.urls = mirror_.candidates(url);
    task.dest = dest;
    task.sha1 = sha1;
    task.size = size;
    task.label = label;
    return task;
  };

  if (!meta.client.url.empty()) {
    tasks.push_back(make_task(paths_.instance_jar(mc), meta.client.url, meta.client.sha1,
                              meta.client.size, "客户端 " + mc + ".jar"));
  }
  for (const auto& lib : meta.libraries) {
    std::string dest = fs::join(paths_.libraries_dir(), lib.download.path);
    std::string url = lib.download.url.empty() ? lib.fallback_url() : lib.download.url;
    tasks.push_back(make_task(dest, url, lib.download.sha1, lib.download.size,
                              fs::filename(dest)));
  }
  if (!meta.logging_file.url.empty()) {
    std::string log_id =
        meta.logging_file.path.empty() ? "client-logging.xml" : meta.logging_file.path;
    tasks.push_back(make_task(fs::join(paths_.cache_dir(), "log_configs", log_id),
                              meta.logging_file.url, meta.logging_file.sha1,
                              meta.logging_file.size, "日志配置"));
  }
  if (!meta.asset_index.url.empty()) {
    tasks.push_back(make_task(fs::join(paths_.assets_dir(), "indexes", meta.assets_id + ".json"),
                              meta.asset_index.url, meta.asset_index.sha1, meta.asset_index.size,
                              "资源索引 " + meta.assets_id + ".json"));
  }

  status("下载游戏文件（" + std::to_string(tasks.size()) + " 个）…");
  std::string download_error;
  if (!downloader.run(tasks, &download_error)) {
    result.error = "下载失败: " + download_error;
    return false;
  }

  // ---------- 资源对象 ----------
  std::string index_path = fs::join(paths_.assets_dir(), "indexes", meta.assets_id + ".json");
  Json index;
  if (!Json::parse_file(index_path, index, &result.error)) {
    result.error = "资源索引读取失败: " + index_path;
    return false;
  }
  const bool virtual_assets = index.get_bool("virtual");
  const bool map_to_resources = index.get_bool("map_to_resources");
  const Json& objects = index["objects"];
  if (config_.download_assets && objects.size() > 0) {
    std::vector<net::DownloadTask> asset_tasks;
    asset_tasks.reserve(objects.size());
    for (const auto& kv : objects.members()) {
      std::string hash = kv.second.get_str("hash");
      if (hash.size() < 2) continue;
      std::string prefix = hash.substr(0, 2);
      std::string dest = fs::join(paths_.assets_dir(), "objects", prefix, hash);
      std::string url = "https://resources.download.minecraft.net/" + prefix + "/" + hash;
      asset_tasks.push_back(make_task(dest, url, hash,
                                      static_cast<uint64_t>(kv.second.get_int("size", 0)),
                                      fs::filename(dest)));
    }
    status("下载资源文件（" + std::to_string(asset_tasks.size()) + " 个）…");
    if (!downloader.run(asset_tasks, &download_error)) {
      result.error = "资源文件下载失败: " + download_error;
      return false;
    }
  }

  if (virtual_assets || map_to_resources) {
    std::string virtual_root = fs::join(paths_.assets_dir(), "virtual", meta.assets_id);
    std::string resources_dir =
        fs::join(paths_.instance_game_dir(instance, config_.version_isolation), "resources");
    for (const auto& kv : objects.members()) {
      std::string hash = kv.second.get_str("hash");
      if (hash.size() < 2) continue;
      std::string source = fs::join(paths_.assets_dir(), "objects", hash.substr(0, 2), hash);
      if (!fs::is_file(source)) continue;
      std::string name = str::replace_all(kv.first, "\\", "/");
      if (virtual_assets) {
        std::string dest = fs::join(virtual_root, name);
        if (!fs::is_file(dest)) fs::copy_file(source, dest);
      }
      if (map_to_resources) {
        std::string dest = fs::join(resources_dir, name);
        if (!fs::is_file(dest)) fs::copy_file(source, dest);
      }
    }
  }

  // ---------- natives ----------
  status("解压 natives…");
  std::string natives_dir = paths_.instance_natives(instance);
  fs::remove_dir_recursive(natives_dir);
  fs::ensure_dir(natives_dir);
  for (const auto& lib : meta.libraries) {
    if (!lib.is_native) continue;
    std::string jar = fs::join(paths_.libraries_dir(), lib.download.path);
    std::string data;
    if (!fs::read_file(jar, data)) {
      log::warn("natives 文件缺失: " + jar);
      continue;
    }
    std::vector<std::string> skip = {"META-INF/"};
    for (const auto& exclude : lib.extract_exclude) skip.push_back(exclude);
    std::string extract_error;
    if (!zip::extract_all(data, natives_dir, &extract_error, skip)) {
      result.error = "解压 natives 失败: " + extract_error;
      return false;
    }
  }

  // ---------- 安装记录 ----------
  Json record = Json::object();
  record["loader"] = loader_id(request.loader);
  record["loader_label"] = loader_label(request.loader);
  record["loader_version"] = loader_version;
  record["base_version"] = mc;
  record["installed_at"] = static_cast<long long>(unix_now());
  record["source"] = mirror_.name();
  record["java_major"] = meta.java_major;
  fs::ensure_dir(paths_.instance_dir(instance));
  fs::write_file_atomic(fs::join(paths_.instance_dir(instance), "mct.json"), record.dump(2) + "\n");

  result.ok = true;
  status("安装完成");
  return true;
}

}  // namespace mct::meta
