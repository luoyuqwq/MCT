#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "app/app.hpp"
#include "auth/accounts.hpp"
#include "core/config.hpp"
#include "launch/java_install.hpp"
#include "launch/launcher.hpp"
#include "meta/installer.hpp"
#include "meta/mirror.hpp"
#include "net/downloader.hpp"
#include "net/http.hpp"
#include "util/fs.hpp"
#include "util/hash_cache.hpp"
#include "util/log.hpp"
#include "util/process.hpp"
#include "util/str.hpp"
#include "version.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

struct Options {
  std::string home;
  std::string game_dir;
  std::string install_version;
  std::string loader = "vanilla";
  std::string loader_version;
  std::string launch_instance;
  int install_java = 0;
};

void print_help() {
  std::printf("%s %s - %s\n", MCT_NAME, MCT_VERSION, MCT_TAGLINE);
  std::printf("Designed By %s\n\n", MCT_DESIGNER);
  std::printf("用法: mct [选项] [数据目录]\n\n");
  std::printf("选项:\n");
  std::printf("  --home <目录>          指定数据目录（默认 %%APPDATA%%\\MCT）\n");
  std::printf("  --game-dir <目录>      指定游戏安装目录（默认 <数据目录>/game）\n");
  std::printf("  --install <版本>       无界面安装指定版本（例如 1.20.1）\n");
  std::printf("  --loader <名称>        加载器：vanilla/fabric/quilt/forge/neoforge\n");
  std::printf("  --loader-version <v>   指定加载器版本，省略则用最新\n");
  std::printf("  --launch <实例>        无界面启动已安装的实例（使用当前账号）\n");
  std::printf("  --install-java <版本>  自动下载安装 Java（8 / 17 / 21）\n");
  std::printf("  --version              显示版本号\n");
  std::printf("  --help                 显示帮助\n\n");
  std::printf("环境变量:\n");
  std::printf("  MCT_HOME               等同 --home\n");
}

mct::meta::LoaderKind parse_loader(const std::string& text) {
  if (text == "fabric") return mct::meta::LoaderKind::Fabric;
  if (text == "quilt") return mct::meta::LoaderKind::Quilt;
  if (text == "forge") return mct::meta::LoaderKind::Forge;
  if (text == "neoforge") return mct::meta::LoaderKind::NeoForge;
  return mct::meta::LoaderKind::Vanilla;
}

int run_headless_install(const mct::Paths& paths, const mct::Config& config,
                         const Options& options) {
  mct::net::HttpClient http;
  if (!config.proxy.empty()) http.set_proxy(config.proxy);
  mct::meta::Mirror mirror(config.game_source, config.custom_mirror, paths.cache_dir());
  mirror.init(http);
  std::printf("下载源: %s\n", mirror.name().c_str());

  mct::meta::GameInstaller installer(paths, config, mirror, http);
  mct::HashCache hash_cache;
  hash_cache.load(mct::fs::join(paths.cache_dir(), "filehashes.json"));
  mct::net::Downloader downloader(
      config.download_threads, [](const mct::net::DownloadStats& stats) {
        if (stats.bytes_total > 0) {
          int percent = static_cast<int>(stats.bytes_done * 100 / stats.bytes_total);
          std::printf("\r  %3d%%  %s / %s  %.1f MB/s  %-40s", percent,
                      mct::str::human_size(stats.bytes_done).c_str(),
                      mct::str::human_size(stats.bytes_total).c_str(),
                      stats.speed / 1024.0 / 1024.0,
                      mct::str::ellipsize(stats.current, 40).c_str());
        } else {
          std::printf("\r  %s  %zu/%zu  %-40s", mct::str::human_size(stats.bytes_done).c_str(),
                      stats.files_done, stats.files_total,
                      mct::str::ellipsize(stats.current, 40).c_str());
        }
        std::fflush(stdout);
      });
  downloader.set_hash_cache(&hash_cache);

  mct::meta::InstallRequest request;
  request.version_id = options.install_version;
  request.loader = parse_loader(options.loader);
  request.loader_version = options.loader_version;
  request.java_path = config.java_path;

  mct::meta::InstallResult result;
  bool ok = installer.install(
      request, downloader,
      [](const std::string& status) { std::printf("\n[*] %s\n", status.c_str()); std::fflush(stdout); },
      result);
  std::printf("\n");
  mct::fs::ensure_dir(paths.cache_dir());
  hash_cache.save(mct::fs::join(paths.cache_dir(), "filehashes.json"));
  if (!ok) {
    std::printf("安装失败: %s\n", result.error.c_str());
    std::printf("日志: %s\n", paths.log_file().c_str());
    return 1;
  }
  std::printf("安装完成: %s\n", result.instance_id.c_str());
  std::printf("目录: %s\n", paths.instance_dir(result.instance_id).c_str());
  return 0;
}

