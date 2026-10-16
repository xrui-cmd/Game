#ifndef __SHOPDESC_H__
#define __SHOPDESC_H__

#include "game/Shop.h" // Shop/UpgradeCard：卡片定义、层数夹取与叠加数学（数值对比实算的同一数据源）
#include <cstddef>     // size_t：文案缓冲长度参数
#include <cstdio>      // std::snprintf：装配数值对比文案

// ============================================================
// ShopDesc：商店「效果描述」文案（批六甲·第四笔）
// ============================================================
//
// 背景（规格批准）：商店每项强化除「名称/层数/价格」外，增加效果描述——
//   第一行 = 静态效果说明（沿用 CardDef::desc，如「伤害 +5」）；
//   第二行 = 动态数值对比（按当前层数实算，如「当前 25 → 购买后 30」）。
//
// 本文件只负责第二行的文案装配（纯逻辑，tests/shop_test.cpp 直测）：
//   - 数值一律经 UpgradeCard.h 的叠加数学函数实算——与购买后的真实生效
//     路径同源（applyCardEffects 用的也是这些函数），文案永远不会与
//     实际数值漂移；
//   - 「基准值」由场景以 ShopPreviewBases 注入（PlayScene 的 m_*Base），
//     本文件不依赖任何运行时状态；
//   - 满层（购买后会封顶不变）时由调用方改用「已达上限」文案，本函数
//     对满层输入按 clamp 后实算（当前 == 购买后——防御性恒可算）。
//
// 文案口径（逐项）：
//   利刃/体魄：整数（点）           「当前 25 → 购买后 30」
//   疾风：冷却（秒，两位小数）      「当前 0.40s → 购买后 0.35s」
//   长臂/疾行/重击/聚宝：像素整数   「当前 96 → 购买后 111」
//   横扫：度数整数                  「当前 120 度 → 购买后 135 度」
//   慧眼：经验乘数（一位小数）      「当前 ×1.0 → 购买后 ×1.2」
// ============================================================

// 动态对比所需的一组「基准值」快照（与 PlayScene 的 m_*Base 一一对应；
// 结构体传值方便单测注入与场景构建——字段语义见注释）
struct ShopPreviewBases
{
    int attackDamage = 25;         // 伤害基准（attack_damage，点）
    float attackCooldown = 0.4f;   // 攻击冷却基准（attack_cooldown，秒）
    float attackRange = 96.0f;     // 攻击范围基准（attack_range，像素）
    float attackArc = 120.0f;      // 攻击张角基准（attack_arc，度）
    float playerSpeed = 300.0f;    // 玩家移速基准（player_speed，像素/秒）
    int playerMaxHp = 100;         // 生命上限基准（player_hp，点）
    float attackKnockback = 30.0f; // 击退基准（attack_knockback，像素）
    float coinMagnet = 140.0f;     // 金币磁吸基准（coin_magnet_radius，像素）
};

// 装配「当前 -> 购买后」动态对比文案（写入 out；cap 为缓冲长度）。
// level 为当前层数（先经 clampCardLevel 夹取——异常输入恒可算）；
// 有限卡满层时 next 被公式夹回原值（文案退化「当前 X → 购买后 X」，
// 面板侧对满层改用「已达上限」，不会真显示退化文案）
inline void shopCompareDesc(CardId id, int level, const ShopPreviewBases& bases,
                            char* out, size_t cap)
{
    const int lv = clampCardLevel(id, level); // 当前层数（夹取到合法区间）
    const int next = lv + 1;                  // 购买一层后的层数（叠加函数内部再夹取）

    // 按卡片逐项装配——每项用与生效路径完全相同的叠加函数实算两侧数值
    switch (id)
    {
    case CardId::Blade: // 利刃：伤害（点）
        std::snprintf(out, cap, "当前 %d → 购买后 %d",
                      cardBladeDamage(bases.attackDamage, lv),   // 当前伤害
                      cardBladeDamage(bases.attackDamage, next)); // 购买后伤害
        break;
    case CardId::Swift: // 疾风：攻击冷却（秒，两位小数）
        std::snprintf(out, cap, "当前 %.2fs → 购买后 %.2fs",
                      cardSwiftCooldown(bases.attackCooldown, lv),   // 当前冷却
                      cardSwiftCooldown(bases.attackCooldown, next)); // 购买后冷却
        break;
    case CardId::Reach: // 长臂：攻击范围（像素整数）
        std::snprintf(out, cap, "当前 %.0f → 购买后 %.0f",
                      cardReachRange(bases.attackRange, lv),   // 当前范围
                      cardReachRange(bases.attackRange, next)); // 购买后范围
        break;
    case CardId::Sweep: // 横扫：张角（度整数）
        std::snprintf(out, cap, "当前 %.0f 度 → 购买后 %.0f 度",
                      cardSweepArc(bases.attackArc, lv),   // 当前张角
                      cardSweepArc(bases.attackArc, next)); // 购买后张角
        break;
    case CardId::Dash: // 疾行：移速（像素/秒整数）
        std::snprintf(out, cap, "当前 %.0f → 购买后 %.0f",
                      cardDashSpeed(bases.playerSpeed, lv),   // 当前移速
                      cardDashSpeed(bases.playerSpeed, next)); // 购买后移速
        break;
    case CardId::Vitality: // 体魄：生命上限（点）
        std::snprintf(out, cap, "当前 %d → 购买后 %d",
                      cardVitalityMaxHp(bases.playerMaxHp, lv),   // 当前上限
                      cardVitalityMaxHp(bases.playerMaxHp, next)); // 购买后上限
        break;
    case CardId::Heavy: // 重击：击退（像素整数）
        std::snprintf(out, cap, "当前 %.0f → 购买后 %.0f",
                      cardHeavyKnockback(bases.attackKnockback, lv),   // 当前击退
                      cardHeavyKnockback(bases.attackKnockback, next)); // 购买后击退
        break;
    case CardId::Insight: // 慧眼：经验乘数（一位小数）
        std::snprintf(out, cap, "当前 ×%.1f → 购买后 ×%.1f",
                      cardInsightXpMultiplier(lv),   // 当前乘数
                      cardInsightXpMultiplier(next)); // 购买后乘数
        break;
    case CardId::Treasure: // 聚宝：金币磁吸范围（像素整数）
        std::snprintf(out, cap, "当前 %.0f → 购买后 %.0f",
                      cardTreasureMagnet(bases.coinMagnet, lv),   // 当前磁吸
                      cardTreasureMagnet(bases.coinMagnet, next)); // 购买后磁吸
        break;
    default: // 异常 id（理论不可达——表外值）：写作「—」（不崩、面板仍完整）
        std::snprintf(out, cap, "—"); // 占位符
        break;
    }
}

#endif // !__SHOPDESC_H__
