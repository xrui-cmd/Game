// ============================================================================
//  boss_test.cpp — 双区 Boss 纯逻辑单元测试（M3·乙）
//
//  被测对象：Src/inc/game/Boss.h（header-only，不依赖 SDL 运行时）
//    - 降临节奏：间隔下限 / 首只 180s / 每 180s 排程 / 同屏唯一跳过；
//    - 双区选型：草地 = 哥布林王 Lv20、雪山 = 霜爵 Lv30；
//    - 属性公式：生命 = 变体基础 30 x8 x 等级缩放（696 / 936）；
//      速度 99、伤害倍率 1.5、体型 72 / 脚盒 54x45；
//    - 专属模组：重砸/冲锋/冰环/重击的蓄力/距离/半径/元素/独立冷却；
//    - 距离 + 冷却驱动的选招（近身/远距/射程外）；
//    - 奖励：金币 25~40、经验 150~200、击杀回血 30%；
//    - 免疫：冻结/减速对 Boss 无效。
//
//  退出码：main 返回失败断言数（全部通过 = 0）。
// ============================================================================

#include "game/Boss.h" // 被测对象（BossType / kBossDefs / 模组表 / 纯函数）

#include <cmath>   // std::fabs：浮点近似断言
#include <cstdio>  // std::printf
#include <cstdlib> // std::system（Windows 控制台切 UTF-8）
#include <limits>  // std::numeric_limits：NaN 防御测试

namespace
{

int gPass = 0; // 通过的断言数
int gFail = 0; // 失败的断言数（作为 main 返回值）

// 布尔断言：条件成立才计入通过
void expectTrue(const char* label, bool value)
{
    const bool ok = value; // 断言条件
    if (ok) { ++gPass; } else { ++gFail; } // 计数
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label); // 输出一行
}

// 整数断言：相等才通过
void expectInt(const char* label, int got, int expect)
{
    const bool ok = (got == expect); // 整数相等
    if (ok) { ++gPass; } else { ++gFail; } // 计数
    std::printf("  [%s] %s | 期望=%d 实际=%d\n",
                ok ? "PASS" : "FAIL", label, expect, got); // 输出期望/实际
}

// 浮点近似断言：|got - expect| <= eps
void expectNear(const char* label, float got, float expect, float eps = 0.001f)
{
    const bool ok = std::fabs(got - expect) <= eps; // 偏差在容差内
    if (ok) { ++gPass; } else { ++gFail; } // 计数
    std::printf("  [%s] %s | 期望≈%.6f 实际=%.6f\n",
                ok ? "PASS" : "FAIL", label, expect, got); // 输出期望/实际
}

// 用例开始分隔（与项目其余测试同款输出格式）
void beginCase(int idx, const char* title)
{
    std::printf("\n[CASE %d] %s\n", idx, title); // 用例标题
}

} // namespace

