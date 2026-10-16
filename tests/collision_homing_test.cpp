// ============================================================================
//  collision_homing_test.cpp — 碰撞盒（Entity）/ 最小分离（Separation）/
//  追踪吸附（Homing）单元测试（批四）
//
//  被测对象 : libs/GameEngine/include/engine/Entity.h + src/Entity.cpp
//             （碰撞盒：setCollisionBox / collisionRect / collisionRectAt）
//             Src/inc/game/Separation.h（最小分离 MTV，header-only 纯函数）
//             Src/inc/game/Homing.h（追踪吸附单步积分，header-only 纯函数）
//  覆盖场景 : ① 碰撞盒——缺省（未设置）= 整张精灵、与 rect() 全等；
//                设置后的位置/尺寸换算；collisionRectAt 候选位置；
//                非法尺寸（0/负）被忽略且保留原盒；setPosition 后跟随
//             ② 最小分离——不重叠/恰好贴合不产出位移；四方向（左右上下）
//                的推出方向与幅度（含亚像素余量）；渗透相等取 X 的确定性；
//                完全重合（零分离向量）的 +x/+y 回退防御；
//                应用分离量后 rectsOverlap 必须为假（闭环验证）
//             ③ 追踪速度——加速与封顶；非正步长不动；负速度/负加速度钳 0；
//                maxSpeed 非正视为无上限（防御）
//             ④ 追踪位移——不足一步准确推进；恰好一步落在目标点；
//                超过一步 clamp 到目标点（绝不过冲）；零步长/已在目标点
//                原样返回（零除防御）；斜向位移的长度正确性
//
//  与其余测试的差异 : Entity.cpp 引用 SDL（日志/纹理 API），本测试编译
//  Entity.cpp 并链接 SDL3 与 SDL3_image 导入库；实际跑到的路径全部是
//  纯计算（不创建窗口、不加载素材），任何机器上都能确定性运行
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "engine/Entity.h"   // Entity：碰撞盒（被测）
#include "engine/Rect.h"     // rectsOverlap：分离闭环验证用（引擎既有 AABB 判定）
#include "game/Separation.h" // computeSeparation：最小分离（被测）
#include "game/Homing.h"     // advanceHomingSpeed / advanceHomingPoint（被测）

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

