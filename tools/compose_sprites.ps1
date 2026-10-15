# ============================================================
# GameDev 素材组装脚本（tools/compose_sprites.ps1）
# ============================================================
# 作用：把 assets/third_party/dungeontileset-ii/ 的原始帧（16px 级像素画）
#       按「整数倍 + 最近邻」放大组装为游戏成品素材（输出到 assets/）：
#         player_sheet.png         骑士（knight_m）run f0~f3 + idle f0，
#                                  共 5 帧、每帧 48x84（3 倍放大）
#         player.png               骑士 idle f0 单帧（精灵表加载失败的回退素材）
#         monster_slime_sheet.png  史莱姆（包内 swampy）4 帧，每帧 48x48
#         monster_goblin_sheet.png 哥布林（goblin run）4 帧，每帧 48x48
#         monster_skelet_sheet.png 骷髅（skelet run）4 帧，每帧 48x48
#         monster_*.png            三个变体的单帧回退（各自动画 f0）
#         gem_sheet.png            经验宝石（coin_anim）4 帧，每帧 18x21
#         gem.png                  宝石单帧回退（coin_anim f0）
#         weapon.png               骑士剑（weapon_knight_sword，3 倍 -> 30x87）
#         background.png           1280x720 地牢地板（floor 类 tile 3 倍 -> 一格 48px，
#                                  固定种子掺少量变体砖，避免大面积死板重复）
#
# 设计原则：
#   1. 幂等：可以重复运行，每次覆盖旧文件，输出结果一致（背景变体砖用固定
#      种子产生相同布局）；可随时重新生成，避免「素材只有一份手工产物」的断链
#   2. 整数倍 + 最近邻放大：像素画放大必须整数倍且不插值——线性插值会把
#      像素边缘糊成渐变，非整数倍会产生宽窄不均的像素块；最近邻保持锐利
#   3. 输出尺寸与代码常量严格一致（Player 48x84 / Monster 48x48 /
#      XpGem 18x21 / 帧数与素材一一对应）；改这里的尺寸必须先改对应代码
#   4. 不依赖第三方工具：只用 Windows PowerShell 5.1 自带的 System.Drawing
#
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/compose_sprites.ps1
# ============================================================

# 遇到任何错误立即终止，避免"看起来成功、实际没生成"的假象
$ErrorActionPreference = "Stop"

# 加载 .NET 自带的绘图库（Windows 系统组件，无需额外安装）
Add-Type -AssemblyName System.Drawing

# $PSScriptRoot 是脚本所在目录（tools/）；仓库根目录是它的上一级
$repoRoot = Split-Path -Parent $PSScriptRoot

# 成品素材输出目录：<仓库根>/assets
$assetsDir = Join-Path $repoRoot "assets"

# 原始帧来源目录：第三方素材包的 frames/ 子目录（每个帧一张独立 PNG）
$framesDir = Join-Path $assetsDir "third_party\dungeontileset-ii\frames"

# 来源目录缺失时直接报错退出：提示使用者先下载素材包（幂等性不受影响，
# 缺失即无法组装，早失败早暴露，比生成一半坏图强）
if (-not (Test-Path $framesDir)) {
    throw "原始帧目录不存在: $framesDir（请先获取 DungeonTilesetII 素材包）"
}

# 统一放大倍数：3 倍。全局唯一常量——所有精灵与地砖都用它放大，
# 保证世界内「像素颗粒」大小一致（玩家 16x28 -> 48x84、怪物 16x16 -> 48x48、
# 地砖 16x16 -> 48x48）。改这个值必须同步改游戏代码里的实体尺寸常量
$scale = 3

# ------------------------------------------------------------
# 工具函数：从原始帧目录加载一张帧图（找不到文件立即抛错）
# 参数：name 为不含扩展名的帧文件名（如 "knight_m_run_anim_f0"）
# ------------------------------------------------------------
function Get-Frame([string]$name) {
    # 拼出该帧的完整路径（原始帧统一为 PNG）
    $path = Join-Path $framesDir "$name.png"
    # 帧文件缺失：直接抛错（拼图缺一块不如整体失败，避免产出残缺素材）
    if (-not (Test-Path $path)) {
        throw "原始帧缺失: $path"
    }
    # FromFile 读取像素数据；调用方用完负责 Dispose
    return [System.Drawing.Image]::FromFile($path)
}

