// 已校验文件缓存：避免每次启动/安装都重新计算大文件的 SHA1
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace mct {

class HashCache {
 public:
  void load(const std::string& path);
  bool save(const std::string& path) const;
  void clear();
  size_t size() const;

  // 命中缓存（大小/mtime/SHA1 都一致）或实际校验通过时返回 true
  bool verify(const std::string& file, uint64_t size, int64_t mtime, const std::string& sha1);
  void remember(const std::string& file, uint64_t size, int64_t mtime, const std::string& sha1);

 private:
  struct Entry {
    uint64_t size = 0;
    int64_t mtime = 0;
    std::string sha1;
  };
  static std::string key_for(const std::string& file);

  mutable std::mutex mutex_;
  std::map<std::string, Entry> entries_;
};

}  // namespace mct
