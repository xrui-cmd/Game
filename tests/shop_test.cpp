// ============================================================================
//  shop_test.cpp — 商店（Shop）单元测试
//
//  被测对象 : Src/inc/game/Shop.h（价格公式与可购判定）+
//             Src/inc/game/ShopLayout.h（九宫格面板布局，批五B；批六甲版式调整）+
//             Src/inc/game/ShopDesc.h（效果描述动态对比，批六甲）
//  覆盖场景 : ① 基础价表（9 项 / 强度定价 4~10 / 越界防御回退第一项）
//             ② 价格公式（价格 = 基础价 x(层数 + 1)：逐层核对、无限卡高层、
//                负层按 0、有限卡封顶——总表打印供报告参考）
//             ③ 可购判定（恰好等于价格可买 / 差 1 不可买 / 满层不可买 /
//                无限卡恒可买 / 负金币防御）
//             ④ 商店面板布局（3x3 网格：居中/行优先/命中/间隙与越界防御——
//                渲染与鼠标命中同一份计算，防坐标漂移）
//             ⑤ 边界契约（金币恰好等于价格、差 1 灰显的两条规格边界）
//             ⑥ 效果描述动态对比（批六甲：按当前层数实算「当前 -> 购买后」、
//                逐项文案格式、满层封顶、负层防御、自定义基准、越界 id）
//
//  依赖 : Shop.h / ShopDesc.h 为纯逻辑头文件；ShopLayout.h 只依赖
//  SDL_rect.h 的类型与内联点测（不链接 SDL3 运行时）。任何机器上都能
//  确定性运行（构建接线见 tests/CMakeLists.txt 的 shop_test 目标）
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/Shop.h"       // 被测类型/函数：shopBasePrice / shopPrice / shopCanBuy
#include "game/ShopDesc.h"   // 被测函数：shopCompareDesc（批六甲·效果描述动态对比）
#include "game/ShopLayout.h" // 被测函数：shopPanelRectAt / shopPanelHitTest（批五B 布局；批六甲版式调整）

#include <cmath>    // std::fabs：浮点容差比较
#include <cstdio>   // std::printf / std::snprintf
#include <cstdlib>  // std::system（Windows 控制台切 UTF-8）
#include <cstring>  // std::strcmp：文案逐字核对（批六甲·ShopDesc）

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

// 断言：字符串相等（批六甲·ShopDesc 文案逐字核对）
void expectStr(const char* label, const char* got, const char* expect)
{
    // 逐字节比较（文案为固定格式，无浮点误差）
    const bool ok = (std::strcmp(got, expect) == 0);
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名 + 期望与实际
    std::printf("  [%s] %s | 期望=\"%s\" 实际=\"%s\"\n", ok ? "PASS" : "FAIL", label, expect, got);
}

// 断言：浮点近似相等（容差比较——布局计算的小数舍入不影响判定）
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

} // namespace