# ------------------------------------------------------------
# 工具函数：创建一张带透明通道的空白画布
# 参数：width / height 为像素尺寸；返回 32 位 ARGB 位图对象
# ------------------------------------------------------------
function New-Canvas {
    param([int]$width, [int]$height)
    # Format32bppArgb = 每像素 32 位且带 Alpha 透明通道，PNG 才能存出透明背景
    return New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
}

# ------------------------------------------------------------
# 工具函数：把整张图以「整数倍 + 最近邻」画到目标位图指定位置
# 参数：dstG 目标画布 Graphics；src 源图；dx/dy 目标位置；scaleFactor 放大倍数
# 关键：NearestNeighbor 不插值、PixelOffsetMode.Half 对齐像素中心，
#       两者组合才能得到「每个源像素都放大成规整方块」的像素画效果
# ------------------------------------------------------------
function Draw-ScaledAt {
    param($dstG, [System.Drawing.Image]$src, [int]$dx, [int]$dy, [int]$scaleFactor)
    # 目标宽高 = 源宽高 x 倍数（整数倍，不产生小数坐标）
    $w = $src.Width * $scaleFactor
    $h = $src.Height * $scaleFactor
    # 目标矩形：先算成变量再传入——PS 5.1 的 New-Object 参数里不允许内联 +/- 运算
    $dstRect = New-Object System.Drawing.Rectangle($dx, $dy, $w, $h)
    # 最近邻插值：放大时直接复制像素，不做颜色混合（像素画的唯一正确姿势）
    $dstG.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    # 像素偏移取 Half：让采样点落在像素中心，避免整数缩放时的半像素错位
    $dstG.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    # 带源矩形与像素单位的重载：0,0,srcW,srcH 表示源图整张，粒度明确
    $dstG.DrawImage($src, $dstRect, 0, 0, $src.Width, $src.Height,
                    [System.Drawing.GraphicsUnit]::Pixel)
}

# ------------------------------------------------------------
# 工具函数：把位图保存为 PNG 文件（幂等：旧文件先删除再保存）
# ------------------------------------------------------------
function Save-Png {
    param([System.Drawing.Bitmap]$bitmap, [string]$path)
    # 若旧文件已存在，先删除，保证重复运行时一定写入全新内容（幂等）
    if (Test-Path $path) {
        # -Force 静默删除，不弹确认
        Remove-Item -Path $path -Force
    }
    # Save 的第二个参数指定编码器格式；PNG 会保留透明通道
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    # 打印生成结果，方便使用者核对路径
    Write-Host "已生成: $path"
}

# ------------------------------------------------------------
# 工具函数：自检 PNG 尺寸
# 参数：path 文件路径；expectedWidth/expectedHeight 预期宽高
# ------------------------------------------------------------
function Assert-PngSize {
    param([string]$path, [int]$expectedWidth, [int]$expectedHeight)
    # 从磁盘重新加载图片，拿到真实落盘的像素数据
    $img = [System.Drawing.Image]::FromFile($path)
    # 读取实际宽度（像素）
    $actualWidth = $img.Width
    # 读取实际高度（像素）
    $actualHeight = $img.Height
    # 立即释放图片对象，关闭文件句柄（否则文件会被锁定）
    $img.Dispose()
    # 尺寸不符直接抛异常，让问题第一时间暴露
    if ($actualWidth -ne $expectedWidth -or $actualHeight -ne $expectedHeight) {
        throw "尺寸不符: $path 实际 ${actualWidth}x${actualHeight}，预期 ${expectedWidth}x${expectedHeight}"
    }
    # 尺寸正确则打印校验通过信息
    Write-Host "尺寸校验通过: $path ($actualWidth x $actualHeight)"
}