int main()
{
#if defined(_WIN32)
    std::system("chcp 65001 > nul 2>&1"); // 控制台切 UTF-8：中文输出不乱码
#endif

    std::printf("==============================================================\n");
    std::printf(" 双区 Boss 单元测试（M3·乙）\n");
    std::printf(" 降临节奏 / 属性公式 / 专属模组 / 选招 / 奖励 / 免疫\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 双区选型与基础属性
    // ------------------------------------------------------------------
    beginCase(1, "双区选型与基础属性");
    {
        expectInt("Boss 种类数 = 2", kBossTypeCount, 2); // 双区各一
        expectTrue("草地选型 = 哥布林王", bossTypeForZone(ZoneId::Grass) == BossType::GoblinKing);
        expectTrue("雪山选型 = 霜爵", bossTypeForZone(ZoneId::Snow) == BossType::FrostDuke);
        expectTrue("哨兵/越界区回退哥布林王", bossTypeForZone(ZoneId::Count) == BossType::GoblinKing);
        expectInt("草地 Boss 等级 = 20", bossLevelForZone(ZoneId::Grass), 20);
        expectInt("雪山 Boss 等级 = 30", bossLevelForZone(ZoneId::Snow), 30);

        expectInt("哥布林王 Lv20 生命 = 696", bossMaxHp(BossType::GoblinKing, 20), 696);
        expectInt("霜爵 Lv30 生命 = 936", bossMaxHp(BossType::FrostDuke, 30), 936);
        expectInt("生命公式 = 30 x8 x (1+10%x(lv-1))",
                  bossMaxHp(BossType::GoblinKing, 1), 240); // 1 级基准 = 240
        expectNear("Boss 速度 = 99（110 x0.9）", bossBaseSpeed(BossType::GoblinKing), 99.0f);
        expectNear("Boss 体型 = 72（48 x1.5）", kBossVisualSize, 72.0f);
        expectNear("Boss 脚盒宽 = 54（36 x1.5）", kBossCollisionWidth, 54.0f);
        expectNear("Boss 脚盒高 = 45（30 x1.5）", kBossCollisionHeight, 45.0f);
        expectNear("Boss 伤害倍率 = 1.5", kBossDamageMultiplier, 1.5f);
        expectNear("重置环带内径 = 500", kBossSpawnRingMin, 500.0f);
        expectNear("重置环带外径 = 700", kBossSpawnRingMax, 700.0f);
    }

    // ------------------------------------------------------------------
    // CASE 2 降临节奏：间隔 / 排程 / 同屏唯一
    // ------------------------------------------------------------------
    beginCase(2, "降临节奏：180s 间隔与同屏唯一");
    {
        expectNear("有效间隔 180", bossSafeInterval(180.0f), 180.0f); // 正常配置
        expectNear("间隔 0 回退 1s", bossSafeInterval(0.0f), 1.0f);   // 配置异常
        expectNear("间隔负数回退 1s", bossSafeInterval(-5.0f), 1.0f); // 配置异常
        expectNear("间隔 NaN 回退 1s",
                   bossSafeInterval(std::numeric_limits<float>::quiet_NaN()), 1.0f); // NaN 防御

        expectNear("首只 = 180s", bossNextAttemptTime(0.0f, 180.0f), 180.0f);
        expectNear("第二次 = 360s", bossNextAttemptTime(180.0f, 180.0f), 360.0f);
        expectNear("第三次 = 540s", bossNextAttemptTime(360.0f, 180.0f), 540.0f);
        expectTrue("179.9s 未到点", !bossSpawnIsDue(179.9f, 180.0f));
        expectTrue("180s 恰好到点", bossSpawnIsDue(180.0f, 180.0f));
        expectTrue("200s 越点也尝试", bossSpawnIsDue(200.0f, 180.0f));
        expectTrue("场上无 Boss：不跳过", !bossSpawnBlockedByAlive(false));
        expectTrue("场上有 Boss：跳过（同屏唯一）", bossSpawnBlockedByAlive(true));
    }

    // ------------------------------------------------------------------
    // CASE 3 专属攻击模组
    // ------------------------------------------------------------------
    beginCase(3, "专属攻击模组：重砸/冲锋/冰环/重击");
    {
        const BossAttackDef& slam = bossAttackDef(BossType::GoblinKing, 0); // 哥布林王近身
        expectTrue("哥布林王第 0 招 = 重砸", slam.module.kind == AttackKind::Slam);
        expectNear("重砸蓄力 0.6s", slam.module.windupSeconds, 0.60f);
        expectNear("重砸 AoE 半径 90", slam.module.hitRadius, 90.0f);
        expectNear("重砸冷却 3.5s", slam.cooldownSeconds, 3.5f);
        expectNear("重砸无位移", slam.module.distance, 0.0f);

        const BossAttackDef& charge = bossAttackDef(BossType::GoblinKing, 1); // 哥布林王远距
        expectTrue("哥布林王第 1 招 = 冲锋", charge.module.kind == AttackKind::Charge);
        expectNear("冲锋蓄力 0.4s", charge.module.windupSeconds, 0.40f);
        expectNear("冲锋距离 220", charge.module.distance, 220.0f);
        expectNear("冲锋时长 0.35s", charge.module.duration, 0.35f);
        expectNear("冲锋冷却 5.0s", charge.cooldownSeconds, 5.0f);

        const BossAttackDef& strike = bossAttackDef(BossType::FrostDuke, 0); // 霜爵近身
        expectTrue("霜爵第 0 招 = 重击（前扑）", strike.module.kind == AttackKind::Lunge);
        expectNear("重击蓄力 0.5s", strike.module.windupSeconds, 0.50f);
        expectNear("重击前扑 90", strike.module.distance, 90.0f);
        expectTrue("重击带冻结", strike.module.element == AttackElement::Freeze);
        expectInt("重击冻结条 +35", strike.module.freezeAmount, 35);
        expectNear("重击减速 x0.6", strike.module.slowFactor, 0.6f);
        expectNear("重击减速 3s", strike.module.slowSeconds, 3.0f);
        expectNear("重击冷却 3.0s", strike.cooldownSeconds, 3.0f);

        const BossAttackDef& ring = bossAttackDef(BossType::FrostDuke, 1); // 霜爵远距
        expectTrue("霜爵第 1 招 = 冰环", ring.module.kind == AttackKind::Ring);
        expectNear("冰环蓄力 0.5s", ring.module.windupSeconds, 0.50f);
        expectNear("冰环半径 160", ring.module.hitRadius, 160.0f);
        expectTrue("冰环带冻结", ring.module.element == AttackElement::Freeze);
        expectInt("冰环冻结条 +15", ring.module.freezeAmount, 15);
        expectNear("冰环冷却 5.0s", ring.cooldownSeconds, 5.0f);

        expectTrue("招式下标越界向上夹取",
                   bossAttackDef(BossType::FrostDuke, 99).module.kind == AttackKind::Ring);
        expectTrue("招式下标越界向下夹取",
                   bossAttackDef(BossType::FrostDuke, -5).module.kind == AttackKind::Lunge);
    }

    // ------------------------------------------------------------------
    // CASE 4 距离 + 冷却驱动选招
    // ------------------------------------------------------------------
    beginCase(4, "距离 + 冷却驱动选招");
    {
        expectInt("哥布林王近身且就绪 -> 重砸",
                  bossSelectAttack(BossType::GoblinKing, 80.0f, true, true), 0);
        expectInt("哥布林王近身但重砸冷却 -> 等待",
                  bossSelectAttack(BossType::GoblinKing, 80.0f, false, true), -1);
        expectInt("哥布林王中距且冲锋就绪 -> 冲锋",
                  bossSelectAttack(BossType::GoblinKing, 200.0f, true, true), 1);
        expectInt("哥布林王中距但冲锋冷却 -> 等待",
                  bossSelectAttack(BossType::GoblinKing, 200.0f, true, false), -1);
        expectInt("哥布林王射程外 -> 继续追击",
                  bossSelectAttack(BossType::GoblinKing, 400.0f, true, true), -1);
        expectInt("霜爵近身 -> 重击",
                  bossSelectAttack(BossType::FrostDuke, 100.0f, true, true), 0);
        expectInt("霜爵中距 -> 冰环",
                  bossSelectAttack(BossType::FrostDuke, 150.0f, true, true), 1);
        expectInt("霜爵冰环冷却 -> 等待",
                  bossSelectAttack(BossType::FrostDuke, 150.0f, true, false), -1);
        expectInt("霜爵射程外 -> 继续追击",
                  bossSelectAttack(BossType::FrostDuke, 300.0f, true, true), -1);
        expectInt("负距离按 0 处理（近身档）",
                  bossSelectAttack(BossType::GoblinKing, -10.0f, true, true), 0);
    }

    // ------------------------------------------------------------------
    // CASE 5 奖励与击杀回血
    // ------------------------------------------------------------------
    beginCase(5, "奖励：金币/经验/击杀回血");
    {
        expectInt("金币 roll 0 -> 25", bossCoinReward(0), 25);
        expectInt("金币 roll 15 -> 40", bossCoinReward(15), 40);
        expectInt("金币 roll 负数夹到 25", bossCoinReward(-3), 25);
        expectInt("金币 roll 越界夹到 40", bossCoinReward(99), 40);
        expectInt("经验 roll 0 -> 150", bossXpReward(0), 150);
        expectInt("经验 roll 50 -> 200", bossXpReward(50), 200);
        expectInt("经验 roll 负数夹到 150", bossXpReward(-1), 150);
        expectInt("经验 roll 越界夹到 200", bossXpReward(99), 200);
        expectInt("回血 100 x30% = 30", bossKillHeal(100, 30.0f), 30);
        expectInt("回血关闭（0%）", bossKillHeal(100, 0.0f), 0);
        expectInt("回血负数关闭", bossKillHeal(100, -5.0f), 0);
        expectInt("回血 >100% 夹到上限", bossKillHeal(200, 150.0f), 200);
        expectInt("回血上限异常返回 0", bossKillHeal(0, 30.0f), 0);
    }

    // ------------------------------------------------------------------
    // CASE 6 免疫规则
    // ------------------------------------------------------------------
    beginCase(6, "Boss 免疫冻结与减速");
    {
        expectTrue("免疫减速", bossImmuneToElement(AttackElement::Slow));
        expectTrue("免疫冻结", bossImmuneToElement(AttackElement::Freeze));
        expectTrue("不免疫无元素", !bossImmuneToElement(AttackElement::None));
    }

    std::printf("\n==============================================================\n");
    std::printf(" 结果：通过 %d / 失败 %d\n", gPass, gFail);
    std::printf("==============================================================\n");
    return gFail; // 退出码 = 失败断言数（0 = 全过）
}
