#ifndef __SPELL_H__
#define __SPELL_H__

#include "game/ChoiceDraw.h" // ChoiceDraw：通用不重复抽取（三选一面板——与升级卡共用同一洗牌核心）
#include <algorithm>         // std::max / std::min：层数夹取与增长

// ============================================================
// Spell：法术池纯逻辑（批五B：六法术、升级三选一、逐层参数）
// ============================================================
//
// 定位：升级（含连升）时弹出「法术三选一」（抽自本文件定义的六法术池），
// 学会后自动施法（仅 Playing、各自冷却独立、无需按键；施法运行时——
// 索敌/弹体/特效/HUD 在 PlayScene）。本文件收敛：法术表（名称/描述/
// 上限 5）、逐层参数公式、层数工具、三抽抽取——全部可单测
//（tests/spell_test.cpp 直测，不依赖 SDL）。
//
// 六法术（规格批准；调平口径 = 「1 级起步 + 每层线性常量」，数值即
// 报告总表的唯一来源）：
//   1) 火球术 Fireball     ：周期向最近敌人发射火球（飞行+命中爆炸）
//                            ——伤害/频率/爆炸半径随层
//   2) 冰霜新星 FrostNova  ：自身为心爆冰环（范围伤害+减速：怪物速度
//                            乘区持续数秒）——半径/伤害/频率随层
//   3) 闪电链 ChainLightning：劈击范围内随机敌并跳跃传导（跳数随层）
//                            ——伤害随层
//   4) 治疗之光 HolyHeal   ：周期回血——回复量/频率随层
//   5) 灼烧光环 BurnAura   ：周身持续灼烧（范围内每 0.5s 一跳）
//                            ——半径/伤害随层
//   6) 护盾术 Shield       ：周期获得护盾点数（先盾后血，HUD 显示）
//                            ——点数/频率随层
//
// 抽取：三抽不重复、满层排除（层 5 移出池）、池不足按实际（满池 6 张时
// 恒抽 3 张；全部满层后 count = 0——场景按欠账消费跳过）。洗牌式核心
// 复用 ChoiceDraw.h（与 M2 升级卡抽取同一算法——批五B 泛化的落点）。
//
// 术语：参数函数的「层数」先经 clampSpellLevel 夹到 [0,5]；参数公式只在
// 「已学」（层 >= 1）时被场景调用，0 层按 1 层参数返回（防御性偏好——
// 公式连续、不产生 0/负参数）。
// ============================================================

// 法术 id：顺序即法术池表顺序（面板展示位序按抽取结果）
enum class SpellId
{
    Fireball = 0,   // 火球术：向最近敌人发射火球（飞行+命中爆炸）
    FrostNova,      // 冰霜新星：周身冰环（范围伤害+减速）
    ChainLightning, // 闪电链：劈击随机敌并跳跃传导
    HolyHeal,       // 治疗之光：周期回血
    BurnAura,       // 灼烧光环：周身持续灼烧
    Shield,         // 护盾术：周期获得护盾点数
    Count           // 哨兵：池张数（= 6）；不是一系真实法术
};

// 法术池张数：层数数组尺寸、遍历上界与索引校验的统一常量
inline constexpr int kSpellCount = static_cast<int>(SpellId::Count); // 6

// 全部法术统一 5 层上限（规格口径：未学可学、已学叠层至 5）
inline constexpr int kSpellMaxLevel = 5;

// 单系法术的静态定义（代码表：名称 / 描述）
struct SpellDef
{
    const char* name; // 法术名（面板与日志显示）
    const char* desc; // 效果描述（面板显示；数值口径见下面各参数函数）
};

