#requires -version 5.1
<#
.SYNOPSIS
    把 Minecraft 版本文件整理成可上传到自己网站的静态镜像目录。

.DESCRIPTION
    生成与 MCT 客户端「自定义站点」兼容的目录结构，并写出 mirror.json。
    下载默认走 BMCLAPI（国内较快），失败时回退 Mojang 官方地址。
    重复运行会自动跳过已下载且校验通过的文件，可断点续传。

.EXAMPLE
    .\tools\mct-publish.ps1 -Version 1.20.1 -OutDir D:\mirror -BaseUrl https://mc.example.com/mirror

.EXAMPLE
    .\tools\mct-publish.ps1 -Version 1.20.1,1.20.4 -OutDir D:\mirror -SkipAssets

.NOTES
    Assets 体积很大（每个版本数百 MB），只想快速试通链路时用 -SkipAssets。
#>
param(
    [Parameter(Mandatory = $true)][string[]]$Version,
    [string]$OutDir = ".\mirror",
    [string]$BaseUrl = "https://mc.example.com/mirror",
    [string]$Name = "MCT 镜像站",
    [switch]$SkipAssets,
    [switch]$Zip
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
} catch { }

# 与 src/meta/mirror.cpp 中 Mirror::add_default_rules() 保持一致
$rules = @(
    @{ From = 'https://libraries.minecraft.net/';              To = '/maven/' },
    @{ From = 'https://resources.download.minecraft.net/';     To = '/assets/' },
    @{ From = 'https://piston-data.mojang.com/';               To = '/data/' },
    @{ From = 'https://piston-meta.mojang.com/';               To = '/meta/' },
    @{ From = 'https://launchermeta.mojang.com/';              To = '/meta/' },
    @{ From = 'https://launcher.mojang.com/';                  To = '/data/' },
    @{ From = 'https://maven.minecraftforge.net/';             To = '/maven/' },
    @{ From = 'https://files.minecraftforge.net/maven/';       To = '/maven/' },
    @{ From = 'https://maven.fabricmc.net/';                   To = '/fabric/maven/' },
    @{ From = 'https://meta.fabricmc.net/';                    To = '/fabric/meta/' },
    @{ From = 'https://maven.neoforged.net/releases/';         To = '/neoforge/maven/' },
    @{ From = 'https://meta.quiltmc.org/';                     To = '/quilt/meta/' }
)

$fastRules = @(
    @{ From = 'https://libraries.minecraft.net/';              To = 'https://bmclapi2.bangbang93.com/maven/' },
    @{ From = 'https://resources.download.minecraft.net/';     To = 'https://bmclapi2.bangbang93.com/assets/' },
    @{ From = 'https://piston-data.mojang.com/';               To = 'https://bmclapi2.bangbang93.com/' },
    @{ From = 'https://piston-meta.mojang.com/';               To = 'https://bmclapi2.bangbang93.com/' },
    @{ From = 'https://launchermeta.mojang.com/';              To = 'https://bmclapi2.bangbang93.com/' },
    @{ From = 'https://launcher.mojang.com/';                  To = 'https://bmclapi2.bangbang93.com/' },
    @{ From = 'https://maven.minecraftforge.net/';             To = 'https://bmclapi2.bangbang93.com/maven/' },
    @{ From = 'https://maven.fabricmc.net/';                   To = 'https://bmclapi2.bangbang93.com/maven/' },
    @{ From = 'https://meta.fabricmc.net/';                    To = 'https://bmclapi2.bangbang93.com/fabric-meta/' },
    @{ From = 'https://maven.neoforged.net/releases/';         To = 'https://bmclapi2.bangbang93.com/maven/' }
)

function Get-MirrorRelative([string]$url) {
    foreach ($rule in $rules) {
        if ($url.StartsWith($rule.From)) {
            return $rule.To.TrimStart('/') + $url.Substring($rule.From.Length)
        }
    }
    return $null
}

function Get-FastUrl([string]$url) {
    foreach ($rule in $fastRules) {
        if ($url.StartsWith($rule.From)) {
            return $rule.To + $url.Substring($rule.From.Length)
        }
    }
    return $url
}

$script:processed = 0
$script:downloaded = 0

