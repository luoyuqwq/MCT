#include "util/zip.hpp"

#include <cstring>

#include "util/fs.hpp"
#include "util/hash.hpp"
#include "util/inflate.hpp"
#include "util/log.hpp"
#include "util/str.hpp"

namespace mct::zip {

namespace {

uint16_t rd16(const unsigned char* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t rd32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t rd64(const unsigned char* p) {
  return static_cast<uint64_t>(rd32(p)) | (static_cast<uint64_t>(rd32(p + 4)) << 32);
}

bool safe_name(const std::string& name) {
  if (name.empty()) return false;
  if (name[0] == '/' || name[0] == '\\') return false;
  if (name.find(':') != std::string::npos) return false;
  std::string norm = str::replace_all(name, "\\", "/");
  for (const auto& part : str::split(norm, '/')) {
    if (part == "..") return false;
  }
  return true;
}

}  // namespace

bool is_zip(const std::string& data) {
  return data.size() > 4 && static_cast<unsigned char>(data[0]) == 'P' &&
         static_cast<unsigned char>(data[1]) == 'K';
}

bool list(const std::string& data, std::vector<Entry>& out, std::string* error) {
  out.clear();
  if (!is_zip(data)) {
    if (error) *error = "不是有效的 ZIP 文件";
    return false;
  }
  const unsigned char* p = reinterpret_cast<const unsigned char*>(data.data());
  const size_t size = data.size();
  if (size < 22) {
    if (error) *error = "ZIP 文件过短";
    return false;
  }
  size_t search_start = size > 65557 ? size - 65557 : 0;
  size_t eocd = std::string::npos;
  for (size_t i = size - 22 + 1; i-- > search_start;) {
    if (rd32(p + i) == 0x06054b50u) {
      eocd = i;
      break;
    }
  }
  if (eocd == std::string::npos) {
    if (error) *error = "未找到 ZIP 中央目录";
    return false;
  }
  uint64_t total = rd16(p + eocd + 10);
  uint64_t cd_offset = rd32(p + eocd + 16);
  uint64_t cd_size = rd32(p + eocd + 12);
  // ZIP64：EOCD 中字段为 0xFFFF/0xFFFFFFFF 时查找 ZIP64 EOCD
  if (total == 0xFFFF || cd_offset == 0xFFFFFFFFu) {
    for (size_t i = eocd; i-- > search_start;) {
      if (rd32(p + i) == 0x06064b50u) {
        total = rd64(p + i + 32);
        cd_offset = rd64(p + i + 48);
        break;
      }
      if (i + 4 > size) break;
    }
  }
  if (cd_offset > size) {
    if (error) *error = "ZIP 中央目录偏移非法";
    return false;
  }
  size_t pos = static_cast<size_t>(cd_offset);
  for (uint64_t i = 0; i < total; ++i) {
    if (pos + 46 > size) {
      if (error) *error = "ZIP 中央目录被截断";
      return false;
    }
    const unsigned char* h = p + pos;
    if (rd32(h) != 0x02014b50u) {
      if (error) *error = "ZIP 中央目录条目签名错误";
      return false;
    }
    Entry e;
    e.flags = rd16(h + 8);
    e.method = rd16(h + 10);
    e.crc = rd32(h + 16);
    e.compressed = rd32(h + 20);
    e.uncompressed = rd32(h + 24);
    uint16_t name_len = rd16(h + 28);
    uint16_t extra_len = rd16(h + 30);
    uint16_t comment_len = rd16(h + 32);
    e.local_offset = rd32(h + 42);
    if (pos + 46 + name_len > size) {
      if (error) *error = "ZIP 条目名称越界";
      return false;
    }
    e.name.assign(reinterpret_cast<const char*>(h + 46), name_len);
    // 解析 ZIP64 extra field
    if (e.compressed == 0xFFFFFFFFu || e.uncompressed == 0xFFFFFFFFu ||
        e.local_offset == 0xFFFFFFFFu) {
      size_t extra_pos = pos + 46 + name_len;
      size_t extra_end = extra_pos + extra_len;
      if (extra_end > size) {
        if (error) *error = "ZIP extra field 越界";
        return false;
      }
      while (extra_pos + 4 <= extra_end) {
        uint16_t id = rd16(p + extra_pos);
        uint16_t len = rd16(p + extra_pos + 2);
        size_t body = extra_pos + 4;
        if (body + len > extra_end) break;
        if (id == 0x0001) {
          size_t off = body;
          if (e.uncompressed == 0xFFFFFFFFu && off + 8 <= body + len) {
            e.uncompressed = rd64(p + off);
            off += 8;
          }
          if (e.compressed == 0xFFFFFFFFu && off + 8 <= body + len) {
            e.compressed = rd64(p + off);
            off += 8;
          }
          if (e.local_offset == 0xFFFFFFFFu && off + 8 <= body + len) {
            e.local_offset = rd64(p + off);
            off += 8;
          }
          break;
        }
        extra_pos = body + len;
      }
    }
    out.push_back(std::move(e));
    pos += 46 + name_len + extra_len + comment_len;
  }
  (void)cd_size;
  return true;
}

bool read_entry(const std::string& data, const Entry& entry, std::string& out,
                std::string* error) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(data.data());
  const size_t size = data.size();
  if (entry.local_offset + 30 > size) {
    if (error) *error = "ZIP 本地文件头越界";
    return false;
  }
  const unsigned char* lh = p + entry.local_offset;
  if (rd32(lh) != 0x04034b50u) {
    if (error) *error = "ZIP 本地文件头签名错误";
    return false;
  }
  uint16_t name_len = rd16(lh + 26);
  uint16_t extra_len = rd16(lh + 28);
  uint64_t start = entry.local_offset + 30 + name_len + extra_len;
  if (start + entry.compressed > size) {
    if (error) *error = "ZIP 条目数据越界";
    return false;
  }
  const char* src = data.data() + start;
  if (entry.method == 0) {
    out.assign(src, static_cast<size_t>(entry.compressed));
  } else if (entry.method == 8) {
    std::string err;
    if (!inflate_impl::raw_deflate(src, static_cast<size_t>(entry.compressed), out, &err,
                                   512ull * 1024 * 1024)) {
      if (error) *error = "解压失败: " + err;
      return false;
    }
  } else {
    if (error) *error = "不支持的压缩算法: " + std::to_string(entry.method);
    return false;
  }
  if (out.size() != entry.uncompressed) {
    if (error)
      *error = str::fmt("解压后大小不符（期望 %llu，实际 %llu）",
                        static_cast<unsigned long long>(entry.uncompressed),
                        static_cast<unsigned long long>(out.size()));
    return false;
  }
  if (hash::crc32(out.data(), out.size()) != entry.crc) {
    if (error) *error = "CRC32 校验失败: " + entry.name;
    return false;
  }
  return true;
}

bool extract_all(const std::string& data, const std::string& dest_dir, std::string* error,
                 const std::vector<std::string>& skip_prefixes, size_t* extracted_count) {
  std::vector<Entry> entries;
  if (!list(data, entries, error)) return false;
  if (!fs::ensure_dir(dest_dir)) {
    if (error) *error = "无法创建目录: " + dest_dir;
    return false;
  }
  size_t count = 0;
  for (const Entry& e : entries) {
    if (e.name.empty() || e.name.back() == '/') continue;
    if (!safe_name(e.name)) {
      log::warn("跳过不安全的 ZIP 条目: " + e.name);
      continue;
    }
    bool skip = false;
    for (const auto& prefix : skip_prefixes) {
      if (str::starts_with(e.name, prefix)) {
        skip = true;
        break;
      }
    }
    if (skip) continue;
    std::string target = fs::join(dest_dir, str::replace_all(e.name, "\\", "/"));
    std::string content;
    std::string err;
    if (!read_entry(data, e, content, &err)) {
      if (error) *error = err + " (" + e.name + ")";
      return false;
    }
    if (!fs::write_file(target, content)) {
      if (error) *error = "无法写入: " + target;
      return false;
    }
    ++count;
  }
  if (extracted_count) *extracted_count = count;
  return true;
}

}  // namespace mct::zip
