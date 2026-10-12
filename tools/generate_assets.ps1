# ============================================================
# GameDev 素材生成脚本（tools/generate_assets.ps1）
# ============================================================
# 作用：用 Windows PowerShell 5.1 自带（无需安装）的 System.Drawing 绘图库，
#       程序化绘制游戏占位素材并保存为 PNG（精灵带透明通道，背景为不透明底图）：
#         assets/player.png        50x50 玩家单帧精灵（绿色方块 + 描边 + 眼睛，回退素材）
#         assets/coin.png          24x24 金币单帧精灵（金色圆 + 描边 + 高光，回退素材）
#         assets/coin_sheet.png    144x24 金币旋转精灵表（6 帧横向排列，每帧 24x24）
#         assets/player_sheet.png  100x50 玩家行走精灵表（2 帧横向排列，每帧 50x50）
#         assets/background.png    1280x720 背景图（深蓝渐变 + 细微网格 + 星点）
#
# 设计原则：
#   1. 不依赖任何第三方工具或素材文件，开箱即可重新生成占位素材
#   2. 幂等：可以重复运行，每次覆盖旧文件，输出结果一致
#   3. 尺寸与游戏逻辑严格一致（玩家 50x50 / 金币 24x24 / 精灵表单帧边长不变），
#      若改尺寸需代码与脚本两边同步修改
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
# 3) 金币旋转精灵表 coin_sheet.png：6 帧横向排列，每帧 24x24
# ============================================================
# 原理：不真正旋转像素，而是让金币"画面的宽度"随转动角度收窄再放开，
#       6 帧连播形成"金片翻转"的循环动画：
#       满宽 -> 逐渐变窄 -> 最窄（近似侧面）-> 逐渐变宽 -> 回到满宽
$coinFrameCount = 6  # 总帧数，必须与 Coin 代码里的 kFrameCount 一致
$coinFrameSize = 24  # 单帧边长，必须与 Coin 的 kSize 一致

# 创建整张精灵表画布：宽 = 6 帧 x 24 = 144，高 = 24
$csBmp = New-Canvas -width ($coinFrameCount * $coinFrameSize) -height $coinFrameSize

# 获取精灵表的 Graphics 绘图对象
$csG = [System.Drawing.Graphics]::FromImage($csBmp)

# 打开抗锯齿：椭圆边缘平滑，避免锯齿块
$csG.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

# 背景清成完全透明，帧与帧之间自然留出透明间隙
$csG.Clear([System.Drawing.Color]::Transparent)

# 每帧的金币宽度比例：1.0 表示满宽（正对观众），越接近 0 越像转到侧面。
# 序列首尾都接近满宽，循环播放时衔接平滑
$csScales = @(1.0, 0.72, 0.42, 0.14, 0.42, 0.72)

# 复用与 coin.png 完全相同的金色配色：主体亮金、描边深金
$csGoldBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 215, 0))
$csGoldPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 184, 134, 11), 2)

# 高光画刷：半透明白色（alpha=210），与 coin.png 保持一致
$csHighlightBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(210, 255, 255, 255))

