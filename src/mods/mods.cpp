#include "mods/mods.hpp"

#include <algorithm>
#include <ctime>

#include "util/fs.hpp"
#include "util/json.hpp"
#include "util/log.hpp"
#include "util/str.hpp"

namespace mct::mods {

namespace {

const char* kMcimModrinth = "https://mod.mcimirror.top/modrinth";
const char* kModrinthApi = "https://api.modrinth.com";
const char* kCurseForgeApi = "https://api.curseforge.com";

int64_t unix_now() { return static_cast<int64_t>(std::time(nullptr)); }

std::string json_string_array(const std::string& value) {
  return "[\"" + value + "\"]";
}

}  // namespace

void ModApi::set_provider(Provider provider) { provider_ = provider; }

std::string ModApi::provider_label() const {
  switch (provider_) {
    case Provider::Domestic: return "国内镜像 (MCIM)";
    case Provider::CurseForge: return "CurseForge";
    case Provider::Modrinth: return "Modrinth";
  }
  return "Modrinth";
}

std::string ModApi::provider_key() const {
  switch (provider_) {
    case Provider::Domestic: return "domestic";
    case Provider::CurseForge: return "curseforge";
    case Provider::Modrinth: return "modrinth";
  }
  return "modrinth";
}

std::string ModApi::loader_display(const std::string& loader) {
  if (loader == "fabric") return "Fabric";
  if (loader == "quilt") return "Quilt";
  if (loader == "forge") return "Forge";
  if (loader == "neoforge") return "NeoForge";
  return "原版";
}

ModApi::ModApi(net::HttpClient& http, Config config, const meta::Mirror& mirror)
    : http_(http), config_(std::move(config)), mirror_(mirror) {
  switch (config_.mod_source) {
    case ModSource::CurseForge: provider_ = Provider::CurseForge; break;
    case ModSource::Modrinth: provider_ = Provider::Modrinth; break;
    default: provider_ = Provider::Domestic; break;
  }
}

std::string ModApi::modrinth_base() const {
  return provider_ == Provider::Domestic ? kMcimModrinth : kModrinthApi;
}

net::HttpResult ModApi::modrinth_get(const std::string& path) {
  net::HttpResult result = http_.get(modrinth_base() + path);
  if (result.ok() || provider_ != Provider::Domestic) return result;
  static bool warned = false;
  if (!warned) {
    log::warn("国内镜像 MCIM 不可用，自动回退到 Modrinth 官方 API");
    warned = true;
  }
  net::HttpResult fallback = http_.get(std::string(kModrinthApi) + path);
  return fallback.ok() ? fallback : result;
}

bool ModApi::search(const SearchQuery& query, std::vector<ModProject>& out, std::string* error) {
  if (provider_ == Provider::CurseForge) return search_curseforge(query, out, error);
  return search_modrinth(query, out, error);
}

bool ModApi::files(const std::string& project_id, const std::string& game_version,
                   const std::string& loader, std::vector<ModFile>& out, std::string* error) {
  if (provider_ == Provider::CurseForge)
    return files_curseforge(project_id, game_version, loader, out, error);
  return files_modrinth(project_id, game_version, loader, out, error);
}

bool ModApi::file_by_id(const std::string& project_id, const std::string& version_id, ModFile& out,
                        std::string* error) {
  if (provider_ == Provider::CurseForge) {
    std::vector<ModFile> list;
    if (!files_curseforge(project_id, "", "", list, error)) return false;
    for (const auto& file : list) {
      if (file.version_id == version_id) {
        out = file;
        return true;
      }
    }
    if (error) *error = "找不到指定的 CurseForge 文件";
    return false;
  }
  return version_by_id_modrinth(version_id, out, error);
}

bool ModApi::dependency_file(const ModDependency& dependency, const std::string& game_version,
                             const std::string& loader, ModFile& out, std::string* error) {
  if (!dependency.version_id.empty() && provider_ != Provider::CurseForge) {
    if (file_by_id(dependency.project_id, dependency.version_id, out, error)) return true;
  }
  std::vector<ModFile> list;
  if (!files(dependency.project_id, game_version, loader, list, error)) return false;
  if (list.empty()) {
    if (error) *error = "依赖模组没有适配当前版本的构建";
    return false;
  }
  out = std::move(list.front());
  return true;
}

std::vector<std::string> ModApi::file_mirror_candidates(const std::string& file_name) const {
  return mirror_.mod_file_candidates(provider_key(), file_name);
}

bool ModApi::search_modrinth(const SearchQuery& query, std::vector<ModProject>& out,
                             std::string* error) {
  int limit = query.limit > 0 ? query.limit : 20;
  std::string facets = "[[\"project_type:mod\"]";
  if (!query.game_version.empty()) facets += ",[\"versions:" + query.game_version + "\"]";
  if (!query.loader.empty()) facets += ",[\"categories:" + query.loader + "\"]";
  facets += "]";
  std::string path = "/v2/search?query=" + str::percent_encode(query.text) +
                     "&limit=" + std::to_string(limit) +
                     "&offset=" + std::to_string(std::max(0, query.offset)) +
                     "&index=relevance&facets=" + str::percent_encode(facets);
  net::HttpResult result = modrinth_get(path);
  if (!result.ok()) {
    if (error)
      *error = "搜索模组失败: " +
               (result.error.empty() ? str::fmt("HTTP %ld", result.status) : result.error);
    return false;
  }
  std::string parse_error;
  Json j = Json::parse(result.body, &parse_error);
  if (!parse_error.empty()) {
    if (error) *error = "搜索结果解析失败: " + parse_error;
    return false;
  }
  out.clear();
  for (const Json& hit : j["hits"].items()) {
    ModProject project;
    project.id = hit.get_str("project_id");
    project.slug = hit.get_str("slug");
    project.title = hit.get_str("title");
    project.description = hit.get_str("description");
    project.author = hit.get_str("author");
    project.downloads = static_cast<uint64_t>(hit.get_int("downloads", 0));
    if (!project.id.empty()) out.push_back(std::move(project));
  }
  return true;
}

namespace {
ModFile parse_modrinth_file(const Json& version) {
  ModFile file;
  file.project_id = version.get_str("project_id");
  file.version_id = version.get_str("id");
  file.version_name = version.get_str("version_number");
  if (file.version_name.empty()) file.version_name = version.get_str("name");
  const Json& files = version["files"];
  const Json* chosen = nullptr;
  for (const Json& f : files.items()) {
    if (f.get_bool("primary", false)) {
      chosen = &f;
      break;
    }
  }
  if (!chosen && files.size() > 0) chosen = &files[0];
  if (chosen) {
    file.url = chosen->get_str("url");
    file.file_name = chosen->get_str("filename");
    file.sha1 = (*chosen)["hashes"].get_str("sha1");
    file.size = static_cast<uint64_t>(chosen->get_int("size", 0));
  }
  for (const Json& dep : version["dependencies"].items()) {
    if (dep.get_str("dependency_type") != "required") continue;
    ModDependency d;
    d.project_id = dep.get_str("project_id");
    d.version_id = dep.get_str("version_id");
    if (!d.project_id.empty()) file.required.push_back(d);
  }
  return file;
}
}  // namespace

bool ModApi::files_modrinth(const std::string& project_id, const std::string& game_version,
                            const std::string& loader, std::vector<ModFile>& out,
                            std::string* error) {
  std::string path = "/v2/project/" + project_id + "/version";
  std::vector<std::string> params;
  if (!game_version.empty())
    params.push_back("game_versions=" + str::percent_encode(json_string_array(game_version)));
  if (!loader.empty()) params.push_back("loaders=" + str::percent_encode(json_string_array(loader)));
  if (!params.empty()) path += "?" + str::join(params, "&");
  net::HttpResult result = modrinth_get(path);
  if (!result.ok()) {
    if (error)
      *error = "获取模组文件失败: " +
               (result.error.empty() ? str::fmt("HTTP %ld", result.status) : result.error);
    return false;
  }
  std::string parse_error;
  Json j = Json::parse(result.body, &parse_error);
  if (!parse_error.empty()) {
    if (error) *error = "模组文件解析失败: " + parse_error;
    return false;
  }
  out.clear();
  for (const Json& v : j.items()) {
    ModFile file = parse_modrinth_file(v);
    if (!file.url.empty()) out.push_back(std::move(file));
  }
  return true;
}

bool ModApi::version_by_id_modrinth(const std::string& version_id, ModFile& out,
                                    std::string* error) {
  net::HttpResult result = modrinth_get("/v2/version/" + version_id);
  if (!result.ok()) {
    if (error) *error = "获取依赖版本失败";
    return false;
  }
  std::string parse_error;
  Json j = Json::parse(result.body, &parse_error);
  if (!parse_error.empty()) {
    if (error) *error = "依赖版本解析失败";
    return false;
  }
  out = parse_modrinth_file(j);
  return true;
}

int ModApi::curseforge_loader_type(const std::string& loader) const {
  if (loader == "forge") return 1;
  if (loader == "fabric") return 4;
  if (loader == "quilt") return 5;
  if (loader == "neoforge") return 6;
  return 0;
}

net::HttpResult ModApi::cf_get(const std::string& path) {
  std::vector<net::HttpHeader> headers = {{"Accept", "application/json"}};
  if (!config_.curseforge_api_key.empty()) headers.push_back({"x-api-key", config_.curseforge_api_key});
  return http_.request("GET", std::string(kCurseForgeApi) + path, "", headers, nullptr, "");
}

bool ModApi::search_curseforge(const SearchQuery& query, std::vector<ModProject>& out,
                               std::string* error) {
  if (config_.curseforge_api_key.empty()) {
    if (error)
      *error = "CurseForge 需要 API Key，请在「设置 → CurseForge API Key」中填写（可免费申请）";
    return false;
  }
  std::string path = "/v1/mods/search?gameId=432&classId=6&sortField=2&sortOrder=desc";
  path += "&searchFilter=" + str::percent_encode(query.text);
  path += "&index=" + std::to_string(std::max(0, query.offset));
  path += "&pageSize=" + std::to_string(query.limit > 0 ? query.limit : 20);
  if (!query.game_version.empty()) path += "&gameVersion=" + str::percent_encode(query.game_version);
  int loader = curseforge_loader_type(query.loader);
  if (loader > 0) path += "&modLoaderType=" + std::to_string(loader);
  net::HttpResult result = cf_get(path);
  if (!result.ok()) {
    if (error)
      *error = "CurseForge 搜索失败: " +
               (result.error.empty() ? str::fmt("HTTP %ld", result.status) : result.error);
    return false;
  }
  std::string parse_error;
  Json j = Json::parse(result.body, &parse_error);
  if (!parse_error.empty()) {
    if (error) *error = "CurseForge 响应解析失败";
    return false;
  }
  out.clear();
  for (const Json& item : j["data"].items()) {
    ModProject project;
    project.id = std::to_string(item.get_int("id", 0));
    project.slug = item.get_str("slug");
    project.title = item.get_str("name");
    project.description = item.get_str("summary");
    project.downloads = static_cast<uint64_t>(item.get_int("downloadCount", 0));
    const Json& authors = item["authors"];
    if (authors.size() > 0) project.author = authors[0].get_str("name");
    if (project.id != "0") out.push_back(std::move(project));
  }
  return true;
}

bool ModApi::files_curseforge(const std::string& project_id, const std::string& game_version,
                              const std::string& loader, std::vector<ModFile>& out,
                              std::string* error) {
  if (config_.curseforge_api_key.empty()) {
    if (error) *error = "CurseForge 需要 API Key，请先在设置中填写";
    return false;
  }
  std::string path = "/v1/mods/" + project_id + "/files?pageSize=50";
  if (!game_version.empty()) path += "&gameVersion=" + str::percent_encode(game_version);
  int loader_type = curseforge_loader_type(loader);
  if (loader_type > 0) path += "&modLoaderType=" + std::to_string(loader_type);
  net::HttpResult result = cf_get(path);
  if (!result.ok()) {
    if (error) *error = "获取 CurseForge 文件失败: " + str::fmt("HTTP %ld", result.status);
    return false;
  }
  std::string parse_error;
  Json j = Json::parse(result.body, &parse_error);
  if (!parse_error.empty()) {
    if (error) *error = "CurseForge 文件解析失败";
    return false;
  }
  out.clear();
  for (const Json& item : j["data"].items()) {
    ModFile file;
    file.project_id = project_id;
    file.version_id = std::to_string(item.get_int("id", 0));
    file.version_name = item.get_str("displayName");
    file.file_name = item.get_str("fileName");
    file.url = item.get_str("downloadUrl");
    file.size = static_cast<uint64_t>(item.get_int("fileLength", 0));
    for (const Json& hash : item["hashes"].items()) {
      if (hash.get_int("algo", 0) == 1) file.sha1 = hash.get_str("value");
    }
    if (file.url.empty() && !file.version_id.empty() && !file.file_name.empty()) {
      long long id = std::stoll(file.version_id);
      file.url = "https://mediafilez.forgecdn.net/files/" + std::to_string(id / 1000) + "/" +
                 std::to_string(id % 1000) + "/" + file.file_name;
    }
    for (const Json& dep : item["dependencies"].items()) {
      if (dep.get_int("relationType", 0) != 3) continue;
      ModDependency d;
      d.project_id = std::to_string(dep.get_int("modId", 0));
      if (d.project_id != "0") file.required.push_back(d);
    }
    if (!file.file_name.empty()) out.push_back(std::move(file));
  }
  return true;
}

std::string ModInstaller::installed_manifest_path(const std::string& mods_dir) {
  return fs::join(mods_dir, ".mct-mods.json");
}

Json ModInstaller::load_manifest(const std::string& mods_dir) {
  Json manifest;
  std::string err;
  if (!Json::parse_file(installed_manifest_path(mods_dir), manifest, &err)) {
    manifest = Json::object();
    manifest["files"] = Json::object();
  }
  if (!manifest.has("files")) manifest["files"] = Json::object();
  return manifest;
}

ModInstaller::ModInstaller(ModApi& api, net::Downloader& downloader)
    : api_(api), downloader_(downloader) {}

bool ModInstaller::install(const ModFile& root, const std::string& mods_dir,
                           const std::string& game_version, const std::string& loader,
                           const std::function<void(const std::string&)>& status,
                           std::string* error) {
  fs::ensure_dir(mods_dir);
  std::vector<std::string> visited;
  return install_one(root, mods_dir, game_version, loader, visited, 0, status, error);
}

bool ModInstaller::install_one(const ModFile& file, const std::string& mods_dir,
                               const std::string& game_version, const std::string& loader,
                               std::vector<std::string>& visited, int depth,
                               const std::function<void(const std::string&)>& status,
                               std::string* error) {
  if (depth > 4) return true;
  std::string key = file.project_id.empty() ? file.file_name : file.project_id;
  if (key.empty()) return true;
  if (std::find(visited.begin(), visited.end(), key) != visited.end()) return true;
  visited.push_back(key);

  if (file.file_name.empty() || file.url.empty()) {
    if (error) *error = "该文件没有可用的下载地址";
    return false;
  }
  if (status) status("安装 " + file.file_name);

  fs::ensure_dir(mods_dir);
  std::string dest = fs::join(mods_dir, file.file_name);
  net::DownloadTask task;
  for (const auto& mirror_url : api_.file_mirror_candidates(file.file_name))
    task.urls.push_back(mirror_url);
  task.urls.push_back(file.url);
  task.dest = dest;
  task.sha1 = file.sha1;
  task.size = file.size;
  task.label = file.file_name;
  std::vector<net::DownloadTask> tasks;
  tasks.push_back(std::move(task));
  std::string download_error;
  if (!downloader_.run(tasks, &download_error)) {
    if (error) *error = "下载 " + file.file_name + " 失败: " + download_error;
    return false;
  }

  Json manifest = load_manifest(mods_dir);
  Json entry = Json::object();
  entry["project_id"] = file.project_id;
  entry["version_id"] = file.version_id;
  entry["version_name"] = file.version_name;
  entry["file_name"] = file.file_name;
  entry["source"] = api_.provider_key();
  entry["sha1"] = file.sha1;
  entry["installed_at"] = static_cast<long long>(unix_now());
  manifest["files"].set(file.file_name, entry);
  fs::write_file_atomic(installed_manifest_path(mods_dir), manifest.dump(2) + "\n");

  for (const ModDependency& dep : file.required) {
    ModFile dependency{};
    if (!api_.dependency_file(dep, game_version, loader, dependency, error)) {
      log::warn("依赖安装失败: " + (error ? *error : std::string()));
      continue;
    }
    if (dependency.file_name == file.file_name) continue;
    if (!install_one(dependency, mods_dir, game_version, loader, visited, depth + 1, status, error))
      return false;
  }
  return true;
}

}  // namespace mct::mods
