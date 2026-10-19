#ifndef __SHRINE_H__
#define __SHRINE_H__

#include "game/Zone.h" // kZoneDefs：草地/雪山复活点坐标的唯一来源（神龛与区表同源）
#include <cstddef>     // std::size_t：数组遍历下标的原生类型（与其余表驱动代码一致）

// ============================================================
// Shrine：复活点（神龛）表与纯查询（批六乙·复活流）
// ============================================================
//
// 本文件只负责「数据 + 可单测的纯逻辑」，不持有纹理、不碰渲染：
//   1. 神龛表：草地中心（默认激活）/ 雪山靠草地一侧（默认未激活）；
//      坐标直接引用 Zone 表（kZoneDefs）——神龛与分区复活点永远同源；
//   2. 激活状态是「本局运行时状态」：由 PlayScene 以 bool 数组持有，
//      R 重开/进入新对局时经 resetShrineActivation 复位；
//   3. 查询函数：
//      - nearestActiveShrine：死亡地点 -> 最近已激活神龛（复活落点）；
//      - nearestInactiveShrineInRange：玩家 -> 范围内最近的未激活神龛
//        （「按 E 激活」提示的判定）。
//   全部函数对空指针/越界输入给出确定性兜底（不崩、结果可预测）。
//
// 交互半径 64px（规格值，代码常量）：玩家中心与神龛中心距离 <= 64 时
// 显示提示；激活键走 config 的 bind_activate（默认 E）。
// ============================================================

// 神龛下标：顺序即表顺序（草地在前——世界西侧；哨兵 Count 供防越界）
enum class ShrineId
{
    Grass = 0, // 草地神龛（草地区中心，默认激活/发亮）
    Snow = 1,  // 雪山神龛（靠草地一侧，默认未激活/灰暗）
    Count      // 哨兵：神龛数（= 2）；不是真实神龛
};

// 神龛数量常量：激活状态数组、循环边界与越界防御的统一口径
inline constexpr int kShrineCount = static_cast<int>(ShrineId::Count); // 2

// 单个神龛的静态定义（坐标、默认状态、日志显示名）
struct ShrineDef
{
    const char* name;     // 神龛名（复活日志「复活于（xxx）」显示）
    float x;              // 神龛中心世界坐标 x（像素）
    float y;              // 神龛中心世界坐标 y（像素）
    bool initiallyActive; // 本局初始是否已激活（草地 true / 雪山 false）
};

// 神龛表：坐标引用 Zone 表（区表即复活点坐标的唯一事实来源）。
// 草地 = 草地区中心（同时是出生点）；雪山 = 靠草地一侧中部。
inline constexpr ShrineDef kShrineDefs[kShrineCount] = {
    // 名称        世界坐标 x                      世界坐标 y                      默认激活
    { "草地中心", kZoneDefs[0].respawnX, kZoneDefs[0].respawnY, true },  // 草地：默认亮
    { "雪山", kZoneDefs[1].respawnX, kZoneDefs[1].respawnY, false },     // 雪山：默认暗
};

// 激活提示的交互半径（像素）：玩家中心进入 64px 内显示「按 E 激活」
inline constexpr float kShrineInteractRadius = 64.0f;

// 无激活点时的兜底下标：草地（正常流程至少草地保持激活，本常量是防御）
inline constexpr int kShrineFallbackIndex = static_cast<int>(ShrineId::Grass);

// 取神龛定义（防御：异常下标回退第一项——不崩、结果可预测）
inline const ShrineDef& shrineDef(int index)
{
    // 下标合法性检查：负值/超出表尾走回退分支
    if (index < 0 || index >= kShrineCount)
    {
        return kShrineDefs[kShrineFallbackIndex]; // 回退草地神龛（世界默认区）
    }
    return kShrineDefs[index]; // 正常路径：按表下标取定义
}

// 复位全部神龛激活状态（进入新对局 / restart 调用）：
// 草地恢复默认激活、雪山恢复默认未激活——「本局保持、R 重开重置」的落点
inline void resetShrineActivation(bool* active)
{
    // 空指针防御：调用方未提供状态数组时不做任何事
    if (active == nullptr)
    {
        return;
    }

    // 逐项按表里的默认状态写回（草地 true / 雪山 false）
    for (int i = 0; i < kShrineCount; ++i)
    {
        active[i] = kShrineDefs[i].initiallyActive; // 恢复本项默认激活状态
    }
}

// 选取距 (x, y) 最近的已激活神龛；返回表下标，-1 = 没有任何激活点。
// 距离用平方比较（省开方，结果与开方同序）；距离相同保留较小下标
// （确定性：草地优先——世界默认区）。
inline int nearestActiveShrine(const bool* active, float x, float y)
{
    // 空指针防御：没有状态数组就没有可判定的激活点
    if (active == nullptr)
    {
        return -1;
    }

    int bestIndex = -1;              // 最近激活点的表下标（-1 = 尚未找到）
    float bestDistanceSq = 0.0f;     // 最近点的距离平方（找到首个后才有效）
    for (int i = 0; i < kShrineCount; ++i)
    {
        if (!active[i]) // 未激活：不参与最近的竞争
        {
            continue;
        }

        const ShrineDef& def = kShrineDefs[i];          // 本项定义（坐标）
        const float dx = def.x - x;                     // 与查询点的水平差
        const float dy = def.y - y;                     // 与查询点的垂直差
        const float distanceSq = dx * dx + dy * dy;     // 距离平方（不需开方）

        // 首个激活点直接占位；其后严格更近才替换（平距保留先出现者）
        if (bestIndex < 0 || distanceSq < bestDistanceSq)
        {
            bestIndex = i;              // 记录更近的激活点
            bestDistanceSq = distanceSq; // 更新比较基准
        }
    }
    return bestIndex; // -1 = 一个激活点都没有（调用方须兜底）
}

// 选取距 (x, y) 最近、且距离不超过 radius 的「未激活」神龛：
// 返回表下标，-1 = 半径内没有可激活的神龛。用于「按 E 激活」提示。
inline int nearestInactiveShrineInRange(const bool* active, float x, float y, float radius)
{
    // 空指针 / 非正半径防御：没有判定语义，直接返回无候选
    if (active == nullptr || !(radius > 0.0f))
    {
        return -1;
    }

    const float radiusSq = radius * radius; // 半径平方（平方比较省开方）
    int bestIndex = -1;                     // 最近未激活点的表下标（-1 = 无）
    float bestDistanceSq = 0.0f;            // 最近点的距离平方
    for (int i = 0; i < kShrineCount; ++i)
    {
        if (active[i]) // 已激活：不能再激活（不参与提示竞争）
        {
            continue;
        }

        const ShrineDef& def = kShrineDefs[i];      // 本项定义（坐标）
        const float dx = def.x - x;                 // 与玩家的水平差
        const float dy = def.y - y;                 // 与玩家的垂直差
        const float distanceSq = dx * dx + dy * dy; // 距离平方

        if (distanceSq > radiusSq) // 超出交互半径：本项不可交互
        {
            continue;
        }

        // 半径内：取最近者（平距保留较小下标——确定性）
        if (bestIndex < 0 || distanceSq < bestDistanceSq)
        {
            bestIndex = i;               // 记录更近的可激活神龛
            bestDistanceSq = distanceSq;  // 更新比较基准
        }
    }
    return bestIndex; // -1 = 半径内没有未激活神龛
}

#endif // !__SHRINE_H__