# ------------------------------------------------------------
# 工具函数：把一串帧横向拼成精灵表（每帧放大 $scale 倍）
# 参数：names 帧文件名数组（顺序即帧序）；width/height 单帧原始尺寸；outPath 输出路径
# 返回：无（直接写盘）
# ------------------------------------------------------------
function Compose-Sheet {
    param([string[]]$names, [int]$frameWidth, [int]$frameHeight, [string]$outPath)
    # 单帧放大后的宽高（整数倍：源尺寸 x 全局倍数）
    $fw = $frameWidth * $scale
    $fh = $frameHeight * $scale
    # 画布宽 = 帧数 x 单帧宽；高 = 单帧高（帧横向一行排列，与引擎裁帧约定一致）
    $sheet = New-Canvas -width ($names.Count * $fw) -height $fh
    # 获取画布的 Graphics 绘图对象（所有绘制都通过它进行）
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    try {
        # 逐帧绘制：第 i 帧的横向起点 = i x 单帧宽
        for ($i = 0; $i -lt $names.Count; $i++) {
            # 加载本帧原始图
            $img = Get-Frame $names[$i]
            try {
                # 本帧在表中的横向起点（帧内所有像素都落在这段区间里）
                $dx = $i * $fw
                # 放大绘制到画布上（最近邻 + 像素对齐，细节见 Draw-ScaledAt）
                Draw-ScaledAt -dstG $g -src $img -dx $dx -dy 0 -scaleFactor $scale
            } finally {
                # 无论绘制成败都释放本帧的源图（关闭文件句柄）
                $img.Dispose()
            }
        }
    } finally {
        # 释放 Graphics（内部持有临时资源）
        $g.Dispose()
    }
    # 保存精灵表（幂等覆盖）
    Save-Png -bitmap $sheet -path $outPath
    # 释放画布位图本身（像素数据在内存里的载体）
    $sheet.Dispose()
}

# ------------------------------------------------------------
# 工具函数：把单帧组装为「单帧回退素材」（放大 $scale 倍后另存）
# 参数：name 帧文件名；outPath 输出路径
# ------------------------------------------------------------
function Compose-Single {
    param([string]$name, [string]$outPath)
    # 加载唯一一帧原始图
    $img = Get-Frame $name
    try {
        # 画布尺寸 = 单帧放大后的宽高
        $w = $img.Width * $scale
        $h = $img.Height * $scale
        # 创建透明画布
        $single = New-Canvas -width $w -height $h
        # 获取画布 Graphics 并绘制放大帧
        $g = [System.Drawing.Graphics]::FromImage($single)
        try {
            Draw-ScaledAt -dstG $g -src $img -dx 0 -dy 0 -scaleFactor $scale
        } finally {
            # 释放 Graphics
            $g.Dispose()
        }
        # 保存（幂等覆盖）
        Save-Png -bitmap $single -path $outPath
        # 释放画布位图
        $single.Dispose()
    } finally {
        # 释放源图
        $img.Dispose()
    }
}

# ============================================================
# 1) 玩家（knight_m）：run f0~f3 + idle f0 共 5 帧；idle 放最后
# ============================================================
# 帧序约定与代码一致：0~3 = 跑动帧（移动时循环），4 = 站立帧（静止时定格）。
# 精灵表帧序一旦改变，Player 代码里的帧号常量必须同步（见 Player.h 注释）
Compose-Sheet -names @(
    "knight_m_run_anim_f0", "knight_m_run_anim_f1",
    "knight_m_run_anim_f2", "knight_m_run_anim_f3",
    "knight_m_idle_anim_f0"
) -frameWidth 16 -frameHeight 28 -outPath (Join-Path $assetsDir "player_sheet.png")

# 单帧回退：骑士站立像（精灵表缺失时动画退化为静止，但形象不变）
Compose-Single -name "knight_m_idle_anim_f0" -outPath (Join-Path $assetsDir "player.png")

# ============================================================
# 2) 怪物三变体：哥布林 / 史莱姆 / 骷髅（各 4 帧 + 单帧回退）
# ============================================================
# 说明：包内没有名为 slime 的帧——史莱姆外观采用同族的绿色软泥怪
# "swampy"（社区中的经典史莱姆形象）；哥布林/骷髅优先用 run 跑动帧
#（有 run 用 run、无 run 的 blob 用其唯一动画）。名称映射与许可细节
# 记录在 assets/third_party/README.md

# ---- 2.1 史莱姆（swampy：包内唯一的团状软泥怪动画，4 帧）----
Compose-Sheet -names @(
    "swampy_anim_f0", "swampy_anim_f1", "swampy_anim_f2", "swampy_anim_f3"
) -frameWidth 16 -frameHeight 16 -outPath (Join-Path $assetsDir "monster_slime_sheet.png")
# 单帧回退：动画首帧
Compose-Single -name "swampy_anim_f0" -outPath (Join-Path $assetsDir "monster_slime.png")

