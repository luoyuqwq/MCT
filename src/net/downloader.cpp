#include "net/downloader.hpp"

#include <algorithm>
#include <thread>

#include "net/http.hpp"
#include "util/fs.hpp"
#include "util/hash.hpp"
#include "util/hash_cache.hpp"
#include "util/log.hpp"
#include "util/str.hpp"

namespace mct::net {

Downloader::Downloader(int threads, ProgressCallback callback)
    : threads_(std::max(1, threads)), callback_(std::move(callback)) {}

Downloader::~Downloader() = default;

bool Downloader::verify_file(const std::string& path, const DownloadTask& task, std::string* why) {
  if (!fs::is_file(path)) {
    if (why) *why = "文件不存在";
    return false;
  }
  if (!task.sha1.empty()) {
    if (!hash::sha1_matches_file(path, task.sha1)) {
      if (why) *why = "SHA1 不匹配";
      return false;
    }
    return true;
  }
  if (task.size > 0 && fs::file_size(path) != task.size) {
    if (why) *why = "文件大小不匹配";
    return false;
  }
  return true;
}

void Downloader::set_current(const std::string& text) {
  std::lock_guard<std::mutex> lock(current_mutex_);
  current_ = text;
}

bool Downloader::verify_task(const std::string& path, const DownloadTask& task,
                             std::string* why) {
  if (!fs::is_file(path)) {
    if (why) *why = "文件不存在";
    return false;
  }
  const uint64_t size = fs::file_size(path);
  if (!task.sha1.empty()) {
    if (hash_cache_) {
      if (hash_cache_->verify(path, size, fs::mtime(path), task.sha1)) return true;
      if (why) *why = "SHA1 不匹配";
      return false;
    }
    if (!hash::sha1_matches_file(path, task.sha1)) {
      if (why) *why = "SHA1 不匹配";
      return false;
    }
    return true;
  }
  if (task.size > 0 && size != task.size) {
    if (why) *why = "文件大小不匹配";
    return false;
  }
  return true;
}

void Downloader::report(bool force) {
  std::lock_guard<std::mutex> lock(report_mutex_);
  auto now = std::chrono::steady_clock::now();
  if (!force && now - last_report_ < std::chrono::milliseconds(100)) return;
  last_report_ = now;
  if (!callback_) return;
  DownloadStats stats;
  stats.bytes_done = bytes_done_.load();
  stats.bytes_total = bytes_total_.load();
  stats.files_done = files_done_.load();
  stats.files_total = files_total_.load();
  {
    std::lock_guard<std::mutex> current_lock(current_mutex_);
    stats.current = current_;
  }
  double elapsed = std::chrono::duration<double>(now - start_).count();
  if (elapsed > 0.2) stats.speed = static_cast<double>(stats.bytes_done) / elapsed;
  callback_(stats);
}

bool Downloader::run(std::vector<DownloadTask>& tasks, std::string* error) {
  cancel_.store(false);
  avoid_first_url_.store(false);
  bytes_done_.store(0);
  bytes_total_.store(0);
  files_done_.store(0);
  files_total_.store(0);
  files_ok_.store(0);
  start_ = std::chrono::steady_clock::now();
  last_report_ = start_ - std::chrono::seconds(10);

  std::vector<DownloadTask*> pending;
  uint64_t total = 0;
  uint64_t skipped_bytes = 0;
  for (auto& task : tasks) {
    if (!task.urls.empty() && !task.dest.empty()) {
      if (verify_task(task.dest, task, nullptr)) {
        bytes_done_ += task.size;
        skipped_bytes += task.size;
        files_ok_ += 1;
        continue;
      }
      pending.push_back(&task);
      total += task.size;
    }
  }
  bytes_total_.store(total + skipped_bytes);
  files_total_.store(pending.size() + files_ok_.load());
  files_done_.store(files_ok_.load());
  report(true);

  if (pending.empty()) {
    report(true);
    return true;
  }

  std::atomic<size_t> next{0};
  std::atomic<bool> failed{false};
  std::mutex err_mutex;
  std::string first_error;

  auto worker = [&]() {
    HttpClient http;
    http.set_timeout_ms(20000, 60000);
    while (!cancel_.load() && !failed.load()) {
      size_t index = next.fetch_add(1);
      if (index >= pending.size()) break;
      DownloadTask* task = pending[index];
      bool ok = false;
      std::string last_error;
      uint64_t reported_received = 0;

      for (int attempt = 0; attempt <= retries_ && !ok && !cancel_.load(); ++attempt) {
        size_t url_count = task->urls.size();
        for (size_t ui = 0; ui < url_count && !ok && !cancel_.load(); ++ui) {
          size_t url_index =
              (avoid_first_url_.load() && url_count > 1) ? (ui + 1) % url_count : ui;
          const std::string& url = task->urls[url_index];
          if (url.empty() || cancel_.load()) continue;
          set_current(task->label.empty() ? fs::filename(task->dest) : task->label);
          reported_received = 0;
          uint64_t previous = 0;
          ProgressFn progress = [&](uint64_t received, uint64_t /*content_total*/) {
            uint64_t delta = received > previous ? received - previous : 0;
            previous = received;
            reported_received = received;
            bytes_done_ += delta;
            report();
            return !cancel_.load();
          };
          HttpResult result = http.get_to_file(url, task->dest, progress);

          if (result.status == 429) {
            int wait_seconds = 5;
            std::string retry_after = result.header("retry-after");
            if (retry_after.empty()) retry_after = result.header("retry-after-short");
            if (!retry_after.empty()) {
              try {
                int value = std::stoi(retry_after);
                if (value > 0) wait_seconds = std::min(value, 120);
              } catch (const std::exception&) {
              }
            }
            log::warn(str::fmt("镜像站限流（429），等待 %d 秒后继续", wait_seconds));
            avoid_first_url_.store(true);
            std::this_thread::sleep_for(std::chrono::seconds(wait_seconds));
          }

          if (!result.error.empty() && result.error != "已取消") {
            last_error = result.error;
            log::warn("下载失败 " + url + " : " + result.error);
          }
          if (result.ok()) {
            std::string why;
            if (verify_task(task->dest, *task, &why)) {
              ok = true;
              break;
            }
            last_error = "校验失败(" + why + "): " + url;
            log::warn(last_error);
            fs::remove_file(task->dest);
          } else if (result.error.empty() || result.error == "已取消") {
            last_error = str::fmt("HTTP %ld: %s", result.status, url.c_str());
            log::warn(last_error);
          }
          // 本次失败，把已计入的字节退回，避免进度条虚高
          bytes_done_ -= reported_received;
          reported_received = 0;
          if (cancel_.load()) break;
        }
        if (!ok && !cancel_.load() && attempt < retries_) {
          std::this_thread::sleep_for(std::chrono::milliseconds(400 * (attempt + 1)));
        }
      }

      if (ok) {
        files_done_ += 1;
        files_ok_ += 1;
      } else if (!cancel_.load()) {
        if (task->required) {
          std::lock_guard<std::mutex> lock(err_mutex);
          if (first_error.empty())
            first_error = (last_error.empty() ? "下载失败" : last_error) + " -> " + task->dest;
          failed.store(true);
        } else {
          log::warn("可选的资源下载失败，已跳过: " + task->dest);
          files_done_ += 1;
        }
      }
      report(true);
    }
  };

  std::vector<std::thread> pool;
  size_t count = std::min<size_t>(static_cast<size_t>(threads_), pending.size());
  for (size_t i = 0; i < count; ++i) pool.emplace_back(worker);
  for (auto& t : pool) t.join();

  report(true);
  if (cancel_.load()) {
    if (error) *error = "下载已取消";
    return false;
  }
  if (failed.load()) {
    if (error) *error = first_error.empty() ? "下载失败" : first_error;
    return false;
  }
  return true;
}

}  // namespace mct::net
