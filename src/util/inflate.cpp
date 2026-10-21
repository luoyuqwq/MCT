#include "util/inflate.hpp"

#include <cstring>

namespace mct::inflate_impl {

namespace {

constexpr int kMaxBits = 15;
constexpr int kMaxLCodes = 286;
constexpr int kMaxDCodes = 30;
constexpr int kMaxCodes = kMaxLCodes + kMaxDCodes;
constexpr int kFixLCodes = 288;
// 固定 Huffman 距离表按规范有 32 个 5 位码（30/31 非法，但需参与构造才能形成完整码表）
constexpr int kFixDCodes = 32;

struct Huffman {
  short count[kMaxBits + 1];
  short symbol[kFixLCodes];
};

struct State {
  const unsigned char* in = nullptr;
  size_t inlen = 0;
  size_t incnt = 0;
  uint32_t bitbuf = 0;
  int bitcnt = 0;
  std::string* out = nullptr;
  size_t limit = 0;
  bool overflow = false;

  int bits(int need) {
    uint32_t val = 0;
    for (int i = 0; i < need; ++i) {
      if (bitcnt == 0) {
        if (incnt >= inlen) return -1;
        bitbuf = in[incnt++];
        bitcnt = 8;
      }
      val |= (bitbuf & 1u) << i;
      bitbuf >>= 1;
      --bitcnt;
    }
    return static_cast<int>(val);
  }
};

int construct(Huffman& h, const short* length, int n) {
  for (int len = 0; len <= kMaxBits; ++len) h.count[len] = 0;
  for (int symbol = 0; symbol < n; ++symbol) {
    if (length[symbol] > kMaxBits) return -1;
    h.count[length[symbol]]++;
  }
  if (h.count[0] == n) return 0;
  int left = 1;
  for (int len = 1; len <= kMaxBits; ++len) {
    left <<= 1;
    left -= h.count[len];
    if (left < 0) return left;
  }
  short offs[kMaxBits + 2];
  offs[1] = 0;
  for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = offs[len] + h.count[len];
  for (int symbol = 0; symbol < n; ++symbol) {
    if (length[symbol] != 0) h.symbol[offs[length[symbol]]++] = static_cast<short>(symbol);
  }
  return left;
}

int decode(State& s, const Huffman& h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len <= kMaxBits; ++len) {
    int b = s.bits(1);
    if (b < 0) return -1;
    code |= b;
    int count = h.count[len];
    if (code - count < first) return h.symbol[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

int codes(State& s, const Huffman& lencode, const Huffman& distcode) {
  static const short lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19, 23, 27,
                                  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const short dbase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,   25,
                                  33,   49,   65,   97,   129,  193,   257,   385,   513,  769,
                                  1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
  static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  for (;;) {
    int symbol = decode(s, lencode);
    if (symbol < 0) return symbol;
    if (symbol < 256) {
      if (s.out->size() >= s.limit) {
        s.overflow = true;
        return -12;
      }
      s.out->push_back(static_cast<char>(symbol));
    } else if (symbol > 256) {
      symbol -= 257;
      if (symbol >= 29) return -10;
      int extra = s.bits(lext[symbol]);
      if (extra < 0) return extra;
      size_t len = static_cast<size_t>(lbase[symbol] + extra);
      int dsym = decode(s, distcode);
      if (dsym < 0) return dsym;
      if (dsym >= 30) return -10;
      extra = s.bits(dext[dsym]);
      if (extra < 0) return extra;
      size_t dist = static_cast<size_t>(dbase[dsym] + extra);
      if (dist > s.out->size()) return -11;
      if (s.out->size() + len > s.limit) {
        s.overflow = true;
        return -12;
      }
      size_t start = s.out->size() - dist;
      for (size_t k = 0; k < len; ++k) s.out->push_back((*s.out)[start + k]);
    } else {
      return 0;
    }
  }
}

int stored(State& s) {
  s.bitcnt = 0;
  s.bitbuf = 0;
  if (s.incnt + 4 > s.inlen) return -5;
  unsigned len = static_cast<unsigned>(s.in[s.incnt]) |
                 (static_cast<unsigned>(s.in[s.incnt + 1]) << 8);
  unsigned nlen = static_cast<unsigned>(s.in[s.incnt + 2]) |
                  (static_cast<unsigned>(s.in[s.incnt + 3]) << 8);
  s.incnt += 4;
  if (len != ((~nlen) & 0xFFFFu)) return -6;
  if (s.incnt + len > s.inlen) return -5;
  if (s.out->size() + len > s.limit) {
    s.overflow = true;
    return -12;
  }
  s.out->append(reinterpret_cast<const char*>(s.in + s.incnt), len);
  s.incnt += len;
  return 0;
}

int fixed(State& s) {
  short lengths[kFixLCodes];
  Huffman lencode, distcode;
  for (int i = 0; i < 144; ++i) lengths[i] = 8;
  for (int i = 144; i < 256; ++i) lengths[i] = 9;
  for (int i = 256; i < 280; ++i) lengths[i] = 7;
  for (int i = 280; i < 288; ++i) lengths[i] = 8;
  if (construct(lencode, lengths, kFixLCodes) != 0) return -8;
  for (int i = 0; i < kFixDCodes; ++i) lengths[i] = 5;
  if (construct(distcode, lengths, kFixDCodes) != 0) return -8;
  return codes(s, lencode, distcode);
}

int dynamic(State& s) {
  short lengths[kMaxCodes];
  Huffman lencode, distcode;
  static const short order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                  11, 4,  12, 3, 13, 2, 14, 1, 15};
  int nlen = s.bits(5);
  if (nlen < 0) return nlen;
  nlen += 257;
  int ndist = s.bits(5);
  if (ndist < 0) return ndist;
  ndist += 1;
  int ncode = s.bits(4);
  if (ncode < 0) return ncode;
  ncode += 4;
  if (nlen > kMaxLCodes || ndist > kMaxDCodes) return -7;
  for (int index = 0; index < ncode; ++index) {
    int v = s.bits(3);
    if (v < 0) return v;
    lengths[order[index]] = static_cast<short>(v);
  }
  for (int index = ncode; index < 19; ++index) lengths[order[index]] = 0;
  int err = construct(lencode, lengths, 19);
  if (err != 0) return -8;
  int index = 0;
  while (index < nlen + ndist) {
    int symbol = decode(s, lencode);
    if (symbol < 0) return symbol;
    if (symbol < 16) {
      lengths[index++] = static_cast<short>(symbol);
    } else {
      int len = 0;
      int repeat = 0;
      if (symbol == 16) {
        if (index == 0) return -9;
        len = lengths[index - 1];
        repeat = s.bits(2);
        if (repeat < 0) return repeat;
        repeat += 3;
      } else if (symbol == 17) {
        repeat = s.bits(3);
        if (repeat < 0) return repeat;
        repeat += 3;
      } else {
        repeat = s.bits(7);
        if (repeat < 0) return repeat;
        repeat += 11;
      }
      if (index + repeat > nlen + ndist) return -9;
      while (repeat-- > 0) lengths[index++] = static_cast<short>(len);
    }
  }
  if (lengths[256] == 0) return -9;
  err = construct(lencode, lengths, nlen);
  if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return -7;
  err = construct(distcode, lengths + nlen, ndist);
  if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return -7;
  return codes(s, lencode, distcode);
}

const char* describe(int code) {
  switch (code) {
    case -1: return "输入数据提前结束";
    case -4: return "非法的块类型";
    case -5: return "存储块长度不足";
    case -6: return "存储块长度校验失败";
    case -7: return "Huffman 码表数量非法";
    case -8: return "Huffman 码表不完整";
    case -9: return "重复码长度非法";
    case -10: return "非法的长度/距离符号";
    case -11: return "距离超出已解压窗口";
    case -12: return "解压后体积超过上限";
    default: return "未知解压错误";
  }
}

}  // namespace

bool raw_deflate(const char* data, size_t size, std::string& out, std::string* error,
                 size_t max_output) {
  State s;
  s.in = reinterpret_cast<const unsigned char*>(data);
  s.inlen = size;
  s.out = &out;
  s.limit = max_output;
  out.clear();
  int err = 0;
  int last = 0;
  do {
    last = s.bits(1);
    if (last < 0) {
      err = last;
      break;
    }
    int type = s.bits(2);
    if (type < 0) {
      err = type;
      break;
    }
    if (type == 0)
      err = stored(s);
    else if (type == 1)
      err = fixed(s);
    else if (type == 2)
      err = dynamic(s);
    else
      err = -4;
    if (err != 0) break;
  } while (!last);
  if (err != 0) {
    if (error) *error = describe(err);
    return false;
  }
  return true;
}

bool zlib(const char* data, size_t size, std::string& out, std::string* error, size_t max_output) {
  if (size < 6) {
    if (error) *error = "zlib 数据过短";
    return false;
  }
  const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
  unsigned cmf = p[0], flg = p[1];
  if ((cmf & 0x0F) != 8 || ((cmf << 8) | flg) % 31 != 0) {
    if (error) *error = "zlib 头部非法";
    return false;
  }
  if (!raw_deflate(data + 2, size - 6, out, error, max_output)) return false;
  if (size >= 6) {
    uint32_t expected = (static_cast<uint32_t>(p[size - 4]) << 24) |
                        (static_cast<uint32_t>(p[size - 3]) << 16) |
                        (static_cast<uint32_t>(p[size - 2]) << 8) |
                        static_cast<uint32_t>(p[size - 1]);
    if (adler32(out.data(), out.size()) != expected) {
      if (error) *error = "zlib Adler-32 校验失败";
      return false;
    }
  }
  return true;
}

uint32_t adler32(const void* data, size_t len, uint32_t seed) {
  const unsigned char* p = static_cast<const unsigned char*>(data);
  uint32_t a = seed & 0xFFFF, b = (seed >> 16) & 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    a = (a + p[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

}  // namespace mct::inflate_impl
