#include "launch/java_install.hpp"

#include <algorithm>
#include <regex>
#include <vector>

#include "core/config.hpp"
#include "net/downloader.hpp"
#include "net/http.hpp"
#include "util/fs.hpp"
#include "util/json.hpp"
#include "util/log.hpp"
#include "util/process.hpp"
#include "util/str.hpp"
#include "util/zip.hpp"

namespace mct::launch {

#ifdef _WIN32
namespace {

struct Candidate {
  std::string url;
  std::string file_name;
  std::string source;
};

bool find_java_binary(const std::string& root, std::string& out, int depth = 0) {
#ifdef _WIN32
  const char* exe = "java.exe";
#else
  const char* exe = "java";
#endif
  const std::string direct = fs::join(fs::join(root, "bin"), exe);
  if (fs::is_file(direct)) {
    out = direct;
    return true;
  }
  if (depth >= 4) return false;
  for (const auto& entry : fs::list_dir(root)) {
    if (!entry.is_dir) continue;
    if (find_java_binary(fs::join(root, entry.name), out, depth + 1)) return true;
  }
  return false;
}

std::vector<Candidate> tuan_candidates(net::HttpClient& http, int major, std::string* error) {
  std::vector<Candidate> out;
  const std::string base = "https://mirrors.tuna.tsinghua.edu.cn/Adoptium/" +
                           std::to_string(major) + "/jre/x64/windows/";
  net::HttpResult result = http.get(base);
  if (!result.ok()) {
    if (error)
      *error = result.error.empty() ? str::fmt("HTTP %ld", result.status) : result.error;
    return out;
  }
  std::regex link_re("href=\"([^\"]+\\.zip)\"");
  std::vector<std::string> names;
  for (std::sregex_iterator it(result.body.begin(), result.body.end(), link_re), end; it != end;
       ++it) {
    names.push_back((*it)[1].str());
  }
  std::sort(names.begin(), names.end());
  if (names.empty()) {
    if (error) *error = "镜像目录中没有找到 JRE zip";
    return out;
  }
  // 目录里通常只有一个最新构建；取字典序最后一个
  out.push_back({base + names.back(), names.back(), "TUNA / Adoptium"});
  return out;
}

std::vector<Candidate> azul_candidates(net::HttpClient& http, int major, std::string* error) {
  std::vector<Candidate> out;
  const std::string url =
      "https://api.azul.com/metadata/v1/zulu/packages/?java_version=" + std::to_string(major) +
      "&os=windows&arch=x64&java_package_type=jre&archive_type=zip&latest=true&release_status=ga";
  net::HttpResult result = http.get(url);
  if (!result.ok()) {
    if (error)
      *error = result.error.empty() ? str::fmt("HTTP %ld", result.status) : result.error;
    return out;
  }
  std::string parse_error;
  Json j = Json::parse(result.body, &parse_error);
  if (!parse_error.empty() || j.size() == 0) {
    if (error) *error = "Azul 元数据解析失败";
    return out;
  }
  const Json& first = j[0];
  std::string download = first.get_str("download_url");
  std::string name = first.get_str("name");
  if (!download.empty()) {
    if (name.empty()) name = "zulu-jre-" + std::to_string(major) + ".zip";
    out.push_back({download, name, "Azul Zulu"});
  }
  return out;
}

std::vector<Candidate> microsoft_candidates(int major) {
  std::vector<Candidate> out;
  if (major != 17 && major != 21) return out;
  out.push_back({"https://aka.ms/download-jdk/microsoft-jdk-" + std::to_string(major) +
                     "-windows-x64.zip",
                 "microsoft-jdk-" + std::to_string(major) + ".zip", "Microsoft OpenJDK"});
  return out;
}

bool download_archive(const Paths& paths, const Candidate& candidate, net::HttpClient& http,
                      net::Downloader& downloader,
                      const std::function<void(const std::string&)>& status, std::string& zip_path,
                      std::string* error) {
  const std::string cache_dir = fs::join(paths.cache_dir(), "java");
  if (!fs::ensure_dir(cache_dir)) {
    if (error) *error = "无法创建缓存目录: " + cache_dir;
    return false;
  }
  zip_path = fs::join(cache_dir, candidate.file_name);
  fs::remove_file(zip_path);
  if (status) status("下载 " + candidate.source + " " + candidate.file_name);

  net::DownloadTask task;
  task.urls = {candidate.url};
  task.dest = zip_path;
  task.label = candidate.file_name;
  task.required = true;
  std::vector<net::DownloadTask> tasks;
  tasks.push_back(std::move(task));
  std::string download_error;
  if (!downloader.run(tasks, &download_error)) {
    if (error) *error = download_error;
    return false;
  }
  std::string data;
  if (!fs::read_file(zip_path, data) || !zip::is_zip(data)) {
    if (error) *error = "下载的文件不是有效的 ZIP";
    fs::remove_file(zip_path);
    return false;
  }
  log::info("Java 包已下载: " + zip_path + " (" + str::human_size(data.size()) + ")");
  return true;
}

bool extract_and_install(const Paths& paths, int major, const std::string& zip_path,
                         const std::function<void(const std::string&)>& status,
                         JavaInstallOutcome& out, std::string* error) {
  if (status) status("解压 Java 运行时…");
  std::string data;
  if (!fs::read_file(zip_path, data)) {
    if (error) *error = "无法读取 " + zip_path;
    return false;
  }
  const std::string java_root = paths.java_dir();
  fs::ensure_dir(java_root);
  const std::string temp = fs::join(java_root, ".tmp-" + std::to_string(major));
  fs::remove_dir_recursive(temp);
  if (!fs::ensure_dir(temp)) {
    if (error) *error = "无法创建解压目录: " + temp;
    return false;
  }

  std::string extract_error;
  size_t extracted = 0;
  if (!zip::extract_all(data, temp, &extract_error, {"META-INF/"}, &extracted)) {
    if (error) *error = "解压失败: " + extract_error;
    fs::remove_dir_recursive(temp);
    return false;
  }
  log::info(str::fmt("Java 解压完成：%zu 个文件", extracted));

  std::string java_exe;
  if (!find_java_binary(temp, java_exe)) {
    if (error) *error = "压缩包中没有找到 bin/java";
    fs::remove_dir_recursive(temp);
    return false;
  }

  const std::string home = fs::parent(fs::parent(java_exe));
  const std::string install = fs::join(java_root, "temurin-" + std::to_string(major));
  fs::remove_dir_recursive(install);
  bool moved = false;
  if (!fs::equal(home, temp)) moved = fs::rename_path(home, install);
  if (!moved) {
    if (!fs::copy_dir(home, install, nullptr, nullptr)) {
      if (error) *error = "移动 Java 文件失败";
      fs::remove_dir_recursive(temp);
      return false;
    }
  }
  fs::remove_dir_recursive(temp);

  const std::string bin = fs::join(fs::join(install, "bin"), "java.exe");
  if (!fs::is_file(bin)) {
    if (error) *error = "安装后缺少 bin/java.exe";
    return false;
  }

  // 试运行一次，确认能启动
  process::Result run = process::run(process::join_args({bin, "-version"}), "");
  int detected = 0;
  std::string version;
  if (!run.started || !parse_java_version(run.output, detected, version)) {
    if (error) *error = "安装后的 Java 无法运行";
    return false;
  }
  log::info(str::fmt("Java 安装成功：%s (%s)", bin.c_str(), version.c_str()));

  out.ok = true;
  out.major = major;
  out.install_dir = install;
  out.java_path = bin;
  return true;
}

}  // namespace
#endif

int recommended_java_major(int required_major) {
  if (required_major <= 8) return 8;
  if (required_major <= 11) return 11;
  if (required_major <= 17) return 17;
  if (required_major <= 21) return 21;
  return 25;
}

bool install_java(const Paths& paths, int required_major, net::HttpClient& http,
                  net::Downloader& downloader,
                  const std::function<void(const std::string&)>& status, JavaInstallOutcome& out) {
#ifdef _WIN32
  std::vector<int> majors = {recommended_java_major(required_major)};
  if (required_major >= 17 && required_major <= 21 && majors.front() != 21) majors.push_back(21);

  std::string last_error;
  for (int major : majors) {
    std::string source_error;
    std::vector<Candidate> candidates;
    auto tuna = tuan_candidates(http, major, &source_error);
    candidates.insert(candidates.end(), tuna.begin(), tuna.end());
    auto azul = azul_candidates(http, major, &source_error);
    candidates.insert(candidates.end(), azul.begin(), azul.end());
    auto ms = microsoft_candidates(major);
    candidates.insert(candidates.end(), ms.begin(), ms.end());
    if (candidates.empty()) {
      last_error = "无法获取 Java " + std::to_string(major) + " 下载地址: " + source_error;
      continue;
    }

    for (const Candidate& candidate : candidates) {
      if (status) status("准备安装 Java " + std::to_string(major) + "（" + candidate.source + "）");
      std::string zip_path;
      std::string download_error;
      if (!download_archive(paths, candidate, http, downloader, status, zip_path, &download_error)) {
        last_error = candidate.source + ": " + download_error;
        log::warn("Java 下载失败: " + last_error);
        continue;
      }
      std::string install_error;
      if (extract_and_install(paths, major, zip_path, status, out, &install_error)) return true;
      last_error = candidate.source + ": " + install_error;
      log::warn("Java 安装失败: " + last_error);
    }
  }
  out.ok = false;
  out.error = last_error.empty() ? "没有可用的 Java 下载源" : last_error;
  return false;
#else
  (void)paths;
  (void)required_major;
  (void)http;
  (void)downloader;
  (void)status;
  out.ok = false;
  out.error = "自动安装 Java 目前仅支持 Windows；Linux/macOS 请用系统包管理器安装 JDK";
  return false;
#endif
}

}  // namespace mct::launch
