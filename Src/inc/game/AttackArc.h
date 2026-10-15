#ifndef __ATTACKARC_H__
#define __ATTACKARC_H__

#include "engine/math/Vec2.h" // Vec2：圆心/中轴/判定点的类型化坐标
#include <algorithm>          // std::clamp：张角归一化到 [0, 360]
#include <cmath>              // std::cos / std::sqrt：夹角余弦比较

// ============================================================
// AttackArc：横扫扇形判定（game 层，纯逻辑——无 SDL 状态、无 IO）
// ============================================================
//
// 用途：主动攻击改版后的「横扫」命中判定。攻击范围是一个以玩家中心为
// 圆心、鼠标瞄准方向为中轴、总张角为 attack_arc 的扇形（弧），命中弧内
// 所有怪物，各自结算伤害/击退（几何示意，中轴向左）：
//
//          \  扇形（总张角 arcDegrees）
//           \ + 半张角
//     origin ●—————→ 中轴（aimDirection）
//           / - 半张角
//          /
//
// 判定规则（与规格一一对应）：
//   - 距离 ≤ 半径：判定点（怪物中心）到圆心（玩家中心）的距离不超过半径；
//   - 夹角 ≤ 半张角：判定点相对中轴的夹角不超过 arcDegrees 的一半。
//   两条同时满足即命中；「≤」含边界（恰好 60° 的半张角内、恰好 96px
//   半径上算命中）。
//
// 为什么抽成纯函数：判定是玩法正确性的核心（多目标横扫），抽离后可以用
// 合成坐标做确定性单元测试（tests/attack_arc_test.cpp，含角度/半径边界
// 与零向量防御），不依赖 SDL、窗口或任何运行时状态。
//
// 退化与防御（全部有单测覆盖）：
//   - radius <= 0      ：没有可命中的面积，不命中（含 0 与负值）；
//   - 中轴为零向量     ：没有方向可言、无法定义扇形，不命中（保守）；
//   - 判定点与圆心重合 ：方向无定义，约定视为命中（距离 0 必然在弧内）；
//   - 张角超出 [0,360] ：夹取到该区间（负按 0、超整圆按整圆），
//     与「夹角 ≤ 半张角」的原始语义一致；NaN 自然落在不命中侧。
//
// 精度说明：夹角比较用余弦（省去反三角），恰好压在半张角上的点因浮点
// 舍入可能落在阈值两侧——实现带一个极小的余弦容差（kCosEpsilon），
// 把「数学上恰好等于半张角」的边界点稳定判为命中（兑现「≤」语义）。
// ============================================================

// 角度 -> 弧度：π/180（float 精度足够——判定误差对应亚像素量级）
constexpr float kAttackArcDegreesToRadians = 3.14159265358979323846f / 180.0f;

// 夹角余弦容差：恰好压在半张角上的点因开方/乘除舍入可能得到略低于
// cosLimit 的值，容差把它稳定拉回命中侧。1e-4 对应 60° 附近约
// 0.007 度的角度余量——不影响实际玩法（怪物体积远大于该量级），
// 但让「理论边界」的测试获得确定性
constexpr float kAttackArcCosEpsilon = 1e-4f;

// 扫程区间判定的角度容差（弧度）：用途同 kAttackArcCosEpsilon——把
// 「恰好压在扫过区间端点（刀锋角）上」的目标从浮点舍入里稳定判为命中
//（「含边界」语义）。1e-4 rad ≈ 0.0057°，远小于一个固定步的最小扫角
// 增量（约 0.7°），不影响玩法，只让边界测试获得确定性
constexpr float kAttackArcSweepEpsilon = 1e-4f;

// 整圈角度（弧度）：2π。扫程角度规范化（wrap）用
constexpr float kAttackArcTwoPi = 6.28318530717958647692f;

