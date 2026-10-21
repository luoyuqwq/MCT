// 终端控制：ANSI 渲染、原始按键、窗口尺寸
#pragma once

#include <string>

namespace mct::ui {

struct KeyEvent {
  enum class Code {
    None,
    Char,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Esc,
    Backspace,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    Tab,
    BackTab,
    CtrlC,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12
  };
  Code code = Code::None;
  std::string text;
};

class Terminal {
 public:
  Terminal();
  ~Terminal();

  bool init();
  void shutdown();
  bool active() const { return active_; }
  bool is_tty() const { return tty_; }

  int width() const;
  int height() const;

  void enter_alt();
  void leave_alt();
  void hide_cursor(bool hide);
  void clear();

  void write(const std::string& text);
  void flush();

  KeyEvent read_key();
  // 等待最多 timeout_ms，返回 false 表示超时
  bool poll_key(int timeout_ms, KeyEvent& out);
  // 输入流已结束（非交互模式，例如管道）
  bool eof() const { return eof_; }

 private:
  bool init_windows();
  bool init_posix();
  void restore();

  bool active_ = false;
  bool tty_ = false;
  bool alt_ = false;
  void* out_handle_ = nullptr;
  void* in_handle_ = nullptr;
  unsigned long old_out_mode_ = 0;
  unsigned long old_in_mode_ = 0;
  unsigned int old_cp_ = 0;
  bool mode_saved_ = false;
  bool eof_ = false;
  wchar_t pending_high_ = 0;
};

}  // namespace mct::ui
