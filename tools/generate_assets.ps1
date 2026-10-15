# ============================================================
# GameDev 音效生成脚本（tools/generate_assets.ps1）
# ============================================================
# 作用：用 Windows PowerShell 5.1 自带（无需安装）的能力，程序化合成游戏
#       音效并保存为 WAV（16-bit PCM 单声道 44.1kHz）：
#         assets/sounds/coin.wav      收币「叮」
#         assets/sounds/start.wav     开始上扬音
#         assets/sounds/gameover.wav  结束下行音
#         assets/sounds/pause.wav     暂停轻提示
#         assets/sounds/hit.wav       受击闷响
#         assets/sounds/record.wav    破纪录上行琶音
#
# 【职责调整】图像素材（角色/怪物/宝石/武器/背景）已移交
#   tools/compose_sprites.ps1——由它从 third_party 原始像素帧组装成品；
#   本脚本只负责声音，不再生成任何 PNG。
#
# 设计原则：
#   1. 不依赖任何第三方工具或素材文件，开箱即可重新生成音效
#   2. 幂等：可以重复运行，每次覆盖旧文件，输出结果一致
#   3. 音效时长由「段表」显式定义（每段 StartHz/EndHz/Seconds），接近合成器
#      的写法——想调音色改段表即可，不必理解底部采样循环细节
#
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/generate_assets.ps1
# ============================================================

# 遇到任何错误立即终止，避免"看起来成功、实际没生成"的假象
$ErrorActionPreference = "Stop"

# $PSScriptRoot 是脚本所在目录（tools/）；仓库根目录是它的上一级
$repoRoot = Split-Path -Parent $PSScriptRoot

# 素材输出目录：<仓库根>/assets
$assetsDir = Join-Path $repoRoot "assets"

# 音效输出目录：<仓库根>/assets/sounds
$soundsDir = Join-Path $assetsDir "sounds"

# 目录不存在则先创建（| Out-Null 丢弃 New-Item 的返回值，保持输出干净）
if (-not (Test-Path $soundsDir)) {
    # -ItemType Directory 明确创建目录；-Force 保证中间层级不存在时也能建好
    New-Item -ItemType Directory -Path $soundsDir -Force | Out-Null
}

# 统一采样率：44100Hz 是通用音效标准速率（与代码侧期望一致）
$wavSampleRate = 44100

# ------------------------------------------------------------
# 工具函数：合成一段（或多段）带包络的正弦波，返回 [-1,1] 的 double 样本数组
# 参数：
#   segments   段列表，每段为 @{ StartHz; EndHz; Seconds }；
#              段内频率从 StartHz 线性滑到 EndHz，段间相位连续
#   amplitude  峰值振幅（0~1），1.0 即 int16 满量程
#   decayTime  指数衰减时间常数（秒）：越小衰减越快（"叮"类音效取小值）
#   fadeIn     起音淡入时长（秒），防爆音
#   fadeOut    收尾淡出时长（秒），防截断爆音
# ------------------------------------------------------------
function New-SineSamples {
    param(
        [Parameter(Mandatory = $true)][array]$segments,
        [double]$amplitude = 0.85,
        [double]$decayTime = 0.08,
        [double]$fadeIn = 0.002,
        [double]$fadeOut = 0.005
    )

    # 总时长（秒）= 各段秒数之和
    $totalSeconds = 0.0
    foreach ($seg in $segments) {
        $totalSeconds += $seg.Seconds
    }

    # 总样本数 = 总时长 x 采样率，四舍五入到整数样本
    $totalSamples = [int][Math]::Round($totalSeconds * $wavSampleRate)

    # 预分配样本数组（比逐项追加快得多，PowerShell 的 += 会反复复制数组）
    $samples = New-Object 'double[]' $totalSamples

    # 相位累加器（弧度）：跨段保持连续，是防止咔哒声的关键
    $phase = 0.0

    # 样本写入下标（跨段持续递增）
    $writeIndex = 0

    # 当前样本对应的全局时间（秒），用于包络计算
    $t = 0.0

    # 单样本时间步长（秒）
    $dt = 1.0 / $wavSampleRate

    foreach ($seg in $segments) {
        # 本段样本数
        $segSamples = [int][Math]::Round($seg.Seconds * $wavSampleRate)

        for ($i = 0; $i -lt $segSamples; $i++) {
            # 段内进度 [0,1)：用于频率线性滑动定位
            $progress = $i / [double]$segSamples

            # 段内瞬时频率（Hz）：StartHz -> EndHz 线性插值
            $freq = $seg.StartHz + ($seg.EndHz - $seg.StartHz) * $progress

            # 相位累加：每样本前进 2π·f·dt 弧度
            $phase += 2.0 * [Math]::PI * $freq * $dt

            # 相位回绕到 [0,2π)：防止长时间累加后浮点精度丢失
            while ($phase -ge 2.0 * [Math]::PI) {
                $phase -= 2.0 * [Math]::PI
            }

            # 包络①：起音淡入——前 fadeIn 秒从 0 线性升到 1
            $envIn = 1.0
            if ($t -lt $fadeIn) {
                $envIn = $t / $fadeIn
            }

            # 包络②：指数衰减——t=0 为 1，之后按 decayTime 时间常数衰减
            $envDecay = [Math]::Exp(-$t / $decayTime)

            # 包络③：收尾淡出——最后 fadeOut 秒从 1 线性降到 0
            $envOut = 1.0
            $remain = $totalSeconds - $t
            if ($remain -lt $fadeOut) {
                $envOut = $remain / $fadeOut
            }

            # 合成：正弦 x 三重包络 x 振幅，写入预分配数组
            $samples[$writeIndex] = [Math]::Sin($phase) * $envIn * $envDecay * $envOut * $amplitude

            # 样本写入位置前进一格
            $writeIndex++

            # 全局时间前进一个采样间隔
            $t += $dt
        }
    }

    # return 前加逗号包一层：阻止 PowerShell 把数组展开成多个返回值
    return ,$samples
}