// 法术池定义表：下标与 SpellId 一一对应
inline constexpr SpellDef kSpellDefs[kSpellCount] = {
    { "火球术", "向最近敌人发射火球" },   // 火球术
    { "冰霜新星", "周身冰环：伤害+减速" }, // 冰霜新星
    { "闪电链", "劈击随机敌人并跳跃" },   // 闪电链
    { "治疗之光", "周期回复生命" },       // 治疗之光
    { "灼烧光环", "周身持续灼烧" },       // 灼烧光环
    { "护盾术", "周期获得护盾" },         // 护盾术
};

// 取法术定义（防御：异常 id 回退第一项——不崩、结果可预测）
inline const SpellDef& spellDef(SpellId id)
{
    // 下标换算与区域校验：合法域 [0, kSpellCount)
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kSpellCount)
    {
        return kSpellDefs[0]; // 越界（理论不可达）：回退到第一项
    }
    return kSpellDefs[index]; // 正常路径：按 id 取表项
}

// ============================================================
// 层数工具（封顶口径的唯一实现）
// ============================================================

// 层数夹取到合法区间 [0, kSpellMaxLevel]：负层按 0（防御）、超过上限按上限
inline int clampSpellLevel(int level)
{
    const int nonNegative = (level > 0) ? level : 0; // 负层/0：先压到 0 起
    return std::min(nonNegative, kSpellMaxLevel);    // 统一封顶 5
}

// 层数是否已满：达到/超过上限即满（抽取的「排除已满层」与选中后的
// 「封顶」都以本判据为准）
inline bool isSpellFull(int level)
{
    return clampSpellLevel(level) >= kSpellMaxLevel; // 夹取后 >= 5 即满
}

// 获得一层后的新层数：+1 后不超上限（已满返回原值）
inline int gainSpellLevel(int level)
{
    const int clamped = clampSpellLevel(level); // 现有层数先夹到合法区间
    return std::min(clamped + 1, kSpellMaxLevel); // +1 后封顶
}

// 参数函数的内部层数口径：夹到 [1, kSpellMaxLevel]（0 层按 1 层——
// 参数函数只在「已学」时被调用，防御性偏好保证公式连续、无 0/负参数）
inline int spellParamLevel(int level)
{
    const int clamped = clampSpellLevel(level); // 先夹到 [0,5]
    return (clamped >= 1) ? clamped : 1;        // 0 按 1（防御）
}

// ============================================================
// 逐层参数（调平总表——每函数 1~5 层的取值见行内注释）
// ============================================================

// ---- 火球术：伤害 / 冷却 / 爆炸半径 ----

// 火球伤害（点）：20 + 10 x（层-1） -> 20/30/40/50/60
inline int spellFireballDamage(int level)
{
    const int lv = spellParamLevel(level); // [1,5]
    return 20 + 10 * (lv - 1);             // 每层 +10
}

// 火球冷却（秒）：2.0 - 0.2 x（层-1） -> 2.0/1.8/1.6/1.4/1.2
inline float spellFireballCooldown(int level)
{
    const int lv = spellParamLevel(level);                        // [1,5]
    return 2.0f - 0.2f * static_cast<float>(lv - 1);              // 每层 -0.2s
}

// 火球爆炸半径（像素）：40 + 10 x（层-1） -> 40/50/60/70/80
inline float spellFireballBlastRadius(int level)
{
    const int lv = spellParamLevel(level);                        // [1,5]
    return 40.0f + 10.0f * static_cast<float>(lv - 1);            // 每层 +10px
}

// ---- 冰霜新星：半径 / 伤害 / 冷却 / 减速时长 ----

// 新星半径（像素）：120 + 20 x（层-1） -> 120/140/160/180/200
inline float spellFrostNovaRadius(int level)
{
    const int lv = spellParamLevel(level);                        // [1,5]
    return 120.0f + 20.0f * static_cast<float>(lv - 1);           // 每层 +20px
}

// 新星伤害（点）：15 + 5 x（层-1） -> 15/20/25/30/35
inline int spellFrostNovaDamage(int level)
{
    const int lv = spellParamLevel(level); // [1,5]
    return 15 + 5 * (lv - 1);              // 每层 +5
}

