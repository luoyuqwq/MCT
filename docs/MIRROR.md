# 自建下载源（静态镜像）规范

MCT 可以把游戏文件、资源文件和模组全部指向你自己的网站。本文件说明目录结构、`mirror.json`
格式、URL 重写规则，以及如何生成和部署。

> 镜像站只需要是**静态文件服务器**：任何能按路径返回文件的 nginx / Apache / OSS / COS / S3 /
> GitHub Pages 都可以。客户端不要求 CORS、不要求特殊 Header。

---

## 1. 目录结构

以 `https://mc.example.com/mirror` 为根：

```
mirror/
├── mirror.json                          # 站点描述（可选，但推荐）
├── mc/
│   └── game/
│       └── version_manifest_v2.json     # 版本清单
├── version/
│   └── 1.20.1/
│       └── json                         # 该版本的 version.json（无扩展名）
├── maven/                               # 对应 libraries.minecraft.net
│   └── com/mojang/brigadier/1.0.18/brigadier-1.0.18.jar
├── assets/                              # 对应 resources.download.minecraft.net
│   └── 5d/5d5d...e0                    # 前两位做子目录，文件名为完整 SHA1
├── data/                                # 对应 piston-data.mojang.com
│   └── v1/objects/<sha1>/client.jar
├── meta/                                # 对应 piston-meta.mojang.com / launchermeta
│   └── v1/packages/<sha1>/<index>.json
├── fabric/
│   ├── maven/                           # 对应 maven.fabricmc.net
│   └── meta/                            # 对应 meta.fabricmc.net
├── neoforge/
│   └── maven/                           # 对应 maven.neoforged.net/releases
└── mods/                                # 可选：自托管模组文件
    ├── modrinth/<filename>.jar
    ├── curseforge/<filename>.jar
    └── domestic/<filename>.jar
```

路径规则很简单：**去掉协议和域名，保留后面的完整路径**，再按上表替换前缀。例如

| 官方 URL | 镜像 URL |
| --- | --- |
| `https://libraries.minecraft.net/com/mojang/brigadier/1.0.18/brigadier-1.0.18.jar` | `<base>/maven/com/mojang/brigadier/1.0.18/brigadier-1.0.18.jar` |
| `https://resources.download.minecraft.net/5d/5d5d...e0` | `<base>/assets/5d/5d5d...e0` |
| `https://piston-data.mojang.com/v1/objects/<sha1>/client.jar` | `<base>/data/v1/objects/<sha1>/client.jar` |
| `https://piston-meta.mojang.com/v1/packages/<sha1>/5.json` | `<base>/meta/v1/packages/<sha1>/5.json` |
| `https://maven.fabricmc.net/net/fabricmc/fabric-loader/0.15.11/fabric-loader-0.15.11.jar` | `<base>/fabric/maven/net/fabricmc/...` |

---

## 2. `mirror.json`

放在站点根目录，客户端在选择「自定义站点」时会先读取它（读不到就用内置默认规则，不会报错）。

```json
{
  "name": "我的 MC 镜像",
  "version": 1,
  "manifest": "mc/game/version_manifest_v2.json",
  "version_json": "version/{id}/json",
  "rules": [
    { "from": "https://libraries.minecraft.net/",          "to": "/maven/" },
    { "from": "https://resources.download.minecraft.net/", "to": "/assets/" },
    { "from": "https://piston-data.mojang.com/",           "to": "/data/" },
    { "from": "https://piston-meta.mojang.com/",           "to": "/meta/" },
    { "from": "https://launchermeta.mojang.com/",          "to": "/meta/" },
    { "from": "https://launcher.mojang.com/",              "to": "/data/" },
    { "from": "https://maven.minecraftforge.net/",         "to": "/maven/" },
    { "from": "https://files.minecraftforge.net/maven/",   "to": "/maven/" },
    { "from": "https://maven.fabricmc.net/",               "to": "/fabric/maven/" },
    { "from": "https://meta.fabricmc.net/",                "to": "/fabric/meta/" },
    { "from": "https://maven.neoforged.net/releases/",     "to": "/neoforge/maven/" },
    { "from": "https://meta.quiltmc.org/",                 "to": "/quilt/meta/" }
  ]
}
```

字段：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `name` | 否 | 显示在启动器「下载源」里的名字 |
| `manifest` | 否 | 版本清单相对路径，默认 `mc/game/version_manifest_v2.json` |
| `version_json` | 否 | 版本 JSON 相对路径模板，`{id}` 会被替换成版本号 |
| `rules` | 否 | 前缀替换规则，按顺序匹配第一条命中的；`to` 以 `/` 开头表示相对站点根，也可以写完整 URL |

`version_manifest_v2.json` 中每个版本的 `url` 字段仍指向 Mojang。客户端**不会**直接用它，
而是用 `version_json` 模板从你的站点取；取不到时才回退到官方 `url`。因此你的站点只需要放
`version/<id>/json`。

