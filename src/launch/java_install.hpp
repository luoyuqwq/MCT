// 自动下载并安装适合版本的 Java（Windows：Temurin JRE，多镜像回退）
#pragma once

#include <functional>
#include <string>

#include "launch/java.hpp"

namespace mct {
struct Paths;
}

namespace mct::net {
class HttpClient;
class Downloader;
}  // namespace mct::net

namespace mct::launch {

struct JavaInstallOutcome {
  bool ok = false;
  std::string error;
  int major = 0;
  std::string install_dir;
  std::string java_path;
};

// 根据版本要求的 Java 主版本，推荐实际安装的主版本（8 / 11 / 17 / 21）
int recommended_java_major(int required_major);

// 下载并解压到 <home>/java/temurin-<major>，返回 bin/java 路径
bool install_java(const Paths& paths, int required_major, net::HttpClient& http,
                  net::Downloader& downloader,
                  const std::function<void(const std::string&)>& status, JavaInstallOutcome& out);

}  // namespace mct::launch