# 逐帧绘制：第 f 帧在整张表中的横向起点 = f * 24
for ($f = 0; $f -lt $coinFrameCount; $f++) {
    # 本帧的宽度比例（从比例表中取出）
    $csScale = $csScales[$f]
    # 本帧金币主体宽度（像素）：满宽 20 按比例缩放，最窄保留 2 像素避免完全消失
    $csFrameW = [Math]::Max(20.0 * $csScale, 2.0)
    # 本帧金币左边界 = 帧起点 + 帧内居中偏移（满宽时正好落在帧内 x=2，与 coin.png 相同）
    $csFrameX = $f * $coinFrameSize + (20.0 - $csFrameW) / 2.0 + 2.0
    # 金币主体外接矩形：垂直方向与 coin.png 相同（上边距 2、高 20、圆心在帧中心）
    $csBodyRect = New-Object System.Drawing.RectangleF($csFrameX, 2.0, $csFrameW, 20.0)
    # 填充金币主体（金色实心椭圆）
    $csG.FillEllipse($csGoldBrush, $csBodyRect)
    # 描边：沿同一外接矩形画深金圆环，让金币更有立体感
    $csG.DrawEllipse($csGoldPen, $csBodyRect)

    # 高光宽度随旋转等比缩放（最窄保留 1.5 像素），像贴在转动的币面上
    $csHlW = [Math]::Max(6.0 * $csScale, 1.5)
    # 高光左边界：金币帧中心 x=12，高光中心相对金币中心偏移 (-2) 按比例缩放，再退半宽
    $csHlX = $f * $coinFrameSize + 12.0 + (10.0 - 12.0) * $csScale - $csHlW / 2.0
    # 高光外接矩形：垂直位置与 coin.png 相同（y=6、高 6、左上角位置）
    $csHlRect = New-Object System.Drawing.RectangleF($csHlX, 6.0, $csHlW, 6.0)
    # 填充高光（半透明白色小椭圆）
    $csG.FillEllipse($csHighlightBrush, $csHlRect)
}

# 保存金币旋转精灵表到 assets/coin_sheet.png（内部先删旧文件，保证幂等）
Save-Png -bitmap $csBmp -path (Join-Path $assetsDir "coin_sheet.png")

# 释放金币精灵表的填充画刷
$csGoldBrush.Dispose()

# 释放描边画笔
$csGoldPen.Dispose()

# 释放高光画刷
$csHighlightBrush.Dispose()

# 释放 Graphics 绘图对象
$csG.Dispose()

# 释放精灵表位图本身
$csBmp.Dispose()

# ============================================================
# 4) 玩家行走精灵表 player_sheet.png：2 帧横向排列，每帧 50x50
# ============================================================
# 原理：帧 1 与 player.png 完全一致（站姿）；帧 2 把身体顶部下压 2 像素、
#       眼睛随身体同步下移，形成"下蹲-站起"的行走起伏。两帧交替即迈步动画
$playerFrameCount = 2  # 总帧数，必须与 Player 代码里的 kFrameCount 一致
$playerFrameSize = 50  # 单帧边长，必须与 Player 的 m_width/m_height 一致

# 创建整张精灵表画布：宽 = 2 帧 x 50 = 100，高 = 50
$psBmp = New-Canvas -width ($playerFrameCount * $playerFrameSize) -height $playerFrameSize

# 获取精灵表的 Graphics 绘图对象
$psG = [System.Drawing.Graphics]::FromImage($psBmp)

# 打开抗锯齿：眼睛圆形边缘平滑
$psG.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

# 背景清成完全透明
$psG.Clear([System.Drawing.Color]::Transparent)

# 复用与 player.png 完全相同的配色：草绿身体、深绿描边、白色眼白、深灰瞳孔
$psBodyBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 76, 175, 80))
$psOutlinePen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 46, 125, 50), 3)
$psEyeBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
$psPupilBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 33, 33, 33))

