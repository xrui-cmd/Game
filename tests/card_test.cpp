// ============================================================================
//  card_test.cpp — 三选一升级卡（UpgradeCard）单元测试
//
//  被测对象 : Src/inc/game/UpgradeCard.h（game 层纯逻辑头文件，header-only）
//  覆盖场景 : ① 卡表与层数工具（9 张表项 / 上限 / 封顶 / 无限卡）
//             ② 叠加数学（冷却乘算 / 加成累加 / 上限封顶 / 经验四舍五入）
//             ③ 三抽不重复 + 排除已满层 + 不足三张按实际（两张无限卡兜底）
//             ④ 种子化随机源（同种子可复现、值域 [0,1)、seed(0) 防御）
//             ⑤ 随机源注入的非法输出防御（NaN / 负 / ≥1 / nullptr）
//             ⑥ 等权性（大样本首抽频率统计）
//             ⑦ 欠账队列（连升连选：add/consume/pending/clear）
//             ⑧ 模拟一局（抽取 -> 选择 -> 封顶 -> 池缩到两张无限卡）
//             ⑨ 卡面板布局（批五A：CardLayout 居中/间距/命中/越界防御——
//                渲染与鼠标命中同一份计算，防坐标漂移）
//
//  与其余测试的差异 : ①~⑧ 测「无任何依赖」的纯逻辑头文件；⑨ 额外引入
//  CardLayout.h（同样 header-only 纯函数，只依赖 SDL_rect.h 的类型与
//  内联点测——不链接 SDL3 运行时），任何机器上都能确定性运行
//  （构建接线见 tests/CMakeLists.txt 的 card_test 目标）
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/UpgradeCard.h" // 被测类型：CardId / CardDef / 抽取 / 叠加数学 / 欠账队列
#include "game/CardLayout.h"  // 被测函数：cardPanelRectAt / cardPanelHitTest（批五A 布局）

#include <cmath>    // std::fabs：浮点容差比较
#include <cstdio>   // std::printf
#include <cstdlib>  // std::system（Windows 控制台切 UTF-8）
#include <cstring>  // std::strcmp：卡名断言
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

