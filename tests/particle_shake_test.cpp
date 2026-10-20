// ============================================================================
//  particle_shake_test.cpp — 粒子池与相机震屏单元测试（M3·甲）
//
//  被测对象 :
//    - engine/ParticleSystem.h + src/ParticleSystem.cpp（固定池 512）：
//      发射/池满覆盖最旧/寿命到期释放/重力与阻尼积分/清空；
//    - engine/Camera.h（header-only）的震屏：
//      addShake 叠加语义 / update 线性衰减 / shakeOffset 边界 /
//      clearShake / worldToScreen 含抖动而 screenToWorld 稳定（瞄准不抖）。
//
//  覆盖场景 :
//    ① 对象池容量：发射 600 粒 -> 活跃恒为 512（多出的覆盖最旧）；
//    ② 池满策略：满池后再发 1 粒 -> 游标处（最旧）被覆盖，其余保留；
//    ③ 寿命淡出：life/maxLife 随 update 递减，归零即释放槽位；
//    ④ 物理推进：无阻尼时重力使 vy 增加、位置按速度积分；有阻尼时速度指数衰减；
//    ⑤ clear：全部槽位释放、游标归零（再次发射从池头开始）；
//    ⑥ 震屏：偏移长度 <= 当前振幅、线性衰减、到期归零、弱震不覆盖强震、
//       clearShake 立即归零；
//    ⑦ 瞄准稳定：震屏期间 screenToWorld 不含抖动（输入稳定），
//       worldToScreen 与无震屏位置之差 = 当前 shakeOffset。
//
//  与其余测试的差异 : ParticleSystem.cpp 的 render 路径引用 SDL 绘制符号，
//  因此编译 ParticleSystem.cpp 并链接主工程构建出的 SDL3 导入库；实际跑到的
//  路径全部是纯计算（不创建窗口、不调用 render），任何环境都能确定性运行。
//
//  构建运行命令（与其余测试同一工程）：
//      cmake -S tests -B tests/build
//      cmake --build tests/build --config Debug
//      tests\bin\particle_shake_test.exe
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "engine/Camera.h"         // Camera：震屏与坐标换算（header-only）
#include "engine/ParticleSystem.h" // ParticleSystem：固定池粒子（编译其 .cpp）

#include <cmath>   // std::fabs / std::exp / std::sqrt：浮点近似与向量长度断言
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

// 开启一个用例（打印标题并记录起始断言计数）
void beginCase(int idx, const char* title)
{
    std::printf("\n[CASE %d] %s\n", idx, title); // 用例标题
}

// 结束一个用例（汇总本用例新增的通过/失败数）
void endCase(int idx, int passBefore, int failBefore)
{
    // 本用例新增的通过/失败数
    const int passed = gPass - passBefore; // 本用例通过数
    const int failed = gFail - failBefore; // 本用例失败数
    std::printf("  ---- CASE %d 小结: %d/%d 通过%s\n", idx, passed, passed + failed,
                failed ? "  <<<< 存在失败" : "");
}

// 构造一份「静止粒子」参数：速度 0、长寿命、固定尺寸，用于池容量/覆盖测试
ParticleParams makeStaticParams(float lifeSeconds, SDL_Color color)
{
    ParticleParams params;                        // 默认参数包
    params.color = color;                         // 指定颜色（验证覆盖后的颜色）
    params.speedMin = 0.0f;                       // 静止：速度下限 0
    params.speedMax = 0.0f;                       // 静止：速度上限 0
    params.lifeMin = lifeSeconds;                 // 固定寿命下限
    params.lifeMax = lifeSeconds;                 // 固定寿命上限
    params.sizeMin = 3.0f;                        // 固定边长下限
    params.sizeMax = 3.0f;                        // 固定边长上限
    params.spreadRadians = 0.0f;                  // 单方向（速度 0，无实际影响）
    params.gravityY = 0.0f;                       // 无重力
    params.damping = 0.0f;                        // 无阻尼
    return params;                                // 交还参数包
}

// 构造一份「直线向东」的物理测试参数：速度固定 100、寿命 1s
ParticleParams makeMotionParams(float gravityY, float damping)
{
    ParticleParams params;                        // 默认参数包
    params.color = SDL_Color{ 255, 255, 255, 255 }; // 白色（物理测试不关心颜色）
    params.speedMin = 100.0f;                     // 固定速度下限 100
    params.speedMax = 100.0f;                     // 固定速度上限 100
    params.lifeMin = 1.0f;                        // 固定寿命下限 1s
    params.lifeMax = 1.0f;                        // 固定寿命上限 1s
    params.sizeMin = 2.0f;                        // 固定边长下限
    params.sizeMax = 2.0f;                        // 固定边长上限
    params.directionRadians = 0.0f;               // 朝屏幕右方（+x）
    params.spreadRadians = 0.0f;                  // 无张角：直线发射
    params.gravityY = gravityY;                   // 指定的重力
    params.damping = damping;                     // 指定的阻尼
    return params;                                // 交还参数包
}