# 逐帧绘制：第 f 帧在整张表中的横向起点 = f * 50
for ($f = 0; $f -lt $playerFrameCount; $f++) {
    # 全身纵向偏移：帧 1 为 0（与 player.png 完全相同），帧 2 为 2 像素（身体下压）
    $psOffsetY = $f * 2
    # 本帧横向起点（帧内所有绘制坐标都要加上它才落到正确帧上）
    $psBaseX = $f * $playerFrameSize

    # 身体左边界 x：帧起点 + 帧内 2 像素透明边距（与 player.png 相同）
    $psBodyLeft = $psBaseX + 2
    # 身体顶边 y：帧 1 为 2（与 player.png 相同），帧 2 下压 $psOffsetY 像素
    $psBodyTop = 2 + $psOffsetY
    # 身体高度：帧 1 为 46；帧 2 顶部下压多少、高度就减多少（底边恒在 y=48，脚底不悬空）
    $psBodyH = 46 - $psOffsetY

    # 注意：PS 5.1 的 New-Object 裸类型带括号语法不允许参数里内联 +/- 运算，
    # 否则会被误解析成成员访问并抛 op_Addition 错误，所以上面先把坐标算好再传入
    # 身体矩形：帧 1 为 (2,2,46,46)；帧 2 为 (2,4,46,44)
    $psBodyRect = New-Object System.Drawing.Rectangle($psBodyLeft, $psBodyTop, 46, $psBodyH)
    # 填充身体（草绿实心矩形）
    $psG.FillRectangle($psBodyBrush, $psBodyRect)
    # 描边：沿身体矩形画深绿边框，提升对比辨识度
    $psG.DrawRectangle($psOutlinePen, $psBodyRect)

    # 左眼白：圆心随身体一起下移 $psOffsetY 像素
    $psG.FillEllipse($psEyeBrush, $psBaseX + 10, 13 + $psOffsetY, 14, 14)
    # 右眼白：与左眼关于帧中线对称
    $psG.FillEllipse($psEyeBrush, $psBaseX + 26, 13 + $psOffsetY, 14, 14)
    # 左瞳孔：偏右下的小圆，像在向前看
    $psG.FillEllipse($psPupilBrush, $psBaseX + 15, 18 + $psOffsetY, 6, 6)
    # 右瞳孔：与左瞳孔完全对称
    $psG.FillEllipse($psPupilBrush, $psBaseX + 31, 18 + $psOffsetY, 6, 6)
}

# 保存玩家行走精灵表到 assets/player_sheet.png（内部先删旧文件，保证幂等）
Save-Png -bitmap $psBmp -path (Join-Path $assetsDir "player_sheet.png")

# 释放身体填充画刷
$psBodyBrush.Dispose()

# 释放描边画笔
$psOutlinePen.Dispose()

# 释放眼白画刷
$psEyeBrush.Dispose()

# 释放瞳孔画刷
$psPupilBrush.Dispose()

# 释放 Graphics 绘图对象
$psG.Dispose()

# 释放精灵表位图本身
$psBmp.Dispose()

# ============================================================
# 5) 背景图 background.png：1280x720 深蓝星空风（渐变 + 网格 + 星点）
# ============================================================
# 定位：铺满窗口的最底层画布。配色刻意低对比、低饱和，
#       保证玩家/金币/HUD 叠在背景之上依然清晰醒目
$bgWidth = 1280   # 背景宽，与游戏初始窗口宽一致（绘制时按窗口实际尺寸拉伸）
$bgHeight = 720   # 背景高，与游戏初始窗口高一致

# 创建背景画布（背景是不透明的，不需要透明通道）
$bgBmp = New-Canvas -width $bgWidth -height $bgHeight

# 获取背景的 Graphics 绘图对象
$bgG = [System.Drawing.Graphics]::FromImage($bgBmp)

# 1) 纵向渐变底色：顶部深藏蓝 -> 底部稍亮钢蓝，给画面一点"天幕"层次
#    外接矩形：铺满整张画布
$bgRect = New-Object System.Drawing.Rectangle(0, 0, $bgWidth, $bgHeight)

# 渐变顶部颜色（先算成变量，避免 New-Object 参数里内联复杂表达式）
$bgTopColor = [System.Drawing.Color]::FromArgb(255, 12, 18, 34)

# 渐变底部颜色：比顶部稍亮一档，形成自然过渡
$bgBottomColor = [System.Drawing.Color]::FromArgb(255, 24, 34, 58)

# 线性渐变画刷：角度 90 表示从上到下渐变
$bgGradient = New-Object System.Drawing.Drawing2D.LinearGradientBrush($bgRect, $bgTopColor, $bgBottomColor, 90)

