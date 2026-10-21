# MCT · Minecraft 终端启动器

> Designed By **Cache_Rikka**

**下载**：Windows x64 单文件版见 [Releases](https://github.com/luoyuqwq/MCT/releases/latest)。

用 C++17 写的**终端内 Minecraft 启动器**，界面与操作参照 HMCL / PCL2，但不依赖任何 GUI：
纯 ANSI 渲染、方向键选择、下载进度条，在 Windows Terminal / cmd / PowerShell / Linux 终端里都能跑。

- 无第三方运行库依赖（Windows 使用 WinHTTP，静态链接）
- 自带 JSON / ZIP / DEFLATE / MD5 / SHA1 实现，不引入 OpenSSL、libcurl、ncurses
- 所有资源（游戏、库、资源文件、模组）都支持官方源、国内镜像和你自己的站点

```
  MCT  Minecraft 终端启动器                        v0.1.0 · Designed By Cache_Rikka
  ────────────────────────────────────────────────────────────────────────────
  账号 Steve · 离线              游戏源 国内镜像 BMCLAPI
  版本隔离 开启                  模组源 国内镜像

  ❯ ① 登录 / 皮肤               离线账号 · 微软正版 · 更换皮肤
    ② 下载游戏                  原版 / Fabric / Quilt / Forge / NeoForge
    ③ 下载模组                  国内镜像 / CurseForge / Modrinth
    ④ 设置                      下载源 · 版本隔离 · 内存 · Java
    启动游戏                    3 个已安装实例
    退出
  ────────────────────────────────────────────────────────────────────────────
  ↑↓ 选择   Enter 确认   Esc 返回
```

---

## 功能

| 模块 | 说明 |
| --- | --- |
| 账号 | 离线账号（OfflinePlayer UUID v3）、微软正版（设备码 OAuth2 → Xbox Live → Minecraft Services，免注册 Azure），令牌自动刷新，多账号切换 |
| 皮肤 | 查询当前皮肤、上传本地 PNG（64×64 / 64×32，classic / slim）、恢复默认 |
| 下载游戏 | 版本清单 + 原版客户端 + 全部库 + 资源索引 + 资源对象 + natives 解压，支持 Fabric / Quilt / Forge / NeoForge |
| 下载模组 | Modrinth、CurseForge、国内镜像（MCIM），按游戏版本与加载器过滤，自动安装必需依赖，可查看/删除已装模组 |
| 启动 | Java 自动探测与一键安装、classpath 构建、JVM/游戏参数按版本 JSON 规则生成、启动后等待退出并回显退出码 |
| 目录 | 游戏安装目录可自定义（默认 `<数据目录>/game`），切换时可选自动复制现有 `versions/libraries/assets/mods` |
| 设置 | 下载源、自定义站点、模组源、版本隔离、线程数、内存、Java 路径、额外 JVM 参数、快照显示、游戏目录、安装 Java |
| 镜像 | 支持自建静态资源站（含生成脚本），见 [docs/MIRROR.md](docs/MIRROR.md) |

### 菜单层级（与需求对应）

- **一级菜单**：`① 登录 / 皮肤`（离线登录、正版登录、更换皮肤）
- **二级菜单**：`② 下载游戏`（选版本 → 选加载器 → 安装）
- **三级菜单**：`③ 下载模组`；也可在「下载游戏」安装完成后直接进入（"是否继续下载模组"）
- **四级菜单**：`④ 设置`（版本隔离、下载源等）

截图中的层级标注在每一屏左上角（如 `一级菜单 / 下载游戏 / 加载器`）。

---

## 编译

要求：CMake ≥ 3.16、支持 C++17 的编译器。**没有第三方依赖**。

### Windows · MSVC

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
:: 产物: build\Release\mct.exe
```

### Windows · MinGW-w64 / MSYS2

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# 产物: build/mct.exe（默认静态链接 libstdc++/libgcc，可直接分发）
```

### Linux / macOS

网络层回退到 `curl` 命令行，需要系统安装 `curl`：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/mct
```

> Linux/macOS 分支为尽力兼容实现，主要开发与验证平台是 Windows 10/11。

---

## 运行

```bat
mct
mct --home D:\MC            :: 指定数据目录
mct --install 1.20.1        :: 无界面安装某个版本
mct --install 1.20.1 --loader fabric --loader-version 0.15.11
mct --help
```

数据目录默认 `%APPDATA%\MCT`（可用 `--home` 或环境变量 `MCT_HOME` 覆盖）：

```
<home>/
  config.json          设置
  accounts.json        账号（含正版令牌，注意保密）
  cache/               版本清单、日志配置、Forge 安装器缓存
  logs/mct.log         日志（UTF-8）
  game/                游戏根目录（.minecraft 等价物，可在设置里改到别处）
    versions/<id>/     version.json / client.jar / natives / mct.json
    libraries/
    assets/
    mods/              关闭版本隔离时共用
```

启动游戏时会把控制台交还给游戏进程，退出后按 Enter 回到启动器。

---

## 账号

### 离线登录

输入 3–16 位字母/数字/下划线即可。UUID 按 `OfflinePlayer:<name>` 的 MD5（UUID v3）生成，与 HMCL 等启动器一致，可进单机与"离线模式"服务器。

### 微软正版登录

只保留**设备码登录**这一种方式，不需要注册 Azure 应用，使用公开的 Minecraft 客户端 ID
`00000000402b5328`（官方启动器与多个开源启动器使用，Live 端点）。

流程（`① 登录 / 皮肤 → 微软正版登录`）：

1. 启动器申请设备码，自动打开浏览器并预填代码（链接同时复制到剪贴板）
2. 也可以手动打开 <https://www.microsoft.com/link>，输入终端显示的 8 位代码
3. 使用拥有 Minecraft 的微软账号登录并授权
4. 终端每 5 秒轮询一次，授权完成后自动继续获取 Xbox / Minecraft 档案

界面会显示轮询状态、已等待秒数和本次代码；失败时错误会写入 `<数据目录>/logs/mct.log`
（搜索“设备码轮询”）。

> 公共 Client ID 由微软公开、社区通用，不排除微软以后调整策略。若失效，只能等社区更新
> Client ID（当前版本只保留设备码登录，不再提供自建 Azure 应用入口）。

登录成功后保存 Minecraft access token + refresh token，令牌过期会在启动/换皮肤前自动刷新。
若账号没有 Minecraft: Java Edition 所有权、没有 Xbox 档案、是儿童账号，都会给出对应的中文提示。

### 更换皮肤

需要正版账号。支持：

- 上传本地 PNG：必须是 64×64 或 64×32、小于 1 MB
- 模型：`classic`（Steve 手臂）或 `slim`（Alex 手臂）
- 恢复默认皮肤

上传走 `PUT /minecraft/profile/skins` 的 multipart 请求，失败会显示 Mojang 返回的错误文本。

---

## 下载游戏

1. `② 下载游戏` → 获取版本清单（默认 BMCLAPI，可切官方或自定义站点）
2. 选择版本（`F` 切换正式版/快照，`/` 搜索版本号）
3. 选择加载器：原版 / Fabric / Quilt / Forge / NeoForge（NeoForge 需 1.20.2+）
4. Fabric/Quilt 可选加载器版本（默认最新）；Forge/NeoForge 会下载官方安装器并用本机 Java 执行 `--installClient`
5. 自动完成：客户端 jar、libraries、资源索引、资源对象、natives 解压
6. 完成后可直接「继续下载模组」和「立即启动」

下载是 8 线程并发（可调）、镜像优先官方兜底、按 SHA1 校验、已存在且正确的文件自动跳过，因此中断后重跑可续传。

实例命名固定，避免同名冲突：

| 加载器 | 实例目录 |
| --- | --- |
| 原版 | `versions/1.20.1` |
| Fabric | `versions/1.20.1-fabric` |
| Quilt | `versions/1.20.1-quilt` |
| Forge | 安装器生成的 `versions/1.20.1-forge-47.x.x` |
| NeoForge | 安装器生成的 `versions/1.20.1-neoforge-...` |

---

## Java 运行时

启动或安装版本时会读取 version JSON 的 `javaVersion.majorVersion`，自动在本机寻找合适的 Java；
找不到（或版本过低）时会提示一键安装：

- 自动选择 Temurin JRE：Java 8 / 17 / 21（按版本要求，必要时向上兼容）
- 下载优先走**清华 TUNA 的 Adoptium 镜像**，失败自动回退 **Azul Zulu**、**Microsoft OpenJDK**
- 安装到 `<数据目录>/java/temurin-<major>`，之后会被自动探测，不需要手填路径
- 也可以在 `④ 设置 → 安装 Java 运行时` 手动装，或命令行：

```bat
mct --install-java 17
```

> 自动安装目前只支持 Windows；Linux/macOS 请用系统包管理器安装 JDK。

---

## 下载模组

入口：`③ 下载模组`（或在安装完成时选择继续），先选实例，再选来源：

| 来源 | 说明 |
| --- | --- |
| 国内镜像 (MCIM) | 走 `mod.mcimirror.top` 的 Modrinth 兼容 API，无需 Key；镜像不可用时自动回退官方 API |
| Modrinth | `api.modrinth.com` 官方 API |
| CurseForge | 需要 API Key（`console.curseforge.com` 免费申请）；未填时会明确提示 |

- 按实例的 Minecraft 版本 + 加载器过滤，不会给出装不上的文件
- 自动解析必需依赖（Modrinth 的 `required` 依赖、CurseForge 的 relationType=3），递归安装
- 安装记录写入 `mods/.mct-mods.json`，可查看与删除
- 模组目录：版本隔离开启时 `versions/<id>/mods`，关闭时 `game/mods`

> 原版实例不允许装模组，会提示先安装带加载器的版本。

---

## 设置

| 项 | 说明 |
| --- | --- |
| 下载源 | 官方 Mojang / 国内镜像 BMCLAPI / 自定义站点 |
| 自定义站点地址 | 你的静态镜像根地址，需要提供 `mirror.json`（见 [docs/MIRROR.md](docs/MIRROR.md)） |
| 模组源 | 国内镜像 / CurseForge / Modrinth |
| CurseForge API Key | CurseForge 源必填 |
| 版本隔离 | **开启**：`versions/<id>` 作为游戏目录，每个版本独立 `mods/`、`saves/`、`options.txt`（HMCL 风格）；**关闭**：统一使用游戏根目录 |
| 游戏目录 | 自定义游戏安装根目录（默认 `<数据目录>/game`），可指向其它磁盘或已有的 `.minecraft`；切换时可选择复制现有文件 |
| 安装 Java 运行时 | 一键下载 Temurin JRE（8 / 17 / 21），装到 `<数据目录>/java`，自动被识别 |
| 下载线程 | 1–64，默认 16 |
| 最大/最小内存 | 传给 JVM 的 `-Xmx` / `-Xms` |
| Java 路径 | 留空自动探测（PATH、JAVA_HOME、`<home>/java`、常见安装目录） |
| 额外 JVM 参数 | 追加到 JVM 参数 |
| Java 路径 | 留空自动探测（PATH、JAVA_HOME、`<home>/java`、常见安装目录） |
| 显示快照版本 | 版本列表默认是否包含快照 |

版本隔离切换后，已安装的模组不会自动搬移（切换前请确认实例目录）。游戏本体、libraries、assets 始终共享，不重复占用空间。

---

## 自建下载源

见 [docs/MIRROR.md](docs/MIRROR.md)。核心流程：

```powershell
# 生成镜像目录（默认从 BMCLAPI 拉取，自动跳过已存在文件）
.\tools\mct-publish.ps1 -Version 1.20.1,1.20.4 -OutDir D:\mirror -BaseUrl https://mc.example.com/mirror

# 上传（任选其一）
rclone copy D:\mirror remote:bucket/mirror -P
scp -r D:\mirror/* user@host:/var/www/mirror/
```

然后在 `④ 设置 → 下载源 → Custom` 填入 `https://mc.example.com/mirror`。

---

## 命令行

| 参数 | 说明 |
| --- | --- |
| `--home <dir>` | 数据目录 |
| `--game-dir <dir>` | 游戏安装目录（覆盖配置，不写回） |
| `--install <版本>` | 无界面安装（打印进度条，适合脚本/CI） |
| `--loader <name>` | `vanilla` / `fabric` / `quilt` / `forge` / `neoforge` |
| `--loader-version <v>` | 指定加载器版本，省略为最新 |
| `--launch <实例>` | 无界面启动已安装的实例（使用当前账号） |
| `--install-java <版本>` | 自动下载安装 Java（8 / 17 / 21），例如 `--install-java 17` |
| `--version` / `--help` | 版本 / 帮助 |

---

## 配置文件

`<home>/config.json`，缺失项使用默认值：

```json
{
  "game_source": "bmclapi",
  "custom_mirror": "",
  "custom_mirror_name": "自定义站点",
  "mod_source": "domestic",
  "version_isolation": true,
  "download_threads": 16,
  "java_path": "",
  "game_dir": "",
  "max_memory_mb": 4096,
  "min_memory_mb": 512,
  "extra_jvm_args": "",
  "curseforge_api_key": "",
  "last_version": "",
  "show_snapshots": false,
  "proxy": "",
  "window_width": 854,
  "window_height": 480,
  "fullscreen": false,
  "download_assets": true
}
```

`proxy` 形如 `http://127.0.0.1:7890`（WinHTTP 自动使用系统代理时留空即可）。

---

## 项目结构

```
src/
  main.cpp            入口、参数、无界面安装
  app/                菜单与业务流程
  ui/                 终端控制（ANSI、按键）、菜单/输入框/进度条
  auth/               账号存储、微软 OAuth、皮肤
  meta/               镜像源、版本 JSON / 规则 / 库、安装器、版本清单
  launch/             Java 探测、启动参数与进程
  mods/               Modrinth / CurseForge / 国内镜像、依赖安装
  net/                HTTP（WinHTTP / curl）、多线程下载器
  core/               配置与目录布局
  util/               JSON、哈希、ZIP/Inflate、文件系统、进程、日志、字符串
tests/
  selftest.cpp        基础库自测（哈希、JSON、UTF-8、Inflate、ZIP）
  probe.cpp           网络冒烟测试（镜像 → 版本 JSON → 模组 API）
tools/
  mct-publish.ps1     生成可上传的静态镜像
docs/
  MIRROR.md           镜像站规范
```

### 测试

```bash
# 基础库（含真实 deflate/zip 夹具）
build/mct_selftest.exe tests/fixtures

# 网络链路（需要联网，不下载游戏文件）
build/mct_probe.exe
```

### 验证情况

- Windows 10 + MinGW-w64 GCC 16.2：Debug / Release 均编译通过，无警告
- `mct_selftest`：MD5/SHA1（含百万字节）、JSON、UTF-8 显示宽度、真实 DEFLATE/ZIP 夹具、
  版本继承合并、规则过滤、参数替换、Maven 路径构造
- `mct_probe`：BMCLAPI 清单 → 1.20.1 版本 JSON → 元数据解析 → 镜像下载；MCIM 搜索 →
  Modrinth 文件列表（含 SHA1）
- 端到端：1.7.10 全量安装（客户端、36 个库、natives 解压、安装记录），并用测试用假 Java
  验证了启动命令生成与进程启动
- 游戏目录：`--game-dir` 与 `config.json` 两种方式都验证了安装/启动路径切换；`fs::copy_dir`
  的复制与进度回调由自测覆盖
- 单文件：Release 产物只依赖系统 DLL；哈希缓存在重复安装时把 26 MB 文件的校验时间从
  ~1.3 s 降到 ~0.24 s
- 未在本机跑通：真实 Java 启动游戏（本机未安装 Java）、Fabric/Forge 完整安装（测试期间 BMCLAPI 触发 429
  限流，解析与合并逻辑由合成数据测试覆盖）；微软设备码登录已由用户实测成功

---

## 常见问题

**启动提示找不到 Java**：在弹窗里选择自动安装即可（Temurin JRE，TUNA 镜像），也可以到
`④ 设置 → 安装 Java 运行时` 手动安装，或填自己的 `java.exe` 路径。1.17+ 需要 Java 17+，
1.20.5+ 推荐 21。

**设备码登录一直提示等待/超时**：确认终端显示的代码与 `microsoft.com/link` 输入的完全一致，且使用的是拥有
Minecraft 的微软账号。每次轮询的 HTTP 状态与微软返回的错误都会写进 `<数据目录>/logs/mct.log`（搜索“设备码轮询”），
可据此判断是代码不匹配、网络拦截还是账号问题。

**CurseForge 搜索失败**：先申请 API Key 并填入设置；该 Key 只保存在本地 `config.json`。

**国内镜像模组搜索 503**：程序会自动回退到 Modrinth 官方 API，日志里会有一条警告。

**下载中断**：重新执行安装即可，已校验通过的文件会跳过。

**提示"镜像站限流（429）"**：BMCLAPI 对请求频率有限制（2026 年起尤其明显）。程序会自动读取
`Retry-After` 等待并优先改用官方地址；如果仍然很慢，建议在设置里切换到「官方 Mojang」或你自己的
镜像站，或者降低下载线程数。

**游戏窗口没出现**：看终端里的游戏输出与 `<home>/logs/mct.log`；确认内存设置不是过大、Java 版本匹配。

**切换游戏目录后看不到原来的版本**：新目录默认为空。切换时如果选择「复制游戏文件」，会把
`versions/libraries/assets/mods` 一起复制过去；不复制则会从旧目录继续使用原文件，但启动器只列出新目录里的实例。

**中文乱码**：请使用 Windows Terminal 或把控制台字体设为等宽中文字体（如 Sarasa Mono SC、更纱黑体）；程序已把代码页设为 UTF-8。

---

## 单文件与性能

- **单 exe 发布**：MinGW 默认 `-static -static-libgcc -static-libstdc++`，MSVC 使用 `/MT`，
  发布目录只需要 `mct.exe`，不依赖 libstdc++/winpthread 等 DLL。
- **体积**：Release 开启 `-ffunction-sections -fdata-sections -Wl,--gc-sections` 与 `-s`（strip），
  并可选用 LTO（`-DMCT_ENABLE_LTO=OFF` 可关闭）。
- **重复安装加速**：`<home>/cache/filehashes.json` 记住已校验文件的（大小、mtime、SHA1），
  再次安装/启动检查时不再重复哈希几百 MB 的资源文件。
- **下载**：默认 16 线程（设置里可调到 64），镜像优先、官方兜底，429 自动读取 `Retry-After`
  退避并改用官方地址；下载与校验都在内存里流式处理，不会整包读入。
- **内存**：JSON 解析、ZIP/Inflate、下载缓冲区均为小对象 + 1 MB 级缓冲，实测 Release 版
  安装 1.7.10（36 个文件、26 MB、含 natives 解压）峰值约 **46 MB**。
- **体积/依赖实测**：Release `mct.exe` 约 **1.55 MB**，依赖仅
  `KERNEL32.dll`、`msvcrt.dll`、`WINHTTP.dll`，可直接拷贝到任意 Win10/11 x64 机器运行。

---

## 说明与边界

- 只支持正版账号登录与离线账号；不提供任何绕过正版验证的功能。
- 不内置任何第三方账号服务、皮肤站或整合包；模组内容版权归原作者。
- 启动器会把完整的启动命令写进日志，便于排查，但日志中**包含 access token**，分享前请删除。
- Minecraft 是 Mojang / Microsoft 的商标，本项目与其无关联。

## 致谢

Designed By **Cache_Rikka**。

## 许可证

MIT，见 [LICENSE](LICENSE)。
