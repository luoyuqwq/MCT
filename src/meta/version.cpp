#include "meta/version.hpp"

#include <algorithm>
#include <regex>

#include "util/fs.hpp"
#include "util/log.hpp"
#include "util/str.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace mct::meta {

namespace {

std::string substitute(const std::string& text, const std::map<std::string, std::string>& subs) {
  std::string out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t open = text.find("${", pos);
    if (open == std::string::npos) {
      out.append(text, pos, std::string::npos);
      break;
    }
    size_t close = text.find('}', open + 2);
    if (close == std::string::npos) {
      out.append(text, pos, std::string::npos);
      break;
    }
    out.append(text, pos, open - pos);
    std::string key = text.substr(open + 2, close - open - 2);
    auto it = subs.find(key);
    if (it != subs.end())
      out += it->second;
    else
      out.append(text, open, close - open + 1);
    pos = close + 1;
  }
  return out;
}

bool match_rule(const Json& rule, const OsInfo& os, const FeatureSet& features) {
  const Json& os_rule = rule["os"];
  if (os_rule.is_object()) {
    std::string name = os_rule.get_str("name");
    if (!name.empty() && name != os.name) return false;
    std::string arch = os_rule.get_str("arch");
    if (!arch.empty() && arch != os.arch) return false;
    std::string version = os_rule.get_str("version");
    if (!version.empty()) {
      try {
        if (!std::regex_search(os.version, std::regex(version))) return false;
      } catch (const std::exception&) {
        // 正则不合法时忽略该条件
      }
    }
  }
  const Json& feat = rule["features"];
  if (feat.is_object()) {
    for (const auto& kv : feat.members()) {
      bool want = kv.second.as_bool();
      bool have = false;
      if (kv.first == "is_demo_user") have = features.demo_user;
      else if (kv.first == "has_custom_resolution") have = features.custom_resolution;
      else if (kv.first == "has_quick_plays_support" || kv.first == "is_quick_play_singleplayer" ||
               kv.first == "is_quick_play_multiplayer" || kv.first == "is_quick_play_realms")
        have = false;
      if (want != have) return false;
    }
  }
  return true;
}

}  // namespace

OsInfo current_os() {
  OsInfo os;
  os.arch = sizeof(void*) == 8 ? "x86_64" : "x86";
#ifdef _WIN32
  os.name = "windows";
  os.version = "10.0";
#elif defined(__APPLE__)
  os.name = "osx";
  os.version = "13.0";
#else
  os.name = "linux";
  os.version = "6.0";
#endif
  return os;
}

bool rules_allow(const Json& rules, const OsInfo& os, const FeatureSet& features) {
  if (!rules.is_array() || rules.size() == 0) return true;
  bool allow = false;
  for (const Json& rule : rules.items()) {
    if (!rule.is_object()) continue;
    if (!match_rule(rule, os, features)) continue;
    std::string action = rule.get_str("action");
    if (action == "allow")
      allow = true;
    else if (action == "disallow")
      allow = false;
  }
  return allow;
}

std::string Library::relative_path() const {
  std::string dir = str::replace_all(group, ".", "/");
  std::string file = artifact + "-" + version + (classifier.empty() ? "" : "-" + classifier) + "." +
                     extension;
  return dir + "/" + artifact + "/" + version + "/" + file;
}

std::string Library::fallback_url() const {
  return "https://libraries.minecraft.net/" + relative_path();
}

