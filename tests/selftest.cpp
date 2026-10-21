// 基础库自测：mct_selftest <fixtures_dir>
#include <cstdio>
#include <string>

#include "util/fs.hpp"
#include "util/hash.hpp"
#include "util/hash_cache.hpp"
#include "util/inflate.hpp"
#include "util/json.hpp"
#include "util/log.hpp"
#include "util/str.hpp"
#include "util/zip.hpp"
#include "meta/version.hpp"
#include "auth/microsoft.hpp"
#include "net/http.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("[FAIL] %s\n", what.c_str());
    ++g_failures;
  }
}

void check_eq(const std::string& got, const std::string& want, const std::string& what) {
  if (got != want) {
    std::printf("[FAIL] %s\n  期望: %s\n  实际: %s\n", what.c_str(), want.c_str(), got.c_str());
    ++g_failures;
  }
}

void test_hash() {
  check_eq(mct::hash::md5_hex(""), "d41d8cd98f00b204e9800998ecf8427e", "md5 空串");
  check_eq(mct::hash::md5_hex("abc"), "900150983cd24fb0d6963f7d28e17f72", "md5 abc");
  check_eq(mct::hash::sha1_hex(""), "da39a3ee5e6b4b0d3255bfef95601890afd80709", "sha1 空串");
  check_eq(mct::hash::sha1_hex("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1 abc");
  check_eq(mct::hash::sha1_hex("The quick brown fox jumps over the lazy dog"),
           "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12", "sha1 fox");
  std::string million(1000000, 'a');
  check_eq(mct::hash::sha1_hex(million), "34aa973cd4c4daa4f61eeb2bdbad27316534016f",
           "sha1 一百万 a");
  check_eq(mct::hash::md5_hex(million), "7707d6ae4e027c70eea2a935c2296f21", "md5 一百万 a");

  std::string uuid = mct::hash::offline_uuid("Steve");
  check(uuid.size() == 36 && uuid[8] == '-' && uuid[13] == '-', "离线 UUID 格式");
  // UUID v3 的版本位固定为 3
  check(uuid[14] == '3', "离线 UUID 版本位");
  check_eq(mct::hash::uuid_with_dashes("069a79f444e94726a5befca90e38aaf5"),
           "069a79f4-44e9-4726-a5be-fca90e38aaf5", "UUID 加连字符");
}

void test_str() {
  check_eq(mct::str::trim("  hi \t"), "hi", "trim");
  check_eq(mct::str::join(mct::str::split("a,b,,c", ','), "|"), "a|b||c", "split/join");
  check_eq(mct::str::ellipsize("abcdef", 4), "abc…", "ellipsize");
  check(mct::str::display_width("你好abc") == 7, "显示宽度 CJK");
  check(mct::str::display_width("abc") == 3, "显示宽度 ASCII");
  check(mct::str::pad("中", 4) == "中  ", "pad CJK");
  check_eq(mct::str::percent_encode("a b/c"), "a%20b%2Fc", "url 编码");
  check_eq(mct::str::human_size(1536), "1.5 KB", "human_size");
  std::vector<std::string> lines = mct::str::wrap("这是一个很长的中文句子用来测试换行", 10);
  check(lines.size() >= 2, "中文换行");
  for (const auto& line : lines) check(mct::str::display_width(line) <= 10, "换行宽度");
}

void test_json() {
  std::string text = R"({"a":1,"b":[true,false,null,"x\n y"],"c":{"d":2.5},"u":"\u4f60\u597d"})";
  std::string err;
  mct::Json j = mct::Json::parse(text, &err);
  check(err.empty(), "JSON 解析错误: " + err);
  check(j.get_int("a") == 1, "JSON 整数");
  check(j["b"].size() == 4, "JSON 数组长度");
  check(j["b"][0].as_bool(), "JSON 布尔");
  check(j["b"][2].is_null(), "JSON null");
  check_eq(j["b"][3].as_string(), "x\n y", "JSON 转义");
  check(mct::str::starts_with(j.dump(), "{\"a\":1,"), "JSON 紧凑输出: " + j.dump());
  check_eq(j.get_str("u"), "你好", "JSON unicode 转义");
  check_eq(j.get_str("missing", "def"), "def", "JSON 默认值");
  check(j["c"].get_num("d") == 2.5, "JSON 小数");

  mct::Json round = mct::Json::parse(j.dump(2), &err);
  check(err.empty(), "JSON 格式化再解析: " + err);
  check(round == j, "JSON 往返一致");

  mct::Json bad = mct::Json::parse("{\"a\":}", &err);
  check(!err.empty(), "JSON 错误检测");
  (void)bad;
}

void test_meta() {
  using mct::Json;
  std::string err;
  mct::meta::OsInfo os = mct::meta::current_os();
  std::string os_name = os.name;

  Json base = Json::parse(R"({
    "id":"1.20.1","type":"release","mainClass":"net.minecraft.client.main.Main",
    "javaVersion":{"majorVersion":17},"assets":"5",
    "assetIndex":{"id":"5","sha1":"x","size":1,"url":"https://piston-meta.mojang.com/5.json"},
    "downloads":{"client":{"url":"https://piston-data.mojang.com/v1/objects/a/client.jar","sha1":"abc","size":100}},
    "arguments":{"jvm":["-Djava.library.path=${natives_directory}","-cp","${classpath}"],
                 "game":["--username","${auth_player_name}"]},
    "libraries":[{"name":"com.example:lib:1.0","downloads":{"artifact":{"url":"https://libraries.minecraft.net/com/example/lib/1.0/lib-1.0.jar","path":"com/example/lib/1.0/lib-1.0.jar","sha1":"s","size":1}}}]
  })", &err);
  check(err.empty(), "基础版本 JSON 解析");
  Json addon = Json::parse(R"({
    "id":"1.20.1-fabric","inheritsFrom":"1.20.1",
    "mainClass":"net.fabricmc.loader.impl.launch.knot.KnotClient",
    "libraries":[{"name":"net.fabricmc:fabric-loader:0.15.11","downloads":{"artifact":{"url":"https://maven.fabricmc.net/f.jar","path":"net/fabricmc/fabric-loader/0.15.11/fabric-loader-0.15.11.jar","sha1":"y","size":2}}}],
    "arguments":{"jvm":["-DFabricMcEmu=net.minecraft.client.main.Main"],
                 "game":[{"rules":[{"action":"allow","os":{"name":"WINDOWS_PLACEHOLDER"}}],"value":"--fabric"}]}
  })", &err);
  addon["arguments"]["game"][0]["rules"][0]["os"]["name"] = os_name;

  Json merged = mct::meta::merge_version_json(base, addon);
  check(!merged.has("inheritsFrom"), "合并后移除 inheritsFrom");
  check(merged["libraries"].size() == 2, "合并后保留双方库");
  check(mct::str::contains(merged.get_str("mainClass"), "KnotClient"), "加载器 mainClass 覆盖原版");
  check(merged["arguments"]["jvm"].size() == 4, "合并后 JVM 参数拼接");

  mct::meta::VersionMeta meta;
  check(mct::meta::VersionMeta::parse(merged, meta, &err), "合并 JSON 可解析: " + err);
  check(meta.libraries.size() == 2, "元数据库数量");
  check(meta.java_major == 17, "Java 版本继承");
  check(meta.libraries[0].name.find("fabric-loader") != std::string::npos, "加载器库排在前面");

  auto allow_rule = mct::Json::parse(
      "[{\"action\":\"allow\",\"os\":{\"name\":\"" + os_name + "\"}}]", &err);
  check(mct::meta::rules_allow(allow_rule, os), "规则：当前系统 allow");
  auto deny_rule = mct::Json::parse(
      "[{\"action\":\"allow\",\"os\":{\"name\":\"not_this_os\"}}]", &err);
  check(!mct::meta::rules_allow(deny_rule, os), "规则：其它系统不匹配");
  auto disallow = mct::Json::parse(
      "[{\"action\":\"allow\"},{\"action\":\"disallow\",\"os\":{\"name\":\"" + os_name + "\"}}]",
      &err);
  check(!mct::meta::rules_allow(disallow, os), "规则：disallow 优先");

  std::map<std::string, std::string> subs;
  subs["auth_player_name"] = "Steve";
  check_eq(mct::meta::substitute_vars("--username ${auth_player_name}", subs),
           "--username Steve", "变量替换");
  check_eq(mct::meta::substitute_vars("${unknown}", subs), "${unknown}", "未知变量保留");

  Json args = Json::parse(
      "[\"--base\",{\"rules\":[{\"action\":\"allow\",\"os\":{\"name\":\"" + os_name +
          "\"}}],\"value\":\"--allowed\"},"
          "{\"rules\":[{\"action\":\"disallow\",\"os\":{\"name\":\"" + os_name +
          "\"}}],\"value\":\"--denied\"}]",
      &err);
  std::vector<std::string> built = mct::meta::build_args(args, os, subs);
  bool has_allowed = false;
  bool has_denied = false;
  for (const auto& value : built) {
    if (value == "--allowed") has_allowed = true;
    if (value == "--denied") has_denied = true;
  }
  check(has_allowed && !has_denied, "参数规则过滤");

  Json native_lib = Json::parse(
      R"({"name":"org.lwjgl:lwjgl:3.3.1:natives-windows","downloads":{"artifact":{"path":"org/lwjgl/lwjgl/3.3.1/lwjgl-3.3.1-natives-windows.jar","url":"https://libraries.minecraft.net/n.jar","sha1":"z","size":3}}})",
      &err);
  mct::meta::Library parsed;
  check(mct::meta::Library::parse(native_lib, os, parsed), "Library 解析");
  check(parsed.is_native, "natives 分类识别");
  check(parsed.relative_path() ==
            "org/lwjgl/lwjgl/3.3.1/lwjgl-3.3.1-natives-windows.jar",
        "Maven 路径构造");

  Json simple_lib = Json::parse(R"({"name":"com.google.guava:guava:31.1-jre"})", &err);
  mct::meta::Library plain;
  check(mct::meta::Library::parse(simple_lib, os, plain), "无 downloads 的库解析");
  check(plain.download.path == "com/google/guava/guava/31.1-jre/guava-31.1-jre.jar",
        "无 downloads 时自动构造路径");
  check(plain.download.url == "https://libraries.minecraft.net/" + plain.download.path,
        "无 downloads 时自动构造 URL");
}

