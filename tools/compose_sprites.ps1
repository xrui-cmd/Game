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
#         coin_sheet.png           金币（coin_anim）4 帧，每帧 18x21——素材包原色
#         coin.png                 金币单帧回退（coin_anim f0）
#         gem_sheet.png            经验宝石（coin_anim）4 帧，每帧 18x21——
#                                  R<->B 换道「染色」为青蓝调（与金币区分）
#         gem.png                  宝石单帧回退（coin_anim f0，同样染色）
#         weapon.png               骑士剑（weapon_knight_sword，3 倍 -> 30x87）
#         background.png           1280x720 地牢地板（floor 类 tile 3 倍 -> 一格 48px，
#                                  固定种子掺少量变体砖，避免大面积死板重复）
#         tiles_floor.png          世界地砖变体表：floor_1~floor_8 共 8 帧，每帧 48x48
#                                  （批二世界系统逐格挑变体铺地板）
#         tiles_wall.png           世界墙块单帧：wall_mid 3 倍 -> 48x48
#         decor_sheet.png          世界装饰表：skull（48x48）+ crate（48x72）
#         tiles_water.png          海洋水砖变体表：3 帧，每帧 48x48（批六甲·
#                                  海洋边界——深蓝基色 + 波纹层次，程序化绘制）
#         tiles_floor_snow.png     雪山地板变体表：floor_1~floor_8 冰霜重染色
#                                  8 帧（批六甲·雪山区——通道与亮度调整）
#         tiles_wall_snow.png      雪山墙块：wall_mid 冰霜重染色 48x48
#         decor_sheet_snow.png     雪山装饰表：skull + crate 冰霜重染色
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
# 工具函数：R<->B 换道（红蓝通道互换）——批三「宝石染色」的实现
# 参数：src 源图；返回同尺寸的新位图（调用方负责 Dispose）
# 说明：金币原色是「橙边/金身/白高光」（R>G>B），换道后变成
#       「蓝边/青身/白高光」（B>G>R）——青蓝调、与金币一眼可分。
#       为什么在组装期染色而不是渲染期颜色调制（SDL_SetTextureColorMod）：
#       乘法调制只能压暗通道（金身 B=0x3E 封顶），无法把金色推成青蓝；
#       通道互换是像素级重排，组装期一次完成、零运行开销
# ------------------------------------------------------------
function Convert-SwapRedBlue {
    param([System.Drawing.Image]$src)
    # 目标画布：与源图同尺寸、带透明通道
    $dst = New-Canvas -width $src.Width -height $src.Height
    # 逐像素换道（源帧仅 6x7 像素级，全表也不过百余像素，无需优化）
    for ($y = 0; $y -lt $src.Height; $y++) {
        for ($x = 0; $x -lt $src.Width; $x++) {
            # 取源像素（ARGB 各分量）
            $p = $src.GetPixel($x, $y)
            # 写入换道像素：R 与 B 互换、G/A 原样（透明像素保持全透明）
            $dst.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($p.A, $p.B, $p.G, $p.R))
        }
    }
    # 返回新位图（源图不动）
    return $dst
}

# ------------------------------------------------------------
# 工具函数：冰霜重染色（批六甲·雪山区素材）——「通道与亮度调整」实现
# 参数：src 源图；返回同尺寸的新位图（调用方负责 Dispose）
# 思路：冷色化 = 压红、略压绿、蓝通道「原蓝 + 红分量补偿 + 底色抬升」：
#       - R' = min(255, R*0.50 + 16)  压红（暖色退场，亮度微抬）
#       - G' = min(255, G*0.72 + 20)  略压绿（保留形状对比）
#       - B' = min(255, B*0.60 + R*0.50 + 48)  蓝提亮（红分量补偿保住明暗层次）
#       效果：褐色地牢素材整体推向冰蓝调——同一张图完成「雪山」换肤，
#       不新增手绘素材。全透明像素保持全透明（不污染边缘）
# ------------------------------------------------------------
function Convert-FrostTint {
    param([System.Drawing.Image]$src)
    # 目标画布：与源图同尺寸、带透明通道
    $dst = New-Canvas -width $src.Width -height $src.Height
    # 逐像素染色（源帧仅 16px 级，全表也不过数千像素，无需优化）
    for ($y = 0; $y -lt $src.Height; $y++) {
        for ($x = 0; $x -lt $src.Width; $x++) {
            # 取源像素（ARGB 各分量）
            $p = $src.GetPixel($x, $y)
            if ($p.A -eq 0) {
                # 全透明像素：原样透明（RGB 不参与显示，保持干净）
                $dst.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(0, 0, 0, 0))
                continue
            }
            # 通道与亮度调整（公式见函数头注释；min 夹到 255 防溢出）
            $nr = [int][Math]::Min(255, [Math]::Round($p.R * 0.50 + 16))
            $ng = [int][Math]::Min(255, [Math]::Round($p.G * 0.72 + 20))
            $nb = [int][Math]::Min(255, [Math]::Round($p.B * 0.60 + $p.R * 0.50 + 48))
            # 写入染色像素：A 原样
            $dst.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($p.A, $nr, $ng, $nb))
        }
    }
    # 返回新位图（源图不动）
    return $dst
}

