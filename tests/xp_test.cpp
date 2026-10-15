// ============================================================================
//  xp_test.cpp — 升级经验曲线（LevelCurve）单元测试
//
//  被测对象 : Src/inc/game/LevelCurve.h（game 层纯逻辑头文件，header-only）
//  覆盖场景 : ① 曲线值断言（默认参数 30/1.35 的逐级需求与四舍五入）
//             ② 自定义参数（整数倍率、半值舍入）
//             ③ 连升溢出（一枚经验溢出一级 / 连升两级 / 一枚连升多级）
//             ④ 0/1 级与负值边界（不崩、按第 1 级处理、负经验忽略）
//             ⑤ 极端值防御（天文等级封顶、max int 基数、NaN/小于 1 的倍率）
//
//  与其余测试的差异 : 本测试测的是「无任何依赖」的纯逻辑头文件——
//  既不链接 SDL3，也不需要窗口/音频/文件，任何机器上都能确定性运行
//  （构建接线见 tests/CMakeLists.txt 的 xp_test 目标）
//
//  注意 : 测试会故意构造「配置写坏」的曲线参数（NaN 倍率等），
//  这些路径在 LevelCurve 内部被夹取消化，不会打印任何警告，也不会崩
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/LevelCurve.h" // 被测类型 LevelCurve / LevelUpResult

