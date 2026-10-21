// 启动游戏：Java 选择、classpath、JVM/游戏参数、子进程
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "auth/accounts.hpp"
#include "core/config.hpp"

namespace mct::launch {

struct LaunchResult {
  bool ok = false;
  std::string error;
  int exit_code = 0;
};

class GameLauncher {
 public:
  GameLauncher(const Paths& paths, const Config& config);

  bool build_command(const std::string& instance, const auth::Account& account,
                     std::vector<std::string>& argv, std::string* error,
                     const std::function<void(const std::string&)>& status = nullptr);

  LaunchResult run(const std::string& instance, const auth::Account& account,
                   const std::function<void(const std::string&)>& status);

 private:
  std::string resolve_jar(const std::string& instance) const;

  const Paths& paths_;
  Config config_;
};

}  // namespace mct::launch
