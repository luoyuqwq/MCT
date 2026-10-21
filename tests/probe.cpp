// 网络冒烟测试：镜像清单 -> 版本 JSON -> 版本解析 -> 模组搜索/文件列表
// 不下载游戏文件，只验证网络与解析链路。
#include <cstdio>
#include <string>

#include "core/config.hpp"
#include "meta/installer.hpp"
#include "meta/mirror.hpp"
#include "meta/version.hpp"
#include "mods/mods.hpp"
#include "net/http.hpp"
#include "auth/microsoft.hpp"
#include "util/fs.hpp"
#include "util/log.hpp"
#include "util/str.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
int g_failures = 0;

void check(bool ok, const std::string& what) {
  std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
  if (!ok) ++g_failures;
}
}  // namespace

int main() {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  mct::Paths paths = mct::Paths::resolve(mct::fs::join(mct::fs::temp_dir(), "mct_probe_home"));
  mct::fs::ensure_dir(paths.home);
  mct::log::init(paths.log_file());

  mct::net::HttpClient http;
  mct::meta::Mirror mirror(mct::GameSource::Bmclapi, "", paths.cache_dir());
  mct::net::HttpResult ping = http.get("https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json");
  check(ping.ok(), "BMCLAPI 版本清单可访问");

  mct::meta::VersionCatalog catalog;
  std::string error;
  bool loaded = catalog.load(http, mirror, paths, true, &error);
  check(loaded, "解析版本清单：" + error);
  if (!loaded) return 1;
  std::printf("      version 数量 = %zu, latest_release = %s\n", catalog.versions.size(),
              catalog.latest_release.c_str());

  const mct::meta::VersionSummary* summary = catalog.find("1.20.1");
  check(summary != nullptr, "清单中存在 1.20.1");
  if (!summary) return 1;

  std::string url = mirror.version_json_url("1.20.1", summary->url);
  mct::net::HttpResult version_result = http.get(url);
  check(version_result.ok(), "下载 1.20.1 版本 JSON");
  if (!version_result.ok()) return 1;

  std::string parse_error;
  mct::Json version_json = mct::Json::parse(version_result.body, &parse_error);
  check(parse_error.empty(), "版本 JSON 可解析：" + parse_error);

  mct::meta::VersionMeta meta;
  bool meta_ok = mct::meta::VersionMeta::parse(version_json, meta, &error);
  check(meta_ok, "版本 JSON 结构解析：" + error);
  if (meta_ok) {
    std::printf("      mainClass = %s\n", meta.main_class.c_str());
    std::printf("      java = %d, libraries = %zu, assets = %s\n", meta.java_major,
                meta.libraries.size(), meta.assets_id.c_str());
    std::printf("      client = %s\n", meta.client.url.c_str());
    int natives = 0;
    for (const auto& lib : meta.libraries) {
      if (lib.is_native) ++natives;
    }
    std::printf("      natives 库 = %d\n", natives);
    check(!meta.libraries.empty(), "库列表非空");
    check(!meta.client.url.empty(), "客户端 jar 有下载地址");
    std::string first_url = mirror.candidates(meta.libraries.front().download.url).front();
    mct::net::HttpResult head = http.get(first_url);
    check(head.ok(), "镜像可获取第一个库：" + first_url);
  }

  mct::Config config;
  config.mod_source = mct::ModSource::Domestic;
  mct::mods::ModApi api(http, config, mirror);
  mct::mods::SearchQuery query;
  query.text = "sodium";
  query.game_version = "1.20.1";
  query.loader = "fabric";
  query.limit = 5;
  std::vector<mct::mods::ModProject> projects;
  bool searched = api.search(query, projects, &error);
  check(searched, "国内镜像搜索模组：" + error);
  if (searched) {
    std::printf("      搜索到 %zu 个结果\n", projects.size());
    for (size_t i = 0; i < projects.size() && i < 3; ++i) {
      std::printf("      - %s (by %s, %s 次下载)\n", projects[i].title.c_str(),
                  projects[i].author.c_str(),
                  mct::str::human_count(projects[i].downloads).c_str());
    }
    check(!projects.empty(), "搜索结果非空");
    if (!projects.empty()) {
      std::vector<mct::mods::ModFile> files;
      bool got_files = api.files(projects[0].id, "1.20.1", "fabric", files, &error);
      check(got_files, "获取模组文件列表：" + error);
      if (got_files && !files.empty()) {
        std::printf("      文件: %s (%s, sha1=%s)\n", files[0].file_name.c_str(),
                    mct::str::human_size(files[0].size).c_str(), files[0].sha1.c_str());
        check(!files[0].url.empty() && !files[0].sha1.empty(), "文件有下载地址与 SHA1");
      }
    }
  }

  // CurseForge 无 Key 时应给出清晰错误
  mct::Config cf_config;
  cf_config.mod_source = mct::ModSource::CurseForge;
  mct::mods::ModApi cf(http, cf_config, mirror);
  std::vector<mct::mods::ModProject> cf_projects;
  std::string cf_error;
  bool cf_ok = cf.search(query, cf_projects, &cf_error);
  check(!cf_ok && mct::str::contains(cf_error, "API Key"),
        "CurseForge 缺少 Key 时提示明确：" + cf_error);

  // 微软登录：验证内置公开 Client ID 与 Live 设备码接口
  mct::auth::MicrosoftAuth ms_auth(http);
  mct::auth::DeviceCode device_code;
  std::string ms_error;
  bool ms_ok = ms_auth.request_device_code(device_code, &ms_error);
  check(ms_ok && !device_code.user_code.empty(), "微软 Live 设备码接口：" + ms_error);
  if (ms_ok) {
    std::printf("      user_code = %s, uri = %s, interval = %d\n", device_code.user_code.c_str(),
                device_code.verification_uri.c_str(), device_code.interval);
    mct::auth::DevicePoll poll = ms_auth.poll_device_code(device_code);
    std::printf("      poll: state=%s status=%ld msg=%s\n", poll.state_name(), poll.status,
                poll.message.c_str());
    check(poll.state == mct::auth::DevicePoll::State::Pending,
          "未授权时设备码轮询应返回 pending");
  }

  mct::log::shutdown();
  if (g_failures == 0) {
    std::printf("\n网络冒烟测试全部通过\n");
    return 0;
  }
  std::printf("\n%d 项失败\n", g_failures);
  return 1;
}
