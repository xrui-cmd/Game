# ============================================================
# GameDev 素材生成脚本（tools/generate_assets.ps1）
# ============================================================
# 作用：用 Windows PowerShell 5.1 自带（无需安装）的 System.Drawing 绘图库，
#       程序化绘制游戏占位素材并保存为带透明通道的 PNG：
#         assets/player.png   50x50 玩家精灵（绿色方块 + 深色描边 + 眼睛）
#         assets/coin.png     24x24 金币精灵（金色圆 + 深色描边 + 高光）
#
# 设计原则：
#   1. 不依赖任何第三方工具或素材文件，开箱即可重新生成占位素材
#   2. 幂等：可以重复运行，每次覆盖旧文件，输出结果一致
#   3. 尺寸与游戏逻辑严格一致（50x50 / 24x24），若改尺寸需两边同步修改
#
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/generate_assets.ps1
# ============================================================

# 遇到任何错误立即终止，避免"看起来成功、实际没生成"的假象
$ErrorActionPreference = "Stop"

# 加载 .NET 自带的绘图库（Windows 系统组件，无需额外安装）
Add-Type -AssemblyName System.Drawing

# $PSScriptRoot 是脚本所在目录（tools/）；仓库根目录是它的上一级
$repoRoot = Split-Path -Parent $PSScriptRoot

# 素材输出目录：<仓库根>/assets
$assetsDir = Join-Path $repoRoot "assets"

# 目录不存在则先创建（| Out-Null 丢弃 New-Item 的返回值，保持输出干净）
if (-not (Test-Path $assetsDir)) {
    # -ItemType Directory 明确创建目录；-Force 保证中间层级不存在时也能建好
    New-Item -ItemType Directory -Path $assetsDir -Force | Out-Null
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

# ============================================================
# 1) 玩家精灵 player.png：50x50 绿色方块 + 深色描边 + 两只眼睛
# ============================================================
# 画布边长必须与 Player 逻辑中的 50x50（m_width/m_height）一致
$playerSize = 50

# 创建 50x50 带透明通道的玩家画布
$playerBmp = New-Canvas -width $playerSize -height $playerSize

# 获取该画布的 Graphics 绘图对象（所有绘制都通过它进行）
$g = [System.Drawing.Graphics]::FromImage($playerBmp)

# 打开抗锯齿，让后面画的圆形眼睛边缘平滑不毛糙
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

# 背景整体清成完全透明（alpha=0），避免出现黑色底
$g.Clear([System.Drawing.Color]::Transparent)

# 身体矩形：四周各留 2 像素透明边，视觉上与整个 50x50 碰撞盒几乎一致
$bodyRect = New-Object System.Drawing.Rectangle(2, 2, 46, 46)

# 身体填充色：明亮的草绿色（R=76, G=175, B=80）
$bodyBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 76, 175, 80))

# 用画刷填满整个身体矩形（方块主体）
$g.FillRectangle($bodyBrush, $bodyRect)

# 描边画笔：深绿色（R=46, G=125, B=50），3 像素宽，用于勾勒轮廓提升辨识度
$outlinePen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 46, 125, 50), 3)

# 沿身体矩形描一圈深绿边框（与玩家程序化回退纹理的"纯色块"形成区分）
$g.DrawRectangle($outlinePen, $bodyRect)

# 眼白画刷：纯白色
$eyeWhiteBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)

# 瞳孔画刷：近黑的深灰色（R=G=B=33），比纯黑视觉上柔和一点
$pupilBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 33, 33, 33))

# 左眼：白色圆形，外接矩形 (10,13,14,14)，圆心约 (17,20)
$g.FillEllipse($eyeWhiteBrush, 10, 13, 14, 14)

# 右眼：白色圆形，外接矩形 (26,13,14,14)，与左眼关于画布中线对称
$g.FillEllipse($eyeWhiteBrush, 26, 13, 14, 14)

# 左瞳孔：深灰小圆，圆心比左眼中心偏右下 1 像素，像在向前看
$g.FillEllipse($pupilBrush, 15, 18, 6, 6)

# 右瞳孔：与左瞳孔完全对称
$g.FillEllipse($pupilBrush, 31, 18, 6, 6)

# 保存玩家精灵到 assets/player.png（内部先删旧文件，保证幂等）
Save-Png -bitmap $playerBmp -path (Join-Path $assetsDir "player.png")

# 释放填充画刷，防止 GDI 句柄泄漏（脚本短也要养成好习惯）
$bodyBrush.Dispose()

# 释放描边画笔
$outlinePen.Dispose()

# 释放眼白画刷
$eyeWhiteBrush.Dispose()

# 释放瞳孔画刷
$pupilBrush.Dispose()

# 释放 Graphics 绘图对象
$g.Dispose()

# 释放玩家位图本身（像素数据在内存中的载体）
$playerBmp.Dispose()

# ============================================================
# 2) 金币精灵 coin.png：24x24 金色圆 + 深色描边 + 高光
# ============================================================
# 画布边长必须与 Coin 逻辑中的 24x24（kSize 常量）一致
$coinSize = 24

# 创建 24x24 带透明通道的金币画布
$coinBmp = New-Canvas -width $coinSize -height $coinSize

# 获取该画布的 Graphics 绘图对象
$gc = [System.Drawing.Graphics]::FromImage($coinBmp)

# 打开抗锯齿：圆形金币必须有平滑边缘，否则看起来是锯齿块
$gc.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

# 背景清成完全透明
$gc.Clear([System.Drawing.Color]::Transparent)

# 金币主体画刷：亮金色（R=255, G=215, B=0，经典"金币金"）
$goldBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 215, 0))

# 金币描边画笔：深金色（R=184, G=134, B=11），2 像素宽，勾出金币轮廓
$goldPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 184, 134, 11), 2)

# 金币圆形外接矩形：四周留约 2 像素，边长 20，圆心正好在画布中心 (12,12)
$coinEllipseRect = New-Object System.Drawing.Rectangle(2, 2, 20, 20)

# 填充金币主体：亮金色实心圆
$gc.FillEllipse($goldBrush, $coinEllipseRect)

# 描边：沿同一外接矩形画深金色圆环，让金币更有立体感
$gc.DrawEllipse($goldPen, $coinEllipseRect)

# 高光画刷：半透明白色（alpha=210），模拟金属反光
$highlightBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(210, 255, 255, 255))

# 左上角高光：小椭圆 (7,6,6,6)，是金币"发光"的点睛之笔
$gc.FillEllipse($highlightBrush, 7, 6, 6, 6)

# 保存金币精灵到 assets/coin.png（内部先删旧文件，保证幂等）
Save-Png -bitmap $coinBmp -path (Join-Path $assetsDir "coin.png")

# 释放金币填充画刷
$goldBrush.Dispose()

# 释放金币描边画笔
$goldPen.Dispose()

# 释放高光画刷
$highlightBrush.Dispose()

# 释放 Graphics 绘图对象
$gc.Dispose()

# 释放金币位图本身
$coinBmp.Dispose()

# ============================================================
# 3) 生成后自检：重新读盘校验两张 PNG 的尺寸与逻辑一致
# ============================================================
# 校验玩家精灵必须为 50x50（与 Player 的 m_width/m_height 对应）
Assert-PngSize -path (Join-Path $assetsDir "player.png") -expectedWidth 50 -expectedHeight 50

# 校验金币精灵必须为 24x24（与 Coin 的 kSize 对应）
Assert-PngSize -path (Join-Path $assetsDir "coin.png") -expectedWidth 24 -expectedHeight 24

# 全部完成提示
Write-Host "全部素材生成并校验完成。"
