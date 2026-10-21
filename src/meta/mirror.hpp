// 下载源 / 镜像站：官方、BMCLAPI、自定义站点（可从站点 mirror.json 读取重写规则）
#pragma once

#include <string>
#include <vector>

#include "core/config.hpp"

namespace mct::net {
class HttpClient;
}

namespace mct::meta {

struct MirrorRule {
  std::string from;
  std::string to;
};

class Mirror {
 public:
  Mirror(GameSource source, std::string custom_base, std::string cache_dir);

  // 自定义站点：尝试读取 <base>/mirror.json（失败则用内置默认规则）
  void init(net::HttpClient& http);

  std::string manifest_url() const;
  std::string version_json_url(const std::string& id,
                               const std::string& official_fallback = "") const;
  // 官方 URL -> 镜像 URL；无法重写时返回原 URL
  std::string rewrite(const std::string& official) const;
  // 镜像优先、官方兜底的候选列表
  std::vector<std::string> candidates(const std::string& official) const;
  // 自定义站点是否提供模组文件（用于“上传到我的网站”场景）
  std::vector<std::string> mod_file_candidates(const std::string& kind,
                                               const std::string& file_name) const;

  std::string name() const;
  const std::string& base() const { return base_; }
  GameSource source() const { return source_; }
  bool custom_loaded() const { return custom_loaded_; }

 private:
  void add_default_rules();
  std::string resolve_rule_target(const std::string& target) const;

  GameSource source_;
  std::string base_;
  std::string cache_dir_;
  std::string name_;
  std::string manifest_path_ = "mc/game/version_manifest_v2.json";
  std::string version_json_path_ = "version/{id}/json";
  std::vector<MirrorRule> rules_;
  bool custom_loaded_ = false;
};

}  // namespace mct::meta
