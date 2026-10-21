#include "util/hash.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "util/fs.hpp"
#include "util/str.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace mct::hash {

namespace {

inline uint32_t rotl32(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

const char* kHex = "0123456789abcdef";

std::string to_hex(const uint8_t* data, size_t len) {
  std::string out;
  out.resize(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out[i * 2] = kHex[data[i] >> 4];
    out[i * 2 + 1] = kHex[data[i] & 0xF];
  }
  return out;
}

// ---------------- MD5 ----------------
struct Md5 {
  uint32_t a = 0x67452301u, b = 0xefcdab89u, c = 0x98badcfeu, d = 0x10325476u;
  uint64_t total = 0;
  uint8_t buf[64];
  size_t used = 0;

  static const uint32_t* K() {
    static uint32_t table[64];
    static bool init = []() {
      for (int i = 0; i < 64; ++i)
        table[i] = static_cast<uint32_t>(std::fabs(std::sin(i + 1.0)) * 4294967296.0);
      return true;
    }();
    (void)init;
    return table;
  }

  void block(const uint8_t* p) {
    static const uint32_t S[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
      m[i] = static_cast<uint32_t>(p[i * 4]) | (static_cast<uint32_t>(p[i * 4 + 1]) << 8) |
             (static_cast<uint32_t>(p[i * 4 + 2]) << 16) |
             (static_cast<uint32_t>(p[i * 4 + 3]) << 24);
    }
    uint32_t A = a, B = b, C = c, D = d;
    const uint32_t* k = K();
    for (int i = 0; i < 64; ++i) {
      uint32_t f;
      int g;
      if (i < 16) {
        f = (B & C) | (~B & D);
        g = i;
      } else if (i < 32) {
        f = (D & B) | (~D & C);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = B ^ C ^ D;
        g = (3 * i + 5) % 16;
      } else {
        f = C ^ (B | ~D);
        g = (7 * i) % 16;
      }
      uint32_t tmp = D;
      D = C;
      C = B;
      B = B + rotl32(A + f + k[i] + m[g], static_cast<int>(S[i]));
      A = tmp;
    }
    a += A;
    b += B;
    c += C;
    d += D;
  }

  void update(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    total += len;
    while (len > 0) {
      size_t take = 64 - used;
      if (take > len) take = len;
      std::memcpy(buf + used, p, take);
      used += take;
      p += take;
      len -= take;
      if (used == 64) {
        block(buf);
        used = 0;
      }
    }
  }

  void finish(uint8_t out[16]) {
    uint64_t bits = total * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (used != 56) update(&zero, 1);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; ++i) lenbuf[i] = static_cast<uint8_t>((bits >> (8 * i)) & 0xFF);
    update(lenbuf, 8);
    uint32_t v[4] = {a, b, c, d};
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<uint8_t>((v[i] >> (8 * j)) & 0xFF);
    }
  }
};

// ---------------- SHA1 ----------------
struct Sha1 {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  uint64_t total = 0;
  uint8_t buf[64];
  size_t used = 0;

