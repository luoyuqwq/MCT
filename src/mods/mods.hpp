// 模组源：国内镜像（MCIM）/ CurseForge / Modrinth
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "meta/mirror.hpp"
#include "net/downloader.hpp"
#include "net/http.hpp"

namespace mct::mods {

enum class Provider { Domestic, CurseForge, Modrinth };

struct ModProject {
  std::string id;
  std::string slug;
  std::string title;
  std::string description;
  std::string author;
  uint64_t downloads = 0;
};

struct ModDependency {
  std::string project_id;
  std::string version_id;
};

struct ModFile {
  std::string project_id;
  std::string version_id;
  std::string version_name;
  std::string file_name;
  std::string url;
  std::string sha1;
  uint64_t size = 0;
  std::vector<ModDependency> required;
};

struct SearchQuery {
  std::string text;
  std::string game_version;
  std::string loader;  // fabric / quilt / forge / neoforge / 空
  int offset = 0;
  int limit = 20;
};

class ModApi {
 public:
  ModApi(net::HttpClient& http, Config config, const meta::Mirror& mirror);

  Provider provider() const { return provider_; }
  void set_provider(Provider provider);
  std::string provider_label() const;
  std::string provider_key() const;  // domestic / curseforge / modrinth

  bool search(const SearchQuery& query, std::vector<ModProject>& out, std::string* error);
  bool files(const std::string& project_id, const std::string& game_version,
             const std::string& loader, std::vector<ModFile>& out, std::string* error);
  bool file_by_id(const std::string& project_id, const std::string& version_id, ModFile& out,
                  std::string* error);
  bool dependency_file(const ModDependency& dependency, const std::string& game_version,
                       const std::string& loader, ModFile& out, std::string* error);

  // 自定义镜像站上可能的文件地址（用户把模组也传到自己网站时优先使用）
  std::vector<std::string> file_mirror_candidates(const std::string& file_name) const;

  static std::string loader_display(const std::string& loader);

 private:
  bool search_modrinth(const SearchQuery&, std::vector<ModProject>&, std::string*);
  bool files_modrinth(const std::string& project_id, const std::string& game_version,
                      const std::string& loader, std::vector<ModFile>&, std::string*);
  bool version_by_id_modrinth(const std::string& version_id, ModFile&, std::string*);
  bool search_curseforge(const SearchQuery&, std::vector<ModProject>&, std::string*);
  bool files_curseforge(const std::string& project_id, const std::string& game_version,
                        const std::string& loader, std::vector<ModFile>&, std::string*);
  std::string modrinth_base() const;
  net::HttpResult modrinth_get(const std::string& path);
  int curseforge_loader_type(const std::string& loader) const;
  net::HttpResult cf_get(const std::string& path);

  net::HttpClient& http_;
  Config config_;
  const meta::Mirror& mirror_;
  Provider provider_ = Provider::Domestic;
};

// 安装到 mods 目录（自动处理必需依赖），并写入 .mct-mods.json
class ModInstaller {
 public:
  ModInstaller(ModApi& api, net::Downloader& downloader);
  bool install(const ModFile& root, const std::string& mods_dir, const std::string& game_version,
               const std::string& loader, const std::function<void(const std::string&)>& status,
               std::string* error);
  static std::string installed_manifest_path(const std::string& mods_dir);
  static Json load_manifest(const std::string& mods_dir);

 private:
  bool install_one(const ModFile& file, const std::string& mods_dir, const std::string& game_version,
                   const std::string& loader, std::vector<std::string>& visited, int depth,
                   const std::function<void(const std::string&)>& status, std::string* error);
  ModApi& api_;
  net::Downloader& downloader_;
};

}  // namespace mct::mods
