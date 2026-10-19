// ============================================================================
//  attack_element_test.cpp — 进攻模组表与冰霜元素状态单元测试（批六丁）
//
//  被测对象（均为 header-only 纯逻辑，不依赖 SDL/窗口/纹理）：
//    1. Src/inc/game/AttackModule.h —— 六型进攻模组表：
//       蓄力/攻击方式/距离/时长/命中半径/伤害倍率/元素参数的范围与定值；
//    2. Src/inc/game/ElementStatus.h —— SlowStatus（减速计时刷新不叠乘）与
//       FreezeMeter（累积/衰减/阈值/冻结结束清零/抗冻缓冲/缓冲期不累积）。
//
//  覆盖场景：
//    ① 六型表完整性：数量、三种攻击方式、六行参数范围、关键定值；
//    ② 减速：施加/刷新/覆盖/到期/非法输入/乘区夹取；
//    ③ 冻结条：累积、每秒衰减 10、满 100 冻结 1.5s、冻结结束清零、
//       2s 抗冻缓冲（缓冲期不累积）、大步长跨过冻结终点的余量处理。
//
//  退出码：main 返回失败断言数（全部通过 = 0）。
// ============================================================================

#include "game/AttackModule.h"  // kAttackModules / AttackKind / AttackElement
#include "game/ElementStatus.h" // SlowStatus / FreezeMeter / FreezeTickResult

#include <cmath>    // std::fabs：浮点近似断言
#include <cstdio>   // std::printf
#include <cstdlib>  // std::system（Windows 控制台切 UTF-8）

namespace
{

int gPass = 0; // 通过的断言数
int gFail = 0; // 失败的断言数（作为 main 返回值）

// 布尔断言：条件成立才计入通过
void expectTrue(const char* label, bool value)
{
    const bool ok = value; // 断言条件
    if (ok) { ++gPass; } else { ++gFail; } // 计数
    // 输出一行 PASS/FAIL（便于定位失败项）
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
}

// 浮点近似断言：|got - expect| <= eps
void expectNear(const char* label, float got, float expect, float eps = 0.001f)
{
    const bool ok = std::fabs(got - expect) <= eps; // 偏差在容差内
    if (ok) { ++gPass; } else { ++gFail; } // 计数
    // 输出期望/实际（六位小数足够诊断）
    std::printf("  [%s] %s | 期望≈%.6f 实际=%.6f\n",
                ok ? "PASS" : "FAIL", label, expect, got);
}

// 整数断言：相等才通过
void expectInt(const char* label, int got, int expect)
{
    const bool ok = (got == expect); // 整数相等
    if (ok) { ++gPass; } else { ++gFail; } // 计数
    // 输出期望/实际
    std::printf("  [%s] %s | 期望=%d 实际=%d\n",
                ok ? "PASS" : "FAIL", label, expect, got);
}

// 用例开始分隔（与项目其余测试同款输出格式）
void beginCase(int idx, const char* title)
{
    std::printf("\n[CASE %d] %s\n", idx, title);
}

} // namespace