int main()
{
#if defined(_WIN32)
    // 控制台代码页切到 UTF-8：中文输出不乱码（失败也不影响测试）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" Shop 商店单元测试 —— Src/inc/game/Shop.h + ShopLayout.h\n");
    std::printf(" 定价: 价格 = 基础价 x(层数 + 1)；基础价 4~10（越强越贵）\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 基础价表：9 项、强度定价 4~10、越界防御
    // ------------------------------------------------------------------
    beginCase(1, "基础价表（9 项 / 4~10 范围 / 越界防御）");
    int p0 = gPass, f0 = gFail;
    {
        // 表项数与卡池一致
        expectInt("kCardCount = 9（商店项数同源）", kCardCount, 9);

        // 逐项基础价断言（强度定价：越强越贵——改价必然改这里）
        expectInt("利刃基础价 = 8", shopBasePrice(CardId::Blade), 8);
        expectInt("疾风基础价 = 9", shopBasePrice(CardId::Swift), 9);
        expectInt("长臂基础价 = 6", shopBasePrice(CardId::Reach), 6);
        expectInt("横扫基础价 = 5", shopBasePrice(CardId::Sweep), 5);
        expectInt("疾行基础价 = 5", shopBasePrice(CardId::Dash), 5);
        expectInt("体魄基础价 = 7", shopBasePrice(CardId::Vitality), 7);
        expectInt("重击基础价 = 4", shopBasePrice(CardId::Heavy), 4);
        expectInt("慧眼基础价 = 7", shopBasePrice(CardId::Insight), 7);
        expectInt("聚宝基础价 = 4", shopBasePrice(CardId::Treasure), 4);

        // 范围检查：全部在规格批准的 4~10 区间内
        bool inRange = true; // 全部在 [4,10]？
        for (int i = 0; i < kCardCount; ++i) // 逐项扫描
        {
            const int price = kShopBasePrices[i]; // 本项基础价
            if (price < 4 || price > 10) { inRange = false; } // 越界即违例
        }
        expectTrue("基础价全部落在 4~10（规格区间）", inRange);

        // 越界防御：异常 id 回退第一项（不崩、结果可预测）
        expectInt("shopBasePrice(越界 id) = 第一项（8）",
                  shopBasePrice(static_cast<CardId>(99)), kShopBasePrices[0]);
        expectInt("shopBasePrice(负 id) = 第一项（8）",
                  shopBasePrice(static_cast<CardId>(-3)), kShopBasePrices[0]);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 价格公式：价格 = 基础价 x(层数 + 1)（含封顶与负层防御）
    // ------------------------------------------------------------------
    beginCase(2, "价格公式（逐层核对 / 封顶 / 负层按 0；总表打印）");
    p0 = gPass; f0 = gFail;
    {
        // 逐层核对：第 1 层收基础价、第 2 层翻倍、依此类推
        expectInt("利刃 lv0 价格 = 8（第 1 层）", shopPrice(CardId::Blade, 0), 8);
        expectInt("利刃 lv1 价格 = 16（第 2 层）", shopPrice(CardId::Blade, 1), 16);
        expectInt("利刃 lv2 价格 = 24", shopPrice(CardId::Blade, 2), 24);

        // 有限卡逐层价格（与上限对应的最后一层）
        expectInt("疾风 lv4 价格 = 45（第 5 层）", shopPrice(CardId::Swift, 4), 45);
        expectInt("长臂 lv3 价格 = 24（第 4 层）", shopPrice(CardId::Reach, 3), 24);
        expectInt("横扫 lv2 价格 = 15（第 3 层）", shopPrice(CardId::Sweep, 2), 15);
        expectInt("疾行 lv4 价格 = 25（第 5 层）", shopPrice(CardId::Dash, 4), 25);
        expectInt("重击 lv2 价格 = 12（第 3 层）", shopPrice(CardId::Heavy, 2), 12);
        expectInt("慧眼 lv4 价格 = 35（第 5 层）", shopPrice(CardId::Insight, 4), 35);
        expectInt("聚宝 lv2 价格 = 12（第 3 层）", shopPrice(CardId::Treasure, 2), 12);

        // 无限卡（利刃/体魄）：高层价格线性增长（不封顶）
        expectInt("利刃 lv10 价格 = 88", shopPrice(CardId::Blade, 10), 88);
        expectInt("体魄 lv99 价格 = 700（无限不封顶）", shopPrice(CardId::Vitality, 99), 700);

        // 负层防御：按 0 层计价（与 clampCardLevel 同口径）
        expectInt("利刃 lv-5 价格 = 8（负层按 0）", shopPrice(CardId::Blade, -5), 8);

        // 有限卡超上限层数：先封顶再计价（99 层按上限 3 层计）
        expectInt("横扫 lv99 价格 = 20（封顶后按 lv3）", shopPrice(CardId::Sweep, 99), 20);

        // 价格总表打印（报告参考：1~上限层逐层价格；无限卡到 5 层）
        std::printf("        [价格总表] 基础价x(层数+1)，第 1~5 层：\n");
        for (int i = 0; i < kCardCount; ++i) // 逐项打印
        {
            const CardId id = static_cast<CardId>(i); // 本项 id
            const CardDef& def = cardDef(id);         // 本项定义（名称/上限）
            std::printf("          %s: ", def.name);  // 名称前缀

            // 打印层数上限：有限卡到上限为止、无限卡到 5 层（演示线性增长）
            const int lvLimit = (def.maxLevel > 0 && def.maxLevel < 5) ? def.maxLevel : 5; // 本项打印层数
            for (int lv = 0; lv < lvLimit; ++lv)      // 逐层价格
            {
                std::printf("%d", shopPrice(id, lv)); // 本层价格
                if (lv + 1 < lvLimit) { std::printf(" / "); } // 分隔
            }
            if (def.maxLevel > 0) // 有限卡标注上限
            {
                std::printf("（上限 %d 层）", def.maxLevel);
            }
            else // 无限卡标注
            {
                std::printf(" / ...（无限）");
            }
            std::printf("\n"); // 换行
        }
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 可购判定：规格边界（恰好相等可买 / 差 1 不可买 / 满层 / 负金币）
    // ------------------------------------------------------------------
    beginCase(3, "可购判定（恰好相等 / 差 1 / 满层 / 无限卡 / 负金币防御）");
    p0 = gPass; f0 = gFail;
    {
        // 规格边界：金币恰好等于价格 -> 可以买（含边界）
        expectTrue("恰好等于价格：可买（8 >= 8）", shopCanBuy(CardId::Blade, 0, 8));
        // 规格边界：差 1 金币 -> 不可买（灰显）
        expectTrue("差 1 金币：不可买（7 < 8）", !shopCanBuy(CardId::Blade, 0, 7));

        // 多出的金币自然可买；第 2 层用第 2 层价格判定
        expectTrue("金币充足可买第 2 层（16 >= 16）", shopCanBuy(CardId::Blade, 1, 16));
        expectTrue("以第 1 层价格买第 2 层不可（15 < 16）", !shopCanBuy(CardId::Blade, 1, 15));

        // 满层：不再售卖（层级上限照旧）——金币再多也不可买
        expectTrue("疾风满 5 层不可买（金币 999）", !shopCanBuy(CardId::Swift, 5, 999));
        expectTrue("横扫满 3 层不可买", !shopCanBuy(CardId::Sweep, 3, 999));

        // 无限卡（利刃/体魄）：只要金币够就恒可买（价格增长是唯一约束）
        expectTrue("利刃 20 层可买（金币 168 = 8x21）", shopCanBuy(CardId::Blade, 20, 168));
        expectTrue("体魄 50 层可买（金币 357 = 7x51）", shopCanBuy(CardId::Vitality, 50, 357));

        // 负金币（数据异常）：恒不可买
        expectTrue("负金币不可买（防御）", !shopCanBuy(CardId::Blade, 0, -1));

        // 金币为 0：所有项都不可买（基础价最低 4）
        expectTrue("零金币不可买（4 > 0）", !shopCanBuy(CardId::Heavy, 0, 0));
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 商店面板布局：3x3 网格（居中/行优先/命中/越界防御）
    // ------------------------------------------------------------------
    beginCase(4, "商店布局（3x3 居中/行优先/命中/间隙与越界防御）");
    p0 = gPass; f0 = gFail;
    {
        // 项数常量与网格形状
        expectInt("kShopItemCount = 9", kShopItemCount, 9);
        expectInt("kShopColumns = 3", kShopColumns, 3);

        // 1280x720（默认窗口）：总宽 3x320 + 2x22 = 1004，居中后
        // 首卡左边界 = 640 - 502 = 138；上边界 = 720 x 0.16 = 115.2
        const SDL_FRect r0 = shopPanelRectAt(0, 1280.0f, 720.0f); // 第 1 行第 1 列
        const SDL_FRect r1 = shopPanelRectAt(1, 1280.0f, 720.0f); // 第 1 行第 2 列
        const SDL_FRect r4 = shopPanelRectAt(4, 1280.0f, 720.0f); // 第 2 行第 2 列（中心）
        const SDL_FRect r8 = shopPanelRectAt(8, 1280.0f, 720.0f); // 第 3 行第 3 列
        expectNear("项 0 左边界（居中起点）", r0.x, 138.0f, 0.001f);
        expectNear("项 1 左边界（含列距 342）", r1.x, 480.0f, 0.001f);
        expectNear("项 0 上边界（屏高 16%）", r0.y, 115.2f, 0.001f);
        expectNear("项 4 上边界（第 2 行 = +172）", r4.y, 287.2f, 0.001f);
        expectNear("项 8 上边界（第 3 行 = +344）", r8.y, 459.2f, 0.001f);
        expectNear("卡宽 = 320", r0.w, 320.0f, 0.001f);
        expectNear("卡高 = 150（批六甲 130 -> 150）", r0.h, 150.0f, 0.001f);

        // 居中对称性：末列右缘 = 屏宽 - 首列左边界
        expectNear("末列右缘与首列左边界对称", 1280.0f - (r8.x + r8.w), r0.x, 0.001f);

        // 越界防御：负下标/越上界 -> 空矩形（w = h = 0；不崩、可判无效）
        const SDL_FRect invalidNeg = shopPanelRectAt(-1, 1280.0f, 720.0f); // 负下标
        const SDL_FRect invalidOver = shopPanelRectAt(9, 1280.0f, 720.0f); // 越上界
        expectTrue("越界防御：负下标返回空矩形", invalidNeg.w == 0.0f && invalidNeg.h == 0.0f);
        expectTrue("越界防御：越上界返回空矩形", invalidOver.w == 0.0f && invalidOver.h == 0.0f);

        // 命中测试：与渲染同一份布局——各项中心必然命中对应下标
        expectInt("命中：项 0 中心 -> 0", shopPanelHitTest(298.0f, 190.2f, 1280.0f, 720.0f), 0);
        expectInt("命中：项 4 中心 -> 4", shopPanelHitTest(640.0f, 362.2f, 1280.0f, 720.0f), 4);
        expectInt("命中：项 8 中心 -> 8", shopPanelHitTest(982.0f, 534.2f, 1280.0f, 720.0f), 8);

        // 贴边内侧 0.5px 命中（左上角附近）；右缘外 1px 属间隙不算
        expectInt("命中：项 0 左上角内侧 0.5px", shopPanelHitTest(138.5f, 115.7f, 1280.0f, 720.0f), 0);
        expectInt("命中：项 0 右缘外 1px（间隙）-> -1",
                  shopPanelHitTest(459.0f, 190.2f, 1280.0f, 720.0f), -1);
        // 列间隙中点（卡 0 右缘 458 与卡 1 左缘 480 之间 -> 469）
        expectInt("命中：列间隙中点 -> -1", shopPanelHitTest(469.0f, 190.2f, 1280.0f, 720.0f), -1);
        // 行间隙中点（第 1 行下缘 265.2 与第 2 行上缘 287.2 之间 -> 276.2）
        expectInt("命中：行间隙中点 -> -1", shopPanelHitTest(298.0f, 276.2f, 1280.0f, 720.0f), -1);
        // 面板外：屏左上（标题区之上）与右侧空白
        expectInt("命中：面板外（屏左上）-> -1", shopPanelHitTest(10.0f, 10.0f, 1280.0f, 720.0f), -1);
        expectInt("命中：面板外（右侧空白）-> -1",
                  shopPanelHitTest(1240.0f, 360.0f, 1280.0f, 720.0f), -1);
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 效果描述动态对比（批六甲·ShopDesc：按当前层数实算 + 文案格式）
    // ------------------------------------------------------------------
    beginCase(5, "效果描述（静态效果 + 动态数值对比：当前 -> 购买后）");
    p0 = gPass; f0 = gFail;
    {
        const ShopPreviewBases b{}; // 默认基准（与内置默认值一致：25/0.4/96/120/300/100/30/140）
        char buf[96];               // 文案缓冲（最长一项也远小于 96 字节）

        // 利刃（伤害点）：0 层与 3 层的对比
        shopCompareDesc(CardId::Blade, 0, b, buf, sizeof(buf));
        expectStr("利刃 lv0：当前 25 -> 购买后 30", buf, "当前 25 → 购买后 30");
        shopCompareDesc(CardId::Blade, 3, b, buf, sizeof(buf));
        expectStr("利刃 lv3：当前 40 -> 购买后 45", buf, "当前 40 → 购买后 45");

        // 疾风（冷却秒，两位小数）：0 层 0.40 -> 0.35
        shopCompareDesc(CardId::Swift, 0, b, buf, sizeof(buf));
        expectStr("疾风 lv0：0.40s -> 0.35s", buf, "当前 0.40s → 购买后 0.35s");
        // 4 层 -> 5 层的对比（未满：0.24 -> 0.21）
        shopCompareDesc(CardId::Swift, 4, b, buf, sizeof(buf));
        expectStr("疾风 lv4：0.24s -> 0.21s（第 5 层）", buf, "当前 0.24s → 购买后 0.21s");
        // 满层（5 层）封顶：next 被公式夹回原值（面板侧改显示「已达上限」）
        shopCompareDesc(CardId::Swift, 5, b, buf, sizeof(buf));
        expectStr("疾风满层 lv5：0.21s -> 0.21s（封顶后不变）", buf, "当前 0.21s → 购买后 0.21s");

        // 长臂（像素整数）
        shopCompareDesc(CardId::Reach, 0, b, buf, sizeof(buf));
        expectStr("长臂 lv0：96 -> 111", buf, "当前 96 → 购买后 111");

        // 横扫（度整数）
        shopCompareDesc(CardId::Sweep, 0, b, buf, sizeof(buf));
        expectStr("横扫 lv0：120 度 -> 135 度", buf, "当前 120 度 → 购买后 135 度");

        // 疾行（移速像素/秒整数）
        shopCompareDesc(CardId::Dash, 0, b, buf, sizeof(buf));
        expectStr("疾行 lv0：300 -> 320", buf, "当前 300 → 购买后 320");

        // 体魄（生命上限点）：2 层已叠的实算
        shopCompareDesc(CardId::Vitality, 2, b, buf, sizeof(buf));
        expectStr("体魄 lv2：140 -> 160", buf, "当前 140 → 购买后 160");

        // 重击（击退像素整数，百分比加法）
        shopCompareDesc(CardId::Heavy, 0, b, buf, sizeof(buf));
        expectStr("重击 lv0：30 -> 45", buf, "当前 30 → 购买后 45");

        // 慧眼（经验乘数一位小数）
        shopCompareDesc(CardId::Insight, 0, b, buf, sizeof(buf));
        expectStr("慧眼 lv0：x1.0 -> x1.2", buf, "当前 ×1.0 → 购买后 ×1.2");

        // 聚宝（磁吸像素整数）：1 层已叠的实算
        shopCompareDesc(CardId::Treasure, 1, b, buf, sizeof(buf));
        expectStr("聚宝 lv1：200 -> 260", buf, "当前 200 → 购买后 260");

        // 负层防御：按 0 层实算（与价格公式同口径）
        shopCompareDesc(CardId::Blade, -5, b, buf, sizeof(buf));
        expectStr("利刃 lv-5：按 0 层实算（25 -> 30）", buf, "当前 25 → 购买后 30");

        // 自定义基准（模拟配置改过的局）：文案随基准联动
        ShopPreviewBases custom = b;   // 拷贝默认
        custom.attackDamage = 40;      // 基准伤害 40
        shopCompareDesc(CardId::Blade, 1, custom, buf, sizeof(buf));
        expectStr("自定义基准（40）：45 -> 50", buf, "当前 45 → 购买后 50");

        // 越界 id（防御）：写作「—」——不崩、面板仍完整
        shopCompareDesc(static_cast<CardId>(99), 0, b, buf, sizeof(buf));
        expectStr("越界 id：防御占位「—」", buf, "—");
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // 汇总
    // ------------------------------------------------------------------
    const int total = gPass + gFail;
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 条断言 | 通过 %d | 失败 %d\n", total, gPass, gFail);
    std::printf(" 结论: %s\n", (gFail == 0)
        ? "全部通过 —— 价格公式/可购判定/面板布局行为符合预期。"
        : "存在失败用例，请检查上方 [FAIL] 行。");
    std::printf(" main 返回值（失败用例数）: %d\n", gFail);
    std::printf("==============================================================\n");

    return gFail; // 失败数作为退出码：全部通过时为 0
}