// 新星冷却（秒）：5.0 - 0.5 x（层-1） -> 5.0/4.5/4.0/3.5/3.0
inline float spellFrostNovaCooldown(int level)
{
    const int lv = spellParamLevel(level);                        // [1,5]
    return 5.0f - 0.5f * static_cast<float>(lv - 1);              // 每层 -0.5s
}

// 减速持续时长（秒）：2.0 + 0.5 x（层-1） -> 2.0/2.5/3.0/3.5/4.0
inline float spellFrostNovaSlowSeconds(int level)
{
    const int lv = spellParamLevel(level);                        // [1,5]
    return 2.0f + 0.5f * static_cast<float>(lv - 1);              // 每层 +0.5s
}

// 减速乘区（常量，全层一致）：怪物速度 x 0.5——「速度乘区持续数秒」的乘区值
inline constexpr float kSpellFrostSlowFactor = 0.5f;

// ---- 闪电链：伤害 / 跳数 / 冷却 ----

// 闪电链每跳伤害（点）：18 + 8 x（层-1） -> 18/26/34/42/50
inline int spellChainDamage(int level)
{
    const int lv = spellParamLevel(level); // [1,5]
    return 18 + 8 * (lv - 1);              // 每层 +8
}

// 闪电链总命中次数（含首击）：2 + （层-1） -> 2/3/4/5/6——
// 即首击 1 次 + 跳跃 1~5 次（「跳数随层」的口径：跳跃次数 = 层数-1）
inline int spellChainHits(int level)
{
    const int lv = spellParamLevel(level); // [1,5]
    return 2 + (lv - 1);                   // 每层 +1 跳
}

// 闪电链冷却（秒）：3.0 - 0.25 x（层-1） -> 3.0/2.75/2.5/2.25/2.0
inline float spellChainCooldown(int level)
{
    const int lv = spellParamLevel(level);                        // [1,5]
    return 3.0f - 0.25f * static_cast<float>(lv - 1);             // 每层 -0.25s
}

// ---- 治疗之光：回复量 / 冷却 ----

// 治疗回复量（点）：5 + 4 x（层-1） -> 5/9/13/17/21
inline int spellHealAmount(int level)
{
    const int lv = spellParamLevel(level); // [1,5]
    return 5 + 4 * (lv - 1);               // 每层 +4
}

// 治疗冷却（秒）：6.0 - 0.5 x（层-1） -> 6.0/5.5/5.0/4.5/4.0
inline float spellHealCooldown(int level)
{
    const int lv = spellParamLevel(level);                        // [1,5]
    return 6.0f - 0.5f * static_cast<float>(lv - 1);              // 每层 -0.5s
}

// ---- 灼烧光环：半径 / 每跳伤害（tick 间隔为常量 0.5s）----

// 光环半径（像素）：70 + 15 x（层-1） -> 70/85/100/115/130
inline float spellAuraRadius(int level)
{
    const int lv = spellParamLevel(level);                        // [1,5]
    return 70.0f + 15.0f * static_cast<float>(lv - 1);            // 每层 +15px
}

// 光环每跳伤害（点）：4 + 2 x（层-1） -> 4/6/8/10/12
inline int spellAuraTickDamage(int level)
{
    const int lv = spellParamLevel(level); // [1,5]
    return 4 + 2 * (lv - 1);               // 每层 +2
}

// 光环跳伤间隔（常量，全层一致）：每 0.5 秒一跳（规格口径）
inline constexpr float kSpellAuraTickSeconds = 0.5f;

// ---- 护盾术：护盾点数 / 冷却 ----

// 护盾点数（点）：20 + 10 x（层-1） -> 20/30/40/50/60。
// 同时是本层的「护盾储存上限」——已有护盾达到该值时不重复施法
inline int spellShieldPoints(int level)
{
    const int lv = spellParamLevel(level); // [1,5]
    return 20 + 10 * (lv - 1);             // 每层 +10
}