function Save-RemoteFile([string]$url, [string]$relative, [string]$sha1) {
    if ([string]::IsNullOrWhiteSpace($relative)) { return }
    $dest = Join-Path $OutDir $relative
    if (Test-Path -LiteralPath $dest) {
        if ([string]::IsNullOrWhiteSpace($sha1)) { return }
        $existing = (Get-FileHash -Algorithm SHA1 -LiteralPath $dest).Hash
        if ($existing -eq $sha1.ToUpper()) { return }
    }
    $parent = Split-Path -Parent $dest
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    $fast = Get-FastUrl $url
    try {
        Invoke-WebRequest -Uri $fast -OutFile $dest -UseBasicParsing -TimeoutSec 300
    } catch {
        if ($fast -ne $url) {
            Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing -TimeoutSec 300
        } else {
            throw
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($sha1)) {
        $got = (Get-FileHash -Algorithm SHA1 -LiteralPath $dest).Hash
        if ($got -ne $sha1.ToUpper()) {
            Remove-Item -LiteralPath $dest -Force
            throw "SHA1 校验失败: $relative"
        }
    }
    $script:downloaded++
}

function Test-LibraryAllowed($lib) {
    if (-not $lib.rules) { return $true }
    $allow = $false
    foreach ($rule in $lib.rules) {
        if ($rule.os -and $rule.os.name -and $rule.os.name -ne 'windows') { continue }
        if ($rule.action -eq 'allow') { $allow = $true }
        elseif ($rule.action -eq 'disallow') { $allow = $false }
    }
    return $allow
}

function Get-MavenPath([string]$name) {
    $coords = $name
    $ext = 'jar'
    $at = $coords.IndexOf('@')
    if ($at -ge 0) { $ext = $coords.Substring($at + 1); $coords = $coords.Substring(0, $at) }
    $parts = $coords.Split(':')
    if ($parts.Count -lt 3) { return $null }
    $group = $parts[0].Replace('.', '/')
    $artifact = $parts[1]
    $version = $parts[2]
    $classifier = ''
    if ($parts.Count -ge 4) { $classifier = $parts[3] }
    $file = "$artifact-$version"
    if ($classifier) { $file += "-$classifier" }
    return "$group/$artifact/$version/$file.$ext"
}

Write-Host "MCT 镜像发布工具" -ForegroundColor Cyan
Write-Host "输出目录: $OutDir"
Write-Host "站点地址: $BaseUrl"
Write-Host ""

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

$manifestUrl = 'https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json'
$manifestRel = 'mc/game/version_manifest_v2.json'
Write-Host "[1/4] 下载版本清单..."
Save-RemoteFile $manifestUrl $manifestRel $null
$manifestPath = Join-Path $OutDir $manifestRel
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json

foreach ($ver in $Version) {
    Write-Host ""
    Write-Host "[*] 处理版本 $ver" -ForegroundColor Yellow
    $entry = $manifest.versions | Where-Object { $_.id -eq $ver } | Select-Object -First 1
    if (-not $entry) {
        Write-Warning "版本清单中找不到 $ver，跳过"
        continue
    }

    $jsonRel = "version/$ver/json"
    Write-Host "    版本 JSON..."
    try {
        Save-RemoteFile "https://bmclapi2.bangbang93.com/version/$ver/json" $jsonRel $null
    } catch {
        Save-RemoteFile $entry.url $jsonRel $null
    }
    $versionJson = Get-Content -LiteralPath (Join-Path $OutDir $jsonRel) -Raw -Encoding UTF8 | ConvertFrom-Json

    if ($versionJson.downloads.client.url) {
        $rel = Get-MirrorRelative $versionJson.downloads.client.url
        if ($rel) {
            Write-Host "    客户端 jar..."
            Save-RemoteFile $versionJson.downloads.client.url $rel $versionJson.downloads.client.sha1
        }
    }

    Write-Host "    库文件 ($($versionJson.libraries.Count) 个声明)..."
    foreach ($lib in $versionJson.libraries) {
        if (-not (Test-LibraryAllowed $lib)) { continue }
        $artifact = $null
        if ($lib.downloads -and $lib.downloads.artifact -and $lib.downloads.artifact.url) {
            $artifact = $lib.downloads.artifact
        }
        if ($artifact) {
            $rel = Get-MirrorRelative $artifact.url
            if ($rel) { Save-RemoteFile $artifact.url $rel $artifact.sha1 }
        } elseif ($lib.name -and -not $lib.downloads) {
            $path = Get-MavenPath $lib.name
            if ($path) {
                $url = "https://libraries.minecraft.net/$path"
                Save-RemoteFile $url "maven/$path" $null
            }
        }
        if ($lib.natives -and $lib.natives.windows) {
            $classifier = $lib.natives.windows.Replace('${arch}', '64')
            $cls = $lib.downloads.classifiers.$classifier
            if ($cls -and $cls.url) {
                $rel = Get-MirrorRelative $cls.url
                if ($rel) { Save-RemoteFile $cls.url $rel $cls.sha1 }
            }
        }
    }

    if ($versionJson.logging -and $versionJson.logging.client -and $versionJson.logging.client.file.url) {
        $logFile = $versionJson.logging.client.file
        $rel = Get-MirrorRelative $logFile.url
        if ($rel) { Save-RemoteFile $logFile.url $rel $logFile.sha1 }
    }

    if ($versionJson.assetIndex -and $versionJson.assetIndex.url) {
        $index = $versionJson.assetIndex
        $indexRel = Get-MirrorRelative $index.url
        if ($indexRel) {
            Write-Host "    资源索引..."
            Save-RemoteFile $index.url $indexRel $index.sha1
            if (-not $SkipAssets) {
                $indexPath = Join-Path $OutDir $indexRel
                if (Test-Path -LiteralPath $indexPath) {
                    $assetJson = Get-Content -LiteralPath $indexPath -Raw -Encoding UTF8 | ConvertFrom-Json
                    $objects = @($assetJson.objects.PSObject.Properties)
                    Write-Host "    资源文件 ($($objects.Count) 个，较慢，可重复运行续传)..."
                    $i = 0
                    foreach ($prop in $objects) {
                        $hash = $prop.Value.hash
                        if (-not $hash -or $hash.Length -lt 2) { continue }
                        $prefix = $hash.Substring(0, 2)
                        $url = "https://resources.download.minecraft.net/$prefix/$hash"
                        Save-RemoteFile $url "assets/$prefix/$hash" $hash
                        $i++
                        if ($i % 200 -eq 0) { Write-Host "      已处理 $i / $($objects.Count)" }
                    }
                }
            }
        }
    }
}

Write-Host ""
Write-Host "[2/4] 写入 mirror.json..."
$mirrorConfig = [ordered]@{
    name         = $Name
    version      = 1
    manifest     = 'mc/game/version_manifest_v2.json'
    version_json = 'version/{id}/json'
    rules        = @($rules | ForEach-Object { [ordered]@{ from = $_.From; to = $_.To } })
}
$mirrorConfig | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutDir 'mirror.json') -Encoding UTF8