# ------------------------------------------------------------
# 工具函数：把 [-1,1] 的 double 样本写成 16-bit PCM 单声道 WAV 文件
# 参数：samples 样本数组；path 输出路径；sampleRate 采样率
# 幂等：旧文件先删除再写（与素材生成脚本的既有约定）
# ------------------------------------------------------------
function Save-Wav16Mono {
    param(
        [Parameter(Mandatory = $true)][double[]]$samples,
        [Parameter(Mandatory = $true)][string]$path,
        [int]$sampleRate = 44100
    )

    # 若旧文件已存在，先删除，保证重复运行时一定写入全新内容（幂等）
    if (Test-Path $path) {
        Remove-Item -Path $path -Force
    }

    # data 块大小（字节）= 样本数 x 每样本 2 字节（16-bit PCM）
    $dataSize = $samples.Length * 2

    # RIFF 大小字段 = 文件总长 - 8：
    # 4("WAVE") + 8(fmt 头) + 16(fmt 体) + 8(data 头) + dataSize = 36 + dataSize
    $riffSize = 36 + $dataSize

    # 打开文件流（Create：不存在则建、存在则清空重写）
    $stream = [System.IO.File]::Open($path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)

    # BinaryWriter 默认小端序，正好符合 RIFF 规范的要求
    $writer = New-Object System.IO.BinaryWriter($stream)

    try {
        # ---- RIFF 头 ----
        # 容器魔数 "RIFF"（ASCII 字节写入，不能用 Write(string)——那会带长度前缀）
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))

        # RIFF 大小字段：其后所有内容的总字节数（即文件大小 - 8）
        $writer.Write([int]$riffSize)

        # 格式类型 "WAVE"
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("WAVE"))

        # ---- fmt 块：描述 PCM 格式 ----
        # 块标识 "fmt "（注意末尾是空格，RIFF 规范如此）
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("fmt "))

        # 块长：标准 PCM 固定 16 字节
        $writer.Write([int]16)

        # 编码：1 = 线性 PCM（未压缩）
        $writer.Write([int16]1)

        # 声道数：1 = 单声道
        $writer.Write([int16]1)

        # 采样率（Hz）
        $writer.Write([int]$sampleRate)

        # 字节率 = 采样率 x 声道数 x 每样本字节数（播放器用它快速算时长）
        $writer.Write([int]($sampleRate * 1 * 2))

        # 块对齐 = 声道数 x 每样本字节数（每个采样帧的字节数）
        $writer.Write([int16]2)

        # 位深：每样本 16 位
        $writer.Write([int16]16)

        # ---- data 块：PCM 样本流 ----
        # 块标识 "data"
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("data"))

        # 样本数据字节数
        $writer.Write([int]$dataSize)

        # 逐样本量化写盘：double [-1,1] -> int16
        foreach ($s in $samples) {
            # 先钳制到 [-1,1]（浮点运算可能产生 ±1.0000001 这类微小越界）
            $clamped = [Math]::Max(-1.0, [Math]::Min(1.0, $s))

            # x32767 后四舍五入：映射到 16 位有符号整数范围
            $writer.Write([int16][Math]::Round($clamped * 32767.0))
        }
    } finally {
        # 无论成败都释放：Dispose 会 flush 缓冲并关闭底层文件句柄
        $writer.Dispose()
        $stream.Dispose()
    }

    # 打印生成结果，方便使用者核对路径
    Write-Host "已生成: $path"
}

