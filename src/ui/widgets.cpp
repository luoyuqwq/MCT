#include "ui/widgets.hpp"

#include <algorithm>
#include <cstdio>

#include "util/clipboard.hpp"
#include "util/str.hpp"
#include "version.hpp"

namespace mct::ui {

namespace {

const Theme& t = *new Theme();

std::string repeat_text(const std::string& unit, int count) {
  std::string out;
  out.reserve(unit.size() * static_cast<size_t>(std::max(0, count)));
  for (int i = 0; i < count; ++i) out += unit;
  return out;
}

void draw(Terminal& term, const std::vector<std::string>& lines) {
  std::string out;
  out.reserve(4096);
  out += "\x1b[H";
  for (const std::string& line : lines) {
    out += "\x1b[2K";
    out += line;
    out += "\r\n";
  }
  out += "\x1b[J";
  term.write(out);
  term.flush();
}

std::string header_line(const std::string& title, const std::string& subtitle, int width) {
  std::string left = "  " + std::string(MCT_NAME);
  std::string mid = subtitle.empty() ? std::string() : "  " + subtitle;
  std::string right =
      std::string("v") + MCT_VERSION + "  ·  Designed By " + MCT_DESIGNER + "  ";
  int used = str::display_width(left) + str::display_width(mid) + str::display_width(right);
  if (used > width) {
    right = std::string("v") + MCT_VERSION + "  ";
    used = str::display_width(left) + str::display_width(mid) + str::display_width(right);
    if (used > width) mid.clear();
  }
  std::string gap = used < width ? std::string(static_cast<size_t>(width - used), ' ') : " ";
  std::string line = t.accent + t.bold + left + t.reset + t.dim + mid + t.reset + gap + t.dim +
                     right + t.reset;
  (void)title;
  return line;
}

std::string separator(int width) {
  return "  " + t.border + repeat_text("─", std::max(4, width - 4)) + t.reset;
}

int next_enabled(const std::vector<MenuItem>& items, int from, int dir) {
  int count = static_cast<int>(items.size());
  if (count == 0) return -1;
  for (int step = 0; step < count; ++step) {
    int index = (from + dir * step) % count;
    if (index < 0) index += count;
    if (!items[index].disabled) return index;
  }
  return -1;
}

}  // namespace

const Theme& theme() { return t; }

int menu(Terminal& term, const MenuOptions& options, const std::vector<MenuItem>& items,
         int initial) {
  if (items.empty()) return -1;
  int selected = std::min(std::max(0, initial), static_cast<int>(items.size()) - 1);
  if (items[selected].disabled) selected = next_enabled(items, selected, 1);
  if (selected < 0) selected = 0;
  int last_width = -1;
  int last_height = -1;
  std::vector<std::string> cached;

  auto build = [&](int width, int height) {
    cached.clear();
    const int inner = std::max(20, width - 4);
    cached.push_back(header_line(options.title, options.subtitle, width));
    if (!options.breadcrumb.empty())
      cached.push_back("  " + t.dim + options.breadcrumb + t.reset);
    cached.push_back(separator(width));
    if (!options.title.empty()) {
      cached.push_back("");
      cached.push_back("  " + t.accent2 + t.bold + options.title + t.reset);
    }
    for (const auto& line : options.header_lines) cached.push_back("  " + line);
    cached.push_back("");

    int fixed = static_cast<int>(cached.size()) + 4 + static_cast<int>(options.footer_lines.size());
    int available = std::max(3, height - fixed);
    int count = static_cast<int>(items.size());
    int start = 0;
    if (count > available) {
      start = selected - available / 2;
      start = std::max(0, std::min(start, count - available));
    }
    int end = std::min(count, start + available);
    if (start > 0)
      cached.push_back("  " + t.dim + str::fmt("↑ 上面还有 %d 项", start) + t.reset);
    for (int i = start; i < end; ++i) {
      const MenuItem& item = items[i];
      bool is_selected = i == selected;
      std::string badge;
      if (!item.badge.empty()) badge = item.badge + " ";
      std::string label = badge + item.label;
      std::string hint = item.hint;
      int label_width = str::display_width(label);
      int hint_width = str::display_width(hint);
      std::string line;
      if (is_selected) {
        line = t.accent + t.bold + "  ❯ " + label + t.reset;
      } else {
        line = std::string("    ") + (item.disabled ? t.dim : t.text) + label + t.reset;
      }
      int used = 4 + label_width;
      if (!hint.empty() && used + hint_width + 2 <= inner) {
        line += std::string(static_cast<size_t>(inner - used - hint_width), ' ');
        line += (is_selected ? t.accent2 : t.dim) + hint + t.reset;
      }
      (void)is_selected;
      if (is_selected) line = t.sel + line;
      if (item.disabled) line = t.dim + "    " + label + t.reset;
      cached.push_back(line);
    }
    if (end < count)
      cached.push_back("  " + t.dim + str::fmt("↓ 下面还有 %d 项", count - end) + t.reset);

    cached.push_back("");
    for (const auto& line : options.footer_lines) cached.push_back("  " + line);
    cached.push_back(separator(width));
    std::string help = "  ↑↓ 选择   Enter 确认";
    if (options.show_back) help += "   Esc 返回";
    if (options.allow_quit) help += options.show_back ? "   Q 返回" : "   Q 退出";
    if (!options.extra_help.empty()) help += "   " + options.extra_help;
    cached.push_back(t.dim + help + t.reset);
  };

  for (;;) {
    int width = std::min(term.width(), 110);
    int height = term.height();
    build(width, height);
    draw(term, cached);
    last_width = width;
    last_height = height;

    KeyEvent key;
    bool has_key = term.poll_key(250, key);
    if (!has_key) {
      if (term.eof()) return -1;
      if (term.width() != last_width || term.height() != last_height) continue;
      continue;
    }
    switch (key.code) {
      case KeyEvent::Code::Up:
        selected = next_enabled(items, selected - 1, -1);
        break;
      case KeyEvent::Code::Down:
        selected = next_enabled(items, selected + 1, 1);
        break;
      case KeyEvent::Code::Home:
        selected = next_enabled(items, 0, 1);
        break;
      case KeyEvent::Code::End:
        selected = next_enabled(items, static_cast<int>(items.size()) - 1, -1);
        break;
      case KeyEvent::Code::PageUp:
        selected = next_enabled(items, std::max(0, selected - 8), -1);
        break;
      case KeyEvent::Code::PageDown:
        selected = next_enabled(items, std::min(static_cast<int>(items.size()) - 1, selected + 8),
                                1);
        break;
      case KeyEvent::Code::Enter:
        if (selected >= 0 && !items[selected].disabled) return selected;
        break;
      case KeyEvent::Code::Esc:
        return -1;
      case KeyEvent::Code::CtrlC:
        return -1;
      case KeyEvent::Code::Char: {
        if (key.text == "q" || key.text == "Q") {
          if (options.allow_quit) return -1;
        } else if ((key.text == "j" || key.text == "J")) {
          selected = next_enabled(items, selected + 1, 1);
        } else if ((key.text == "k" || key.text == "K")) {
          selected = next_enabled(items, selected - 1, -1);
        } else if (key.text.size() == 1 && key.text[0] >= '1' && key.text[0] <= '9') {
          int index = key.text[0] - '1';
          if (index < static_cast<int>(items.size()) && !items[index].disabled) {
            selected = index;
            return selected;
          }
        }
        break;
      }
      default:
        break;
    }
  }
}

bool prompt(Terminal& term, const PromptOptions& options, std::string& value) {
  std::string buffer = options.initial;
  size_t cursor = buffer.size();
  if (cursor > options.max_length) {
    buffer.resize(options.max_length);
    cursor = buffer.size();
  }
  for (;;) {
    int width = std::min(term.width(), 110);
    int inner = std::max(20, width - 6);
    std::vector<std::string> lines;
    lines.push_back(header_line("", options.title, width));
    lines.push_back(separator(width));
    lines.push_back("");
    if (!options.label.empty())
      lines.push_back("  " + t.text + t.bold + options.label + t.reset);
    std::string shown;
    if (options.password) {
      size_t count = str::utf8_length(buffer);
      shown = std::string(count, '*');
      lines.push_back("  " + t.accent + "❯ " + t.reset + shown + t.accent + "▏" + t.reset);
    } else {
      size_t pos = 0;
      std::string before;
      while (pos < buffer.size()) {
        size_t start = pos;
        str::decode_utf8(buffer, pos);
        if (start >= cursor) break;
        before.append(buffer, start, pos - start);
      }
      std::string after = buffer.substr(std::min(cursor, buffer.size()));
      if (str::display_width(before) > inner) before = "…" + before.substr(before.size() / 2);
      lines.push_back("  " + t.accent + "❯ " + t.reset + t.text + before + t.reset + t.accent +
                      "▏" + t.reset + t.text + after + t.reset);
    }
    lines.push_back("");
    if (!options.hint.empty()) {
      for (const auto& line : str::wrap(options.hint, inner)) {
        lines.push_back("  " + t.dim + line + t.reset);
      }
    }
    lines.push_back("");
    lines.push_back(separator(width));
    lines.push_back(t.dim + "  Enter 确认   Esc 取消   ←→ 移动光标   Ctrl+U 清空" + t.reset);
    draw(term, lines);

    KeyEvent key = term.read_key();
    if (term.eof()) return false;
    if (key.code == KeyEvent::Code::Enter) {
      value = buffer;
      return true;
    }
    if (key.code == KeyEvent::Code::Esc || key.code == KeyEvent::Code::CtrlC) return false;
    if (key.code == KeyEvent::Code::Backspace) {
      if (cursor > 0) {
        size_t pos = 0;
        size_t prev = 0;
        while (pos < cursor) {
          prev = pos;
          str::decode_utf8(buffer, pos);
        }
        buffer.erase(prev, cursor - prev);
        cursor = prev;
      }
      continue;
    }
    if (key.code == KeyEvent::Code::Delete) {
      if (cursor < buffer.size()) {
        size_t pos = cursor;
        str::decode_utf8(buffer, pos);
        buffer.erase(cursor, pos - cursor);
      }
      continue;
    }
    if (key.code == KeyEvent::Code::Left) {
      if (cursor > 0) {
        size_t pos = 0;
        size_t prev = 0;
        while (pos < cursor) {
          prev = pos;
          str::decode_utf8(buffer, pos);
        }
        cursor = prev;
      }
      continue;
    }
    if (key.code == KeyEvent::Code::Right) {
      if (cursor < buffer.size()) {
        size_t pos = cursor;
        str::decode_utf8(buffer, pos);
        cursor = pos;
      }
      continue;
    }
    if (key.code == KeyEvent::Code::Home) {
      cursor = 0;
      continue;
    }
    if (key.code == KeyEvent::Code::End) {
      cursor = buffer.size();
      continue;
    }
    if (key.code == KeyEvent::Code::CtrlC) {
      return false;
    }
    if (key.code == KeyEvent::Code::Char) {
      if (key.text.size() == 1 && key.text[0] == 21) {  // Ctrl+U
        buffer.clear();
        cursor = 0;
        continue;
      }
      if (key.text.size() == 1 && key.text[0] == 22) {  // Ctrl+V：直接读系统剪贴板
        std::string pasted = clipboard::text();
        pasted = str::replace_all(pasted, "\r", "");
        pasted = str::replace_all(pasted, "\n", "");
        if (!pasted.empty()) {
          if (buffer.size() + pasted.size() > options.max_length)
            pasted.resize(options.max_length > buffer.size() ? options.max_length - buffer.size() : 0);
          buffer.insert(cursor, pasted);
          cursor += pasted.size();
        }
        continue;
      }
      if (buffer.size() + key.text.size() > options.max_length) continue;
      buffer.insert(cursor, key.text);
      cursor += key.text.size();
    }
    if (key.code == KeyEvent::Code::F2) {
      std::string pasted = clipboard::text();
      pasted = str::replace_all(pasted, "\r", "");
      pasted = str::replace_all(pasted, "\n", "");
      if (!pasted.empty() && buffer.size() + pasted.size() <= options.max_length) {
        buffer.insert(cursor, pasted);
        cursor += pasted.size();
      }
    }
  }
}

bool confirm(Terminal& term, const std::string& title, const std::string& message,
             bool default_yes) {
  std::vector<MenuItem> items;
  items.push_back({default_yes ? "是（默认）" : "是", "", "", false});
  items.push_back({default_yes ? "否" : "否（默认）", "", "", false});
  MenuOptions options;
  options.title = title;
  options.subtitle = "";
  options.allow_quit = false;
  options.show_back = true;
  for (const auto& line : str::wrap(message, std::max(20, std::min(term.width(), 110) - 8))) {
    options.header_lines.push_back(line);
  }
  int index = menu(term, options, items, default_yes ? 0 : 1);
  return index == 0;
}

void message(Terminal& term, const std::string& title, const std::vector<std::string>& lines) {
  int width = std::min(term.width(), 110);
  std::vector<std::string> out;
  out.push_back(header_line("", "", width));
  out.push_back(separator(width));
  out.push_back("");
  out.push_back("  " + t.accent2 + t.bold + title + t.reset);
  out.push_back("");
  for (const auto& line : lines) {
    for (const auto& wrapped : str::wrap(line, std::max(20, width - 6))) {
      out.push_back("  " + t.text + wrapped + t.reset);
    }
  }
  out.push_back("");
  out.push_back(separator(width));
  out.push_back(t.dim + "  按 Enter 继续" + t.reset);
  draw(term, out);
  for (;;) {
    KeyEvent key = term.read_key();
    if (term.eof()) break;
    if (key.code == KeyEvent::Code::Enter || key.code == KeyEvent::Code::Esc ||
        key.code == KeyEvent::Code::CtrlC)
      break;
  }
}

void error_box(Terminal& term, const std::string& title, const std::string& text) {
  int width = std::min(term.width(), 110);
  std::vector<std::string> out;
  out.push_back(header_line("", "", width));
  out.push_back(separator(width));
  out.push_back("");
  out.push_back("  " + t.bad + t.bold + title + t.reset);
  out.push_back("");
  for (const auto& line : str::wrap(text, std::max(20, width - 6))) {
    out.push_back("  " + t.text + line + t.reset);
  }
  out.push_back("");
  out.push_back(separator(width));
  out.push_back(t.dim + "  按 Enter 继续" + t.reset);
  draw(term, out);
  for (;;) {
    KeyEvent key = term.read_key();
    if (term.eof()) break;
    if (key.code == KeyEvent::Code::Enter || key.code == KeyEvent::Code::Esc ||
        key.code == KeyEvent::Code::CtrlC)
      break;
  }
}

void wait_enter(Terminal& term) {
  for (;;) {
    KeyEvent key = term.read_key();
    if (term.eof()) return;
    if (key.code == KeyEvent::Code::Enter || key.code == KeyEvent::Code::Esc ||
        key.code == KeyEvent::Code::CtrlC)
      return;
  }
}

void draw_waiting(Terminal& term, const std::string& title, const std::string& text) {
  int width = std::min(term.width(), 110);
  std::vector<std::string> out;
  out.push_back(header_line("", "", width));
  out.push_back(separator(width));
  out.push_back("");
  out.push_back("  " + t.accent2 + t.bold + title + t.reset);
  out.push_back("");
  for (const auto& line : str::wrap(text, std::max(20, width - 6))) {
    out.push_back("  " + t.dim + line + t.reset);
  }
  out.push_back("");
  out.push_back("  " + t.accent + "…" + t.reset);
  draw(term, out);
}

ProgressView::ProgressView(Terminal& term, std::string title)
    : term_(term), title_(std::move(title)) {}

void ProgressView::set_status(const std::string& status) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_ = status;
}

