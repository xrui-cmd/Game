#ifndef __UPGRADECARD_H__
#define __UPGRADECARD_H__

#include "game/ChoiceDraw.h" // ChoiceDraw：通用不重复抽取（批五B 从本文件抽出——卡池/法术池共用）
#include <algorithm> // std::min：层数封顶
#include <cmath>     // std::lround：经验乘算取整
#include <cstdint>   // std::uint32_t：种子化随机源的状态类型

// ============================================================
// UpgradeCard：九项强化表（M2 升级卡 → 批五B 商店在售，game 层纯逻辑）
// ============================================================
//
// 职责：把「九项强化表」「层数封顶/叠加数学」「连升欠账队列」「种子化
// 随机源」全部收敛成可单测的纯逻辑（tests/card_test.cpp 直测）——
// 不依赖 SDL、窗口与任何运行时状态。PlayScene 只负责把结果落实到局面
// （数值注入/面板绘制/输入处理）。
//
// 批五B 迁移（规格批准）：九项强化退出「升级三选一」（升级改发法术，
// 见 game/Spell.h），改为商店在售——对局中随时按 B 开商店（世界冻结），
// 用局内金币购买升级（价格公式见 game/Shop.h）。本文件的层数工具与
// 叠加数学原样服务商店购买路径（applyCardEffects 统一重算注入）；
// 抽取逻辑泛化到 ChoiceDraw.h（drawUpgradeCards 保留——行为与 M2 逐
// 字节一致，card_test 回归即检验）。
//
// 强化池（9 张，代码表；上限 0 = 无限）：
//   1) 利刃 Blade    ：伤害 +5（无限）
//   2) 疾风 Swift    ：攻速 +12%（冷却 x0.88/层，限 5）——乘法叠乘
//   3) 长臂 Reach    ：攻击范围 +15（限 4）
//   4) 横扫 Sweep    ：张角 +15 度（限 3）——与原「分裂」的适配改版
//      （横扫已多目标，增张角即顺劈更多方向）
//   5) 疾行 Dash     ：移速 +20（限 5）
//   6) 体魄 Vitality ：生命上限 +20 且即时回血 20（无限）
//   7) 重击 Heavy    ：击退 +50%（限 3）——百分比加法叠加
//   8) 慧眼 Insight  ：经验获取 +20%（限 5）——吸收时乘算
//   9) 聚宝 Treasure ：金币磁吸范围 +60（限 3）——原「磁石」的适配
//      改版（宝石已自动吸收，磁石转向金币磁吸）
//
// 叠加口径（规格批准）：
//   - 乘算类：疾风（冷却 x0.88^层）、慧眼（1 + 0.2 x 层，乘到经验值上）；
//   - 累加类：其余全部按「每层常量 x 层数」线性叠加到基准值上；
//   - 层数封顶：所有公式先经 clampCardLevel 夹取——即使异常层数输入，
//     结果也等价于「顶到上限」（上限封顶语义有单测）。
//
// 随机源注入（种子化）：抽取函数经函数指针注入随机源（CardRandomFn）——
// 游戏内传 CardSeedRandom::nextFrom（xorshift32 种子流），单测可传任意
// 固定序列；同一种子的抽取序列完全可复现。
// ============================================================

// 卡牌 id：顺序即卡池表顺序（卡表、层数数组、面板展示位序共用）
enum class CardId
{
    Blade = 0,  // 利刃：伤害 +5/层（无限）
    Swift,      // 疾风：攻速 +12%/层（冷却 x0.88/层，限 5）
    Reach,      // 长臂：攻击范围 +15/层（限 4）
    Sweep,      // 横扫：张角 +15 度/层（限 3）
    Dash,       // 疾行：移速 +20/层（限 5）
    Vitality,   // 体魄：生命上限 +20/层 且即时回血 20（无限）
    Heavy,      // 重击：击退 +50%/层（限 3）
    Insight,    // 慧眼：经验获取 +20%/层（限 5）
    Treasure,   // 聚宝：金币磁吸范围 +60/层（限 3）
    Count       // 哨兵：卡池张数（= 9）；不是一张真实的卡
};