# ------------------------------------------------------------
# 工具函数：校验 WAV 文件头与时长
# 参数：path 文件路径；expectedSeconds 预期时长（秒）；tolerance 允许误差（秒）
# 校验项：RIFF/WAVE 魔数、fmt 块（PCM/单声道/16bit/采样率）、
#         data 块与文件长度自洽、时长 = 数据字节数 / 字节率
# ------------------------------------------------------------
function Assert-Wav16Mono {
    param(
        [Parameter(Mandatory = $true)][string]$path,
        [Parameter(Mandatory = $true)][double]$expectedSeconds,
        [int]$sampleRate = 44100,
        [double]$tolerance = 0.005
    )

    # 重新读盘：拿到真实落盘字节（而不是内存里的中间结果）
    $bytes = [System.IO.File]::ReadAllBytes($path)

    # 文件至少要有一个 44 字节的标准头才可能合法
    if ($bytes.Length -lt 44) {
        throw "WAV 过短: $path（只有 $($bytes.Length) 字节）"
    }

    # ---- 1) RIFF 容器魔数 ----
    # 偏移 0~3 应为 "RIFF"
    $riff = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4)
    # 偏移 8~11 应为 "WAVE"
    $wave = [System.Text.Encoding]::ASCII.GetString($bytes, 8, 4)
    if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
        throw "WAV 魔数不符: $path（读到 '$riff'/'$wave'）"
    }

    # ---- 2) RIFF 大小字段与真实文件长度自洽 ----
    # 偏移 4 的大小字段 = 文件大小 - 8
    $riffSize = [BitConverter]::ToInt32($bytes, 4)
    if ($riffSize -ne ($bytes.Length - 8)) {
        throw "RIFF 大小字段不符: $path（字段 $riffSize，实际 $($bytes.Length - 8)）"
    }

    # ---- 3) fmt 块：PCM / 单声道 / 16bit / 采样率 ----
    # 偏移 12~15 应为 "fmt "
    $fmtId = [System.Text.Encoding]::ASCII.GetString($bytes, 12, 4)
    if ($fmtId -ne "fmt ") {
        throw "fmt 块标识不符: $path"
    }
    # 偏移 20：编码（1=PCM）
    $audioFormat = [BitConverter]::ToUInt16($bytes, 20)
    # 偏移 22：声道数
    $channels = [BitConverter]::ToUInt16($bytes, 22)
    # 偏移 24：采样率
    $sr = [BitConverter]::ToInt32($bytes, 24)
    # 偏移 34：位深
    $bits = [BitConverter]::ToUInt16($bytes, 34)
    if ($audioFormat -ne 1) { throw "不是 PCM 编码: $path（编码 $audioFormat）" }
    if ($channels -ne 1) { throw "不是单声道: $path（$channels 声道）" }
    if ($sr -ne $sampleRate) { throw "采样率不符: $path（$sr，预期 $sampleRate）" }
    if ($bits -ne 16) { throw "位深不符: $path（$bits，预期 16）" }

    # ---- 4) data 块与文件长度自洽 ----
    # 偏移 36~39 应为 "data"（本脚本只写最简 44 字节头，data 紧随 fmt）
    $dataId = [System.Text.Encoding]::ASCII.GetString($bytes, 36, 4)
    if ($dataId -ne "data") {
        throw "data 块标识不符: $path"
    }
    # 偏移 40：样本数据字节数
    $dataSize = [BitConverter]::ToInt32($bytes, 40)
    if ((44 + $dataSize) -ne $bytes.Length) {
        throw "data 大小与文件长度不符: $path"
    }

    # ---- 5) 时长 = data 字节数 / 字节率（单声道 16bit：字节率 = 采样率 x 2）----
    $seconds = $dataSize / [double]($sampleRate * 2)
    if ([Math]::Abs($seconds - $expectedSeconds) -gt $tolerance) {
        throw "时长不符: $path（实际 $([Math]::Round($seconds, 4))s，预期 $expectedSeconds s）"
    }

    # 全部通过：打印校验结果（含格式与实测时长）
    Write-Host "WAV 校验通过: $path（PCM/单声道/16bit/${sampleRate}Hz，时长 $([Math]::Round($seconds, 3))s）"
}

# ---- 1) 收币音 coin.wav：「叮」----
# 双段正弦：880Hz 一段 -> 1320Hz 一段（频率升高让"叮"更亮），约 100ms；
# decayTime 取 0.035s 快速衰减，模拟金属币短促碰响的余韵
$coinSamples = New-SineSamples -segments @(
    @{ StartHz = 880.0;  EndHz = 880.0;  Seconds = 0.04 },
    @{ StartHz = 1320.0; EndHz = 1320.0; Seconds = 0.06 }
) -amplitude 0.85 -decayTime 0.035
# 写盘：16-bit PCM 单声道 44.1kHz
Save-Wav16Mono -samples $coinSamples -path (Join-Path $soundsDir "coin.wav") -sampleRate $wavSampleRate