# ---- 2.2 哥布林（goblin run：跑动 4 帧）----
Compose-Sheet -names @(
    "goblin_run_anim_f0", "goblin_run_anim_f1", "goblin_run_anim_f2", "goblin_run_anim_f3"
) -frameWidth 16 -frameHeight 16 -outPath (Join-Path $assetsDir "monster_goblin_sheet.png")
# 单帧回退：跑动首帧
Compose-Single -name "goblin_run_anim_f0" -outPath (Join-Path $assetsDir "monster_goblin.png")

# ---- 2.3 骷髅（skelet run：跑动 4 帧）----
Compose-Sheet -names @(
    "skelet_run_anim_f0", "skelet_run_anim_f1", "skelet_run_anim_f2", "skelet_run_anim_f3"
) -frameWidth 16 -frameHeight 16 -outPath (Join-Path $assetsDir "monster_skelet_sheet.png")
# 单帧回退：跑动首帧
Compose-Single -name "skelet_run_anim_f0" -outPath (Join-Path $assetsDir "monster_skelet.png")

# ============================================================
# 3) 经验宝石（coin_anim，4 帧旋转金币 + 单帧回退）
# ============================================================
# 源帧只有 6x7 像素：3 倍放大后为 18x21——小尺寸拾取物，与代码里
# XpGem 的碰撞盒尺寸严格一致（18x21）
Compose-Sheet -names @(
    "coin_anim_f0", "coin_anim_f1", "coin_anim_f2", "coin_anim_f3"
) -frameWidth 6 -frameHeight 7 -outPath (Join-Path $assetsDir "gem_sheet.png")
# 单帧回退：旋转首帧
Compose-Single -name "coin_anim_f0" -outPath (Join-Path $assetsDir "gem.png")

# ============================================================
# 4) 武器（weapon_knight_sword）：自动攻击的挥剑表现素材
# ============================================================
# 原始 10x29 像素、剑尖朝上：游戏侧按「目标方向 + 90 度」旋转绘制
#（旋转 90 度后剑尖指向右侧，即朝向目标的基准姿态）
Compose-Single -name "weapon_knight_sword" -outPath (Join-Path $assetsDir "weapon.png")

# ============================================================
# 5) 背景：1280x720 地牢地板（floor 类 tile 拼贴，一格 48px）
# ============================================================
# 拼贴规则：
#   - 每格 = 原始 16x16 floor tile 放大 3 倍 = 48x48；
#   - 基础砖 floor_1 占多数（保持大面积统一），随机掺入 floor_2~floor_8
#     变体（约 28%，含裂纹/碎石等细节，避免整屏死板重复）；
#   - 随机数用固定种子：每次运行布局完全一致（幂等性要求）
#   - 横向 27 列 = 1296px（超出 1280 的 16px 被画布自然裁掉）；
#     纵向 15 行 = 720px 恰好铺满
$bgWidth = 1280  # 背景宽（像素），与游戏初始窗口宽一致
$bgHeight = 720  # 背景高（像素），与游戏初始窗口高一致
$tileSrcSize = 16                # 原始地砖边长（像素）
$tileSize = $tileSrcSize * $scale # 放大后每格边长 = 48px
$bgCols = [int][Math]::Ceiling($bgWidth / [double]$tileSize)  # 横向格数：27 列
$bgRows = [int]($bgHeight / $tileSize)                        # 纵向格数：15 行

# 创建背景画布（不透明地表底图，透明通道留着不用）
$bgBmp = New-Canvas -width $bgWidth -height $bgHeight

# 获取背景的 Graphics 绘图对象
$bgG = [System.Drawing.Graphics]::FromImage($bgBmp)

# 固定种子的伪随机数：种子写死，每次运行得到相同的变体砖布局（幂等）
$bgRng = New-Object System.Random(20261015)

# 预加载全部 9 张地砖（floor_1 基础砖 + floor_2~floor_8 变体砖），
# 避免在双层循环里反复读盘；下标 0 不使用，地板编号即数组下标
$floorTiles = @()
for ($i = 0; $i -le 8; $i++) {
    # 数组占位：实际是 1..8 的帧图（floor_1 起）
    $floorTiles += $null
}
for ($i = 1; $i -le 8; $i++) {
    # 逐张加载到对应下标（用完统一释放）
    $floorTiles[$i] = Get-Frame "floor_$i"
}