$files = @(Get-ChildItem -LiteralPath $OutDir -Recurse -File)
$totalBytes = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ("[3/4] 完成：{0} 个文件，共 {1:N1} MB" -f $files.Count, ($totalBytes / 1MB))

if ($Zip) {
    $zipPath = "$OutDir.zip"
    Write-Host "[4/4] 打包 $zipPath ..."
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Compress-Archive -Path (Join-Path $OutDir '*') -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host ("      压缩包 {0:N1} MB" -f ((Get-Item -LiteralPath $zipPath).Length / 1MB))
} else {
    Write-Host "[4/4] 跳过打包（可加 -Zip）"
}

Write-Host ""
Write-Host "下一步：" -ForegroundColor Green
Write-Host "  1. 把 $OutDir 里的全部内容上传到 $BaseUrl 对应的目录（保持相对路径不变）"
Write-Host "  2. 用一个静态 Web 服务器提供这些文件（nginx / Apache / OSS / COS 均可）"
Write-Host "  3. 在 MCT 的「④ 设置 → 下载源 → 自定义站点」填入：$BaseUrl"
Write-Host "  4. 确认浏览器能打开 $BaseUrl/mirror.json 与 $BaseUrl/mc/game/version_manifest_v2.json"
Write-Host ""
Write-Host "常用上传方式："
Write-Host "  rclone copy `"$OutDir`" remote:bucket/mirror -P"
Write-Host "  scp -r `"$OutDir/*`" user@host:/var/www/mirror/"
