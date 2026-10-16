#ifndef __SHOP_H__
#define __SHOP_H__

#include "game/UpgradeCard.h" // CardId / kCardCount / clampCardLevel / isCardFull：商店售卖同一批强化

// ============================================================
// Shop：商店纯逻辑（批五B：九项强化用金币购买、价格随层递增）
// ============================================================
//
// 背景（批五B 商店迁移）：M2 的九张升级卡退出「升级三选一」（升级改发
// 法术，见 game/Spell.h），改为商店在售——对局中随时按 B 开商店（世界
// 冻结、同暂停语义），用局内金币（击杀掉落）购买；九项强化只能在此用
// 金币升级。面板布局见 game/ShopLayout.h；购买流程（扣币/层数/生效/
// 日志）在 PlayScene。
//
// 价格口径（规格批准）：价格 = 基础价 x（当前层数 + 1）——买第 1 层收
// 基础价、第 2 层翻倍、依此类推（越买越贵，约束经济膨胀与无限卡刷级）。
// 基础价按强度定（4~10），见 kShopBasePrices 表与注释：越强越贵。
//
// 本文件只收敛「价格」与「能否购买」两条纯逻辑（单测直测，见
// tests/shop_test.cpp）；金币扣减/层数增长/效果重算都是场景职责
//（扣减 m_coinCount、层数走 gainCardLevel、效果走 applyCardEffects）。
// ============================================================

// 各项强化的基础价（与 CardId 一一对应；4~10 的强度定价）：
//   利刃 8（无限层、伤害核心）、疾风 9（冷却乘算，攻速收益最大）、
//   长臂 6（范围 +15 覆盖更远）、横扫 5、疾行 5、体魄 7（无限层、
//   容错核心）、重击 4（击退偏防御性）、慧眼 7（经验滚雪球）、
//   聚宝 4（拾取便利）。此处注释即「价格总表」的来源（报告口径）
inline constexpr int kShopBasePrices[kCardCount] = {
    8, // 利刃 Blade
    9, // 疾风 Swift
    6, // 长臂 Reach
    5, // 横扫 Sweep
    5, // 疾行 Dash
    7, // 体魄 Vitality
    4, // 重击 Heavy
    7, // 慧眼 Insight
    4, // 聚宝 Treasure
};

// 取某强化的基础价（下标换算 + 越界防御：回退第一项——与 cardDef 同口径；
// 正常流程 id 恒合法，防御只为「异常 id 不崩、结果可预测」）
inline int shopBasePrice(CardId id)
{
    const int index = static_cast<int>(id); // 卡 id -> 表下标
    if (index < 0 || index >= kCardCount)   // 越界（理论不可达）
    {
        return kShopBasePrices[0]; // 回退第一项（防御，不崩）
    }
    return kShopBasePrices[index]; // 正常路径：按 id 取表项
}

// 当前价格 = 基础价 x（当前层数 + 1）。层数先经 clampCardLevel 夹取——
// 异常层数（负/超上限）与合法层数同价，价格口诀永远可算（防御）
inline int shopPrice(CardId id, int level)
{
    const int lv = clampCardLevel(id, level); // 层数夹取（负按 0、有限卡封顶）
    return shopBasePrice(id) * (lv + 1);      // 第 n 层价格 = 基础价 x n
}

// 能否购买：未满层 且 金币足够（恰好等于价格可以买——含边界）。
// 金币为负（数据异常）恒不可买；无限卡（maxLevel=0）由 isCardFull
// 恒 false 表达「永远未满」——由价格增长承担约束
inline bool shopCanBuy(CardId id, int level, int coins)
{
    if (coins < 0) // 负金币（防御：数据异常，宁可不卖）
    {
        return false; // 不可购买
    }
    if (isCardFull(id, level)) // 满层：不再售卖（层级上限照旧）
    {
        return false; // 不可购买
    }
    return coins >= shopPrice(id, level); // 金币足够（含恰好相等——规格边界）
}

#endif // !__SHOP_H__