---

## 3. 客户端如何选择地址

对每个要下载的文件，客户端按以下顺序构造候选地址：

1. 镜像地址（`mirror.json` 规则重写后的 URL）
2. 官方原始 URL（兜底）

两个都失败才算这个文件下载失败。下载完成后按 version JSON 里的 SHA1 校验；不匹配会删除并重试。

这也是为什么镜像站可以**只放一部分文件**（例如只镜像 libraries、其余走官方），不会导致安装失败。

---

## 4. 生成镜像

`tools/mct-publish.ps1` 会：

- 从 BMCLAPI 拉取版本清单、版本 JSON、客户端 jar、libraries、日志配置、资源索引与全部资源对象
- 按上面的规则写入本地目录
- 下载时校验 SHA1，失败自动回退官方地址
- 已存在且校验通过的文件自动跳过（可重复运行续传）
- 最后写出 `mirror.json`

```powershell
# 只镜像一个版本，不含资源对象（体积小，适合先验证）
.\tools\mct-publish.ps1 -Version 1.20.1 -OutDir D:\mirror -SkipAssets

# 完整镜像两个版本（资源对象会去重，多个版本共用 assets/objects）
.\tools\mct-publish.ps1 -Version 1.20.1,1.20.4 -OutDir D:\mirror -BaseUrl https://mc.example.com/mirror -Zip

# 追加更多版本时继续用同一个 OutDir 即可增量补全
.\tools\mct-publish.ps1 -Version 1.21.1 -OutDir D:\mirror
```

参数：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `-Version` | 必填 | 一个或多个版本号 |
| `-OutDir` | `.\mirror` | 输出目录 |
| `-BaseUrl` | `https://mc.example.com/mirror` | 只写进提示信息与 mirror.json 备注，不影响目录结构 |
| `-Name` | `MCT 镜像站` | mirror.json 的 `name` |
| `-SkipAssets` | 关 | 跳过资源对象下载（资源文件是体积大头） |
| `-Zip` | 关 | 另外打一个 zip，方便上传对象存储 |

资源对象很大：1.20.1 约 500 MB、几千个文件。脚本是顺序下载的，第一次会比较慢；
中断后重新执行会从断点继续。

---

## 5. 部署

把 `mirror/` 里的内容原样上传到站点根：

```bash
rclone copy D:/mirror remote:bucket/mirror -P
# 或
scp -r D:/mirror/* user@host:/var/www/mirror/
# 或（对象存储）
ossutil cp -r D:/mirror oss://bucket/mirror/
```

nginx 最小配置：

```nginx
server {
    listen 443 ssl http2;
    server_name mc.example.com;

    # 静态文件
    root /var/www;
    location /mirror/ {
        autoindex off;
        # 客户端会校验 SHA1，可以放心长缓存
        add_header Cache-Control "public, max-age=31536000, immutable";
    }

    # 版本清单建议短缓存，方便你更新
    location = /mirror/mc/game/version_manifest_v2.json {
        add_header Cache-Control "public, max-age=600";
        try_files $uri =404;
    }

    # mirror.json 不缓存
    location = /mirror/mirror.json {
        add_header Cache-Control "no-cache";
        try_files $uri =404;
    }
}
```

要点：

- `version/<id>/json` 没有 `.json` 后缀，确保你的服务器不会把它当成目录；
  部分对象存储需要对无扩展名文件设置 `Content-Type: application/json`。
- 游戏 jar / 模组 jar 建议 `Content-Type: application/octet-stream`。
- 客户端不检查 HTTP 状态码以外的内容，也不发 `Range` 请求，分段下载由客户端自己按文件并发。
- 建议开启 HTTPS。HTTP 也能用，但启动器与浏览器都更容易被拦截/篡改。

---

## 6. 模组镜像（可选）

如果希望模组也走自己的站，把文件按来源放到：

```
<base>/mods/modrinth/<原始文件名>
<base>/mods/curseforge/<原始文件名>
<base>/mods/domestic/<原始文件名>
```

客户端在安装模组时会**优先**尝试这个地址，失败再用 API 给出的官方 CDN 地址，并校验 SHA1。
文件名必须与 API 返回的 `filename` 完全一致。

也可以把常用整合包里的模组批量放进去，配合你站点的对象存储做加速。

---

## 7. 合规与安全

- 只镜像 Mojang 公开分发的客户端文件、依赖库与资源文件；不要镜像盗版启动器、破解补丁或付费内容。
- `mirror.json` 与资源都是只读静态文件，不需要任何后端接口，也就没有鉴权/注入面。
- 不要在同一域名下放置可执行脚本上传入口；镜像目录应只读。
- 如果站点带宽有限，可以只镜像 libraries + assets，把版本清单与版本 JSON 指向官方（客户端支持部分镜像）。
- 日志会记录实际使用的镜像地址，便于排查 404。