# ---- 2) 开始音 start.wav：短促上扬 ----
# 单一线性滑音 600Hz -> 900Hz（上行约六度），约 120ms；
# 衰减放慢（0.25s）保持"确认/出发"的明亮听感
$startSamples = New-SineSamples -segments @(
    @{ StartHz = 600.0; EndHz = 900.0; Seconds = 0.12 }
) -amplitude 0.8 -decayTime 0.25
Save-Wav16Mono -samples $startSamples -path (Join-Path $soundsDir "start.wav") -sampleRate $wavSampleRate

# ---- 3) 结束音 gameover.wav：三连下行 ----
# 440Hz -> 330Hz -> 220Hz 三段，每段 0.2s，共 0.6s；
# 音高逐级下降传达"结束"；衰减放慢（0.5s）让三个音保持清晰可辨
$gameoverSamples = New-SineSamples -segments @(
    @{ StartHz = 440.0; EndHz = 440.0; Seconds = 0.2 },
    @{ StartHz = 330.0; EndHz = 330.0; Seconds = 0.2 },
    @{ StartHz = 220.0; EndHz = 220.0; Seconds = 0.2 }
) -amplitude 0.8 -decayTime 0.5
Save-Wav16Mono -samples $gameoverSamples -path (Join-Path $soundsDir "gameover.wav") -sampleRate $wavSampleRate

# ---- 4) 暂停音 pause.wav：轻短提示 ----
# 660Hz 恒频约 80ms；振幅压低到 0.55——暂停/恢复是轻动作，音效不应抢戏
$pauseSamples = New-SineSamples -segments @(
    @{ StartHz = 660.0; EndHz = 660.0; Seconds = 0.08 }
) -amplitude 0.55 -decayTime 0.05
Save-Wav16Mono -samples $pauseSamples -path (Join-Path $soundsDir "pause.wav") -sampleRate $wavSampleRate

# ---- 5) 命中音 hit.wav：低频下行「闷响」----
# 160Hz -> 70Hz 单段滑音约 150ms：低频下坠模拟被撞的闷响；
# 振幅拉高（0.9）突出冲击感，搭配较快的衰减（0.06s）保持"短促一下"的打击听感
$hitSamples = New-SineSamples -segments @(
    @{ StartHz = 160.0; EndHz = 70.0; Seconds = 0.15 }
) -amplitude 0.9 -decayTime 0.06
Save-Wav16Mono -samples $hitSamples -path (Join-Path $soundsDir "hit.wav") -sampleRate $wavSampleRate

# ---- 6) 破纪录音 record.wav：上行三连音琶音 ----
# C5(523.25Hz) -> E5(659.25Hz) -> G5(783.99Hz) 大三和弦琶音，每段 0.1s、
# 共约 0.3s：音高逐级上行，与结束音 gameover.wav 的三连下行形成鲜明对比，
# 传递"达成新纪录"的明亮情绪；衰减放慢（0.15s）让三个音都清晰可辨
$recordSamples = New-SineSamples -segments @(
    @{ StartHz = 523.25; EndHz = 523.25; Seconds = 0.1 },
    @{ StartHz = 659.25; EndHz = 659.25; Seconds = 0.1 },
    @{ StartHz = 783.99; EndHz = 783.99; Seconds = 0.1 }
) -amplitude 0.8 -decayTime 0.15
Save-Wav16Mono -samples $recordSamples -path (Join-Path $soundsDir "record.wav") -sampleRate $wavSampleRate

# ============================================================
# 生成后自检：重新读盘校验全部 WAV 的头与时长
# ============================================================
# 校验收币音：约 100ms（两段共 4410 个样本，恰好 0.1s）
Assert-Wav16Mono -path (Join-Path $soundsDir "coin.wav") -expectedSeconds 0.1

# 校验开始音：约 120ms（5292 个样本，恰好 0.12s）
Assert-Wav16Mono -path (Join-Path $soundsDir "start.wav") -expectedSeconds 0.12

# 校验结束音：约 600ms（26460 个样本，恰好 0.6s）
Assert-Wav16Mono -path (Join-Path $soundsDir "gameover.wav") -expectedSeconds 0.6

# 校验暂停音：约 80ms（3528 个样本，恰好 0.08s）
Assert-Wav16Mono -path (Join-Path $soundsDir "pause.wav") -expectedSeconds 0.08

# 校验命中音：约 150ms（6615 个样本，恰好 0.15s）
Assert-Wav16Mono -path (Join-Path $soundsDir "hit.wav") -expectedSeconds 0.15

# 校验破纪录音：约 300ms（三段各 4410 个样本，合计恰好 0.3s）
Assert-Wav16Mono -path (Join-Path $soundsDir "record.wav") -expectedSeconds 0.3

# 全部完成提示
Write-Host "全部音效生成并校验完成。"
