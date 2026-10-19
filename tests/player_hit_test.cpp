// ============================================================================
//  player_hit_test.cpp — 玩家受击反馈（击退/输入抑制/不穿墙）单元测试（批五A）
//
//  被测对象 : Src/cpp/game/Player.cpp 的受击击退部分（applyHitKnockback +
//             update 内的指数衰减推进 / 输入抑制窗口 / 重置归位）
//
//  覆盖场景 : ① 击退位移与时长——沿来源反方向总位移 40~50px、历时约
//                0.18s（22 个固定步）走完；窗口结束后不再位移；方向正确
//             ② 输入抑制——击退窗口内按住反向 WASD 不产生位移（只退不
//                进）；窗口结束后输入恢复（位置随按键重新增长）
//             ③ 不穿墙——贴墙受击：朝墙方向的击退被碰撞滑动截停（位移 ~0）
//             ④ 重置归位——击退中途 reset：位置就位且残余击退不再位移
//
//  与其余测试的差异 : Player.cpp 引用 SDL（日志 / IMG 纹理加载符号），
//  本测试编译 Player.cpp + World.cpp + Entity.cpp + Input.cpp 并链接
//  SDL3 与 SDL3_image；实际跑到的路径不加载纹理（不渲染）、不依赖真实
//  输入（Input::updateFromKeyArray 注入合成按键）与窗口，任何机器上都能
//  确定性运行
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/Player.h"  // Player（被测）；内含 Entity/UpdateContext
#include "game/World.h"   // World：碰撞滑动与可走查询（击退位移的物理环境）
#include "engine/Input.h" // Input：合成键盘注入（输入抑制用例）

#include <cmath>   // std::fabs：浮点近似断言
#include <cstdio>  // std::printf
#include <cstdlib> // std::system（Windows 控制台切 UTF-8）

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
    // 输出一行结果：PASS/FAIL + 用例名 + 期望与实际
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

// 玩家脚盒中心（世界坐标）：击退位移断言统一用碰撞盒口径
SDL_FPoint centerOf(const Player& player)
{
    const SDL_FRect box = player.collisionRect(); // 脚盒（世界坐标）
    return SDL_FPoint{ box.x + box.w * 0.5f, box.y + box.h * 0.5f };
}

// 玩家精灵中心（世界坐标）：构造「伤害来源」参考点用——与
// applyHitKnockback 内部的方向口径一致（函数取精灵中心计算方向；
// 若拿脚盒中心当来源，两者 y 差 25px 会引入斜向角度偏差）
SDL_FPoint spriteCenterOf(const Player& player)
{
    const SDL_FRect r = player.rect(); // 完整精灵盒
    return SDL_FPoint{ r.x + r.w * 0.5f, r.y + r.h * 0.5f };
}

// 推进 n 个固定步（上下文只填 deltaTime——无输入/无渲染路径）
void stepN(Player& player, UpdateContext& ctx, int n)
{
    for (int i = 0; i < n; ++i)
    {
        player.update(ctx); // 每个固定步一次
    }
}

} // namespace