void ProgressView::set_cancel_callback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  cancel_callback_ = std::move(callback);
}

void ProgressView::render_locked(const net::DownloadStats& stats, bool finished) {
  int width = std::min(term_.width(), 110);
  int bar_width = std::max(10, std::min(60, width - 20));
  double ratio = 0.0;
  if (stats.bytes_total > 0) ratio = static_cast<double>(stats.bytes_done) / stats.bytes_total;
  if (finished) ratio = 1.0;
  ratio = std::max(0.0, std::min(1.0, ratio));
  int filled = static_cast<int>(ratio * bar_width);
  std::string bar = t.good + repeat_text("█", filled) + t.border +
                    repeat_text("░", bar_width - filled) + t.reset;

  std::vector<std::string> lines;
  lines.push_back(header_line("", "", width));
  lines.push_back(separator(width));
  lines.push_back("");
  lines.push_back("  " + t.accent2 + t.bold + title_ + t.reset);
  lines.push_back("");
  lines.push_back("  " + bar + "  " + t.text +
                  str::fmt("%3d%%", static_cast<int>(ratio * 100)) + t.reset);
  std::string bytes = str::human_size(stats.bytes_done);
  if (stats.bytes_total > 0) bytes += " / " + str::human_size(stats.bytes_total);
  std::string line = "  " + t.text + bytes;
  if (stats.speed > 0) line += "  ·  " + str::human_size(static_cast<uint64_t>(stats.speed)) + "/s";
  if (stats.files_total > 0)
    line += str::fmt("  ·  文件 %zu/%zu", stats.files_done, stats.files_total);
  line += t.reset;
  lines.push_back(line);
  lines.push_back("");
  std::string current = status_.empty() ? stats.current : status_;
  if (finished) current = final_text_;
  lines.push_back("  " + t.dim + str::ellipsize(current, std::max(10, width - 6)) + t.reset);
  lines.push_back("");
  lines.push_back(separator(width));
  lines.push_back(t.dim + (finished ? "  完成" : "  Esc 取消") + t.reset);
  draw(term_, lines);
}

void ProgressView::update(const net::DownloadStats& stats) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (finished_) return;
  if (!cancel_requested_) {
    KeyEvent key;
    if (term_.poll_key(0, key) && key.code == KeyEvent::Code::Esc) {
      cancel_requested_ = true;
      cancelled_ = true;
      if (cancel_callback_) {
        auto callback = cancel_callback_;
        callback();
      }
    }
  }
  auto now = std::chrono::steady_clock::now();
  if (now - last_draw_ < std::chrono::milliseconds(80)) return;
  last_draw_ = now;
  render_locked(stats, false);
}

void ProgressView::finish(bool ok, const std::string& text) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (finished_) return;
  finished_ = true;
  final_text_ = text;
  net::DownloadStats stats;
  stats.bytes_total = 1;
  stats.bytes_done = ok ? 1 : 0;
  render_locked(stats, true);
}

}  // namespace mct::ui
