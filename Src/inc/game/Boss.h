#ifndef __BOSS_H__
#define __BOSS_H__

#include "game/AttackModule.h" // AttackModule / AttackKind / AttackElement：Boss 专属攻击模组复用同一执行器
#include "game/MonsterLevel.h" // scaledMonsterHp：Boss 生命沿用「基础 x 8 x 等级缩放」的既有公式
#include "game/Zone.h"         // ZoneId / zoneDef / bossTypeForZone 的区表来源（草地/雪山）
#include <algorithm>           // std::clamp / std::max：参数与奖励的边界夹取
#include <cmath>               // std::lround：回血与生命取整

// ============================================================
// Boss：M3·乙 双区 Boss 的纯逻辑定义（header-only，可单测）
// ============================================================
//
// 设计口径（规格「M3·乙 Boss 机制」）：
//   1. 双区选型：Boss 按「生成点所在区」决定身份——
//        草地 = 哥布林王（GoblinKing）
//        雪山 = 霜爵（FrostDuke）
//      外观复用同区基础变体（哥布林 / 霜哥布林）并放大，不新增素材。
//   2. 属性：
//        视觉/碰撞盒 x1.5（48 -> 72 视觉；脚盒 36x30 -> 54x45）；
//        生命 = 变体基础生命 x 8 x 等级缩放（草地 Lv20 / 雪山 Lv30）；
//        攻击伤害 = 既有等级伤害 x 1.5；
//        速度 = 变体基础速度 x 0.9（略降，保持「可跑开」的设计红线）。
//   3. 专属攻击：每个 Boss 两招（近身/远距各一），参数与冷却见
//      kBossAttackDefs；距离 + 冷却驱动的选择见 bossSelectAttack。
//   4. 免疫：Boss 免疫减速与冻结（规格明确）——纯判据 bossImmuneToElement
//      供实体层与单测共用；实体层的减速入口会据此忽略并打日志。
//   5. 奖励：金币 25~40、经验 150~200、击杀回血 30%（百分比可配置）；
//      奖励函数全部为纯函数，单测可直接核对边界。
//   6. 降临节奏：boss_interval_seconds（默认 180s）首只 = 开局后 180s，
//      此后每 180s 尝试一次；场上同时最多 1 只（存活则本次跳过并日志）。
//
// 本文件不依赖 SDL 运行时、不拥有实体状态——场景与 Monster 只调用这些
// 纯函数并落实结果，测试可直接包含（tests/boss_test.cpp）。
// ============================================================

// Boss 身份：下标与 kBossDefs / kBossAttackDefs 严格一致，不可交换
enum class BossType
{
    GoblinKing = 0, // 哥布林王（草地）：重砸 / 冲锋
    FrostDuke,      // 霜爵（雪山）：冰环 / 重击
    Count           // 哨兵：Boss 种类数（= 2，不是真实 Boss）
};

// Boss 种类数（数组尺寸与循环上界的统一口径）
inline constexpr int kBossTypeCount = static_cast<int>(BossType::Count);

// Boss 单招数量（每个 Boss 固定两招：近身一招、远距一招）
inline constexpr int kBossAttackCount = 2;

// ---- 体型与属性常量（规格定稿；报告中的总表即本段）----
inline constexpr float kBossScale = 1.5f;             // 体型放大倍数：视觉与碰撞盒同步
inline constexpr float kBossVisualSize = 72.0f;       // Boss 视觉边长（48 x 1.5 = 72）
inline constexpr float kBossCollisionWidth = 54.0f;   // Boss 脚盒宽（36 x 1.5）
inline constexpr float kBossCollisionHeight = 45.0f;  // Boss 脚盒高（30 x 1.5）
inline constexpr float kBossCollisionOffsetX = 9.0f;  // 脚盒左偏移（6 x 1.5）
inline constexpr float kBossCollisionOffsetY = 18.0f; // 脚盒上偏移（12 x 1.5）
inline constexpr int kBossHpMultiplier = 8;           // 生命倍率：变体基础 x 8
inline constexpr int kBossVariantBaseHp = 30;         // 两 Boss 复用哥布林系基础生命（30）
inline constexpr int kBossVariantBaseSpeed = 110;     // 两 Boss 复用哥布林系基础速度（110）
inline constexpr float kBossSpeedFactor = 0.9f;       // 速度略降：110 x 0.9 = 99 像素/秒
inline constexpr float kBossDamageMultiplier = 1.5f;  // 攻击伤害倍率（规格 x1.5）