// 卡池张数：层数数组尺寸、遍历上界与索引校验的统一常量
inline constexpr int kCardCount = static_cast<int>(CardId::Count); // 9

// 单张卡的静态定义（代码表：名称 / 描述 / 层数上限）
struct CardDef
{
    const char* name; // 卡名（面板与日志显示）
    const char* desc; // 效果描述（面板显示；每层生效口径与数值常量一致）
    int maxLevel;     // 层数上限；0 = 无限层（哨兵——0 上限的卡没有意义）
};

// 卡池定义表：下标与 CardId 一一对应（名称/描述/上限；数值口径见下面常量组）
inline constexpr CardDef kCardDefs[kCardCount] = {
    { "利刃", "伤害 +5", 0 },          // 利刃：无限层（maxLevel=0）
    { "疾风", "攻速 +12%", 5 },        // 疾风：冷却乘算，限 5 层
    { "长臂", "攻击范围 +15", 4 },     // 长臂：范围累加，限 4 层
    { "横扫", "张角 +15 度", 3 },      // 横扫：张角累加，限 3 层
    { "疾行", "移速 +20", 5 },         // 疾行：移速累加，限 5 层
    { "体魄", "生命上限 +20，回血 20", 0 }, // 体魄：无限层（上限与回血同值）
    { "重击", "击退 +50%", 3 },        // 重击：百分比加法，限 3 层
    { "慧眼", "经验获取 +20%", 5 },    // 慧眼：经验乘算，限 5 层
    { "聚宝", "金币磁吸 +60", 3 },     // 聚宝：磁吸半径累加，限 3 层
};

// 取卡定义（防御：异常 id 回退第一张——不崩、结果可预测）
inline const CardDef& cardDef(CardId id)
{
    // 下标换算与区域校验：合法域 [0, kCardCount)
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kCardCount)
    {
        return kCardDefs[0]; // 越界（理论不可达）：回退到第一张
    }
    return kCardDefs[index]; // 正常路径：按 id 取表项
}

// ---- 数值常量组（「生效」口径的唯一来源：公式与卡面文案都以它们为准）----
inline constexpr int kCardBladeDamagePerLevel = 5;          // 利刃：每层伤害 +5（点）
inline constexpr float kCardSwiftCooldownFactor = 0.88f;    // 疾风：每层冷却 x0.88（= 攻速 +12%）
inline constexpr float kCardReachRangePerLevel = 15.0f;     // 长臂：每层攻击范围 +15（像素）
inline constexpr float kCardSweepArcPerLevel = 15.0f;       // 横扫：每层张角 +15（度）
inline constexpr float kCardDashSpeedPerLevel = 20.0f;      // 疾行：每层移速 +20（像素/秒）
inline constexpr int kCardVitalityHpPerLevel = 20;          // 体魄：每层生命上限 +20（点）
inline constexpr int kCardVitalityHealPerLevel = 20;        // 体魄：获得一层时即时回血 20（点）
inline constexpr float kCardHeavyKnockbackPerLevel = 0.5f;  // 重击：每层击退 +50%（加法比例）
inline constexpr float kCardInsightXpPerLevel = 0.2f;       // 慧眼：每层经验获取 +20%（乘法比例）
inline constexpr float kCardTreasureMagnetPerLevel = 60.0f; // 聚宝：每层金币磁吸半径 +60（像素）

// ============================================================
// 层数工具（封顶口径的唯一实现——全部公式先经这里夹取）
// ============================================================

// 层数是否已满：有限卡（maxLevel > 0）达到/超过上限即满；无限卡恒不满。
// 抽取的「排除已满层」与选中后的「封顶」都以本判据为准
inline bool isCardFull(CardId id, int level)
{
    const CardDef& def = cardDef(id);                        // 取卡定义（上限口径）
    return def.maxLevel > 0 && level >= def.maxLevel;        // 有限卡封顶即满；无限卡恒 false
}