#include <cstdio>   // std::printf
#include <cstdlib>  // std::system（Windows 控制台切 UTF-8）
#include <limits>   // std::numeric_limits：构造 NaN / max int 等极端参数

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
    std::printf(" LevelCurve 升级经验曲线单元测试 —— Src/inc/game/LevelCurve.h\n");
    std::printf(" 曲线定义: needed(level) = round(base x growth^(level-1))\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 默认参数曲线值：base=30、growth=1.35（与 config 默认一致）
    // ------------------------------------------------------------------
    beginCase(1, "默认参数（30/1.35）-> 逐级需求与四舍五入");
    int p0 = gPass, f0 = gFail;
    {
        LevelCurve curve; // 默认构造：base=30、growth=1.35（与 config 缺键回退一致）
        // 前 6 级需求逐一断言（规格给出的配置值下的期望序列）
        expectInt("needed(1) = 30", curve.needed(1), 30);
        // 30 x 1.35 = 40.5：四舍五入应得 41（验证「半值向上」的取整规则）
        expectInt("needed(2) = round(40.5) = 41", curve.needed(2), 41);
        expectInt("needed(3) = round(54.675) = 55", curve.needed(3), 55);
        expectInt("needed(4) = round(73.81125) = 74", curve.needed(4), 74);
        expectInt("needed(5) = round(99.645..) = 100", curve.needed(5), 100);
        expectInt("needed(6) = round(134.52..) = 135", curve.needed(6), 135);
        // 更深两级（42.5 万这类累乘误差若存在会在此暴露）
        expectInt("needed(7) = round(181.60..) = 182", curve.needed(7), 182);
        expectInt("needed(8) = round(245.16..) = 245", curve.needed(8), 245);
        // 单调性抽查：需求必须逐级严格不减（曲线对玩家永远不倒退）
        bool monotonic = true; // 单调性是否成立（发现一次违例即置 false）
        for (int lv = 1; lv < 30; ++lv)
        {
            if (curve.needed(lv + 1) < curve.needed(lv))
            {
                monotonic = false; // 出现「下一级更便宜」：违例
            }
        }
        expectTrue("needed 在 1~30 级内单调不减", monotonic);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 自定义参数：整数倍率与半值舍入
    // ------------------------------------------------------------------
    beginCase(2, "自定义参数（5/1.5、10/2）-> 舍入规则与等比序列");
    p0 = gPass; f0 = gFail;
    {
        LevelCurve half;   // base=5、growth=1.5：逐步出现 .5 / .25 这类半值
        half.base = 5;     // 第 1 级需求改为 5（走配置注入同款赋值路径）
        half.growth = 1.5f; // 倍率 1.5：5 -> 7.5 -> 11.25 -> ...
        expectInt("base=5,growth=1.5: needed(1) = 5", half.needed(1), 5);
        // 7.5 是「恰好半个单位」的边界：四舍五入（远离零）应得 8
        expectInt("base=5,growth=1.5: needed(2) = round(7.5) = 8", half.needed(2), 8);
        expectInt("base=5,growth=1.5: needed(3) = round(11.25) = 11", half.needed(3), 11);

        LevelCurve exact;    // base=10、growth=2：整数等比序列，无舍入歧义
        exact.base = 10;     // 第 1 级需求 10
        exact.growth = 2.0f; // 每级翻倍：10 / 20 / 40 / 80
        expectInt("base=10,growth=2: needed(4) = 80", exact.needed(4), 80);
        expectInt("base=10,growth=2: needed(5) = 160", exact.needed(5), 160);
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 连升溢出：一枚经验可能溢出一级、连升两级、连升多级
    // ------------------------------------------------------------------
    beginCase(3, "连升结算 -> 溢出扣减与余数进位");
    p0 = gPass; f0 = gFail;
    {
        LevelCurve curve; // 默认 30/1.35 曲线

        // 恰好踩线：30 点经验从 1 级升到 2 级，余数 0
        LevelUpResult just = curve.apply(1, 0, 30);
        expectInt("恰好 30 点: 升 1 级", just.levelsGained, 1);
        expectInt("恰好 30 点: 等级 2", just.level, 2);
        expectInt("恰好 30 点: 余数 0", just.xp, 0);

        // 差 1 点不升：29 点不足第 1 级需求
        LevelUpResult almost = curve.apply(1, 0, 29);
        expectInt("29 点: 不升级", almost.levelsGained, 0);
        expectInt("29 点: 仍是 1 级", almost.level, 1);
        expectInt("29 点: 进度 29", almost.xp, 29);

        // 「其一枚宝石连升两级」的规格场景：1 级已有 65 点，再拾取 10 点 ->
        // 75 -> 扣 30 余 45（升 2 级）-> 扣 41 余 4（升 3 级）-> 4 < 55 停
        LevelUpResult doubleUp = curve.apply(1, 65, 10);
        expectInt("65+10=75: 连升 2 级", doubleUp.levelsGained, 2);
        expectInt("65+10=75: 等级 3", doubleUp.level, 3);
        expectInt("65+10=75: 余数 4", doubleUp.xp, 4);

        // 大额一次给足：1000 点 -> 累计需求 30+41+55+74+100+135+182+245=862
        // 升到 9 级，余 1000-862=138（< 331，停在 9 级）
        LevelUpResult overflow = curve.apply(1, 0, 1000);
        expectInt("1000 点: 连升 8 级", overflow.levelsGained, 8);
        expectInt("1000 点: 等级 9", overflow.level, 9);
        expectInt("1000 点: 余数 138", overflow.xp, 138);

        // 结算不变量：任何结算后的进度都必须小于当前级需求（不会再溢出一级）
        expectTrue("结算后 xp < needed(level)（1000 点用例）",
                   overflow.xp < curve.needed(overflow.level));
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 0/1 级与负值边界：不崩、语义明确（防御性归一）
    // ------------------------------------------------------------------
    beginCase(4, "0/1 级与负值边界 -> 归一处理不崩");
    p0 = gPass; f0 = gFail;
    {
        LevelCurve curve; // 默认曲线

        // needed 对非正等级：按第 1 级处理（有明确返回，而不是崩溃/垃圾值）
        expectInt("needed(0) 按第 1 级 = 30", curve.needed(0), 30);
        expectInt("needed(-100) 按第 1 级 = 30", curve.needed(-100), 30);

        // apply 全零：等级归一到 1、进度 0、没升级
        LevelUpResult zero = curve.apply(0, 0, 0);
        expectInt("apply(0,0,0): 等级归一到 1", zero.level, 1);
        expectInt("apply(0,0,0): 进度 0", zero.xp, 0);
        expectInt("apply(0,0,0): 未升级", zero.levelsGained, 0);

        // 负经验：忽略（按 0 处理），不倒扣等级、不崩
        LevelUpResult negGain = curve.apply(0, 0, -50);
        expectInt("apply(0,0,-50): 未升级", negGain.levelsGained, 0);
        expectInt("apply(0,0,-50): 等级 1", negGain.level, 1);

        // 负进度：按 0 处理，不产生「负经验池」
        LevelUpResult negXp = curve.apply(1, -20, 0);
        expectInt("apply(1,-20,0): 进度归零", negXp.xp, 0);

        // 负等级 + 负进度 + 负增量：三重非法输入仍给出合法结果
        LevelUpResult allNeg = curve.apply(-3, -5, -9);
        expectInt("apply(-3,-5,-9): 等级 1", allNeg.level, 1);
        expectInt("apply(-3,-5,-9): 进度 0", allNeg.xp, 0);

        // 正常等级、零增量：状态原样返回
        LevelUpResult keep = curve.apply(5, 3, 0);
        expectInt("apply(5,3,0): 等级不变", keep.level, 5);
        expectInt("apply(5,3,0): 进度不变", keep.xp, 3);
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 极端值防御：天文等级封顶、max int 基数、坏倍率（NaN/<1）
    // ------------------------------------------------------------------
    beginCase(5, "极端参数 -> 封顶与夹取，不溢出、不崩溃、循环可终止");
    p0 = gPass; f0 = gFail;
    {
        LevelCurve curve; // 默认曲线

        // 天文等级：1.35^999999 早已溢出 double -> 必须返回封顶值而不是 UB
        expectInt("needed(1000000) 封顶 1e9", curve.needed(1000000), 1000000000);

        // max int 基数 + 天文倍率：double 迅速超过封顶，同样走封顶分支
        LevelCurve huge; // 自定义极端参数
        huge.base = std::numeric_limits<int>::max(); // 基数取 int 最大值
        huge.growth = 1e30f; // 倍率极端大
        expectInt("base=int_max,growth=1e30: needed(5) 封顶", huge.needed(5), 1000000000);

        // 倍率小于 1（配置写错）：按 1 处理 -> 需求恒等于基数
        LevelCurve flat; // 倍率归一的防御路径
        flat.growth = 0.5f; // 小于 1 的倍率
        expectInt("growth=0.5 归一后 needed(2) = base", flat.needed(2), flat.base);

        // NaN 倍率（如配置解析异常产生）：比较恒 false 被夹住，不崩
        LevelCurve nanCurve; // NaN 倍率的防御路径
        nanCurve.growth = std::numeric_limits<float>::quiet_NaN(); // 构造 NaN
        expectInt("growth=NaN 归一后 needed(2) = base", nanCurve.needed(2), nanCurve.base);

        // 超大增量（int 上限）：long long 池计算，连升有限次后正常收敛
        LevelUpResult massive = curve.apply(1, 0, std::numeric_limits<int>::max());
        expectTrue("max int 增量: 正常升级（等级 > 1）", massive.level > 1);
        expectTrue("max int 增量: 结算后 xp < needed(level)",
                   massive.xp < curve.needed(massive.level));

        // 基数被写坏为 0：needed 按 1 处理——否则升级循环将永不扣减、死循环。
        // 需求序列从 1 起仍按 growth 增长：1/1/2/2/3/4...，10 点恰好连升 5 级
        // （扣 1+1+2+2+3=9，余 1 不足以再升 4）——循环正常收敛即通过标准
        LevelCurve zeroBase; // 基数 0 的防御路径
        zeroBase.base = 0;   // 非法基数
        expectInt("base=0 归一后 needed(1) = 1", zeroBase.needed(1), 1);
        LevelUpResult zeroBaseApply = zeroBase.apply(1, 0, 10);
        expectInt("base=0 时 10 点连升 5 级（需求 1/1/2/2/3）", zeroBaseApply.levelsGained, 5);
        expectInt("base=0 结算后进度 1", zeroBaseApply.xp, 1);
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    const int total = gPass + gFail;
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 条断言 | 通过 %d | 失败 %d\n", total, gPass, gFail);
    std::printf(" 结论: %s\n", (gFail == 0)
        ? "全部通过 —— 升级曲线行为符合预期。"
        : "存在失败用例，请检查上方 [FAIL] 行。");
    std::printf(" main 返回值（失败用例数）: %d\n", gFail);
    std::printf("==============================================================\n");

    return gFail; // 失败数作为退出码：全部通过时为 0
}
