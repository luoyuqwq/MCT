#include "app/app.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "auth/microsoft.hpp"
#include "auth/skin.hpp"
#include "launch/java_install.hpp"
#include "launch/launcher.hpp"
#include "meta/version.hpp"
#include "mods/mods.hpp"
#include "ui/widgets.hpp"
#include "util/clipboard.hpp"
#include "util/fs.hpp"
#include "util/hash.hpp"
#include "util/log.hpp"
#include "util/process.hpp"
#include "util/str.hpp"
#include "version.hpp"

namespace mct {

namespace {

const ui::Theme& th = ui::theme();

bool parse_int(const std::string& text, int min_value, int max_value, int& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  long value = std::strtol(text.c_str(), &end, 10);
  if (!end || *end != '\0') return false;
  if (value < min_value || value > max_value) return false;
  out = static_cast<int>(value);
  return true;
}

std::string strip_quotes(std::string text) {
  text = str::trim(text);
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
    text = text.substr(1, text.size() - 2);
  return text;
}

bool valid_offline_name(const std::string& name) {
  if (name.size() < 3 || name.size() > 16) return false;
  for (char c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
      return false;
  }
  return true;
}

int compare_version(const std::string& a, const std::string& b) {
  std::vector<std::string> pa = str::split(a, '.');
  std::vector<std::string> pb = str::split(b, '.');
  size_t count = std::max(pa.size(), pb.size());
  for (size_t i = 0; i < count; ++i) {
    int va = i < pa.size() ? std::atoi(pa[i].c_str()) : 0;
    int vb = i < pb.size() ? std::atoi(pb[i].c_str()) : 0;
    if (va != vb) return va < vb ? -1 : 1;
  }
  return 0;
}

std::string instance_summary(const Paths& paths, const std::string& id) {
  Json record = meta::load_instance_record(paths, id);
  std::string loader = record.get_str("loader_label");
  std::string base = record.get_str("base_version", id);
  if (loader.empty() || loader == "原版")
    return "原版 " + base;
  std::string loader_version = record.get_str("loader_version");
  return loader + (loader_version.empty() ? "" : " " + loader_version) + " · " + base;
}

}  // namespace

App::App(Paths paths, Config config)
    : paths_(std::move(paths)), config_(std::move(config)) {
  if (!config_.game_dir.empty())
    paths_.game_root = fs::normalize(fs::absolute(config_.game_dir));
  accounts_.load(paths_.accounts_file());
  hash_cache_.load(fs::join(paths_.cache_dir(), "filehashes.json"));
}

void App::save_hash_cache() {
  fs::ensure_dir(paths_.cache_dir());
  hash_cache_.save(fs::join(paths_.cache_dir(), "filehashes.json"));
}

meta::Mirror App::make_mirror() {
  meta::Mirror mirror(config_.game_source, config_.custom_mirror, paths_.cache_dir());
  mirror.init(http_);
  return mirror;
}

void App::save_config() { config_.save(paths_.config_file()); }
void App::save_accounts() { accounts_.save(paths_.accounts_file()); }

std::vector<std::string> App::installed_instances() { return meta::list_instances(paths_); }

int App::run() {
  if (!term_.init()) {
    std::fprintf(stderr, "无法初始化终端\n");
    return 1;
  }
  term_.enter_alt();
  bool running = true;
  try {
    while (running) {
      int choice = main_screen();
      if (choice < 0) break;
      switch (choice) {
        case 0: accounts_screen(); break;
        case 1: download_game_screen(); break;
        case 2: mods_screen(""); break;
        case 3: settings_screen(); break;
        case 4: launch_screen(); break;
        default: running = false; break;
      }
    }
  } catch (...) {
    save_hash_cache();
    term_.leave_alt();
    term_.shutdown();
    throw;
  }
  save_hash_cache();
  term_.leave_alt();
  term_.shutdown();
  return 0;
}

int App::main_screen() {
  std::vector<std::string> instances = installed_instances();
  std::vector<ui::MenuItem> items;
  items.push_back({"① 登录 / 皮肤", "离线账号 · 微软正版 · 更换皮肤", "", false});
  items.push_back({"② 下载游戏", "原版 / Fabric / Quilt / Forge / NeoForge", "", false});
  items.push_back({"③ 下载模组", "国内镜像 / CurseForge / Modrinth", "", false});
  items.push_back({"④ 设置", "下载源 · 版本隔离 · 游戏目录 · Java", "", false});
  items.push_back({"启动游戏", std::to_string(instances.size()) + " 个已安装实例", "", false});
  items.push_back({"退出", "", "", false});

  ui::MenuOptions options;
  options.subtitle = "Minecraft 终端启动器";
  options.allow_quit = true;
  options.show_back = false;
  const auth::Account* account = accounts_.current();
  std::string account_text = account ? account->name + " · " + account->type_label() : "未登录";
  options.header_lines.push_back(th.dim + "账号 " + th.reset + th.text + account_text + th.reset +
                                 "        " + th.dim + "游戏源 " + th.reset + th.text +
                                 config_.game_source_label() + th.reset);
  options.header_lines.push_back(
      th.dim + "版本隔离 " + th.reset +
      (config_.version_isolation ? th.good + "开启" : th.warn + "关闭") + th.reset + "        " +
      th.dim + "模组源 " + th.reset + th.text + config_.mod_source_label() + th.reset);
  if (!flash_.empty()) options.header_lines.push_back(th.good + flash_ + th.reset);
  options.footer_lines.push_back(th.dim + "数据目录 " + paths_.home + th.reset);

  int index = ui::menu(term_, options, items, 0);
  return index < 0 ? -1 : index;
}

void App::accounts_screen() {
  for (;;) {
    std::vector<ui::MenuItem> items;
    const int account_count = static_cast<int>(accounts_.accounts().size());
    for (const auto& account : accounts_.accounts()) {
      bool current = accounts_.current_uuid() == account.uuid;
      items.push_back({account.name, account.type_label() + " · " + account.uuid,
                       current ? "●" : " ", false});
    }
    items.push_back({"新建离线账号", "输入用户名即可，无需密码", "+", false});
    items.push_back({"微软正版登录", "设备码登录，免注册 Azure", "MS", false});
    items.push_back({"更换皮肤", "上传 PNG / 恢复默认（仅正版）", "SK", false});
    items.push_back({"删除当前账号", "", "DEL", accounts_.current() == nullptr});
    items.push_back({"返回", "", "", false});
    const int back_index = static_cast<int>(items.size()) - 1;

    ui::MenuOptions options;
    options.title = "① 登录与皮肤";
    options.breadcrumb = "一级菜单 / 账号";
    options.header_lines.push_back(
        th.dim + "离线账号使用 OfflinePlayer UUID，可与正版账号共存" + th.reset);
    if (!flash_.empty()) options.header_lines.push_back(th.good + flash_ + th.reset);

    int index = ui::menu(term_, options, items, 0);
    if (index < 0 || index == back_index) return;

    if (index < account_count) {
      const auth::Account& account = accounts_.accounts()[index];
      accounts_.set_current(account.uuid);
      save_accounts();
      flash_ = "已切换到 " + account.name;
      continue;
    }
    switch (index - account_count) {
      case 0: offline_login(); break;
      case 1: microsoft_login(); break;
      case 2: skin_screen(); break;
      case 3: {
        const auth::Account* current = accounts_.current();
        if (!current) break;
        if (ui::confirm(term_, "删除账号", "确定删除账号 " + current->name + " 吗？", false)) {
          std::string name = current->name;
          accounts_.remove(current->uuid);
          save_accounts();
          flash_ = "已删除账号 " + name;
        }
        break;
      }
      default: break;
    }
  }
}

void App::offline_login() {
  ui::PromptOptions options;
  options.title = "离线登录";
  options.label = "用户名";
  options.hint = "3-16 位，仅限字母、数字、下划线。离线模式不验证密码，只用于单机与局域网。";
  std::string name;
  if (!ui::prompt(term_, options, name)) return;
  name = str::trim(name);
  if (!valid_offline_name(name)) {
    ui::error_box(term_, "用户名不合法", "用户名需要 3-16 位，只能包含字母、数字和下划线。");
    return;
  }
  auth::Account account;
  account.type = auth::AccountType::Offline;
  account.name = name;
  account.uuid = hash::offline_uuid(name);
  account.skin_model = "classic";
  accounts_.upsert(account);
  accounts_.set_current(account.uuid);
  save_accounts();
  flash_ = "已登录离线账号 " + name;
  ui::message(term_, "离线登录成功",
              {"用户名：" + name, "UUID：" + account.uuid, "", "现在可以启动游戏了。"});
}

void App::microsoft_login() {
  ui::draw_waiting(term_, "微软正版登录", "正在向微软申请设备码…");
  auth::MicrosoftAuth auth(http_);
  auth::DeviceCode code;
  std::string error;
  if (!auth.request_device_code(code, &error)) {
    ui::error_box(term_, "无法开始登录", error);
    return;
  }

  std::string complete = code.verification_uri_complete;
  if (complete.empty())
    complete = code.verification_uri + "?otc=" + str::percent_encode(code.user_code);
  clipboard::set_text(complete);
  const bool opened = process::open_url(complete);
  log::info("微软登录：已申请设备码 " + code.user_code);

  ui::message(term_, "微软正版登录",
              {(opened ? "已打开浏览器（代码已自动填入），链接也已复制到剪贴板。"
                       : "请手动打开浏览器，链接已复制到剪贴板："),
               "1. 打开：" + code.verification_uri,
               "2. 输入代码：" + code.user_code,
               "3. 使用拥有 Minecraft 的微软账号登录并授权", "",
               "按 Enter 开始等待授权（Esc 取消）"});

  std::string poll_state = "等待授权…";
  int waited_seconds = 0;
  auto on_poll = [&](const auth::DevicePoll& poll) {
    switch (poll.state) {
      case auth::DevicePoll::State::Pending:
        poll_state = "等待你在浏览器中确认…";
        break;
      case auth::DevicePoll::State::SlowDown:
        poll_state = "微软要求降速，已自动延长轮询间隔…";
        break;
      case auth::DevicePoll::State::Authorized:
        poll_state = "授权成功";
        break;
      default:
        poll_state = poll.message.empty() ? "等待授权…" : poll.message;
        break;
    }
  };
  auto wait = [&](int seconds) -> bool {
    for (int elapsed = 0; elapsed < seconds; ++elapsed) {
      ui::KeyEvent key;
      if (term_.poll_key(1000, key)) {
        if (key.code == ui::KeyEvent::Code::Esc || key.code == ui::KeyEvent::Code::CtrlC)
          return false;
      }
      ui::draw_waiting(term_, "等待微软授权",
                       "状态：" + poll_state + "\n已等待 " + std::to_string(waited_seconds) +
                           " 秒 · " + std::to_string(seconds - elapsed) +
                           " 秒后重试（Esc 取消）" +
                           (waited_seconds >= 60
                                ? "\n如果浏览器已显示成功但这里仍在等待，请确认输入的是本次代码 " +
                                      code.user_code
                                : ""));
    }
    waited_seconds += seconds;
    return true;
  };

  std::string ms_access;
  std::string refresh_token;
  int64_t expires_at = 0;
  if (!auth.wait_for_token(code, ms_access, refresh_token, expires_at, wait, &error, on_poll)) {
    ui::error_box(term_, "登录未完成",
                  error + "\n\n每次轮询的错误都会记录在日志中：" + paths_.log_file());
    return;
  }

  ui::draw_waiting(term_, "微软正版登录", "授权成功，正在获取 Xbox Live 与 Minecraft 档案…");
  auth::Account account;
  if (!auth.login_minecraft(ms_access, account, &error)) {
    ui::error_box(term_, "登录失败", error);
    return;
  }
  account.refresh_token = refresh_token;
  accounts_.upsert(account);
  accounts_.set_current(account.uuid);
  save_accounts();
  flash_ = "已登录正版账号 " + account.name;
  ui::message(term_, "登录成功",
              {"玩家：" + account.name, "UUID：" + account.uuid,
               "皮肤模型：" + account.skin_model});
}

bool App::refresh_microsoft_account(const std::string& uuid) {
  auth::Account* account = accounts_.find(uuid);
  if (!account) return false;
  if (account->offline() || account->token_valid()) return true;
  if (account->refresh_token.empty()) {
    ui::error_box(term_, "登录已过期", "缺少刷新令牌，请重新执行「微软正版登录」。");
    return false;
  }

  ui::draw_waiting(term_, "刷新登录", "正在刷新微软登录状态…");
  auth::MicrosoftAuth auth(http_);
  std::string ms_access;
  std::string refresh;
  int64_t expires_at = 0;
  std::string error;
  if (!auth.refresh(account->refresh_token, ms_access, refresh, expires_at, &error)) {
    ui::error_box(term_, "刷新失败", error);
    return false;
  }
  auth::Account updated;
  if (!auth.login_minecraft(ms_access, updated, &error)) {
    ui::error_box(term_, "刷新失败", error);
    return false;
  }
  updated.refresh_token = refresh;
  accounts_.upsert(updated);
  accounts_.set_current(updated.uuid);
  save_accounts();
  return true;
}

bool App::ensure_account(auth::Account& account) {
  auth::Account* current = accounts_.current();
  if (!current) {
    if (!ui::confirm(term_, "还没有账号", "当前没有登录账号，现在创建一个离线账号吗？")) return false;
    offline_login();
    current = accounts_.current();
    if (!current) return false;
  }
  const std::string uuid = current->uuid;
  if (!current->offline() && !current->token_valid()) {
    if (!refresh_microsoft_account(uuid)) return false;
  }
  current = accounts_.find(uuid);
  if (!current) return false;
  account = *current;
  return true;
}

void App::skin_screen() {
  auth::Account* current = accounts_.current();
  if (!current) {
    ui::message(term_, "没有账号", {"请先登录一个账号（离线账号不能更换皮肤）。"});
    return;
  }
  if (current->offline()) {
    ui::message(term_, "需要正版账号",
                {"离线账号不能上传皮肤。", "请使用「微软正版登录」后再试。"});
    return;
  }
  auth::Account account;
  if (!ensure_account(account)) return;

  auth::SkinService skins(http_);
  auth::SkinInfo info;
  std::string error;
  if (!skins.fetch(account.access_token, info, &error)) {
    ui::error_box(term_, "获取皮肤失败", error);
    return;
  }

  for (;;) {
    std::vector<ui::MenuItem> items;
    items.push_back({"上传本地皮肤 PNG", "64x64 或 64x32 的 PNG 文件", "UP", false});
    items.push_back(
        {"皮肤模型",
         std::string("当前：") + (account.skin_model == "slim" ? "纤细 slim" : "经典 classic"), "",
         false});
    items.push_back({"恢复默认皮肤（Steve / Alex）", "", "DEL", false});
    items.push_back({"当前皮肤地址", info.url.empty() ? "未设置" : info.url, "", true});
    items.push_back({"返回", "", "", false});
    const int back_index = static_cast<int>(items.size()) - 1;

    ui::MenuOptions options;
    options.title = "② 更换皮肤";
    options.breadcrumb = "一级菜单 / 账号 / 皮肤";
    options.header_lines.push_back(th.dim + "账号 " + account.name + "  ·  模型 " +
                                   account.skin_model + th.reset);
    int index = ui::menu(term_, options, items, 0);
    if (index < 0 || index == back_index) return;

    if (index == 0) {
      ui::PromptOptions prompt;
      prompt.title = "上传皮肤";
      prompt.label = "PNG 文件路径";
      prompt.hint = "直接把文件拖进终端也可以（会自动去掉引号）。要求 64x64 或 64x32。";
      std::string path;
      if (!ui::prompt(term_, prompt, path)) continue;
      path = strip_quotes(path);
      if (!fs::is_file(path)) {
        ui::error_box(term_, "文件不存在", path);
        continue;
      }
      std::vector<ui::MenuItem> models = {{"经典 classic", "普通手臂（Steve 模型）", "", false},
                                          {"纤细 slim", "细手臂（Alex 模型）", "", false}};
      ui::MenuOptions model_options;
      model_options.title = "选择皮肤模型";
      model_options.show_back = true;
      int model = ui::menu(term_, model_options, models, 0);
      if (model < 0) continue;
      std::string variant = model == 1 ? "slim" : "classic";
      ui::draw_waiting(term_, "上传皮肤", "正在上传到 Mojang…");
      if (!skins.upload(account.access_token, path, variant, &error)) {
        ui::error_box(term_, "上传失败", error);
        continue;
      }
      auth::Account* stored = accounts_.find(account.uuid);
      if (stored) {
        stored->skin_model = variant;
        account.skin_model = variant;
        save_accounts();
      }
      ui::message(term_, "皮肤已更新", {"模型：" + variant, "重新进入服务器后生效。"});
    } else if (index == 1) {
      std::vector<ui::MenuItem> models = {{"经典 classic", "", "", false},
                                          {"纤细 slim", "", "", false}};
      ui::MenuOptions options2;
      options2.title = "选择皮肤模型";
      int model = ui::menu(term_, options2, models, account.skin_model == "slim" ? 1 : 0);
      if (model < 0) continue;
      std::string variant = model == 1 ? "slim" : "classic";
      std::string variant_name = variant == "slim" ? "Alex（纤细）" : "Steve（经典）";
      ui::message(term_, "皮肤模型说明",
                  {"变体 " + variant + " 对应 " + variant_name + "。",
                   "模型会随下次上传皮肤时一起提交给 Mojang。"});
      auth::Account* stored = accounts_.find(account.uuid);
      if (stored) {
        stored->skin_model = variant;
        account.skin_model = variant;
        save_accounts();
      }
    } else if (index == 2) {
      if (!ui::confirm(term_, "恢复默认皮肤", "确定删除当前皮肤，恢复默认外观吗？", false))
        continue;
      ui::draw_waiting(term_, "恢复默认皮肤", "正在请求 Mojang…");
      if (!skins.reset(account.access_token, &error)) {
        ui::error_box(term_, "操作失败", error);
        continue;
      }
      info.url.clear();
      ui::message(term_, "已恢复默认皮肤", {"重新进入服务器后生效。"});
    }
  }
}

void App::settings_screen() {
  if (java_cache_.empty()) {
    ui::draw_waiting(term_, "设置", "正在检测本机 Java…");
    java_cache_ = launch::find_all_java(paths_, config_);
  }
  for (;;) {
    std::string java_hint = "自动探测";
    if (!config_.java_path.empty())
      java_hint = config_.java_path;
    else if (!java_cache_.empty()) {
      const launch::JavaInfo& java = java_cache_.front();
      const std::string folder = fs::filename(fs::parent(fs::parent(java.path)));
      java_hint = "自动：Java " + java.version + (folder.empty() ? "" : "（" + folder + "）");
    } else
      java_hint = "自动：（未检测到 Java）";

    std::vector<ui::MenuItem> items;
    items.push_back({"下载源", config_.game_source_label(), "", false});
    items.push_back({"自定义站点地址",
                     config_.custom_mirror.empty() ? "未设置（选“自定义站点”时使用）"
                                                   : config_.custom_mirror,
                     "", false});
    items.push_back({"模组源", config_.mod_source_label(), "", false});
    items.push_back(
        {"CurseForge API Key", config_.curseforge_api_key.empty() ? "未设置（CurseForge 需要）" : "已设置",
         "", false});
    items.push_back({"版本隔离",
                     config_.version_isolation ? "开启 · 每个版本独立 mods 与存档"
                                               : "关闭 · 所有版本共用 game 目录",
                     "", false});
    items.push_back({"下载线程", std::to_string(config_.download_threads), "", false});
    items.push_back({"最大内存", std::to_string(config_.max_memory_mb) + " MB", "", false});
    items.push_back({"最小内存", std::to_string(config_.min_memory_mb) + " MB", "", false});
    items.push_back({"Java 路径", java_hint, "", false});
    items.push_back(
        {"额外 JVM 参数", config_.extra_jvm_args.empty() ? "无" : config_.extra_jvm_args, "", false});
    items.push_back({"显示快照版本", config_.show_snapshots ? "是" : "否", "", false});
    items.push_back({"游戏目录", paths_.game_dir(), "DIR", false});
    items.push_back({"安装 Java 运行时", "自动下载 Temurin JRE（8 / 17 / 21）", "JRE", false});
    items.push_back({"保存并返回", "", "", false});
    const int back_index = static_cast<int>(items.size()) - 1;

    ui::MenuOptions options;
    options.title = "④ 设置";
    options.breadcrumb = "一级菜单 / 设置";
    options.header_lines.push_back(th.dim + "所有修改会立即保存到 " +
                                   paths_.config_file() + th.reset);
    if (!flash_.empty()) options.header_lines.push_back(th.good + flash_ + th.reset);

    int index = ui::menu(term_, options, items, 0);
    if (index < 0 || index == back_index) {
      save_config();
      return;
    }
    switch (index) {
      case 0: {
        if (config_.game_source == GameSource::Official)
          config_.game_source = GameSource::Bmclapi;
        else if (config_.game_source == GameSource::Bmclapi)
          config_.game_source = GameSource::Custom;
        else
          config_.game_source = GameSource::Official;
        if (config_.game_source == GameSource::Custom && config_.custom_mirror.empty()) {
          ui::PromptOptions prompt;
          prompt.title = "自定义站点";
          prompt.label = "站点根地址";
          prompt.hint = "例如 https://mc.example.com/mirror  （末尾不需要斜杠）";
          std::string url;
          if (ui::prompt(term_, prompt, url)) {
            url = str::trim(url);
            while (!url.empty() && url.back() == '/') url.pop_back();
            config_.custom_mirror = url;
          }
        }
        save_config();
        break;
      }
      case 1: {
        ui::PromptOptions prompt;
        prompt.title = "自定义站点";
        prompt.label = "站点根地址";
        prompt.hint = "留空表示不使用。示例：https://mc.example.com/mirror";
        prompt.initial = config_.custom_mirror;
        std::string url;
        if (ui::prompt(term_, prompt, url)) {
          url = str::trim(url);
          while (!url.empty() && url.back() == '/') url.pop_back();
          config_.custom_mirror = url;
          save_config();
        }
        break;
      }
      case 2: {
        if (config_.mod_source == ModSource::Domestic)
          config_.mod_source = ModSource::Modrinth;
        else if (config_.mod_source == ModSource::Modrinth)
          config_.mod_source = ModSource::CurseForge;
        else
          config_.mod_source = ModSource::Domestic;
        save_config();
        break;
      }
      case 3: {
        ui::PromptOptions prompt;
        prompt.title = "CurseForge API Key";
        prompt.label = "API Key";
        prompt.hint = "在 console.curseforge.com 免费申请；留空则无法使用 CurseForge 源。";
        prompt.initial = config_.curseforge_api_key;
        std::string key;
        if (ui::prompt(term_, prompt, key)) {
          config_.curseforge_api_key = str::trim(key);
          save_config();
        }
        break;
      }
      case 4:
        config_.version_isolation = !config_.version_isolation;
        save_config();
        flash_ = config_.version_isolation ? "版本隔离已开启" : "版本隔离已关闭";
        break;
      case 5: {
        ui::PromptOptions prompt;
        prompt.title = "下载线程";
        prompt.label = "线程数（1-64）";
        prompt.initial = std::to_string(config_.download_threads);
        std::string text;
        if (ui::prompt(term_, prompt, text)) {
          int value = 0;
          if (parse_int(text, 1, 64, value)) {
            config_.download_threads = value;
            save_config();
          } else {
            ui::error_box(term_, "输入无效", "请输入 1-64 之间的整数。");
          }
        }
        break;
      }
      case 6: {
        ui::PromptOptions prompt;
        prompt.title = "最大内存";
        prompt.label = "最大内存（MB）";
        prompt.initial = std::to_string(config_.max_memory_mb);
        std::string text;
        if (ui::prompt(term_, prompt, text)) {
          int value = 0;
          if (parse_int(text, 512, 65536, value)) {
            config_.max_memory_mb = value;
            if (config_.min_memory_mb > value) config_.min_memory_mb = value;
            save_config();
          } else {
            ui::error_box(term_, "输入无效", "请输入 512-65536 之间的整数。");
          }
        }
        break;
      }
      case 7: {
        ui::PromptOptions prompt;
        prompt.title = "最小内存";
        prompt.label = "最小内存（MB）";
        prompt.initial = std::to_string(config_.min_memory_mb);
        std::string text;
        if (ui::prompt(term_, prompt, text)) {
          int value = 0;
          if (parse_int(text, 0, config_.max_memory_mb, value)) {
            config_.min_memory_mb = value;
            save_config();
          } else {
            ui::error_box(term_, "输入无效", "请输入 0 到最大内存之间的整数。");
          }
        }
        break;
      }
      case 8: {
        ui::PromptOptions prompt;
        prompt.title = "Java 路径";
        prompt.label = "java.exe 路径（留空 = 自动）";
        prompt.initial = config_.java_path;
        prompt.hint = "示例：C:\\Program Files\\Eclipse Adoptium\\jdk-21\\bin\\java.exe";
        std::string path;
        if (ui::prompt(term_, prompt, path)) {
          path = strip_quotes(path);
          config_.java_path = path;
          java_cache_.clear();
          save_config();
        }
        break;
      }
      case 9: {
        ui::PromptOptions prompt;
        prompt.title = "额外 JVM 参数";
        prompt.label = "参数（留空清除）";
        prompt.initial = config_.extra_jvm_args;
        prompt.hint = "例如 -XX:+UseG1GC -Dfile.encoding=UTF-8";
        std::string args;
        if (ui::prompt(term_, prompt, args)) {
          config_.extra_jvm_args = args;
          save_config();
        }
        break;
      }
      case 10:
        config_.show_snapshots = !config_.show_snapshots;
        save_config();
        break;
      case 11:
        change_game_dir();
        break;
      case 12: {
        std::vector<ui::MenuItem> versions = {
            {"Java 8", "适合 1.7.10 及更早版本", "", false},
            {"Java 17", "适合 1.17 – 1.20.4", "", false},
            {"Java 21", "适合 1.20.5 及更新版本", "", false},
            {"返回", "", "", false},
        };
        ui::MenuOptions java_options;
        java_options.title = "安装 Java 运行时";
        java_options.breadcrumb = "一级菜单 / 设置 / Java";
        int choice = ui::menu(term_, java_options, versions, 1);
        if (choice >= 0 && choice < 3) {
          const int major = choice == 0 ? 8 : (choice == 1 ? 17 : 21);
          install_java_runtime(major);
        }
        break;
      }
      default:
        break;
    }
  }
}

void App::change_game_dir() {
  const std::string current = paths_.game_dir();
  ui::PromptOptions prompt;
  prompt.title = "游戏目录";
  prompt.label = "游戏安装根目录";
  prompt.hint =
      "versions、libraries、assets、mods 都会放在这里。留空恢复默认（<数据目录>/game）。";
  prompt.initial = current;
  std::string value;
  if (!ui::prompt(term_, prompt, value)) return;
  value = strip_quotes(value);
  const std::string target =
      value.empty() ? fs::join(paths_.home, "game") : fs::normalize(fs::absolute(value));
  if (fs::equal(target, current)) {
    ui::message(term_, "游戏目录未变化", {current});
    return;
  }
  if (!fs::ensure_dir(target)) {
    ui::error_box(term_, "无法使用该目录", "创建目录失败：" + target);
    return;
  }
  bool copy_existing = false;
  if (fs::is_dir(current) && !fs::is_dir(fs::join(target, "versions"))) {
    copy_existing = ui::confirm(
        term_, "复制游戏文件",
        "新目录里还没有 versions。是否把现有游戏文件（" + current +
            "）复制过去？\n文件很多时可能需要几分钟，可以在进度界面按 Esc 取消。",
        true);
  }

  if (copy_existing) {
    ui::ProgressView progress(term_, "复制游戏文件");
    std::atomic<bool> cancelled{false};
    progress.set_cancel_callback([&cancelled]() { cancelled.store(true); });
    net::DownloadStats stats;
    stats.current = "正在统计文件…";
    progress.update(stats);
    bool ok = fs::copy_dir(
        current, target,
        [&progress, &stats](const fs::CopyProgress& copy) {
          stats.bytes_done = copy.bytes_done;
          stats.bytes_total = copy.bytes_total;
          stats.files_done = copy.files_done;
          stats.files_total = copy.files_total;
          stats.current = copy.current;
          progress.update(stats);
        },
        [&cancelled]() { return cancelled.load(); });
    progress.finish(ok, ok ? "复制完成" : "复制已取消");
    if (!ok) {
      ui::error_box(term_, "复制未完成",
                    "已保留原游戏目录，配置未修改。\n目标目录中可能残留了部分文件，可以手动删除。");
      return;
    }
  }

  config_.game_dir = value.empty() ? "" : target;
  paths_.game_root = target;
  save_config();
  flash_ = "游戏目录已切换到 " + target;
  std::vector<std::string> lines = {"新目录：" + target};
  if (!copy_existing && fs::is_dir(current))
    lines.push_back("原目录未搬移：" + current + "（可手动删除）");
  lines.push_back("之后安装的版本、模组都会使用新目录。");
  ui::message(term_, "游戏目录已更新", lines);
}

std::string App::pick_instance(const std::string& title, const std::string& subtitle) {
  std::vector<std::string> instances = installed_instances();
  if (instances.empty()) {
    ui::message(term_, "没有已安装的版本",
                {"请先到「② 下载游戏」安装一个版本，之后再回来。"});
    return std::string();
  }
  std::vector<ui::MenuItem> items;
  for (const auto& id : instances) items.push_back({id, instance_summary(paths_, id), "", false});
  items.push_back({"返回", "", "", false});
  ui::MenuOptions options;
  options.title = title;
  options.subtitle = subtitle;
  int index = ui::menu(term_, options, items, 0);
  if (index < 0 || index >= static_cast<int>(instances.size())) return std::string();
  return instances[index];
}

void App::launch_screen() { launch_instance(pick_instance("启动游戏", "选择要启动的实例")); }

bool App::ensure_java(const std::string& instance) {
  meta::VersionMeta meta;
  std::string error;
  if (!meta::load_version_meta(paths_, instance, meta, &error)) return true;  // 交给启动流程报错
  const int required = meta.java_major;
  launch::JavaInfo java;
  if (find_java(paths_, config_, required, java, &error) && java.major >= required) return true;

  const int target = launch::recommended_java_major(required);
  if (!ui::confirm(term_, "需要 Java " + std::to_string(required),
                   "没有找到满足要求的 Java（该版本需要 Java " + std::to_string(required) +
                       " 及以上）。\n\n是否自动下载安装 Temurin JRE " + std::to_string(target) +
                       "？\n约 40–60 MB，安装到 " + paths_.java_dir(),
                   true)) {
    return false;
  }
  return install_java_runtime(target);
}

bool App::install_java_runtime(int major) {
  ui::ProgressView progress(term_, "安装 Java " + std::to_string(major));
  net::Downloader downloader(
      config_.download_threads,
      [&progress](const net::DownloadStats& stats) { progress.update(stats); });
  downloader.set_hash_cache(&hash_cache_);
  progress.set_cancel_callback([&downloader]() { downloader.cancel(); });

  launch::JavaInstallOutcome outcome;
  bool ok = launch::install_java(
      paths_, major, http_, downloader,
      [&progress](const std::string& status) { progress.set_status(status); }, outcome);
  progress.finish(ok, ok ? "安装完成" : outcome.error);
  if (!ok) {
    ui::error_box(term_, "Java 安装失败", outcome.error);
    return false;
  }
  java_cache_.clear();
  save_hash_cache();
  ui::message(term_, "Java 安装完成",
              {"版本：Java " + std::to_string(outcome.major), "路径：" + outcome.java_path});
  return true;
}

void App::launch_instance(const std::string& instance) {
  if (instance.empty()) return;
  auth::Account account;
  if (!ensure_account(account)) return;
  if (!ensure_java(instance)) return;

  ui::draw_waiting(term_, "启动游戏", "检查 Java、classpath 与启动参数…");
  launch::GameLauncher launcher(paths_, config_);
  std::vector<std::string> argv;
  std::string error;
  if (!launcher.build_command(instance, account, argv, &error)) {
    ui::error_box(term_, "无法启动", error);
    return;
  }
  const std::string game_dir = paths_.instance_game_dir(instance, config_.version_isolation);
  config_.last_version = instance;
  save_config();

  term_.leave_alt();
  term_.shutdown();

  std::printf("\n");
  std::printf("  MCT · 启动 %s\n", instance.c_str());
  std::printf("  Designed By Cache_Rikka\n");
  std::printf("  账号 %s (%s)\n", account.name.c_str(), account.type_label().c_str());
  std::printf("  Java %s\n", argv.empty() ? "" : argv[0].c_str());
  std::printf("  目录 %s\n\n", game_dir.c_str());
  std::fflush(stdout);

  int exit_code = 0;
  std::string spawn_error;
  bool started = process::spawn_and_wait(process::join_args(argv), game_dir, &exit_code,
                                         &spawn_error);
  if (!started) {
    std::printf("  启动失败：%s\n", spawn_error.c_str());
  } else if (exit_code == 0) {
    std::printf("  游戏已正常退出。\n");
  } else {
    std::printf("  游戏异常退出，退出码 %d。\n", exit_code);
    std::printf("  如果启动失败，请检查 Java 版本、内存设置或查看日志。\n");
  }
  std::printf("\n  按 Enter 返回启动器…");
  std::fflush(stdout);
  std::getchar();

  term_.init();
  term_.enter_alt();
}

void App::download_game_screen() {
  meta::Mirror mirror = make_mirror();
  ui::draw_waiting(term_, "下载游戏", "正在获取版本清单（" + mirror.name() + "）…");
  meta::VersionCatalog catalog;
  std::string error;
  if (!catalog.load(http_, mirror, paths_, false, &error)) {
    ui::error_box(term_, "获取版本清单失败", error);
    return;
  }

  bool snapshots = config_.show_snapshots;
  std::string filter;
  for (;;) {
    std::vector<const meta::VersionSummary*> visible;
    for (const auto& version : catalog.versions) {
      if (!snapshots && version.type != "release") continue;
      if (!filter.empty() && !str::icontains(version.id, filter)) continue;
      visible.push_back(&version);
    }
    const size_t max_show = 60;
    std::vector<ui::MenuItem> items;
    items.push_back({"筛选：仅正式版 / 显示快照", snapshots ? "当前：显示快照" : "当前：仅正式版", "F",
                     false});
    items.push_back({"搜索版本号", filter.empty() ? "未设置" : "关键词：" + filter, "/", false});
    size_t shown = std::min(visible.size(), max_show);
    for (size_t i = 0; i < shown; ++i) {
      const auto* version = visible[i];
      std::string hint = version->release_time.empty() ? version->type : version->release_time.substr(0, 10);
      if (version->id == catalog.latest_release) hint += " · 最新正式版";
      else if (version->id == catalog.latest_snapshot) hint += " · 最新快照";
      items.push_back({version->id, hint, version->type == "release" ? "" : "S", false});
    }
    if (visible.empty()) items.push_back({"没有匹配的版本", "", "", true});
    items.push_back({"返回", "", "", false});
    const int back_index = static_cast<int>(items.size()) - 1;

    ui::MenuOptions options;
    options.title = "② 下载游戏";
    options.breadcrumb = "一级菜单 / 下载游戏";
    options.header_lines.push_back(th.dim + "共 " + std::to_string(visible.size()) +
                                   " 个版本，显示前 " + std::to_string(shown) +
                                   " 个；可用搜索快速定位" + th.reset);
    options.footer_lines.push_back(th.dim + "下载源：" + mirror.name() + th.reset);

    int index = ui::menu(term_, options, items, 0);
    if (index < 0 || index == back_index) return;
    if (index == 0) {
      snapshots = !snapshots;
      config_.show_snapshots = snapshots;
      save_config();
      continue;
    }
    if (index == 1) {
      ui::PromptOptions prompt;
      prompt.title = "搜索版本";
      prompt.label = "版本号关键词";
      prompt.hint = "留空显示全部。例如 1.20、1.21.4、24w14a";
      prompt.initial = filter;
      std::string text;
      if (ui::prompt(term_, prompt, text)) filter = str::trim(text);
      continue;
    }
    if (index < 2 || index >= 2 + static_cast<int>(shown)) continue;
    const meta::VersionSummary* chosen = visible[static_cast<size_t>(index - 2)];

    std::vector<ui::MenuItem> loaders;
    loaders.push_back({"原版", "不安装模组加载器", "", false});
    loaders.push_back({"Fabric", "轻量、更新快，适合大多数模组", "F", false});
    loaders.push_back({"Quilt", "Fabric 的衍生加载器", "Q", false});
    loaders.push_back({"Forge", "老牌加载器，模组生态最大", "FG", false});
    bool neoforge_ok = compare_version(chosen->id, "1.20.2") >= 0;
    loaders.push_back(
        {"NeoForge", neoforge_ok ? "Forge 分支，1.20.2+" : "需要 1.20.2 及以上版本", "NF",
         !neoforge_ok});
    loaders.push_back({"返回", "", "", false});
    ui::MenuOptions loader_options;
    loader_options.title = "选择加载器 · " + chosen->id;
    loader_options.breadcrumb = "一级菜单 / 下载游戏 / 加载器";
    int loader_index = ui::menu(term_, loader_options, loaders, 0);
    if (loader_index < 0 || loader_index >= 5) continue;

    meta::LoaderKind loader = meta::LoaderKind::Vanilla;
    if (loader_index == 1) loader = meta::LoaderKind::Fabric;
    if (loader_index == 2) loader = meta::LoaderKind::Quilt;
    if (loader_index == 3) loader = meta::LoaderKind::Forge;
    if (loader_index == 4) loader = meta::LoaderKind::NeoForge;

    std::string loader_version;
    if (loader != meta::LoaderKind::Vanilla) {
      if (!choose_loader_version(chosen->id, loader, mirror, loader_version)) continue;
    }
    install_version(chosen->id, loader, loader_version);
    return;
  }
}

bool App::choose_loader_version(const std::string& mc, meta::LoaderKind loader, meta::Mirror& mirror,
                                std::string& version) {
  version.clear();
  ui::draw_waiting(term_, "加载器版本", "正在获取 " + meta::loader_label(loader) + " 版本列表…");
  meta::GameInstaller installer(paths_, config_, mirror, http_);
  std::vector<std::string> versions;
  std::string error;
  if (!installer.loader_versions(mc, loader, versions, &error)) {
    ui::message(term_, "无法获取加载器版本", {error, "将自动选择最新可用版本。"});
    return true;
  }
  if (versions.empty()) {
    ui::message(term_, "没有可用版本", {"将自动选择最新可用版本。"});
    return true;
  }
  std::vector<ui::MenuItem> items;
  items.push_back({"自动选择（最新）", versions.front(), "A", false});
  size_t count = std::min<size_t>(versions.size(), 12);
  for (size_t i = 0; i < count; ++i) {
    items.push_back({versions[i], i == 0 ? "最新" : "", "", false});
  }
  items.push_back({"返回", "", "", false});
  ui::MenuOptions options;
  options.title = meta::loader_label(loader) + " 版本 · " + mc;
  options.breadcrumb = "一级菜单 / 下载游戏 / 加载器版本";
  int index = ui::menu(term_, options, items, 0);
  if (index < 0 || index == static_cast<int>(items.size()) - 1) return false;
  if (index == 0)
    version.clear();
  else
    version = versions[static_cast<size_t>(index - 1)];
  return true;
}

void App::install_version(const std::string& mc, meta::LoaderKind loader,
                          const std::string& loader_version) {
  meta::Mirror mirror = make_mirror();
  meta::GameInstaller installer(paths_, config_, mirror, http_);
  meta::InstallRequest request;
  request.version_id = mc;
  request.loader = loader;
  request.loader_version = loader_version;
  request.java_path = config_.java_path;

  std::string title = "安装 " + mc;
  if (loader != meta::LoaderKind::Vanilla) title += " · " + meta::loader_label(loader);
  ui::ProgressView progress(term_, title);
  net::Downloader downloader(config_.download_threads,
                             [&progress](const net::DownloadStats& stats) { progress.update(stats); });
  downloader.set_hash_cache(&hash_cache_);
  progress.set_cancel_callback([&downloader]() { downloader.cancel(); });

  meta::InstallResult result;
  bool ok = installer.install(
      request, downloader, [&progress](const std::string& status) { progress.set_status(status); },
      result);
  progress.finish(ok, ok ? "安装完成" : result.error);
  if (!ok) {
    ui::error_box(term_, "安装失败", result.error);
    return;
  }
  save_hash_cache();
  std::vector<std::string> lines = {
      "实例：" + result.instance_id,
      "位置：" + paths_.instance_dir(result.instance_id),
      "加载器：" + meta::loader_label(loader),
  };
  ui::message(term_, "安装完成", lines);

  // 装完游戏顺手检查 Java；缺失时询问是否自动安装
  ensure_java(result.instance_id);

  if (ui::confirm(term_, "下一步", "是否继续为这个实例下载模组？")) mods_screen(result.instance_id);
  if (ui::confirm(term_, "启动游戏", "现在启动 " + result.instance_id + " 吗？"))
    launch_instance(result.instance_id);
}

void App::mods_screen(const std::string& preset_instance) {
  std::string instance = preset_instance;
  if (instance.empty()) instance = pick_instance("③ 下载模组", "选择要安装模组的实例");
  if (instance.empty()) return;

  Json record = meta::load_instance_record(paths_, instance);
  std::string mc = record.get_str("base_version", instance);
  std::string loader = record.get_str("loader", "vanilla");
  if (loader == "vanilla") {
    ui::error_box(term_, "无法安装模组",
                  "实例 " + instance + " 是原版实例。模组需要 Fabric / Quilt / Forge / NeoForge。\n"
                  "请到「② 下载游戏」重新安装一个带加载器的版本。");
    return;
  }
  const std::string mods_dir = paths_.instance_mods_dir(instance, config_.version_isolation);
  fs::ensure_dir(mods_dir);

  meta::Mirror mirror = make_mirror();
  mods::ModApi api(http_, config_, mirror);
  mods::Provider provider = mods::Provider::Domestic;
  if (config_.mod_source == ModSource::CurseForge) provider = mods::Provider::CurseForge;
  if (config_.mod_source == ModSource::Modrinth) provider = mods::Provider::Modrinth;
  api.set_provider(provider);

  std::string query;
  int offset = 0;
  for (;;) {
    ui::draw_waiting(term_, "下载模组",
                     "正在从 " + api.provider_label() + " 搜索 " + mc + " / " + loader + " 的模组…");
    std::vector<mods::ModProject> projects;
    std::string error;
    mods::SearchQuery search;
    search.text = query;
    search.game_version = mc;
    search.loader = loader;
    search.offset = offset;
    search.limit = 20;
    if (!api.search(search, projects, &error)) {
      ui::error_box(term_, "搜索失败", error);
      return;
    }

    std::vector<ui::MenuItem> items;
    items.push_back({"搜索关键字", query.empty() ? "热门模组" : "当前：" + query, "/", false});
    items.push_back({"切换模组源", api.provider_label(), "S", false});
    items.push_back({"已安装模组", "查看 / 删除", "M", false});
    const int projects_start = static_cast<int>(items.size());
    for (const auto& project : projects) {
      std::string hint = "by " + project.author + " · " + str::human_count(project.downloads) +
                         " 次下载 · " + str::ellipsize(project.description, 48);
      items.push_back({project.title, hint, "", false});
    }
    int prev_index = -1;
    int next_index = -1;
    if (offset > 0) {
      prev_index = static_cast<int>(items.size());
      items.push_back({"← 上一页", "", "", false});
    }
    if (projects.size() >= 20) {
      next_index = static_cast<int>(items.size());
      items.push_back({"下一页 →", "", "", false});
    }
    items.push_back({"返回", "", "", false});
    const int back_index = static_cast<int>(items.size()) - 1;

    ui::MenuOptions options;
    options.title = "③ 下载模组";
    options.breadcrumb = "一级菜单 / 下载模组";
    options.header_lines.push_back(th.dim + "实例 " + th.reset + th.text + instance + th.reset +
                                   th.dim + "  ·  " + mc + " / " + loader + "  ·  " +
                                   api.provider_label() + th.reset);
    options.footer_lines.push_back(th.dim + "模组目录 " + mods_dir + th.reset);
    int index = ui::menu(term_, options, items, 0);
    if (index < 0 || index == back_index) return;
    if (index == 0) {
      ui::PromptOptions prompt;
      prompt.title = "搜索模组";
      prompt.label = "关键词";
      prompt.hint = "留空 = 查看热门模组。例如 sodium、jei、机械动力";
      prompt.initial = query;
      std::string text;
      if (ui::prompt(term_, prompt, text)) {
        query = str::trim(text);
        offset = 0;
      }
      continue;
    }
    if (index == 1) {
      std::vector<ui::MenuItem> sources = {
          {"国内镜像 (MCIM)", "无需 API Key，国内访问较快", "", false},
          {"Modrinth", "官方 API，需要能访问 api.modrinth.com", "", false},
          {"CurseForge", "需要 API Key，模组数量最多", "", false},
      };
      ui::MenuOptions source_options;
      source_options.title = "选择模组源";
      source_options.show_back = true;
      int current = api.provider() == mods::Provider::Domestic
                        ? 0
                        : (api.provider() == mods::Provider::Modrinth ? 1 : 2);
      int choice = ui::menu(term_, source_options, sources, current);
      if (choice >= 0) {
        if (choice == 0) api.set_provider(mods::Provider::Domestic);
        if (choice == 1) api.set_provider(mods::Provider::Modrinth);
        if (choice == 2) api.set_provider(mods::Provider::CurseForge);
        if (choice == 2 && config_.curseforge_api_key.empty()) {
          ui::message(term_, "CurseForge 需要 API Key",
                      {"请在「④ 设置 → CurseForge API Key」中填写后再试。"});
        }
        offset = 0;
      }
      continue;
    }
    if (index == 2) {
      Json manifest = mods::ModInstaller::load_manifest(mods_dir);
      std::vector<ui::MenuItem> installed;
      std::vector<std::string> names;
      for (const auto& kv : manifest["files"].members()) {
        names.push_back(kv.first);
        std::string version_name = kv.second.get_str("version_name");
        std::string source = kv.second.get_str("source");
        installed.push_back({kv.first, source + " · " + version_name, "", false});
      }
      if (names.empty()) installed.push_back({"（还没有安装模组）", "", "", true});
      installed.push_back({"返回", "", "", false});
      ui::MenuOptions installed_options;
      installed_options.title = "已安装模组 · " + instance;
      installed_options.breadcrumb = "一级菜单 / 下载模组 / 已安装";
      installed_options.header_lines.push_back(th.dim + "目录 " + mods_dir + th.reset);
      int choice = ui::menu(term_, installed_options, installed, 0);
      if (choice >= 0 && choice < static_cast<int>(names.size())) {
        if (ui::confirm(term_, "删除模组", "确定删除 " + names[choice] + " 吗？", false)) {
          fs::remove_file(fs::join(mods_dir, names[choice]));
          manifest["files"].erase(names[choice]);
          fs::write_file_atomic(mods::ModInstaller::installed_manifest_path(mods_dir),
                                manifest.dump(2) + "\n");
        }
      }
      continue;
    }
    if (index == prev_index) {
      offset = std::max(0, offset - 20);
      continue;
    }
    if (index == next_index) {
      offset += 20;
      continue;
    }
    if (index < projects_start || index >= projects_start + static_cast<int>(projects.size()))
      continue;
    const mods::ModProject& project = projects[static_cast<size_t>(index - projects_start)];

    ui::draw_waiting(term_, project.title, "正在获取适配 " + mc + " / " + loader + " 的文件…");
    std::vector<mods::ModFile> files;
    if (!api.files(project.id, mc, loader, files, &error)) {
      ui::error_box(term_, "获取文件失败", error);
      continue;
    }
    if (files.empty()) {
      ui::message(term_, "没有兼容版本",
                  {"该模组没有适配 " + mc + " + " + loader + " 的文件。",
                   "可以尝试更换模组源或游戏版本。"});
      continue;
    }
    std::vector<ui::MenuItem> file_items;
    for (const auto& file : files) {
      file_items.push_back({file.version_name,
                            file.file_name + " · " + str::human_size(file.size), "", false});
    }
    file_items.push_back({"返回", "", "", false});
    ui::MenuOptions file_options;
    file_options.title = project.title;
    file_options.breadcrumb = "一级菜单 / 下载模组 / 选择版本";
    file_options.header_lines.push_back(th.dim + str::ellipsize(project.description, 90) + th.reset);
    int choice = ui::menu(term_, file_options, file_items, 0);
    if (choice < 0 || choice >= static_cast<int>(files.size())) continue;

    const mods::ModFile selected = files[static_cast<size_t>(choice)];
    ui::ProgressView progress(term_, "安装模组 · " + project.title);
    net::Downloader downloader(
        config_.download_threads,
        [&progress](const net::DownloadStats& stats) { progress.update(stats); });
    downloader.set_hash_cache(&hash_cache_);
    progress.set_cancel_callback([&downloader]() { downloader.cancel(); });
    mods::ModInstaller mod_installer(api, downloader);
    std::string install_error;
    bool ok = mod_installer.install(
        selected, mods_dir, mc, loader,
        [&progress](const std::string& status) { progress.set_status(status); }, &install_error);
    progress.finish(ok, ok ? "安装完成" : install_error);
    if (!ok) {
      ui::error_box(term_, "模组安装失败", install_error);
      continue;
    }
    save_hash_cache();
    bool more = ui::confirm(term_, "安装完成",
                            "已安装 " + selected.file_name + "（含依赖）。继续浏览其他模组？");
    if (!more) return;
  }
}

}  // namespace mct
