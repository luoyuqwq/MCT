#include "util/process.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#include "util/str.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace mct::process {

#ifdef _WIN32

std::string quote_arg(const std::string& arg) {
  if (arg.empty()) return "\"\"";
  bool need_quote = arg.find_first_of(" \t\n\v\"") != std::string::npos;
  if (!need_quote) return arg;
  std::string out = "\"";
  size_t i = 0;
  while (true) {
    size_t backslashes = 0;
    while (i < arg.size() && arg[i] == '\\') {
      ++i;
      ++backslashes;
    }
    if (i == arg.size()) {
      out.append(backslashes * 2, '\\');
      break;
    }
    if (arg[i] == '"') {
      out.append(backslashes * 2 + 1, '\\');
      out += '"';
      ++i;
    } else {
      out.append(backslashes, '\\');
      out += arg[i];
      ++i;
    }
  }
  out += '"';
  return out;
}

std::string join_args(const std::vector<std::string>& args) {
  std::string line;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i) line += ' ';
    line += quote_arg(args[i]);
  }
  return line;
}

namespace {
std::string pipe_error(DWORD code) {
  return "错误码 " + std::to_string(code);
}

struct HandleGuard {
  HANDLE h = nullptr;
  explicit HandleGuard(HANDLE x = nullptr) : h(x) {}
  ~HandleGuard() {
    if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
  }
  HandleGuard(const HandleGuard&) = delete;
  HandleGuard& operator=(const HandleGuard&) = delete;
};
}  // namespace

Result run(const std::string& command_line, const std::string& workdir,
           const std::function<void(const std::string&)>& on_output) {
  Result result;
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
    result.error = "创建管道失败: " + pipe_error(GetLastError());
    return result;
  }
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
  HandleGuard read_guard(read_pipe);
  HandleGuard write_guard(write_pipe);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = write_pipe;
  si.hStdError = write_pipe;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi{};
  std::wstring cmd = str::widen(command_line);
  std::wstring cwd = str::widen(workdir);
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');

  BOOL ok = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                           nullptr, workdir.empty() ? nullptr : cwd.c_str(), &si, &pi);
  if (!ok) {
    result.error = "无法启动进程: " + pipe_error(GetLastError());
    return result;
  }
  result.started = true;
  HandleGuard process_guard(pi.hProcess);
  HandleGuard thread_guard(pi.hThread);
  CloseHandle(write_pipe);
  write_guard.h = nullptr;

  char buffer[4096];
  DWORD got = 0;
  while (ReadFile(read_pipe, buffer, sizeof(buffer) - 1, &got, nullptr) && got > 0) {
    buffer[got] = '\0';
    result.output.append(buffer, got);
    if (on_output) on_output(std::string(buffer, got));
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  result.exit_code = static_cast<int>(code);
  return result;
}

bool spawn_and_wait(const std::string& command_line, const std::string& workdir, int* exit_code,
                    std::string* error) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring cmd = str::widen(command_line);
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');
  std::wstring cwd = str::widen(workdir);
  if (!CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, 0, nullptr,
                      workdir.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
    if (error) *error = "无法启动游戏进程: " + pipe_error(GetLastError());
    return false;
  }
  HandleGuard process_guard(pi.hProcess);
  HandleGuard thread_guard(pi.hThread);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  if (exit_code) *exit_code = static_cast<int>(code);
  return true;
}

bool open_url(const std::string& url) {
  HINSTANCE result =
      ShellExecuteW(nullptr, L"open", str::widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<intptr_t>(result) > 32;
}

#else

std::string quote_arg(const std::string& arg) {
  if (arg.empty()) return "''";
  bool simple = arg.find_first_of(" \t\n'\"\\$`") == std::string::npos;
  if (simple) return arg;
  std::string out = "'";
  for (char c : arg) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}

std::string join_args(const std::vector<std::string>& args) {
  std::string line;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i) line += ' ';
    line += quote_arg(args[i]);
  }
  return line;
}

Result run(const std::string& command_line, const std::string& workdir,
           const std::function<void(const std::string&)>& on_output) {
  Result result;
  std::string cmd = command_line;
  if (!workdir.empty()) cmd = "cd " + quote_arg(workdir) + " && " + cmd;
  cmd += " 2>&1";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    result.error = "无法启动进程";
    return result;
  }
  result.started = true;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe)) {
    result.output += buffer;
    if (on_output) on_output(std::string(buffer));
  }
  int status = pclose(pipe);
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

bool spawn_and_wait(const std::string& command_line, const std::string& workdir, int* exit_code,
                    std::string* error) {
  std::string cmd = command_line;
  if (!workdir.empty()) cmd = "cd " + quote_arg(workdir) + " && " + cmd;
  int status = std::system(cmd.c_str());
  if (exit_code) *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  if (status == -1) {
    if (error) *error = "无法启动进程";
    return false;
  }
  return true;
}

bool open_url(const std::string& url) {
  std::string opener = "xdg-open";
#ifdef __APPLE__
  opener = "open";
#endif
  std::string cmd = opener + " " + quote_arg(url) + " >/dev/null 2>&1 &";
  return std::system(cmd.c_str()) == 0;
}

#endif

}  // namespace mct::process
