#ifndef __MONSTER_LEVEL_H__
#define __MONSTER_LEVEL_H__

#include "game/Zone.h" // ZoneId / ZoneDef / zoneDef：区等级区间与神龛坐标的唯一来源
#include <algorithm>   // std::clamp / std::max：等级/数值的边界夹取与防御
#include <cmath>       // std::sqrt / std::lround：距离归一与四舍五入

// ============================================================
// MonsterLevel：怪物等级与等级数值缩放（批六丙·纯逻辑）
// ============================================================
//
// 本文件把「出生点 -> 怪物等级」与「等级 -> 生命/伤害/经验/金币」收敛为
// 可单测的纯函数，场景与实体只负责调用并落实结果：
//
// 1. 等级梯度（规格口径）：
//    - 草地 1~20：level = 1 + round(19 x clamp(dist / grassMax, 0, 1)) + jitter；
//    - 雪山 21~30：level = 21 + round(9 x clamp(dist / snowMax, 0, 1)) + jitter；
//    - dist = 出生点 -> 本区神龛（= 区表复活点）的欧氏距离；
//    - jitter 取 -1..+1（整数），避免同距离怪物等级完全一致（完美圆环感）；
//    - 最终结果夹回本区等级区间（抖动不越区，规格边界要求）。
//
// 2. 区最远距离（grassMax / snowMax）：
//    - 以默认 8x4 世界（12288x6144）的区边界预计算，取区内距神龛最远的角：
//        grassMax = sqrt(3072^2 + 3072^2) = 4344.464（草地区 0..6144 x 0..6144，
//                   神龛 3072,3072，四角等距）；
//        snowMax  = sqrt(5376^2 + 3072^2) = 6191.814（雪山区 6144..12288 x 0..6144，
//                   神龛 6912,3072，最远角为东侧上下角 (12288,0)/(12288,6144)）。
//    - 自定义小世界仍沿用同一归一化分母（神龛坐标固定；最终 clamp 保证不越区），
//      等级分布会随区实际尺寸略变，但区间语义不变（报告口径）。
//
// 3. 数值缩放（初稿，调平表见总案）：
//    - 生命 = round(基础生命 x (1 + 0.10 x (lv-1)))；
//    - 攻击伤害 = round(基础伤害 x (1 + 0.06 x (lv-1)))；
//    - 移速不参与等级缩放（沿用既有难度成长曲线）；
//    - 宝石经验 = round(10 x (1 + 0.10 x (lv-1)))；
//    - 金币数量 = 变体基础 + floor((lv-1) / 10)（每 10 级多 1 枚）。
//
// 4. 全部函数对异常输入（越界等级/负基础值/零分母/越界 jitter）做确定性
//    夹取，不抛异常、不崩溃；等级 1 的所有缩放恒等于基础值（规格边界）。
// ============================================================

// ---- 等级缩放系数（初稿；调平只改这三个常量，公式在下方函数中）----
inline constexpr float kMonsterHpPerLevel = 0.10f;     // 每级生命成长：+10% 基础值
inline constexpr float kMonsterDamagePerLevel = 0.06f; // 每级伤害成长：+6% 基础值
inline constexpr float kMonsterXpPerLevel = 0.10f;     // 每级经验成长：+10% 基础值

// ---- 区最远距离（默认 8x4 世界；推导见文件头第 2 条）----
inline constexpr float kMonsterGrassMaxDistance = 4344.464f; // 草地区到神龛最远角距离（像素）
inline constexpr float kMonsterSnowMaxDistance = 6191.814f;  // 雪山区到神龛最远角距离（像素）

// 取本区的等级归一化分母：雪山用雪区最远角，其余（含越界/哨兵）回退草地。
// 纯查询——决定「距离 -> 等级」曲线的横向尺度（同一距离在雪区占比更小）
inline float monsterZoneMaxDistance(ZoneId zone)
{
    // 雪山：东侧大区（区宽 6144、神龛偏西），最远角距离更大
    if (zone == ZoneId::Snow)
    {
        return kMonsterSnowMaxDistance; // 雪区归一化分母
    }
    return kMonsterGrassMaxDistance; // 草地（及防御性越界）归一化分母
}

