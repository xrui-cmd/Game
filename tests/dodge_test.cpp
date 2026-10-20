// ============================================================================
//  dodge_test.cpp — 角色闪避（冲刺）纯逻辑单元测试（M3·丙）
//
//  被测对象：Src/inc/game/Dodge.h（header-only，不依赖 SDL / 引擎运行时）
//    - 最终参数：时长 0.18s / 距离 120px / 冷却 1.2s；
//    - 位移曲线：二次缓出（快起慢收），p=0 位移 0、p=1 恰为 120px，
//      不同固定步长（120/60/144Hz）离散积分都精确收敛到 120px；
//    - 方向选择：WASD 移动输入优先（归一化）、无输入取鼠标方向、
//      双重退化沿用朝向、全部退化朝右（绝不 NaN）；
//    - 禁触发条件：非 Playing / 冻结 / 冲刺中 / 冷却中及其优先级；
//    - 冷却推进：递减到 0 截断、负步长防御、1.2s 后恢复。
//
//  退出码：main 返回失败断言数（全部通过 = 0）。
// ============================================================================

#include "game/Dodge.h" // 被测对象（DodgeRules：常量 / 方向 / 曲线 / 冷却 / 门控）

#include <cmath>   // std::fabs / std::sqrt：浮点近似与方向长度断言
#include <cstdio>  // std::printf：逐条打印断言结果
#include <cstdlib> // std::system：Windows 控制台切 UTF-8

namespace
{

int gPass = 0; // 通过的断言数
int gFail = 0; // 失败的断言数（作为 main 返回值）

// 布尔断言：条件成立才计入通过
void expectTrue(const char* label, bool value)
{
    const bool ok = value;                // 断言条件
    if (ok) { ++gPass; } else { ++gFail; } // 计数
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label); // 输出一行
}

// 浮点近似断言：|got - expect| <= eps
void expectNear(const char* label, float got, float expect, float eps = 0.001f)
{
    const bool ok = std::fabs(got - expect) <= eps; // 偏差在容差内
    if (ok) { ++gPass; } else { ++gFail; }           // 计数
    std::printf("  [%s] %s | 期望≈%.6f 实际=%.6f\n",
                ok ? "PASS" : "FAIL", label, expect, got); // 输出期望/实际
}

// 枚举断言：把 DodgeStartBlock 映射成字符串后比较（失败时输出可读原因）
void expectBlock(const char* label, DodgeRules::DodgeStartBlock got,
                 DodgeRules::DodgeStartBlock expect)
{
    const bool ok = (got == expect); // 枚举相等
    if (ok) { ++gPass; } else { ++gFail; } // 计数
    std::printf("  [%s] %s | 期望=%d 实际=%d\n",
                ok ? "PASS" : "FAIL", label,
                static_cast<int>(expect), static_cast<int>(got)); // 输出枚举数值
}

// 用例开始分隔（与项目其余测试同款输出格式）
void beginCase(int idx, const char* title)
{
    std::printf("\n[CASE %d] %s\n", idx, title); // 用例标题
}

// 按固定步长模拟一次冲刺：返回累计位移（调用 distanceAtProgress 差分，
// 与 Player::update 的积分方式完全一致）
float simulateDashDistance(float stepSeconds)
{
    float elapsed = 0.0f;      // 已进行时长（秒）
    float travelled = 0.0f;    // 累计位移（像素）
    float previous = 0.0f;     // 上一步的累计曲线值
    while (true)
    {
        elapsed += stepSeconds; // 推进一个固定步
        const float p = DodgeRules::progressAtElapsed(elapsed); // 本步进度（到点夹 1）
        const float current = DodgeRules::distanceAtProgress(p); // 本步累计曲线值
        travelled += current - previous; // 本步位移 = 曲线差分
        previous = current;              // 保存供下一步差分
        if (p >= 1.0f)                   // 到达终点：停止
        {
            break; // 退出模拟
        }
    }
    return travelled; // 总位移（像素）
}

} // namespace