int run_headless_launch(const mct::Paths& paths, const mct::Config& config,
                        const Options& options) {
  mct::auth::AccountStore accounts;
  accounts.load(paths.accounts_file());
  const mct::auth::Account* account = accounts.current();
  if (!account) {
    std::printf("当前没有账号，请先运行 mct 登录（或直接编辑 accounts.json）\n");
    return 1;
  }
  mct::launch::GameLauncher launcher(paths, config);
  std::vector<std::string> argv;
  std::string error;
  if (!launcher.build_command(options.launch_instance, *account, argv, &error)) {
    std::printf("无法启动: %s\n", error.c_str());
    std::printf("日志: %s\n", paths.log_file().c_str());
    return 1;
  }
  const std::string game_dir =
      paths.instance_game_dir(options.launch_instance, config.version_isolation);
  std::printf("启动实例: %s\n", options.launch_instance.c_str());
  std::printf("账号: %s (%s)\n", account->name.c_str(), account->type_label().c_str());
  std::printf("游戏目录: %s\n", game_dir.c_str());
  std::printf("命令: %s\n\n", mct::process::join_args(argv).c_str());
  std::fflush(stdout);

  int exit_code = 0;
  std::string spawn_error;
  if (!mct::process::spawn_and_wait(mct::process::join_args(argv), game_dir, &exit_code,
                                    &spawn_error)) {
    std::printf("启动失败: %s\n", spawn_error.c_str());
    return 1;
  }
  std::printf("游戏退出码: %d\n", exit_code);
  return 0;
}

