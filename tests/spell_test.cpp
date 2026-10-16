// ============================================================================
//  spell_test.cpp — 法术池（Spell）单元测试
//
//  被测对象 : Src/inc/game/Spell.h（六法术表 / 逐层参数 / 层数工具 /
//             三抽抽取）+ Src/inc/game/ChoiceDraw.h（通用不重复抽取核心）
//  覆盖场景 : ① 法术表（6 系 / 名称逐一 / 上限 5 / 越界防御回退第一项）
//             ② 层数工具（clamp / 满层判据 / 获得一层封顶）
//             ③ 逐层参数（六法术 x 1~5 层全表核对：伤害/冷却/半径/跳数/
//                回复/护盾——调平总表的断言化；0 层按 1 层、超层封顶）
//             ④ 三抽抽取（不重复 / 满层排除 / 池不足按实际 / 全满池空 /
//                种子可复现 / 非法随机源防御 / nullptr 层数防御）
//             ⑤ 通用抽取核心 ChoiceDraw（池边界 / 容量截断 / 空池语义）
//             ⑥ 模拟一局（30 层学满 -> 池空 -> 清零复位 -> 池恢复）
//
//  依赖 : 两个头文件均为纯逻辑（无 SDL、无 .cpp、无链接）；随机源经注入
//  固定序列/种子，任何环境都能确定性运行
//  （构建接线见 tests/CMakeLists.txt 的 spell_test 目标）
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/Spell.h"       // 被测类型：SpellId / SpellDef / 逐层参数 / drawSpellChoices
#include "game/ChoiceDraw.h"  // 被测函数：drawDistinctFromPool（通用抽取核心）
#include "game/UpgradeCard.h" // 测试注入件：CardSeedRandom（种子流）/ LevelUpQueue（欠账队列）

#include <cmath>    // std::fabs：浮点容差比较
#include <cstdio>   // std::printf
#include <cstdlib>  // std::system（Windows 控制台切 UTF-8）
#include <cstring>  // std::strcmp：法术名断言
#include <limits>   // std::numeric_limits：构造 NaN 等极端输入

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

// 断言：布尔条件成立
void expectTrue(const char* label, bool value)
{
    // 条件为 true 才算通过
    const bool ok = value;
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
}

// 断言：浮点近似相等（容差比较——逐层减法的浮点舍入不影响判定）
void expectNear(const char* label, float got, float expect, float tolerance)
{
    // 差值不超过容差才算通过
    const bool ok = std::fabs(got - expect) <= tolerance;
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名 + 期望与实际（6 位小数够用）
    std::printf("  [%s] %s | 期望=%.6f 实际=%.6f\n", ok ? "PASS" : "FAIL", label, expect, got);
}

void beginCase(int idx, const char* title)
{
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

// ---- 测试用注入件 ----

// 固定序列随机源：依次输出预设值（循环使用）——用于「确定性输出」断言。
// 以静态 trampoline 作为 ChoiceRandomFn 注入，context 传本对象指针
struct FixedRandom
{
    const float* values; // 预设序列（非空）
    int count;           // 序列长度（> 0）
    int index = 0;       // 当前游标（取模循环）
    static float next(void* context)
    {
        FixedRandom* self = static_cast<FixedRandom*>(context); // 还原宿主对象
        const float v = self->values[self->index % self->count]; // 循环取值
        self->index++;                                          // 游标推进
        return v;                                               // 交还本次值
    }
};

} // namespace