// 出生点 -> 怪物等级（规格主入口）：
//   zone   出生点所属区（场景经 zoneAtWorld 判定后传入）；
//   x / y  出生点世界坐标（怪物中心）；
//   jitter 等级抖动（-1..+1；越界值夹回该区间）。
// 返回等级已夹在本区 [levelMin, levelMax] 内——抖动或浮点边界都不越区。
inline int monsterLevelForSpawn(ZoneId zone, float x, float y, int jitter)
{
    const ZoneDef& def = zoneDef(zone); // 本区定义：等级区间 + 神龛（复活点）坐标

    // 出生点到本区神龛的欧氏距离：神龛与区表复活点同源（Shrine.h 亦引用它）
    const float dx = x - def.respawnX;               // 横向差（像素）
    const float dy = y - def.respawnY;               // 纵向差（像素）
    const float distance = std::sqrt(dx * dx + dy * dy); // 欧氏距离（像素）

    // 归一化分母：常量恒为正；仍做防御（未来改表为 0 时不产生除零/NaN）
    const float rawMax = monsterZoneMaxDistance(zone); // 本区最远距离
    const float maxDistance = (rawMax > 0.0f) ? rawMax : 1.0f; // 零/负值防御

    // 距离占比夹到 [0,1]：神龛附近 0（最低级）、区最远角 1（最高级）
    const float t = std::clamp(distance / maxDistance, 0.0f, 1.0f); // 归一化位置

    // 等级跨度：草地 19（1 -> 20）、雪山 9（21 -> 30）——由区表推导，不重复写死
    const int span = def.levelMax - def.levelMin; // 区间跨度（含端点差）

    // 基础等级 = 区下限 + round(跨度 x 归一化距离)：四舍五入（.5 向上）
    const int baseLevel = def.levelMin +
                          static_cast<int>(std::lround(static_cast<float>(span) * t));

    // 抖动夹到 -1..+1：调用方误传越界值也不会把等级拉出设计范围
    const int safeJitter = std::clamp(jitter, -1, 1); // 合法抖动区间

    // 最终夹回本区区间：近神龛 -1 不低于下限、最远角 +1 不超过上限
    return std::clamp(baseLevel + safeJitter, def.levelMin, def.levelMax);
}

// 等级 -> 生命上限：round(基础生命 x (1 + 10% x (lv-1)))，至少 1 点。
// 等级 1 时缩放系数恰为 1.0（基础生命原值）；负基础值按 0 防御。
inline int scaledMonsterHp(int baseHp, int level)
{
    const int safeLevel = std::max(1, level);   // 等级下限 1（0/负值按 1 处理）
    const int safeBase = std::max(0, baseHp);   // 基础生命非负
    const float scale = 1.0f + kMonsterHpPerLevel * static_cast<float>(safeLevel - 1); // 等级缩放系数
    const long scaled = std::lround(static_cast<float>(safeBase) * scale); // 四舍五入后的生命
    return std::max(1, static_cast<int>(scaled)); // 出生至少 1 点（与既有口径一致）
}

// 等级 -> 攻击伤害：round(基础伤害 x (1 + 6% x (lv-1)))。
// 等级 1 时等于 config 的 monster_damage；负基础值按 0 防御。
inline int scaledMonsterDamage(int baseDamage, int level)
{
    const int safeLevel = std::max(1, level);         // 等级下限 1
    const int safeBase = std::max(0, baseDamage);     // 基础伤害非负
    const float scale = 1.0f + kMonsterDamagePerLevel * static_cast<float>(safeLevel - 1); // 等级缩放系数
    const long scaled = std::lround(static_cast<float>(safeBase) * scale); // 四舍五入后的伤害
    return std::max(0, static_cast<int>(scaled));     // 伤害非负（配置 0 = 无伤，保留语义）
}

// 等级 -> 单颗经验宝石经验：round(基础经验 x (1 + 10% x (lv-1)))。
// 基础值由场景的 kGemXpValue（10）传入；等级 1 时与原口径完全一致。
inline int monsterXpValue(int baseXp, int level)
{
    const int safeLevel = std::max(1, level);     // 等级下限 1
    const int safeBase = std::max(0, baseXp);     // 基础经验非负
    const float scale = 1.0f + kMonsterXpPerLevel * static_cast<float>(safeLevel - 1); // 等级缩放系数
    const long scaled = std::lround(static_cast<float>(safeBase) * scale); // 四舍五入后的经验
    return std::max(0, static_cast<int>(scaled)); // 经验非负
}

// 等级 -> 击杀金币数量：变体基础 + floor((lv-1) / 10)。
// 即 1~10 级 +0、11~20 级 +1、21~30 级 +2；整数除法对非负值即向下取整。
inline int monsterCoinDrop(int baseCoins, int level)
{
    const int safeLevel = std::max(1, level);   // 等级下限 1（低等级不加成）
    const int safeBase = std::max(0, baseCoins); // 变体基础枚数非负
    return safeBase + (safeLevel - 1) / 10;      // 每满 10 级多掉 1 枚
}

#endif // !__MONSTER_LEVEL_H__