// 断言：浮点近似相等（容差比较——乘算序列的浮点舍入不影响判定）
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
// 以静态 trampoline 作为 CardRandomFn 注入，context 传本对象指针
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
    std::printf(" UpgradeCard 三选一升级卡单元测试 —— Src/inc/game/UpgradeCard.h\n");
    std::printf(" 卡池: 利刃/疾风/长臂/横扫/疾行/体魄/重击/慧眼/聚宝（9 张）\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 卡表与层数工具：9 张表项、上限口径、封顶与无限卡
    // ------------------------------------------------------------------
    beginCase(1, "卡表（名称/上限）与层数工具（isCardFull/clamp/gain）");
    int p0 = gPass, f0 = gFail;
    {
        // 卡池张数与枚举哨兵一致
        expectInt("kCardCount = 9", kCardCount, 9);

        // 卡名逐一断言（顺序与 CardId 一一对应——卡表错位会在此暴露）
        expectTrue("cardDef(Blade).name = 利刃", std::strcmp(cardDef(CardId::Blade).name, "利刃") == 0);
        expectTrue("cardDef(Swift).name = 疾风", std::strcmp(cardDef(CardId::Swift).name, "疾风") == 0);
        expectTrue("cardDef(Reach).name = 长臂", std::strcmp(cardDef(CardId::Reach).name, "长臂") == 0);
        expectTrue("cardDef(Sweep).name = 横扫", std::strcmp(cardDef(CardId::Sweep).name, "横扫") == 0);
        expectTrue("cardDef(Dash).name = 疾行", std::strcmp(cardDef(CardId::Dash).name, "疾行") == 0);
        expectTrue("cardDef(Vitality).name = 体魄", std::strcmp(cardDef(CardId::Vitality).name, "体魄") == 0);
        expectTrue("cardDef(Heavy).name = 重击", std::strcmp(cardDef(CardId::Heavy).name, "重击") == 0);
        expectTrue("cardDef(Insight).name = 慧眼", std::strcmp(cardDef(CardId::Insight).name, "慧眼") == 0);
        expectTrue("cardDef(Treasure).name = 聚宝", std::strcmp(cardDef(CardId::Treasure).name, "聚宝") == 0);

        // 层数上限逐一断言（规格表：无限 0 / 5 / 4 / 3 / 5 / 无限 0 / 3 / 5 / 3）
        expectInt("利刃上限 = 0（无限）", cardDef(CardId::Blade).maxLevel, 0);
        expectInt("疾风上限 = 5", cardDef(CardId::Swift).maxLevel, 5);
        expectInt("长臂上限 = 4", cardDef(CardId::Reach).maxLevel, 4);
        expectInt("横扫上限 = 3", cardDef(CardId::Sweep).maxLevel, 3);
        expectInt("疾行上限 = 5", cardDef(CardId::Dash).maxLevel, 5);
        expectInt("体魄上限 = 0（无限）", cardDef(CardId::Vitality).maxLevel, 0);
        expectInt("重击上限 = 3", cardDef(CardId::Heavy).maxLevel, 3);
        expectInt("慧眼上限 = 5", cardDef(CardId::Insight).maxLevel, 5);
        expectInt("聚宝上限 = 3", cardDef(CardId::Treasure).maxLevel, 3);

        // isCardFull：无限卡恒不满（天文层数也不满）
        expectTrue("利刃 100 层不满（无限）", !isCardFull(CardId::Blade, 100));
        expectTrue("体魄 999 层不满（无限）", !isCardFull(CardId::Vitality, 999));
        // isCardFull：有限卡在「>= 上限」时判满（恰好到上限即满）
        expectTrue("疾风 4 层未满", !isCardFull(CardId::Swift, 4));
        expectTrue("疾风 5 层已满", isCardFull(CardId::Swift, 5));
        expectTrue("疾风 6 层已满（超上限防御）", isCardFull(CardId::Swift, 6));

        // clampCardLevel：负层按 0、有限卡封顶、无限卡不封顶
        expectInt("clamp(横扫, -3) = 0", clampCardLevel(CardId::Sweep, -3), 0);
        expectInt("clamp(横扫, 99) = 3（封顶）", clampCardLevel(CardId::Sweep, 99), 3);
        expectInt("clamp(利刃, 99) = 99（无限不封顶）", clampCardLevel(CardId::Blade, 99), 99);

        // gainCardLevel：当前合法层 +1；满层仍回上限（不会越界）；无限卡持续增长
        expectInt("gain(疾风, 0) = 1", gainCardLevel(CardId::Swift, 0), 1);
        expectInt("gain(疾风, 4) = 5", gainCardLevel(CardId::Swift, 4), 5);
        expectInt("gain(疾风, 5) = 5（封顶不越界）", gainCardLevel(CardId::Swift, 5), 5);
        expectInt("gain(利刃, 100) = 101（无限）", gainCardLevel(CardId::Blade, 100), 101);
        expectInt("gain(利刃, -2) = 1（负层先归零）", gainCardLevel(CardId::Blade, -2), 1);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 叠加数学：冷却乘算、加成累加、上限封顶、经验乘算与舍入
    // ------------------------------------------------------------------
    beginCase(2, "叠加数学（乘算/累加/封顶/四舍五入）");
    p0 = gPass; f0 = gFail;
    {
        // 利刃：25 + 5 x 层（累加；无限卡不封顶）
        expectInt("利刃 lv0: 25", cardBladeDamage(25, 0), 25);
        expectInt("利刃 lv1: 30", cardBladeDamage(25, 1), 30);
        expectInt("利刃 lv10: 75", cardBladeDamage(25, 10), 75);
        expectInt("利刃 lv-5（负层按 0）: 25", cardBladeDamage(25, -5), 25);

        // 疾风：0.4 x 0.88^层（乘算；限 5——超限层数先被夹到 5）
        expectNear("疾风 lv0: 0.4", cardSwiftCooldown(0.4f, 0), 0.4f, 1e-6f);
        expectNear("疾风 lv1: 0.4x0.88 = 0.352", cardSwiftCooldown(0.4f, 1), 0.352f, 1e-5f);
        // 0.88^5 = 0.5277319168 -> 0.4 x = 0.21109276672
        expectNear("疾风 lv5: 0.4x0.88^5 = 0.211093", cardSwiftCooldown(0.4f, 5), 0.21109277f, 1e-4f);
        // 超上限层数（99）封顶到 5 层：结果与 5 层一致（上限封顶语义）
        expectNear("疾风 lv99 封顶 = lv5 值", cardSwiftCooldown(0.4f, 99),
                   cardSwiftCooldown(0.4f, 5), 1e-6f);

        // 长臂：96 + 15 x 层（限 4 -> 最高 156）
        expectNear("长臂 lv0: 96", cardReachRange(96.0f, 0), 96.0f, 1e-6f);
        expectNear("长臂 lv4: 156", cardReachRange(96.0f, 4), 156.0f, 1e-6f);
        expectNear("长臂 lv99 封顶 = 156", cardReachRange(96.0f, 99), 156.0f, 1e-6f);

        // 横扫：120 + 15 x 层（限 3 -> 最高 165）
        expectNear("横扫 lv0: 120", cardSweepArc(120.0f, 0), 120.0f, 1e-6f);
        expectNear("横扫 lv3: 165", cardSweepArc(120.0f, 3), 165.0f, 1e-6f);
        expectNear("横扫 lv99 封顶 = 165", cardSweepArc(120.0f, 99), 165.0f, 1e-6f);

        // 疾行：300 + 20 x 层（限 5 -> 最高 400）
        expectNear("疾行 lv0: 300", cardDashSpeed(300.0f, 0), 300.0f, 1e-6f);
        expectNear("疾行 lv5: 400", cardDashSpeed(300.0f, 5), 400.0f, 1e-6f);
        expectNear("疾行 lv99 封顶 = 400", cardDashSpeed(300.0f, 99), 400.0f, 1e-6f);

        // 体魄：100 + 20 x 层（无限卡：累加不封顶）
        expectInt("体魄 lv0: 100", cardVitalityMaxHp(100, 0), 100);
        expectInt("体魄 lv3: 160", cardVitalityMaxHp(100, 3), 160);
        expectInt("体魄 lv50: 1100（无限不封顶）", cardVitalityMaxHp(100, 50), 1100);

        // 重击：30 x (1 + 0.5 x 层)（百分比加法叠加；限 3 -> 最高 75）
        expectNear("重击 lv0: 30", cardHeavyKnockback(30.0f, 0), 30.0f, 1e-6f);
        expectNear("重击 lv1: 45", cardHeavyKnockback(30.0f, 1), 45.0f, 1e-6f);
        expectNear("重击 lv3: 75", cardHeavyKnockback(30.0f, 3), 75.0f, 1e-6f);
        expectNear("重击 lv99 封顶 = 75", cardHeavyKnockback(30.0f, 99), 75.0f, 1e-6f);

        // 慧眼：乘数 = 1 + 0.2 x 层（限 5 -> 最高 2.0）
        expectNear("慧眼乘数 lv0: 1.0", cardInsightXpMultiplier(0), 1.0f, 1e-6f);
        expectNear("慧眼乘数 lv5: 2.0", cardInsightXpMultiplier(5), 2.0f, 1e-6f);
        expectNear("慧眼乘数 lv99 封顶 = 2.0", cardInsightXpMultiplier(99), 2.0f, 1e-6f);
        // 慧眼经验值：10 -> 12（lv1）/ 20（lv5）；非整数结果四舍五入（3x1.2=3.6->4）
        expectInt("慧眼 lv0: 10 点原值", cardInsightXpValue(10, 0), 10);
        expectInt("慧眼 lv1: 10 -> 12", cardInsightXpValue(10, 1), 12);
        expectInt("慧眼 lv5: 10 -> 20", cardInsightXpValue(10, 5), 20);
        expectInt("慧眼 lv1: 3 -> round(3.6) = 4", cardInsightXpValue(3, 1), 4);
        expectInt("慧眼非正基准（防御）: 0 -> 0", cardInsightXpValue(0, 3), 0);

        // 聚宝：140 + 60 x 层（限 3 -> 最高 320）
        expectNear("聚宝 lv0: 140", cardTreasureMagnet(140.0f, 0), 140.0f, 1e-6f);
        expectNear("聚宝 lv3: 320", cardTreasureMagnet(140.0f, 3), 320.0f, 1e-6f);
        expectNear("聚宝 lv99 封顶 = 320", cardTreasureMagnet(140.0f, 99), 320.0f, 1e-6f);
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 抽取：三张不重复、都在池内、排除已满层、不足三张按实际
    // ------------------------------------------------------------------
    beginCase(3, "三抽不重复 + 满层排除 + 不足三张（两张无限卡兜底）");
    p0 = gPass; f0 = gFail;
    {
        int levels[kCardCount] = {}; // 全零层：9 张全在池
        CardSeedRandom rng;          // 默认状态即可（种子非 0）
        rng.seed(1001u);             // 固定种子（序列确定，便于断言）

        // 连续抽 200 次：每次恰好 3 张、三张互不相同、id 在 [0,9)
        bool alwaysThree = true;     // 张数恒为 3
        bool alwaysDistinct = true;  // 三张互不相同
        bool alwaysInRange = true;   // id 全部合法
        for (int iter = 0; iter < 200; ++iter) // 200 轮大样本
        {
            const CardDrawResult draw = drawUpgradeCards(levels, &CardSeedRandom::nextFrom, &rng);
            if (draw.count != 3) { alwaysThree = false; } // 池为 9：应恒抽 3 张
            if (draw.cards[0] == draw.cards[1] || draw.cards[0] == draw.cards[2] ||
                draw.cards[1] == draw.cards[2])
            {
                alwaysDistinct = false; // 出现重复：违例
            }
            for (int k = 0; k < draw.count; ++k) // 逐个检查 id 范围
            {
                const int id = static_cast<int>(draw.cards[k]); // 卡 id 数值
                if (id < 0 || id >= kCardCount) { alwaysInRange = false; } // 越界即违例
            }
        }
        expectTrue("200 次抽取：张数恒为 3", alwaysThree);
        expectTrue("200 次抽取：三张互不相同", alwaysDistinct);
        expectTrue("200 次抽取：卡 id 全部合法", alwaysInRange);

        // 排除已满层：疾风满 5 层后不再入池（其他 8 张仍在——抽样统计出现）
        int levels2[kCardCount] = {};  // 独立层数数组
        levels2[static_cast<int>(CardId::Swift)] = 5; // 疾风拉满
        bool swiftSeen = false;        // 抽取结果里是否出现疾风（应恒 false）
        bool othersSeen[kCardCount] = {}; // 其余卡是否出现过（都应出现）
        for (int iter = 0; iter < 200; ++iter) // 大样本
        {
            const CardDrawResult draw = drawUpgradeCards(levels2, &CardSeedRandom::nextFrom, &rng);
            for (int k = 0; k < draw.count; ++k) // 逐张记录
            {
                if (draw.cards[k] == CardId::Swift) { swiftSeen = true; } // 满层卡混入：违例
                othersSeen[static_cast<int>(draw.cards[k])] = true;       // 记录出现过
            }
        }
        expectTrue("疾风满 5 层后 200 次抽取从不出现", !swiftSeen);
        bool allOthersSeen = true; // 其余 8 张是否全部出现过
        for (int i = 0; i < kCardCount; ++i) // 逐个检查
        {
            if (i == static_cast<int>(CardId::Swift)) { continue; } // 跳过已满的疾风
            if (!othersSeen[i]) { allOthersSeen = false; } // 未出现过：样本异常
        }
        expectTrue("其余 8 张在 200 次中全部出现过（池子完整）", allOthersSeen);

        // 不足三张：7 张有限卡全部拉满 -> 池只剩利刃/体魄两张无限卡
        int levels3[kCardCount] = {}; // 独立层数数组
        levels3[static_cast<int>(CardId::Swift)] = 5;     // 疾风拉满
        levels3[static_cast<int>(CardId::Reach)] = 4;     // 长臂拉满
        levels3[static_cast<int>(CardId::Sweep)] = 3;     // 横扫拉满
        levels3[static_cast<int>(CardId::Dash)] = 5;      // 疾行拉满
        levels3[static_cast<int>(CardId::Heavy)] = 3;     // 重击拉满
        levels3[static_cast<int>(CardId::Insight)] = 5;   // 慧眼拉满
        levels3[static_cast<int>(CardId::Treasure)] = 3;  // 聚宝拉满
        bool exactlyTwo = true;    // 张数恒为 2
        bool onlyUnlimited = true; // 只出现两张无限卡且不重复
        for (int iter = 0; iter < 50; ++iter) // 多次取样
        {
            const CardDrawResult draw = drawUpgradeCards(levels3, &CardSeedRandom::nextFrom, &rng);
            if (draw.count != 2) { exactlyTwo = false; } // 池 2 张：应恒抽 2 张
            const bool isBlade0 = draw.cards[0] == CardId::Blade;     // 首张是利刃？
            const bool isVitality0 = draw.cards[0] == CardId::Vitality; // 首张是体魄？
            const bool isBlade1 = draw.cards[1] == CardId::Blade;     // 次张是利刃？
            const bool isVitality1 = draw.cards[1] == CardId::Vitality; // 次张是体魄？
            if (!((isBlade0 || isVitality0) && (isBlade1 || isVitality1) && draw.cards[0] != draw.cards[1]))
            {
                onlyUnlimited = false; // 混入非无限卡或重复：违例
            }
        }
        expectTrue("7 张有限卡满层后：张数恒为 2（不足三张按实际）", exactlyTwo);
        expectTrue("池中恒为利刃/体魄两张且不重复（无限卡兜底）", onlyUnlimited);

        // 负层数（防御）按未满处理：所有卡仍可入池
        int levels4[kCardCount]; // 全 -5 的层数数组
        for (int& v : levels4) { v = -5; } // 逐项填充负值
        const CardDrawResult negDraw = drawUpgradeCards(levels4, &CardSeedRandom::nextFrom, &rng);
        expectInt("层数全为负（防御按 0）：仍抽满 3 张", negDraw.count, 3);
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 种子化随机源：同种子可复现、值域、seed(0) 防御
    // ------------------------------------------------------------------
    beginCase(4, "种子化随机源（xorshift32：可复现 / 值域 / 0 种子防御）");
    p0 = gPass; f0 = gFail;
    {
        // 同种子前 24 个输出逐一相等（序列完全可复现）
        CardSeedRandom a; // 流 A
        CardSeedRandom b; // 流 B
        a.seed(20261016u); // 同一种子
        b.seed(20261016u); // 同上
        bool identical = true; // 前 24 个值是否逐一相等
        for (int i = 0; i < 24; ++i) // 逐值比较
        {
            if (a.next01() != b.next01()) { identical = false; break; } // 出现差异：不可复现
        }
        expectTrue("同种子：前 24 个随机数逐一相等", identical);

        // 值域：next01 恒落在 [0,1)
        CardSeedRandom rng; // 独立流
        rng.seed(555u);     // 任意种子
        bool inRange = true; // 值域是否全程合法
        for (int i = 0; i < 1000; ++i) // 大样本
        {
            const float v = rng.next01(); // 取一个值
            if (!(v >= 0.0f && v < 1.0f)) { inRange = false; break; } // 越界：违例
        }
        expectTrue("next01 恒落在 [0,1)（1000 次）", inRange);

        // seed(0) 防御：算法状态不为 0（不退化死锁），输出仍然合法
        CardSeedRandom zeroSeed; // 零种子流
        zeroSeed.seed(0u);       // 0 会被置换为固定非 0 常量
        bool zeroSeedOk = true;  // 置换后输出是否合法
        for (int i = 0; i < 50; ++i) // 取样
        {
            const float v = zeroSeed.next01(); // 取一个值
            if (!(v >= 0.0f && v < 1.0f)) { zeroSeedOk = false; break; } // 越界/NaN：违例
        }
        expectTrue("seed(0) 置换非 0：输出仍合法（不崩不死锁）", zeroSeedOk);

        // 抽取序列复现：同种子两个流各抽 8 次，结果逐一相同
        int levels[kCardCount] = {}; // 全零层（池恒定，复现只看随机流）
        CardSeedRandom s1; s1.seed(777u); // 流 1
        CardSeedRandom s2; s2.seed(777u); // 流 2（同种子）
        bool drawsIdentical = true; // 抽取序列是否完全一致
        for (int i = 0; i < 8; ++i) // 8 轮比较
        {
            const CardDrawResult d1 = drawUpgradeCards(levels, &CardSeedRandom::nextFrom, &s1);
            const CardDrawResult d2 = drawUpgradeCards(levels, &CardSeedRandom::nextFrom, &s2);
            if (d1.count != d2.count) { drawsIdentical = false; break; } // 张数不同：违例
            for (int k = 0; k < d1.count; ++k) // 逐张比较
            {
                if (d1.cards[k] != d2.cards[k]) { drawsIdentical = false; } // 卡不同：违例
            }
        }
        expectTrue("同种子：8 轮抽取序列完全一致（可复现）", drawsIdentical);

        // 不同种子：8 轮抽取序列不完全相同（碰撞概率可忽略）
        CardSeedRandom s3; s3.seed(1u);     // 种子 1
        CardSeedRandom s4; s4.seed(2u);     // 种子 2
        bool anyDifferent = false; // 是否出现差异
        for (int i = 0; i < 8; ++i) // 8 轮比较
        {
            const CardDrawResult d3 = drawUpgradeCards(levels, &CardSeedRandom::nextFrom, &s3);
            const CardDrawResult d4 = drawUpgradeCards(levels, &CardSeedRandom::nextFrom, &s4);
            if (d3.count != d4.count) { anyDifferent = true; break; } // 张数不同即差异
            for (int k = 0; k < d3.count; ++k) // 逐张比较
            {
                if (d3.cards[k] != d4.cards[k]) { anyDifferent = true; } // 卡不同即差异
            }
        }
        expectTrue("不同种子：序列出现差异（种子确实生效）", anyDifferent);
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 注入随机源的边界：固定序列的确定性 + 非法输出防御
    // ------------------------------------------------------------------
    beginCase(5, "随机源注入（固定序列确定性 / NaN / 负 / ≥1 / nullptr）");
    p0 = gPass; f0 = gFail;
    {
        int levels[kCardCount] = {}; // 全零层

        // 固定序列全 0：每次取池首 —— 首张必然是卡表第一张「利刃」
        const float lowSeq[] = { 0.0f };          // 恒定取池首
        FixedRandom low = { lowSeq, 1, 0 };       // 固定源实例
        const CardDrawResult lowDraw = drawUpgradeCards(levels, &FixedRandom::next, &low);
        expectTrue("固定 0.0：首张为池首（利刃）", lowDraw.cards[0] == CardId::Blade);
        expectInt("固定 0.0：仍抽满 3 张", lowDraw.count, 3);

        // 固定序列 0.9999：索引落到池尾附近 —— 首张必然是卡表最后一张「聚宝」
        const float highSeq[] = { 0.9999f };      // 恒定取池尾
        FixedRandom high = { highSeq, 1, 0 };     // 固定源实例
        const CardDrawResult highDraw = drawUpgradeCards(levels, &FixedRandom::next, &high);
        expectTrue("固定 0.9999：首张为池尾（聚宝）", highDraw.cards[0] == CardId::Treasure);

        // NaN 输出：夹到 0（取池首），不崩、结果合法
        const CardRandomFn nanFn = [](void*) -> float // 无捕获 lambda -> 函数指针
        {
            return std::numeric_limits<float>::quiet_NaN(); // 构造 NaN
        };
        const CardDrawResult nanDraw = drawUpgradeCards(levels, nanFn, nullptr);
        expectInt("NaN 输出：仍抽满 3 张（夹到池首）", nanDraw.count, 3);
        expectTrue("NaN 输出：首张合法（利刃）", nanDraw.cards[0] == CardId::Blade);

        // 负输出：同样夹到 0
        const CardRandomFn negFn = [](void*) -> float // 负值源
        {
            return -7.5f; // 非法负值
        };
        const CardDrawResult negDraw = drawUpgradeCards(levels, negFn, nullptr);
        expectInt("负输出：仍抽满 3 张", negDraw.count, 3);
        expectTrue("负输出：首张合法（利刃）", negDraw.cards[0] == CardId::Blade);

        // ≥1 输出：夹到池尾（下标越界防御）
        const CardRandomFn overFn = [](void*) -> float // 超界源
        {
            return 1.5f; // 非法 ≥1
        };
        const CardDrawResult overDraw = drawUpgradeCards(levels, overFn, nullptr);
        expectInt("≥1 输出：仍抽满 3 张", overDraw.count, 3);
        expectTrue("≥1 输出：首张合法（聚宝——夹到池尾）", overDraw.cards[0] == CardId::Treasure);
        expectTrue("≥1 输出：三张不重复", overDraw.cards[0] != overDraw.cards[1] &&
                   overDraw.cards[0] != overDraw.cards[2] && overDraw.cards[1] != overDraw.cards[2]);

        // nullptr 随机源：退化常量 0.5（确定性兜底）——首张 = 池[int(0.5x9)] = 池[4] = 疾行
        const CardDrawResult nullDraw = drawUpgradeCards(levels, nullptr, nullptr);
        expectInt("nullptr 源：仍抽满 3 张", nullDraw.count, 3);
        expectTrue("nullptr 源：首张为池[4]（疾行，0.5 兜底）", nullDraw.cards[0] == CardId::Dash);
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // CASE 6 等权性：大样本首抽频率均匀（池内每张概率相同）
    // ------------------------------------------------------------------
    beginCase(6, "等权性（9000 次首抽频率统计）");
    p0 = gPass; f0 = gFail;
    {
        int levels[kCardCount] = {};   // 全零层（池 9 张）
        CardSeedRandom rng;            // 独立随机流
        rng.seed(42u);                 // 固定种子（样本可复现）

        int firstCount[kCardCount] = {}; // 每张卡作为首张出现的次数
        const int rounds = 9000;         // 样本量：均值 1000
        for (int i = 0; i < rounds; ++i) // 大样本
        {
            const CardDrawResult draw = drawUpgradeCards(levels, &CardSeedRandom::nextFrom, &rng);
            firstCount[static_cast<int>(draw.cards[0])]++; // 首张计数
        }

        // 每张频率应落在均值 ±30%（xorshift32 的均匀性远超此界限；
        // 界限放宽只为容许统计涨落，同时能敏感捕获「权重偏置」类缺陷）
        bool balanced = true; // 频率是否全部在界内
        int minCount = rounds; // 最小频率（报告用）
        int maxCount = 0;      // 最大频率（报告用）
        for (int i = 0; i < kCardCount; ++i) // 逐张检查
        {
            if (firstCount[i] < minCount) { minCount = firstCount[i]; } // 更新最小
            if (firstCount[i] > maxCount) { maxCount = firstCount[i]; } // 更新最大
            if (firstCount[i] < 700 || firstCount[i] > 1300) { balanced = false; } // 越界：偏置
        }
        expectTrue("9000 次首抽：每张频率均在 700~1300（均值 1000 ± 30%）", balanced);
        std::printf("        （频率区间：min=%d max=%d，均值=1000）\n", minCount, maxCount);
    }
    endCase(6, p0, f0);

    // ------------------------------------------------------------------
    // CASE 7 欠账队列：连升连选（add/consume/pending/clear）
    // ------------------------------------------------------------------
    beginCase(7, "欠账队列（升级记账 / 选择消费 / 清零 / 防御）");
    p0 = gPass; f0 = gFail;
    {
        LevelUpQueue queue; // 空队列起步
        expectInt("初始欠账 = 0", queue.pending(), 0);
        expectTrue("空队列 consume 返回 false（空转安全）", !queue.consume());
        expectInt("空转后欠账仍 = 0", queue.pending(), 0);

        // 连升 2 级：记两笔欠账 -> 连选两次
        queue.add(2); // 一次连升记两笔
        expectInt("add(2) 后欠账 = 2", queue.pending(), 2);
        expectTrue("第 1 次 consume：还有欠账（true）", queue.consume());
        expectInt("第 1 次消费后欠账 = 1", queue.pending(), 1);
        expectTrue("第 2 次 consume：欠账清完（false）", !queue.consume());
        expectInt("第 2 次消费后欠账 = 0", queue.pending(), 0);

        // 两次单升级（各一笔）：累加语义
        queue.add(1); // 第一次升级
        queue.add(1); // 第二次升级
        expectInt("add(1)+add(1) 后欠账 = 2", queue.pending(), 2);

        // 防御：非正数输入不产生欠账
        queue.clear();          // 先清零
        queue.add(0);           // 0 笔：忽略
        queue.add(-3);          // 负笔：忽略
        expectInt("add(0)/add(-3) 后欠账 = 0（防御）", queue.pending(), 0);

        // 清零（R 重开）：场上欠账作废
        queue.add(5);      // 堆积 5 笔
        queue.clear();     // 重开清零
        expectInt("clear 后欠账 = 0", queue.pending(), 0);
        expectTrue("clear 后 consume 返回 false", !queue.consume());
    }
    endCase(7, p0, f0);

    // ------------------------------------------------------------------
    // CASE 8 模拟一局：抽取 -> 选择 -> 封顶 -> 池缩到两张无限卡
    // ------------------------------------------------------------------
    beginCase(8, "模拟一局（100 次选择：不越界封顶、满层退池、池缩到两张）");
    p0 = gPass; f0 = gFail;
    {
        int levels[kCardCount] = {};    // 局内层数（从零开始）
        LevelUpQueue queue;             // 欠账队列（模拟连升）
        CardSeedRandom rng;             // 卡池随机源
        rng.seed(2026u);                // 固定种子（流程可复现）

        bool cappedOk = true;   // 全程「层数不超上限」是否成立
        bool queueOk = true;    // 全程欠账队列是否配对（记账=消费+剩余）
        int accounted = 0;      // 已记账欠账数
        int consumed = 0;       // 已消费数

        for (int round = 0; round < 100; ++round) // 模拟 100 次升级选择
        {
            queue.add(1);  // 记一笔欠账（模拟一次升级）
            ++accounted;   // 账面同步
            if (queue.pending() <= 0) { queueOk = false; } // 记账后应欠账

            const CardDrawResult draw = drawUpgradeCards(levels, &CardSeedRandom::nextFrom, &rng);
            if (draw.count <= 0) { cappedOk = false; break; } // 恒有卡可抽（两张无限兜底）

            const CardId chosen = draw.cards[0]; // 模拟「选第 1 张」
            levels[static_cast<int>(chosen)] = gainCardLevel(chosen, levels[static_cast<int>(chosen)]); // 层数 +1
            queue.consume(); // 消费一笔
            ++consumed;      // 账面同步

            // 每轮检查：所有有限卡层数不超过上限
            for (int i = 0; i < kCardCount; ++i) // 逐卡校验
            {
                const CardDef& def = cardDef(static_cast<CardId>(i)); // 卡定义
                if (def.maxLevel > 0 && levels[i] > def.maxLevel) { cappedOk = false; } // 越界：违例
            }
        }

        expectTrue("100 轮全程：有限卡层数从不越过上限", cappedOk);
        expectTrue("100 轮全程：欠账记账与消费配对", queueOk && accounted == consumed);
        expectInt("100 轮后欠账清空", queue.pending(), 0);

        // 池的极限：100 次选择足以把 7 张有限卡全部拉满（总 28 层）——
        // 此后池只剩利刃/体魄两张无限卡（count == 2）
        const CardDrawResult late = drawUpgradeCards(levels, &CardSeedRandom::nextFrom, &rng);
        expectInt("长期模拟后：池为两张无限卡（count = 2）", late.count, 2);
        const bool lateOnlyUnlimited = // 两张均为无限卡且不重复
            ((late.cards[0] == CardId::Blade || late.cards[0] == CardId::Vitality) &&
             (late.cards[1] == CardId::Blade || late.cards[1] == CardId::Vitality) &&
             late.cards[0] != late.cards[1]);
        expectTrue("长期模拟后：抽到的恒为利刃/体魄", lateOnlyUnlimited);

        // 验证「7 张有限卡全满」的推断（28 层恰好被填满——100 轮足够）
        bool allFiniteCapped = true; // 有限卡是否全部满层
        for (int i = 0; i < kCardCount; ++i) // 逐卡检查
        {
            const CardDef& def = cardDef(static_cast<CardId>(i)); // 卡定义
            if (def.maxLevel > 0 && levels[i] < def.maxLevel) { allFiniteCapped = false; } // 未满：推断失效
        }
        expectTrue("长期模拟后：7 张有限卡全部满层", allFiniteCapped);

        // 「R 重开」模拟：层数清零 -> 全部卡重新入池（下次抽取含有限卡）
        for (int& v : levels) { v = 0; } // 清零（restart 的纯逻辑等价）
        bool anyFiniteSeen = false;      // 抽取结果是否重新出现有限卡
        for (int iter = 0; iter < 10; ++iter) // 重开后抽样
        {
            const CardDrawResult draw = drawUpgradeCards(levels, &CardSeedRandom::nextFrom, &rng);
            for (int k = 0; k < draw.count; ++k) // 逐张检查
            {
                const CardDef& def = cardDef(draw.cards[k]); // 卡定义
                if (def.maxLevel > 0) { anyFiniteSeen = true; } // 有限卡回来了
            }
        }
        expectTrue("重开清零后：有限卡重新入池", anyFiniteSeen);
    }
    endCase(8, p0, f0);

    // ------------------------------------------------------------------
    // CASE 9 卡面板布局（批五A）：CardLayout 共享计算——渲染与命中同源
    // ------------------------------------------------------------------
    beginCase(9, "卡面板布局（居中/间距/命中/越界防御；渲染与命中同一份计算）");
    p0 = gPass; f0 = gFail;
    {
        // 1280x720（默认窗口）：三张卡总宽 3x300 + 2x30 = 960，
        // 居中后首卡左边界 = 640 - 480 = 160；卡片行上边界 = 720 x 0.30 = 216
        const SDL_FRect c3_0 = cardPanelRectAt(0, 3, 1280.0f, 720.0f); // 三张：卡 0
        const SDL_FRect c3_1 = cardPanelRectAt(1, 3, 1280.0f, 720.0f); // 三张：卡 1
        const SDL_FRect c3_2 = cardPanelRectAt(2, 3, 1280.0f, 720.0f); // 三张：卡 2
        expectNear("三张：卡 0 左边界（居中）", c3_0.x, 160.0f, 0.001f);
        expectNear("三张：卡 1 左边界（含间距 30）", c3_1.x, 490.0f, 0.001f);
        expectNear("三张：卡 2 左边界", c3_2.x, 820.0f, 0.001f);
        expectNear("三张：卡片行上边界（屏高 30%）", c3_0.y, 216.0f, 0.001f);
        expectNear("三张：卡宽", c3_0.w, 300.0f, 0.001f);
        expectNear("三张：卡高", c3_0.h, 300.0f, 0.001f);
        // 右边界对称性：末卡右缘 = 屏宽 - 首卡左边界（居中不偏）
        expectNear("三张：末卡右缘与首卡左边界对称", 1280.0f - (c3_2.x + c3_2.w), c3_0.x, 0.001f);

        // 两张：总宽 630 -> 首卡 325；一张：总宽 300 -> 首卡 490
        const SDL_FRect c2_0 = cardPanelRectAt(0, 2, 1280.0f, 720.0f); // 两张：卡 0
        expectNear("两张：卡 0 左边界（重新居中）", c2_0.x, 325.0f, 0.001f);
        const SDL_FRect c1_0 = cardPanelRectAt(0, 1, 1280.0f, 720.0f); // 一张：卡 0
        expectNear("一张：卡 0 左边界（单卡居中）", c1_0.x, 490.0f, 0.001f);

        // 越界防御：非法位序/空张数 -> 空矩形（w = h = 0；不崩、可判无效）
        const SDL_FRect invalidNeg = cardPanelRectAt(-1, 3, 1280.0f, 720.0f); // 负位序
        const SDL_FRect invalidOver = cardPanelRectAt(3, 3, 1280.0f, 720.0f); // 越上界
        const SDL_FRect invalidZero = cardPanelRectAt(0, 0, 1280.0f, 720.0f); // 空张数
        expectTrue("越界防御：负位序返回空矩形", invalidNeg.w == 0.0f && invalidNeg.h == 0.0f);
        expectTrue("越界防御：越上界返回空矩形", invalidOver.w == 0.0f && invalidOver.h == 0.0f);
        expectTrue("越界防御：空张数返回空矩形", invalidZero.w == 0.0f && invalidZero.h == 0.0f);

        // 命中测试：与渲染同一份布局——各卡中心必然命中对应位序
        expectInt("命中：卡 0 中心 -> 0", cardPanelHitTest(310.0f, 366.0f, 3, 1280.0f, 720.0f), 0);
        expectInt("命中：卡 1 中心 -> 1", cardPanelHitTest(640.0f, 366.0f, 3, 1280.0f, 720.0f), 1);
        expectInt("命中：卡 2 中心 -> 2", cardPanelHitTest(970.0f, 366.0f, 3, 1280.0f, 720.0f), 2);
        // 贴边内侧 0.5px 命中（左上角附近）；右缘外 1px 属间隙不算
        expectInt("命中：卡 0 左上角内侧 0.5px", cardPanelHitTest(160.5f, 216.5f, 3, 1280.0f, 720.0f), 0);
        expectInt("命中：卡 0 右缘外 1px（间隙）-> -1", cardPanelHitTest(461.0f, 366.0f, 3, 1280.0f, 720.0f), -1);
        expectInt("命中：卡 1/卡 2 间隙中点 -> -1", cardPanelHitTest(805.0f, 366.0f, 3, 1280.0f, 720.0f), -1);
        expectInt("命中：面板外（屏左上）-> -1", cardPanelHitTest(10.0f, 10.0f, 3, 1280.0f, 720.0f), -1);
        expectInt("命中：卡片行上方（标题区）-> -1", cardPanelHitTest(640.0f, 100.0f, 3, 1280.0f, 720.0f), -1);
        // 两张面板：卡 1 中心（655+150=805, 366）命中 1
        expectInt("命中：两张面板的卡 1 中心 -> 1", cardPanelHitTest(805.0f, 366.0f, 2, 1280.0f, 720.0f), 1);
    }
    endCase(9, p0, f0);

    // ------------------------------------------------------------------
    const int total = gPass + gFail;
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 条断言 | 通过 %d | 失败 %d\n", total, gPass, gFail);
    std::printf(" 结论: %s\n", (gFail == 0)
        ? "全部通过 —— 卡池/抽取/叠加数学/欠账队列行为符合预期。"
        : "存在失败用例，请检查上方 [FAIL] 行。");
    std::printf(" main 返回值（失败用例数）: %d\n", gFail);
    std::printf("==============================================================\n");

    return gFail; // 失败数作为退出码：全部通过时为 0
}
