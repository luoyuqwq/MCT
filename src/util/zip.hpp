// 最小 ZIP 读取器（支持 store/deflate），用于解压 natives、读取皮肤包等。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mct::zip {

struct Entry {
  std::string name;
  uint16_t flags = 0;
  uint16_t method = 0;
  uint32_t crc = 0;
  uint64_t compressed = 0;
  uint64_t uncompressed = 0;
  uint64_t local_offset = 0;
};

bool is_zip(const std::string& data);
bool list(const std::string& data, std::vector<Entry>& out, std::string* error = nullptr);

// 解压单个条目（自动处理 deflate）
bool read_entry(const std::string& data, const Entry& entry, std::string& out,
                std::string* error = nullptr);

// 解压全部条目到 dest_dir，跳过 skip_prefix 前缀（默认跳过 META-INF/）
bool extract_all(const std::string& data, const std::string& dest_dir, std::string* error = nullptr,
                 const std::vector<std::string>& skip_prefixes = {"META-INF/"},
                 size_t* extracted_count = nullptr);

}  // namespace mct::zip