// 护盾冷却（秒）：8.0 - 0.5 x（层-1） -> 8.0/7.5/7.0/6.5/6.0
inline float spellShieldCooldown(int level)
{
    const int lv = spellParamLevel(level);                        // [1,5]
    return 8.0f - 0.5f * static_cast<float>(lv - 1);              // 每层 -0.5s
}

// ---- 施法几何常量（与调平相关的固定参数，集中于此便于调参）----

inline constexpr float kSpellFireballSpeed = 420.0f;       // 火球飞行速度（像素/秒）
inline constexpr float kSpellFireballLifeSeconds = 1.5f;   // 火球寿命（秒；到点自爆）
inline constexpr float kSpellFireballSearchRadius = 420.0f; // 火球的「最近敌人」搜索半径（像素）
inline constexpr float kSpellChainSearchRadius = 240.0f;   // 闪电链首击的搜索半径（像素）
inline constexpr float kSpellChainJumpRadius = 160.0f;     // 闪电链跳跃的搜索半径（像素）

// ============================================================
// 三抽不重复（排除已满层、等权、不足三张按实际）
// ============================================================

// 一次抽取的结果：法术 id 数组 + 实际张数（0 = 池空/全部满层）
struct SpellDrawResult
{
    SpellId spells[kChoiceDrawMax] = {}; // 抽到的法术（按展示位序 1/2/3；仅前 count 个有效）
    int count = 0;                       // 实际张数（0~3；0 = 无选项可展示）
};

// 抽取一次升级选择的三张法术：
//   1. 候选池 = 全部未满层的法术（排除已满层——层 5 不再入池）；
//   2. 洗牌式逐张抽取（ChoiceDraw::drawDistinctFromPool，与升级卡同一
//      核心）：剩余池中按注入随机源均匀取一、抽走处用池尾填补——
//      不重复且池内等权；
//   3. 池不足 3 张时按实际张数返回（count < 3；0 = 池空）。
// levels 为各法术当前层数数组（下标与 SpellId 一一对应，长度 kSpellCount）。
// fn 为随机源（nullptr 时退化常量 0.5——确定性兜底，不崩）；fn 的非法
// 输出（NaN/负/≥1）由抽取核心统一夹到合法索引（防御，单测覆盖）
inline SpellDrawResult drawSpellChoices(const int* levels, ChoiceRandomFn fn, void* context)
{
    // 层数数组未提供（防御路径）：无池可建，返回空结果
    if (levels == nullptr)
    {
        return SpellDrawResult{}; // count = 0
    }

    // ---- 1) 构建候选池：顺序扫过法术表，未满层的法术索引依次入池 ----
    int pool[kSpellCount]; // 候选池（法术索引；池尺寸 = 法术池张数）
    int poolSize = 0;      // 池内张数
    for (int i = 0; i < kSpellCount; ++i) // 遍历全部法术位
    {
        if (!isSpellFull(levels[i])) // 未满层才入池（满层排除）
        {
            pool[poolSize] = i; // 写池尾（记录索引——通用抽取只认识索引）
            ++poolSize;         // 尺寸 +1
        }
    }

    // ---- 2) 通用抽取核心：洗牌式取最多 3 个不重复索引 ----
    const ChoiceDrawResult drawn = drawDistinctFromPool(pool, poolSize, fn, context);

    // ---- 3) 索引还原为法术 id（展示位序照旧：结果位序 = 抽取位序）----
    SpellDrawResult result;     // 抽取结果（默认 count=0）
    result.count = drawn.count; // 张数照搬（池不足按实际、池空为 0）
    for (int k = 0; k < drawn.count; ++k) // 逐张还原
    {
        result.spells[k] = static_cast<SpellId>(drawn.indices[k]); // 索引 -> 法术 id
    }
    return result; // 交还抽取结果
}

#endif // !__SPELL_H__
