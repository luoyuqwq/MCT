// 终端界面组件：菜单、输入框、确认框、提示、下载进度
#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "net/downloader.hpp"
#include "ui/term.hpp"

namespace mct::ui {

struct Theme {
  std::string reset = "\x1b[0m";
  std::string bold = "\x1b[1m";
  std::string dim = "\x1b[38;5;244m";
  std::string text = "\x1b[38;5;252m";
  std::string accent = "\x1b[38;5;81m";
  std::string accent2 = "\x1b[38;5;213m";
  std::string good = "\x1b[38;5;114m";
  std::string warn = "\x1b[38;5;222m";
  std::string bad = "\x1b[38;5;203m";
  std::string sel = "\x1b[48;5;236m";
  std::string border = "\x1b[38;5;240m";
};

const Theme& theme();

struct MenuItem {
  std::string label;
  std::string hint;
  std::string badge;
  bool disabled = false;
};

struct MenuOptions {
  std::string title;
  std::string subtitle;
  std::string breadcrumb;
  std::vector<std::string> header_lines;
  std::vector<std::string> footer_lines;
  bool allow_quit = true;
  bool show_back = true;
  std::string extra_help;
};

// 返回选中索引；Esc / Q 返回 -1
int menu(Terminal& term, const MenuOptions& options, const std::vector<MenuItem>& items,
         int initial = 0);

struct PromptOptions {
  std::string title;
  std::string label;
  std::string hint;
  std::string initial;
  bool password = false;
  size_t max_length = 512;
};

bool prompt(Terminal& term, const PromptOptions& options, std::string& value);

bool confirm(Terminal& term, const std::string& title, const std::string& message,
             bool default_yes = true);

void message(Terminal& term, const std::string& title, const std::vector<std::string>& lines);
void error_box(Terminal& term, const std::string& title, const std::string& text);
void wait_enter(Terminal& term);
void draw_waiting(Terminal& term, const std::string& title, const std::string& text);

class ProgressView {
 public:
  ProgressView(Terminal& term, std::string title);
  void set_status(const std::string& status);
  void set_cancel_callback(std::function<void()> callback);
  void update(const net::DownloadStats& stats);
  bool cancelled() const { return cancelled_; }
  void finish(bool ok, const std::string& text);

 private:
  void render_locked(const net::DownloadStats& stats, bool finished);

  Terminal& term_;
  std::string title_;
  std::string status_;
  std::string final_text_;
  std::mutex mutex_;
  bool cancelled_ = false;
  bool finished_ = false;
  bool cancel_requested_ = false;
  std::function<void()> cancel_callback_;
  std::chrono::steady_clock::time_point last_draw_{};
};

}  // namespace mct::ui