// 层数夹取到合法区间 [0, maxLevel]：负层按 0（防御）、超过上限按上限
//（「上限封顶」语义的落点——公式即使收到异常层数也按顶值计算）
inline int clampCardLevel(CardId id, int level)
{
    const int nonNegative = (level > 0) ? level : 0;         // 负层/0：先压到 0 起
    const CardDef& def = cardDef(id);                        // 取卡定义（上限口径）
    if (def.maxLevel <= 0)                                   // 无限卡（0 或异常负上限按无限处理）
    {
        return nonNegative;                                  // 不封顶：原样返回
    }
    return std::min(nonNegative, def.maxLevel);              // 有限卡：夹到上限
}

// 获得一层后的新层数：有限卡封顶（已满返回原值）、无限卡直接 +1。
// 选择卡牌时的唯一合法入口——「满层后不再加」的集中保证
inline int gainCardLevel(CardId id, int level)
{
    const int clamped = clampCardLevel(id, level);           // 现有层数先夹到合法区间
    const CardDef& def = cardDef(id);                        // 取卡定义（上限口径）
    if (def.maxLevel <= 0)                                   // 无限卡：无封顶
    {
        return clamped + 1;                                  // 层数 +1
    }
    return std::min(clamped + 1, def.maxLevel);              // 有限卡：+1 后不超上限
}

// ============================================================
// 叠加数学（从「基准值 + 层数」重算当前值；幂等——重复调用结果一致）
// ============================================================

// 利刃：伤害 = 基准 + 5 x 层数（累加）
inline int cardBladeDamage(int base, int level)
{
    const int lv = clampCardLevel(CardId::Blade, level);     // 层数夹取（负按 0，无限不封顶）
    return base + kCardBladeDamagePerLevel * lv;             // 每层 +5 的线性叠加
}

// 疾风：冷却 = 基准 x 0.88^层数（乘法叠乘——「攻速 +12%」的等比表达）。
// 用逐次乘法而非 powf：每层的乘算语义最直白，单测期望值可直接沿乘序列核对
inline float cardSwiftCooldown(float base, int level)
{
    const int lv = clampCardLevel(CardId::Swift, level);     // 层数夹取（限 5）
    float result = base;                                     // 从基准冷却起步
    for (int i = 0; i < lv; ++i)                             // 逐层叠乘
    {
        result *= kCardSwiftCooldownFactor;                  // 每层 x0.88
    }
    return result;                                           // 叠乘后的冷却（秒）
}

// 长臂：攻击范围 = 基准 + 15 x 层数（累加）
inline float cardReachRange(float base, int level)
{
    const int lv = clampCardLevel(CardId::Reach, level);     // 层数夹取（限 4）
    return base + kCardReachRangePerLevel * static_cast<float>(lv); // 每层 +15 像素
}

// 横扫：张角 = 基准 + 15 x 层数（累加；度）
inline float cardSweepArc(float base, int level)
{
    const int lv = clampCardLevel(CardId::Sweep, level);     // 层数夹取（限 3）
    return base + kCardSweepArcPerLevel * static_cast<float>(lv); // 每层 +15 度
}

// 疾行：移速 = 基准 + 20 x 层数（累加）
inline float cardDashSpeed(float base, int level)
{
    const int lv = clampCardLevel(CardId::Dash, level);      // 层数夹取（限 5）
    return base + kCardDashSpeedPerLevel * static_cast<float>(lv); // 每层 +20 像素/秒
}

// 体魄：生命上限 = 基准 + 20 x 层数（累加；即时回血在选择流程单独发放——
// 本函数只算上限，重算路径绝不重复加血）
inline int cardVitalityMaxHp(int base, int level)
{
    const int lv = clampCardLevel(CardId::Vitality, level);  // 层数夹取（无限不封顶）
    return base + kCardVitalityHpPerLevel * lv;              // 每层 +20 点上限
}