# ------------------------------------------------------------
# 工具函数：把一串帧横向拼成精灵表（每帧放大 $scale 倍）
# 参数：names 帧文件名数组（顺序即帧序）；width/height 单帧原始尺寸；outPath 输出路径
#       swapRedBlue 是否对每帧做 R<->B 换道（批三宝石染色用）
#       frostTint 是否对每帧做冰霜重染色（批六甲雪山素材用）
# 返回：无（直接写盘）
# ------------------------------------------------------------
function Compose-Sheet {
    param([string[]]$names, [int]$frameWidth, [int]$frameHeight, [string]$outPath,
          [switch]$swapRedBlue, [switch]$frostTint)
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
            # 需要染色时先出一张副本（原图仍负责释放；副本绘制后释放）
            $useImg = $img
            $tinted = $false # 是否创建过染色副本（决定 finally 是否释放副本）
            if ($swapRedBlue) {
                $useImg = Convert-SwapRedBlue $img
                $tinted = $true
            }
            elseif ($frostTint) {
                $useImg = Convert-FrostTint $img
                $tinted = $true
            }
            try {
                # 本帧在表中的横向起点（帧内所有像素都落在这段区间里）
                $dx = $i * $fw
                # 放大绘制到画布上（最近邻 + 像素对齐，细节见 Draw-ScaledAt）
                Draw-ScaledAt -dstG $g -src $useImg -dx $dx -dy 0 -scaleFactor $scale
            } finally {
                # 染色副本（若创建过）先释放——它不属于 Get-Frame 的调用方
                if ($tinted) {
                    $useImg.Dispose()
                }
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
# 参数：name 帧文件名；outPath 输出路径；swapRedBlue 是否 R<->B 换道；
#       frostTint 是否冰霜重染色（批六甲雪山素材用）
# ------------------------------------------------------------
function Compose-Single {
    param([string]$name, [string]$outPath, [switch]$swapRedBlue, [switch]$frostTint)
    # 加载唯一一帧原始图
    $img = Get-Frame $name
    # 需要染色时先出一张副本
    $useImg = $img
    $tinted = $false # 是否创建过染色副本（决定 finally 是否释放副本）
    if ($swapRedBlue) {
        $useImg = Convert-SwapRedBlue $img
        $tinted = $true
    }
    elseif ($frostTint) {
        $useImg = Convert-FrostTint $img
        $tinted = $true
    }
    try {
        # 画布尺寸 = 单帧放大后的宽高
        $w = $useImg.Width * $scale
        $h = $useImg.Height * $scale
        # 创建透明画布
        $single = New-Canvas -width $w -height $h
        # 获取画布 Graphics 并绘制放大帧
        $g = [System.Drawing.Graphics]::FromImage($single)
        try {
            Draw-ScaledAt -dstG $g -src $useImg -dx 0 -dy 0 -scaleFactor $scale
        } finally {
            # 释放 Graphics
            $g.Dispose()
        }
        # 保存（幂等覆盖）
        Save-Png -bitmap $single -path $outPath
        # 释放画布位图
        $single.Dispose()
    } finally {
        # 释放染色副本（若创建过）
        if ($tinted) {
            $useImg.Dispose()
        }
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
# 3) 金币（素材包原色）与经验宝石（同源帧换道染色：青蓝调）
# ============================================================
# 源帧只有 6x7 像素：3 倍放大后为 18x21——小尺寸拾取物，与代码里
# Coin / XpGem 的碰撞盒尺寸严格一致（18x21）。
# 批三起两者同源不同色（「一眼可分」）：
#   - 金币 = coin_anim 原始配色（橙边/金身/白高光）；
#   - 经验宝石 = coin_anim 经 R<->B 换道（蓝边/青身/白高光）。
# 不引入新素材：区分手段是染色（详见 Convert-SwapRedBlue 注释）

# ---- 3.1 金币（原色，4 帧旋转 + 单帧回退）----
Compose-Sheet -names @(
    "coin_anim_f0", "coin_anim_f1", "coin_anim_f2", "coin_anim_f3"
) -frameWidth 6 -frameHeight 7 -outPath (Join-Path $assetsDir "coin_sheet.png")
# 单帧回退：旋转首帧
Compose-Single -name "coin_anim_f0" -outPath (Join-Path $assetsDir "coin.png")

# ---- 3.2 经验宝石（同源帧 R<->B 换道 -> 青蓝调）----
Compose-Sheet -names @(
    "coin_anim_f0", "coin_anim_f1", "coin_anim_f2", "coin_anim_f3"
) -frameWidth 6 -frameHeight 7 -outPath (Join-Path $assetsDir "gem_sheet.png") -swapRedBlue
# 单帧回退：旋转首帧（同样染色）
Compose-Single -name "coin_anim_f0" -outPath (Join-Path $assetsDir "gem.png") -swapRedBlue

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
# 6) 世界地砖 / 墙体 / 装饰（批二·世界与相机）
# ============================================================
# 世界 = 16x16 格 x 48px 的区块拼成；格级素材规则：
#   - 地板：8 张变体（floor_1~floor_8）横向拼成变体表，世界生成时逐格
#     按坐标哈希挑一张（确定性）——大面积不平铺同一块砖，避免死板重复；
#   - 墙体：wall_mid（石墙块）3 倍放大为 48x48——墙体碰撞格的可视面，
#     世界边界一圈与内部随机墙簇都用它；
#   - 装饰：不占格、无碰撞的纯视觉摆件（骷髅头 / 木箱）；两帧源尺寸不同
#     （16x16 与 16x24），不能用等距 Compose-Sheet，改手排：
#     skull 放 x=0（48x48），crate 放 x=48（48x72，比一格高，绘制时
#     底部对齐格底——游戏侧 World 负责裁剪坐标）

# ---- 6.1 世界地砖变体表：floor_1~floor_8，共 8 帧 x 48 = 384x48 ----
Compose-Sheet -names @(
    "floor_1", "floor_2", "floor_3", "floor_4",
    "floor_5", "floor_6", "floor_7", "floor_8"
) -frameWidth 16 -frameHeight 16 -outPath (Join-Path $assetsDir "tiles_floor.png")

# ---- 6.2 世界墙块：wall_mid 单帧 3 倍 -> 48x48 ----
Compose-Single -name "wall_mid" -outPath (Join-Path $assetsDir "tiles_wall.png")

# ---- 6.3 世界装饰表：skull（48x48）+ crate（48x72）手排（原色与雪山版共用函数）----
# 参数：outPath 输出路径；frostTint 是否冰霜重染色（批六甲雪山装饰用）
function Compose-DecorSheet {
    param([string]$outPath, [switch]$frostTint)
    # 画布宽 = (16 + 16) x 3 = 96；高 = max(16, 24) x 3 = 72（取较高帧）
    $decorBmp = New-Canvas -width 96 -height 72
    # 获取装饰画布的 Graphics 绘图对象（所有绘制都通过它进行）
    $decorG = [System.Drawing.Graphics]::FromImage($decorBmp)
    try {
        # 第一帧：骷髅头（16x16 -> 48x48）落于 x=0（游戏侧裁剪源矩形 0,0,16,16）
        $skullImg = Get-Frame "skull"
        $useSkull = $skullImg # 实际绘制的图（染色时替换为副本）
        if ($frostTint) {
            $useSkull = Convert-FrostTint $skullImg
        }
        try {
            # 放大绘制到画布左半区
            Draw-ScaledAt -dstG $decorG -src $useSkull -dx 0 -dy 0 -scaleFactor $scale
        } finally {
            # 染色副本先释放（若创建过），再释放骷髅源图（关闭文件句柄）
            if ($frostTint) {
                $useSkull.Dispose()
            }
            $skullImg.Dispose()
        }
        # 第二帧：木箱（16x24 -> 48x72）落于 x=48（游戏侧裁剪源矩形 16,0,16,24）
        $crateImg = Get-Frame "crate"
        $useCrate = $crateImg # 实际绘制的图（染色时替换为副本）
        if ($frostTint) {
            $useCrate = Convert-FrostTint $crateImg
        }
        try {
            # 放大绘制到画布右半区（72 高恰好到画布底边）
            Draw-ScaledAt -dstG $decorG -src $useCrate -dx 48 -dy 0 -scaleFactor $scale
        } finally {
            # 染色副本先释放（若创建过），再释放木箱源图
            if ($frostTint) {
                $useCrate.Dispose()
            }
            $crateImg.Dispose()
        }
    } finally {
        # 释放装饰画布 Graphics（内部持有临时资源）
        $decorG.Dispose()
    }
    # 保存装饰表（幂等覆盖）
    Save-Png -bitmap $decorBmp -path $outPath
    # 释放装饰画布位图
    $decorBmp.Dispose()
}

# 原色装饰表（草地区/主菜单沿用）
Compose-DecorSheet -outPath (Join-Path $assetsDir "decor_sheet.png")

# ---- 6.4 雪山素材（批六甲·冰霜重染色版）----
# 说明：雪山区的三张素材 = 现有地牢素材经 Convert-FrostTint 的冷色化版本
#（通道与亮度调整——见函数头注释），同一产线幂等生成；世界烘焙时雪山区
# 的格按「本格所属区」（game/Zone.h）选用这三张表（原区沿用原色表）
Compose-Sheet -names @(
    "floor_1", "floor_2", "floor_3", "floor_4",
    "floor_5", "floor_6", "floor_7", "floor_8"
) -frameWidth 16 -frameHeight 16 -outPath (Join-Path $assetsDir "tiles_floor_snow.png") -frostTint

Compose-Single -name "wall_mid" -outPath (Join-Path $assetsDir "tiles_wall_snow.png") -frostTint

Compose-DecorSheet -outPath (Join-Path $assetsDir "decor_sheet_snow.png") -frostTint

# ============================================================
# 7) 攻击反馈素材（批五A：程序化绘制）
# ============================================================
# 说明（报告口径）：DungeonTileset II 素材包内没有「警示感叹号」与
# 「爪痕爆点」两类帧，无法从原始帧组装——本节的 alert 与 hit_fx
# 由脚本直接程序化绘制（System.Drawing 画矩形/线段/圆），输出仍是
# 与其余素材一致的成品 PNG；脚本整体依旧「幂等」：重复运行覆盖同
# 内容文件，结果一致。
#   alert.png           怪物蓄力警示「!」：24x36（金黄感叹号 + 暗色
#                       底影 + 近白高光，暗色地图上高对比可见）
#   monster_hit_fx.png  命中爪痕/爆点：48x48（三条橙红斜爪痕 + 中心
#                       黄爆点 + 白心，绘制在接触点 0.15s 渐隐）

# ---- 7.1 alert.png（24x36：竖条 + 点，两层色 + 高光）----
$alertBmp = New-Canvas -width 24 -height 36
# 获取警示画布的 Graphics 绘图对象
$alertG = [System.Drawing.Graphics]::FromImage($alertBmp)
try {
    # 竖条与圆点的几何（放大后像素网格直接落笔；圆点与竖条间留 4px 缝）
    # 底影（暗棕，相对主体右下各偏 1px）：让「!」在亮暗底色上都有轮廓
    $alertShadow = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 120, 70, 20))
    # 主体（金黄）：警示的主色，与受击红染（红）在色相上区分层次
    $alertMain = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 214, 64))
    # 高光（近白偏黄）：竖条/圆点左缘的一道亮线，做伪立体
    $alertHi = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 245, 200))
    try {
        # 底影：竖条与圆点各画一块（右下偏 1px）
        $alertG.FillRectangle($alertShadow, 9, 4, 9, 21)  # 竖条底影
        $alertG.FillRectangle($alertShadow, 9, 29, 9, 8)  # 圆点底影
        # 主体：竖条（x 8..16, y 3..23）与圆点（x 8..16, y 28..35）
        $alertG.FillRectangle($alertMain, 8, 3, 9, 21)    # 竖条主体
        $alertG.FillRectangle($alertMain, 8, 28, 9, 8)    # 圆点主体
        # 高光：竖条与圆点各在左缘画一道 3px 亮线
        $alertG.FillRectangle($alertHi, 10, 3, 3, 21)     # 竖条高光
        $alertG.FillRectangle($alertHi, 10, 28, 3, 8)     # 圆点高光
    } finally {
        # 三支画刷用完释放（关闭 GDI 句柄）
        $alertShadow.Dispose()
        $alertMain.Dispose()
        $alertHi.Dispose()
    }
} finally {
    # 释放警示画布 Graphics（内部持有临时资源）
    $alertG.Dispose()
}
# 保存警示图（幂等覆盖）
Save-Png -bitmap $alertBmp -path (Join-Path $assetsDir "alert.png")
# 释放警示画布位图
$alertBmp.Dispose()