// 震屏偏移长度（|shakeOffset|），用于「不超过当前振幅」的断言
float shakeLength(const Camera& camera)
{
    const Vec2 offset = camera.shakeOffset();     // 当前偏移向量
    return std::sqrt(offset.x * offset.x + offset.y * offset.y); // 欧氏长度
}

} // 匿名命名空间结束

int main()
{
#if defined(_WIN32)
    // 控制台切 UTF-8 代码页（中文输出可读；失败也不影响断言）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" 粒子池与相机震屏单元测试（M3·甲）\n");
    std::printf(" 对象池 512 / 池满覆盖最旧 / 寿命淡出 / 重力阻尼 / 震屏衰减\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 对象池容量与池满策略
    // ------------------------------------------------------------------
    beginCase(1, "对象池容量与池满策略（覆盖最旧）");
    int p0 = gPass; // 本用例起始通过数
    int f0 = gFail; // 本用例起始失败数
    {
        ParticleSystem particles; // 被测对象：默认 512 槽位全空
        expectTrue("初始无活跃粒子", particles.aliveCount() == 0); // 空池断言
        expectTrue("容量常量 = 512", ParticleSystem::capacity() == 512); // 规格容量断言

        // 发射 600 粒（超过容量）：池满后新粒子按策略覆盖最旧，活跃数恒为 512
        const ParticleParams params = makeStaticParams(100.0f, SDL_Color{ 255, 0, 0, 255 }); // 红色长寿命
        particles.emitBurst(1.0f, 1.0f, 600, params); // 超量爆发
        expectTrue("发射 600 粒后活跃数 = 512（池满封顶）", particles.aliveCount() == 512); // 容量断言

        // 为消除「600 粒后游标落在 88」的干扰，清空后恰好发射 512 粒：
        // 此时游标回到槽位 0，再追加 1 粒即可确定性验证「覆盖最旧」
        particles.clear(); // 回到空池（游标归零）
        particles.emitBurst(1.0f, 1.0f, 512, params); // 恰好填满 512 槽位（游标回到 0）
        expectTrue("恰好 512 粒后池满", particles.aliveCount() == 512); // 前置断言

        // 满池再发 1 粒（蓝色）：游标此刻在槽位 0，因此槽位 0 被覆盖成
        // 蓝色新粒子，槽位 1 仍是上一轮的红色旧粒子
        const ParticleParams blue = makeStaticParams(100.0f, SDL_Color{ 0, 0, 255, 255 }); // 蓝色长寿命
        particles.emitBurst(2.0f, 2.0f, 1, blue); // 满池追加 1 粒
        expectTrue("满池追加后活跃数仍 = 512", particles.aliveCount() == 512); // 不扩容断言
        expectNear("槽位 0（最旧）被新粒子覆盖：x = 2", particles.particles()[0].x, 2.0f); // 覆盖位置断言
        expectTrue("槽位 0 颜色 = 新粒子蓝色", particles.particles()[0].b == 255 && particles.particles()[0].r == 0); // 覆盖颜色断言
        expectNear("槽位 1 未被覆盖：x 仍 = 1", particles.particles()[1].x, 1.0f); // 其余保留断言
        expectTrue("槽位 1 颜色 = 旧粒子红色", particles.particles()[1].r == 255 && particles.particles()[1].b == 0); // 其余颜色断言
    }
    endCase(1, p0, f0); // 用例 1 汇总

    // ------------------------------------------------------------------
    // CASE 2 寿命淡出与物理推进（重力 / 阻尼）
    // ------------------------------------------------------------------
    beginCase(2, "寿命淡出与物理推进（重力 / 阻尼）");
    p0 = gPass; // 本用例起始通过数
    f0 = gFail; // 本用例起始失败数
    {
        ParticleSystem particles; // 被测对象

        // 子用例 2a：寿命 0.1s 的粒子推进 0.05s 后剩余一半、alpha 比例 0.5
        particles.clear(); // 从空池开始
        const ParticleParams lifeParams = makeStaticParams(0.1f, SDL_Color{ 255, 255, 255, 255 }); // 寿命 0.1s
        particles.emitBurst(0.0f, 0.0f, 1, lifeParams); // 发射 1 粒
        expectNear("发射后剩余寿命 = 0.1", particles.particles()[0].life, 0.1f); // 初始寿命断言
        expectNear("发射后 maxLife = 0.1", particles.particles()[0].maxLife, 0.1f); // 淡出分母断言
        particles.update(0.05f); // 推进一半寿命
        expectNear("推进 0.05s 后剩余寿命 = 0.05", particles.particles()[0].life, 0.05f); // 寿命递减断言
        expectNear("淡出比例 life/maxLife = 0.5", particles.particles()[0].life / particles.particles()[0].maxLife, 0.5f); // 淡出比例断言
        expectTrue("推进一半后粒子仍活跃", particles.aliveCount() == 1); // 未到期断言
        particles.update(0.06f); // 再推进超过剩余寿命
        expectTrue("寿命归零后槽位释放（活跃数 0）", particles.aliveCount() == 0); // 到期释放断言

        // 子用例 2b：无阻尼 + 重力 100：0.1s 后 vy = 10、x = 10（先更新速度再积分）
        particles.clear(); // 清空回到空池
        const ParticleParams gravityParams = makeMotionParams(100.0f, 0.0f); // 重力 100/无阻尼
        particles.emitBurst(0.0f, 0.0f, 1, gravityParams); // 朝 +x 发射
        particles.update(0.1f); // 推进 0.1s
        expectNear("重力：vy = 100 x 0.1 = 10", particles.particles()[0].vy, 10.0f, 0.01f); // 重力断言
        expectNear("位移：x = 100 x 0.1 = 10", particles.particles()[0].x, 10.0f, 0.01f); // 位移断言
        expectNear("无阻尼：vx 保持 100", particles.particles()[0].vx, 100.0f, 0.01f); // 无阻尼断言

        // 子用例 2c：阻尼 2/秒：0.5s 后 vx 衰减为 100 x exp(-1) ≈ 36.79
        particles.clear(); // 清空回到空池
        const ParticleParams dampedParams = makeMotionParams(0.0f, 2.0f); // 无重力/阻尼 2
        particles.emitBurst(0.0f, 0.0f, 1, dampedParams); // 朝 +x 发射
        particles.update(0.5f); // 推进 0.5s
        expectNear("阻尼：vx = 100 x exp(-1) ≈ 36.79", particles.particles()[0].vx, 100.0f * std::exp(-1.0f), 0.02f); // 阻尼断言
    }
    endCase(2, p0, f0); // 用例 2 汇总

    // ------------------------------------------------------------------
    // CASE 3 clear（全部释放 + 游标归零）
    // ------------------------------------------------------------------
    beginCase(3, "clear 清空（全部释放 + 游标归零）");
    p0 = gPass; // 本用例起始通过数
    f0 = gFail; // 本用例起始失败数
    {
        ParticleSystem particles; // 被测对象
        const ParticleParams params = makeStaticParams(100.0f, SDL_Color{ 10, 20, 30, 255 }); // 长寿命粒子
        particles.emitBurst(5.0f, 5.0f, 10, params); // 发射 10 粒
        expectTrue("清空前活跃数 = 10", particles.aliveCount() == 10); // 前置断言
        particles.clear(); // 清空
        expectTrue("清空后活跃数 = 0", particles.aliveCount() == 0); // 清空断言
        particles.emitBurst(7.0f, 7.0f, 1, params); // 清空后重新发射（应从槽位 0 起）
        expectNear("清空后首粒写入槽位 0（x = 7）", particles.particles()[0].x, 7.0f); // 游标归零断言
    }
    endCase(3, p0, f0); // 用例 3 汇总

    // ------------------------------------------------------------------
    // CASE 4 震屏：衰减 / 叠加 / 清零
    // ------------------------------------------------------------------
    beginCase(4, "震屏：衰减 / 叠加 / 清零");
    p0 = gPass; // 本用例起始通过数
    f0 = gFail; // 本用例起始失败数
    {
        Camera camera; // 被测相机（震屏无需视口/世界尺寸）
        expectTrue("初始未震屏", !camera.isShaking()); // 初始状态断言
        expectTrue("初始偏移为零", shakeLength(camera) == 0.0f); // 零偏移断言

        // 登记 6px/0.15s：初始偏移长度恰为 6（单位圆方向，相位 0 时为 (0,6)）
        camera.addShake(6.0f, 0.15f); // 登记震屏
        expectTrue("登记后处于震屏中", camera.isShaking()); // 状态断言
        expectNear("初始偏移长度 = 6", shakeLength(camera), 6.0f); // 最大振幅断言

        // 推进一半时长（0.075s）：线性衰减到 3px
        camera.update(0.075f); // 推进 0.075s
        expectNear("推进一半后偏移长度 = 3（线性衰减）", shakeLength(camera), 3.0f, 0.02f); // 衰减断言

        // 再推进 0.076s（超过剩余 0.075s）：到期归零
        camera.update(0.076f); // 推进到期
        expectTrue("到期后不再震屏", !camera.isShaking()); // 结束状态断言
        expectTrue("到期后偏移为零", shakeLength(camera) == 0.0f); // 归零断言

        // 叠加语义：6px 震屏进行中登记 2px 弱震 -> 弱震被忽略（当前振幅仍 6）
        camera.addShake(6.0f, 0.2f); // 强震
        camera.update(0.05f); // 消耗 0.05s（剩余 0.15/0.20 -> 当前振幅 4.5）
        camera.addShake(2.0f, 0.5f); // 弱震：应被忽略
        expectNear("弱震不覆盖强震：当前振幅仍 4.5", shakeLength(camera), 4.5f, 0.02f); // 叠加语义断言

        // 更强（同强）震屏覆盖并重新计时：2px -> 6px 应直接回到 6px
        camera.addShake(6.0f, 0.15f); // 同强覆盖（重新计时）
        expectNear("同强震屏覆盖并重新计时：偏移回到 6", shakeLength(camera), 6.0f, 0.001f); // 覆盖断言

        // clearShake：立即归零（暂停/重开的清抖入口）
        camera.clearShake(); // 立即清零
        expectTrue("clearShake 后不再震屏", !camera.isShaking()); // 清零状态断言
        expectTrue("clearShake 后偏移为零", shakeLength(camera) == 0.0f); // 清零偏移断言

        // 无效参数：非正振幅/时长不进入震屏
        camera.addShake(0.0f, 0.2f); // 零振幅
        camera.addShake(4.0f, 0.0f); // 零时长
        expectTrue("非正参数被忽略（仍未震屏）", !camera.isShaking()); // 入口防御断言
    }
    endCase(4, p0, f0); // 用例 4 汇总

    // ------------------------------------------------------------------
    // CASE 5 震屏与坐标换算：渲染抖动、输入稳定
    // ------------------------------------------------------------------
    beginCase(5, "震屏只扰渲染：worldToScreen 抖动 / screenToWorld 稳定");
    p0 = gPass; // 本用例起始通过数
    f0 = gFail; // 本用例起始失败数
    {
        Camera camera; // 被测相机
        camera.setViewport(100.0f, 100.0f); // 视口 100x100
        camera.setWorldSize(1000.0f, 1000.0f); // 世界 1000x1000
        camera.snapTo(500.0f, 500.0f); // 相机中心对准 (500,500) -> 左上角 (450,450)

        // 无震屏：worldToScreen / screenToWorld 精确互逆
        const Vec2 screenBefore = camera.worldToScreen(500.0f, 500.0f); // 世界中心 -> 屏幕
        expectNear("无震屏：世界中心映射到屏幕 (50,50).x", screenBefore.x, 50.0f); // 平移断言 x
        expectNear("无震屏：世界中心映射到屏幕 (50,50).y", screenBefore.y, 50.0f); // 平移断言 y
        const Vec2 worldRoundTrip = camera.screenToWorld(screenBefore.x, screenBefore.y); // 往返换算
        expectNear("无震屏：往返还原世界 x", worldRoundTrip.x, 500.0f); // 互逆断言 x
        expectNear("无震屏：往返还原世界 y", worldRoundTrip.y, 500.0f); // 互逆断言 y

        // 震屏期间：worldToScreen 相对无震屏位置偏移 = shakeOffset
        camera.addShake(6.0f, 1.0f); // 长震屏（本用例不推进，保持满振幅）
        const Vec2 offset = camera.shakeOffset(); // 当前抖动偏移
        const Vec2 screenShaken = camera.worldToScreen(500.0f, 500.0f); // 震屏下的屏幕位置
        expectNear("震屏：worldToScreen 横移 = shakeOffset.x", screenShaken.x - screenBefore.x, offset.x); // 渲染抖动断言 x
        expectNear("震屏：worldToScreen 纵移 = shakeOffset.y", screenShaken.y - screenBefore.y, offset.y); // 渲染抖动断言 y

        // 输入稳定：screenToWorld 不受震屏影响（瞄准坐标不抖）
        const Vec2 worldInput = camera.screenToWorld(50.0f, 50.0f); // 输入换算（震屏中）
        expectNear("震屏：screenToWorld 仍还原 (500,500).x（瞄准稳定）", worldInput.x, 500.0f); // 输入稳定断言 x
        expectNear("震屏：screenToWorld 仍还原 (500,500).y（瞄准稳定）", worldInput.y, 500.0f); // 输入稳定断言 y
    }
    endCase(5, p0, f0); // 用例 5 汇总

    // ------------------------------------------------------------------
    // 汇总
    // ------------------------------------------------------------------
    const int total = gPass + gFail; // 断言总数
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 条断言，通过 %d，失败 %d\n", total, gPass, gFail);
    std::printf("==============================================================\n");
    return gFail; // 退出码 = 失败数（0 = 全过）
}
