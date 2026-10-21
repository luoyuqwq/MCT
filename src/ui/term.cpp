#include "ui/term.hpp"

#include <cstdio>
#include <cstring>

#include "util/str.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace mct::ui {

namespace {

const char* kAltEnter = "\x1b[?1049h";
const char* kAltLeave = "\x1b[?1049l";
const char* kHideCursor = "\x1b[?25l";
const char* kShowCursor = "\x1b[?25h";
const char* kClear = "\x1b[2J\x1b[H";

#ifdef _WIN32
KeyEvent::Code map_vk(WORD vk) {
  switch (vk) {
    case VK_UP: return KeyEvent::Code::Up;
    case VK_DOWN: return KeyEvent::Code::Down;
    case VK_LEFT: return KeyEvent::Code::Left;
    case VK_RIGHT: return KeyEvent::Code::Right;
    case VK_RETURN: return KeyEvent::Code::Enter;
    case VK_ESCAPE: return KeyEvent::Code::Esc;
    case VK_BACK: return KeyEvent::Code::Backspace;
    case VK_DELETE: return KeyEvent::Code::Delete;
    case VK_HOME: return KeyEvent::Code::Home;
    case VK_END: return KeyEvent::Code::End;
    case VK_PRIOR: return KeyEvent::Code::PageUp;
    case VK_NEXT: return KeyEvent::Code::PageDown;
    case VK_TAB: return KeyEvent::Code::Tab;
    case VK_F1: return KeyEvent::Code::F1;
    case VK_F2: return KeyEvent::Code::F2;
    case VK_F3: return KeyEvent::Code::F3;
    case VK_F4: return KeyEvent::Code::F4;
    case VK_F5: return KeyEvent::Code::F5;
    case VK_F6: return KeyEvent::Code::F6;
    case VK_F7: return KeyEvent::Code::F7;
    case VK_F8: return KeyEvent::Code::F8;
    case VK_F9: return KeyEvent::Code::F9;
    case VK_F10: return KeyEvent::Code::F10;
    case VK_F11: return KeyEvent::Code::F11;
    case VK_F12: return KeyEvent::Code::F12;
    default: return KeyEvent::Code::None;
  }
}
#endif

}  // namespace

Terminal::Terminal() = default;

Terminal::~Terminal() {
  if (active_) shutdown();
}

bool Terminal::init() {
#ifdef _WIN32
  return init_windows();
#else
  return init_posix();
#endif
}

#ifdef _WIN32

bool Terminal::init_windows() {
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  if (out == INVALID_HANDLE_VALUE || in == INVALID_HANDLE_VALUE) {
    active_ = true;
    tty_ = false;
    return true;
  }
  DWORD out_mode = 0;
  DWORD in_mode = 0;
  tty_ = GetConsoleMode(out, &out_mode) != 0 && GetConsoleMode(in, &in_mode) != 0;
  if (!tty_) {
    active_ = true;
    return true;
  }
  old_out_mode_ = out_mode;
  old_in_mode_ = in_mode;
  old_cp_ = GetConsoleOutputCP();
  mode_saved_ = true;
  out_handle_ = out;
  in_handle_ = in;

  SetConsoleOutputCP(CP_UTF8);
  SetConsoleMode(out, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
  DWORD new_in = (in_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_MOUSE_INPUT |
                              ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE)) |
                 ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT;
  SetConsoleMode(in, new_in);
  active_ = true;
  return true;
}

void Terminal::shutdown() {
  if (!active_) return;
  leave_alt();
  hide_cursor(false);
  if (mode_saved_) {
    SetConsoleMode(static_cast<HANDLE>(out_handle_), old_out_mode_);
    SetConsoleMode(static_cast<HANDLE>(in_handle_), old_in_mode_);
    if (old_cp_ != 0) SetConsoleOutputCP(old_cp_);
    mode_saved_ = false;
  }
  active_ = false;
}

int Terminal::width() const {
  if (!tty_) return 100;
  CONSOLE_SCREEN_BUFFER_INFO info;
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!GetConsoleScreenBufferInfo(out, &info)) return 100;
  int w = info.srWindow.Right - info.srWindow.Left + 1;
  return w > 20 ? w : 100;
}

