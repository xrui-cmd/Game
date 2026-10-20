// ============================================================================
//  monster_recycle_test.cpp — 远处休眠怪物回收判据单元测试（批六补丁）
//
//  被测对象：Src/inc/game/MonsterRecycle.h（header-only 纯逻辑）——
//  shouldRecycleDormantMonster 的三条件（非激活区块 / 巡逻状态 / 距离阈值）
//  与默认阈值 kMonsterRecycleMinDistance。不依赖 SDL / 世界 / 场景，
//  任何环境都能确定性构建运行。
//
//  覆盖场景：
//    ① 默认阈值 = 1800px；
//    ② 三条件全满足才回收；
//    ③ 任一条件不满足都不回收（激活区块 / 仇恨追击 / 回巢）；
//    ④ 距离边界：恰好等于阈值不回收，严格大于才回收；
//    ⑤ 自定义阈值与 NaN 距离防御。
//
//  退出码：main 返回失败断言数（全部通过 = 0）。
// ============================================================================

#include "game/MonsterRecycle.h" // shouldRecycleDormantMonster / kMonsterRecycleMinDistance

#include <cmath>    // std::nanf：NaN 距离防御用例
#include <cstdio>   // std::printf
#include <cstdlib>  // std::system（Windows 控制台切 UTF-8）

namespace
{

int gPass = 0; // 通过的断言数
int gFail = 0; // 失败的断言数（作为 main 返回值）

// 布尔断言：条件成立才计入通过
void expectTrue(const char* label, bool value)
{
    const bool ok = value; // 断言条件
    if (ok) { ++gPass; } else { ++gFail; } // 计数
    // 输出一行 PASS/FAIL（便于定位失败项）
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
}

// 浮点近似断言：|got - expect| <= eps（阈值常量核对）
void expectNear(const char* label, float got, float expect, float eps = 0.001f)
{
    const bool ok = std::fabs(got - expect) <= eps; // 偏差在容差内
    if (ok) { ++gPass; } else { ++gFail; } // 计数
    // 输出期望/实际（诊断用）
    std::printf("  [%s] %s | 期望≈%.6f 实际=%.6f\n",
                ok ? "PASS" : "FAIL", label, expect, got);
}

// 用例开始分隔（与项目其余测试同款输出格式）
void beginCase(int idx, const char* title)
{
    std::printf("\n[CASE %d] %s\n", idx, title);
}

} // namespace

int main()
{
#if defined(_WIN32)
    // 控制台切 UTF-8：中文输出不乱码（失败也不影响测试结果）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" 远处休眠怪物回收判据单元测试（批六补丁）\n");
    std::printf(" 三条件：非激活区块 + 巡逻状态 + 距玩家 > 1800px\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 默认阈值与三条件全满足
    // ------------------------------------------------------------------
    beginCase(1, "默认阈值与三条件全满足");
    {
        expectNear("默认回收阈值 = 1800px", kMonsterRecycleMinDistance, 1800.0f);

        // 非激活 + 巡逻 + 距离超阈值：唯一应回收的组合
        expectTrue("非激活 + 巡逻 + 1801px -> 回收",
                   shouldRecycleDormantMonster(false, true, 1801.0f));
        expectTrue("远距离（5000px）同样回收",
                   shouldRecycleDormantMonster(false, true, 5000.0f));
    }

    // ------------------------------------------------------------------
    // CASE 2 任一条件不满足都不回收（防止误删眼前的怪/玩家拉的怪）
    // ------------------------------------------------------------------
    beginCase(2, "激活区块 / 仇恨 / 回巢均不回收");
    {
        // 条件一：激活区块（玩家视野附近）
        expectTrue("激活区块 + 巡逻 + 超远 -> 不回收",
                   !shouldRecycleDormantMonster(true, true, 5000.0f));

        // 条件二：AI 状态不是巡逻（Chase 仇恨追击 / Return 栓绳回巢）
        expectTrue("非激活 + 仇恨追击 + 超远 -> 不回收",
                   !shouldRecycleDormantMonster(false, false, 5000.0f));

        // 条件三：距离不够远（即使休眠且巡逻）
        expectTrue("非激活 + 巡逻 + 1000px -> 不回收",
                   !shouldRecycleDormantMonster(false, true, 1000.0f));
    }

    // ------------------------------------------------------------------
    // CASE 3 距离边界：严格大于阈值
    // ------------------------------------------------------------------
    beginCase(3, "距离阈值边界（严格大于）");
    {
        expectTrue("恰好 1800px -> 不回收",
                   !shouldRecycleDormantMonster(false, true, 1800.0f));
        expectTrue("1799.9px -> 不回收",
                   !shouldRecycleDormantMonster(false, true, 1799.9f));
        expectTrue("1800.1px -> 回收",
                   shouldRecycleDormantMonster(false, true, 1800.1f));
    }

    // ------------------------------------------------------------------
    // CASE 4 自定义阈值与 NaN 防御
    // ------------------------------------------------------------------
    beginCase(4, "自定义阈值与异常距离防御");
    {
        // 自定义阈值：单测/未来调参可注入更小的距离
        expectTrue("自定义阈值 500：600px 回收",
                   shouldRecycleDormantMonster(false, true, 600.0f, 500.0f));
        expectTrue("自定义阈值 500：500px 不回收",
                   !shouldRecycleDormantMonster(false, true, 500.0f, 500.0f));

        // NaN 距离：比较恒假 -> 一律不回收（异常值绝不误删怪物）
        const float nanDistance = std::nanf(""); // 构造 NaN
        expectTrue("NaN 距离 -> 不回收",
                   !shouldRecycleDormantMonster(false, true, nanDistance));
    }

    // ------------------------------------------------------------------
    // 汇总
    // ------------------------------------------------------------------
    std::printf("\n==============================================================\n");
    std::printf(" 断言结果：%d 通过 / %d 失败（共 %d）\n", gPass, gFail, gPass + gFail);
    std::printf("==============================================================\n");
    return gFail; // 退出码 = 失败断言数（0 = 全绿）
}