int main()
{
#if defined(_WIN32)
    std::system("chcp 65001 > nul 2>&1"); // 控制台切 UTF-8：中文输出不乱码
#endif

    std::printf("==============================================================\n"); // 横幅上边线
    std::printf(" 角色闪避（冲刺）纯逻辑单元测试（M3·丙）\n"); // 标题
    std::printf(" 参数 / 位移曲线 / 方向选择 / 禁触发条件 / 冷却\n"); // 覆盖范围
    std::printf("==============================================================\n"); // 横幅下边线

    // ------------------------------------------------------------------
    // CASE 1 冲刺最终参数
    // ------------------------------------------------------------------
    beginCase(1, "冲刺最终参数"); // 用例 1：最终参数
    {
        expectNear("冲刺时长 = 0.18s", DodgeRules::kDurationSeconds, 0.18f); // 规格时长
        expectNear("冲刺距离 = 120px", DodgeRules::kDistancePixels, 120.0f); // 规格距离
        expectNear("冲刺冷却 = 1.2s", DodgeRules::kCooldownSeconds, 1.2f);   // 规格冷却

        // 初速 = 曲线在 p=0 的导数 = 2D/T ≈ 1333.33 px/s（快起的量化口径）
        const float initialSpeed = 2.0f * DodgeRules::kDistancePixels /
                                   DodgeRules::kDurationSeconds; // 理论初速
        expectNear("二次缓出初速 ≈ 1333.33 px/s", initialSpeed, 1333.333f, 0.01f); // 快起

        // 末速 = 2D(1-1)/T = 0：慢收（终点自然停住，不硬截断）
        const float endSpeed = 2.0f * DodgeRules::kDistancePixels *
                               (1.0f - 1.0f) / DodgeRules::kDurationSeconds; // 理论末速
        expectNear("二次缓出末速 = 0", endSpeed, 0.0f); // 慢收
    }

    // ------------------------------------------------------------------
    // CASE 2 方向选择（移动输入优先 / 无输入朝鼠标 / 退化兜底）
    // ------------------------------------------------------------------
    beginCase(2, "方向选择"); // 用例 2：方向选择
    {
        const DodgeRules::DodgeDirection right =
            DodgeRules::selectDirection(1.0f, 0.0f, 0.0f, 100.0f, 1.0f, 0.0f); // 右移优先于鼠标
        expectNear("右移：x = 1", right.x, 1.0f); // 单位方向 x
        expectNear("右移：y = 0", right.y, 0.0f); // 单位方向 y

        const DodgeRules::DodgeDirection diagonal =
            DodgeRules::selectDirection(1.0f, 1.0f, 0.0f, -100.0f, 1.0f, 0.0f); // 斜向移动
        expectNear("斜向：x = 1/√2", diagonal.x, 0.70710678f); // 归一化 x
        expectNear("斜向：y = 1/√2", diagonal.y, 0.70710678f); // 归一化 y
        expectNear("斜向合成长度 = 1",
                   DodgeRules::vectorLength(diagonal.x, diagonal.y), 1.0f); // 不再 1.41 倍

        const DodgeRules::DodgeDirection up =
            DodgeRules::selectDirection(0.0f, -1.0f, 100.0f, 0.0f, 1.0f, 0.0f); // 上移优先
        expectNear("上移：x = 0", up.x, 0.0f); // 无水平分量
        expectNear("上移：y = -1", up.y, -1.0f); // 屏幕 y 向上为负

        const DodgeRules::DodgeDirection leftUp =
            DodgeRules::selectDirection(-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f); // 左下合成
        expectNear("左下：x = -1/√2", leftUp.x, -0.70710678f); // 归一化 x
        expectNear("左下：y = -1/√2", leftUp.y, -0.70710678f); // 归一化 y

        const DodgeRules::DodgeDirection aimRight =
            DodgeRules::selectDirection(0.0f, 0.0f, 25.0f, 0.0f, -1.0f, 0.0f); // 无移动：朝鼠标
        expectNear("无移动：朝鼠标 x = 1", aimRight.x, 1.0f); // 鼠标方向优先于朝向
        expectNear("无移动：朝鼠标 y = 0", aimRight.y, 0.0f); // 水平方向

        const DodgeRules::DodgeDirection aimDown =
            DodgeRules::selectDirection(0.0f, 0.0f, 0.0f, 50.0f, 1.0f, 0.0f); // 鼠标在下方
        expectNear("鼠标下方：x = 0", aimDown.x, 0.0f); // 无水平分量
        expectNear("鼠标下方：y = 1", aimDown.y, 1.0f); // 朝下（屏幕 y 正方向）

        const DodgeRules::DodgeDirection aim3to4 =
            DodgeRules::selectDirection(0.0f, 0.0f, 3.0f, 4.0f, 1.0f, 0.0f); // 3-4-5 向量
        expectNear("瞄准 3-4-5：x = 0.6", aim3to4.x, 0.6f); // 归一化 x
        expectNear("瞄准 3-4-5：y = 0.8", aim3to4.y, 0.8f); // 归一化 y

        const DodgeRules::DodgeDirection facing =
            DodgeRules::selectDirection(0.0f, 0.0f, 0.0f, 0.0f, -2.0f, 0.0f); // 双重退化：朝向
        expectNear("双重退化：沿用朝向 x = -1", facing.x, -1.0f); // 朝左
        expectNear("双重退化：沿用朝向 y = 0", facing.y, 0.0f); // 水平

        const DodgeRules::DodgeDirection fallback =
            DodgeRules::selectDirection(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f); // 全部退化
        expectNear("全部退化：x = 1（确定性朝右）", fallback.x, 1.0f); // 兜底 x
        expectNear("全部退化：y = 0", fallback.y, 0.0f); // 兜底 y

        const DodgeRules::DodgeDirection tiny =
            DodgeRules::selectDirection(0.0001f, 0.0f, 0.0f, 10.0f, 1.0f, 0.0f); // 近零移动输入
        expectNear("近零输入按无输入处理：x = 0", tiny.x, 0.0f); // 移动输入被忽略
        expectNear("近零输入回退鼠标：y = 1", tiny.y, 1.0f); // 鼠标方向生效

        const DodgeRules::DodgeDirection nanFallback =
            DodgeRules::normalizeOrFallback(0.0f, 0.0f, 0.0f, 0.0f); // 零向量归一化
        expectNear("零向量兜底朝右：x = 1", nanFallback.x, 1.0f); // 不产生 NaN
        expectNear("零向量兜底朝右：y = 0", nanFallback.y, 0.0f); // 水平
    }

    // ------------------------------------------------------------------
    // CASE 3 位移曲线与总距离（不同固定步长）
    // ------------------------------------------------------------------
    beginCase(3, "位移曲线与总距离"); // 用例 3：位移曲线与总距离
    {
        expectNear("进度 0：位移 = 0", DodgeRules::distanceAtProgress(0.0f), 0.0f); // 起点
        expectNear("进度 0.5：位移 = 90（二次缓出）",
                   DodgeRules::distanceAtProgress(0.5f), 90.0f); // 1-(1-0.5)^2
        expectNear("进度 1：位移 = 120", DodgeRules::distanceAtProgress(1.0f), 120.0f); // 终点
        expectNear("进度 <0 夹到 0", DodgeRules::distanceAtProgress(-1.0f), 0.0f); // 下界
        expectNear("进度 >1 夹到 120", DodgeRules::distanceAtProgress(2.0f), 120.0f); // 上界

        // 单调递增：每个采样点都严格大于前一个（快起慢收但不回头）
        float previousDistance = 0.0f; // 前一采样位移
        bool monotonic = true;         // 单调标记
        for (int i = 1; i <= 20; ++i)  // 0.05 步长采样 20 次
        {
            const float now = DodgeRules::distanceAtProgress(i * 0.05f); // 当前采样
            if (!(now > previousDistance)) // 未严格递增即失败
            {
                monotonic = false; // 记录失败
            }
            previousDistance = now; // 更新前一采样
        }
        expectTrue("位移曲线单调递增", monotonic); // 不会回退

        // 前段位移占比 > 进度（快起）：p=0.25 已走 43.75% x 120 = 52.5px
        expectNear("进度 0.25 位移 = 52.5（前段更快）",
                   DodgeRules::distanceAtProgress(0.25f), 52.5f); // 快起量化
        // 后段位移占比小（慢收）：p=0.75 已走 93.75% x 120 = 112.5px，
        // 最后 25% 进度只再走 7.5px
        expectNear("进度 0.75 位移 = 112.5（后段更慢）",
                   DodgeRules::distanceAtProgress(0.75f), 112.5f); // 慢收量化

        // 不同固定步长积分：120Hz / 60Hz / 144Hz 都应精确得到 120px
        expectNear("120Hz 模拟总位移 = 120px", simulateDashDistance(1.0f / 120.0f), 120.0f); // 默认步长
        expectNear("60Hz 模拟总位移 = 120px", simulateDashDistance(1.0f / 60.0f), 120.0f); // 粗步长
        expectNear("144Hz 模拟总位移 = 120px", simulateDashDistance(1.0f / 144.0f), 120.0f); // 细步长
    }

    // ------------------------------------------------------------------
    // CASE 4 进度与冷却推进
    // ------------------------------------------------------------------
    beginCase(4, "进度与冷却"); // 用例 4：进度与冷却
    {
        expectNear("时长 0：进度 = 0", DodgeRules::progressAtElapsed(0.0f), 0.0f); // 起点
        expectNear("时长 0.09：进度 = 0.5", DodgeRules::progressAtElapsed(0.09f), 0.5f); // 中点
        expectNear("时长 0.18：进度 = 1", DodgeRules::progressAtElapsed(0.18f), 1.0f); // 终点
        expectNear("时长 0.30：进度夹到 1", DodgeRules::progressAtElapsed(0.30f), 1.0f); // 超时
        expectNear("负时长：进度夹到 0", DodgeRules::progressAtElapsed(-0.5f), 0.0f); // 防御

        expectNear("冷却 1.2 推进 0.5 = 0.7",
                   DodgeRules::cooldownAfter(1.2f, 0.5f), 0.7f); // 常规递减
        expectNear("冷却推进到 0 截断",
                   DodgeRules::cooldownAfter(0.3f, 0.5f), 0.0f); // 不出现负冷却
        expectNear("负步长不增加冷却",
                   DodgeRules::cooldownAfter(0.4f, -1.0f), 0.4f); // 防御负 dt

        // 连续 144 个 1/120 步（恰好 1.2s）：冷却归零、可再次冲刺
        float cooldown = DodgeRules::kCooldownSeconds; // 起始冷却
        for (int i = 0; i < 144; ++i)                  // 模拟 1.2 秒
        {
            cooldown = DodgeRules::cooldownAfter(cooldown, 1.0f / 120.0f); // 逐步递减
        }
        expectNear("1.2s 后冷却 = 0", cooldown, 0.0f); // 冷却恢复
    }

    // ------------------------------------------------------------------
    // CASE 5 禁触发条件与优先级
    // ------------------------------------------------------------------
    beginCase(5, "禁触发条件与优先级"); // 用例 5：禁触发条件与优先级
    {
        expectBlock("全条件满足：允许",
                    DodgeRules::dodgeStartBlock(true, false, false, 0.0f),
                    DodgeRules::DodgeStartBlock::None); // 可冲刺
        expectTrue("canStartDodge 全满足 = true",
                   DodgeRules::canStartDodge(true, false, false, 0.0f)); // 布尔入口

        expectBlock("非 Playing：禁止",
                    DodgeRules::dodgeStartBlock(false, false, false, 0.0f),
                    DodgeRules::DodgeStartBlock::NotPlaying); // 面板/暂停/过场
        expectBlock("冻结中：禁止",
                    DodgeRules::dodgeStartBlock(true, true, false, 0.0f),
                    DodgeRules::DodgeStartBlock::Frozen); // 冻结锁定
        expectBlock("冲刺中：禁止再次触发",
                    DodgeRules::dodgeStartBlock(true, false, true, 0.0f),
                    DodgeRules::DodgeStartBlock::Dashing); // 同一次冲刺
        expectBlock("冷却中：禁止",
                    DodgeRules::dodgeStartBlock(true, false, false, 0.001f),
                    DodgeRules::DodgeStartBlock::Cooldown); // 冷却剩余 > 0
        expectBlock("冷却恰好 0：允许",
                    DodgeRules::dodgeStartBlock(true, false, false, 0.0f),
                    DodgeRules::DodgeStartBlock::None); // 边界允许

        // 优先级：状态 > 冻结 > 冲刺中 > 冷却（日志取第一个不满足项）
        expectBlock("优先级：非 Playing 压过冻结/冷却",
                    DodgeRules::dodgeStartBlock(false, true, true, 1.0f),
                    DodgeRules::DodgeStartBlock::NotPlaying); // 状态最优先
        expectBlock("优先级：冻结压过冲刺/冷却",
                    DodgeRules::dodgeStartBlock(true, true, true, 1.0f),
                    DodgeRules::DodgeStartBlock::Frozen); // 冻结第二
        expectBlock("优先级：冲刺中压过冷却",
                    DodgeRules::dodgeStartBlock(true, false, true, 1.0f),
                    DodgeRules::DodgeStartBlock::Dashing); // 冲刺第三
        expectBlock("只剩冷却：报告冷却",
                    DodgeRules::dodgeStartBlock(true, false, false, 1.0f),
                    DodgeRules::DodgeStartBlock::Cooldown); // 冷却最后
    }

    // ------------------------------------------------------------------
    // CASE 6 一次完整冲刺生命周期（触发 -> 冲刺 -> 冷却 -> 再触发）
    // ------------------------------------------------------------------
    beginCase(6, "完整冲刺生命周期"); // 用例 6：完整冲刺生命周期
    {
        bool dashing = false;                    // 冲刺状态
        float cooldownLeft = 0.0f;               // 冷却剩余
        const float dt = 1.0f / 120.0f;          // 固定步长
        float elapsed = 0.0f;                    // 冲刺计时

        // 第一次触发：冷却与冲刺同时置位（与 Player::tryDodge 一致）
        const bool firstStart = DodgeRules::canStartDodge(true, false, dashing, cooldownLeft); // 首次判定
        expectTrue("首次可触发", firstStart); // 无阻塞
        if (firstStart) // 模拟成功启动
        {
            dashing = true;                                         // 进入冲刺
            elapsed = 0.0f;                                         // 计时清零
            cooldownLeft = DodgeRules::kCooldownSeconds;            // 冷却起算
        }

        // 冲刺途中再按：冲刺中优先于冷却，返回 Dashing
        expectBlock("冲刺中再按：Dashing",
                    DodgeRules::dodgeStartBlock(true, false, dashing, cooldownLeft),
                    DodgeRules::DodgeStartBlock::Dashing); // 不可重复触发

        // 推进 0.18s：退出冲刺，但冷却仍剩 1.02s
        while (elapsed < DodgeRules::kDurationSeconds) // 按固定步推进
        {
            elapsed += dt;                                                  // 计时
            cooldownLeft = DodgeRules::cooldownAfter(cooldownLeft, dt);     // 冷却同步递减
        }
        dashing = false; // 时长到点：冲刺结束
        // 0.18s 在 120Hz 下实际走 22 步 = 0.18333s（第 22 步到点），
        // 因此冷却剩余 = 1.2 - 22/120 ≈ 1.016667s（与运行时同一步数口径）
        expectNear("冲刺结束冷却剩余 ≈ 1.017s", cooldownLeft, 1.016667f, 0.002f); // 1.2-22/120

        // 冷却中再按：返回 Cooldown（第二次无效）
        expectBlock("冷却中再按：Cooldown",
                    DodgeRules::dodgeStartBlock(true, false, dashing, cooldownLeft),
                    DodgeRules::DodgeStartBlock::Cooldown); // 第二次无效

        // 继续推进直到冷却归零：恢复可触发
        while (cooldownLeft > 0.0f) // 按固定步推进
        {
            cooldownLeft = DodgeRules::cooldownAfter(cooldownLeft, dt); // 递减
        }
        expectBlock("1.2s 后恢复：允许",
                    DodgeRules::dodgeStartBlock(true, false, dashing, cooldownLeft),
                    DodgeRules::DodgeStartBlock::None); // 冷却结束

        // R 重开：Player::reset 会把冷却清零、冲刺状态清空（模拟重置）
        dashing = false;        // 冲刺状态清零
        cooldownLeft = 0.0f;    // 冷却清零
        expectBlock("R 重开后立即可冲刺",
                    DodgeRules::dodgeStartBlock(true, false, dashing, cooldownLeft),
                    DodgeRules::DodgeStartBlock::None); // 重开复位
    }

    // ------------------------------------------------------------------
    // 汇总
    // ------------------------------------------------------------------
    std::printf("\n==============================================================\n"); // 汇总上边线
    std::printf(" 通过 %d 条，失败 %d 条\n", gPass, gFail); // 汇总
    std::printf("==============================================================\n"); // 汇总下边线

    return gFail; // 退出码 = 失败断言数（全部通过 = 0）
}