int main()
{
#if defined(_WIN32)
    // 控制台代码页切到 UTF-8：中文输出不乱码（失败也不影响测试）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" 玩家受击反馈（击退/输入抑制/不穿墙）单元测试（批五A）\n");
    std::printf(" 击退：初速 480px/s 指数衰减、约 42px / 0.18s；期间输入被抑制\n");
    std::printf("==============================================================\n");

    UpdateContext ctx{}; // 固定步上下文：只填 deltaTime（其余字段缺失走防御分支）
    ctx.deltaTime = kDt;

    // ------------------------------------------------------------------
    // CASE 1 击退位移与时长（方向 / 总量 / 窗口后不再位移）
    // ------------------------------------------------------------------
    beginCase(1, "击退位移与时长（总位移 40~50px、约 0.18s 走完）");
    int p0 = gPass, f0 = gFail;
    {
        World world;                      // 1x1 区块世界（768x768：出生保护区必然空旷）
        world.generate(1, 1, 12345u);     // 固定种子：布局确定（可复现）
        Player player;
        player.setWorld(&world);          // 注入世界：击退位移走碰撞滑动
        const SDL_FPoint spawn = world.spawnPointFor(player.width(), player.height());
        player.reset(spawn.x, spawn.y);   // 出生在世界中心（保护区）

        const SDL_FPoint before = centerOf(player); // 基线：脚盒中心
        // 来源在左侧 200px（y 取精灵中心——与函数内部方向口径一致）：
        // 击退方向 = 来源 -> 玩家 = 朝 +x（向右）
        const SDL_FPoint src = spriteCenterOf(player);
        player.applyHitKnockback(src.x - 200.0f, src.y);

        stepN(player, ctx, 22); // 0.183s：击退窗口（0.18s ≈ 22 步）刚走完
        const float dx22 = centerOf(player).x - before.x; // 窗口满时的位移
        expectTrue("窗口满（22 步）时位移已 >= 40px", dx22 >= 40.0f);

        stepN(player, ctx, 18); // 再推 0.15s：窗口已结束
        const SDL_FPoint after = centerOf(player);
        const float dx = after.x - before.x; // 总位移
        expectTrue("击退总位移在 40~50px 区间", dx >= 40.0f && dx <= 50.0f);
        expectTrue("窗口后位移不再增长（衰减已清零）", dx - dx22 <= 0.5f);
        expectTrue("击退方向沿来源反方向（+x）", dx > 0.0f);
        expectNear("击退方向垂直分量（|dy| 应 ≈ 0）", after.y - before.y, 0.0f, 0.001f);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 输入抑制（窗口内只退不进；窗口结束输入恢复）
    // ------------------------------------------------------------------
    beginCase(2, "输入抑制（击退窗口内按住反向键不生效、窗口后恢复）");
    p0 = gPass; f0 = gFail;
    {
        World world;
        world.generate(1, 1, 12345u);
        Player player;
        player.setWorld(&world);
        const SDL_FPoint spawn = world.spawnPointFor(player.width(), player.height());
        player.reset(spawn.x, spawn.y);

        Input input; // 合成输入系统：绑定四个移动动作（与游戏绑定一致）
        input.bind("move_up", SDL_SCANCODE_W);
        input.bind("move_down", SDL_SCANCODE_S);
        input.bind("move_left", SDL_SCANCODE_A);
        input.bind("move_right", SDL_SCANCODE_D);
        ctx.input = &input; // 注入上下文：Player::update 读动作查询

        bool keys[SDL_SCANCODE_COUNT] = {}; // 全松起步
        keys[SDL_SCANCODE_D] = true;        // 按住 D（右移 +x）
        input.updateFromKeyArray(keys);     // 采样：held(move_right) = true

        const SDL_FPoint before = centerOf(player);
        // 来源在右侧 200px（y 取精灵中心——方向口径一致）：击退朝 -x（左）
        // ——与 D 键（+x）方向相反。输入若未被抑制，+x 速度会抵消一部分
        // 击退；被抑制则应「只退不进」
        const SDL_FPoint src = spriteCenterOf(player);
        player.applyHitKnockback(src.x + 200.0f, src.y);

        stepN(player, ctx, 22); // 覆盖完整击退窗口（0.18s ≈ 22 步）
        const float dxKnock = centerOf(player).x - before.x; // 窗口内位移
        expectTrue("击退窗口内：按住 D 键仍只退不进（位移 <= -40）", dxKnock <= -40.0f);

        const float xAtWindowEnd = centerOf(player).x; // 窗口结束时刻的位置
        stepN(player, ctx, 30); // 再推 0.25s：输入恢复后应随 D 键向右移动
        const float dxAfter = centerOf(player).x - xAtWindowEnd; // 恢复后的位移
        expectTrue("窗口结束后：输入恢复（位置随 D 键回升 > 20px）", dxAfter > 20.0f);
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 不穿墙（贴墙受击：朝墙方向的击退被截停）
    // ------------------------------------------------------------------
    beginCase(3, "不穿墙（贴墙击退被碰撞截停）");
    p0 = gPass; f0 = gFail;
    {
        World world;
        world.generate(1, 1, 54321u); // 固定种子：墙/水布局确定

        // 找一面「左阻挡（墙）右空」的格（多行扫描：挑第一个满足的）。
        // 批六乙：玩家可进入水域——水不再是碰撞阻挡，因此本用例必须找
        // **真正的墙**（仍实心）；水格只作为世界边界存在，不参与截停验证
        int wallX = -999; // 阻挡格 x（配对格 wallX+1 可走）
        int wallY = -999; // 阻挡格 y（同一行）
        for (int y = 0; y < world.cellsY() && wallX < 0; ++y) // 逐行扫描（全图找墙）
        {
            for (int x = 0; x < world.cellsX() - 1; ++x) // 逐格向右扫描（右侧必须有配对格）
            {
                // 本格是墙，且右侧相邻格可走（墙/水都不行——配对格必须真正空）
                const bool blockedHere = world.wallAt(x, y); // 本格墙
                const bool blockedRight = world.wallAt(x + 1, y) || world.waterAt(x + 1, y); // 右邻阻挡
                if (blockedHere && !blockedRight) // 左墙右空
                {
                    wallX = x; // 命中：记录并结束
                    wallY = y;
                    break;
                }
            }
        }
        expectTrue("环境构置：找到一面「左墙右空」的格（可复现扫描）", wallX >= 0);
        if (wallX >= 0)
        {
            Player player;
            player.setWorld(&world);
            // 玩家脚盒（32x26、偏移 (8,54)）贴墙站：左缘距墙右缘 2px、
            // 整体落在配对格的左端（不跨格），精灵左上角由脚盒反推
            const float footLeft = static_cast<float>(wallX + 1) * 48.0f + 2.0f; // 脚盒左缘
            const float spriteX = footLeft - 8.0f;                              // 精灵 x = 脚盒左缘 - 偏移 x
            const float spriteY = static_cast<float>(wallY) * 48.0f + 10.0f - 54.0f; // 精灵 y（脚盒落行内）
            player.reset(spriteX, spriteY);

            // 环境自检：贴墙站位必须可走（否则本用例前提不成立——直接失败示警）
            expectTrue("环境自检：贴墙站位的脚盒可走", world.isRectWalkable(player.collisionRect()));

            const SDL_FPoint before = centerOf(player);
            // 来源在右侧（y 取精灵中心——方向口径一致）：击退朝 -x（墙的
            // 方向）——44px 预算被墙截停
            const SDL_FPoint src = spriteCenterOf(player);
            player.applyHitKnockback(src.x + 200.0f, src.y);
            stepN(player, ctx, 40); // 0.333s：若穿墙早已穿入（窗口 0.18s）
            const float dx = centerOf(player).x - before.x;
            expectTrue("朝墙击退被截停：位移约 0（不穿墙）", dx > -2.0f && dx < 1.0f);
        }
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 重置归位（击退中途 reset：位置就位、残余击退清零）
    // ------------------------------------------------------------------
    beginCase(4, "重置归位（击退中途 reset 清态）");
    p0 = gPass; f0 = gFail;
    {
        World world;
        world.generate(1, 1, 777u);
        Player player;
        player.setWorld(&world);
        const SDL_FPoint spawn = world.spawnPointFor(player.width(), player.height());
        player.reset(spawn.x, spawn.y);

        // 触发击退并推进 5 步（0.04s——击退进行中）
        const SDL_FPoint src = spriteCenterOf(player); // 来源在左侧（纯 +x 击退）
        player.applyHitKnockback(src.x - 300.0f, src.y);
        stepN(player, ctx, 5);

        // 中途重开归位：位置瞬间覆盖到目标点、击退状态清零
        const float targetX = spawn.x + 100.0f; // 世界内的就位点（保护区附近）
        const float targetY = spawn.y + 50.0f;
        player.reset(targetX, targetY);
        expectNear("reset 后精灵位置就位（x）", player.rect().x, targetX, 0.001f);
        expectNear("reset 后精灵位置就位（y）", player.rect().y, targetY, 0.001f);

        stepN(player, ctx, 30); // 0.25s：残余击退若未清会产生位移
        expectNear("reset 后残余击退不再位移（x）", player.rect().x, targetX, 0.001f);
        expectNear("reset 后残余击退不再位移（y）", player.rect().y, targetY, 0.001f);
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // 汇总
    // ------------------------------------------------------------------
    const int total = gPass + gFail; // 断言总数
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 条断言，通过 %d，失败 %d\n", total, gPass, gFail);
    std::printf("==============================================================\n");
    return gFail; // 退出码 = 失败数（0 = 全过）
}
