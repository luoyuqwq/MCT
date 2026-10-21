// 皮肤服务：查询 / 上传 / 恢复默认
#pragma once

#include <string>

#include "net/http.hpp"

namespace mct::auth {

struct SkinInfo {
  std::string url;
  std::string variant;
};

class SkinService {
 public:
  explicit SkinService(net::HttpClient& http);

  bool fetch(const std::string& mc_token, SkinInfo& info, std::string* error);
  bool upload(const std::string& mc_token, const std::string& png_path, const std::string& variant,
              std::string* error);
  bool reset(const std::string& mc_token, std::string* error);

  // 校验皮肤 PNG（64x64 或 64x32）
  static bool validate_png(const std::string& data, int& width, int& height, std::string* error);

 private:
  net::HttpClient& http_;
};

}  // namespace mct::auth
