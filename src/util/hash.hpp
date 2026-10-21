// 哈希与 UUID：MD5 / SHA1 / CRC32 / Murmur2（CurseForge 指纹）
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mct::hash {

std::string md5_raw(const void* data, size_t len);  // 16 字节二进制
std::string md5_hex(const void* data, size_t len);
std::string md5_hex(const std::string& s);
std::string sha1_hex(const void* data, size_t len);
std::string sha1_hex(const std::string& s);

std::string md5_file(const std::string& path);
std::string sha1_file(const std::string& path);
bool sha1_matches_file(const std::string& path, const std::string& expected);

uint32_t crc32(const void* data, size_t len, uint32_t seed = 0);
uint32_t murmur2(const void* data, size_t len);
// CurseForge 计算指纹前会过滤空白字符
uint32_t curseforge_fingerprint(const std::string& content);

// 离线模式 UUID（UUID v3，namespace 为 "OfflinePlayer:" + name）
std::string offline_uuid(const std::string& name);
std::string uuid_with_dashes(const std::string& raw);
std::string uuid_without_dashes(const std::string& uuid);
// 32 位小写十六进制（用于 GameProfile id）
std::string random_hex(size_t bytes);

}  // namespace mct::hash
