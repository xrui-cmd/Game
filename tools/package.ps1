# ============================================================
# ROGUE ARENA 发布打包脚本（tools/package.ps1）
# ============================================================
# 作用：按 x64-release 预设构建，并把运行所需文件收集到临时 staging，
#       压缩为 dist/RogueArena-v0.1.0-win64.zip。
# 幂等：重复运行会重建 staging 并覆盖旧 zip，产物一致。
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1
# ============================================================

# 任何错误立即终止：构建/收集/压缩失败都不留下「看似成功」的假象
$ErrorActionPreference = "Stop"

# 压缩包版本号：与 release tag / 玩家说明保持一致（改版本时三处同步）
$version = "v0.1.0"

# 仓库根目录：脚本位于 tools/，上一级即根
$repoRoot = Split-Path -Parent $PSScriptRoot

# release 构建目录（CMakePresets 的 binaryDir 规则：out/build/<preset>）
$buildDir = Join-Path $repoRoot "out\build\x64-release"

# 发布产物目录（已在 .gitignore 中忽略，不入库）
$distDir = Join-Path $repoRoot "dist"

# 最终 zip 路径：包名固定 RogueArena-<版本>-win64.zip
$zipPath = Join-Path $distDir "RogueArena-$version-win64.zip"

# 临时 staging 目录：先完整收集再压缩，避免 zip 内出现半成品
$stagingDir = Join-Path $distDir ".staging-$version"

# 玩家说明源文件（打包时放到 zip 根目录）
$notesPath = Join-Path $repoRoot "docs\玩家说明.txt"

# 源素材目录：打包前用它刷新构建输出，确保包内素材与仓库一致
$sourceAssets = Join-Path $repoRoot "assets"

# 源配置目录：同样用于刷新构建输出（game.ini 以仓库当前版本为准）
$sourceConfig = Join-Path $repoRoot "config"

# 运行时必需的 4 个 DLL（SDL3 系列；顺序仅用于日志显示）
$runtimeDlls = @("SDL3.dll", "SDL3_image.dll", "SDL3_ttf.dll", "SDL3_mixer.dll")

# 定位 Visual Studio 安装目录：优先用 vswhere（VS 官方查询工具）
$vswherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

# vswhere 不存在：无法准备 MSVC 环境，直接报错
if (-not (Test-Path -LiteralPath $vswherePath))
{
    # 抛出明确指引，避免在缺少工具链时继续构建
    throw "未找到 vswhere.exe：请安装 Visual Studio（含 C++ 工具链）"
}

# 查询最新 VS 安装路径（-latest 取最高版本）
$vsPath = & $vswherePath -latest -property installationPath

# 查询失败（空值）：终止并提示
if ([string]::IsNullOrWhiteSpace($vsPath))
{
    # 没有安装路径就无法 import 编译环境
    throw "vswhere 未返回 Visual Studio 安装路径"
}

# vcvars64.bat：把 cl.exe / ninja 等工具加入当前进程环境
$vcvarsPath = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"

# 脚本缺失：明确报错（没有 MSVC 环境无法构建）
if (-not (Test-Path -LiteralPath $vcvarsPath))
{
    # 输出缺失路径，便于核对 VS 组件安装
    throw "未找到 vcvars64.bat：$vcvarsPath"
}

# 执行 vcvars64 并导出全部环境变量（cmd 的 set 输出 KEY=VALUE 行）
$vsEnvLines = & cmd.exe /c "`"$vcvarsPath`" >nul 2>&1 && set"

# 逐行解析并写回当前 PowerShell 进程（子进程环境不会自动继承回来）
foreach ($line in $vsEnvLines)
{
    # 仅处理标准 KEY=VALUE 行（跳过空行/异常行）
    if ($line -match "^([^=]+)=(.*)$")
    {
        # 覆盖同名环境变量：确保 cl/ninja 使用 VS 自带版本
        Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
    }
}

# 优先使用 PATH 中的 cmake；找不到时回退 VS 自带 cmake
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue

# 有系统 cmake：取其可执行文件路径
if ($cmakeCommand)
{
    # Source 即 cmake.exe 的完整路径
    $cmakePath = $cmakeCommand.Source
}
else
{
    # VS 自带 CMake 的固定位置（与 VS 安装器布局一致）
    $cmakePath = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}

# 两种方式都找不到：终止并提示
if (-not (Test-Path -LiteralPath $cmakePath))
{
    # 提示安装 CMake 或勾选 VS 的 CMake 组件
    throw "未找到 cmake.exe：请安装 CMake 或使用 Visual Studio 的 CMake 组件"
}

# 输出本次使用的 cmake 路径，便于排查环境问题
Write-Host "[package] cmake: $cmakePath"

# 配置日志文件：stdout+stderr 合并重定向（避免 PS 5.1 把 CMake 的 stderr 当异常）
$configureLogPath = Join-Path ([System.IO.Path]::GetTempPath()) "RogueArena-configure-$version.log"

# 进入仓库根执行预设配置（preset 的路径均相对源码根）
Push-Location -LiteralPath $repoRoot
try
{
    # 按 x64-release 预设配置：每次都跑一遍（幂等），同时刷新 GLOB 文件清单
    & cmd.exe /c "`"$cmakePath`" --preset x64-release > `"$configureLogPath`" 2>&1"
    # 保存配置退出码（随后统一判断）
    $configureExit = $LASTEXITCODE
    # 读取配置日志并逐行输出（保留配置过程可读性）
    $configureLog = @(Get-Content -LiteralPath $configureLogPath -ErrorAction SilentlyContinue)
    # 逐行打印配置输出
    foreach ($line in $configureLog)
    {
        # 输出当前配置行
        Write-Host $line
    }
    # 清理配置日志（不留下临时文件）
    Remove-Item -LiteralPath $configureLogPath -Force -ErrorAction SilentlyContinue
    # 配置失败：抛出退出码，避免后续构建误跑
    if ($configureExit -ne 0)
    {
        # 带上退出码，便于区分是环境还是工程问题
        throw "cmake --preset x64-release 失败（退出码 $configureExit）"
    }
}
finally
{
    # 无论成败都回到原工作目录
    Pop-Location
}