bool Library::parse(const Json& j, const OsInfo& os, Library& out) {
  out = Library();
  out.name = j.get_str("name");
  if (out.name.empty()) return false;
  std::string coords = out.name;
  size_t at = coords.find('@');
  if (at != std::string::npos) {
    out.extension = coords.substr(at + 1);
    coords = coords.substr(0, at);
  }
  std::vector<std::string> parts = str::split(coords, ':');
  if (parts.size() < 3) return false;
  out.group = parts[0];
  out.artifact = parts[1];
  out.version = parts[2];
  if (parts.size() >= 4) out.classifier = parts[3];
  if (parts.size() >= 5 && !parts[4].empty()) out.extension = parts[4];

  const Json& downloads = j["downloads"];
  const Json& artifact_dl = downloads["artifact"];
  if (artifact_dl.is_object()) {
    out.download.url = artifact_dl.get_str("url");
    out.download.path = artifact_dl.get_str("path");
    out.download.sha1 = artifact_dl.get_str("sha1");
    out.download.size = static_cast<uint64_t>(artifact_dl.get_int("size", 0));
  }

  const Json& natives = j["natives"];
  if (natives.is_object()) {
    std::string classifier = natives.get_str(os.name);
    if (classifier.empty() && os.name == "osx") classifier = natives.get_str("macos");
    if (!classifier.empty()) {
      classifier = str::replace_all(classifier, "${arch}", sizeof(void*) == 8 ? "64" : "32");
      const Json& cls = downloads["classifiers"][classifier];
      if (cls.is_object()) {
        out.download.url = cls.get_str("url");
        out.download.path = cls.get_str("path");
        out.download.sha1 = cls.get_str("sha1");
        out.download.size = static_cast<uint64_t>(cls.get_int("size", 0));
      }
      out.classifier = classifier;
      out.natives_key = classifier;
      out.is_native = true;
    }
  } else if (str::starts_with(out.classifier, "natives")) {
    out.is_native = true;
    out.natives_key = out.classifier;
  }

  if (out.download.path.empty()) out.download.path = out.relative_path();
  if (out.download.url.empty() && !out.is_native) out.download.url = out.fallback_url();
  if (out.is_native && out.download.url.empty()) {
    out.download.url = "https://libraries.minecraft.net/" + out.download.path;
  }
  const Json& exclude = j["extract"]["exclude"];
  for (const Json& item : exclude.items()) {
    std::string s = item.as_string();
    if (!s.empty()) out.extract_exclude.push_back(s);
  }
  return true;
}

bool VersionMeta::parse(const Json& j, VersionMeta& out, std::string* error) {
  if (!j.is_object()) {
    if (error) *error = "版本 JSON 不是对象";
    return false;
  }
  OsInfo os = current_os();
  out = VersionMeta();
  out.raw = j;
  out.id = j.get_str("id");
  out.type = j.get_str("type", "release");
  out.main_class = j.get_str("mainClass");
  out.assets_id = j.get_str("assets", "legacy");
  if (j["assetIndex"].is_object()) {
    out.asset_index.url = j["assetIndex"].get_str("url");
    out.asset_index.sha1 = j["assetIndex"].get_str("sha1");
    out.asset_index.size = static_cast<uint64_t>(j["assetIndex"].get_int("size", 0));
    out.asset_index.path = j["assetIndex"].get_str("id", out.assets_id);
    if (j["assetIndex"].has("id")) out.assets_id = j["assetIndex"].get_str("id");
  }
  if (j["downloads"]["client"].is_object()) {
    out.client.url = j["downloads"]["client"].get_str("url");
    out.client.sha1 = j["downloads"]["client"].get_str("sha1");
    out.client.size = static_cast<uint64_t>(j["downloads"]["client"].get_int("size", 0));
  }
  out.java_major = static_cast<int>(j["javaVersion"].get_int("majorVersion", 8));
  out.jvm_args = j["arguments"]["jvm"];
  out.game_args = j["arguments"]["game"];
  out.legacy_game_args = j.get_str("minecraftArguments");
  if (j["logging"]["client"].is_object()) {
    const Json& logging = j["logging"]["client"];
    out.logging_argument = logging.get_str("argument");
    const Json& file = logging["file"];
    out.logging_file.url = file.get_str("url");
    out.logging_file.sha1 = file.get_str("sha1");
    out.logging_file.size = static_cast<uint64_t>(file.get_int("size", 0));
    out.logging_file.path = file.get_str("id");
  }
  for (const Json& lib : j["libraries"].items()) {
    if (!rules_allow(lib["rules"], os)) continue;
    Library parsed;
    if (Library::parse(lib, os, parsed)) out.libraries.push_back(std::move(parsed));
  }
  if (out.main_class.empty()) {
    if (error) *error = "版本 JSON 缺少 mainClass（可能是不完整的加载器配置）";
    return false;
  }
  return true;
}

std::vector<std::string> VersionMeta::classpath_libraries() const {
  std::vector<std::string> out;
  for (const auto& lib : libraries) {
    if (!lib.is_native) out.push_back(lib.download.path);
  }
  return out;
}