// ---- 降临节奏常量（配置键 boss_interval_seconds 的缺省值与下限）----
inline constexpr float kBossIntervalDefault = 180.0f; // 首只与后续尝试间隔（秒）
inline constexpr float kBossIntervalMin = 1.0f;       // 配置下限（秒）：0/负值按 1s 处理，避免同帧刷爆

// ---- 降临位置环带（规格 500~700px；与普通怪 520~900 区分）----
inline constexpr float kBossSpawnRingMin = 500.0f; // 环带内径（像素）
inline constexpr float kBossSpawnRingMax = 700.0f; // 环带外径（像素）

// ---- 奖励常量（规格：金币 25~40、经验 150~200、击杀回血 30%）----
inline constexpr int kBossCoinMin = 25;              // 金币下限（枚）
inline constexpr int kBossCoinMax = 40;              // 金币上限（枚）
inline constexpr int kBossXpMin = 150;               // 经验下限（点）
inline constexpr int kBossXpMax = 200;               // 经验上限（点）
inline constexpr float kBossKillHealPercentDefault = 30.0f; // 击杀回血缺省（最大生命百分比）
inline constexpr int kBossCoinRolls = kBossCoinMax - kBossCoinMin + 1; // 金币均匀 roll 数（16）
inline constexpr int kBossXpRolls = kBossXpMax - kBossXpMin + 1;       // 经验均匀 roll 数（51）

// 单个 Boss 的静态定义（身份/所属区/固定等级/两招的触发距离）
struct BossDef
{
    const char* name;      // Boss 名（日志/顶部血条：哥布林王 / 霜爵）
    ZoneId zone;           // 归属区（草地 / 雪山）——按生成点所在区选型
    int level;             // 固定等级（草地 20 / 雪山 30——取区等级高点）
    float meleeRange;      // 近身招触发距离（像素）：距离 <= 本值用第 0 招
    float rangedRange;     // 远距招触发距离（像素）：本值内用第 1 招；更远则继续追击
};

// 双 Boss 定义表（下标 = BossType；顺序不可交换）
inline constexpr BossDef kBossDefs[kBossTypeCount] = {
    // 名称        区            等级  近身距离  远距距离
    { "哥布林王", ZoneId::Grass, 20,   100.0f,   300.0f }, // 重砸 90px / 冲锋 220px
    { "霜爵",     ZoneId::Snow,  30,   120.0f,   170.0f }, // 重击接触 / 冰环 160px
};

// 单个 Boss 攻击招式的完整定义（复用 AttackModule 执行器 + 独立冷却）
struct BossAttackDef
{
    const char* name;          // 招式名（日志：重砸/冲锋/冰环/重击）
    AttackModule module;       // 执行参数（蓄力/方式/位移/时长/命中半径/元素）
    float cooldownSeconds;     // 本招独立冷却（秒；出招结算后开始计时）
};