void test_microsoft_auth() {
  check_eq(mct::auth::MicrosoftAuth::builtin_client_id(), "00000000402b5328", "内置公开 Client ID");
  mct::auth::DevicePoll poll;
  check_eq(poll.state_name(), "error", "设备码状态名");
}
void test_copy_and_cache() {
  const std::string base = mct::fs::join(mct::fs::temp_dir(), "mct_selftest_copy");
  mct::fs::remove_dir_recursive(base);
  const std::string src = mct::fs::join(base, "src");
  const std::string dst = mct::fs::join(base, "dst");
  mct::fs::ensure_dir(mct::fs::join(src, "versions", "1.20.1"));
  const std::string big(100000, 'x');
  mct::fs::write_file(mct::fs::join(src, "versions", "1.20.1", "a.txt"), big);
  mct::fs::write_file(mct::fs::join(src, "root.bin"), "hello");

  uint64_t last_done = 0;
  size_t last_files = 0;
  bool ok = mct::fs::copy_dir(
      src, dst,
      [&](const mct::fs::CopyProgress& p) {
        last_done = p.bytes_done;
        last_files = p.files_done;
      },
      nullptr);
  check(ok, "copy_dir 成功");
  check(last_files == 2 && last_done == 100005, "copy_dir 进度回调");
  std::string copied;
  mct::fs::read_file(mct::fs::join(dst, "root.bin"), copied);
  check_eq(copied, "hello", "copy_dir 小文件内容");
  check(mct::fs::file_size(mct::fs::join(dst, "versions", "1.20.1", "a.txt")) == big.size(),
        "copy_dir 大文件大小");

  mct::HashCache cache;
  const std::string hash_file = mct::fs::join(base, "hash.bin");
  mct::fs::write_file(hash_file, "abc");
  const uint64_t size = mct::fs::file_size(hash_file);
  const int64_t mtime = mct::fs::mtime(hash_file);
  const std::string sha = mct::hash::sha1_hex("abc");
  check(cache.verify(hash_file, size, mtime, sha), "哈希缓存首次校验");
  check(cache.verify(hash_file, size, mtime, sha), "哈希缓存命中");
  check(!cache.verify(hash_file, size, mtime, "deadbeef"), "哈希缓存拒绝错误 SHA1");
  mct::fs::write_file(hash_file, "abcd");
  check(!cache.verify(hash_file, 4, mtime, sha), "文件内容变化后重新校验失败");
  cache.remember(hash_file, size, mtime, sha);
  check(cache.size() >= 1, "哈希缓存条目数");

  mct::fs::remove_dir_recursive(base);
}

