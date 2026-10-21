// 多线程下载器：镜像回退、SHA1 校验、断点跳过、进度聚合。
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace mct {
class HashCache;
}

namespace mct::net {

struct DownloadTask {
  std::vector<std::string> urls;  // 按顺序尝试（镜像优先）
  std::string dest;
  std::string sha1;
  uint64_t size = 0;
  bool required = true;
  std::string label;
};

struct DownloadStats {
  uint64_t bytes_done = 0;
  uint64_t bytes_total = 0;
  size_t files_done = 0;
  size_t files_total = 0;
  std::string current;
  double speed = 0;  // 字节/秒
  bool finished = false;
};

using ProgressCallback = std::function<void(const DownloadStats&)>;

class Downloader {
 public:
  Downloader(int threads, ProgressCallback callback);
  ~Downloader();

  void cancel() { cancel_.store(true); }
  bool cancelled() const { return cancel_.load(); }
  void set_retries(int retries) { retries_ = retries; }
  // 可选：跳过对大文件的重复哈希校验
  void set_hash_cache(HashCache* cache) { hash_cache_ = cache; }

  // 下载所有任务；返回 false 时 error 说明失败原因
  bool run(std::vector<DownloadTask>& tasks, std::string* error);

  // 校验已存在的文件（sha1 优先，其次大小）；why 返回失败原因
  static bool verify_file(const std::string& path, const DownloadTask& task, std::string* why);

 private:
  void report(bool force = false);
  void set_current(const std::string& text);
  bool verify_task(const std::string& path, const DownloadTask& task, std::string* why);

  int threads_;
  int retries_ = 2;
  ProgressCallback callback_;
  std::atomic<bool> cancel_{false};
  std::atomic<bool> avoid_first_url_{false};
  std::atomic<uint64_t> bytes_done_{0};
  std::atomic<uint64_t> bytes_total_{0};
  std::atomic<size_t> files_done_{0};
  std::atomic<size_t> files_total_{0};
  std::atomic<size_t> files_ok_{0};

  std::mutex current_mutex_;
  std::string current_;

  std::mutex report_mutex_;
  std::chrono::steady_clock::time_point start_{};
  std::chrono::steady_clock::time_point last_report_{};
  HashCache* hash_cache_ = nullptr;
};

}  // namespace mct::net