# ---- 7.2 monster_hit_fx.png（48x48：三条斜爪痕 + 中心爆点）----
$fxBmp = New-Canvas -width 48 -height 48
# 获取特画画布的 Graphics 绘图对象
$fxG = [System.Drawing.Graphics]::FromImage($fxBmp)
try {
    # 爪痕主色（橙红）：与受击红染同族，读作「打中了」
    $fxClawPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 240, 66, 50), 5)
    # 爪痕内芯（亮橙）：叠在粗痕上，做「撕裂发光」的层次
    $fxCorePen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 255, 150, 70), 2)
    # 爆点（黄底 + 白心）
    $fxBurstBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 210, 80))
    $fxCoreBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 250, 240))
    try {
        # 三条平行斜痕（左上 -> 右下 45°），错开分布在中心两侧
        $fxG.DrawLine($fxClawPen, 6, 16, 32, 42)   # 左下痕
        $fxG.DrawLine($fxClawPen, 6, 6, 42, 42)    # 中痕（过中心）
        $fxG.DrawLine($fxClawPen, 16, 6, 42, 32)   # 右上痕
        # 亮橙内芯叠在三条粗痕中央（视觉上像痕内的高温）
        $fxG.DrawLine($fxCorePen, 6, 16, 32, 42)   # 左下痕内芯
        $fxG.DrawLine($fxCorePen, 6, 6, 42, 42)    # 中痕内芯
        $fxG.DrawLine($fxCorePen, 16, 6, 42, 32)   # 右上痕内芯
        # 中心爆点：黄圆 r≈8、白心 r≈4（画在爪痕之上，读作打击核心）
        $fxG.FillEllipse($fxBurstBrush, 16, 16, 16, 16) # 黄圆（中心 24,24）
        $fxG.FillEllipse($fxCoreBrush, 20, 20, 8, 8)    # 白心
    } finally {
        # 全部画笔/画刷用完释放（关闭 GDI 句柄）
        $fxClawPen.Dispose()
        $fxCorePen.Dispose()
        $fxBurstBrush.Dispose()
        $fxCoreBrush.Dispose()
    }
} finally {
    # 释放特画画布 Graphics
    $fxG.Dispose()
}
# 保存特画图（幂等覆盖）
Save-Png -bitmap $fxBmp -path (Join-Path $assetsDir "monster_hit_fx.png")
# 释放特画画布位图
$fxBmp.Dispose()

