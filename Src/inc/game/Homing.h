#ifndef __HOMING_H__
#define __HOMING_H__

#include <cmath> // std::sqrt：追踪位移的距离换算

// ============================================================
// Homing：追踪吸附的单步积分纯函数（批四·经验自动吸收 / 金币磁吸）
// ============================================================
//
// 两种掉落物共用同一套「朝玩家飞」的数学：
//   - 经验宝石：击落后立即追踪（起步即高速：700 -> 900px/s 加速封顶）；
//   - 金币磁吸：玩家进入 coin_magnet_radius 后开始追踪（从静止加速）。
// 抽成纯函数的意义：吸收/磁吸的手感参数（起步速度/加速度/封顶）与
// 「抵达不过冲」的边界行为可在单测里确定性验证（tests/）。
//
// 契约：
//   - advanceHomingSpeed：速度以 accel x dt 递增、封顶 maxSpeed；
//     防御非正 dt（不推进）与负速度/负加速度（按 0 处理，吸附速度只有
//     正向语义）——异常输入不产生倒飞；
//   - advanceHomingPoint：从 (x, y) 朝目标移动至多 step 像素；不足一步
//     时恰好落在目标点上（绝不过冲——过冲会让吸附物在目标两侧来回跳）；
//     step <= 0 或已在目标点上时原样返回（零除/NaN 防御）。
// ============================================================

// 追踪速度单步推进：返回新速度（>= 0、不超过 maxSpeed）
inline float advanceHomingSpeed(float speed, float accel, float maxSpeed, float dt)
{
    // 非正步长（暂停/异常调用）：不推进，速度保持（下一有效步继续加速）
    if (dt <= 0.0f)
    {
        return speed;
    }

    // 速度/加速度按 0 下限防御（负值没有吸附语义）；NaN 经比较落 0 侧
    const float safeSpeed = (speed > 0.0f) ? speed : 0.0f; // 当前速度（钳到非负）
    const float safeAccel = (accel > 0.0f) ? accel : 0.0f; // 加速度（钳到非负）
    const float next = safeSpeed + safeAccel * dt;         // 线性加速后的候选速度

    // 封顶：maxSpeed 非正视为无上限（防御性——正常调用方恒传正值）
    return (maxSpeed > 0.0f) ? ((next < maxSpeed) ? next : maxSpeed) : next;
}

// 追踪位移结果（一步之后的新点坐标）
struct HomingPoint
{
    float x = 0.0f; // 新位置 x
    float y = 0.0f; // 新位置 y
};

// 追踪位移单步：从 (x, y) 朝目标移动至多 step 像素（不足一步恰好落点）
inline HomingPoint advanceHomingPoint(float x, float y,
                                      float targetX, float targetY, float step)
{
    const float dx = targetX - x;           // 指向目标的向量 x
    const float dy = targetY - y;           // 向量 y
    const float distSq = dx * dx + dy * dy; // 距离平方（先比较，省一次开方）

    // 无效步长或已在目标点上：原样返回（防御：不产生零除/NaN）
    if (!(step > 0.0f) || distSq <= 0.0f)
    {
        return HomingPoint{ x, y };
    }

    const float dist = std::sqrt(distSq); // 到目标的距离（像素）

    // 一步之内可达：直接落在目标点——严格不过冲（吸附物的位置收敛）
    if (step >= dist)
    {
        return HomingPoint{ targetX, targetY };
    }

    const float nx = dx / dist; // 单位方向 x（距离非零，除法安全）
    const float ny = dy / dist; // 单位方向 y
    return HomingPoint{ x + nx * step, // 沿方向推进 step 像素
                        y + ny * step };
}

#endif // !__HOMING_H__