// Boss 专属攻击模组表（下标 = [BossType][0 近身 / 1 远距]）。
// 参数口径（规格 + 可玩性微调，报告总表）：
//   哥布林王·重砸：蓄力 0.6s，原地砸地 AoE 90px，冷却 3.5s；
//   哥布林王·冲锋：蓄力 0.4s，向玩家冲 220px/0.35s，撞墙/命中即止，冷却 5.0s；
//   霜爵·冰环：蓄力 0.5s，360° 冰环半径 160px + 冻结条 +15，冷却 5.0s；
//   霜爵·重击：蓄力 0.5s，前扑 90px/0.2s，冻结条 +35 + 减速 x0.6/3s，冷却 3.0s。
// 伤害倍率统一 1.0：Boss 的 x1.5 由 kBossDamageMultiplier 在结算层统一乘算。
inline constexpr BossAttackDef kBossAttackDefs[kBossTypeCount][kBossAttackCount] = {
    {
        // 哥布林王第 0 招：重砸（近身）：蓄力 0.6s -> 原地 AoE 90px
        { "重砸",
          { "重砸", AttackKind::Slam, 0.60f, 0.0f, 0.12f, 90.0f,
            1.0f, AttackElement::None, 0.0f, 0.0f, 0, false },
          3.50f },
        // 哥布林王第 1 招：冲锋（远距）：蓄力 0.4s -> 220px 冲锋，撞墙/命中即止
        { "冲锋",
          { "冲锋", AttackKind::Charge, 0.40f, 220.0f, 0.35f, 0.0f,
            1.0f, AttackElement::None, 0.0f, 0.0f, 0, false },
          5.00f },
    },
    {
        // 霜爵第 0 招：重击（近身）：前扑 90px + 冻结条 +35 + 减速 x0.6/3s
        { "重击",
          { "重击", AttackKind::Lunge, 0.50f, 90.0f, 0.20f, 0.0f,
            1.0f, AttackElement::Freeze, 0.6f, 3.0f, 35, true },
          3.00f },
        // 霜爵第 1 招：冰环（远距）：360° 冰环 160px + 冻结条 +15
        { "冰环",
          { "冰环", AttackKind::Ring, 0.50f, 0.0f, 0.12f, 160.0f,
            1.0f, AttackElement::Freeze, 0.0f, 0.0f, 15, true },
          5.00f },
    },
};

// Boss 名查询（越界哨兵/异常值回退第一项——与 cardDef/zoneDef 同口径）
inline const char* bossTypeName(BossType type)
{
    const int index = static_cast<int>(type); // BossType -> 表下标
    if (index < 0 || index >= kBossTypeCount) // 越界（理论不可达）
    {
        return kBossDefs[0].name; // 回退哥布林王（确定性兜底）
    }
    return kBossDefs[index].name; // 正常路径：按身份取名字
}

// 按身份取定义（越界回退第一项；所有属性/距离/等级的唯一入口）
inline const BossDef& bossDef(BossType type)
{
    const int index = static_cast<int>(type); // BossType -> 表下标
    if (index < 0 || index >= kBossTypeCount) // 越界（理论不可达）
    {
        return kBossDefs[0]; // 回退哥布林王
    }
    return kBossDefs[index]; // 正常路径
}

// 按身份与招式下标取攻击定义（越界双向夹取——绝不越界读表）
inline const BossAttackDef& bossAttackDef(BossType type, int attackIndex)
{
    const int typeIndex = std::clamp(static_cast<int>(type), 0, kBossTypeCount - 1); // 身份夹取
    const int attackIdx = std::clamp(attackIndex, 0, kBossAttackCount - 1);          // 招式夹取
    return kBossAttackDefs[typeIndex][attackIdx]; // 返回表项（生命周期 = 程序全程）
}

// 生成点所在区 -> Boss 身份：雪山 = 霜爵、其余（草地/哨兵/世界外） = 哥布林王
inline BossType bossTypeForZone(ZoneId zone)
{
    return (zone == ZoneId::Snow) ? BossType::FrostDuke : BossType::GoblinKing; // 双区选型
}

// 生成点所在区 -> Boss 固定等级（草地 20 / 雪山 30——取本区高点档）
inline int bossLevelForZone(ZoneId zone)
{
    return bossDef(bossTypeForZone(zone)).level; // 等级随身份表走（单一事实来源）
}

// Boss 生命上限：变体基础 30 x 8 = 240，再套既有「每级 +10% 基础」缩放。
// 草地 Lv20 = round(240 x 2.9) = 696；雪山 Lv30 = round(240 x 3.9) = 936。
inline int bossMaxHp(BossType type, int level)
{
    (void)type; // 两 Boss 同源哥布林系基础生命；参数保留供未来分化
    return scaledMonsterHp(kBossVariantBaseHp * kBossHpMultiplier, level); // 基础 x8 -> 等级缩放
}

// Boss 基础速度（像素/秒）：变体基础 110 x 0.9 = 99，略低于同族小怪。
// 场景对 Boss 注入固定速度（不再叠难度成长），保持「可跑开」的红线。
inline float bossBaseSpeed(BossType type)
{
    (void)type; // 两 Boss 同速；参数保留供未来分化
    return static_cast<float>(kBossVariantBaseSpeed) * kBossSpeedFactor; // 110 x 0.9
}