int main()
{
#if defined(_WIN32)
    // 控制台代码页切到 UTF-8：中文输出不乱码（失败也不影响测试）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" Spell 法术池单元测试 —— Src/inc/game/Spell.h + ChoiceDraw.h\n");
    std::printf(" 法术: 火球术/冰霜新星/闪电链/治疗之光/灼烧光环/护盾术（6 系）\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 法术表：6 系、名称逐一、上限 5、越界防御
    // ------------------------------------------------------------------
    beginCase(1, "法术表（6 系 / 名称逐一 / 上限 5 / 越界防御）");
    int p0 = gPass, f0 = gFail;
    {
        // 池张数与枚举哨兵一致
        expectInt("kSpellCount = 6", kSpellCount, 6);
        expectInt("kSpellMaxLevel = 5（统一上限）", kSpellMaxLevel, 5);

        // 法术名逐一断言（顺序与 SpellId 一一对应——表错位会在此暴露）
        expectTrue("spellDef(Fireball).name = 火球术",
                   std::strcmp(spellDef(SpellId::Fireball).name, "火球术") == 0);
        expectTrue("spellDef(FrostNova).name = 冰霜新星",
                   std::strcmp(spellDef(SpellId::FrostNova).name, "冰霜新星") == 0);
        expectTrue("spellDef(ChainLightning).name = 闪电链",
                   std::strcmp(spellDef(SpellId::ChainLightning).name, "闪电链") == 0);
        expectTrue("spellDef(HolyHeal).name = 治疗之光",
                   std::strcmp(spellDef(SpellId::HolyHeal).name, "治疗之光") == 0);
        expectTrue("spellDef(BurnAura).name = 灼烧光环",
                   std::strcmp(spellDef(SpellId::BurnAura).name, "灼烧光环") == 0);
        expectTrue("spellDef(Shield).name = 护盾术",
                   std::strcmp(spellDef(SpellId::Shield).name, "护盾术") == 0);

        // 描述非空（面板文案来源；空串属于表数据损坏）
        bool descOk = true; // 全部描述非空？
        for (int i = 0; i < kSpellCount; ++i) // 逐系检查
        {
            if (spellDef(static_cast<SpellId>(i)).desc[0] == '\0') { descOk = false; } // 空描述违例
        }
        expectTrue("六系描述全部非空（面板文案完好）", descOk);

        // 越界防御：异常 id 回退第一项（不崩、结果可预测）
        expectTrue("spellDef(越界 id) 回退第一项（火球术）",
                   std::strcmp(spellDef(static_cast<SpellId>(99)).name, "火球术") == 0);
        expectTrue("spellDef(负 id) 回退第一项",
                   std::strcmp(spellDef(static_cast<SpellId>(-3)).name, "火球术") == 0);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 层数工具：clamp / 满层 / 获得一层（含防御）
    // ------------------------------------------------------------------
    beginCase(2, "层数工具（clampSpellLevel / isSpellFull / gainSpellLevel）");
    p0 = gPass; f0 = gFail;
    {
        // clamp：负层按 0、超上限封顶、合法层原样
        expectInt("clamp(-3) = 0", clampSpellLevel(-3), 0);
        expectInt("clamp(0) = 0", clampSpellLevel(0), 0);
        expectInt("clamp(3) = 3", clampSpellLevel(3), 3);
        expectInt("clamp(5) = 5", clampSpellLevel(5), 5);
        expectInt("clamp(99) = 5（封顶）", clampSpellLevel(99), 5);

        // isSpellFull：恰好到上限即满；超上限仍为满；上限下未满
        expectTrue("0 层未满", !isSpellFull(0));
        expectTrue("4 层未满", !isSpellFull(4));
        expectTrue("5 层已满", isSpellFull(5));
        expectTrue("6 层已满（超上限防御）", isSpellFull(6));
        expectTrue("负层未满（防御按 0）", !isSpellFull(-2));

        // gain：+1；满层停在一上限（不越界）；负层先归零再 +1
        expectInt("gain(0) = 1", gainSpellLevel(0), 1);
        expectInt("gain(4) = 5", gainSpellLevel(4), 5);
        expectInt("gain(5) = 5（封顶不越界）", gainSpellLevel(5), 5);
        expectInt("gain(-4) = 1（负层先归零）", gainSpellLevel(-4), 1);
        expectInt("gain(99) = 5（超层封顶）", gainSpellLevel(99), 5);
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 逐层参数：六法术 1~5 层全表核对（调平总表的断言化）
    // ------------------------------------------------------------------
    beginCase(3, "逐层参数（六法术 x 1~5 层；0 层按 1 层 / 超层封顶）");
    p0 = gPass; f0 = gFail;
    {
        // ---- 火球术：伤害 20/30/40/50/60；冷却 2.0/1.8/1.6/1.4/1.2；
        //      爆炸半径 40/50/60/70/80 ----
        expectInt("火球伤害 lv1 = 20", spellFireballDamage(1), 20);
        expectInt("火球伤害 lv3 = 40", spellFireballDamage(3), 40);
        expectInt("火球伤害 lv5 = 60", spellFireballDamage(5), 60);
        expectNear("火球冷却 lv1 = 2.0", spellFireballCooldown(1), 2.0f, 1e-5f);
        expectNear("火球冷却 lv5 = 1.2", spellFireballCooldown(5), 1.2f, 1e-5f);
        expectNear("火球爆炸半径 lv1 = 40", spellFireballBlastRadius(1), 40.0f, 1e-5f);
        expectNear("火球爆炸半径 lv5 = 80", spellFireballBlastRadius(5), 80.0f, 1e-5f);
        expectInt("火球 0 层按 1 层参数（防御）", spellFireballDamage(0), 20);
        expectInt("火球 99 层封顶按 5 层（防御）", spellFireballDamage(99), 60);

        // ---- 冰霜新星：半径 120/140/160/180/200；伤害 15/20/25/30/35；
        //      冷却 5.0/4.5/4.0/3.5/3.0；减速时长 2.0/2.5/3.0/3.5/4.0 ----
        expectNear("冰霜半径 lv1 = 120", spellFrostNovaRadius(1), 120.0f, 1e-5f);
        expectNear("冰霜半径 lv5 = 200", spellFrostNovaRadius(5), 200.0f, 1e-5f);
        expectInt("冰霜伤害 lv1 = 15", spellFrostNovaDamage(1), 15);
        expectInt("冰霜伤害 lv5 = 35", spellFrostNovaDamage(5), 35);
        expectNear("冰霜冷却 lv1 = 5.0", spellFrostNovaCooldown(1), 5.0f, 1e-5f);
        expectNear("冰霜冷却 lv5 = 3.0", spellFrostNovaCooldown(5), 3.0f, 1e-5f);
        expectNear("冰霜减速时长 lv1 = 2.0", spellFrostNovaSlowSeconds(1), 2.0f, 1e-5f);
        expectNear("冰霜减速时长 lv5 = 4.0", spellFrostNovaSlowSeconds(5), 4.0f, 1e-5f);
        expectNear("冰霜减速乘区 = 0.5（常量）", kSpellFrostSlowFactor, 0.5f, 1e-6f);

        // ---- 闪电链：伤害 18/26/34/42/50；跳数 2/3/4/5/6；
        //      冷却 3.0/2.75/2.5/2.25/2.0 ----
        expectInt("闪电伤害 lv1 = 18", spellChainDamage(1), 18);
        expectInt("闪电伤害 lv5 = 50", spellChainDamage(5), 50);
        expectInt("闪电跳数 lv1 = 2（首击 + 1 跳）", spellChainHits(1), 2);
        expectInt("闪电跳数 lv5 = 6（首击 + 5 跳）", spellChainHits(5), 6);
        expectNear("闪电冷却 lv1 = 3.0", spellChainCooldown(1), 3.0f, 1e-5f);
        expectNear("闪电冷却 lv3 = 2.5", spellChainCooldown(3), 2.5f, 1e-5f);
        expectNear("闪电冷却 lv5 = 2.0", spellChainCooldown(5), 2.0f, 1e-5f);

        // ---- 治疗之光：回复 5/9/13/17/21；冷却 6.0/5.5/5.0/4.5/4.0 ----
        expectInt("治疗回复 lv1 = 5", spellHealAmount(1), 5);
        expectInt("治疗回复 lv5 = 21", spellHealAmount(5), 21);
        expectNear("治疗冷却 lv1 = 6.0", spellHealCooldown(1), 6.0f, 1e-5f);
        expectNear("治疗冷却 lv5 = 4.0", spellHealCooldown(5), 4.0f, 1e-5f);

        // ---- 灼烧光环：半径 70/85/100/115/130；每跳伤害 4/6/8/10/12；
        //      tick 间隔 0.5s（常量）----
        expectNear("光环半径 lv1 = 70", spellAuraRadius(1), 70.0f, 1e-5f);
        expectNear("光环半径 lv5 = 130", spellAuraRadius(5), 130.0f, 1e-5f);
        expectInt("光环跳伤 lv1 = 4", spellAuraTickDamage(1), 4);
        expectInt("光环跳伤 lv5 = 12", spellAuraTickDamage(5), 12);
        expectNear("光环 tick 间隔 = 0.5（常量）", kSpellAuraTickSeconds, 0.5f, 1e-6f);

        // ---- 护盾术：护盾点数 20/30/40/50/60；冷却 8.0/7.5/7.0/6.5/6.0 ----
        expectInt("护盾点数 lv1 = 20", spellShieldPoints(1), 20);
        expectInt("护盾点数 lv5 = 60", spellShieldPoints(5), 60);
        expectNear("护盾冷却 lv1 = 8.0", spellShieldCooldown(1), 8.0f, 1e-5f);
        expectNear("护盾冷却 lv5 = 6.0", spellShieldCooldown(5), 6.0f, 1e-5f);

        // ---- 单调性：伤害类随层递增、冷却类随层递减（调平的红线性质）----
        bool monotonic = true; // 单调性是否全程成立
        for (int lv = 1; lv < kSpellMaxLevel; ++lv) // 相邻层比较
        {
            if (!(spellFireballDamage(lv + 1) > spellFireballDamage(lv))) { monotonic = false; } // 火球伤害递增
            if (!(spellFrostNovaDamage(lv + 1) > spellFrostNovaDamage(lv))) { monotonic = false; } // 冰霜伤害递增
            if (!(spellChainDamage(lv + 1) > spellChainDamage(lv))) { monotonic = false; } // 闪电伤害递增
            if (!(spellHealAmount(lv + 1) > spellHealAmount(lv))) { monotonic = false; } // 治疗递增
            if (!(spellAuraTickDamage(lv + 1) > spellAuraTickDamage(lv))) { monotonic = false; } // 光环递增
            if (!(spellShieldPoints(lv + 1) > spellShieldPoints(lv))) { monotonic = false; } // 护盾递增
            if (!(spellFireballCooldown(lv + 1) < spellFireballCooldown(lv))) { monotonic = false; } // 火球冷却递减
            if (!(spellFrostNovaCooldown(lv + 1) < spellFrostNovaCooldown(lv))) { monotonic = false; } // 冰霜冷却递减
            if (!(spellChainCooldown(lv + 1) < spellChainCooldown(lv))) { monotonic = false; } // 闪电冷却递减
            if (!(spellHealCooldown(lv + 1) < spellHealCooldown(lv))) { monotonic = false; } // 治疗冷却递减
            if (!(spellShieldCooldown(lv + 1) < spellShieldCooldown(lv))) { monotonic = false; } // 护盾冷却递减
        }
        expectTrue("全系单调性：伤害递增 / 冷却递减（1~5 层）", monotonic);
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 三抽抽取：不重复 / 满层排除 / 池不足 / 防御
    // ------------------------------------------------------------------
    beginCase(4, "三抽抽取（不重复 / 满层排除 / 池不足 / 随机源防御）");
    p0 = gPass; f0 = gFail;
    {
        int levels[kSpellCount] = {}; // 全零层：6 系全在池
        CardSeedRandom rng;           // 默认状态即可（种子非 0）
        rng.seed(1001u);              // 固定种子（序列确定，便于断言）

        // 连续抽 200 次：每次恰好 3 张、互不相同、id 在 [0,6)
        bool alwaysThree = true;    // 张数恒为 3
        bool alwaysDistinct = true; // 三张互不相同
        bool alwaysInRange = true;  // id 全部合法
        for (int iter = 0; iter < 200; ++iter) // 200 轮大样本
        {
            const SpellDrawResult draw = drawSpellChoices(levels, &CardSeedRandom::nextFrom, &rng);
            if (draw.count != 3) { alwaysThree = false; } // 池为 6：应恒抽 3 张
            if (draw.spells[0] == draw.spells[1] || draw.spells[0] == draw.spells[2] ||
                draw.spells[1] == draw.spells[2])
            {
                alwaysDistinct = false; // 出现重复：违例
            }
            for (int k = 0; k < draw.count; ++k) // 逐个检查 id 范围
            {
                const int id = static_cast<int>(draw.spells[k]); // 法术 id 数值
                if (id < 0 || id >= kSpellCount) { alwaysInRange = false; } // 越界即违例
            }
        }
        expectTrue("200 次抽取：张数恒为 3", alwaysThree);
        expectTrue("200 次抽取：三张互不相同", alwaysDistinct);
        expectTrue("200 次抽取：法术 id 全部合法", alwaysInRange);

        // 满层排除：火球满 5 层后不再入池（其余 5 系仍在——抽样应出现）
        int levels2[kSpellCount] = {}; // 独立层数数组
        levels2[static_cast<int>(SpellId::Fireball)] = 5; // 火球拉满
        bool fireballSeen = false; // 抽取结果里是否出现火球（应恒 false）
        bool othersSeen[kSpellCount] = {}; // 其余系是否出现过（都应出现）
        for (int iter = 0; iter < 200; ++iter) // 大样本
        {
            const SpellDrawResult draw = drawSpellChoices(levels2, &CardSeedRandom::nextFrom, &rng);
            for (int k = 0; k < draw.count; ++k) // 逐张记录
            {
                if (draw.spells[k] == SpellId::Fireball) { fireballSeen = true; } // 满层混入：违例
                othersSeen[static_cast<int>(draw.spells[k])] = true;              // 记录出现过
            }
        }
        expectTrue("火球满 5 层后 200 次抽取从不出现", !fireballSeen);
        bool allOthersSeen = true; // 其余 5 系是否全部出现过
        for (int i = 0; i < kSpellCount; ++i) // 逐系检查
        {
            if (i == static_cast<int>(SpellId::Fireball)) { continue; } // 跳过已满的火球
            if (!othersSeen[i]) { allOthersSeen = false; } // 未出现过：样本异常
        }
        expectTrue("其余 5 系在 200 次中全部出现过（池子完整）", allOthersSeen);

        // 池不足：5 系全满 -> 池只剩护盾 1 张（不足三张按实际）
        int levels3[kSpellCount] = {}; // 独立层数数组
        levels3[static_cast<int>(SpellId::Fireball)] = 5;      // 火球满
        levels3[static_cast<int>(SpellId::FrostNova)] = 5;     // 冰霜满
        levels3[static_cast<int>(SpellId::ChainLightning)] = 5; // 闪电满
        levels3[static_cast<int>(SpellId::HolyHeal)] = 5;      // 治疗满
        levels3[static_cast<int>(SpellId::BurnAura)] = 5;      // 光环满
        const SpellDrawResult one = drawSpellChoices(levels3, &CardSeedRandom::nextFrom, &rng);
        expectInt("5 系满层后：张数 = 1（池不足按实际）", one.count, 1);
        expectTrue("只抽到护盾术（唯一未满系）", one.spells[0] == SpellId::Shield);

        // 全满：池空 -> count = 0（场景按欠账消费跳过）
        int levels4[kSpellCount]; // 全 5 层数组
        for (int& v : levels4) { v = kSpellMaxLevel; } // 全部拉满
        const SpellDrawResult empty = drawSpellChoices(levels4, &CardSeedRandom::nextFrom, &rng);
        expectInt("全满层：池空（count = 0）", empty.count, 0);

        // nullptr 层数（防御）：返回空结果、不崩
        const SpellDrawResult nullLevels = drawSpellChoices(nullptr, &CardSeedRandom::nextFrom, &rng);
        expectInt("nullptr 层数防御：count = 0", nullLevels.count, 0);

        // 种子可复现：同种子两个流各抽 8 次，结果逐一相同
        int levels5[kSpellCount] = {}; // 全零层（池恒定，复现只看随机流）
        CardSeedRandom s1; s1.seed(777u); // 流 1
        CardSeedRandom s2; s2.seed(777u); // 流 2（同种子）
        bool drawsIdentical = true; // 抽取序列是否完全一致
        for (int i = 0; i < 8; ++i) // 8 轮比较
        {
            const SpellDrawResult d1 = drawSpellChoices(levels5, &CardSeedRandom::nextFrom, &s1);
            const SpellDrawResult d2 = drawSpellChoices(levels5, &CardSeedRandom::nextFrom, &s2);
            if (d1.count != d2.count) { drawsIdentical = false; break; } // 张数不同：违例
            for (int k = 0; k < d1.count; ++k) // 逐张比较
            {
                if (d1.spells[k] != d2.spells[k]) { drawsIdentical = false; } // 法术不同：违例
            }
        }
        expectTrue("同种子：8 轮抽取序列完全一致（可复现）", drawsIdentical);

        // 固定序列 0.0：每次取池首 —— 首张必然是火球术（池序第一）
        const float lowSeq[] = { 0.0f };      // 恒定取池首
        FixedRandom low = { lowSeq, 1, 0 };   // 固定源实例
        const SpellDrawResult lowDraw = drawSpellChoices(levels5, &FixedRandom::next, &low);
        expectTrue("固定 0.0：首张为池首（火球术）", lowDraw.spells[0] == SpellId::Fireball);
        expectInt("固定 0.0：仍抽满 3 张", lowDraw.count, 3);

        // 固定序列 0.9999：索引落到池尾 —— 首张必然为护盾术（池序最后）
        const float highSeq[] = { 0.9999f };    // 恒定取池尾
        FixedRandom high = { highSeq, 1, 0 };   // 固定源实例
        const SpellDrawResult highDraw = drawSpellChoices(levels5, &FixedRandom::next, &high);
        expectTrue("固定 0.9999：首张为池尾（护盾术）", highDraw.spells[0] == SpellId::Shield);

        // NaN 输出：夹到 0（取池首），不崩、结果合法
        const ChoiceRandomFn nanFn = [](void*) -> float // 无捕获 lambda -> 函数指针
        {
            return std::numeric_limits<float>::quiet_NaN(); // 构造 NaN
        };
        const SpellDrawResult nanDraw = drawSpellChoices(levels5, nanFn, nullptr);
        expectInt("NaN 输出：仍抽满 3 张（夹到池首）", nanDraw.count, 3);
        expectTrue("NaN 输出：首张合法（火球术）", nanDraw.spells[0] == SpellId::Fireball);

        // ≥1 输出：夹到池尾
        const ChoiceRandomFn overFn = [](void*) -> float // 超界源
        {
            return 1.5f; // 非法 ≥1
        };
        const SpellDrawResult overDraw = drawSpellChoices(levels5, overFn, nullptr);
        expectInt("≥1 输出：仍抽满 3 张", overDraw.count, 3);
        expectTrue("≥1 输出：首张合法（护盾术——夹到池尾）", overDraw.spells[0] == SpellId::Shield);

        // nullptr 随机源：退化常量 0.5 —— 首张 = 池[int(0.5x6)] = 池[3] = 治疗之光
        const SpellDrawResult nullDraw = drawSpellChoices(levels5, nullptr, nullptr);
        expectInt("nullptr 源：仍抽满 3 张", nullDraw.count, 3);
        expectTrue("nullptr 源：首张为池[3]（治疗之光，0.5 兜底）",
                   nullDraw.spells[0] == SpellId::HolyHeal);
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 通用抽取核心 ChoiceDraw：池边界与容量截断
    // ------------------------------------------------------------------
    beginCase(5, "ChoiceDraw 通用核心（池边界 / 容量截断 / 空池语义）");
    p0 = gPass; f0 = gFail;
    {
        // 三张池（值 10/20/30）：抽 3，结果互不相同且都在集合里
        const int pool3[] = { 10, 20, 30 }; // 三项候选
        CardSeedRandom rng;                 // 独立随机流
        rng.seed(55u);                      // 固定种子
        const ChoiceDrawResult d3 = drawDistinctFromPool(pool3, 3, &CardSeedRandom::nextFrom, &rng);
        expectInt("池 3 项：抽满 3", d3.count, 3);
        bool distinct3 = (d3.indices[0] != d3.indices[1] &&
                          d3.indices[0] != d3.indices[2] &&
                          d3.indices[1] != d3.indices[2]); // 不重复
        bool inSet3 = true; // 全部来自池
        for (int k = 0; k < d3.count; ++k) // 逐张检查
        {
            const int v = d3.indices[k]; // 本张值
            if (v != 10 && v != 20 && v != 30) { inSet3 = false; } // 池外值违例
        }
        expectTrue("池 3 项：结果互不相同", distinct3);
        expectTrue("池 3 项：结果全部来自池", inSet3);

        // 两顶池：count = 2（不足三张按实际）
        const int pool2[] = { 7, 8 }; // 两项候选
        const ChoiceDrawResult d2 = drawDistinctFromPool(pool2, 2, &CardSeedRandom::nextFrom, &rng);
        expectInt("池 2 项：count = 2（不足按实际）", d2.count, 2);
        expectTrue("池 2 项：结果互不相同", d2.indices[0] != d2.indices[1]);
        expectTrue("池 2 项：值合法", (d2.indices[0] == 7 || d2.indices[0] == 8) &&
                   (d2.indices[1] == 7 || d2.indices[1] == 8));

        // 空池 / nullptr：count = 0（防御）
        const ChoiceDrawResult empty1 = drawDistinctFromPool(nullptr, 5, nullptr, nullptr);
        const ChoiceDrawResult empty2 = drawDistinctFromPool(pool2, 0, nullptr, nullptr);
        const ChoiceDrawResult empty3 = drawDistinctFromPool(pool2, -3, nullptr, nullptr);
        expectInt("nullptr 池：count = 0", empty1.count, 0);
        expectInt("空池（size 0）：count = 0", empty2.count, 0);
        expectInt("负池大小：count = 0", empty3.count, 0);

        // 容量截断：20 项池（0..19）超过上限 16——抽取结果必须全部 < 16
        //（截断语义 = 「宁可少入池、绝不越界」）
        int pool20[20] = {}; // 20 项候选
        for (int i = 0; i < 20; ++i) { pool20[i] = i; } // 值 = 下标
        bool withinCap = true; // 结果是否全部落在前 16 项
        for (int iter = 0; iter < 50; ++iter) // 抽样 50 次
        {
            const ChoiceDrawResult d = drawDistinctFromPool(pool20, 20, &CardSeedRandom::nextFrom, &rng);
            for (int k = 0; k < d.count; ++k) // 逐张检查
            {
                if (d.indices[k] >= 16) { withinCap = false; } // 越容量违例
            }
        }
        expectTrue("容量截断：20 项池的抽取结果全部 < 16", withinCap);

        // 恰好容量边界：16 项池全可抽（第一张可能为任意值，只查 count）
        const ChoiceDrawResult d16 = drawDistinctFromPool(pool20, 16, &CardSeedRandom::nextFrom, &rng);
        expectInt("池 16 项（容量边界）：count = 3", d16.count, 3);
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // CASE 6 模拟一局：逐级学习 -> 满层退池 -> 池空 -> 清零复位
    // ------------------------------------------------------------------
    beginCase(6, "模拟一局（30 层学满 -> 池空 -> 清零复位 -> 池恢复）");
    p0 = gPass; f0 = gFail;
    {
        int levels[kSpellCount] = {}; // 局内法术层数（从零开始）
        LevelUpQueue queue;           // 欠账队列（模拟连升——复用升级卡队列）
        CardSeedRandom rng;           // 抽取随机流
        rng.seed(2026u);              // 固定种子（流程可复现）

        bool cappedOk = true; // 全程「层数不超上限」是否成立
        bool queueOk = true;  // 全程欠账队列是否配对（记账=消费+剩余）
        int accounted = 0;    // 已记账欠账数
        int consumed = 0;     // 已消费数

        for (int round = 0; round < 60; ++round) // 模拟 60 次升级选择（足以 30 层学满）
        {
            queue.add(1);  // 记一笔欠账（模拟一次升级）
            ++accounted;   // 账面同步
            if (queue.pending() <= 0) { queueOk = false; } // 记账后应欠账

            const SpellDrawResult draw = drawSpellChoices(levels, &CardSeedRandom::nextFrom, &rng);
            if (draw.count <= 0) // 池空：场景语义 = 消费欠账并跳过
            {
                queue.consume(); // 消费一笔
                ++consumed;      // 账面同步
                continue;        // 本轮无选择
            }

            const SpellId chosen = draw.spells[0]; // 模拟「选第 1 张」
            levels[static_cast<int>(chosen)] = gainSpellLevel(levels[static_cast<int>(chosen)]); // 层数 +1
            queue.consume(); // 消费一笔
            ++consumed;      // 账面同步

            // 每轮检查：所有系层数不超过上限
            for (int i = 0; i < kSpellCount; ++i) // 逐系校验
            {
                if (levels[i] > kSpellMaxLevel) { cappedOk = false; } // 越界：违例
            }
        }

        expectTrue("60 轮全程：层数从不越过上限", cappedOk);
        expectTrue("60 轮全程：欠账记账与消费配对", queueOk && accounted == consumed);
        expectInt("60 轮后欠账清空", queue.pending(), 0);

        // 池的极限：60 次选择足以把 6 系全部拉满（总 30 层）——
        // 此后池空（count == 0）
        const SpellDrawResult late = drawSpellChoices(levels, &CardSeedRandom::nextFrom, &rng);
        expectInt("长期模拟后：池空（count = 0）", late.count, 0);
        bool allCapped = true; // 六系是否全部满层
        for (int i = 0; i < kSpellCount; ++i) // 逐系检查
        {
            if (levels[i] < kSpellMaxLevel) { allCapped = false; } // 未满：推断失效
        }
        expectTrue("长期模拟后：六系全部满层（5x6 = 30 层）", allCapped);

        // 「R 重开」模拟：层数清零 -> 全系重新入池（下次抽取 count 回到 3）
        for (int& v : levels) { v = 0; } // 清零（restart 的纯逻辑等价）
        const SpellDrawResult fresh = drawSpellChoices(levels, &CardSeedRandom::nextFrom, &rng);
        expectInt("重开清零后：池恢复（count = 3）", fresh.count, 3);
    }
    endCase(6, p0, f0);

    // ------------------------------------------------------------------
    // 汇总
    // ------------------------------------------------------------------
    const int total = gPass + gFail;
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 条断言 | 通过 %d | 失败 %d\n", total, gPass, gFail);
    std::printf(" 结论: %s\n", (gFail == 0)
        ? "全部通过 —— 法术表/逐层参数/层数工具/三抽抽取行为符合预期。"
        : "存在失败用例，请检查上方 [FAIL] 行。");
    std::printf(" main 返回值（失败用例数）: %d\n", gFail);
    std::printf("==============================================================\n");

    return gFail; // 失败数作为退出码：全部通过时为 0
}