int Terminal::height() const {
  if (!tty_) return 40;
  CONSOLE_SCREEN_BUFFER_INFO info;
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!GetConsoleScreenBufferInfo(out, &info)) return 40;
  int h = info.srWindow.Bottom - info.srWindow.Top + 1;
  return h > 10 ? h : 40;
}

KeyEvent Terminal::read_key() {
  if (!tty_) {
    KeyEvent event;
    int c = std::getchar();
    if (c == EOF) {
      eof_ = true;
      return event;
    }
    if (c == '\r' || c == '\n')
      event.code = KeyEvent::Code::Enter;
    else if (c == 27)
      event.code = KeyEvent::Code::Esc;
    else {
      event.code = KeyEvent::Code::Char;
      event.text = static_cast<char>(c);
    }
    return event;
  }
  for (;;) {
    DWORD available = 0;
    if (!GetNumberOfConsoleInputEvents(static_cast<HANDLE>(in_handle_), &available) || available == 0) {
      Sleep(8);
      continue;
    }
    INPUT_RECORD records[32];
    DWORD got = 0;
    if (!ReadConsoleInputW(static_cast<HANDLE>(in_handle_), records, 32, &got)) {
      Sleep(8);
      continue;
    }
    for (DWORD i = 0; i < got; ++i) {
      if (records[i].EventType != KEY_EVENT) continue;
      const KEY_EVENT_RECORD& k = records[i].Event.KeyEvent;
      if (!k.bKeyDown) continue;
      if (k.uChar.UnicodeChar == 3) {
        KeyEvent event;
        event.code = KeyEvent::Code::CtrlC;
        return event;
      }
      KeyEvent::Code code = map_vk(k.wVirtualKeyCode);
      if (code != KeyEvent::Code::None) {
        KeyEvent event;
        event.code = code;
        return event;
      }
      wchar_t ch = k.uChar.UnicodeChar;
      if (ch == 0) continue;
      if (ch >= 0xD800 && ch <= 0xDBFF) {
        pending_high_ = ch;
        continue;
      }
      if (ch >= 0xDC00 && ch <= 0xDFFF && pending_high_) {
        wchar_t pair[3] = {pending_high_, ch, 0};
        pending_high_ = 0;
        KeyEvent event;
        event.code = KeyEvent::Code::Char;
        event.text = str::narrow(std::wstring(pair));
        return event;
      }
      pending_high_ = 0;
      if (ch < 0x20) {
        if (ch >= 1 && ch <= 26) {
          KeyEvent event;
          event.code = KeyEvent::Code::Char;
          event.text = std::string(1, static_cast<char>(ch));
          return event;
        }
        continue;
      }
      KeyEvent event;
      event.code = KeyEvent::Code::Char;
      event.text = str::narrow(std::wstring(1, ch));
      return event;
    }
  }
}

bool Terminal::poll_key(int timeout_ms, KeyEvent& out) {
  if (!tty_) {
    if (eof_) return false;
    out = read_key();
    return out.code != KeyEvent::Code::None;
  }
  DWORD result = WaitForSingleObject(static_cast<HANDLE>(in_handle_), static_cast<DWORD>(timeout_ms));
  if (result != WAIT_OBJECT_0) return false;
  out = read_key();
  return out.code != KeyEvent::Code::None;
}

#else  // POSIX

namespace {
struct termios g_saved_termios;
bool g_saved_valid = false;
int read_byte_timeout(int timeout_ms) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(STDIN_FILENO, &set);
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  int ready = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &tv);
  if (ready <= 0) return -1;
  unsigned char c = 0;
  if (read(STDIN_FILENO, &c, 1) != 1) return -1;
  return c;
}
}  // namespace

bool Terminal::init_posix() {
  if (!isatty(STDIN_FILENO)) {
    active_ = true;
    tty_ = false;
    return true;
  }
  struct termios raw;
  if (tcgetattr(STDIN_FILENO, &raw) != 0) {
    active_ = true;
    tty_ = false;
    return true;
  }
  if (!g_saved_valid) {
    g_saved_termios = raw;
    g_saved_valid = true;
  }
  raw.c_lflag &= ~(ICANON | ECHO | ISIG);
  raw.c_iflag &= ~(IXON | ICRNL);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  tty_ = true;
  active_ = true;
  return true;
}

