#ifndef __RESPAWN_RULES_H__
#define __RESPAWN_RULES_H__

#include <algorithm> // std::max：金币/等级/经验的非负与最低等级夹取
#include <vector>    // std::vector：升级历史栈（每次升级选择入栈一笔）

// ============================================================
// RespawnRules：死亡惩罚与升级历史栈（批六乙·纯逻辑）
// ============================================================
//
// 本文件把「升级历史栈 + 死亡惩罚数学」收敛成可单测的纯函数，
// PlayScene 只负责把结果落实到局面（金币/等级/经验/法术层数）。
//
// 升级历史栈语义（规格）：
//   - 每次升级三选一「确认选择」时入栈一笔：法术 + 选择后的层数快照；
//   - 死亡且等级 > 1 时出栈最近一笔：该法术层数 -1；
//     层数归 0 -> 该法术自动停止施法、HUD 图标消失（场景侧按层数查询）；
//   - 等级 > 1 时该级经验清零（m_xp = 0）；等级 = 1 时无等级惩罚。
//
// 死亡惩罚（规格）：
//   - 金币 = 金币 / 2（整数除法即向下取整）；
//   - 等级 -1（最低 1）；等级 > 1 时经验清零并出栈丢失最近升级技能；
//   - 史实：历史栈为空（例如池满导致升级没有实际选择）时仍降级，
//     但不丢失技能（防御路径，日志记为「失去技能：无」）。
//
// 全部函数对负值/空指针/越界下标做确定性兜底，不抛异常、不崩溃。
// ============================================================

// 一笔升级选择记录：法术下标 + 选择生效后的层数快照。
// 层数用于日志/核对；出栈时按「当前层数 -1」执行，确保连续死亡也能
// 逐层归还（当前层数可能已因更近的死亡而低于快照）。
struct UpgradeChoiceRecord
{
    int spellIndex = 0; // 法术下标（与 SpellId 强转的整数值一致）
    int levelAfter = 0; // 该次选择后的层数快照（1~5；0 = 防御占位）
};

// 升级选择入栈：每次三选一确认后调用（场景层在层数 +1 之后调用）
inline void pushUpgradeChoice(std::vector<UpgradeChoiceRecord>& history,
                              int spellIndex, int levelAfter)
{
    // 直接追加一笔：不设上限——一次升级一笔，容量与对局时长同阶（可忽略）
    history.push_back(UpgradeChoiceRecord{ spellIndex, levelAfter });
}

// 死亡惩罚结算结果（纯数据；场景据此写回局面与日志）
struct DeathPenaltyResult
{
    int coins = 0;                      // 惩罚后金币（原值 / 2，向下取整）
    int level = 1;                      // 惩罚后等级（最低 1）
    int xp = 0;                         // 惩罚后当前级经验（降级时清零）
    bool levelLost = false;             // 是否发生了等级 -1
    bool skillLost = false;             // 是否实际丢失了一层技能
    int lostSpellIndex = -1;            // 丢失技能的法术下标（-1 = 无）
    int lostSpellLevelBefore = 0;       // 丢失前该法术层数
    int lostSpellLevelAfter = 0;        // 丢失后该法术层数（0 = 已移除）
};

// 统一死亡惩罚入口（HP 归零与落水共用）：
//   coins       当前金币；
//   level       当前等级；
//   xp          当前级已积累经验；
//   history     升级历史栈（等级 > 1 且非空时出栈一笔）；
//   spellLevels 各法术层数数组（长度 spellLevelCount；可为空——仅防御）；
//   spellLevelCount 层数数组长度（越界下标不写、不崩）。
// 返回惩罚结果；不修改 coins/level/xp 的入参（值传递）。
inline DeathPenaltyResult applyDeathPenalty(int coins, int level, int xp,
                                            std::vector<UpgradeChoiceRecord>& history,
                                            int* spellLevels, int spellLevelCount)
{
    DeathPenaltyResult result; // 默认值即「无惩罚」的安全起点

    // 金币减半：先夹到非负，再整数除 2（向下取整；1 金币 -> 0）
    result.coins = std::max(0, coins) / 2;

    // 等级先夹到最低 1（负数/0 输入按 1 处理），经验夹到非负
    const int safeLevel = std::max(1, level); // 合法等级下限
    result.level = safeLevel;                 // 默认保持原等级
    result.xp = std::max(0, xp);              // 默认保持原经验

    if (safeLevel > 1) // 等级 > 1：执行等级惩罚
    {
        result.level = safeLevel - 1; // 等级 -1（最低 1 由上面的夹取保证）
        result.xp = 0;                // 该级经验清零（规格硬要求）
        result.levelLost = true;      // 标记：本次发生了降级

        if (!history.empty()) // 有历史记录：出栈最近一笔升级选择
        {
            const UpgradeChoiceRecord record = history.back(); // 最近一次选择
            history.pop_back();                                // 出栈（该级成长归还）

            // 法术下标合法且层数数组可用：执行「该法术层数 -1」
            if (spellLevels != nullptr &&
                record.spellIndex >= 0 && record.spellIndex < spellLevelCount)
            {
                const int before = std::max(0, spellLevels[record.spellIndex]); // 当前层数（防御夹取）
                const int after = std::max(0, before - 1);                      // 层数 -1（不低于 0）
                spellLevels[record.spellIndex] = after;                         // 写回层数（0 = 移除）

                result.lostSpellIndex = record.spellIndex; // 记录丢失的法术
                result.lostSpellLevelBefore = before;      // 丢失前层数（日志/断言用）
                result.lostSpellLevelAfter = after;        // 丢失后层数
                result.skillLost = (before > 0);           // 只有在真的有层可扣时才算丢失
            }
            // 下标非法：历史已出栈但无技能可扣——按「失去技能：无」处理（防御）
        }
    }

    return result; // 交还惩罚结果（调用方负责写回与日志）
}

#endif // !__RESPAWN_RULES_H__
