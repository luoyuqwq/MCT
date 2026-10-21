// 文件系统工具：UTF-8 路径，Windows 下走宽字符 API。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mct::fs {

struct FileInfo {
  std::string name;
  bool is_dir = false;
  uint64_t size = 0;
  int64_t mtime = 0;
};

inline char sep() {
#ifdef _WIN32
  return '\\';
#else
  return '/';
#endif
}

std::string join(const std::string& a, const std::string& b);
std::string join(const std::string& a, const std::string& b, const std::string& c);
std::string join(const std::string& a, const std::string& b, const std::string& c,
                 const std::string& d);
std::string parent(const std::string& path);
std::string filename(const std::string& path);
std::string stem(const std::string& path);
std::string extension(const std::string& path);
std::string normalize(const std::string& path);

bool exists(const std::string& path);
bool is_dir(const std::string& path);
bool is_file(const std::string& path);
bool ensure_dir(const std::string& path);

bool read_file(const std::string& path, std::string& out);
bool write_file(const std::string& path, const std::string& data);
// 先写临时文件再改名，避免半个文件
bool write_file_atomic(const std::string& path, const std::string& data);
bool remove_file(const std::string& path);
bool remove_dir_recursive(const std::string& path);
bool rename_path(const std::string& from, const std::string& to);
bool copy_file(const std::string& from, const std::string& to);

uint64_t file_size(const std::string& path);
int64_t mtime(const std::string& path);
std::vector<FileInfo> list_dir(const std::string& path);

struct CopyProgress {
  uint64_t bytes_done = 0;
  uint64_t bytes_total = 0;
  size_t files_done = 0;
  size_t files_total = 0;
  std::string current;
};

// 递归复制目录（跳过目标端已存在且大小相同的文件），可用于切换游戏目录
bool copy_dir(const std::string& src, const std::string& dst,
              const std::function<void(const CopyProgress&)>& on_progress = nullptr,
              const std::function<bool()>& cancelled = nullptr);

std::string absolute(const std::string& path);
std::string current_dir();
std::string home_dir();
std::string appdata_dir();
std::string temp_dir();
std::string exe_dir();
std::string get_env(const std::string& name);
std::vector<std::string> path_dirs();
bool equal(const std::string& a, const std::string& b);

}  // namespace mct::fs
