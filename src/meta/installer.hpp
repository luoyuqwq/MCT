// 版本安装：原版 + Fabric/Quilt/Forge/NeoForge，含资源索引与 natives 解压
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "meta/mirror.hpp"
#include "meta/version.hpp"
#include "net/downloader.hpp"
#include "net/http.hpp"

namespace mct::meta {

enum class LoaderKind { Vanilla, Fabric, Quilt, Forge, NeoForge };

std::string loader_label(LoaderKind kind);
std::string loader_id(LoaderKind kind);
LoaderKind loader_from_id(const std::string& id);

struct VersionSummary {
  std::string id;
  std::string type;
  std::string url;
  std::string sha1;
  std::string release_time;
};

struct VersionCatalog {
  std::vector<VersionSummary> versions;
  std::string latest_release;
  std::string latest_snapshot;

  bool load(net::HttpClient& http, const Mirror& mirror, const Paths& paths, bool force,
            std::string* error);
  const VersionSummary* find(const std::string& id) const;
};

struct InstallRequest {
  std::string version_id;
  LoaderKind loader = LoaderKind::Vanilla;
  std::string loader_version;  // 空 = 自动选择
  std::string java_path;       // Forge 安装器用；空 = 自动
};

struct InstallResult {
  bool ok = false;
  std::string error;
  std::string instance_id;
};

using StatusFn = std::function<void(const std::string&)>;

class GameInstaller {
 public:
  GameInstaller(const Paths& paths, const Config& config, const Mirror& mirror,
                net::HttpClient& http);

  bool install(const InstallRequest& request, net::Downloader& downloader, const StatusFn& status,
               InstallResult& result);

  bool loader_versions(const std::string& mc_version, LoaderKind kind,
                       std::vector<std::string>& versions, std::string* error);

  static std::string instance_id_for(const std::string& version, LoaderKind loader);

 private:
  bool fetch_version_json(const std::string& id, Json& out, std::string* error);
  bool fetch_loader_profile(const InstallRequest& request, Json& profile,
                            std::string& loader_version, std::string* error);
  bool install_forge(const InstallRequest& request, net::Downloader& downloader,
                     const StatusFn& status, std::string& instance_id,
                     std::string& loader_version, std::string* error);
  bool download_file(const std::string& url, const std::string& dest, std::string* error);
  bool download_one(const std::string& dest, const std::vector<std::string>& urls,
                    const std::string& sha1, uint64_t size, const std::string& label,
                    net::Downloader& downloader, std::string* error);

  const Paths& paths_;
  Config config_;
  const Mirror& mirror_;
  net::HttpClient& http_;
};

}  // namespace mct::meta