int main()
{
#if defined(_WIN32)
    // 控制台切 UTF-8：中文输出不乱码（失败也不影响测试结果）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" 进攻模组表与冰霜元素状态单元测试（批六丁）\n");
    std::printf(" 六型模组参数范围 / 减速刷新 / 冻结条累积·衰减·阈值·缓冲\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 六型进攻模组表：数量 / 方式 / 参数范围 / 关键定值
    // ------------------------------------------------------------------
    beginCase(1, "六型进攻模组表完整性与参数范围");
    {
        expectInt("模组数量 = 6", kAttackModuleCount, 6); // 六型怪物一一对应

        // 逐行检查所有模组的通用参数范围（六型都必须是可执行的合法动作）
        bool allRangesOk = true; // 通用范围汇总
        for (int i = 0; i < kAttackModuleCount; ++i) // 遍历六型
        {
            const AttackModule& m = attackModuleForIndex(i); // 当前模组
            const bool windupOk = m.windupSeconds >= 0.2f && m.windupSeconds <= 0.6f; // 蓄力窗口
            const bool distanceOk = m.distance > 0.0f && m.distance <= 200.0f; // 位移预算
            const bool durationOk = m.duration > 0.0f && m.duration <= 0.5f; // 位移时长
            const bool radiusOk = m.hitRadius >= 0.0f && m.hitRadius <= 50.0f; // 命中半径
            const bool damageOk = m.damageMultiplier >= 0.5f && m.damageMultiplier <= 2.0f; // 伤害倍率
            if (!(windupOk && distanceOk && durationOk && radiusOk && damageOk)) // 任一项越界
            {
                allRangesOk = false; // 标记失败
            }
        }
        expectTrue("六型通用参数均在可玩范围（蓄力/距离/时长/半径/倍率）", allRangesOk);

        const AttackModule& slime = attackModuleForIndex(0); // 0 史莱姆
        expectTrue("史莱姆 = 跳跃砸击", slime.kind == AttackKind::JumpSmash);
        expectNear("史莱姆蓄力 0.45s", slime.windupSeconds, 0.45f);
        expectNear("史莱姆跃出 80px", slime.distance, 80.0f);
        expectNear("史莱姆位移 0.3s", slime.duration, 0.30f);
        expectNear("史莱姆落点半径 40px", slime.hitRadius, 40.0f);
        expectTrue("史莱姆无元素", slime.element == AttackElement::None);

        const AttackModule& goblin = attackModuleForIndex(1); // 1 哥布林
        expectTrue("哥布林 = 前扑", goblin.kind == AttackKind::Lunge);
        expectNear("哥布林蓄力 0.35s（现有手感）", goblin.windupSeconds, 0.35f);
        expectNear("哥布林前扑 24px", goblin.distance, 24.0f);
        expectNear("哥布林位移 0.12s", goblin.duration, 0.12f);
        expectTrue("哥布林无元素", goblin.element == AttackElement::None);

        const AttackModule& skelet = attackModuleForIndex(2); // 2 骷髅
        expectTrue("骷髅 = 突刺冲锋", skelet.kind == AttackKind::Thrust);
        expectNear("骷髅蓄力 0.3s", skelet.windupSeconds, 0.30f);
        expectNear("骷髅突刺 140px", skelet.distance, 140.0f);
        expectNear("骷髅位移 0.25s", skelet.duration, 0.25f);
        expectTrue("骷髅无元素", skelet.element == AttackElement::None);

        const AttackModule& iceSlime = attackModuleForIndex(3); // 3 冰史莱姆
        expectTrue("冰史莱姆 = 跳跃砸击", iceSlime.kind == AttackKind::JumpSmash);
        expectTrue("冰史莱姆带减速", iceSlime.element == AttackElement::Slow);
        expectNear("冰史莱姆减速 x0.8", iceSlime.slowFactor, 0.8f);
        expectNear("冰史莱姆减速 3s", iceSlime.slowSeconds, 3.0f);
        expectTrue("冰史莱姆有冰霜视觉（落地冰环）", iceSlime.frostVisual);

        const AttackModule& frostGoblin = attackModuleForIndex(4); // 4 霜哥布林
        expectTrue("霜哥布林 = 前扑", frostGoblin.kind == AttackKind::Lunge);
        expectTrue("霜哥布林带冻结", frostGoblin.element == AttackElement::Freeze);
        expectInt("霜哥布林冻结条 +25", frostGoblin.freezeAmount, 25);
        expectTrue("霜哥布林有冰霜视觉（冰爪）", frostGoblin.frostVisual);

        const AttackModule& frostSkelet = attackModuleForIndex(5); // 5 霜骷髅
        expectTrue("霜骷髅 = 突刺", frostSkelet.kind == AttackKind::Thrust);
        expectTrue("霜骷髅带冻结", frostSkelet.element == AttackElement::Freeze);
        expectInt("霜骷髅冻结条 +20", frostSkelet.freezeAmount, 20);
        expectTrue("霜骷髅有冰霜视觉（冰痕）", frostSkelet.frostVisual);
    }

    // ------------------------------------------------------------------
    // CASE 2 减速：施加 / 刷新不叠乘 / 覆盖 / 到期 / 非法输入
    // ------------------------------------------------------------------
    beginCase(2, "减速计时：刷新不叠乘与到期恢复");
    {
        SlowStatus slow; // 被测：减速状态
        expectNear("初始乘区 = 1", slow.moveMultiplier(), 1.0f);
        expectTrue("初始未生效", !slow.active());

        slow.apply(0.8f, 3.0f); // 冰史莱姆减速
        expectNear("施加后乘区 = 0.8", slow.moveMultiplier(), 0.8f);
        expectNear("剩余 3s", slow.remaining(), 3.0f);
        expectTrue("生效中", slow.active());

        expectTrue("推进 1s 未到期", !slow.update(1.0f));
        expectNear("剩余 2s", slow.remaining(), 2.0f);

        slow.apply(0.8f, 3.0f); // 重复命中：刷新时长
        expectNear("刷新后剩余 3s", slow.remaining(), 3.0f);
        expectNear("刷新不叠乘（仍 x0.8）", slow.moveMultiplier(), 0.8f);

        slow.apply(0.5f, 2.0f); // 更强的减速覆盖旧乘区
        expectNear("覆盖后乘区 = 0.5", slow.moveMultiplier(), 0.5f);
        expectNear("覆盖后剩余 2s", slow.remaining(), 2.0f);

        expectTrue("推进 1.99s 未到期", !slow.update(1.99f));
        expectTrue("最后一小步到期", slow.update(0.02f));
        expectNear("到期乘区回 1", slow.moveMultiplier(), 1.0f);
        expectNear("到期剩余 0", slow.remaining(), 0.0f);
        expectTrue("到期后不再重复报告", !slow.update(1.0f));

        slow.apply(0.8f, 0.0f); // 非法时长：忽略
        expectTrue("0 秒减速被忽略", !slow.active());
        slow.apply(0.8f, -2.0f); // 负时长：忽略
        expectTrue("负秒减速被忽略", !slow.active());

        slow.apply(1.5f, 1.0f); // 乘区 >1（加速误用）：夹回 1
        expectNear("乘区上限夹到 1", slow.moveMultiplier(), 1.0f);
        slow.apply(-0.2f, 1.0f); // 负乘区：夹到 0
        expectNear("负乘区夹到 0", slow.moveMultiplier(), 0.0f);

        slow.reset(); // 复位
        expectTrue("复位后无减速", !slow.active());
    }

    // ------------------------------------------------------------------
    // CASE 3 冻结条：累积 / 每秒衰减 10 / 阈值冻结
    // ------------------------------------------------------------------
    beginCase(3, "冻结条累积、衰减与阈值冻结");
    {
        FreezeMeter meter; // 被测：冻结条
        expectNear("初始值 0", meter.value(), 0.0f);
        expectTrue("初始未冻结", !meter.isFrozen());
        expectTrue("初始无缓冲", !meter.isImmune());

        FreezeTickResult r = meter.add(25); // 霜哥布林命中
        expectInt("+25", static_cast<int>(meter.value()), 25);
        expectTrue("+25 未触发冻结", !r.freezeStarted);
        r = meter.add(20); // 霜骷髅命中
        expectInt("再 +20 = 45", static_cast<int>(meter.value()), 45);
        expectTrue("45 未触发冻结", !r.freezeStarted);
        r = meter.add(0); // 0 点：忽略
        expectInt("+0 不变", static_cast<int>(meter.value()), 45);
        expectTrue("+0 无上升沿", !r.freezeStarted);
        r = meter.add(-5); // 负点：忽略
        expectInt("负点不变", static_cast<int>(meter.value()), 45);
        expectTrue("负点无上升沿", !r.freezeStarted);

        meter.update(1.0f); // 一秒不挨打：衰减 10
        expectInt("衰减 1s 后 = 35", static_cast<int>(meter.value()), 35);
        meter.update(2.0f); // 两秒：再衰减 20
        expectInt("再衰减 2s 后 = 15", static_cast<int>(meter.value()), 15);
        meter.update(2.0f); // 过量衰减：夹到 0
        expectNear("衰减不出现负值", meter.value(), 0.0f);

        meter.add(60); // 累积到 60
        r = meter.add(40); // 再 +40 -> 满 100
        expectTrue("满 100 触发冻结", r.freezeStarted);
        expectTrue("冻结状态成立", meter.isFrozen());
        expectNear("冻结 1.5s", meter.freezeLeft(), 1.5f);
        expectNear("冻结期间保持满条", meter.value(), 100.0f);

        r = meter.add(50); // 冻结中：不再累积
        expectTrue("冻结中 +50 无上升沿", !r.freezeStarted);
        expectNear("冻结中仍为 100", meter.value(), 100.0f);
    }

    // ------------------------------------------------------------------
    // CASE 4 冻结结束：清零 / 2s 抗冻缓冲 / 缓冲期不累积 / 大步长
    // ------------------------------------------------------------------
    beginCase(4, "冻结结束清零与抗冻缓冲");
    {
        FreezeMeter meter; // 被测：冻结条
        meter.add(100);    // 直接触发冻结

        FreezeTickResult r = meter.update(1.0f); // 1s：冻结还剩 0.5s
        expectTrue("1s 时未结束", !r.freezeEnded);
        expectTrue("仍在冻结", meter.isFrozen());
        expectNear("冻结剩余 0.5s", meter.freezeLeft(), 0.5f);

        r = meter.update(0.5f); // 走完 1.5s
        expectTrue("1.5s 时冻结结束", r.freezeEnded);
        expectTrue("解除冻结", !meter.isFrozen());
        expectNear("冻结结束清零", meter.value(), 0.0f);
        expectTrue("进入抗冻缓冲", meter.isImmune());
        expectNear("缓冲 2s", meter.immunityLeft(), 2.0f);

        r = meter.add(25); // 缓冲期：不累积
        expectInt("缓冲期 +25 被忽略", static_cast<int>(meter.value()), 0);
        expectTrue("缓冲期无冻结上升沿", !r.freezeStarted);

        meter.update(1.99f); // 缓冲还剩 0.01s
        expectTrue("缓冲期内仍免疫", meter.isImmune());
        meter.update(0.02f); // 缓冲结束
        expectTrue("缓冲结束", !meter.isImmune());
        expectNear("缓冲结束值仍 0", meter.value(), 0.0f);

        meter.add(20); // 缓冲结束后恢复累积
        expectInt("缓冲后可累积 +20", static_cast<int>(meter.value()), 20);
        meter.update(1.0f); // 正常衰减
        expectInt("衰减 1s 后 = 10", static_cast<int>(meter.value()), 10);

        // 大步长跨过冻结终点：剩余步长应继续计入抗冻缓冲
        FreezeMeter big; // 大步长用例
        big.add(100);    // 触发冻结
        r = big.update(2.0f); // 1.5s 冻结 + 0.5s 缓冲
        expectTrue("大步长一步结束冻结", r.freezeEnded);
        expectNear("大步长缓冲余量 = 1.5s", big.immunityLeft(), 1.5f);
        expectNear("大步长后值清零", big.value(), 0.0f);

        big.reset(); // 复位
        expectTrue("复位解除冻结", !big.isFrozen());
        expectTrue("复位清缓冲", !big.isImmune());
        expectNear("复位清值", big.value(), 0.0f);
    }

    // ------------------------------------------------------------------
    // 汇总
    // ------------------------------------------------------------------
    std::printf("\n==============================================================\n");
    std::printf(" 断言结果：%d 通过 / %d 失败（共 %d）\n", gPass, gFail, gPass + gFail);
    std::printf("==============================================================\n");
    return gFail; // 退出码 = 失败断言数（0 = 全绿）
}