# ============================================================
# 8) 法术素材（批五B：程序化绘制——特效贴图 + HUD 图标）
# ============================================================
# 说明（报告口径）：六种法术特效（火球/冰环/闪电/治疗光/光环/护盾罩）
# 与六枚 HUD 图标同样无法从 DungeonTileset II 原帧组装——由脚本直接
# 程序化绘制（System.Drawing 画圆/环/折线），输出仍是成品 PNG；
# 脚本整体依旧「幂等」：重复运行覆盖同内容文件，结果一致。
#
# 尺寸约定（与 PlayScene.h 的绘制常量一一对应，改一处必须两边同步）：
#   spell_fireball.png        32x32   火球弹体（飞旋绘制；兼作爆炸放大）
#   spell_frost_nova.png     128x128  冰环（缩放展开 + 淡出）
#   spell_chain_lightning.png 32x32   闪电端点电花（折线由代码绘制）
#   spell_heal.png            64x64   治疗光（十字光，上浮 + 淡出）
#   spell_burn_aura.png      160x160  灼烧光环（常驻旋转）
#   spell_shield.png          96x96   护盾罩（呼吸缩放）
#   icon_fireball / icon_frost_nova / icon_chain_lightning /
#   icon_heal / icon_burn_aura / icon_shield  —— 40x40 六枚 HUD 图标

# ---- 8.1 spell_fireball.png（32x32：外晕/主体/内核/白心 四层同心圆）----
$fireballBmp = New-Canvas -width 32 -height 32
# 获取火球画布的 Graphics 绘图对象
$fireballG = [System.Drawing.Graphics]::FromImage($fireballBmp)
try {
    # 四层圆刷（外到内）：低透明外晕 -> 橙主体 -> 亮黄内核 -> 近白高光心
    $fbGlow = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(90, 255, 120, 40))    # 外晕（半透明）
    $fbBody = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 160, 40))   # 主体橙
    $fbCore = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 220, 80))   # 内核亮黄
    $fbHeart = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 250, 220)) # 白心
    try {
        $fireballG.FillEllipse($fbGlow, 0, 0, 32, 32)   # 外晕：整幅圆（软边观感）
        $fireballG.FillEllipse($fbBody, 4, 4, 24, 24)   # 主体：r=12
        $fireballG.FillEllipse($fbCore, 8, 8, 16, 16)   # 内核：r=8
        $fireballG.FillEllipse($fbHeart, 12, 12, 8, 8)  # 白心：r=4
    } finally {
        # 四支画刷用完释放（关闭 GDI 句柄）
        $fbGlow.Dispose()
        $fbBody.Dispose()
        $fbCore.Dispose()
        $fbHeart.Dispose()
    }
} finally {
    # 释放火球画布 Graphics
    $fireballG.Dispose()
}
# 保存火球图（幂等覆盖）并释放画布
Save-Png -bitmap $fireballBmp -path (Join-Path $assetsDir "spell_fireball.png")
$fireballBmp.Dispose()

# ---- 8.2 spell_frost_nova.png（128x128：双环 + 8 根冰刺，青蓝调）----
$frostBmp = New-Canvas -width 128 -height 128
# 获取冰环画布的 Graphics 绘图对象
$frostG = [System.Drawing.Graphics]::FromImage($frostBmp)
try {
    # 外环（亮青粗线）/ 内环（深青细线）/ 冰刺（近白短线）
    $fnOuter = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(220, 180, 235, 255), 6) # 外环笔
    $fnInner = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(200, 90, 180, 240), 3)  # 内环笔
    $fnSpike = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(235, 240, 250, 255), 3) # 冰刺笔
    try {
        $frostG.DrawEllipse($fnOuter, 8, 8, 112, 112) # 外环：圆心 (64,64) r=56
        $frostG.DrawEllipse($fnInner, 30, 30, 68, 68) # 内环：r=34
        # 8 根冰刺：从 r=44 到 r=60 的径向短线（45° 等分）
        for ($k = 0; $k -lt 8; $k++) {
            $angle = $k * [Math]::PI / 4.0 # 本根方位角（弧度）
            $x1 = [float](64 + [Math]::Cos($angle) * 44) # 内端点 x
            $y1 = [float](64 + [Math]::Sin($angle) * 44) # 内端点 y
            $x2 = [float](64 + [Math]::Cos($angle) * 60) # 外端点 x
            $y2 = [float](64 + [Math]::Sin($angle) * 60) # 外端点 y
            $frostG.DrawLine($fnSpike, $x1, $y1, $x2, $y2) # 画本根冰刺
        }
    } finally {
        # 三支画笔用完释放
        $fnOuter.Dispose()
        $fnInner.Dispose()
        $fnSpike.Dispose()
    }
} finally {
    # 释放冰环画布 Graphics
    $frostG.Dispose()
}
# 保存冰环图（幂等覆盖）并释放画布
Save-Png -bitmap $frostBmp -path (Join-Path $assetsDir "spell_frost_nova.png")
$frostBmp.Dispose()

# ---- 8.3 spell_chain_lightning.png（32x32：中心亮斑 + 6 向锯齿光线）----
$boltBmp = New-Canvas -width 32 -height 32
# 获取电花画布的 Graphics 绘图对象
$boltG = [System.Drawing.Graphics]::FromImage($boltBmp)
try {
    # 中心光斑（黄底 + 白心）与锯齿光线笔（亮青）
    $boltBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 230, 120)) # 光斑黄
    $boltHeart = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 245)) # 光斑白心
    $boltPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(235, 200, 230, 255), 2)       # 光线笔
    try {
        # 6 向锯齿光线：每根两段折线（中点向旁偏 6px——固定形状，幂等）
        for ($k = 0; $k -lt 6; $k++) {
            $angle = $k * [Math]::PI / 3.0 # 本根方位角（60° 等分）
            $midX = [float](16 + [Math]::Cos($angle) * 7 + [Math]::Cos($angle + 1.5708) * 5) # 折点 x（垂直偏移）
            $midY = [float](16 + [Math]::Sin($angle) * 7 + [Math]::Sin($angle + 1.5708) * 5) # 折点 y
            $endX = [float](16 + [Math]::Cos($angle) * 15) # 端点 x
            $endY = [float](16 + [Math]::Sin($angle) * 15) # 端点 y
            $boltG.DrawLine($boltPen, 16, 16, $midX, $midY) # 内段
            $boltG.DrawLine($boltPen, $midX, $midY, $endX, $endY) # 外段
        }
        # 中心光斑画在光线之上（读作放电源点）
        $boltG.FillEllipse($boltBrush, 10, 10, 12, 12) # 黄斑 r=6
        $boltG.FillEllipse($boltHeart, 13, 13, 6, 6)   # 白心 r=3
    } finally {
        # 画刷/画笔用完释放
        $boltBrush.Dispose()
        $boltHeart.Dispose()
        $boltPen.Dispose()
    }
} finally {
    # 释放电花画布 Graphics
    $boltG.Dispose()
}
# 保存电花图（幂等覆盖）并释放画布
Save-Png -bitmap $boltBmp -path (Join-Path $assetsDir "spell_chain_lightning.png")
$boltBmp.Dispose()

