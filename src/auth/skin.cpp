#include "auth/skin.hpp"

#include <cstdint>
#include <cstring>

#include "util/fs.hpp"
#include "util/hash.hpp"
#include "util/json.hpp"
#include "util/str.hpp"

namespace mct::auth {

namespace {
const char* kProfileUrl = "https://api.minecraftservices.com/minecraft/profile";
const char* kSkinUrl = "https://api.minecraftservices.com/minecraft/profile/skins";
const char* kActiveSkinUrl = "https://api.minecraftservices.com/minecraft/profile/skins/active";

uint32_t read_be32(const unsigned char* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

std::string describe(const net::HttpResult& result) {
  if (!result.error.empty()) return result.error;
  std::string message = str::fmt("HTTP %ld", result.status);
  if (!result.body.empty()) {
    std::string err;
    Json j = Json::parse(result.body, &err);
    if (err.empty() && j.is_object() && j.has("errorMessage"))
      message += " " + j.get_str("errorMessage");
  }
  return message;
}
}  // namespace

SkinService::SkinService(net::HttpClient& http) : http_(http) {}

bool SkinService::validate_png(const std::string& data, int& width, int& height, std::string* error) {
  static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (data.size() < 33 ||
      std::memcmp(data.data(), signature, 8) != 0) {
    if (error) *error = "不是有效的 PNG 文件";
    return false;
  }
  const unsigned char* p = reinterpret_cast<const unsigned char*>(data.data());
  if (std::memcmp(p + 12, "IHDR", 4) != 0) {
    if (error) *error = "PNG 缺少 IHDR 块";
    return false;
  }
  width = static_cast<int>(read_be32(p + 16));
  height = static_cast<int>(read_be32(p + 20));
  if (width != 64 || (height != 64 && height != 32)) {
    if (error)
      *error = str::fmt("皮肤尺寸必须为 64x64 或 64x32，当前为 %dx%d", width, height);
    return false;
  }
  return true;
}

bool SkinService::fetch(const std::string& mc_token, SkinInfo& info, std::string* error) {
  net::HttpResult result =
      http_.request("GET", kProfileUrl, "", {{"Authorization", "Bearer " + mc_token}}, nullptr, "");
  if (!result.ok()) {
    if (error) *error = "获取皮肤失败: " + describe(result);
    return false;
  }
  std::string err;
  Json j = Json::parse(result.body, &err);
  if (!err.empty()) {
    if (error) *error = "档案响应解析失败";
    return false;
  }
  info.url.clear();
  info.variant.clear();
  const Json& skins = j["skins"];
  if (skins.size() > 0) {
    info.url = skins[0].get_str("url");
    info.variant = skins[0].get_str("variant", "classic");
  }
  return true;
}

bool SkinService::upload(const std::string& mc_token, const std::string& png_path,
                         const std::string& variant, std::string* error) {
  std::string png;
  if (!fs::read_file(png_path, png)) {
    if (error) *error = "无法读取文件: " + png_path;
    return false;
  }
  int width = 0, height = 0;
  if (!validate_png(png, width, height, error)) return false;
  if (png.size() > 1024 * 1024) {
    if (error) *error = "皮肤文件过大（上限 1 MB）";
    return false;
  }
  std::string boundary = "----MCTBoundary" + hash::random_hex(8);
  std::string body;
  body.reserve(png.size() + 512);
  body += "--" + boundary + "\r\n";
  body += "Content-Disposition: form-data; name=\"variant\"\r\n\r\n";
  body += variant;
  body += "\r\n--" + boundary + "\r\n";
  body += "Content-Disposition: form-data; name=\"file\"; filename=\"skin.png\"\r\n";
  body += "Content-Type: image/png\r\n\r\n";
  body += png;
  body += "\r\n--" + boundary + "--\r\n";

  net::HttpResult result = http_.put(
      kSkinUrl, body, "multipart/form-data; boundary=" + boundary,
      {{"Authorization", "Bearer " + mc_token}, {"Accept", "application/json"}});
  if (!result.ok()) {
    if (error) *error = "上传皮肤失败: " + describe(result);
    return false;
  }
  return true;
}

bool SkinService::reset(const std::string& mc_token, std::string* error) {
  net::HttpResult result = http_.request(
      "DELETE", kActiveSkinUrl, "", {{"Authorization", "Bearer " + mc_token}}, nullptr, "");
  if (!result.ok()) {
    if (error) *error = "恢复默认皮肤失败: " + describe(result);
    return false;
  }
  return true;
}

}  // namespace mct::auth
