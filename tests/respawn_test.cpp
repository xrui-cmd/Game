// ============================================================================
//  respawn_test.cpp — 复活规则单元测试（批六乙）
//
//  被测对象 : Src/inc/game/Shrine.h（神龛表 / 最近激活点 / 交互范围）+
//             Src/inc/game/RespawnRules.h（升级历史栈 / 死亡惩罚数学）
//
//  覆盖场景 : ① 神龛表数值（2 座 / 名称 / 坐标与 Zone 表同源 / 默认激活态）
//             ② 激活状态复位（草地亮、雪山暗）
//             ③ 最近已激活复活点选择（含平距确定性、无激活点防御）
//             ④ 「按 E 激活」范围判定（64px 边界、只选未激活、最近者）
//             ⑤ 死亡惩罚数学（金币减半向下取整 / 最低 1 级 / 经验清零）
//             ⑥ 升级历史栈出栈（层数 -1、归零移除、空栈防御、非法下标）
//             ⑦ 多次死亡连续出栈（逐次归还升级所得技能）
//
//  依赖 : 纯头文件逻辑；Shrine.h 经 Zone.h/World.h 使用 SDL 类型与内联
//  函数（不链接 SDL3 运行时）——任何环境都能确定性运行。
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/Shrine.h"       // 被测：神龛表 / nearestActiveShrine / 交互范围
#include "game/RespawnRules.h" // 被测：升级历史栈 / applyDeathPenalty
#include "game/Spell.h"        // kSpellCount：法术层数数组尺寸

#include <cmath>   // std::fabs：浮点近似断言
#include <cstdio>  // std::printf
#include <cstdlib> // std::system（Windows 控制台切 UTF-8）
#include <cstring> // std::strcmp：神龛名逐字核对
#include <vector>  // std::vector：升级历史栈

namespace
{

int gPass = 0; // 通过的断言数
int gFail = 0; // 失败的断言数（作为 main 返回值）

// 断言：整数相等
void expectInt(const char* label, int got, int expect)
{
    // 实际值与期望值一致才算通过
    const bool ok = (got == expect);
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名 + 期望与实际
    std::printf("  [%s] %s | 期望=%d 实际=%d\n", ok ? "PASS" : "FAIL", label, expect, got);
}

// 断言：浮点近似相等（|got - expect| <= eps）
void expectNear(const char* label, float got, float expect, float eps = 0.01f)
{
    // 偏差不超过容差才算通过
    const bool ok = std::fabs(got - expect) <= eps;
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名 + 期望与实际
    std::printf("  [%s] %s | 期望≈%.3f 实际=%.3f\n", ok ? "PASS" : "FAIL", label, expect, got);
}

// 断言：布尔条件成立
void expectTrue(const char* label, bool value)
{
    // 条件为 true 才算通过
    const bool ok = value;
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
}

// 断言：字符串相等
void expectStr(const char* label, const char* got, const char* expect)
{
    // 逐字节比较（神龛名为固定文案）
    const bool ok = (std::strcmp(got, expect) == 0);
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名 + 期望与实际
    std::printf("  [%s] %s | 期望=\"%s\" 实际=\"%s\"\n", ok ? "PASS" : "FAIL", label, expect, got);
}

void beginCase(int idx, const char* title)
{
    // 用例标题分隔（与其余测试同一输出风格）
    std::printf("\n[CASE %d] %s\n", idx, title);
}

void endCase(int idx, int passBefore, int failBefore)
{
    // 本用例新增的通过/失败数
    const int passed = gPass - passBefore;
    const int failed = gFail - failBefore;
    std::printf("  ---- CASE %d 小结: %d/%d 通过%s\n", idx, passed, passed + failed,
                failed ? "  <<<< 存在失败" : "");
}

// 构造一份标准法术层数数组（全 0），供惩罚用例复用
void clearSpellLevels(int* levels)
{
    // 逐系清零：数组长度 = 法术池张数
    for (int i = 0; i < kSpellCount; ++i)
    {
        levels[i] = 0; // 本系未学
    }
}

} // namespace

