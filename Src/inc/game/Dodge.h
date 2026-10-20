#ifndef __DODGE_H__
#define __DODGE_H__

#include <algorithm> // std::clamp：把进度/冷却夹到合法区间
#include <cmath>     // std::sqrt：方向向量求长（归一化）

// ============================================================
// DodgeRules：角色闪避（冲刺）的纯逻辑规则（M3·丙）
// ============================================================
//
// 设计目标（测试口径与运行时共享同一套数学，见 tests/dodge_test.cpp）：
//   1. 时长与距离：冲刺历时 kDurationSeconds（0.18s）、总位移
//      kDistancePixels（120px）——速度曲线「快起慢收」但积分为精确 120；
//   2. 冷却：kCooldownSeconds（1.2s）——从按下成功的那一步起计时，
//      冷却未满不可再次冲刺；
//   3. 方向选择：移动输入（WASD 合成）优先、无输入时取「玩家中心 -> 鼠标」，
//      两者都退化时沿用玩家当前朝向（绝不产生 NaN）；
//   4. 禁触发条件：仅 Playing、未冻结、未处于冲刺中、冷却已就绪——
//      四条判定集中在此，场景与玩家实体都消费同一函数。
//
// 位移曲线说明（distanceAtProgress）：
//   进度 p∈[0,1] 的累计距离 = D x (1 - (1-p)^2)（二次缓出）。
//   对 p 求导得到瞬时速度 = 2D(1-p)/T：p=0 时 2D/T（约 1333px/s，快起），
//   p=1 时 0（慢收），积分恰为 D = 120px。逐固定步取「本步终点累计距离 -
//   起点累计距离」作为位移量，因此无论步长如何细分，总距离都精确收敛到 120。
namespace DodgeRules
{
// —— 冲刺最终参数（代码常量；改数值须同步注释与测试期望）——
constexpr float kDurationSeconds = 0.18f; // 冲刺持续时长（秒；规格约 0.18）
constexpr float kDistancePixels = 120.0f; // 冲刺总距离（像素；规格约 120）
constexpr float kCooldownSeconds = 1.2f;  // 冲刺冷却（秒；规格 1.2）

// 方向退化阈值：向量长度小于它视为「没有方向」（与 Player 既有口径一致）。
// 1e-3 远小于任何真实输入/瞄准距离，只会命中「完全重合」这类退化输入
constexpr float kDirectionEpsilon = 0.001f; // 方向退化阈值（向量长度，像素）

// 冲刺方向（单位向量）：不带 SDL/引擎依赖，便于纯逻辑测试直接构造
struct DodgeDirection
{
    float x = 1.0f; // 水平分量（单位向量；初始朝右）
    float y = 0.0f; // 垂直分量（屏幕坐标系，向下为正）
};

// 求向量长度：方向归一化与退化判定的公共小工具
inline float vectorLength(float x, float y)
{
    return std::sqrt(x * x + y * y); // 欧氏长度（像素）
}

// 归一化方向；长度为 0 时回退到给定朝向；朝向也退化时兜底朝右。
// 用于「冲刺方向」的最终收敛：任何输入都不会产生除零/NaN
inline DodgeDirection normalizeOrFallback(float x, float y,
                                           float fallbackX, float fallbackY)
{
    const float length = vectorLength(x, y); // 候选方向长度
    if (length > kDirectionEpsilon) // 候选方向有效：直接归一化
    {
        return DodgeDirection{ x / length, y / length }; // 单位向量（保留输入朝向）
    }

    const float fallbackLength = vectorLength(fallbackX, fallbackY); // 回退方向长度
    if (fallbackLength > kDirectionEpsilon) // 回退方向有效：归一化后返回
    {
        return DodgeDirection{ fallbackX / fallbackLength, // 回退方向 x 分量
                               fallbackY / fallbackLength }; // 回退方向 y 分量
    }

    return DodgeDirection{ 1.0f, 0.0f }; // 全部退化：确定性朝右（绝不 NaN）
}

// 选择冲刺方向（规格 2）：
//   - 有移动输入：WASD 合成方向归一化（斜向也是单位长度，不再有 1.41 倍）；
//   - 无移动输入：玩家中心 -> 鼠标方向归一化（aimDir 由场景预先换算好）；
//   - 鼠标与玩家中心重合：沿用当前朝向（facing），朝向也退化则朝右。
// 返回单位向量；所有分支都保证长度 = 1（或确定性兜底）
inline DodgeDirection selectDirection(float moveX, float moveY,
                                      float aimDirX, float aimDirY,
                                      float facingX, float facingY)
{
    const float moveLength = vectorLength(moveX, moveY); // 移动输入合成长度
    if (moveLength > kDirectionEpsilon) // 移动输入优先（规格 2）
    {
        return DodgeDirection{ moveX / moveLength, moveY / moveLength }; // 归一化移动方向
    }

    const float aimLength = vectorLength(aimDirX, aimDirY); // 鼠标瞄准向量长度
    if (aimLength > kDirectionEpsilon) // 无移动输入：朝鼠标方向
    {
        return DodgeDirection{ aimDirX / aimLength, aimDirY / aimLength }; // 归一化瞄准方向
    }

    return normalizeOrFallback(facingX, facingY, 1.0f, 0.0f); // 双重退化：沿用朝向/朝右
}

// 冲刺进度 -> 累计位移（像素）：D x (1 - (1-p)^2) 的二次缓出曲线。
// 性质：p=0 为 0、p=1 恰为 D、单调递增；两端速度 2D/T -> 0（快起慢收）
inline float distanceAtProgress(float progress)
{
    const float p = std::clamp(progress, 0.0f, 1.0f); // 进度夹到 [0,1]
    const float remaining = 1.0f - p;                 // 剩余进度（1-p）
    return kDistancePixels * (1.0f - remaining * remaining); // 累计位移（像素）
}

// 已进行时长 -> 归一化进度 [0,1]：时长常量恒为正，除法安全
inline float progressAtElapsed(float elapsedSeconds)
{
    return std::clamp(elapsedSeconds / kDurationSeconds, 0.0f, 1.0f); // 时长归一化并夹取
}

// 冷却推进：返回递减 dt 后的剩余冷却（秒；夹到 0，不出现负值）。
// 负 dt 视为 0（防御——调用方永远按固定步长传入正数）
inline float cooldownAfter(float remainingSeconds, float deltaSeconds)
{
    const float step = std::max(deltaSeconds, 0.0f); // 防御：负步长不增加冷却剩余
    return std::max(remainingSeconds - step, 0.0f);  // 递减并夹到 0
}

// 冲刺禁触发原因（None = 允许；其余值同时服务日志文案与测试断言）
enum class DodgeStartBlock
{
    None,       // 允许冲刺
    NotPlaying, // 非 Playing 状态（暂停/升级面板/商店/过场）
    Frozen,     // 冻结中（冻结条满 100 的锁定）
    Dashing,    // 已经在冲刺中（同一次冲刺不可重复触发）
    Cooldown    // 冷却未满
};

// 冲刺触发判定（规格 1/3/5 的集中口径）：四个条件全通过才允许。
// 顺序即优先级：状态 > 冻结 > 正在冲刺 > 冷却（日志取第一个不满足项）
inline DodgeStartBlock dodgeStartBlock(bool playing, bool frozen,
                                       bool dashing, float cooldownLeft)
{
    if (!playing) // 规格 1：仅 Playing 状态响应
    {
        return DodgeStartBlock::NotPlaying; // 面板/暂停/过场一律不触发
    }
    if (frozen) // 规格 5：冻结中禁止闪避
    {
        return DodgeStartBlock::Frozen; // 冻结锁定优先于其他判定
    }
    if (dashing) // 同一次冲刺内不可重复触发
    {
        return DodgeStartBlock::Dashing; // 冲刺进行中
    }
    if (cooldownLeft > 0.0f) // 规格 3：冷却中不可触发
    {
        return DodgeStartBlock::Cooldown; // 冷却剩余 > 0
    }
    return DodgeStartBlock::None; // 全部条件满足：允许冲刺
}

// 布尔便捷入口：供玩家实体/场景读写更直观
inline bool canStartDodge(bool playing, bool frozen,
                          bool dashing, float cooldownLeft)
{
    return dodgeStartBlock(playing, frozen, dashing, cooldownLeft) ==
           DodgeStartBlock::None; // 无阻塞原因 = 可冲刺
}

} // namespace DodgeRules

#endif // !__DODGE_H__