# ---- 8.4 spell_heal.png（64x64：金色光晕圆 + 双层十字光）----
$healBmp = New-Canvas -width 64 -height 64
# 获取治疗光画布的 Graphics 绘图对象
$healG = [System.Drawing.Graphics]::FromImage($healBmp)
try {
    # 光晕（半透明黄圆）与十字（金色主十字 + 近白内芯）
    $healGlow = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(80, 255, 230, 140))  # 光晕
    $healMain = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 215, 90))  # 主十字
    $healCore = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 250, 230)) # 内芯
    try {
        $healG.FillEllipse($healGlow, 2, 2, 60, 60)   # 光晕：r=30（整幅柔光感）
        $healG.FillRectangle($healMain, 26, 10, 12, 44) # 竖条：12x44 居中
        $healG.FillRectangle($healMain, 10, 26, 44, 12) # 横条：44x12 居中
        $healG.FillRectangle($healCore, 29, 16, 6, 32)  # 竖条内芯
        $healG.FillRectangle($healCore, 16, 29, 32, 6)  # 横条内芯
    } finally {
        # 三支画刷用完释放
        $healGlow.Dispose()
        $healMain.Dispose()
        $healCore.Dispose()
    }
} finally {
    # 释放治疗光画布 Graphics
    $healG.Dispose()
}
# 保存治疗光图（幂等覆盖）并释放画布
Save-Png -bitmap $healBmp -path (Join-Path $assetsDir "spell_heal.png")
$healBmp.Dispose()

# ---- 8.5 spell_burn_aura.png（160x160：外环 + 内环 + 六点火斑）----
$auraBmp = New-Canvas -width 160 -height 160
# 获取光环画布的 Graphics 绘图对象
$auraG = [System.Drawing.Graphics]::FromImage($auraBmp)
try {
    # 外环（橙粗线）/ 内环（亮黄细线）/ 火斑（小圆，散布环上）
    $auOuter = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(210, 255, 140, 40), 10) # 外环笔
    $auInner = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(190, 255, 200, 80), 4)  # 内环笔
    $auFlame = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(200, 255, 170, 60)) # 火斑刷
    try {
        $auraG.DrawEllipse($auOuter, 10, 10, 140, 140) # 外环：r=70（判定半径满级 130 的贴图上缘）
        $auraG.DrawEllipse($auInner, 26, 26, 108, 108) # 内环：r=54
        # 六点火斑：分布在外环线上的小圆（旋转绘制时的「火星」细节）
        for ($k = 0; $k -lt 6; $k++) {
            $angle = $k * [Math]::PI / 3.0 # 本点方位角
            $fx = [float](80 + [Math]::Cos($angle) * 70 - 6) # 火斑左上角 x（r=6 居中）
            $fy = [float](80 + [Math]::Sin($angle) * 70 - 6) # 火斑左上角 y
            $auraG.FillEllipse($auFlame, $fx, $fy, 12, 12)   # 画本点火斑
        }
    } finally {
        # 画笔/画刷用完释放
        $auOuter.Dispose()
        $auInner.Dispose()
        $auFlame.Dispose()
    }
} finally {
    # 释放光环画布 Graphics
    $auraG.Dispose()
}
# 保存光环图（幂等覆盖）并释放画布
Save-Png -bitmap $auraBmp -path (Join-Path $assetsDir "spell_burn_aura.png")
$auraBmp.Dispose()

# ---- 8.6 spell_shield.png（96x96：半透明罩体 + 亮边 + 左上高光弧）----
$shieldBmp = New-Canvas -width 96 -height 96
# 获取护盾画布的 Graphics 绘图对象
$shieldG = [System.Drawing.Graphics]::FromImage($shieldBmp)
try {
    # 罩体（半透明蓝圆）/ 亮边（青白粗环）/ 高光弧（左上 90 度）
    $shFill = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(60, 130, 190, 255)) # 罩体填充
    $shRim = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(220, 170, 220, 255), 4)      # 罩体边
    $shHi = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(235, 240, 250, 255), 3)       # 高光弧
    try {
        $shieldG.FillEllipse($shFill, 4, 4, 88, 88)  # 罩体：r=44 半透明填充
        $shieldG.DrawEllipse($shRim, 4, 4, 88, 88)   # 罩体亮边
        # 高光弧：左上 90 度（-180° 起扫 90°；GDI+ 角度顺时针、0° 在右）
        $shieldG.DrawArc($shHi, 4, 4, 88, 88, 180, 90) # 左上弧段
    } finally {
        # 画刷/画笔用完释放
        $shFill.Dispose()
        $shRim.Dispose()
        $shHi.Dispose()
    }
} finally {
    # 释放护盾画布 Graphics
    $shieldG.Dispose()
}
# 保存护盾图（幂等覆盖）并释放画布
Save-Png -bitmap $shieldBmp -path (Join-Path $assetsDir "spell_shield.png")
$shieldBmp.Dispose()

# ---- 8.7 HUD 图标（六枚 40x40）：同款符号的简化版 ----

# icon_fireball.png（40x40：火球三层圆）
$iconFireballBmp = New-Canvas -width 40 -height 40
$iconFireballG = [System.Drawing.Graphics]::FromImage($iconFireballBmp)
try {
    $icFbBody = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 160, 40))  # 主体
    $icFbCore = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 220, 80))  # 内核
    $icFbHeart = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 250, 220)) # 白心
    try {
        $iconFireballG.FillEllipse($icFbBody, 4, 4, 32, 32)   # 主体：r=16
        $iconFireballG.FillEllipse($icFbCore, 10, 10, 20, 20) # 内核：r=10
        $iconFireballG.FillEllipse($icFbHeart, 15, 15, 10, 10) # 白心：r=5
    } finally {
        $icFbBody.Dispose()
        $icFbCore.Dispose()
        $icFbHeart.Dispose()
    }
} finally {
    $iconFireballG.Dispose()
}
Save-Png -bitmap $iconFireballBmp -path (Join-Path $assetsDir "icon_fireball.png")
$iconFireballBmp.Dispose()

# icon_frost_nova.png（40x40：冰环 + 六向雪花线）
$iconFrostBmp = New-Canvas -width 40 -height 40
$iconFrostG = [System.Drawing.Graphics]::FromImage($iconFrostBmp)
try {
    $icFnRim = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(225, 180, 235, 255), 4) # 环笔
    $icFnSpike = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(235, 240, 250, 255), 2) # 雪花笔
    try {
        $iconFrostG.DrawEllipse($icFnRim, 6, 6, 28, 28) # 环：r=14
        # 六向雪花线：r=6 到 r=14（60° 等分）
        for ($k = 0; $k -lt 6; $k++) {
            $angle = $k * [Math]::PI / 3.0 # 本线方位角
            $x1 = [float](20 + [Math]::Cos($angle) * 6)  # 内端 x
            $y1 = [float](20 + [Math]::Sin($angle) * 6)  # 内端 y
            $x2 = [float](20 + [Math]::Cos($angle) * 14) # 外端 x
            $y2 = [float](20 + [Math]::Sin($angle) * 14) # 外端 y
            $iconFrostG.DrawLine($icFnSpike, $x1, $y1, $x2, $y2) # 画本线
        }
    } finally {
        $icFnRim.Dispose()
        $icFnSpike.Dispose()
    }
} finally {
    $iconFrostG.Dispose()
}
Save-Png -bitmap $iconFrostBmp -path (Join-Path $assetsDir "icon_frost_nova.png")
$iconFrostBmp.Dispose()