void Terminal::shutdown() {
  if (!active_) return;
  leave_alt();
  hide_cursor(false);
  if (tty_ && g_saved_valid) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
    g_saved_valid = false;
  }
  active_ = false;
}

int Terminal::width() const {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 20) return ws.ws_col;
  return 100;
}

int Terminal::height() const {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 10) return ws.ws_row;
  return 40;
}

bool Terminal::poll_key(int timeout_ms, KeyEvent& out) {
  int c = read_byte_timeout(timeout_ms);
  if (c < 0) return false;
  out = KeyEvent();
  if (c == 27) {
    int c2 = read_byte_timeout(20);
    if (c2 < 0) {
      out.code = KeyEvent::Code::Esc;
      return true;
    }
    if (c2 == '[' || c2 == 'O') {
      int c3 = read_byte_timeout(20);
      switch (c3) {
        case 'A': out.code = KeyEvent::Code::Up; break;
        case 'B': out.code = KeyEvent::Code::Down; break;
        case 'C': out.code = KeyEvent::Code::Right; break;
        case 'D': out.code = KeyEvent::Code::Left; break;
        case 'H': out.code = KeyEvent::Code::Home; break;
        case 'F': out.code = KeyEvent::Code::End; break;
        case 'Z': out.code = KeyEvent::Code::BackTab; break;
        case '1': read_byte_timeout(20); out.code = KeyEvent::Code::Home; break;
        case '3': read_byte_timeout(20); out.code = KeyEvent::Code::Delete; break;
        case '4': read_byte_timeout(20); out.code = KeyEvent::Code::End; break;
        case '5': read_byte_timeout(20); out.code = KeyEvent::Code::PageUp; break;
        case '6': read_byte_timeout(20); out.code = KeyEvent::Code::PageDown; break;
        default: out.code = KeyEvent::Code::None; break;
      }
      return out.code != KeyEvent::Code::None;
    }
    out.code = KeyEvent::Code::Esc;
    return true;
  }
  if (c == '\r' || c == '\n') {
    out.code = KeyEvent::Code::Enter;
    return true;
  }
  if (c == 127 || c == 8) {
    out.code = KeyEvent::Code::Backspace;
    return true;
  }
  if (c == 3) {
    out.code = KeyEvent::Code::CtrlC;
    return true;
  }
  if (c == 9) {
    out.code = KeyEvent::Code::Tab;
    return true;
  }
  if (c >= 0x20) {
    std::string bytes;
    bytes += static_cast<char>(c);
    // 读取后续 UTF-8 字节
    int extra = 0;
    if ((c & 0xE0) == 0xC0) extra = 1;
    else if ((c & 0xF0) == 0xE0) extra = 2;
    else if ((c & 0xF8) == 0xF0) extra = 3;
    for (int i = 0; i < extra; ++i) {
      int next = read_byte_timeout(30);
      if (next < 0) break;
      bytes += static_cast<char>(next);
    }
    out.code = KeyEvent::Code::Char;
    out.text = bytes;
    return true;
  }
  out.code = KeyEvent::Code::None;
  return false;
}

KeyEvent Terminal::read_key() {
  KeyEvent event;
  while (!poll_key(1000, event)) {
  }
  return event;
}

#endif

void Terminal::enter_alt() {
  if (alt_ || !tty_) return;
  write(kAltEnter);
  write(kClear);
  hide_cursor(true);
  alt_ = true;
}

void Terminal::leave_alt() {
  if (!alt_) return;
  hide_cursor(false);
  write(kAltLeave);
  alt_ = false;
}

void Terminal::hide_cursor(bool hide) {
  if (!tty_) return;
  write(hide ? kHideCursor : kShowCursor);
  flush();
}

void Terminal::clear() {
  if (!tty_) return;
  write(kClear);
}

void Terminal::write(const std::string& text) {
  if (text.empty()) return;
  std::fwrite(text.data(), 1, text.size(), stdout);
}

void Terminal::flush() { std::fflush(stdout); }

}  // namespace mct::ui