# 用渐变铺满整张背景（不透明，直接覆盖）
$bgG.FillRectangle($bgGradient, $bgRect)

# 2) 细微网格：每 64 像素一条淡蓝细线（alpha 只有 28，凑近看才明显），
#    给空旷的深蓝底一点"坐标纸"的纹理感
$bgGridPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(28, 120, 160, 220), 1)

# 竖线族：x 从 0 到 1279，步长 64
for ($gx = 0; $gx -lt $bgWidth; $gx += 64) {
    # 在 x = gx 处从顶到底画一条 1 像素竖线
    $bgG.DrawLine($bgGridPen, $gx, 0, $gx, $bgHeight)
}

# 横线族：y 从 0 到 719，步长 64
for ($gy = 0; $gy -lt $bgHeight; $gy += 64) {
    # 在 y = gy 处从左到右画一条 1 像素横线
    $bgG.DrawLine($bgGridPen, 0, $gy, $bgWidth, $gy)
}

# 3) 星点：固定种子的伪随机数撒点。
#    固定种子是幂等的关键——每次运行都得到完全相同的位置序列
$bgRng = New-Object System.Random(20261012)

# 星点画刷：半透明淡蓝白（alpha 85），淡淡的、不抢戏
$bgStarBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(85, 200, 220, 255))

# 一共撒 160 颗小星
for ($i = 0; $i -lt 160; $i++) {
    # 星点左上角横坐标：取 [0, 1280) 的随机整数
    $starX = $bgRng.Next(0, $bgWidth)
    # 星点左上角纵坐标：取 [0, 720) 的随机整数
    $starY = $bgRng.Next(0, $bgHeight)
    # 星点直径：随机取 1 或 2 像素，小到不会喧宾夺主
    $starD = $bgRng.Next(1, 3)
    # 画一颗圆形小星（1~2 像素的点用圆形画刷边缘更柔和）
    $bgG.FillEllipse($bgStarBrush, $starX, $starY, $starD, $starD)
}

# 保存背景图到 assets/background.png（内部先删旧文件，保证幂等）
Save-Png -bitmap $bgBmp -path (Join-Path $assetsDir "background.png")

# 释放渐变画刷
$bgGradient.Dispose()

# 释放网格画笔
$bgGridPen.Dispose()

# 释放星点画刷
$bgStarBrush.Dispose()

# 释放 Graphics 绘图对象
$bgG.Dispose()

# 释放背景位图本身
$bgBmp.Dispose()

# ============================================================
# 6) 生成后自检：重新读盘校验全部 PNG 的尺寸与逻辑一致
# ============================================================
# 校验玩家精灵必须为 50x50（与 Player 的 m_width/m_height 对应）
Assert-PngSize -path (Join-Path $assetsDir "player.png") -expectedWidth 50 -expectedHeight 50

# 校验金币精灵必须为 24x24（与 Coin 的 kSize 对应）
Assert-PngSize -path (Join-Path $assetsDir "coin.png") -expectedWidth 24 -expectedHeight 24

# 校验金币旋转精灵表必须为 144x24（6 帧 x 每帧 24 宽，与 Coin 的 kFrameCount/kSize 对应）
Assert-PngSize -path (Join-Path $assetsDir "coin_sheet.png") -expectedWidth 144 -expectedHeight 24

# 校验玩家行走精灵表必须为 100x50（2 帧 x 每帧 50 宽，与 Player 的 kFrameCount 对应）
Assert-PngSize -path (Join-Path $assetsDir "player_sheet.png") -expectedWidth 100 -expectedHeight 50

# 校验背景图必须为 1280x720（与窗口初始尺寸一致；绘制时按窗口实际尺寸拉伸）
Assert-PngSize -path (Join-Path $assetsDir "background.png") -expectedWidth 1280 -expectedHeight 720

# 全部完成提示
Write-Host "全部素材生成并校验完成。"