// 是否命中：point 是否落在「origin 为圆心、aimDirection 为中轴、
// radius 为半径、arcDegrees 为总张角」的扇形内。
// aimDirection 不必归一化（内部按点积/模长换算夹角）；point 与 origin
// 是同一坐标系下的两个位置（游戏里用中心点——玩家中心与怪物中心）
inline bool isPointInAttackArc(const Vec2& origin, const Vec2& aimDirection,
                               const Vec2& point, float radius, float arcDegrees)
{
  // 半径非正：扇形没有面积（0 半径退化为一个点，约定不算命中）
  if (!(radius > 0.0f))
  {
    return false; // 防御：负半径/零半径不命中，也避免后续平方比较失去意义
  }

  // 中轴零向量：没有方向就定义不出扇形——保守判定为不命中。
  // 注意用「!(x > 0)」写法：NaN 也会落进这个分支，一并拒绝
  const float aimLengthSq = aimDirection.lengthSquared(); // 中轴长度平方（不开方）
  if (!(aimLengthSq > 0.0f))
  {
    return false; // 零向量/NaN 中轴：不命中
  }

  // 判定点到圆心的位移向量与平方距离（半径比较用平方，省一次开方）
  const Vec2 delta = point - origin;          // 圆心 -> 判定点
  const float distSq = delta.lengthSquared(); // 距离平方

  // 半径判定：距离 ≤ 半径（含边界；平方比较与开方比较等价）
  if (!(distSq <= radius * radius))
  {
    return false; // 半径之外：不命中（更近的边界条件不可能成立）
  }

  // 判定点与圆心重合：方向无定义，但距离为 0 必然「不超过半径」，
  // 几何上普遍约定圆心的点落在任意扇形内——判为命中
  if (!(distSq > 0.0f))
  {
    return true; // 圆心上的点：命中（距离 0 ≤ 半径，夹角视为 0）
  }

  // 张角归一化：[0, 360]（负按 0、超整圆按整圆）。cos 在 [0, 180°]
  // 单调递减，夹取后与「夹角 ≤ 半张角」的原始语义保持一致
  const float safeArc = std::clamp(arcDegrees, 0.0f, 360.0f);

  // 半张角（弧度）与它的余弦阈值：夹角余弦 ≥ cos(半张角) 等价于
  // 夹角 ≤ 半张角（夹角在 [0,180°]，余弦单调递减）
  const float halfArcRad = safeArc * 0.5f * kAttackArcDegreesToRadians;
  const float cosLimit = std::cos(halfArcRad);

  // 夹角余弦：cos = 点积 / （两向量模长之积）。中轴模长已在上面校验非零，
  // delta 模长已校验非零，除法的分母恒为正有限值
  const float cosAngle =
    dot(aimDirection, delta) / (std::sqrt(aimLengthSq) * std::sqrt(distSq));

  // 夹角判定：cosAngle ≥ cosLimit 即夹角 ≤ 半张角。容差抵消「恰好边界」
  // 的浮点舍入误差（见文件头「精度说明」）
  return cosAngle >= cosLimit - kAttackArcCosEpsilon;
}

// ============================================================
// 扫程曲线与刀锋角（本批「手感快修」新增）
// ============================================================
//
// 为什么在这里：横扫的攻击结算从「出手瞬间整弧一次结算」改为「伤害随
// 刀锋逐帧扫过结算」后，判定与渲染必须共享同一条「刀锋现在扫到哪里」
// 的数学表达——以下三个纯函数就是那条表达：
//   - easeOutSwingProgress：扫程进度缓出曲线（快起慢收）；
//   - bladeAngleAt        ：进度 -> 刀锋角（−半张角 -> +半张角）；
//   - isAngleInSweep      ：角度是否落在「本帧扫过」的角区间内。
// 判定侧（PlayScene 逐帧取相邻两个进度对应的刀锋角求区间）与渲染侧
// （Player 用同一进度画剑）调用同一组函数、同一组参数——「画到哪、
// 打到哪」由此成为结构性保证（而不是两处各自维护的近似公式）。
// 三个函数都不含 SDL 依赖，单测见 tests/attack_arc_test.cpp。

// 扫程进度缓出：t01 ∈ [0,1] 的线性进度 -> 缓出后的扫程位置。
// 曲线选 1-(1-t)^2（二次缓出），理由：
//   1. 手感——挥剑是「手腕发力快起动、收尾卸力慢下来」：前段快、
//      后段慢（t=0.5 时已完成 75% 扫程），比匀速更接近真实挥砍节奏；
//   2. 数学简单——一次多项式、严格单调、端点精确（0->0、1->1），
//      单测可直接验证采样值，不依赖三角函数的浮点特性；
//   3. 与移动手感（approach 渐近）同族的「平滑起动/停止」语言，
//      全项目运动观感一致。
// 边界：非正（含 NaN）按 0、达到/超过 1 按 1——越界输入夹到端点，
// 与 AttackArc 其余函数「坏值退化为保守行为」的惯例一致
inline float easeOutSwingProgress(float t01)
{
  // 非正与 NaN 防御：!(t01 > 0) 一并接住 0、负值与 NaN（NaN 比较为假）
  if (!(t01 > 0.0f))
  {
    return 0.0f; // 未开始扫：停在起点（缓出曲线的 0 端）
  }

  // 达到/超过 1（含 +inf）：停在终点（缓出曲线的 1 端）
  if (t01 >= 1.0f)
  {
    return 1.0f;
  }

  // 二次缓出主体：1 - (1-t)^2 = 2t - t²，前快后慢
  const float inv = 1.0f - t01; // 剩余线性进度（1-t）
  return 1.0f - inv * inv;      // 缓出位置：t=0.5 时给 0.75（前段已完成 3/4）
}