// 重击：击退 = 基准 x (1 + 0.5 x 层数)（百分比加法叠加——限 3 层即最高
// 基准的 2.5 倍；与疾风的乘法叠乘是两种批准口径，注释明示）
inline float cardHeavyKnockback(float base, int level)
{
    const int lv = clampCardLevel(CardId::Heavy, level);     // 层数夹取（限 3）
    const float multiplier = 1.0f + kCardHeavyKnockbackPerLevel * static_cast<float>(lv); // 1 + 0.5x层
    return base * multiplier;                                // 基准击退 x 比例
}

// 慧眼：经验乘数 = 1 + 0.2 x 层数（限 5 层即最高 2.0 倍）
inline float cardInsightXpMultiplier(int level)
{
    const int lv = clampCardLevel(CardId::Insight, level);   // 层数夹取（限 5）
    return 1.0f + kCardInsightXpPerLevel * static_cast<float>(lv); // 1 + 0.2x层
}

// 慧眼：吸收经验值 = round(基准值 x 乘数)。基准非正（防御）原样返回；
// 层数 0 时乘数为 1——原值不变（不含慧眼时与旧行为严格一致）
inline int cardInsightXpValue(int baseValue, int level)
{
    if (baseValue <= 0)                                      // 非正基准：没有可乘的经验
    {
        return baseValue;                                    // 原样返回（防御）
    }
    const float multiplier = cardInsightXpMultiplier(level); // 乘数（已夹层数）
    return static_cast<int>(std::lround(static_cast<float>(baseValue) * multiplier)); // 乘算后四舍五入
}

// 聚宝：金币磁吸范围 = 基准 + 60 x 层数（累加）
inline float cardTreasureMagnet(float base, int level)
{
    const int lv = clampCardLevel(CardId::Treasure, level);  // 层数夹取（限 3）
    return base + kCardTreasureMagnetPerLevel * static_cast<float>(lv); // 每层 +60 像素
}

// ============================================================
// 种子化随机源（场景与单测共用；xorshift32）
// ============================================================

// 宿主注入的随机源：[0,1) 均匀分布；context 为宿主自备上下文（可空）。
// 与 MonsterAiRandomFn 同款设计——单测注入固定序列、游戏注入种子流
using CardRandomFn = float (*)(void* context);

// xorshift32 种子流：构造/seed 写入种子，next01 输出 [0,1) 均匀随机数。
// 同一种子的输出序列完全可复现（「种子可复现」由它兑现）；
// state 为 0 时算法死锁，seed 把 0 置换为固定非 0 常量（确定性兜底）
struct CardSeedRandom
{
    std::uint32_t state = 0x9E3779B9u; // 内部状态（种子）；默认值非 0（可直接用）

    // 写入种子：0 置换为固定的非 0 常量（不崩、序列仍确定可复现）
    void seed(std::uint32_t s)
    {
        state = (s != 0u) ? s : 0x9E3779B9u; // 非 0 原样，否则用黄金比例常量
    }

    // 下一个 [0,1)：xorshift32 步进后取高 24 位映射（2^24 个均匀格点，
    // 对 9 张卡池的抽取足够均匀；float 在 [0,1) 上的表示精度亦为 2^-24 量级）
    float next01()
    {
        std::uint32_t x = state; // 读取当前状态
        x ^= x << 13;            // xorshift32 第一步（左移 13 异或）
        x ^= x >> 17;            // 第二步（右移 17 异或）
        x ^= x << 5;             // 第三步（左移 5 异或）——Marsaglia 经典三元组
        state = x;               // 步进结果写回状态
        return static_cast<float>(x >> 8) * (1.0f / 16777216.0f); // 高 24 位 / 2^24 -> [0,1)
    }

    // 静态 trampoline：作为 CardRandomFn 注入（context 传本对象指针）
    static float nextFrom(void* context)
    {
        return static_cast<CardSeedRandom*>(context)->next01(); // 转发到实例方法
    }
};

// ============================================================
// 三抽不重复（排除已满层、等权、不足三张按实际）
// ============================================================

