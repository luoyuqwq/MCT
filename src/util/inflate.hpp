// DEFLATE / zlib 解压（纯 C++ 实现，无第三方依赖）
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mct::inflate_impl {

// 解压 raw DEFLATE 流
bool raw_deflate(const char* data, size_t size, std::string& out, std::string* error = nullptr,
                 size_t max_output = 512ull * 1024 * 1024);

// 解压 zlib 包装的数据（PNG 等）
bool zlib(const char* data, size_t size, std::string& out, std::string* error = nullptr,
          size_t max_output = 512ull * 1024 * 1024);

// 计算 Adler-32（PNG 校验用）
uint32_t adler32(const void* data, size_t len, uint32_t seed = 1);

}  // namespace mct::inflate_impl