// 刀锋角（弧度）：本次扫程在 progress01 处的刀锋相对世界坐标的角度。
// 几何：瞄准角为中轴、总张角 arcDegrees——刀锋从「瞄准角 − 半张角」
// 扫到「瞄准角 + 半张角」，位置由缓出曲线给出：
//   blade = aim − half + arc × easeOut(progress)
// 判定（PlayScene 取相邻两步的刀锋角求「本步扫过区间」）与渲染
// （Player 画剑朝向）共用本函数，保证「画到哪、打到哪」。
// 注意：返回值刻意不做 ±π 归一化——它沿扫程连续增长（可能越 π），
// 供「上一角 -> 本角」的区间判定直接使用（环绕归一化由 isAngleInSweep
// 内部承担，见下）
inline float bladeAngleAt(float aimRad, float progress01, float arcDegrees)
{
  // 张角夹取 [0,360]：负值/NaN 按 0（不扫）、超整圈按整圈——
  // 与 isPointInAttackArc 的张角归一化口径一致
  const float safeArc = (arcDegrees > 0.0f) ? std::min(arcDegrees, 360.0f) : 0.0f;

  // 半张角（弧度）：扫程的一半跨度（arc=120° 时即熟悉的 ±60°）
  const float halfArcRad = safeArc * 0.5f * kAttackArcDegreesToRadians;

  // 缓出位置 [0,1] -> 扫程角度：起点 = aim − half、终点 = aim + half；
  // progress 非数值时缓出函数把它压到 0（保守：当作仍在起点）
  const float sweepPosition = easeOutSwingProgress(progress01); // 缓出后的扫程位置
  return aimRad - halfArcRad + 2.0f * halfArcRad * sweepPosition; // 刀锋角（连续值）
}

// 角度规范化：把任意角（含负值、超整圈）映射到 [0, 2π)。
// 用途：isAngleInSweep 的两处「正向增量」换算。
// 注意：数学上的「正向」= 角度增大方向，在屏幕坐标（y 向下）里表现为
// 顺时针——与扫程从 −半张角 扫到 +半张角 的方向一致
inline float wrapAttackArcAngle(float angleRad)
{
  float wrapped = std::fmod(angleRad, kAttackArcTwoPi); // 一次取模到 (−2π, 2π)
  if (wrapped < 0.0f)
  {
    wrapped += kAttackArcTwoPi; // 负值补一整圈：进入 [0, 2π)
  }
  return wrapped; // NaN 原样穿出——后续比较自然落到「不命中」侧（保守）
}

// 角度是否落在「从 fromRad 沿角度增大方向扫到 toRad」的区间内（含两端）。
// 语义与用法：
//   - 调用方保证 from/to 是同一扫程相邻两步的刀锋角（跨度远小于 2π），
//     区间即「本步刀锋扫过的角区间」；判定刀锋是否扫到了某个目标角；
//   - 实现：把「to 相对 from」与「angle 相对 from」都换算成 [0,2π) 的
//     正向增量再比较——±π 环绕、跨 0（如 170° 扫到 210°）都是同一条
//     逻辑，无需分支特判；
//   - 含边界：恰好压在起点或终点角上的目标算命中（与 AttackArc 的
//     「≤ 含边界」语义一致），容差 kAttackArcSweepEpsilon 吸收舍入误差；
//   - from == to（零宽区间，如扫程刚开始的一步）时只有恰好等于该角的
//     目标命中——语义自然，无需特判。
inline bool isAngleInSweep(float angleRad, float fromRad, float toRad)
{
  // 扫程跨度：to 相对 from 的正向增量（正常调用下是一个小角度）
  const float span = wrapAttackArcAngle(toRad - fromRad);

  // 目标角相对扫程起点的正向增量（即「还需要扫多远才到它」）
  const float delta = wrapAttackArcAngle(angleRad - fromRad);

  // 含边界命中：delta ≤ span。容差把恰好压在端点上的目标从浮点舍入里
  // 拉回命中侧（见常量注释）；NaN 会因比较为假自然落到不命中
  return delta <= span + kAttackArcSweepEpsilon;
}

#endif // !__ATTACKARC_H__