  void block(const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
             (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(p[i * 4 + 2]) << 8) | static_cast<uint32_t>(p[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      uint32_t tmp = rotl32(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rotl32(b, 30);
      b = a;
      a = tmp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }

  void update(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    total += len;
    while (len > 0) {
      size_t take = 64 - used;
      if (take > len) take = len;
      std::memcpy(buf + used, p, take);
      used += take;
      p += take;
      len -= take;
      if (used == 64) {
        block(buf);
        used = 0;
      }
    }
  }

  void finish(uint8_t out[20]) {
    uint64_t bits = total * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (used != 56) update(&zero, 1);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; ++i) lenbuf[i] = static_cast<uint8_t>((bits >> (56 - 8 * i)) & 0xFF);
    update(lenbuf, 8);
    for (int i = 0; i < 5; ++i) {
      out[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
      out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
      out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
      out[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
    }
  }
};

FILE* open_binary(const std::string& path) {
#ifdef _WIN32
  return _wfopen(str::widen(path).c_str(), L"rb");
#else
  return std::fopen(path.c_str(), "rb");
#endif
}

std::string hash_file(const std::string& path, bool sha1) {
  FILE* f = open_binary(path);
  if (!f) return std::string();
  std::vector<char> buf(256 * 1024);
  Md5 m;
  Sha1 s;
  while (true) {
    size_t n = std::fread(buf.data(), 1, buf.size(), f);
    if (n == 0) break;
    if (sha1)
      s.update(buf.data(), n);
    else
      m.update(buf.data(), n);
  }
  std::fclose(f);
  if (sha1) {
    uint8_t d[20];
    s.finish(d);
    return to_hex(d, 20);
  }
  uint8_t d[16];
  m.finish(d);
  return to_hex(d, 16);
}

}  // namespace

std::string md5_raw(const void* data, size_t len) {
  Md5 m;
  m.update(data, len);
  uint8_t d[16];
  m.finish(d);
  return std::string(reinterpret_cast<char*>(d), 16);
}

std::string md5_hex(const void* data, size_t len) {
  Md5 m;
  m.update(data, len);
  uint8_t d[16];
  m.finish(d);
  return to_hex(d, 16);
}

std::string md5_hex(const std::string& s) { return md5_hex(s.data(), s.size()); }

std::string sha1_hex(const void* data, size_t len) {
  Sha1 s;
  s.update(data, len);
  uint8_t d[20];
  s.finish(d);
  return to_hex(d, 20);
}

std::string sha1_hex(const std::string& s) { return sha1_hex(s.data(), s.size()); }

std::string md5_file(const std::string& path) { return hash_file(path, false); }
std::string sha1_file(const std::string& path) { return hash_file(path, true); }

bool sha1_matches_file(const std::string& path, const std::string& expected) {
  if (expected.empty()) return false;
  std::string got = sha1_file(path);
  return !got.empty() && str::iequals(got, expected);
}

uint32_t crc32(const void* data, size_t len, uint32_t seed) {
  static uint32_t table[256];
  static bool init = []() {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
      table[i] = c;
    }
    return true;
  }();
  (void)init;
  uint32_t c = seed ^ 0xFFFFFFFFu;
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

uint32_t murmur2(const void* data, size_t len) {
  const uint32_t m = 0x5bd1e995u;
  const int r = 24;
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t h = 1u ^ static_cast<uint32_t>(len);
  size_t remaining = len;
  while (remaining >= 4) {
    uint32_t k = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                 (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    k *= m;
    k ^= k >> r;
    k *= m;
    h *= m;
    h ^= k;
    p += 4;
    remaining -= 4;
  }
  switch (remaining) {
    case 3: h ^= static_cast<uint32_t>(p[2]) << 16; [[fallthrough]];
    case 2: h ^= static_cast<uint32_t>(p[1]) << 8; [[fallthrough]];
    case 1: h ^= static_cast<uint32_t>(p[0]); h *= m; break;
    default: break;
  }
  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;
  return h;
}

uint32_t curseforge_fingerprint(const std::string& content) {
  std::string filtered;
  filtered.reserve(content.size());
  for (unsigned char c : content) {
    if (c == 9 || c == 10 || c == 13 || c == 32) continue;
    filtered += static_cast<char>(c);
  }
  return murmur2(filtered.data(), filtered.size());
}

std::string offline_uuid(const std::string& name) {
  std::string digest = md5_raw(("OfflinePlayer:" + name).data(), name.size() + 14);
  uint8_t b[16];
  std::memcpy(b, digest.data(), 16);
  b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x30);
  b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);
  std::string hex = to_hex(b, 16);
  return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
         hex.substr(16, 4) + "-" + hex.substr(20, 12);
}

std::string uuid_with_dashes(const std::string& raw) {
  std::string s;
  s.reserve(raw.size());
  for (char c : raw)
    if (c != '-') s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (s.size() != 32) return raw;
  return s.substr(0, 8) + "-" + s.substr(8, 4) + "-" + s.substr(12, 4) + "-" + s.substr(16, 4) +
         "-" + s.substr(20, 12);
}

std::string uuid_without_dashes(const std::string& uuid) {
  std::string s;
  for (char c : uuid)
    if (c != '-') s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string random_hex(size_t bytes) {
  static thread_local std::mt19937_64 rng(std::random_device{}());
  std::string out;
  out.reserve(bytes * 2);
  for (size_t i = 0; i < bytes; ++i) {
    uint32_t v = static_cast<uint32_t>(rng() & 0xFF);
    out += kHex[v >> 4];
    out += kHex[v & 0xF];
  }
  return out;
}

}  // namespace mct::hash