# icon_chain_lightning.png（40x40：三段闪电折线）
$iconBoltBmp = New-Canvas -width 40 -height 40
$iconBoltG = [System.Drawing.Graphics]::FromImage($iconBoltBmp)
try {
    $icBoltPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 255, 230, 120), 4) # 闪电笔（金黄）
    try {
        # Z 字折线：上 -> 中 -> 下（固定形状）
        $iconBoltG.DrawLine($icBoltPen, 24, 4, 14, 20) # 上段：右上 -> 中左
        $iconBoltG.DrawLine($icBoltPen, 14, 20, 26, 20) # 横折：中左 -> 中右
        $iconBoltG.DrawLine($icBoltPen, 26, 20, 16, 36) # 下段：中右 -> 下左
    } finally {
        $icBoltPen.Dispose()
    }
} finally {
    $iconBoltG.Dispose()
}
Save-Png -bitmap $iconBoltBmp -path (Join-Path $assetsDir "icon_chain_lightning.png")
$iconBoltBmp.Dispose()

# icon_heal.png（40x40：金色十字）
$iconHealBmp = New-Canvas -width 40 -height 40
$iconHealG = [System.Drawing.Graphics]::FromImage($iconHealBmp)
try {
    $icHealBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 215, 90)) # 十字刷
    try {
        $iconHealG.FillRectangle($icHealBrush, 16, 6, 8, 28) # 竖条
        $iconHealG.FillRectangle($icHealBrush, 6, 16, 28, 8) # 横条
    } finally {
        $icHealBrush.Dispose()
    }
} finally {
    $iconHealG.Dispose()
}
Save-Png -bitmap $iconHealBmp -path (Join-Path $assetsDir "icon_heal.png")
$iconHealBmp.Dispose()

# icon_burn_aura.png（40x40：圆环 + 中心小火点）
$iconAuraBmp = New-Canvas -width 40 -height 40
$iconAuraG = [System.Drawing.Graphics]::FromImage($iconAuraBmp)
try {
    $icAuRing = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(230, 255, 140, 40), 5) # 环笔
    $icAuFlame = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(220, 255, 200, 80)) # 火点刷
    try {
        $iconAuraG.DrawEllipse($icAuRing, 6, 6, 28, 28)   # 外环：r=14
        $iconAuraG.FillEllipse($icAuFlame, 15, 15, 10, 10) # 中心火点：r=5
    } finally {
        $icAuRing.Dispose()
        $icAuFlame.Dispose()
    }
} finally {
    $iconAuraG.Dispose()
}
Save-Png -bitmap $iconAuraBmp -path (Join-Path $assetsDir "icon_burn_aura.png")
$iconAuraBmp.Dispose()

# icon_shield.png（40x40：盾牌形 + 内十字）
$iconShieldBmp = New-Canvas -width 40 -height 40
$iconShieldG = [System.Drawing.Graphics]::FromImage($iconShieldBmp)
try {
    # 盾形多边形（上宽下尖的经典轮廓）与内十字
    $icShBody = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(220, 110, 170, 240)) # 盾体
    $icShEdge = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(235, 200, 235, 255), 2)      # 盾边
    $icShCross = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 240, 250, 255)) # 内十字
    try {
        # 顶点数组：左上 -> 右上 -> 右中 -> 底尖 -> 左中（顺时针）
        $pts = New-Object 'System.Drawing.Point[]' 5
        $pts[0] = New-Object System.Drawing.Point(8, 6)   # 左上
        $pts[1] = New-Object System.Drawing.Point(32, 6)  # 右上
        $pts[2] = New-Object System.Drawing.Point(32, 20) # 右中
        $pts[3] = New-Object System.Drawing.Point(20, 34) # 底尖
        $pts[4] = New-Object System.Drawing.Point(8, 20)  # 左中
        $iconShieldG.FillPolygon($icShBody, $pts)  # 填充盾体
        $iconShieldG.DrawPolygon($icShEdge, $pts)  # 描边
        $iconShieldG.FillRectangle($icShCross, 18, 10, 4, 16) # 内十字竖
        $iconShieldG.FillRectangle($icShCross, 12, 16, 16, 4) # 内十字横
    } finally {
        $icShBody.Dispose()
        $icShEdge.Dispose()
        $icShCross.Dispose()
    }
} finally {
    $iconShieldG.Dispose()
}
Save-Png -bitmap $iconShieldBmp -path (Join-Path $assetsDir "icon_shield.png")
$iconShieldBmp.Dispose()

# ============================================================
# 9) 海洋水砖（批六甲·海洋边界）
# ============================================================
# 说明（报告口径）：DungeonTileset II 素材包内没有「水面」帧——水砖由
# 脚本程序化绘制（System.Drawing 正弦折线 + 矩形填充），输出仍是与其余
# 素材一致的成品 PNG；脚本整体依旧「幂等」：重复运行覆盖同内容文件，
# 结果一致。世界最外 2 圈格在烘焙时按坐标哈希挑 3 个变体之一铺放
#（见 World::bakeChunk），配套「岸边浪花过渡线」在烘焙期按邻格判定
# 程序化叠加（不占素材）。
#   tiles_water.png  3 变体 x 48px = 144x48：深蓝基色 + 三道正弦波纹
#                    （相位随变体偏移）+ 底部深水条 + 两处高光短划

