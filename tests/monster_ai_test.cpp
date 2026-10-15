// ============================================================================
//  monster_ai_test.cpp — 怪物 AI 状态决策（MonsterAi）单元测试
//
//  被测对象 : Src/inc/game/MonsterAi.h（game 层纯逻辑头文件，header-only）
//  覆盖场景 : ① 状态转换条件与边界（仇恨恰好 320 / 栓绳恰好 500 /
//                到巢恰好 epsilon；仇恨优先于到巢；无玩家目标不仇恨）
//             ② 巡逻流程（索要游荡点 / 到达转停顿 / 停顿 1~2s 后索要 /
//                追赶超时放弃；配置夹取）
//             ③ 回巢回血纯函数（=0 关闭 / >0 按比例 / 溢出夹取 / 取整）
//             ④ Return 到巢全过程（enteredReturn 与 returnedHome 的一次性
//                事件标志；回巢目标点为巢心）
//             ⑤ Return 途中玩家重进仇恨 -> 立即重新 Chase
//             ⑥ 池复用 reset / clear（状态、游荡目标、停顿全量复位；
//                巢坐标重写；回血百分比保留）
//
//  与其余测试的差异 : 本测试测的是「无任何依赖」的纯逻辑头文件——
//  既不链接 SDL3，也不需要窗口/音频/文件，任何机器上都能确定性运行
//  （构建接线见 tests/CMakeLists.txt 的 monster_ai_test 目标）。
//  随机源经 setRandom 注入固定序列，巡逻节奏完全可复现
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/MonsterAi.h" // 被测类型 MonsterAi / MonsterAiState / Config / StepResult