// 一次抽取的结果：卡 id 数组 + 实际张数。
// 张数不足 3 的两种来源：可选池不足（满层排除后的自然结果——两张无限卡
// 兜底时池至少 2 张）与池空（防御路径，理论不可达）
struct CardDrawResult
{
    CardId cards[kChoiceDrawMax] = {}; // 抽到的卡（按展示位序 1/2/3；仅前 count 个有效）
    int count = 0;                     // 实际张数（0~3；0 = 池空，无卡可展示）
};

// 抽取一次升级选择的三张卡（保留 M2 接口与行为；批五B 起抽取核心泛化
// 到 ChoiceDraw.h——本函数只做「构建候选池 -> 通用抽取 -> 索引还原」）：
//   1. 候选池 = 全部未满层的卡（排除已满层——「满层不再入池」）；
//   2. 洗牌式逐张抽取（ChoiceDraw::drawDistinctFromPool）：剩余池中按注入
//      随机源均匀取一、抽走处用池尾填补——不重复且池内等权；
//   3. 池不足 3 张时按实际张数返回（count < 3；0 = 池空）。
// levels 为各卡当前层数数组（下标与 CardId 一一对应，长度 kCardCount）。
// fn 为随机源（nullptr 时退化为常量 0.5——确定性兜底，不崩）；fn 的
// 非法输出（NaN/负/≥1）一律夹到合法索引（防御，单测覆盖）
inline CardDrawResult drawUpgradeCards(const int* levels, CardRandomFn fn, void* context)
{
    // ---- 1) 构建候选池：顺序扫过卡表，未满层的卡索引依次入池 ----
    int pool[kCardCount]; // 候选池（卡索引；最坏 9 张——池尺寸 = 卡池张数）
    int poolSize = 0;     // 池内张数
    for (int i = 0; i < kCardCount; ++i) // 遍历全部卡位
    {
        const CardId id = static_cast<CardId>(i); // 当前卡 id
        if (!isCardFull(id, levels[i]))           // 未满层才入池（满层排除）
        {
            pool[poolSize] = i; // 写池尾（记录索引——通用抽取只认识索引）
            ++poolSize;         // 尺寸 +1
        }
    }

    // ---- 2) 通用抽取核心（批五B 泛化）：洗牌式取最多 3 个不重复索引 ----
    const ChoiceDrawResult drawn = drawDistinctFromPool(pool, poolSize, fn, context);

    // ---- 3) 索引还原为卡 id（展示位序照旧：结果位序 = 抽取位序）----
    CardDrawResult result;    // 抽取结果（默认 count=0）
    result.count = drawn.count; // 张数照搬（池不足按实际、池空为 0）
    for (int k = 0; k < drawn.count; ++k) // 逐张还原
    {
        result.cards[k] = static_cast<CardId>(drawn.indices[k]); // 索引 -> 卡 id
    }
    return result; // 交还抽取结果
}

// ============================================================
// 升级欠账队列（连升连选）
// ============================================================

// 欠账队列：升级（含溢出连升）时逐级记一笔欠账；每次选择消费一笔。
// 「欠账 > 0 即弹面板」由场景消费本类查询——纯数据，无任何依赖
class LevelUpQueue
{
public:
    // 记入 count 笔欠账（连升多级一次记多笔）；非正数（防御）忽略
    void add(int count)
    {
        if (count > 0) // 只有正数有意义（0 级升级/负值都无欠账）
        {
            m_pending += count; // 欠账累加
        }
    }

    // 消费一笔欠账并返回「是否还有剩余」：false = 欠账已清完（选择流程结束）。
    // 空队列调用：不消耗、返回 false（防御——重复调用安全）
    bool consume()
    {
        if (m_pending > 0) // 有欠账才扣
        {
            --m_pending; // 消费一笔
        }
        return m_pending > 0; // 剩余欠账 > 0？这就是「还要再弹一组」的判据
    }

    // 当前欠账数（面板标题/日志与门控用）
    int pending() const
    {
        return m_pending; // 待选次数
    }

    // 清零（R 重开：上一局的欠账不带进新一局）
    void clear()
    {
        m_pending = 0; // 归零
    }

private:
    int m_pending = 0; // 欠账数（每次升级 +1；每次选择 -1）
};

#endif // !__UPGRADECARD_H__