Json merge_version_json(const Json& base, const Json& addon) {
  Json result = base;
  if (addon.has("id")) result["id"] = addon["id"];
  if (addon.has("mainClass")) result["mainClass"] = addon["mainClass"];
  if (addon.has("assets")) result["assets"] = addon["assets"];
  if (addon.has("javaVersion")) result["javaVersion"] = addon["javaVersion"];
  if (addon.has("assetIndex")) result["assetIndex"] = addon["assetIndex"];

  Json libs = Json::array();
  std::vector<std::string> seen;
  auto add_libraries = [&](const Json& list) {
    for (const Json& item : list.items()) {
      std::string name = item.get_str("name");
      if (name.empty()) continue;
      if (std::find(seen.begin(), seen.end(), name) != seen.end()) continue;
      seen.push_back(name);
      libs.push_back(item);
    }
  };
  add_libraries(addon["libraries"]);
  add_libraries(base["libraries"]);
  result["libraries"] = libs;

  Json jvm = Json::array();
  Json game = Json::array();
  for (const Json& v : base["arguments"]["jvm"].items()) jvm.push_back(v);
  for (const Json& v : addon["arguments"]["jvm"].items()) jvm.push_back(v);
  for (const Json& v : base["arguments"]["game"].items()) game.push_back(v);
  for (const Json& v : addon["arguments"]["game"].items()) game.push_back(v);
  if (jvm.size() > 0 || game.size() > 0) {
    Json args = Json::object();
    if (jvm.size() > 0) args["jvm"] = jvm;
    if (game.size() > 0) args["game"] = game;
    result["arguments"] = args;
  }
  if (addon.has("minecraftArguments")) result["minecraftArguments"] = addon["minecraftArguments"];
  result.erase("inheritsFrom");
  return result;
}

bool load_version_json(const Paths& paths, const std::string& id, Json& out, std::string* error) {
  Json current;
  std::string err;
  if (!Json::parse_file(paths.instance_json(id), current, &err)) {
    if (error) *error = "读取版本 JSON 失败: " + err;
    return false;
  }
  int depth = 0;
  std::string parent_id = current.get_str("inheritsFrom");
  while (!parent_id.empty()) {
    if (++depth > 5) {
      if (error) *error = "版本继承层级过深";
      return false;
    }
    Json parent;
    if (!Json::parse_file(paths.instance_json(parent_id), parent, &err)) {
      if (error) *error = "缺少父版本 " + parent_id + "，请重新安装";
      return false;
    }
    current = merge_version_json(parent, current);
    parent_id = current.get_str("inheritsFrom");
  }
  out = current;
  return true;
}

bool load_version_meta(const Paths& paths, const std::string& id, VersionMeta& out,
                       std::string* error) {
  Json json;
  if (!load_version_json(paths, id, json, error)) return false;
  return VersionMeta::parse(json, out, error);
}

std::string substitute_vars(const std::string& text,
                            const std::map<std::string, std::string>& subs) {
  return substitute(text, subs);
}

std::vector<std::string> build_args(const Json& arg_array, const OsInfo& os,
                                    const std::map<std::string, std::string>& subs,
                                    const FeatureSet& features) {
  std::vector<std::string> out;
  if (!arg_array.is_array()) return out;
  for (const Json& item : arg_array.items()) {
    if (item.is_string()) {
      out.push_back(substitute(item.as_string(), subs));
      continue;
    }
    if (!item.is_object()) continue;
    if (!rules_allow(item["rules"], os, features)) continue;
    const Json& value = item["value"];
    if (value.is_string()) {
      out.push_back(substitute(value.as_string(), subs));
    } else if (value.is_array()) {
      for (const Json& v : value.items()) out.push_back(substitute(v.as_string(), subs));
    }
  }
  return out;
}

bool instance_exists(const Paths& paths, const std::string& id) {
  return fs::is_file(paths.instance_json(id));
}

std::vector<std::string> list_instances(const Paths& paths) {
  std::vector<std::string> out;
  for (const auto& entry : fs::list_dir(paths.versions_dir())) {
    if (!entry.is_dir) continue;
    if (fs::is_file(fs::join(paths.versions_dir(), entry.name, entry.name + ".json")))
      out.push_back(entry.name);
  }
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    return str::lower(a) > str::lower(b);
  });
  return out;
}

Json load_instance_record(const Paths& paths, const std::string& id) {
  Json record;
  std::string err;
  Json::parse_file(fs::join(paths.instance_dir(id), "mct.json"), record, &err);
  return record;
}

}  // namespace mct::meta
