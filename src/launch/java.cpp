#include "launch/java.hpp"

#include <algorithm>
#include <map>
#include <regex>

#include "util/fs.hpp"
#include "util/log.hpp"
#include "util/process.hpp"
#include "util/str.hpp"

namespace mct::launch {

namespace {

#ifdef _WIN32
const char* kJavaExe = "javaw.exe";
const char* kJavaConsoleExe = "java.exe";
#else
const char* kJavaExe = "java";
const char* kJavaConsoleExe = "java";
#endif

std::string java_in_dir(const std::string& dir, const char* exe) {
  return fs::join(dir, exe);
}

void add_candidate(std::vector<std::string>& list, const std::string& path) {
  if (path.empty()) return;
  std::string normalized = fs::normalize(path);
  for (const auto& existing : list) {
    if (str::iequals(existing, normalized)) return;
  }
  if (fs::is_file(normalized)) list.push_back(normalized);
}

void add_java_home(std::vector<std::string>& list, const std::string& home) {
  if (home.empty()) return;
  add_candidate(list, java_in_dir(fs::join(home, "bin"), kJavaConsoleExe));
  add_candidate(list, java_in_dir(fs::join(home, "bin"), kJavaExe));
}

void scan_directory(std::vector<std::string>& list, const std::string& dir) {
  if (dir.empty() || !fs::is_dir(dir)) return;
  for (const auto& entry : fs::list_dir(dir)) {
    if (!entry.is_dir) continue;
    add_java_home(list, fs::join(dir, entry.name));
  }
}

#ifdef _WIN32
void scan_windows_javas(std::vector<std::string>& list) {
  std::vector<std::string> roots = {
      fs::join(fs::get_env("ProgramFiles"), "Java"),
      fs::join(fs::get_env("ProgramFiles"), "Eclipse Adoptium"),
      fs::join(fs::get_env("ProgramFiles"), "Microsoft"),
      fs::join(fs::get_env("ProgramFiles"), "Zulu"),
      fs::join(fs::get_env("ProgramFiles"), "BellSoft"),
      fs::join(fs::get_env("ProgramFiles"), "Amazon Corretto"),
      fs::join(fs::get_env("ProgramFiles"), "Semeru"),
      fs::join(fs::get_env("LOCALAPPDATA"), "Programs", "Eclipse Adoptium"),
  };
  for (const auto& root : roots) scan_directory(list, root);
  // Microsoft Store 版本的 Java
  std::string local = fs::get_env("LOCALAPPDATA");
  if (!local.empty()) {
    for (const auto& name : fs::list_dir(fs::join(local, "Packages"))) {
      if (!str::contains(str::lower(name.name), "java")) continue;
      std::string home = fs::join(fs::join(local, "Packages", name.name),
                                 fs::join("LocalCache", "Local", "Java"));
      scan_directory(list, home);
    }
  }
}
#endif

}  // namespace

bool parse_java_version(const std::string& output, int& major, std::string& version) {
  std::regex re("version \"([^\"]+)\"");
  std::smatch match;
  if (!std::regex_search(output, match, re)) return false;
  version = match[1].str();
  std::vector<std::string> parts = str::split(version, '.');
  if (parts.empty()) return false;
  try {
    if (parts[0] == "1" && parts.size() >= 2) {
      major = std::stoi(parts[1]);
    } else {
      std::string digits;
      for (char c : parts[0]) {
        if (c >= '0' && c <= '9')
          digits += c;
        else
          break;
      }
      major = digits.empty() ? 0 : std::stoi(digits);
    }
  } catch (const std::exception&) {
    return false;
  }
  return major > 0;
}

std::vector<JavaInfo> find_all_java(const Paths& paths, const Config& config) {
  std::vector<std::string> candidates;
  if (!config.java_path.empty()) {
    if (fs::is_dir(config.java_path)) {
      add_java_home(candidates, config.java_path);
    } else {
      add_candidate(candidates, config.java_path);
    }
  }
  add_java_home(candidates, fs::get_env("JAVA_HOME"));
  scan_directory(candidates, paths.java_dir());
  for (const auto& dir : fs::path_dirs()) {
    add_candidate(candidates, fs::join(dir, kJavaConsoleExe));
    add_candidate(candidates, fs::join(dir, kJavaExe));
  }
#ifdef _WIN32
  scan_windows_javas(candidates);
#else
  scan_directory(candidates, "/usr/lib/jvm");
  scan_directory(candidates, "/Library/Java/JavaVirtualMachines");
#endif

  std::vector<JavaInfo> result;
  for (const auto& path : candidates) {
    process::Result run = process::run(process::join_args({path, "-version"}), "");
    if (!run.started) continue;
    JavaInfo info;
    info.path = path;
    std::string combined = run.output;
    if (!parse_java_version(combined, info.major, info.version)) continue;
    bool duplicate = false;
    for (const auto& existing : result) {
      if (str::iequals(existing.path, info.path)) duplicate = true;
    }
    if (duplicate) continue;
    result.push_back(std::move(info));
  }
  std::sort(result.begin(), result.end(), [](const JavaInfo& a, const JavaInfo& b) {
    if (a.major != b.major) return a.major > b.major;
    return a.path < b.path;
  });
  return result;
}

bool find_java(const Paths& paths, const Config& config, int required_major, JavaInfo& out,
               std::string* error) {
  std::vector<JavaInfo> all = find_all_java(paths, config);
  if (all.empty()) {
    if (error)
      *error =
          "没有找到可用的 Java。请安装 Java 21（推荐 Temurin / Zulu）或在设置中指定 java.exe 路径";
    return false;
  }
  const JavaInfo* best = nullptr;
  for (const auto& java : all) {
    if (java.major >= required_major) {
      if (!best || java.major < best->major) best = &java;
    }
  }
  if (!best) {
    best = &all.front();
    log::warn(str::fmt("Java 版本偏低：需要 %d，找到最高 %d", required_major, best->major));
  }
  out = *best;
  return true;
}

}  // namespace mct::launch