# 构建日志文件：stdout+stderr 全部重定向到文件（避免 PS 5.1 把 stderr 当异常）
$buildLogPath = Join-Path ([System.IO.Path]::GetTempPath()) "RogueArena-build-$version.log"

# 进入仓库根构建（Ninja 构建目录内自行解析相对路径）
Push-Location -LiteralPath $repoRoot
try
{
    # 用 cmd 重定向执行构建：> 覆盖写日志、2>&1 合并错误流
    & cmd.exe /c "`"$cmakePath`" --build `"$buildDir`" > `"$buildLogPath`" 2>&1"
    # 保存构建退出码（后续判断是否成功）
    $buildExit = $LASTEXITCODE
}
finally
{
    # 返回原工作目录
    Pop-Location
}

# 读取构建日志（构建输出可能含中文/UTF-8，按系统默认编码读取即可）
$buildLog = @(Get-Content -LiteralPath $buildLogPath -ErrorAction SilentlyContinue)

# 逐行打印构建日志：保留构建过程可读性（失败时也能看到完整原因）
foreach ($line in $buildLog)
{
    # 输出当前构建行
    Write-Host $line
}

# 清理临时日志：不把中间文件留在系统临时目录
Remove-Item -LiteralPath $buildLogPath -Force -ErrorAction SilentlyContinue

# 构建失败：终止并附退出码
if ($buildExit -ne 0)
{
    # 明确失败信息（不继续收集半成品）
    throw "x64-release 构建失败（退出码 $buildExit）"
}

# 扫描编译器/链接器警告：release 发布要求 0 警告
$warningLines = @($buildLog | Where-Object { $_ -match "warning (C|LNK)[0-9]+" })

# 有警告：打印并终止（发布线不允许带警告产物）
if ($warningLines.Count -gt 0)
{
    # 逐条输出警告行，便于定位
    $warningLines | ForEach-Object { Write-Host "  $_" }
    # 抛出明确错误（包含条数）
    throw "构建出现 $($warningLines.Count) 条警告：发布要求 0 警告"
}

# 构建完成提示（0 警告是本脚本的硬性门槛）
Write-Host "[package] 构建完成：0 警告"

# 构建输出中的素材目录：随后用仓库源目录刷新（去掉历史残留）
$buildAssets = Join-Path $buildDir "assets"

# 构建输出中的配置目录：随后用仓库源目录刷新
$buildConfig = Join-Path $buildDir "config"

# 源素材目录缺失：无法刷新，直接报错
if (-not (Test-Path -LiteralPath $sourceAssets))
{
    # 指出缺失路径（素材是运行必需项）
    throw "素材目录缺失：$sourceAssets"
}

# 刷新素材：CMake POST_BUILD 只增改不删除，旧版本文件会残留在构建目录
Remove-Item -LiteralPath $buildAssets -Recurse -Force -ErrorAction SilentlyContinue

# 从仓库源目录整目录复制，保证包内素材与当前仓库完全一致
Copy-Item -LiteralPath $sourceAssets -Destination $buildDir -Recurse

# 刷新配置：先删旧副本（避免已删除的配置键残留在包内）
Remove-Item -LiteralPath $buildConfig -Recurse -Force -ErrorAction SilentlyContinue

# 复制仓库当前配置（game.ini 修改后重启生效——包内需为最新版本）
Copy-Item -LiteralPath $sourceConfig -Destination $buildDir -Recurse

# 必需文件清单：主程序 + 4 个运行库（均在构建输出根目录）
$requiredFiles = @("GameDev.exe") + $runtimeDlls