# ---- 9.1 tiles_water.png（3 变体，每变体 48x48）----
$waterBmp = New-Canvas -width 144 -height 48
# 获取水砖画布的 Graphics 绘图对象
$waterG = [System.Drawing.Graphics]::FromImage($waterBmp)
try {
    # 基色（深蓝）/ 深水条（更暗，读作水域下沿层次）/ 波纹笔 / 高光笔
    $waterBase = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 24, 46, 88))   # 深蓝基色
    $waterDeep = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 18, 36, 72))   # 底部深水条
    $wavePen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 58, 108, 168), 2)       # 波纹（中蓝 2px）
    $sparkPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 120, 180, 225), 2)     # 高光（亮蓝 2px）
    try {
        # 逐变体绘制：v = 0..2，横向起点 = v x 48（与引擎裁帧约定一致）
        for ($v = 0; $v -lt 3; $v++) {
            $ox = $v * 48 # 本变体在表中的横向起点（像素）
            # 基色铺满本格（不透明——水域是实体地表，不需要透明背景）
            $waterG.FillRectangle($waterBase, $ox, 0, 48, 48)
            # 底部深水条：8px 高的更暗色带（水深的观感层次）
            $waterG.FillRectangle($waterDeep, $ox, 40, 48, 8)
            # 三道正弦波纹：横向每 4px 取一点（12 段折线），相位随变体错开
            for ($row = 0; $row -lt 3; $row++) {
                $baseY = 10 + $row * 12 # 本道波纹的中线高度（10 / 22 / 34）
                $phase = ($v * 16) + ($row * 5) # 相位偏移：变体间波纹形态不同
                $pts = New-Object 'System.Drawing.Point[]' 13 # 13 个点（x = 0..48 步进 4）
                for ($i = 0; $i -lt 13; $i++) {
                    $x = $i * 4 # 本点相对本变体左缘的 x
                    # 正弦纵摆（振幅 1.5px，四舍五入到整像素——像素风不失真）
                    $yp = [int]($baseY + [Math]::Round(1.5 * [Math]::Sin(($x + $phase) / 7.0)))
                    $xp = $ox + $x # 本点全局 x（PS 5.1 的 New-Object 参数不能内联运算——先算好）
                    $pts[$i] = New-Object System.Drawing.Point($xp, $yp) # 记录本点
                }
                $waterG.DrawLines($wavePen, $pts) # 画本道波纹
            }
            # 两处高光短划（亮蓝 8px 横线；位置随变体轻微偏移——阳光碎点）
            $sx1 = $ox + 6 + (($v * 7) % 20) # 第一处起点 x（变体偏移）
            $waterG.DrawLine($sparkPen, $sx1, 6, ($sx1 + 8), 6) # 上带高光
            $sx2 = $ox + 30 - (($v * 5) % 16) # 第二处起点 x（变体偏移）
            $waterG.DrawLine($sparkPen, $sx2, 34, ($sx2 + 8), 34) # 下带高光
        }
    } finally {
        # 画刷/画笔用完释放（关闭 GDI 句柄）
        $waterBase.Dispose()
        $waterDeep.Dispose()
        $wavePen.Dispose()
        $sparkPen.Dispose()
    }
} finally {
    # 释放水砖画布 Graphics
    $waterG.Dispose()
}
# 保存水砖图（幂等覆盖）并释放画布
Save-Png -bitmap $waterBmp -path (Join-Path $assetsDir "tiles_water.png")
$waterBmp.Dispose()

# ============================================================
# 10) 批六乙事件素材：复活点神龛（两态）与落水水花
# ============================================================
# 说明：素材包内没有神龛/水花帧，沿用批五A/五B 的「程序化绘制」口径。
#   shrine.png : 2 帧横向（暗/亮），每帧 64x80；左 = 未激活、右 = 已激活。
#                PlayScene 按激活状态选源帧（亮帧更亮 + 青色宝石光晕）。
#   splash.png : 单帧 64x64 水面飞溅；运行时由代码做放大 + 淡出（约 0.4s）。
# 两者都走 Save-Png（幂等覆盖），尺寸在文末自检中核对。

# ---- 10.1 shrine.png：两帧横向（128x80）----
$shrineBmp = New-Canvas -width 128 -height 80
$shrineG = [System.Drawing.Graphics]::FromImage($shrineBmp)
try {
    # 逐帧绘制：f=0 未激活（灰暗）/ f=1 已激活（发亮）
    for ($f = 0; $f -lt 2; $f++) {
        $ox = $f * 64 # 本帧横向起点（像素）
        $lit = ($f -eq 1) # 是否亮态帧

        # 颜色分档：亮态整体提亮，暗态偏冷灰
        if ($lit) {
            $stoneBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 116, 132, 158)) # 石柱亮部
            $stoneDark = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 72, 84, 106))  # 石柱暗部
            $gemBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 150, 245, 255))   # 宝石亮青
            $glowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(90, 90, 220, 255))   # 光晕
        } else {
            $stoneBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 66, 72, 86))  # 石柱暗部
            $stoneDark = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 44, 50, 62))  # 石柱更深
            $gemBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 64, 82, 96))    # 宝石灰蓝
            $glowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(0, 0, 0, 0))        # 暗态无光晕
        }
        $outlinePen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 24, 28, 38), 2)          # 外轮廓
        $baseBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 50, 56, 70))       # 底座
        $runePen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 255, 214, 120), 2)          # 亮态符文

        try {
            # 光晕（仅亮态）：宝石中心的半透明大圆——激活后「发亮」的主信号
            $shrineG.FillEllipse($glowBrush, $ox + 10, 0, 44, 44)

            # 底座：宽 52 / 高 14，贴帧底（y=64）
            $shrineG.FillRectangle($baseBrush, $ox + 6, 64, 52, 14)
            $shrineG.DrawRectangle($outlinePen, $ox + 6, 64, 52, 14)

            # 石柱：宽 28 / 高 42（从 y=24 到 y=66；底部略压住底座上缘）
            $shrineG.FillRectangle($stoneBrush, $ox + 18, 24, 28, 42)
            # 柱身左侧暗部：4px 竖条（体积感）
            $shrineG.FillRectangle($stoneDark, $ox + 18, 24, 6, 42)
            $shrineG.DrawRectangle($outlinePen, $ox + 18, 24, 28, 42)

            # 柱顶宝石：中心 (32,20)，半径 10
            $shrineG.FillEllipse($gemBrush, $ox + 22, 10, 20, 20)
            $shrineG.DrawEllipse($outlinePen, $ox + 22, 10, 20, 20)

            # 亮态符文：柱身中段一条竖线 + 两颗小点（暗态不画）
            if ($lit) {
                $shrineG.DrawLine($runePen, $ox + 32, 34, $ox + 32, 56) # 竖向符文
                $shrineG.FillEllipse($runePen.Brush, $ox + 29, 38, 6, 6) # 上符文点
                $shrineG.FillEllipse($runePen.Brush, $ox + 29, 48, 6, 6) # 下符文点
            }
        } finally {
            # 本帧画刷/画笔释放（GDI 句柄）
            $stoneBrush.Dispose()
            $stoneDark.Dispose()
            $gemBrush.Dispose()
            $glowBrush.Dispose()
            $outlinePen.Dispose()
            $baseBrush.Dispose()
            $runePen.Dispose()
        }
    }
} finally {
    # 释放神龛画布 Graphics
    $shrineG.Dispose()
}
# 保存神龛两态表（幂等覆盖）并释放画布
Save-Png -bitmap $shrineBmp -path (Join-Path $assetsDir "shrine.png")
$shrineBmp.Dispose()