int main()
{
#if defined(_WIN32)
    // 控制台代码页切到 UTF-8：中文输出不乱码（失败也不影响测试）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" 复活规则单元测试 —— 神龛选择 / 惩罚数学 / 升级历史栈（批六乙）\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 神龛表数值（名称 / 坐标 / 默认激活）
    // ------------------------------------------------------------------
    beginCase(1, "神龛表：草地中心默认激活 / 雪山默认未激活");
    int p0 = gPass, f0 = gFail;
    {
        expectInt("kShrineCount = 2", kShrineCount, 2); // 两座神龛
        expectStr("草地神龛名", kShrineDefs[0].name, "草地中心"); // 复活日志显示名
        expectStr("雪山神龛名", kShrineDefs[1].name, "雪山");     // 复活日志显示名

        // 坐标与 Zone 表同源（不复制魔法数字）
        expectNear("草地神龛 x = Zone 复活点 x", kShrineDefs[0].x, kZoneDefs[0].respawnX);
        expectNear("草地神龛 y = Zone 复活点 y", kShrineDefs[0].y, kZoneDefs[0].respawnY);
        expectNear("雪山神龛 x = Zone 复活点 x", kShrineDefs[1].x, kZoneDefs[1].respawnX);
        expectNear("雪山神龛 y = Zone 复活点 y", kShrineDefs[1].y, kZoneDefs[1].respawnY);

        // 规格坐标（默认世界）
        expectNear("草地神龛 = (3072,3072)", kShrineDefs[0].x, 3072.0f);
        expectNear("雪山神龛 = (6912,3072)", kShrineDefs[1].x, 6912.0f);

        // 默认激活态：草地亮 / 雪山暗
        expectTrue("草地默认激活（亮）", kShrineDefs[0].initiallyActive);
        expectTrue("雪山默认未激活（暗）", !kShrineDefs[1].initiallyActive);

        // 交互半径是规格值 64px
        expectNear("交互半径 = 64px", kShrineInteractRadius, 64.0f);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 激活状态复位（R 重开/新对局：草地亮、雪山暗）
    // ------------------------------------------------------------------
    beginCase(2, "resetShrineActivation：草地 true / 雪山 false");
    p0 = gPass; f0 = gFail;
    {
        bool active[kShrineCount] = { false, true }; // 人为打乱：草地暗、雪山亮
        resetShrineActivation(active);               // 复位到表默认
        expectTrue("复位后草地激活", active[0]);      // 草地 true
        expectTrue("复位后雪山未激活", !active[1]);   // 雪山 false

        resetShrineActivation(nullptr); // 空指针防御：不崩
        expectTrue("resetShrineActivation(nullptr) 不崩", true); // 走到这里即通过
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 最近已激活复活点选择
    // ------------------------------------------------------------------
    beginCase(3, "nearestActiveShrine：最近者 / 平距 / 无激活点防御");
    p0 = gPass; f0 = gFail;
    {
        const float grassX = kShrineDefs[0].x; // 草地 x
        const float snowX = kShrineDefs[1].x;  // 雪山 x
        const float y = kShrineDefs[0].y;      // 两座神龛同一 y

        bool grassOnly[kShrineCount] = { true, false }; // 只有草地激活
        expectInt("只有草地激活：死亡点靠近雪山仍回草地",
                  nearestActiveShrine(grassOnly, snowX, y), 0); // 雪山未激活不参与
        expectInt("只有草地激活：死亡点靠近草地 -> 草地",
                  nearestActiveShrine(grassOnly, grassX, y), 0);

        bool both[kShrineCount] = { true, true }; // 两座都激活
        expectInt("两座激活：死亡点靠近雪山 -> 雪山",
                  nearestActiveShrine(both, snowX, y), 1);
        expectInt("两座激活：死亡点靠近草地 -> 草地",
                  nearestActiveShrine(both, grassX, y), 0);

        // 中点平距：保留较小下标（草地）——确定性
        const float midX = (grassX + snowX) * 0.5f; // 4992
        expectInt("两座激活：中点平距 -> 草地（下标小者优先）",
                  nearestActiveShrine(both, midX, y), 0);

        bool none[kShrineCount] = { false, false }; // 全未激活
        expectInt("无激活点 -> -1", nearestActiveShrine(none, grassX, y), -1);
        expectInt("active = nullptr -> -1", nearestActiveShrine(nullptr, grassX, y), -1);
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 激活提示范围（64px）
    // ------------------------------------------------------------------
    beginCase(4, "nearestInactiveShrineInRange：64px 边界 / 只选未激活 / 最近者");
    p0 = gPass; f0 = gFail;
    {
        const float snowX = kShrineDefs[1].x; // 雪山神龛 x
        const float y = kShrineDefs[1].y;     // 雪山神龛 y

        bool defaultState[kShrineCount] = { true, false }; // 草地亮 / 雪山暗
        expectInt("站在雪山神龛上 -> 可激活雪山",
                  nearestInactiveShrineInRange(defaultState, snowX, y, kShrineInteractRadius), 1);
        expectInt("距雪山 32px -> 可激活",
                  nearestInactiveShrineInRange(defaultState, snowX - 32.0f, y, kShrineInteractRadius), 1);
        expectInt("距雪山恰好 64px -> 边界包含",
                  nearestInactiveShrineInRange(defaultState, snowX - 64.0f, y, kShrineInteractRadius), 1);
        expectInt("距雪山 65px -> 超出范围",
                  nearestInactiveShrineInRange(defaultState, snowX - 65.0f, y, kShrineInteractRadius), -1);

        // 草地已激活：即使站在草地附近也不返回可激活项
        expectInt("站在草地神龛上（已激活）-> 无提示",
                  nearestInactiveShrineInRange(defaultState, kShrineDefs[0].x, kShrineDefs[0].y,
                                               kShrineInteractRadius), -1);

        bool bothInactive[kShrineCount] = { false, false }; // 两座都未激活
        expectInt("两座未激活：靠近雪山 -> 雪山",
                  nearestInactiveShrineInRange(bothInactive, snowX, y, kShrineInteractRadius), 1);
        expectInt("两座未激活：靠近草地 -> 草地",
                  nearestInactiveShrineInRange(bothInactive, kShrineDefs[0].x, kShrineDefs[0].y,
                                               kShrineInteractRadius), 0);

        bool bothActive[kShrineCount] = { true, true }; // 两座都激活
        expectInt("两座激活 -> 无提示",
                  nearestInactiveShrineInRange(bothActive, snowX, y, kShrineInteractRadius), -1);
        expectInt("半径 0 -> 无提示",
                  nearestInactiveShrineInRange(bothInactive, snowX, y, 0.0f), -1);
        expectInt("active = nullptr -> -1",
                  nearestInactiveShrineInRange(nullptr, snowX, y, kShrineInteractRadius), -1);
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 死亡惩罚数学（金币 / 等级 / 经验）
    // ------------------------------------------------------------------
    beginCase(5, "applyDeathPenalty：金币减半 / 等级 -1 / 经验清零");
    p0 = gPass; f0 = gFail;
    {
        int spellLevels[kSpellCount]; // 法术层数数组
        clearSpellLevels(spellLevels); // 全 0 起步

        std::vector<UpgradeChoiceRecord> history; // 升级历史栈
        pushUpgradeChoice(history, 0, 1); // 第 1 次：火球 1 层
        pushUpgradeChoice(history, 0, 2); // 第 2 次：火球 2 层
        pushUpgradeChoice(history, 0, 3); // 第 3 次：火球 3 层
        spellLevels[0] = 3;               // 当前火球 3 层

        const DeathPenaltyResult result =
            applyDeathPenalty(7, 5, 20, history, spellLevels, kSpellCount); // 7 金币 / 5 级 / 20 经验

        expectInt("金币 7 -> 3（向下取整）", result.coins, 3);   // floor(7/2)
        expectInt("等级 5 -> 4", result.level, 4);               // 等级 -1
        expectInt("经验 20 -> 0", result.xp, 0);                 // 该级经验清零
        expectTrue("发生降级标记", result.levelLost);          // levelLost = true
        expectTrue("丢失技能标记", result.skillLost);          // 最近升级技能被归还
        expectInt("丢失法术下标 = 火球", result.lostSpellIndex, 0); // 出栈的是最后火球
        expectInt("丢失前层数 = 3", result.lostSpellLevelBefore, 3); // 记录快照
        expectInt("丢失后层数 = 2", result.lostSpellLevelAfter, 2);  // 层数 -1
        expectInt("法术层数写回 = 2", spellLevels[0], 2);      // 实际数组已更新
        expectInt("历史栈长度 = 2", static_cast<int>(history.size()), 2); // 出栈一笔

        // 金币为 1 / 0 的边界
        std::vector<UpgradeChoiceRecord> emptyHistory; // 空历史
        const DeathPenaltyResult oneCoin =
            applyDeathPenalty(1, 1, 5, emptyHistory, spellLevels, kSpellCount); // 1 金币 / 1 级
        expectInt("金币 1 -> 0", oneCoin.coins, 0);        // 向下取整
        expectInt("等级 1 保持 1", oneCoin.level, 1);      // 无等级惩罚
        expectInt("等级 1 经验保持 5", oneCoin.xp, 5);     // 无等级惩罚不清经验
        expectTrue("等级 1 不降级", !oneCoin.levelLost);   // 标记 false
        expectTrue("等级 1 无技能损失", !oneCoin.skillLost); // 历史为空且未降级
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // CASE 6 升级历史栈出栈（归零移除 / 空栈 / 非法下标）
    // ------------------------------------------------------------------
    beginCase(6, "历史栈出栈：层数归零移除 / 空栈防御 / 非法下标防御");
    p0 = gPass; f0 = gFail;
    {
        int spellLevels[kSpellCount]; // 法术层数数组
        clearSpellLevels(spellLevels); // 全 0 起步
        spellLevels[1] = 1;            // 冰霜新星 1 层

        std::vector<UpgradeChoiceRecord> history; // 升级历史栈
        pushUpgradeChoice(history, 1, 1);         // 唯一一笔：冰霜 1 层

        const DeathPenaltyResult result =
            applyDeathPenalty(10, 2, 8, history, spellLevels, kSpellCount); // 2 级死亡
        expectInt("冰霜 1 -> 0（移除）", spellLevels[1], 0); // 层数归零
        expectInt("丢失后层数 = 0", result.lostSpellLevelAfter, 0); // 归零即停止施法
        expectTrue("实际丢失技能", result.skillLost);      // before > 0
        expectInt("历史栈清空", static_cast<int>(history.size()), 0); // 出栈最后一笔

        // 空栈降级：仍然 -1 级、经验清零，但没有技能可丢
        std::vector<UpgradeChoiceRecord> emptyHistory; // 空历史
        const DeathPenaltyResult emptyResult =
            applyDeathPenalty(3, 4, 99, emptyHistory, spellLevels, kSpellCount); // 4 级死亡
        expectInt("空栈：等级 4 -> 3", emptyResult.level, 3);  // 等级惩罚照常
        expectInt("空栈：经验清零", emptyResult.xp, 0);        // 该级经验清零
        expectTrue("空栈：无技能损失", !emptyResult.skillLost); // 没有记录可出栈

        // 非法下标（历史记录损坏）：照常降级，不越界写层数
        std::vector<UpgradeChoiceRecord> badHistory; // 损坏历史
        pushUpgradeChoice(badHistory, 999, 9);       // 越界法术下标
        const DeathPenaltyResult badResult =
            applyDeathPenalty(4, 3, 10, badHistory, spellLevels, kSpellCount); // 3 级死亡
        expectInt("非法下标：等级 3 -> 2", badResult.level, 2); // 等级惩罚照常
        expectTrue("非法下标：不标记技能损失", !badResult.skillLost); // 无合法层数可扣
        expectInt("非法下标：历史已出栈", static_cast<int>(badHistory.size()), 0); // 记录被消费

        // 层数数组为空（防御）：历史出栈但不写数组
        std::vector<UpgradeChoiceRecord> nullLevelsHistory; // 历史一笔
        pushUpgradeChoice(nullLevelsHistory, 0, 2);         // 火球 2 层快照
        const DeathPenaltyResult nullResult =
            applyDeathPenalty(4, 3, 0, nullLevelsHistory, nullptr, 0); // 层数数组缺失
        expectTrue("空层数数组：不崩且无技能损失", !nullResult.skillLost); // 防御路径
        expectInt("空层数数组：等级仍 -1", nullResult.level, 2); // 等级惩罚独立执行

        // 负值输入防御
        std::vector<UpgradeChoiceRecord> negativeHistory; // 空历史
        const DeathPenaltyResult negativeResult =
            applyDeathPenalty(-9, 0, -5, negativeHistory, spellLevels, kSpellCount); // 全负输入
        expectInt("负金币 -> 0", negativeResult.coins, 0); // 夹到非负后再减半
        expectInt("等级 0 -> 1", negativeResult.level, 1); // 最低 1 级
        expectInt("负经验 -> 0", negativeResult.xp, 0);    // 夹到非负
    }
    endCase(6, p0, f0);

    // ------------------------------------------------------------------
    // CASE 7 多次死亡连续出栈（逐次归还升级所得技能）
    // ------------------------------------------------------------------
    beginCase(7, "连续死亡：逐次出栈 / 技能逐层归还");
    p0 = gPass; f0 = gFail;
    {
        int spellLevels[kSpellCount]; // 法术层数数组
        clearSpellLevels(spellLevels); // 全 0 起步
        spellLevels[0] = 1;            // 火球 1 层
        spellLevels[2] = 1;            // 闪电链 1 层

        std::vector<UpgradeChoiceRecord> history; // 升级历史栈
        pushUpgradeChoice(history, 0, 1);         // 先学火球
        pushUpgradeChoice(history, 2, 1);         // 再学闪电链（最近一次）

        // 第 1 次死亡：3 级 -> 2 级，归还闪电链（1 -> 0，移除）
        const DeathPenaltyResult first =
            applyDeathPenalty(6, 3, 12, history, spellLevels, kSpellCount);
        expectInt("第 1 次死亡：等级 3 -> 2", first.level, 2); // 等级 -1
        expectInt("第 1 次死亡：闪电链 1 -> 0", spellLevels[2], 0); // 最近技能移除
        expectInt("第 1 次死亡：火球保持 1", spellLevels[0], 1);    // 更早技能不动

        // 第 2 次死亡：2 级 -> 1 级，归还火球（1 -> 0，移除）
        const DeathPenaltyResult second =
            applyDeathPenalty(first.coins, first.level, first.xp, history, spellLevels, kSpellCount);
        expectInt("第 2 次死亡：等级 2 -> 1", second.level, 1); // 到最低 1 级
        expectInt("第 2 次死亡：火球 1 -> 0", spellLevels[0], 0); // 更早技能也移除
        expectInt("第 2 次死亡：历史清空", static_cast<int>(history.size()), 0); // 两笔全部出栈

        // 第 3 次死亡：1 级只扣金币，不再降级/出栈
        const DeathPenaltyResult third =
            applyDeathPenalty(5, 1, 0, history, spellLevels, kSpellCount);
        expectInt("第 3 次死亡：金币 5 -> 2", third.coins, 2); // 金币照常减半
        expectInt("第 3 次死亡：等级保持 1", third.level, 1);  // 最低 1 级
        expectTrue("第 3 次死亡：不降级", !third.levelLost);   // 无等级惩罚
    }
    endCase(7, p0, f0);

    // ------------------------------------------------------------------
    // 总结
    // ------------------------------------------------------------------
    std::printf("\n==============================================================\n");
    std::printf(" 总结: %d 通过, %d 失败\n", gPass, gFail);
    std::printf("==============================================================\n");
    return gFail; // 退出码 = 失败数（0 = 全部通过）
}
