#ifndef __ELEMENTSTATUS_H__
#define __ELEMENTSTATUS_H__

#include <algorithm> // std::clamp / std::max：乘区与计时夹取

// ============================================================
// ElementStatus：玩家身上的冰霜元素状态（批六丁）
// ============================================================
//
// 两个互相独立的纯逻辑小类（header-only，不依赖 SDL，可单测）：
//   1. SlowStatus —— 减速计时：
//      - 冰史莱姆命中：乘区 x0.8、持续 3s；
//      - 重复命中「刷新时长、不叠乘」：新乘区覆盖旧乘区、倒计时重置为满；
//      - 与雪区常驻 x0.85 的乘算是场景层职责（本类只给一个乘区）；
//   2. FreezeMeter —— 冻结条：
//      - 0~100；霜怪命中累积（霜哥布林 +25 / 霜骷髅 +20）；
//      - 满 100 -> 冻结 1.5s（玩家不可移动/不可攻击由场景消费）；
//      - 不挨打时每秒衰减 10；
//      - 冻结结束清零，并给 2s「抗冻缓冲」——缓冲期内不再累积；
//      - HUD 在 value > 0 或冻结中显示。
//
// 计时单位：秒；所有 update 入参为固定步长。dt <= 0 视为异常调用，
// 不推进任何计时（防御，不产生负时长）。
// ============================================================

// 减速状态：单乘区 + 剩余时长（刷新覆盖，不叠乘）
class SlowStatus
{
public:
    // 施加减速：factor 夹到 [0,1]（0 = 完全停住；>1 是加速误用，夹回 1），
    // seconds <= 0 忽略（不产生状态）。重复施加 = 覆盖乘区并刷新时长。
    void apply(float factor, float seconds)
    {
        if (!(seconds > 0.0f)) // 非正时长（含 NaN）：调用方错误，忽略
        {
            return; // 不改变当前状态
        }
        m_factor = std::clamp(factor, 0.0f, 1.0f); // 乘区夹取（负值按 0，超 1 按 1）
        m_left = seconds; // 剩余时长重置为满值（重复命中刷新、不叠加）
    }

    // 推进计时：返回 true 表示「本步刚好到期」（场景据此恢复移速并打日志）。
    // 到期时乘区回 1、剩余归零；未到期返回 false。
    bool update(float dt)
    {
        if (!(dt > 0.0f)) // 非正步长：不推进（防御异常调用）
        {
            return false; // 本步无状态变化
        }
        if (m_left <= 0.0f) // 本来就没有减速
        {
            return false; // 无需处理
        }
        m_left = std::max(m_left - dt, 0.0f); // 倒计时递减并夹到 0
        if (m_left > 0.0f) // 还没到期
        {
            return false; // 乘区继续生效
        }
        m_factor = 1.0f; // 到期：乘区恢复原速
        return true; // 通知调用方「本步刚到期」
    }

    // 当前速度乘区：无减速时恒 1（场景移速乘算直接消费）
    float moveMultiplier() const
    {
        return (m_left > 0.0f) ? m_factor : 1.0f; // 仅有效期内返回乘区
    }

    // 剩余时长（秒；冒烟/单测查询）
    float remaining() const { return m_left; }

    // 是否生效中（HUD 小图标显示判据）
    bool active() const { return m_left > 0.0f; }

    // 复位（死亡复活 / 重开）：清除乘区与计时
    void reset()
    {
        m_factor = 1.0f; // 乘区回原速
        m_left = 0.0f;   // 计时归零
    }

private:
    float m_factor = 1.0f; // 减速乘区（0~1；1 = 无减速）
    float m_left = 0.0f;   // 剩余时长（秒；<=0 = 无减速）
};

// 单步推进结果：两个上升沿（冻结开始 / 冻结结束）供场景打日志与切换玩家状态
struct FreezeTickResult
{
    bool freezeStarted = false; // 本步冻结刚触发（累积满 100）
    bool freezeEnded = false;   // 本步冻结刚结束（1.5s 走完，清条 + 进入缓冲）
};

// 冻结条：累积 / 衰减 / 阈值冻结 / 冻结结束清零 + 抗冻缓冲
class FreezeMeter
{
public:
    static constexpr int kMaxValue = 100;          // 冻结条上限（满值触发冻结）
    static constexpr float kFreezeSeconds = 1.5f;  // 冻结时长（秒）
    static constexpr float kDecayPerSecond = 10.0f; // 不挨打时每秒衰减量
    static constexpr float kImmunitySeconds = 2.0f; // 冻结结束后的抗冻缓冲（秒）