// 降临间隔的有效值：配置 <= 0/NaN 时按 1s 处理（绝不出现同帧无限尝试）
inline float bossSafeInterval(float configuredSeconds)
{
    if (!(configuredSeconds >= kBossIntervalMin)) // 非正/NaN：配置异常
    {
        return kBossIntervalMin; // 回退下限
    }
    return configuredSeconds; // 合法配置原样使用
}

// 下一次尝试时刻 = 本次时刻 + 有效间隔（纯函数：单测核对 180 -> 360 -> 540）
inline float bossNextAttemptTime(float currentNextTime, float configuredInterval)
{
    return currentNextTime + bossSafeInterval(configuredInterval); // 线性排程
}

// 本步是否到点尝试降临（恰好等于也算到点——「每 180s 尝试一次」）
inline bool bossSpawnIsDue(float elapsedSeconds, float nextAttemptTime)
{
    return elapsedSeconds >= nextAttemptTime; // 到点/越点都尝试
}

// 场上已有 Boss 时本次是否跳过：true = 跳过（每 180s 一次但同屏唯一）
inline bool bossSpawnBlockedByAlive(bool bossAlive)
{
    return bossAlive; // 唯一性规则：存活即跳过
}

// 距离/冷却驱动的招式选择：
//   - 距离 <= 近身距离：只考虑第 0 招（冷却就绪才出招，否则等待）；
//   - 近身距离 < 距离 <= 远距距离：只考虑第 1 招；
//   - 更远：返回 -1（继续追击，进入射程再出招）。
// 返回招式下标（0/1），-1 = 本步不出招。
inline int bossSelectAttack(BossType type, float distance,
                            bool meleeReady, bool rangedReady)
{
    const BossDef& def = bossDef(type);               // 本 Boss 的触发距离
    const float safeDistance = (distance > 0.0f) ? distance : 0.0f; // 负距离防御
    if (safeDistance <= def.meleeRange)               // 近身档
    {
        return meleeReady ? 0 : -1;                   // 近身招就绪才出
    }
    if (safeDistance <= def.rangedRange)              // 远距档
    {
        return rangedReady ? 1 : -1;                  // 远距招就绪才出
    }
    return -1;                                        // 射程外：继续追击
}

// 招式独立冷却（秒；越界下标夹取）
inline float bossAttackCooldown(BossType type, int attackIndex)
{
    return bossAttackDef(type, attackIndex).cooldownSeconds; // 表驱动
}

// Boss 是否免疫某元素：规格明确免疫冻结与减速；其他元素（含无元素）不在此列
inline bool bossImmuneToElement(AttackElement element)
{
    return element == AttackElement::Slow || element == AttackElement::Freeze; // 两种控制均免疫
}

// Boss 击杀金币奖励：roll 在 [0, 15] 均匀取值 -> 25~40 枚；越界夹取
inline int bossCoinReward(int roll)
{
    const int safeRoll = std::clamp(roll, 0, kBossCoinRolls - 1); // roll 夹到合法区间
    return kBossCoinMin + safeRoll;                               // 25 + roll
}

// Boss 击杀经验奖励：roll 在 [0, 50] 均匀取值 -> 150~200 点；越界夹取
inline int bossXpReward(int roll)
{
    const int safeRoll = std::clamp(roll, 0, kBossXpRolls - 1); // roll 夹到合法区间
    return kBossXpMin + safeRoll;                               // 150 + roll
}

// Boss 击杀回血：round(最大生命 x 百分比 / 100)，夹到 [0, maxHp]。
// 百分比 <= 0 = 关闭；>100 按 100 处理（回血永不超过上限）。
inline int bossKillHeal(int maxHp, float percent)
{
    if (maxHp <= 0 || !(percent > 0.0f)) // 上限异常/关闭：不回血
    {
        return 0; // 无回血
    }
    const float safePercent = std::clamp(percent, 0.0f, 100.0f); // 百分比夹取
    const int heal = static_cast<int>(std::lround( // 四舍五入到整点
        static_cast<float>(maxHp) * safePercent / 100.0f)); // 最大生命 x 百分比
    return std::min(heal, maxHp); // 回血量不超过上限
}

#endif // !__BOSS_H__
