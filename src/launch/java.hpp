// Java 探测
#pragma once

#include <string>
#include <vector>

#include "core/config.hpp"

namespace mct::launch {

struct JavaInfo {
  std::string path;
  int major = 0;
  std::string version;
  bool valid() const { return !path.empty() && major > 0; }
};

bool parse_java_version(const std::string& output, int& major, std::string& version);
std::vector<JavaInfo> find_all_java(const Paths& paths, const Config& config);
bool find_java(const Paths& paths, const Config& config, int required_major, JavaInfo& out,
               std::string* error);

}  // namespace mct::launch