# 逐个校验存在性：缺失即终止，避免打出发不起来的包
foreach ($name in $requiredFiles)
{
    # 拼接构建产物绝对路径
    $path = Join-Path $buildDir $name
    # 缺失时抛出文件路径，便于排查
    if (-not (Test-Path -LiteralPath $path))
    {
        # 明确缺失文件名（不进入压缩环节）
        throw "构建产物缺失：$path"
    }
}

# 素材目录缺失：上方刷新步骤未成功（复制失败或源目录为空）
if (-not (Test-Path -LiteralPath $buildAssets))
{
    # 输出期望路径，便于排查刷新步骤
    throw "构建输出缺少 assets 目录：$buildAssets"
}

# 配置目录缺失：上方刷新步骤未成功
if (-not (Test-Path -LiteralPath $buildConfig))
{
    # 输出期望路径，便于排查刷新步骤
    throw "构建输出缺少 config 目录：$buildConfig"
}

# 玩家说明缺失：打包前必须先编写（docs/玩家说明.txt）
if (-not (Test-Path -LiteralPath $notesPath))
{
    # 提示源文件位置（说明文档属于发布必带项）
    throw "玩家说明缺失：$notesPath"
}

# 确保 dist 目录存在（首次运行）
if (-not (Test-Path -LiteralPath $distDir))
{
    # 创建发布目录（父目录必然存在）
    New-Item -ItemType Directory -Path $distDir | Out-Null
}

# 清理旧 staging：保证每次从空目录开始（幂等）
if (Test-Path -LiteralPath $stagingDir)
{
    # 递归删除上次残留
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}

# 创建本次 staging
New-Item -ItemType Directory -Path $stagingDir | Out-Null

# staging 收集与压缩：放 try/finally 保证临时目录必被清理
try
{
    # 复制主程序到 zip 根目录
    Copy-Item -LiteralPath (Join-Path $buildDir "GameDev.exe") -Destination $stagingDir
    # 复制 4 个运行库到 zip 根目录（与 exe 同级才能被加载）
    foreach ($dll in $runtimeDlls)
    {
        # 逐个复制（DLL 与 exe 必须同目录）
        Copy-Item -LiteralPath (Join-Path $buildDir $dll) -Destination $stagingDir
    }
    # 复制素材目录（含 fonts/sounds 等全部运行时资源）
    Copy-Item -LiteralPath $buildAssets -Destination $stagingDir -Recurse
    # 复制配置目录（玩家可修改 game.ini）
    Copy-Item -LiteralPath $buildConfig -Destination $stagingDir -Recurse
    # 复制玩家说明到 zip 根目录（解压即可见）
    Copy-Item -LiteralPath $notesPath -Destination (Join-Path $stagingDir "玩家说明.txt")

    # 删除旧 zip：Compress-Archive -Force 在 PS 5.1 下偶有占用问题，先删更稳
    if (Test-Path -LiteralPath $zipPath)
    {
        # 覆盖式重建：删除旧包
        Remove-Item -LiteralPath $zipPath -Force
    }

    # 压缩 staging 内容到 zip 根（-Path 带 \* 避免多一层目录）
    Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $zipPath -CompressionLevel Optimal

    # 压缩失败：明确报错（防止旧包被误当成本次产物）
    if (-not (Test-Path -LiteralPath $zipPath))
    {
        # 指出期望的 zip 路径
        throw "压缩失败：未生成 $zipPath"
    }
}
finally
{
    # 无论成败都清理 staging（不把中间目录留在 dist）
    if (Test-Path -LiteralPath $stagingDir)
    {
        # 递归清理临时目录
        Remove-Item -LiteralPath $stagingDir -Recurse -Force
    }
}

# 读取 zip 文件信息（大小与条目清单）
Add-Type -AssemblyName System.IO.Compression.FileSystem

# 以只读方式打开 zip（不锁定文件，稍后可继续替换）
$zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)

# 统计并输出包信息
try
{
    # 统计 zip 内条目数
    $entryCount = $zip.Entries.Count
    # 总字节数用于报告包体规模
    $totalBytes = ($zip.Entries | Measure-Object -Property Length -Sum).Sum
    # 输出包路径与概要
    Write-Host "[package] 已生成：$zipPath"
    # 输出条目数与解压后大小（MB，保留两位）
    Write-Host ("[package] 条目 {0} 个，解压后 {1:N2} MB" -f $entryCount, ($totalBytes / 1MB))
    # 列出 zip 根目录文件（名称不含 / 或 \ 的才是根级条目）
    $zip.Entries | Where-Object { $_.FullName -notmatch "[\\/]" } | ForEach-Object {
        # 输出根级条目名称
        Write-Host "  - $($_.FullName)"
    }
}
finally
{
    # 关闭 zip 句柄（释放文件锁）
    $zip.Dispose()
}

# 输出压缩包自身大小（MB，保留两位）
Write-Host ("[package] zip 体积：{0:N2} MB" -f ((Get-Item -LiteralPath $zipPath).Length / 1MB))