int run_install_java(const mct::Paths& paths, const mct::Config& config, int major) {
  mct::net::HttpClient http;
  if (!config.proxy.empty()) http.set_proxy(config.proxy);
  mct::HashCache hash_cache;
  hash_cache.load(mct::fs::join(paths.cache_dir(), "filehashes.json"));
  mct::net::Downloader downloader(
      config.download_threads, [](const mct::net::DownloadStats& stats) {
        if (stats.bytes_total > 0) {
          int percent = static_cast<int>(stats.bytes_done * 100 / stats.bytes_total);
          std::printf("\r  %3d%%  %s / %s  %.1f MB/s  %-40s", percent,
                      mct::str::human_size(stats.bytes_done).c_str(),
                      mct::str::human_size(stats.bytes_total).c_str(),
                      stats.speed / 1024.0 / 1024.0,
                      mct::str::ellipsize(stats.current, 40).c_str());
        } else {
          std::printf("\r  %s  %zu/%zu  %-40s", mct::str::human_size(stats.bytes_done).c_str(),
                      stats.files_done, stats.files_total,
                      mct::str::ellipsize(stats.current, 40).c_str());
        }
        std::fflush(stdout);
      });
  downloader.set_hash_cache(&hash_cache);

  mct::launch::JavaInstallOutcome outcome;
  bool ok = mct::launch::install_java(
      paths, major, http, downloader,
      [](const std::string& status) { std::printf("\n[*] %s\n", status.c_str()); std::fflush(stdout); },
      outcome);
  std::printf("\n");
  mct::fs::ensure_dir(paths.cache_dir());
  hash_cache.save(mct::fs::join(paths.cache_dir(), "filehashes.json"));
  if (!ok) {
    std::printf("Java 安装失败: %s\n", outcome.error.c_str());
    std::printf("日志: %s\n", paths.log_file().c_str());
    return 1;
  }
  std::printf("Java %d 安装完成\n", outcome.major);
  std::printf("路径: %s\n", outcome.java_path.c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--version" || arg == "-v") {
      std::printf("%s %s (Designed By %s)\n", MCT_NAME, MCT_VERSION, MCT_DESIGNER);
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      print_help();
      return 0;
    }
    if (arg == "--home" && i + 1 < argc) {
      options.home = argv[++i];
      continue;
    }
    if (mct::str::starts_with(arg, "--home=")) {
      options.home = arg.substr(7);
      continue;
    }
    if (arg == "--game-dir" && i + 1 < argc) {
      options.game_dir = argv[++i];
      continue;
    }
    if (mct::str::starts_with(arg, "--game-dir=")) {
      options.game_dir = arg.substr(11);
      continue;
    }
    if (arg == "--install" && i + 1 < argc) {
      options.install_version = argv[++i];
      continue;
    }
    if (mct::str::starts_with(arg, "--install=")) {
      options.install_version = arg.substr(10);
      continue;
    }
    if (arg == "--loader" && i + 1 < argc) {
      options.loader = argv[++i];
      continue;
    }
    if (arg == "--loader-version" && i + 1 < argc) {
      options.loader_version = argv[++i];
      continue;
    }
    if (arg == "--launch" && i + 1 < argc) {
      options.launch_instance = argv[++i];
      continue;
    }
    if (arg == "--install-java" && i + 1 < argc) {
      options.install_java = std::atoi(argv[++i]);
      continue;
    }
    if (mct::str::starts_with(arg, "--install-java=")) {
      options.install_java = std::atoi(arg.substr(15).c_str());
      continue;
    }
    if (!arg.empty() && arg[0] != '-') {
      options.home = arg;
      continue;
    }
    std::fprintf(stderr, "未知参数: %s（使用 --help 查看帮助）\n", arg.c_str());
    return 2;
  }

  mct::Paths paths = mct::Paths::resolve(options.home);
  mct::fs::ensure_dir(paths.home);
  mct::log::init(paths.log_file());
#ifdef _WIN32
  SetConsoleTitleW(mct::str::widen(std::string(MCT_NAME) + " Designed By " + MCT_DESIGNER +
                                   " - " + MCT_TAGLINE)
                       .c_str());
#endif

  mct::Config config = mct::Config::load(paths.config_file());
  if (!options.game_dir.empty()) config.game_dir = options.game_dir;
  if (!config.game_dir.empty())
    paths.game_root = mct::fs::normalize(mct::fs::absolute(config.game_dir));

  int code = 0;
  try {
    if (options.install_java > 0) {
      code = run_install_java(paths, config, options.install_java);
    } else if (!options.install_version.empty()) {
      code = run_headless_install(paths, config, options);
    } else if (!options.launch_instance.empty()) {
      code = run_headless_launch(paths, config, options);
    } else {
      mct::App app(paths, config);
      code = app.run();
    }
  } catch (const std::exception& error) {
    mct::log::error(std::string("未处理异常: ") + error.what());
    std::printf("\n发生未处理的错误: %s\n", error.what());
    std::printf("日志: %s\n", paths.log_file().c_str());
    code = 1;
  } catch (...) {
    mct::log::error("未知异常");
    std::printf("\n发生未知错误，日志: %s\n", paths.log_file().c_str());
    code = 1;
  }
  mct::log::shutdown();
  return code;
}
