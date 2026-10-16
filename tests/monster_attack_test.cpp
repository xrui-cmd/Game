// ============================================================================
//  monster_attack_test.cpp — 怪物攻击状态机（蓄力前扑）单元测试（批四）
//
//  被测对象 : Src/inc/game/Monster.h + Src/cpp/game/Monster.cpp 的攻击部分
//             （蓄力 0.3s 后倾 / 前扑 10px 前冲 / 接触范围判定 / 冷却门控 /
//              Hit-Miss 结算 / 池复用清态 / 方向零向量防御）
//
//  覆盖场景 : ① 出招触发——接触范围内冷却就绪即蓄力（attacking = true）；
//                远离（超出触接 + 6px 余量）不出招
//             ② 蓄力——移速归零 + 轻微后倾（反方向后退，中段位置回退）
//             ③ 前扑——朝锁定方向短促前冲（净位移 ≈ -4 后倾 + 10 前冲）
//             ④ 结算——玩家仍在范围 = Hit；蓄力中玩家离开 = Miss（落空）
//             ⑤ 冷却——出招后冷却期内不再出招；冷却走完可再次出招
//             ⑥ 池复用——despawn/spawnAt 后无旧攻击状态；新怪接触即可出招
//             ⑦ 零向量防御——玩家盒中心与怪物中心完全重合时仍能出招，
//                锁定方向回退 +x（确定性，无 NaN）
//
//  与其余测试的差异 : Monster.cpp / World.cpp 引用 SDL（日志/随机数），
//  本测试编译 Monster.cpp + Entity.cpp + World.cpp 并链接 SDL3 与
//  SDL3_image；实际跑到的路径不需要世界（World 指针为 nullptr 的防御
//  分支）与纹理（不做渲染），任何机器上都能确定性运行
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/Monster.h" // Monster / MonsterAttackResult（被测）；内含 Entity/UpdateContext

#include <cmath>    // std::fabs：浮点近似断言
#include <cstdio>   // std::printf
#include <cstdlib>  // std::system（Windows 控制台切 UTF-8）