#include <cmath>    // std::fabs：浮点近似断言
#include <cstdio>   // std::printf
#include <cstdlib>  // std::system（Windows 控制台切 UTF-8）

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
void expectNear(const char* label, float got, float expect, float eps = 0.001f)
{
    // 偏差不超过容差才算通过
    const bool ok = std::fabs(got - expect) <= eps;
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名 + 期望与实际（六位小数足够诊断）
    std::printf("  [%s] %s | 期望≈%.6f 实际=%.6f\n", ok ? "PASS" : "FAIL", label, expect, got);
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

// 断言：状态枚举相等（输出可读的状态名）
void expectState(const char* label, MonsterAiState got, MonsterAiState expect)
{
    // 枚举相等才算通过
    const bool ok = (got == expect);
    if (ok) { ++gPass; } else { ++gFail; }
    // 状态名查表：让 PASS/FAIL 行一眼能读懂
    const auto name = [](MonsterAiState s)
    {
        // 三态映射：Patrol/Chase/Return 的中文名（日志可读性）
        switch (s)
        {
        case MonsterAiState::Patrol: return "巡逻";
        case MonsterAiState::Chase:  return "追击";
        case MonsterAiState::Return: return "回巢";
        }
        return "未知"; // 理论不可达；防御编译器告警
    };
    // 输出一行结果：期望状态名与实际状态名
    std::printf("  [%s] %s | 期望=%s 实际=%s\n", ok ? "PASS" : "FAIL", label, name(expect), name(got));
}

// 随机源桩：按固定序列循环返回 [0,1) 值——巡逻取点/停顿时长的确定性来源
struct RandomStub
{
    const float* values = nullptr; // 固定序列（调用方持有）
    int count = 0;                 // 序列长度（至少 1）
    int index = 0;                 // 下一个取值下标（循环递增）
};

// 随机源适配：满足 MonsterAiRandomFn 签名（void* 上下文 -> RandomStub）
float stubRandom(void* context)
{
    RandomStub& stub = *static_cast<RandomStub*>(context); // 还原桩
    const float value = stub.values[stub.index % stub.count]; // 取当前值（循环）
    ++stub.index; // 推进到下一个（循环使用，永不越界）
    return value; // [0,1) 的确定性值
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

// 固定的测试步长（秒）：状态转换不依赖它；巡逻计时用它推进
constexpr float kDt = 0.1f;

} // namespace

int main()
{
#if defined(_WIN32)
    // 控制台代码页切到 UTF-8：中文输出不乱码（失败也不影响测试）
    std::system("chcp 65001 > nul 2>&1");
#endif

    // 固定随机序列：全部返回 0（停顿取 1.0s 下限、取点退回简并——测试只关心逻辑）
    const float randomValues[] = { 0.0f };
    RandomStub stub{ randomValues, 1, 0 }; // 循环返回 0.0 的随机桩

    std::printf("==============================================================\n");
    std::printf(" MonsterAi 怪物 AI 状态决策单元测试 —— Src/inc/game/MonsterAi.h\n");
    std::printf(" 状态机: Patrol(巡逻) -> Chase(仇恨) -> Return(栓绳回巢) -> Patrol\n");
    std::printf(" 边界: 仇恨<=320px / 栓绳>500px / 到巢<=8px / 仇恨优先于到巢\n");
    std::printf("==============================================================\n");

    // 公共配置：与游戏内默认一致（patrol 96 / aggro 320 / leash 500）
    MonsterAi::Config config; // 头文件默认值即规格默认值

    // ------------------------------------------------------------------
    // CASE 1 状态转换条件与边界
    // ------------------------------------------------------------------
    beginCase(1, "状态转换条件与边界（恰好等于半径的行为）");
    int p0 = gPass, f0 = gFail;
    {
        // 默认值抽查：规格三半径与到巢判据（防止头文件默认值被无意改动）
        expectNear("默认巡逻半径 96", config.patrolRadius, 96.0f);
        expectNear("默认仇恨半径 320", config.aggroRadius, 320.0f);
        expectNear("默认栓绳半径 500", config.leashRadius, 500.0f);
        expectNear("默认到巢判据 8", config.homeArriveEpsilon, 8.0f);

        MonsterAi ai;                    // 被测实例
        ai.setConfig(config);            // 注入默认参数
        ai.setRandom(&stubRandom, &stub); // 注入确定性随机源
        const float dt = kDt;            // 测试步长

        // ---- 1.1 仇恨进入：恰好 320 也追、320.5 不追 ----
        ai.reset(0.0f, 0.0f); // 巢在原点
        MonsterAi::StepResult r = ai.update(0.0f, 0.0f, true, 320.5f, 0.0f, dt);
        expectState("玩家 320.5px：保持巡逻", r.state, MonsterAiState::Patrol);
        expectTrue("320.5px：未进入仇恨", !r.enteredChase);
        r = ai.update(0.0f, 0.0f, true, 320.0f, 0.0f, dt);
        expectState("玩家恰好 320px：进入追击（<= 也算）", r.state, MonsterAiState::Chase);
        expectTrue("恰好 320px：enteredChase 置位", r.enteredChase);

        // ---- 1.2 无玩家目标：不仇恨（防御路径）----
        ai.reset(0.0f, 0.0f);
        r = ai.update(0.0f, 0.0f, false, 0.0f, 0.0f, dt);
        expectState("无玩家目标：保持巡逻", r.state, MonsterAiState::Patrol);

        // ---- 1.3 栓绳触发：恰好 500 仍追、500.5 回巢 ----
        ai.reset(0.0f, 0.0f);
        r = ai.update(0.0f, 0.0f, true, 100.0f, 0.0f, dt); // 先进入追击
        expectState("玩家 100px：进入追击", r.state, MonsterAiState::Chase);
        r = ai.update(500.0f, 0.0f, true, 100.0f, 0.0f, dt); // 怪物移到离巢恰好 500
        expectState("离巢恰好 500px：仍追击（> 才触发）", r.state, MonsterAiState::Chase);
        expectTrue("恰好 500px：未触发栓绳", !r.enteredReturn);
        r = ai.update(500.5f, 0.0f, true, 100.0f, 0.0f, dt); // 超过 500 一点
        expectState("离巢 500.5px：触发栓绳回巢", r.state, MonsterAiState::Return);
        expectTrue("500.5px：enteredReturn 置位", r.enteredReturn);

        // ---- 1.4 巡逻中离巢再远也不触发栓绳（栓绳只在追击中生效）----
        ai.reset(0.0f, 0.0f);
        r = ai.update(600.0f, 0.0f, true, 3000.0f, 0.0f, dt);
        expectState("巡逻离巢 600px：仍巡逻（栓绳只作用于追击）", r.state, MonsterAiState::Patrol);
        expectTrue("巡逻离巢：enteredReturn 未置位", !r.enteredReturn);

        // ---- 1.5 到巢判据：恰好 8 算到家、8.5 还不算 ----
        ai.reset(0.0f, 0.0f);
        r = ai.update(0.0f, 0.0f, true, 100.0f, 0.0f, dt);  // 进入追击
        r = ai.update(600.0f, 0.0f, true, 3000.0f, 0.0f, dt); // 拉远 -> 回巢
        expectState("离巢 600px：回巢", r.state, MonsterAiState::Return);
        r = ai.update(8.5f, 0.0f, true, 3000.0f, 0.0f, dt); // 离家 8.5：还不算到
        expectState("离巢 8.5px：继续回巢", r.state, MonsterAiState::Return);
        expectTrue("离巢 8.5px：returnedHome 未置位", !r.returnedHome);
        r = ai.update(8.0f, 0.0f, true, 3000.0f, 0.0f, dt); // 恰好 8：算到家
        expectState("离巢恰好 8px：回到巢穴转巡逻", r.state, MonsterAiState::Patrol);
        expectTrue("恰好 8px：returnedHome 置位", r.returnedHome);

        // ---- 1.6 仇恨优先于到巢：贴巢 + 玩家恰好 320 -> 先追玩家 ----
        ai.reset(0.0f, 0.0f);
        r = ai.update(0.0f, 0.0f, true, 100.0f, 0.0f, dt);  // 进入追击
        r = ai.update(600.0f, 0.0f, true, 3000.0f, 0.0f, dt); // 拉远 -> 回巢
        expectState("（铺垫）离巢 600px：回巢", r.state, MonsterAiState::Return);
        // 贴着巢（离巢 2px <= 8，本应到家）但玩家恰好 320px：仇恨优先
        r = ai.update(2.0f, 0.0f, true, 322.0f, 0.0f, dt);
        expectState("贴巢但玩家恰好 320px：重新追击（仇恨优先）", r.state, MonsterAiState::Chase);
        expectTrue("仇恨优先：enteredChase 置位", r.enteredChase);
        expectTrue("仇恨优先：returnedHome 未置位", !r.returnedHome);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 巡逻流程（索要游荡点 / 到达停顿 / 停顿后索要 / 超时放弃）
    // ------------------------------------------------------------------
    beginCase(2, "巡逻流程与配置夹取");
    p0 = gPass; f0 = gFail;
    {
        MonsterAi ai;                     // 被测实例
        ai.setConfig(config);             // 默认参数
        ai.setRandom(&stubRandom, &stub); // 随机恒为 0（停顿取 1.0s 下限）
        const float dt = kDt;             // 测试步长

        // ---- 2.1 生成后首步：索要游荡点（尚无目标）----
        ai.reset(0.0f, 0.0f);
        MonsterAi::StepResult r = ai.update(0.0f, 0.0f, true, 3000.0f, 0.0f, dt);
        expectTrue("生成后：向宿主索要游荡点", ai.wantsPatrolTarget());
        expectTrue("索要期间：无移动目标", !ai.hasGoal());

        // ---- 2.2 供水后走向游荡点 ----
        ai.setPatrolTarget(40.0f, 0.0f); // 宿主挑好的点（世界坐标）
        r = ai.update(0.0f, 0.0f, true, 3000.0f, 0.0f, dt);
        expectTrue("供水后：停止索要", !ai.wantsPatrolTarget());
        expectTrue("供水后：有移动目标", ai.hasGoal());
        expectNear("目标点 x = 40", ai.goalX(), 40.0f);
        expectNear("目标点 y = 0", ai.goalY(), 0.0f);

        // ---- 2.3 到达游荡点：转停顿（无目标）；随后 1s 倒计时后重新索要 ----
        // 第 1 步：距点 2.5 < 4 -> 到达，内部转入停顿（本步目标先于判定
        // 计算，仍为游荡点——下一步起无目标）
        r = ai.update(37.5f, 0.0f, true, 3000.0f, 0.0f, dt);
        // 第 2 步：进入停顿期：无移动目标、不索要（正在倒计时）
        r = ai.update(37.5f, 0.0f, true, 3000.0f, 0.0f, dt);
        expectTrue("到达后：无移动目标（进入停顿）", !ai.hasGoal());
        // 再走 9 步（合计 0.9s）：停顿未满 1s，仍未索要
        for (int i = 0; i < 9; ++i)
        {
            r = ai.update(37.5f, 0.0f, true, 3000.0f, 0.0f, dt);
        }
        expectTrue("停顿未满 1s：仍未索要", !ai.wantsPatrolTarget());
        // 第 10 步（合计 1.0s）：倒计时归零，下一步索要新游荡点
        r = ai.update(37.5f, 0.0f, true, 3000.0f, 0.0f, dt);
        r = ai.update(37.5f, 0.0f, true, 3000.0f, 0.0f, dt);
        expectTrue("停顿满 1s：重新索要游荡点", ai.wantsPatrolTarget());

        // ---- 2.4 追赶超时放弃：目标 3s 走不到 -> 强制转停顿 ----
        ai.reset(0.0f, 0.0f);
        ai.update(0.0f, 0.0f, true, 3000.0f, 0.0f, dt); // 索要
        ai.setPatrolTarget(90.0f, 0.0f);                 // 一个「走不到」的点
        // 原地不动 3.3s（33 步）：超过 patrolGiveUpSeconds 3.0 -> 放弃并停顿。
        // 步数取 33 而非 30：0.1f 反复累加存在浮点尾差（可能 2.9999997），
        // 多走几步让「超过 3s」的判定与浮点细节解耦
        for (int i = 0; i < 33; ++i)
        {
            r = ai.update(0.0f, 0.0f, true, 3000.0f, 0.0f, dt);
        }
        expectTrue("追赶超时：放弃目标转停顿（无移动目标）", !ai.hasGoal());
        expectTrue("追赶超时：不再索要（在停顿中）", !ai.wantsPatrolTarget());

        // ---- 2.5 配置夹取：负巡逻半径夹到 0 ----
        MonsterAi::Config clamped;    // 一份坏配置
        clamped.patrolRadius = -50.0f; // 负半径：非法
        MonsterAi ai2;                 // 独立实例（不污染 CASE 1/2 的其余断言）
        ai2.setConfig(clamped);        // 注入坏值（内部应夹取）
        expectNear("负巡逻半径被夹到 0", ai2.patrolRadius(), 0.0f);
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 回巢回血纯函数（=0 关闭 / >0 按比例 / 溢出夹取 / 取整）
    // ------------------------------------------------------------------
    beginCase(3, "回巢回血（healedHpAfterReturn）");
    p0 = gPass; f0 = gFail;
    {
        // =0 关闭：生命原样（无论伤残还是满血）
        expectInt("百分比 0：不改生命（伤残）", MonsterAi::healedHpAfterReturn(5, 20, 0.0f), 5);
        expectInt("百分比 0：不改生命（满血）", MonsterAi::healedHpAfterReturn(20, 20, 0.0f), 20);
        // 负百分比：等价关闭
        expectInt("百分比 -10：不改生命", MonsterAi::healedHpAfterReturn(5, 20, -10.0f), 5);
        // >0 按比例：20 x 50% = 10
        expectInt("50%：5 -> 15（20 上限）", MonsterAi::healedHpAfterReturn(5, 20, 50.0f), 15);
        expectInt("50%：0 -> 10（从空血回一半）", MonsterAi::healedHpAfterReturn(0, 20, 50.0f), 10);
        // 溢出夹取：18 + 10 = 28 -> 20
        expectInt("50%：18 -> 20（溢出夹到上限）", MonsterAi::healedHpAfterReturn(18, 20, 50.0f), 20);
        // 满血回血：仍是满血
        expectInt("50%：满血仍满血", MonsterAi::healedHpAfterReturn(20, 20, 50.0f), 20);
        // 超过 100%：夹到上限
        expectInt("150%：5 -> 20（夹到上限）", MonsterAi::healedHpAfterReturn(5, 20, 150.0f), 20);
        // 取整（四舍五入）：15 x 30% = 4.5 -> 5；7 x 50% = 3.5 -> 4
        expectInt("30% x15 上限：3 -> 8（4.5 取整 5）", MonsterAi::healedHpAfterReturn(3, 15, 30.0f), 8);
        expectInt("50% x7 上限：1 -> 5（3.5 取整 4）", MonsterAi::healedHpAfterReturn(1, 7, 50.0f), 5);
        // 上限为 0（异常）：不改生命（防御）
        expectInt("上限 0：不改生命", MonsterAi::healedHpAfterReturn(5, 0, 50.0f), 5);
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 Return 到巢全过程（事件一次性 + 目标点为巢心）
    // ------------------------------------------------------------------
    beginCase(4, "Return 到巢全过程与事件一次性");
    p0 = gPass; f0 = gFail;
    {
        MonsterAi ai;                     // 被测实例
        ai.setConfig(config);             // 默认参数
        ai.setRandom(&stubRandom, &stub); // 确定性随机
        const float dt = kDt;             // 测试步长

        // 铺垫：进入追击 -> 拉远触发回巢
        ai.reset(0.0f, 0.0f);
        MonsterAi::StepResult r = ai.update(0.0f, 0.0f, true, 100.0f, 0.0f, dt);
        expectTrue("铺垫：进入追击", r.enteredChase);
        r = ai.update(600.0f, 0.0f, true, 3000.0f, 0.0f, dt);
        expectState("拉远：回巢", r.state, MonsterAiState::Return);
        expectTrue("拉远：enteredReturn 置位", r.enteredReturn);

        // 回巢目标点 = 巢心（0, 0）
        expectTrue("回巢中：有移动目标（巢）", ai.hasGoal());
        expectNear("回巢目标 x = 巢心 0", ai.goalX(), 0.0f);
        expectNear("回巢目标 y = 巢心 0", ai.goalY(), 0.0f);

        // 逐步走回：enteredReturn 只在本步为 true（下步起转 false）
        r = ai.update(400.0f, 0.0f, true, 3000.0f, 0.0f, dt);
        expectTrue("回巢途中：enteredReturn 不再置位", !r.enteredReturn);
        r = ai.update(9.0f, 0.0f, true, 3000.0f, 0.0f, dt);
        expectState("离巢 9px：仍在回巢", r.state, MonsterAiState::Return);
        r = ai.update(8.0f, 0.0f, true, 3000.0f, 0.0f, dt);
        expectState("离巢 8px：到家转巡逻", r.state, MonsterAiState::Patrol);
        expectTrue("到家：returnedHome 置位", r.returnedHome);
        r = ai.update(8.0f, 0.0f, true, 3000.0f, 0.0f, dt);
        expectTrue("到家事件一次性：下一步不再置位", !r.returnedHome);
        // 到家后转回巡逻流程：索要新游荡点
        expectTrue("到家后：重新索要游荡点", ai.wantsPatrolTarget());
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 Return 途中重进仇恨（玩家恰好 320 -> 立即重新追击）
    // ------------------------------------------------------------------
    beginCase(5, "Return 途中重进仇恨");
    p0 = gPass; f0 = gFail;
    {
        MonsterAi ai;                     // 被测实例
        ai.setConfig(config);             // 默认参数
        ai.setRandom(&stubRandom, &stub); // 确定性随机
        const float dt = kDt;             // 测试步长

        // 铺垫：追击 -> 回巢
        ai.reset(0.0f, 0.0f);
        ai.update(0.0f, 0.0f, true, 100.0f, 0.0f, dt);        // 进入追击
        MonsterAi::StepResult r = ai.update(600.0f, 0.0f, true, 3000.0f, 0.0f, dt); // 回巢
        expectState("铺垫：回巢中", r.state, MonsterAiState::Return);

        // 玩家距离 320.5：仍回巢（未进仇恨）
        r = ai.update(300.0f, 0.0f, true, 620.5f, 0.0f, dt);
        expectState("玩家 320.5px：继续回巢", r.state, MonsterAiState::Return);
        // 玩家恰好 320：立即重新追击
        r = ai.update(300.0f, 0.0f, true, 620.0f, 0.0f, dt);
        expectState("玩家恰好 320px：重新追击", r.state, MonsterAiState::Chase);
        expectTrue("重进仇恨：enteredChase 置位", r.enteredChase);
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // CASE 6 池复用重置（reset / clear：状态与巢全量复位）
    // ------------------------------------------------------------------
    beginCase(6, "池复用后状态重置（reset/clear）");
    p0 = gPass; f0 = gFail;
    {
        MonsterAi ai;                     // 被测实例
        ai.setConfig(config);             // 默认参数
        ai.setRandom(&stubRandom, &stub); // 确定性随机
        ai.setReturnHealPercent(50.0f);   // 注入回血百分比（复位时不应丢失）
        const float dt = kDt;             // 测试步长

        // 把实例搅乱：追击 -> 回巢 -> 有游荡点/停顿等中间态
        ai.reset(0.0f, 0.0f);
        ai.update(0.0f, 0.0f, true, 100.0f, 0.0f, dt);        // 追击
        ai.update(600.0f, 0.0f, true, 3000.0f, 0.0f, dt);     // 回巢
        expectState("复用前：处于回巢状态", ai.state(), MonsterAiState::Return);

        // 池复用：reset 到新巢（50, 60）
        ai.reset(50.0f, 60.0f);
        expectState("reset 后：状态回巡逻", ai.state(), MonsterAiState::Patrol);
        expectNear("reset 后：巢 x 换新", ai.nestX(), 50.0f);
        expectNear("reset 后：巢 y 换新", ai.nestY(), 60.0f);
        expectTrue("reset 后：无移动目标（等供水）", !ai.hasGoal());
        expectTrue("reset 后：尚未索要（下一步才索要）", !ai.wantsPatrolTarget());
        expectNear("reset 后：回血百分比保留", ai.returnHealPercent(), 50.0f);

        // 复用后的第一步：以新巢为中心进入巡逻流程
        MonsterAi::StepResult r = ai.update(50.0f, 60.0f, true, 3000.0f, 0.0f, dt);
        expectTrue("复用后：按新巢索要游荡点", ai.wantsPatrolTarget());
        expectState("复用后：状态为巡逻", r.state, MonsterAiState::Patrol);

        // 再次搅乱（追击 + 游荡目标），验证 clear 也全量复位
        ai.update(50.0f, 60.0f, true, 100.0f, 60.0f, dt); // 距玩家 50 -> 追击
        ai.clear();                                        // 回收
        expectState("clear 后：状态回巡逻", ai.state(), MonsterAiState::Patrol);
        expectTrue("clear 后：无移动目标", !ai.hasGoal());
        expectTrue("clear 后：无索要标记", !ai.wantsPatrolTarget());
        expectNear("clear 后：巢坐标保留", ai.nestX(), 50.0f);
    }
    endCase(6, p0, f0);

    // ------------------------------------------------------------------
    // 汇总
    // ------------------------------------------------------------------
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 通过 / %d 失败\n", gPass, gFail);
    std::printf("==============================================================\n");

    return gFail; // 退出码 = 失败用例数（全过 = 0）
}