try {
    # 逐行逐列铺砖：外层行、内层列（行序不影响结果，布局由随机序列决定）
    for ($row = 0; $row -lt $bgRows; $row++) {
        for ($col = 0; $col -lt $bgCols; $col++) {
            # 抽签：72% 概率用基础砖 floor_1，其余 28% 从 2~8 变体里随机挑一块
            if ($bgRng.Next(100) -lt 72) {
                # 基础砖：大面积统一的地表底色
                $tile = $floorTiles[1]
            } else {
                # 变体砖：裂纹/碎石等细节，打破重复感（Next(2,9) 取 [2,9) 含 2~8）
                $tile = $floorTiles[$bgRng.Next(2, 9)]
            }
            # 本格的落点（左上角像素坐标）
            $dx = $col * $tileSize
            $dy = $row * $tileSize
            # 放大绘制本格（最近邻 + 像素对齐）
            Draw-ScaledAt -dstG $bgG -src $tile -dx $dx -dy $dy -scaleFactor $scale
        }
    }
} finally {
    # 释放全部地砖源图（无论铺贴成败，句柄必须归还）
    for ($i = 1; $i -le 8; $i++) {
        if ($null -ne $floorTiles[$i]) {
            $floorTiles[$i].Dispose()
        }
    }
    # 释放背景 Graphics
    $bgG.Dispose()
}

# 保存背景图（幂等覆盖）
Save-Png -bitmap $bgBmp -path (Join-Path $assetsDir "background.png")
# 释放背景画布位图
$bgBmp.Dispose()

# ============================================================
# 6) 生成后自检：重新读盘校验全部成品 PNG 的尺寸
# ============================================================
# 与代码常量的对应关系（任一处改动必须两边同步）：
#   player_sheet: 5 帧 x 48 宽 = 240，高 84（Player 的 m_w=48 / m_h=84）
Assert-PngSize -path (Join-Path $assetsDir "player_sheet.png") -expectedWidth 240 -expectedHeight 84
#   player: 单帧 48x84（回退素材，尺寸与精灵表单帧一致）
Assert-PngSize -path (Join-Path $assetsDir "player.png") -expectedWidth 48 -expectedHeight 84

#   三怪物精灵表：4 帧 x 48 宽 = 192，高 48（Monster 单帧 48x48）
Assert-PngSize -path (Join-Path $assetsDir "monster_slime_sheet.png") -expectedWidth 192 -expectedHeight 48
Assert-PngSize -path (Join-Path $assetsDir "monster_goblin_sheet.png") -expectedWidth 192 -expectedHeight 48
Assert-PngSize -path (Join-Path $assetsDir "monster_skelet_sheet.png") -expectedWidth 192 -expectedHeight 48

#   三怪物单帧回退：48x48
Assert-PngSize -path (Join-Path $assetsDir "monster_slime.png") -expectedWidth 48 -expectedHeight 48
Assert-PngSize -path (Join-Path $assetsDir "monster_goblin.png") -expectedWidth 48 -expectedHeight 48
Assert-PngSize -path (Join-Path $assetsDir "monster_skelet.png") -expectedWidth 48 -expectedHeight 48

#   宝石精灵表：4 帧 x 18 宽 = 72，高 21（XpGem 的 m_w=18 / m_h=21）
Assert-PngSize -path (Join-Path $assetsDir "gem_sheet.png") -expectedWidth 72 -expectedHeight 21
#   宝石单帧回退：18x21
Assert-PngSize -path (Join-Path $assetsDir "gem.png") -expectedWidth 18 -expectedHeight 21

#   武器：30x87（原始 10x29 的 3 倍）
Assert-PngSize -path (Join-Path $assetsDir "weapon.png") -expectedWidth 30 -expectedHeight 87

#   背景：1280x720（与初始窗口一致；绘制时按窗口实际尺寸拉伸）
Assert-PngSize -path (Join-Path $assetsDir "background.png") -expectedWidth 1280 -expectedHeight 720

# 全部完成提示
Write-Host "全部成品素材组装并校验完成。"