# ---- 10.2 splash.png：单帧 64x64 水面飞溅 ----
$splashBmp = New-Canvas -width 64 -height 64
$splashG = [System.Drawing.Graphics]::FromImage($splashBmp)
try {
    # 颜色：中心水花亮青白、外圈水蓝、水滴深蓝（透明底）
    $coreBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(235, 225, 250, 255)) # 中心亮白
    $ringPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(220, 130, 210, 245), 3)       # 外环水蓝
    $dropBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(230, 150, 225, 255)) # 飞溅水滴
    $innerPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(200, 90, 170, 225), 2)        # 内环
    try {
        # 中心水花：扁椭圆（水面爆开的核）
        $splashG.FillEllipse($coreBrush, 24, 28, 16, 10)
        # 外环：横向椭圆（波浪扩散）
        $splashG.DrawEllipse($ringPen, 8, 30, 48, 14)
        # 内环：略小的第二圈
        $splashG.DrawEllipse($innerPen, 18, 32, 28, 9)

        # 飞溅水滴：左右各 3 条斜向外的小水滴（椭圆）
        $splashG.FillEllipse($dropBrush, 12, 12, 7, 10)  # 左上水滴
        $splashG.FillEllipse($dropBrush, 45, 10, 7, 11)  # 右上水滴
        $splashG.FillEllipse($dropBrush, 4, 24, 6, 9)    # 左中水滴
        $splashG.FillEllipse($dropBrush, 54, 24, 6, 9)   # 右中水滴
        $splashG.FillEllipse($dropBrush, 20, 4, 6, 9)    # 中上水滴
        $splashG.FillEllipse($dropBrush, 38, 3, 6, 9)    # 中上偏右水滴
    } finally {
        # 画刷/画笔释放（GDI 句柄）
        $coreBrush.Dispose()
        $ringPen.Dispose()
        $dropBrush.Dispose()
        $innerPen.Dispose()
    }
} finally {
    # 释放水花画布 Graphics
    $splashG.Dispose()
}
# 保存水花贴图（幂等覆盖）并释放画布
Save-Png -bitmap $splashBmp -path (Join-Path $assetsDir "splash.png")
$splashBmp.Dispose()

# ============================================================
# 11) 生成后自检：重新读盘校验全部成品 PNG 的尺寸
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

#   金币精灵表：4 帧 x 18 宽 = 72，高 21（Coin 的 m_w=18 / m_h=21）
Assert-PngSize -path (Join-Path $assetsDir "coin_sheet.png") -expectedWidth 72 -expectedHeight 21
#   金币单帧回退：18x21
Assert-PngSize -path (Join-Path $assetsDir "coin.png") -expectedWidth 18 -expectedHeight 21

#   宝石精灵表：4 帧 x 18 宽 = 72，高 21（XpGem 的 m_w=18 / m_h=21）
Assert-PngSize -path (Join-Path $assetsDir "gem_sheet.png") -expectedWidth 72 -expectedHeight 21
#   宝石单帧回退：18x21
Assert-PngSize -path (Join-Path $assetsDir "gem.png") -expectedWidth 18 -expectedHeight 21

#   武器：30x87（原始 10x29 的 3 倍）
Assert-PngSize -path (Join-Path $assetsDir "weapon.png") -expectedWidth 30 -expectedHeight 87

#   背景：1280x720（与初始窗口一致；绘制时按窗口实际尺寸拉伸）
Assert-PngSize -path (Join-Path $assetsDir "background.png") -expectedWidth 1280 -expectedHeight 720

#   世界素材（批二）：地砖 8 帧 x 48 = 384 宽、48 高；墙块 48x48；装饰表 96x72
Assert-PngSize -path (Join-Path $assetsDir "tiles_floor.png") -expectedWidth 384 -expectedHeight 48
Assert-PngSize -path (Join-Path $assetsDir "tiles_wall.png") -expectedWidth 48 -expectedHeight 48
Assert-PngSize -path (Join-Path $assetsDir "decor_sheet.png") -expectedWidth 96 -expectedHeight 72

#   海洋水砖（批六甲）：3 变体 x 48 = 144 宽、48 高
Assert-PngSize -path (Join-Path $assetsDir "tiles_water.png") -expectedWidth 144 -expectedHeight 48

#   雪山素材（批六甲·冰霜重染色）：与地牢素材同尺寸——地砖 384x48、
#   墙块 48x48、装饰表 96x72
Assert-PngSize -path (Join-Path $assetsDir "tiles_floor_snow.png") -expectedWidth 384 -expectedHeight 48
Assert-PngSize -path (Join-Path $assetsDir "tiles_wall_snow.png") -expectedWidth 48 -expectedHeight 48
Assert-PngSize -path (Join-Path $assetsDir "decor_sheet_snow.png") -expectedWidth 96 -expectedHeight 72

#   攻击反馈素材（批五A）：警示「!」24x36；爪痕爆点 48x48
Assert-PngSize -path (Join-Path $assetsDir "alert.png") -expectedWidth 24 -expectedHeight 36
Assert-PngSize -path (Join-Path $assetsDir "monster_hit_fx.png") -expectedWidth 48 -expectedHeight 48

#   法术特效素材（批五B）：火球 32x32、冰环 128x128、电花 32x32、
#   治疗光 64x64、光环 160x160、护盾罩 96x96（与 PlayScene.h 常量一一对应）
Assert-PngSize -path (Join-Path $assetsDir "spell_fireball.png") -expectedWidth 32 -expectedHeight 32
Assert-PngSize -path (Join-Path $assetsDir "spell_frost_nova.png") -expectedWidth 128 -expectedHeight 128
Assert-PngSize -path (Join-Path $assetsDir "spell_chain_lightning.png") -expectedWidth 32 -expectedHeight 32
Assert-PngSize -path (Join-Path $assetsDir "spell_heal.png") -expectedWidth 64 -expectedHeight 64
Assert-PngSize -path (Join-Path $assetsDir "spell_burn_aura.png") -expectedWidth 160 -expectedHeight 160
Assert-PngSize -path (Join-Path $assetsDir "spell_shield.png") -expectedWidth 96 -expectedHeight 96

#   法术 HUD 图标（批五B）：六枚 40x40
Assert-PngSize -path (Join-Path $assetsDir "icon_fireball.png") -expectedWidth 40 -expectedHeight 40
Assert-PngSize -path (Join-Path $assetsDir "icon_frost_nova.png") -expectedWidth 40 -expectedHeight 40
Assert-PngSize -path (Join-Path $assetsDir "icon_chain_lightning.png") -expectedWidth 40 -expectedHeight 40
Assert-PngSize -path (Join-Path $assetsDir "icon_heal.png") -expectedWidth 40 -expectedHeight 40
Assert-PngSize -path (Join-Path $assetsDir "icon_burn_aura.png") -expectedWidth 40 -expectedHeight 40
Assert-PngSize -path (Join-Path $assetsDir "icon_shield.png") -expectedWidth 40 -expectedHeight 40

#   批六乙事件素材：神龛两帧横向（2 x 64 = 128 宽，80 高）；水花 64x64
Assert-PngSize -path (Join-Path $assetsDir "shrine.png") -expectedWidth 128 -expectedHeight 80
Assert-PngSize -path (Join-Path $assetsDir "splash.png") -expectedWidth 64 -expectedHeight 64

# 全部完成提示
Write-Host "全部成品素材组装并校验完成。"