namespace
{

int gPass = 0; // 通过的断言数
int gFail = 0; // 失败的断言数（作为 main 返回值）

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

// 断言：攻击结算结果枚举相等（输出可读名）
void expectResult(const char* label, MonsterAttackResult got, MonsterAttackResult expect)
{
    // 枚举相等才算通过
    const bool ok = (got == expect);
    if (ok) { ++gPass; } else { ++gFail; }
    // 枚举 -> 名称映射：让 PASS/FAIL 行一眼能读懂
    const auto name = [](MonsterAttackResult r)
    {
        // 三态映射：无/落空/命中（中文可读）
        switch (r)
        {
        case MonsterAttackResult::None: return "无";
        case MonsterAttackResult::Miss: return "落空";
        case MonsterAttackResult::Hit:  return "命中";
        }
        return "未知"; // 理论不可达；防御编译器告警
    };
    // 输出一行结果：期望名与实际名
    std::printf("  [%s] %s | 期望=%s 实际=%s\n", ok ? "PASS" : "FAIL", label, name(expect), name(got));
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

// 固定步长（秒）：与游戏一致（120Hz 的固定步）
constexpr float kDt = 1.0f / 120.0f;

// 步伐推进一步（上下文只用到 deltaTime——怪物逻辑不读输入/视口）
void step(Monster& monster, UpdateContext& ctx)
{
    monster.update(ctx); // 单步推进（AI 决策 + 攻击状态机）
}

// 推进 n 步
void stepN(Monster& monster, UpdateContext& ctx, int n)
{
    for (int i = 0; i < n; ++i)
    {
        step(monster, ctx); // 每步恰好一个固定步
    }
}

// 推进直到攻击结束（attacking 从 true 变 false）；返回实际步数（上限保护）
int stepUntilAttackEnds(Monster& monster, UpdateContext& ctx, int cap)
{
    int steps = 0; // 已推进步数
    while (monster.attacking() && steps < cap)
    {
        step(monster, ctx);
        ++steps;
    }
    return steps; // 达到上限仍未结束时返回 cap（调用方断言用）
}

// 「怪物中心」便捷查询（判定/位移断言用）
SDL_FPoint centerOf(const Monster& monster)
{
    const SDL_FRect box = monster.collisionRect(); // 脚盒（世界坐标）
    return SDL_FPoint{ box.x + box.w * 0.5f, box.y + box.h * 0.5f };
}

} // namespace

int main()
{
#if defined(_WIN32)
    // 控制台代码页切到 UTF-8：中文输出不乱码（失败也不影响测试）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" 怪物攻击状态机（蓄力前扑）单元测试（批四）\n");
    std::printf(" 接触 -> 蓄力 0.3s（后倾 4px）-> 前扑 10px -> Hit/Miss -> 冷却\n");
    std::printf("==============================================================\n");

    UpdateContext ctx{}; // 固定步上下文：只填 deltaTime（怪物不读输入/视口）
    ctx.deltaTime = kDt;

    // ------------------------------------------------------------------
    // CASE 1 出招触发与蓄力（接触判定 + 后倾）
    // ------------------------------------------------------------------
    beginCase(1, "出招触发与蓄力（接触 -> 蓄力后倾）");
    int p0 = gPass, f0 = gFail;
    {
        Monster m;            // 全新怪物（默认哥布林）
        m.spawnAt(500.0f, 500.0f);  // 出生在 (500,500)（中心坐标）
        m.setAttackCooldown(1.2f);  // 标准冷却（monster_attack_cooldown 默认值）
        m.setChaseSpeed(0.0f);      // 站桩：排除追击位移，只观察攻击动作
        // 玩家盒：左缘距怪物脚盒右缘 2px（触接 + 余量内——应触发接触）
        const SDL_FRect playerBox{ 520.0f, 490.0f, 32.0f, 26.0f };
        m.setTarget(playerBox);     // 注入目标（AI 仇恨与接触判定共用的玩家位置）

        // 未出招前
        expectTrue("初始：未在攻击", !m.attacking());

        // 第 1 步：接触 + 冷却就绪 -> 出招（进入蓄力，日志「怪物出招」）
        step(m, ctx);
        expectTrue("接触且冷却就绪 -> 出招（attacking）", m.attacking());

        // 蓄力中段推进 29 步（合计 0.25s < 0.3s——仍在蓄力窗口内）
        stepN(m, ctx, 29);
        expectTrue("0.25s 时仍在蓄力（0.3s 未到）", m.attacking());

        // 蓄力后倾方向 = 出招反方向（背离玩家）：中心应向 -x 方向回退
        const SDL_FPoint mid = centerOf(m); // 蓄力中段的中心
        expectTrue("蓄力中段：沿反方向后倾（x 回退）", mid.x < 497.5f);
        expectTrue("蓄力中段：后倾幅度轻微（未超过 5px）", mid.x > 495.0f);

        // 蓄力期间不产生结算（结果只在收招时发布一次）
        expectResult("蓄力中：无待消费结果", m.consumeAttackResult(), MonsterAttackResult::None);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 前扑与命中结算（净位移 ≈ -4 + 10；Hit 结果）
    // ------------------------------------------------------------------
    beginCase(2, "前扑命中（净位移与 Hit 结算）");
    p0 = gPass; f0 = gFail;
    {
        Monster m;
        m.spawnAt(500.0f, 500.0f); // 重新出生（中心 500,500）
        m.setAttackCooldown(1.2f);
        m.setChaseSpeed(0.0f);
        const SDL_FRect playerBox{ 520.0f, 490.0f, 32.0f, 26.0f };
        m.setTarget(playerBox);

        const SDL_FPoint before = centerOf(m); // 基线：脚盒中心（前扑前后的差值口径）

        step(m, ctx); // 出招（进入蓄力）
        expectTrue("出招进入攻击", m.attacking());

        // 推进到攻击结束（0.3s 蓄力 + 0.1s 前扑 ≈ 48 步）
        const int steps = stepUntilAttackEnds(m, ctx, 200); // 每步含 1 次推进
        expectTrue("攻击在合理步数内结束（<= 60 步）", steps <= 60);

        // 结算：玩家未动，仍在接触范围 -> Hit
        expectResult("命中结算 = Hit", m.consumeAttackResult(), MonsterAttackResult::Hit);
        // 结果只发布一次：再消费恒为 None
        expectResult("结算结果不重复发布", m.consumeAttackResult(), MonsterAttackResult::None);

        // 净位移 ≈ -4px（后倾） + 10px（前扑） = +6px 沿出招方向（≈ +x）
        const SDL_FPoint after = centerOf(m);
        expectTrue("净位移：向前约 +6px", after.x - before.x > 4.5f);
        expectTrue("净位移：不超过前冲量（> +7.5 说明过冲）", after.x - before.x < 7.5f);
        expectTrue("净位移：横向几乎不动（朝向水平）", std::fabs(after.y - before.y) < 1.5f);
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 冷却门控（出招后冷却期内不重复出招；冷却走完可再出招）
    // ------------------------------------------------------------------
    beginCase(3, "冷却门控（monster_attack_cooldown）");
    p0 = gPass; f0 = gFail;
    {
        Monster m;
        m.spawnAt(500.0f, 500.0f);
        m.setAttackCooldown(1.2f); // 1.2s = 144 步
        m.setChaseSpeed(0.0f);
        const SDL_FRect playerBox{ 520.0f, 490.0f, 32.0f, 26.0f };
        m.setTarget(playerBox);

        step(m, ctx); // 出招
        const int attackSteps = stepUntilAttackEnds(m, ctx, 200); // 攻击走完（≈48 步）
        expectTrue("第一轮攻击结束", !m.attacking() && attackSteps <= 60);
        m.consumeAttackResult(); // 消费第一轮的 Hit（进入冷却）

        // 冷却期内：连续推进 10 步不出招（1.2s 冷却远未走完）
        stepN(m, ctx, 10);
        expectTrue("冷却期内不出招", !m.attacking());

        // 推进到「再次出招」：冷却在攻击结束瞬间起算（1.2s = 144 步），
        // 上面已消耗 10 步——剩余约 134 步（浮点余量给半步）；上限 200 步
        int waitSteps = 0; // 等待步数
        while (!m.attacking() && waitSteps < 200)
        {
            step(m, ctx);
            ++waitSteps;
        }
        expectTrue("冷却走完后重新出招", m.attacking());
        expectTrue("等待步数符合冷却预期（125~148 步）",
                   waitSteps >= 125 && waitSteps <= 148);
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 蓄力落空（玩家在蓄力窗口内离开 -> Miss）
    // ------------------------------------------------------------------
    beginCase(4, "蓄力落空（玩家离开 -> Miss）");
    p0 = gPass; f0 = gFail;
    {
        Monster m;
        m.spawnAt(500.0f, 500.0f);
        m.setAttackCooldown(1.2f);
        m.setChaseSpeed(0.0f);
        m.setTarget(SDL_FRect{ 520.0f, 490.0f, 32.0f, 26.0f }); // 接触范围内

        step(m, ctx); // 出招
        expectTrue("出招进入攻击", m.attacking());

        // 蓄力中段（10 步 = 1/12 s）：玩家瞬移远离（躲避窗口生效）
        stepN(m, ctx, 10);
        m.setTarget(SDL_FRect{ 900.0f, 900.0f, 32.0f, 26.0f }); // 远离（中心约 (916,913)）

        // 攻击照常走完（前扑朝锁定旧方向），结算应为 Miss
        const int steps = stepUntilAttackEnds(m, ctx, 200);
        expectTrue("攻击照常走完（未中途取消）", steps <= 60);
        expectResult("玩家已离开 -> 落空（Miss）", m.consumeAttackResult(), MonsterAttackResult::Miss);
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 远离不出招（接触范围外 + 冷却就绪也不触发）
    // ------------------------------------------------------------------
    beginCase(5, "远离不出招（接触范围 + 6px 余量的边界）");
    p0 = gPass; f0 = gFail;
    {
        Monster m;
        m.spawnAt(500.0f, 500.0f);
        m.setAttackCooldown(0.0f); // 无冷却：只要接触就必然触发——放大「不触发」的信号
        m.setChaseSpeed(0.0f);
        // 玩家盒左缘距怪物脚盒右缘 7px（超出 6px 余量——不构成接触）
        m.setTarget(SDL_FRect{ 525.0f, 490.0f, 32.0f, 26.0f });

        stepN(m, ctx, 240); // 2 秒（远超任何攻击触发窗口）
        expectTrue("远离：始终未出招", !m.attacking());
        expectResult("远离：无结算结果", m.consumeAttackResult(), MonsterAttackResult::None);
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // CASE 6 零向量防御（玩家盒中心与怪物中心完全重合）
    // ------------------------------------------------------------------
    beginCase(6, "零向量防御（完全重合 -> +x 回退方向）");
    p0 = gPass; f0 = gFail;
    {
        Monster m;
        m.spawnAt(500.0f, 500.0f); // 怪物中心 (500,500)
        m.setAttackCooldown(0.0f);
        m.setChaseSpeed(0.0f);
        // 玩家盒中心恰好也在 (500,500)：出招方向向量长度为 0 —— 回退 +x
        m.setTarget(SDL_FRect{ 484.0f, 487.0f, 32.0f, 26.0f });

        const SDL_FPoint before = centerOf(m); // 基线：脚盒中心（前后差值口径）

        step(m, ctx); // 重合中出招（零向量防御路径）
        expectTrue("完全重合也能出招（不崩、无 NaN）", m.attacking());

        const int steps = stepUntilAttackEnds(m, ctx, 200);
        expectTrue("攻击走完", steps <= 60);
        expectResult("重合结算 = Hit（仍在范围内）", m.consumeAttackResult(), MonsterAttackResult::Hit);

        // 锁定方向回退 +x：净位移 ≈ -4 + 10 = +6 全在 x 轴、y 不动
        const SDL_FPoint after = centerOf(m);
        expectTrue("回退方向 +x：净位移 +6（x）", after.x - before.x > 4.5f && after.x - before.x < 7.5f);
        expectTrue("回退方向 +x：y 几乎不动", std::fabs(after.y - before.y) < 0.2f);
    }
    endCase(6, p0, f0);

    // ------------------------------------------------------------------
    // CASE 7 池复用（despawn / spawnAt 后不带旧攻击状态）
    // ------------------------------------------------------------------
    beginCase(7, "池复用（旧攻击状态清零）");
    p0 = gPass; f0 = gFail;
    {
        Monster m;
        m.spawnAt(300.0f, 300.0f);
        m.setAttackCooldown(0.0f);
        m.setChaseSpeed(0.0f);
        m.setTarget(SDL_FRect{ 320.0f, 290.0f, 32.0f, 26.0f }); // 接触

        stepN(m, ctx, 3); // 出招并处于蓄力中（attacking = true）
        expectTrue("复用前：攻击进行中", m.attacking());

        // 回收：攻击状态必须清零（阶段/计时/脉冲/待消费结果）
        m.despawn();
        expectTrue("despawn 后：无攻击进行", !m.attacking());
        expectResult("despawn 后：无残留结算结果", m.consumeAttackResult(), MonsterAttackResult::None);

        // 池复用：换落点重新出生
        m.spawnAt(700.0f, 700.0f);
        expectTrue("spawnAt 后：新怪不带旧攻击状态", !m.attacking());
        expectResult("spawnAt 后：无残留结算结果", m.consumeAttackResult(), MonsterAttackResult::None);

        // 新怪接触即可出招（冷却已在清态中归零——池复用的干净起点）
        m.setChaseSpeed(0.0f);
        m.setTarget(SDL_FRect{ 720.0f, 690.0f, 32.0f, 26.0f }); // 接触
        step(m, ctx);
        expectTrue("复用后新怪：接触即可出招", m.attacking());
    }
    endCase(7, p0, f0);

    // ------------------------------------------------------------------
    // 汇总
    // ------------------------------------------------------------------
    const int total = gPass + gFail; // 断言总数
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 条断言，通过 %d，失败 %d\n", total, gPass, gFail);
    std::printf("==============================================================\n");
    return gFail; // 退出码 = 失败数（0 = 全过）
}
