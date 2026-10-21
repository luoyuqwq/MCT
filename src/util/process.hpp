// 子进程：捕获输出运行命令、启动游戏进程
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace mct::process {

struct Result {
  bool started = false;
  int exit_code = -1;
  std::string output;
  std::string error;
};

// 运行命令并捕获输出（stdout+stderr 合并）
Result run(const std::string& command_line, const std::string& workdir,
           const std::function<void(const std::string&)>& on_output = nullptr);

// 启动进程并等待退出（继承当前控制台，用于启动游戏）
bool spawn_and_wait(const std::string& command_line, const std::string& workdir, int* exit_code,
                    std::string* error);

std::string quote_arg(const std::string& arg);
std::string join_args(const std::vector<std::string>& args);

// 用系统默认浏览器打开 URL（Windows: ShellExecute，Linux: xdg-open，macOS: open）
bool open_url(const std::string& url);

}  // namespace mct::process
