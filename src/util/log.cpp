#include "util/log.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

#include "util/fs.hpp"
#include "util/str.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace mct::log {

namespace {
std::mutex g_mutex;
std::string g_path;
FILE* g_file = nullptr;

void write_locked(const char* level, const std::string& message) {
  if (g_file) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday, tm_buf.tm_hour,
                  tm_buf.tm_min, tm_buf.tm_sec, static_cast<int>(ms.count()));
    std::fprintf(g_file, "[%s] [%-5s] %s\n", stamp, level, message.c_str());
    std::fflush(g_file);
  }
}
}  // namespace

void init(const std::string& file) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_path = file;
  std::string dir = fs::parent(file);
  if (!dir.empty()) fs::ensure_dir(dir);
  bool fresh = !fs::is_file(file) || fs::file_size(file) == 0;
#ifdef _WIN32
  g_file = _wfopen(str::widen(file).c_str(), L"ab");
#else
  g_file = std::fopen(file.c_str(), "ab");
#endif
  if (g_file) {
    if (fresh) std::fwrite("\xEF\xBB\xBF", 1, 3, g_file);  // UTF-8 BOM，方便记事本识别
    std::fprintf(g_file, "\n===== MCT %s 启动 =====\n", MCT_VERSION);
    std::fflush(g_file);
  }
}

void shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file) {
    std::fclose(g_file);
    g_file = nullptr;
  }
}

void write(const char* level, const std::string& message) {
  std::lock_guard<std::mutex> lock(g_mutex);
  write_locked(level, message);
}

void debug(const std::string& message) { write("DEBUG", message); }
void info(const std::string& message) { write("INFO", message); }
void warn(const std::string& message) { write("WARN", message); }
void error(const std::string& message) { write("ERROR", message); }
std::string path() { return g_path; }

}  // namespace mct::log
