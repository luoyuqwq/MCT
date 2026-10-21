#include "util/hash_cache.hpp"

#include "util/fs.hpp"
#include "util/hash.hpp"
#include "util/json.hpp"
#include "util/log.hpp"
#include "util/str.hpp"

namespace mct {

std::string HashCache::key_for(const std::string& file) {
  std::string key = fs::normalize(file);
#ifdef _WIN32
  key = str::lower(key);
#endif
  return key;
}

void HashCache::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
}

size_t HashCache::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

void HashCache::load(const std::string& path) {
  std::string error;
  Json root;
  if (!Json::parse_file(path, root, &error) || !root.is_object()) {
    log::info("哈希缓存不存在或无效，将重新校验文件");
    return;
  }
  std::map<std::string, Entry> loaded;
  for (const auto& kv : root["files"].members()) {
    Entry entry;
    entry.size = static_cast<uint64_t>(kv.second.get_int("size", 0));
    entry.mtime = kv.second.get_int("mtime", 0);
    entry.sha1 = str::lower(kv.second.get_str("sha1"));
    if (entry.size > 0 && !entry.sha1.empty()) loaded[kv.first] = std::move(entry);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  entries_ = std::move(loaded);
  log::info(str::fmt("已加载哈希缓存：%zu 个文件", entries_.size()));
}

bool HashCache::save(const std::string& path) const {
  Json root = Json::object();
  root["version"] = 1;
  Json files = Json::object();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : entries_) {
      Json entry = Json::object();
      entry["size"] = static_cast<long long>(kv.second.size);
      entry["mtime"] = kv.second.mtime;
      entry["sha1"] = kv.second.sha1;
      files.set(kv.first, std::move(entry));
    }
  }
  root["files"] = files;
  return fs::write_file_atomic(path, root.dump());
}

bool HashCache::verify(const std::string& file, uint64_t size, int64_t mtime,
                       const std::string& sha1) {
  if (sha1.empty()) return false;
  const std::string key = key_for(file);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it != entries_.end() && it->second.size == size && it->second.mtime == mtime &&
        str::iequals(it->second.sha1, sha1)) {
      return true;
    }
  }
  if (!hash::sha1_matches_file(file, sha1)) return false;
  remember(file, size, mtime, sha1);
  return true;
}

void HashCache::remember(const std::string& file, uint64_t size, int64_t mtime,
                         const std::string& sha1) {
  if (sha1.empty()) return;
  Entry entry;
  entry.size = size;
  entry.mtime = mtime;
  entry.sha1 = str::lower(sha1);
  std::lock_guard<std::mutex> lock(mutex_);
  entries_[key_for(file)] = std::move(entry);
}

}  // namespace mct