    // 累积冻结条：amount <= 0 忽略；冻结中 / 抗冻缓冲中不累积。
    // 达到 100 -> 立即冻结 1.5s，并把值夹在上限（HUD 满条）。
    FreezeTickResult add(int amount)
    {
        FreezeTickResult result{}; // 默认：无上升沿
        if (amount <= 0) // 非正累积：调用方错误，忽略
        {
            return result; // 不改变状态
        }
        if (m_frozen || m_immunityLeft > 0.0f) // 冻结中或缓冲期：不再累积
        {
            return result; // 保持现状（缓冲期「不再累积」的规格口径）
        }

        m_value = std::min(m_value + static_cast<float>(amount), // 加上本次点数
                           static_cast<float>(kMaxValue)); // 夹到 100 上限
        if (m_value >= static_cast<float>(kMaxValue)) // 达到阈值：触发冻结
        {
            m_value = static_cast<float>(kMaxValue); // 满条显示
            m_frozen = true;                         // 进入冻结（场景据此锁玩家）
            m_freezeLeft = kFreezeSeconds;           // 冻结倒计时置满
            result.freezeStarted = true;             // 上升沿：冻结开始
        }
        return result; // 交还本步结果
    }

    // 推进计时：冻结中 = 倒计时；冻结结束 -> 清零 + 2s 抗冻缓冲；
    // 非冻结且非缓冲 = 每秒衰减 10（「不挨打时衰减」）。
    FreezeTickResult update(float dt)
    {
        FreezeTickResult result{}; // 默认：无上升沿
        if (!(dt > 0.0f)) // 非正步长：不推进（防御）
        {
            return result; // 状态不变
        }

        if (m_frozen) // 冻结中：只走冻结倒计时，不衰减
        {
            if (dt < m_freezeLeft) // 本步走不完冻结
            {
                m_freezeLeft -= dt; // 扣减剩余冻结时长
                return result;      // 尚未结束
            }

            const float overflow = dt - m_freezeLeft; // 越过冻结终点的剩余步长
            m_frozen = false;                         // 解除冻结
            m_freezeLeft = 0.0f;                      // 冻结倒计时归零
            m_value = 0.0f;                           // 冻结结束清零（规格）
            m_immunityLeft = kImmunitySeconds;        // 进入 2s 抗冻缓冲
            // 大步长越过终点时把剩余时间继续计入缓冲（避免长帧后缓冲白白多给）
            m_immunityLeft = std::max(m_immunityLeft - overflow, 0.0f); // 缓冲扣减
            result.freezeEnded = true;                // 上升沿：冻结结束
            return result;                            // 本步只处理冻结结束
        }

        if (m_immunityLeft > 0.0f) // 抗冻缓冲：只走缓冲计时
        {
            m_immunityLeft = std::max(m_immunityLeft - dt, 0.0f); // 缓冲递减
            return result; // 缓冲期不累积、不衰减（值已为 0）
        }

        // 普通状态：不挨打时每秒衰减 10（连续衰减，固定步与帧率无关）
        m_value = std::max(m_value - kDecayPerSecond * dt, 0.0f); // 衰减并夹到 0
        return result; // 无上升沿
    }

    // 当前冻结条数值（0~100；HUD 条与单测查询）
    float value() const { return m_value; }

    // 当前值 / 上限（HUD 条按比例填充；上限为 0 防御不在此类发生）
    float value01() const { return m_value / static_cast<float>(kMaxValue); }

    // 是否冻结中（玩家移动/攻击锁定判据）
    bool isFrozen() const { return m_frozen; }

    // 冻结剩余时长（秒；HUD/日志展示）
    float freezeLeft() const { return m_freezeLeft; }

    // 抗冻缓冲剩余时长（秒；>0 = 不累积）
    float immunityLeft() const { return m_immunityLeft; }

    // 是否处于抗冻缓冲（HUD/日志查询）
    bool isImmune() const { return m_immunityLeft > 0.0f; }

    // 复位（死亡复活 / 重开）：清空全部状态
    void reset()
    {
        m_value = 0.0f;        // 冻结条清零
        m_frozen = false;      // 解除冻结
        m_freezeLeft = 0.0f;   // 冻结计时归零
        m_immunityLeft = 0.0f; // 缓冲清零
    }

private:
    float m_value = 0.0f;        // 当前冻结条（0~100）
    bool m_frozen = false;       // 是否冻结中
    float m_freezeLeft = 0.0f;   // 冻结剩余时长（秒）
    float m_immunityLeft = 0.0f; // 抗冻缓冲剩余时长（秒）
};

#endif // !__ELEMENTSTATUS_H__