void test_zip_and_inflate(const std::string& fixtures) {
  // 固定 Huffman 块（距离表 32 个 5 位码）回归测试
  {
    const unsigned char fixed_stream[] = {0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0x07, 0x00};
    std::string fixed_out;
    std::string fixed_error;
    bool fixed_ok = mct::inflate_impl::raw_deflate(
        reinterpret_cast<const char*>(fixed_stream), sizeof(fixed_stream), fixed_out,
        &fixed_error);
    check(fixed_ok, "inflate 固定 Huffman 块: " + fixed_error);
    check_eq(fixed_out, "hello", "固定 Huffman 块内容");
  }

  std::string raw;
  std::string expect;
  if (!mct::fs::read_file(mct::fs::join(fixtures, "sample.deflate"), raw)) {
    std::printf("[SKIP] 缺少 %s\n", mct::fs::join(fixtures, "sample.deflate").c_str());
    return;
  }
  if (!mct::fs::read_file(mct::fs::join(fixtures, "sample.txt"), expect)) {
    std::printf("[FAIL] 缺少 sample.txt\n");
    ++g_failures;
    return;
  }
  std::string out;
  std::string err;
  bool ok = mct::inflate_impl::raw_deflate(raw.data(), raw.size(), out, &err);
  check(ok, "inflate 解压: " + err);
  check_eq(out, expect, "inflate 内容一致");

  std::string zipdata;
  if (mct::fs::read_file(mct::fs::join(fixtures, "sample.zip"), zipdata)) {
    std::vector<mct::zip::Entry> entries;
    check(mct::zip::list(zipdata, entries, &err), "zip 列表: " + err);
    check(!entries.empty(), "zip 条目非空");
    std::string dest = mct::fs::join(mct::fs::temp_dir(), "mct_selftest_zip");
    mct::fs::remove_dir_recursive(dest);
    check(mct::zip::extract_all(zipdata, dest, &err), "zip 解压: " + err);
    std::string got;
    check(mct::fs::read_file(mct::fs::join(dest, "sample.txt"), got), "zip 读取 sample.txt");
    check_eq(got, expect, "zip 内容一致");
    mct::fs::remove_dir_recursive(dest);
  } else {
    std::printf("[SKIP] 缺少 sample.zip\n");
  }
}

}  // namespace

int main(int argc, char** argv) {
  mct::log::init(mct::fs::join(mct::fs::temp_dir(), "mct_selftest.log"));
  test_hash();
  test_str();
  test_json();
  test_meta();
  test_microsoft_auth();
  test_copy_and_cache();
  if (argc > 1) test_zip_and_inflate(argv[1]);
  if (g_failures == 0) {
    std::printf("OK: 全部自测通过\n");
    return 0;
  }
  std::printf("FAILED: %d 项失败\n", g_failures);
  return 1;
}
