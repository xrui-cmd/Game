#ifndef __SEPARATION_H__
#define __SEPARATION_H__

#include <SDL3/SDL.h> // SDL_FRect：AABB 的基本类型
#include <algorithm>  // std::min / std::max：两轴渗透量的计算

// ============================================================
// Separation：AABB 最小分离（MTV）纯函数（批四·体积碰撞）
// ============================================================
//
// 用途：两个轴对齐盒重叠时，计算「把 a 推出、使其脱离 b」的最小平移
// 向量（Minimum Translation Vector）。两处消费：
//   1. 玩家↔怪物重叠兜底（任何来源——击退/瞬移产生的重合按最小分离量
//      把怪物推出，玩家优先不动）；
//   2. 怪物↔怪物轻量软分离（两两重叠各退一半）。
//
// 口径（与引擎 Rect.h 的既有约定对齐）：
//   - 两轴渗透量 = 各自的投影重叠长度；取渗透较小的轴推出（位移最小）；
//   - 推出方向 = 沿该轴「a 中心相对 b 中心」的背离方向（把 a 推远）；
//   - 边界恰好贴合（渗透 <= 0）视为未重叠——与 rectsOverlap「贴合不算
//     碰撞」同一判据；
//   - 完全重合（两中心同点）的零分离向量防御：方向取 +x/+y 确定性回退，
//     绝不产生 0 向量或 NaN（推出去总比原地打转好）；
//   - 渗透轴相等（overlapX == overlapY）时取 X——确定性口径，便于测试。
//
// 精度注记：推出量额外加 kSeparationEpsilon（0.01px）——让分离后的两盒
// 之间留一条亚像素小缝，避免浮点舍入导致「恰好贴合被判为仍重叠」的
// 死循环（相对于最小分离量只多 0.01 像素，观感与语义无差别）。
// ============================================================

// 分离结果：overlapping = false 时 pushX/pushY 恒为 0
struct SeparationResult
{
    bool overlapping = false; // 是否重叠（两轴渗透都 > 0 才算）
    float pushX = 0.0f;       // 把 a 推出 b 所需的水平位移（含方向符号）
    float pushY = 0.0f;       // 把 a 推出 b 所需的垂直位移（含方向符号）
};

// 亚像素分离余量（像素）：见文件头「精度注记」
constexpr float kSeparationEpsilon = 0.01f;

// 计算「把 a 推出 b」的最小分离量。a/b 为世界坐标矩形（左上角 + 宽高）
inline SeparationResult computeSeparation(const SDL_FRect& a, const SDL_FRect& b)
{
    SeparationResult result{}; // 默认：未重叠、零位移

    // 两轴渗透量（投影重叠长度）：min(右缘) - max(左缘) 及其 y 轴对应式。
    // <= 0 表示该轴已分离（含恰好贴合——贴合不算重叠）
    const float overlapX = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x); // 水平渗透
    const float overlapY = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y); // 垂直渗透

    // 任一轴不重叠：两盒未重叠（与 rectsOverlap 同一判据），原样返回
    if (overlapX <= 0.0f || overlapY <= 0.0f)
    {
        return result; // overlapping 保持 false、位移保持 0
    }

    result.overlapping = true; // 两轴都有渗透：确认重叠

    // 取渗透较小的轴推出（最小平移）；相等时取 X（确定性口径）
    if (overlapX <= overlapY)
    {
        // 沿 X 推出：方向 = a 中心背离 b 中心；中心重合（零向量）回退 +x。
        // 推出量 = 渗透 + 亚像素余量（分离后留缝，防浮点死循环）
        const float centerDiff = (a.x + a.w * 0.5f) - (b.x + b.w * 0.5f); // 两中心横向差
        result.pushX = (centerDiff >= 0.0f)
            ? overlapX + kSeparationEpsilon  // a 在右（或重合）：向右推
            : -(overlapX + kSeparationEpsilon); // a 在左：向左推
        result.pushY = 0.0f; // 单轴推出：另一轴不动
    }
    else
    {
        // 沿 Y 推出：方向 = a 中心背离 b 中心；中心重合（零向量）回退 +y
        const float centerDiff = (a.y + a.h * 0.5f) - (b.y + b.h * 0.5f); // 两中心纵向差
        result.pushY = (centerDiff >= 0.0f)
            ? overlapY + kSeparationEpsilon  // a 在下（或重合）：向下推
            : -(overlapY + kSeparationEpsilon); // a 在上：向上推
        result.pushX = 0.0f; // 单轴推出：另一轴不动
    }
    return result; // 交调用方（推出/各退一半由消费侧决定）
}

#endif // !__SEPARATION_H__