// 测试用实体：模拟派生类「构造时固定尺寸」的用法（48x84 与玩家同尺寸）
class TestEntity : public Entity
{
public:
    // 固定尺寸：Entity 的尺寸习惯由派生类构造写入（受保护成员可直接访问）
    TestEntity()
    {
        m_w = 48.0f; // 宽 48（与玩家精灵一致）
        m_h = 84.0f; // 高 84
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
    std::printf(" 碰撞盒 / 最小分离 / 追踪吸附 单元测试（批四）\n");
    std::printf(" Entity.collisionRect（脚盒换算）/ computeSeparation / advanceHoming*\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 碰撞盒：缺省整张精灵 / 设置后换算 / 非法忽略 / 位置跟随
    // ------------------------------------------------------------------
    beginCase(1, "碰撞盒（Entity）");
    int p0 = gPass, f0 = gFail;
    {
        TestEntity e;               // 未设置碰撞盒的实体
        e.setPosition(100.0f, 200.0f); // 瞬移到位（内部同步插值快照）

        // 缺省语义：碰撞盒 = 整张精灵（与 rect() 完全一致）
        SDL_FRect r = e.collisionRect();
        expectNear("缺省 x = 100", r.x, 100.0f);
        expectNear("缺省 y = 200", r.y, 200.0f);
        expectNear("缺省 w = 48", r.w, 48.0f);
        expectNear("缺省 h = 84", r.h, 84.0f);
        const SDL_FRect full = e.rect(); // 完整精灵盒
        expectTrue("缺省 collisionRect 与 rect() 相同",
                   r.x == full.x && r.y == full.y && r.w == full.w && r.h == full.h);

        // 设置脚部小盒（32x26、偏移 (8,54)——与玩家实际取值一致）
        e.setCollisionBox(32.0f, 26.0f, 8.0f, 54.0f);
        r = e.collisionRect();
        expectNear("脚盒 x = 100+8", r.x, 108.0f);   // 精灵左上角 + 偏移
        expectNear("脚盒 y = 200+54", r.y, 254.0f);
        expectNear("脚盒 w = 32", r.w, 32.0f);
        expectNear("脚盒 h = 26", r.h, 26.0f);

        // collisionRectAt：候选位置（不改状态）——供落点校验
        const SDL_FRect at = e.collisionRectAt(0.0f, 0.0f);
        expectNear("at(0,0).x = 8", at.x, 8.0f);
        expectNear("at(0,0).y = 54", at.y, 54.0f);
        expectNear("at(0,0).w = 32", at.w, 32.0f);
        expectNear("at(0,0).h = 26", at.h, 26.0f);

        // 非法尺寸（0 / 负）：忽略并保留原盒（不静默降级为「无盒」）
        e.setCollisionBox(0.0f, 26.0f, 0.0f, 0.0f);   // 宽为 0
        e.setCollisionBox(32.0f, -5.0f, 0.0f, 0.0f);  // 高为负
        r = e.collisionRect();
        expectNear("非法尺寸被忽略：仍为 32 宽", r.w, 32.0f);
        expectNear("非法尺寸被忽略：仍为 26 高", r.h, 26.0f);

        // 位置跟随：setPosition 后碰撞盒随精灵左上角平移
        e.setPosition(0.0f, 0.0f);
        r = e.collisionRect();
        expectNear("瞬移后脚盒 x = 8", r.x, 8.0f);
        expectNear("瞬移后脚盒 y = 54", r.y, 54.0f);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 最小分离（MTV）：方向/幅度/贴合/重合防御/闭环
    // ------------------------------------------------------------------
    beginCase(2, "最小分离（computeSeparation）");
    p0 = gPass; f0 = gFail;
    {
        // 不重叠：无位移
        {
            const SDL_FRect a{ 0.0f, 0.0f, 10.0f, 10.0f };
            const SDL_FRect b{ 20.0f, 0.0f, 10.0f, 10.0f };
            const SeparationResult sep = computeSeparation(a, b);
            expectTrue("相离：不判定重叠", !sep.overlapping);
            expectNear("相离：pushX = 0", sep.pushX, 0.0f);
            expectNear("相离：pushY = 0", sep.pushY, 0.0f);
        }
        // 恰好贴合（右缘 == 左缘）：不算重叠（与 rectsOverlap 同约定）
        {
            const SDL_FRect a{ 0.0f, 0.0f, 10.0f, 10.0f };
            const SDL_FRect b{ 10.0f, 0.0f, 30.0f, 10.0f };
            const SeparationResult sep = computeSeparation(a, b);
            expectTrue("贴合：不判定重叠", !sep.overlapping);
        }
        // 水平重叠、a 在左：沿 -x 推出（渗透 5 + 余量 0.01）
        {
            const SDL_FRect a{ 0.0f, 0.0f, 10.0f, 10.0f };
            const SDL_FRect b{ 5.0f, 0.0f, 10.0f, 10.0f };
            const SeparationResult sep = computeSeparation(a, b);
            expectTrue("左压右：判定重叠", sep.overlapping);
            expectNear("左压右：pushX = -(5.01)", sep.pushX, -5.01f);
            expectNear("左压右：pushY = 0", sep.pushY, 0.0f);
        }
        // 水平重叠、a 在右：沿 +x 推出
        {
            const SDL_FRect a{ 5.0f, 0.0f, 10.0f, 10.0f };
            const SDL_FRect b{ 0.0f, 0.0f, 10.0f, 10.0f };
            const SeparationResult sep = computeSeparation(a, b);
            expectNear("右压左：pushX = +(5.01)", sep.pushX, 5.01f);
        }
        // 垂直重叠、a 在上：沿 -y 推出（垂直渗透更小 -> 取 Y 轴）
        {
            const SDL_FRect a{ 0.0f, 0.0f, 10.0f, 10.0f };
            const SDL_FRect b{ 0.0f, 5.0f, 10.0f, 10.0f };
            const SeparationResult sep = computeSeparation(a, b);
            expectTrue("上压下：判定重叠", sep.overlapping);
            expectNear("上压下：pushX = 0", sep.pushX, 0.0f);
            expectNear("上压下：pushY = -(5.01)", sep.pushY, -5.01f);
        }
        // 垂直重叠、a 在下：沿 +y 推出
        {
            const SDL_FRect a{ 0.0f, 5.0f, 10.0f, 10.0f };
            const SDL_FRect b{ 0.0f, 0.0f, 10.0f, 10.0f };
            const SeparationResult sep = computeSeparation(a, b);
            expectNear("下压上：pushY = +(5.01)", sep.pushY, 5.01f);
        }
        // 渗透相等（5 == 5）：确定性取 X 轴（文件头口径）
        {
            const SDL_FRect a{ 0.0f, 0.0f, 10.0f, 10.0f };
            const SDL_FRect b{ 5.0f, 5.0f, 10.0f, 10.0f };
            const SeparationResult sep = computeSeparation(a, b);
            expectNear("渗透相等取 X 轴：pushX = -(5.01)", sep.pushX, -5.01f);
            expectNear("渗透相等取 X 轴：pushY = 0", sep.pushY, 0.0f);
        }
        // 完全重合（零分离向量防御）：取 X 轴、+x 确定性回退，推出量 = 宽 + 余量
        {
            const SDL_FRect a{ 0.0f, 0.0f, 10.0f, 10.0f };
            const SDL_FRect b{ 0.0f, 0.0f, 10.0f, 10.0f };
            const SeparationResult sep = computeSeparation(a, b);
            expectTrue("完全重合：判定重叠", sep.overlapping);
            expectNear("完全重合：pushX = +(10.01)（+x 回退）", sep.pushX, 10.01f);
            expectNear("完全重合：pushY = 0", sep.pushY, 0.0f);
        }
        // 闭环验证：应用分离量后两盒不再重叠（rectsOverlap 为假）
        {
            SDL_FRect a{ 0.0f, 0.0f, 10.0f, 10.0f };
            const SDL_FRect b{ 5.0f, 0.0f, 10.0f, 10.0f };
            const SeparationResult sep = computeSeparation(a, b);
            a.x += sep.pushX; // 应用推出量（模拟消费侧 nudge）
            a.y += sep.pushY;
            expectTrue("闭环：推出后不再重叠", !rectsOverlap(a, b));
        }
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 追踪速度：加速 / 封顶 / 步长与负值防御
    // ------------------------------------------------------------------
    beginCase(3, "追踪速度（advanceHomingSpeed）");
    p0 = gPass; f0 = gFail;
    {
        // 常规线性加速：700 + 800 x 0.1 = 780
        expectNear("700 加速 0.1s = 780", advanceHomingSpeed(700.0f, 800.0f, 900.0f, 0.1f), 780.0f);
        // 加速撞到封顶：700 + 800 x 0.25 = 900（恰好封顶）
        expectNear("700 加速 0.25s = 900（封顶）", advanceHomingSpeed(700.0f, 800.0f, 900.0f, 0.25f), 900.0f);
        // 已接近封顶：一步内不超顶
        expectNear("890 加速 0.1s = 900（不超顶）", advanceHomingSpeed(890.0f, 800.0f, 900.0f, 0.1f), 900.0f);
        // 非正步长：原样返回（暂停/异常调用不推进）
        expectNear("dt=0 不推进", advanceHomingSpeed(750.0f, 800.0f, 900.0f, 0.0f), 750.0f);
        expectNear("dt<0 不推进", advanceHomingSpeed(750.0f, 800.0f, 900.0f, -0.5f), 750.0f);
        // 负速度（调用方错误）：按 0 起步——吸附速度只有正向语义
        expectNear("负速度钳 0 后加速", advanceHomingSpeed(-100.0f, 2000.0f, 800.0f, 0.1f), 200.0f);
        // 负加速度：不加速（保持原速）
        expectNear("负加速度不加", advanceHomingSpeed(500.0f, -100.0f, 800.0f, 0.1f), 500.0f);
        // maxSpeed 非正（防御）：视为无上限
        expectNear("maxSpeed=0 无上限", advanceHomingSpeed(100.0f, 1000.0f, 0.0f, 0.1f), 200.0f);
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 追踪位移：推进 / 落点 / 不过冲 / 零除防御 / 斜向
    // ------------------------------------------------------------------
    beginCase(4, "追踪位移（advanceHomingPoint）");
    p0 = gPass; f0 = gFail;
    {
        // 水平推进（不足一步）：(0,0) 朝 (100,0) 走 5 -> (5,0)
        {
            const HomingPoint p = advanceHomingPoint(0.0f, 0.0f, 100.0f, 0.0f, 5.0f);
            expectNear("不足一步：x = 5", p.x, 5.0f);
            expectNear("不足一步：y = 0", p.y, 0.0f);
        }
        // 恰好一步：距离 10、步长 10 -> 落在目标点 (10,0)
        {
            const HomingPoint p = advanceHomingPoint(0.0f, 0.0f, 10.0f, 0.0f, 10.0f);
            expectNear("恰好一步：x = 10（落点）", p.x, 10.0f);
        }
        // 超过一步（防过冲）：距离 10、步长 25 -> clamp 在目标点，绝不越过
        {
            const HomingPoint p = advanceHomingPoint(0.0f, 0.0f, 10.0f, 0.0f, 25.0f);
            expectNear("超步 clamp：x = 10（不过冲）", p.x, 10.0f);
            expectNear("超步 clamp：y = 0", p.y, 0.0f);
        }
        // 已在目标点：原样返回（零距离防御——不产生 0/0）
        {
            const HomingPoint p = advanceHomingPoint(7.0f, 8.0f, 7.0f, 8.0f, 5.0f);
            expectNear("已在目标点：x 不变", p.x, 7.0f);
            expectNear("已在目标点：y 不变", p.y, 8.0f);
        }
        // 零步长 / 负步长：原样返回（防御）
        {
            const HomingPoint p = advanceHomingPoint(1.0f, 2.0f, 10.0f, 10.0f, 0.0f);
            expectNear("零步长：x 不变", p.x, 1.0f);
            expectNear("零步长：y 不变", p.y, 2.0f);
            const HomingPoint q = advanceHomingPoint(1.0f, 2.0f, 10.0f, 10.0f, -3.0f);
            expectNear("负步长：x 不变", q.x, 1.0f);
        }
        // 斜向（3-4-5 直角三角形）：距离 5、步长 2.5 -> 中点 (1.5, 2)
        {
            const HomingPoint p = advanceHomingPoint(0.0f, 0.0f, 3.0f, 4.0f, 2.5f);
            expectNear("斜向中点：x = 1.5", p.x, 1.5f);
            expectNear("斜向中点：y = 2", p.y, 2.0f);
        }
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
