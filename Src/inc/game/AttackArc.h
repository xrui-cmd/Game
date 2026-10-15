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

#endif // !__ATTACKARC_H__
