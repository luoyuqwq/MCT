#include "meta/mirror.hpp"

#include "net/http.hpp"
#include "util/fs.hpp"
#include "util/json.hpp"
#include "util/log.hpp"
#include "util/str.hpp"

namespace mct::meta {

namespace {
const char* kOfficialManifest = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
const char* kBmclapiBase = "https://bmclapi2.bangbang93.com";

std::string trim_base(std::string s) {
  while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
  return s;
}
}  // namespace

Mirror::Mirror(GameSource source, std::string custom_base, std::string cache_dir)
    : source_(source), base_(trim_base(std::move(custom_base))), cache_dir_(std::move(cache_dir)) {
  if (source_ == GameSource::Custom) {
    name_ = "自定义站点";
    add_default_rules();
  } else if (source_ == GameSource::Bmclapi) {
    name_ = "BMCLAPI 国内镜像";
  } else {
    name_ = "Mojang 官方";
  }
}

void Mirror::add_default_rules() {
  // 与 tools/mct-publish.ps1 生成的目录结构保持一致
  rules_ = {
      {"https://libraries.minecraft.net/", "/maven/"},
      {"https://resources.download.minecraft.net/", "/assets/"},
      {"https://piston-data.mojang.com/", "/data/"},
      {"https://piston-meta.mojang.com/", "/meta/"},
      {"https://launchermeta.mojang.com/", "/meta/"},
      {"https://launcher.mojang.com/", "/data/"},
      {"https://maven.minecraftforge.net/", "/maven/"},
      {"https://files.minecraftforge.net/maven/", "/maven/"},
      {"https://maven.fabricmc.net/", "/fabric/maven/"},
      {"https://meta.fabricmc.net/", "/fabric/meta/"},
      {"https://maven.neoforged.net/releases/", "/neoforge/maven/"},
      {"https://meta.quiltmc.org/", "/quilt/meta/"},
  };
}

std::string Mirror::resolve_rule_target(const std::string& target) const {
  if (str::starts_with(target, "http://") || str::starts_with(target, "https://")) return target;
  return base_ + (target.empty() || target.front() == '/' ? "" : "/") + target;
}

void Mirror::init(net::HttpClient& http) {
  if (source_ != GameSource::Custom || base_.empty()) return;
  net::HttpResult result = http.get(base_ + "/mirror.json");
  if (!result.ok()) {
    log::info("自定义站点未提供 mirror.json，使用内置路径规则");
    return;
  }
  std::string err;
  Json j = Json::parse(result.body, &err);
  if (!err.empty() || !j.is_object()) {
    log::warn("mirror.json 解析失败: " + err);
    return;
  }
  if (j.has("name")) name_ = j.get_str("name", name_);
  if (j.has("manifest")) manifest_path_ = j.get_str("manifest", manifest_path_);
  if (j.has("version_json")) version_json_path_ = j.get_str("version_json", version_json_path_);
  const Json& rules = j["rules"];
  if (rules.is_array() && rules.size() > 0) {
    rules_.clear();
    for (size_t i = 0; i < rules.size(); ++i) {
      MirrorRule rule;
      rule.from = rules[i].get_str("from");
      rule.to = rules[i].get_str("to");
      if (!rule.from.empty() && !rule.to.empty()) rules_.push_back(rule);
    }
  }
  custom_loaded_ = true;
  log::info("已加载自定义镜像站配置: " + name_);
}

std::string Mirror::manifest_url() const {
  switch (source_) {
    case GameSource::Official: return kOfficialManifest;
    case GameSource::Bmclapi: return std::string(kBmclapiBase) + "/mc/game/version_manifest_v2.json";
    case GameSource::Custom:
      return base_.empty() ? kOfficialManifest : base_ + "/" + manifest_path_;
  }
  return kOfficialManifest;
}

std::string Mirror::version_json_url(const std::string& id,
                                     const std::string& official_fallback) const {
  switch (source_) {
    case GameSource::Official:
      return official_fallback;
    case GameSource::Bmclapi:
      return std::string(kBmclapiBase) + "/version/" + id + "/json";
    case GameSource::Custom:
      if (base_.empty()) return official_fallback;
      return base_ + "/" + str::replace_all(version_json_path_, "{id}", id);
  }
  return official_fallback;
}

std::string Mirror::rewrite(const std::string& official) const {
  if (source_ == GameSource::Official || official.empty()) return official;
  if (source_ == GameSource::Bmclapi) {
    static const std::vector<MirrorRule> rules = {
        {"https://launchermeta.mojang.com/", "https://bmclapi2.bangbang93.com/"},
        {"https://piston-meta.mojang.com/", "https://bmclapi2.bangbang93.com/"},
        {"https://launcher.mojang.com/", "https://bmclapi2.bangbang93.com/"},
        {"https://piston-data.mojang.com/", "https://bmclapi2.bangbang93.com/"},
        {"https://libraries.minecraft.net/", "https://bmclapi2.bangbang93.com/maven/"},
        {"https://resources.download.minecraft.net/", "https://bmclapi2.bangbang93.com/assets/"},
        {"https://maven.minecraftforge.net/", "https://bmclapi2.bangbang93.com/maven/"},
        {"https://files.minecraftforge.net/maven/", "https://bmclapi2.bangbang93.com/maven/"},
        {"https://maven.fabricmc.net/", "https://bmclapi2.bangbang93.com/maven/"},
        {"https://meta.fabricmc.net/", "https://bmclapi2.bangbang93.com/fabric-meta/"},
        {"https://maven.neoforged.net/releases/", "https://bmclapi2.bangbang93.com/maven/"},
    };
    for (const auto& rule : rules) {
      if (str::starts_with(official, rule.from))
        return rule.to + official.substr(rule.from.size());
    }
    return official;
  }
  // 自定义站点
  for (const auto& rule : rules_) {
    if (str::starts_with(official, rule.from)) {
      return resolve_rule_target(rule.to) + official.substr(rule.from.size());
    }
  }
  return official;
}

std::vector<std::string> Mirror::candidates(const std::string& official) const {
  std::vector<std::string> out;
  if (source_ == GameSource::Official) {
    out.push_back(official);
    return out;
  }
  std::string mirrored = rewrite(official);
  if (!mirrored.empty() && mirrored != official) out.push_back(mirrored);
  out.push_back(official);
  return out;
}

std::vector<std::string> Mirror::mod_file_candidates(const std::string& kind,
                                                     const std::string& file_name) const {
  std::vector<std::string> out;
  if (source_ == GameSource::Custom && !base_.empty()) {
    out.push_back(base_ + "/mods/" + kind + "/" + file_name);
  }
  return out;
}

std::string Mirror::name() const {
  if (source_ == GameSource::Custom && !name_.empty()) return name_;
  return name_;
}

}  // namespace mct::meta
