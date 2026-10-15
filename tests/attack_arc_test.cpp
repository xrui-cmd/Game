// ============================================================================
//  attack_arc_test.cpp — 横扫弧形判定（AttackArc）单元测试
//
//  被测对象 : Src/inc/game/AttackArc.h（game 层纯逻辑头文件，header-only）
//  覆盖场景 : ① 弧内命中（中轴/两侧/近边界/圆心点/未归一化中轴）
//             ② 弧外不命中（背后/垂直/超半径/越界角度）
//             ③ 角度边界——以「两怪夹角」视角验证 119° 与 121° 的行为：
//                总张角 120°（半张角 60°）下，±59.5°（夹角 119°）两只都
//                命中；±60.5°（夹角 121°）两只都落空；恰好 ±60° 按
//                「≤ 半张角」含边界处理（容差把理论边界稳定判为命中）
//             ④ 半径边界（恰好 96 命中 / 96.1 落空 / 斜向 3-4-5 边界）
//             ⑤ 零向量与退化防御（零向量中轴不命中、圆心点命中、
//                半径 <=0 不命中、张角归一化 -30°/400°/360°/0°）
//             ⑥ 扫程缓出曲线（手感快修：端点 0->0 / 1->1、采样值、
//                单调性、前快后慢、越界夹取与 NaN 防御）
//             ⑦ 刀锋角映射（首角 −半张角 / 末角 +半张角 / 缓出中点、
//                张角归一化、跨 π 的连续值、NaN 进度防御）
//             ⑧ 扫过角度区间（普通区间/±π 环绕/跨 0/负角/零宽区间、
//                含边界与容差、外点不命中）
//
//  与其余测试的差异 : 被测对象是无任何运行时依赖的纯头文件（只用到
//  引擎 math 层的 Vec2 与标准库），不链接 SDL3、不需要窗口/音频/文件，
//  任何机器上都能确定性运行（构建接线见 tests/CMakeLists.txt）
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/AttackArc.h" // 被测函数 isPointInAttackArc（内含 engine/math/Vec2.h）

#include <cmath>   // std::cos / std::sin：按角度生成判定点
#include <cstdio>  // std::printf
#include <cstdlib> // std::system（Windows 控制台切 UTF-8）

namespace
{

int gPass = 0; // 通过的断言数
int gFail = 0; // 失败的断言数（作为 main 返回值）

// 断言：布尔条件成立
void expectTrue(const char* label, bool value)
{
    // 条件为 true 才算通过
    const bool ok = value;
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
}

// 断言：浮点近似相等（absolute 容差；默认 1e-5，远小于任何被测间隔）
void expectNear(const char* label, float actual, float expected, float eps = 1e-5f)
{
    // 差值在容差内才算通过（abs 避免方向差异误判）
    const bool ok = std::fabs(actual - expected) <= eps;
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：失败时附带期望/实得数值，便于定位
    std::printf("  [%s] %s（期望 %.6f 实得 %.6f）\n",
                ok ? "PASS" : "FAIL", label, expected, actual);
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

// 从「相对中轴的角度（度）+ 距离（像素）」生成判定点（极坐标 -> 直角坐标）。
// 测试里用它构造精确的角度边界样本；中轴固定为 +x 方向（0° = 正右）
Vec2 pointAtDegrees(float degrees, float distancePx)
{
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f; // π/180
    const float rad = degrees * kDegToRad;                        // 换算为弧度
    return Vec2{ std::cos(rad) * distancePx,                      // x = cosθ x 距离
                 std::sin(rad) * distancePx };                    // y = sinθ x 距离
}

// 度数 -> 弧度：角度类断言（扫程区间/刀锋角）统一用它换算，避免重复写 π/180
float toRad(float degrees)
{
    // 直接复用被测头文件导出的换算常量（与实现同一精度口径）
    return degrees * kAttackArcDegreesToRadians;
}

// 测试统一基准：圆心 (0,0)、中轴朝右、半径 96、总张角 120（半张角 60°）
constexpr float kRadius = 96.0f;  // 半径（与游戏 attack_range 默认一致）
constexpr float kArc = 120.0f;    // 总张角（与游戏 attack_arc 默认一致）

} // namespace

int main()
{
#if defined(_WIN32)
    // 控制台代码页切到 UTF-8：中文输出不乱码（失败也不影响测试）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" AttackArc 横扫扇形判定单元测试 —— Src/inc/game/AttackArc.h\n");
    std::printf(" 基准: 圆心(0,0) 中轴(1,0) 半径96 总张角120 度（半张角60）\n");
    std::printf("==============================================================\n");

    // 常用常量：原点与右向中轴（多数用例共用）
    const Vec2 origin{ 0.0f, 0.0f };   // 圆心 = 玩家中心
    const Vec2 aimRight{ 1.0f, 0.0f }; // 中轴 = 朝右

    // ------------------------------------------------------------------
    // CASE 1 弧内命中：中轴 / 两侧 / 近边界 / 圆心点 / 未归一化中轴
    // ------------------------------------------------------------------
    beginCase(1, "弧内 -> 中轴与两侧的判定点全部命中");
    int p0 = gPass, f0 = gFail;
    {
        // 正前方中轴上的点：45 像素 < 半径 96
        expectTrue("中轴 (45,0) 命中", isPointInAttackArc(origin, aimRight, Vec2{ 45.0f, 0.0f }, kRadius, kArc));

        // 两侧对称点（±45°、距 50）：都在半张角 60° 内
        expectTrue("+45 度 (50px) 命中", isPointInAttackArc(origin, aimRight, pointAtDegrees(45.0f, 50.0f), kRadius, kArc));
        expectTrue("-45 度 (50px) 命中", isPointInAttackArc(origin, aimRight, pointAtDegrees(-45.0f, 50.0f), kRadius, kArc));

        // 贴近边界的内侧（59°、距 90）：角度 < 60 且距离 < 96
        expectTrue("+59 度 (90px) 命中", isPointInAttackArc(origin, aimRight, pointAtDegrees(59.0f, 90.0f), kRadius, kArc));
        expectTrue("-59 度 (90px) 命中", isPointInAttackArc(origin, aimRight, pointAtDegrees(-59.0f, 90.0f), kRadius, kArc));

        // 恰好落在圆心上的点：方向无定义，约定视为命中（距离 0 ≤ 半径）
        expectTrue("圆心点 (0,0) 命中", isPointInAttackArc(origin, aimRight, origin, kRadius, kArc));

        // 中轴未归一化（长度 5 而非 1）：内部按点积/模长换算夹角，结果一致
        const Vec2 longAim{ 5.0f, 0.0f }; // 同方向的非单位向量
        expectTrue("未归一化中轴 (5,0) 命中", isPointInAttackArc(origin, longAim, Vec2{ 45.0f, 0.0f }, kRadius, kArc));

        // 圆心不在原点的平移场景：相对几何一致，命中
        const Vec2 shiftedOrigin{ 100.0f, 200.0f }; // 平移后的圆心
        expectTrue("平移圆心命中", isPointInAttackArc(shiftedOrigin, aimRight, Vec2{ 145.0f, 200.0f }, kRadius, kArc));
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 弧外不命中：背后 / 垂直 / 近边界外侧 / 超半径
    // ------------------------------------------------------------------
    beginCase(2, "弧外 -> 背后与垂直方向全部不命中");
    p0 = gPass; f0 = gFail;
    {
        // 正后方（180°）：弧外
        expectTrue("背后 (-50,0) 不命中", !isPointInAttackArc(origin, aimRight, Vec2{ -50.0f, 0.0f }, kRadius, kArc));

        // 垂直方向（±90°）：超出半张角 60°
        expectTrue("垂直 +90 度 不命中", !isPointInAttackArc(origin, aimRight, pointAtDegrees(90.0f, 50.0f), kRadius, kArc));
        expectTrue("垂直 -90 度 不命中", !isPointInAttackArc(origin, aimRight, pointAtDegrees(-90.0f, 50.0f), kRadius, kArc));

        // 边界外侧（±61°）：角度超过半张角 1 度
        expectTrue("+61 度 不命中", !isPointInAttackArc(origin, aimRight, pointAtDegrees(61.0f, 50.0f), kRadius, kArc));
        expectTrue("-61 度 不命中", !isPointInAttackArc(origin, aimRight, pointAtDegrees(-61.0f, 50.0f), kRadius, kArc));

        // 角度在弧内但距离超半径（150 > 96）：不命中
        expectTrue("中轴上超半径 (150,0) 不命中", !isPointInAttackArc(origin, aimRight, Vec2{ 150.0f, 0.0f }, kRadius, kArc));
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 角度边界：两怪夹角 119° 全中 / 121° 全空 / 恰好 60° 含边界
    // ------------------------------------------------------------------
    beginCase(3, "角度边界 -> 119 度全中、121 度全空、恰好边界含入");
    p0 = gPass; f0 = gFail;
    {
        // 两只怪对称分布在中轴两侧 ±59.5°（彼此夹角 119°）：都在 120° 弧内
        const Vec2 upper119 = pointAtDegrees(59.5f, 80.0f);  // 上侧怪
        const Vec2 lower119 = pointAtDegrees(-59.5f, 80.0f); // 下侧怪
        expectTrue("夹角 119 度-上侧命中", isPointInAttackArc(origin, aimRight, upper119, kRadius, kArc));
        expectTrue("夹角 119 度-下侧命中", isPointInAttackArc(origin, aimRight, lower119, kRadius, kArc));

        // 两只怪对称分布在中轴两侧 ±60.5°（彼此夹角 121°）：都超出 120° 弧
        const Vec2 upper121 = pointAtDegrees(60.5f, 80.0f);  // 上侧怪（越界）
        const Vec2 lower121 = pointAtDegrees(-60.5f, 80.0f); // 下侧怪（越界）
        expectTrue("夹角 121 度-上侧不命中", !isPointInAttackArc(origin, aimRight, upper121, kRadius, kArc));
        expectTrue("夹角 121 度-下侧不命中", !isPointInAttackArc(origin, aimRight, lower121, kRadius, kArc));

        // 恰好压在半张角上（±60°）：按「≤ 半张角」含边界——容差把
        // 理论边界点的浮点舍入稳定拉回命中侧
        expectTrue("恰好 +60 度命中（含边界）", isPointInAttackArc(origin, aimRight, pointAtDegrees(60.0f, 80.0f), kRadius, kArc));
        expectTrue("恰好 -60 度命中（含边界）", isPointInAttackArc(origin, aimRight, pointAtDegrees(-60.0f, 80.0f), kRadius, kArc));

        // 换一个张角（90 度 -> 半张角 45°）：边界行为随参数正确收缩
        expectTrue("弧90:+44.9 度命中", isPointInAttackArc(origin, aimRight, pointAtDegrees(44.9f, 50.0f), kRadius, 90.0f));
        expectTrue("弧90:恰好 +45 度命中", isPointInAttackArc(origin, aimRight, pointAtDegrees(45.0f, 50.0f), kRadius, 90.0f));
        expectTrue("弧90:+45.1 度不命中", !isPointInAttackArc(origin, aimRight, pointAtDegrees(45.1f, 50.0f), kRadius, 90.0f));
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 半径边界：恰好 96 命中 / 96.1 落空 / 斜向 3-4-5 边界
    // ------------------------------------------------------------------
    beginCase(4, "半径边界 -> 恰好 96 命中、超出 0.1 落空");
    p0 = gPass; f0 = gFail;
    {
        // 中轴方向恰好一个半径长的点（96 == 96）：含边界，命中
        expectTrue("恰好半径 (96,0) 命中", isPointInAttackArc(origin, aimRight, Vec2{ 96.0f, 0.0f }, kRadius, kArc));
        // 恰好半径 + 0.1：落空
        expectTrue("半径+0.1 (96.1,0) 不命中", !isPointInAttackArc(origin, aimRight, Vec2{ 96.1f, 0.0f }, kRadius, kArc));

        // 斜向边界（30°、距 96）：斜距恰好等于半径，命中
        expectTrue("30 度恰好半径命中", isPointInAttackArc(origin, aimRight, pointAtDegrees(30.0f, 96.0f), kRadius, kArc));
        // 斜向边界 + 0.5 像素：落空
        expectTrue("30 度半径+0.5 不命中", !isPointInAttackArc(origin, aimRight, pointAtDegrees(30.0f, 96.5f), kRadius, kArc));

        // 3-4-5 整数三角验证（距离恰 100 的整数点）：自定义半径 100
        expectTrue("(60,80) 距 100 命中", isPointInAttackArc(origin, aimRight, Vec2{ 60.0f, 80.0f }, 100.0f, kArc));
        expectTrue("(60.3,80.4) 距约 100.5 不命中", !isPointInAttackArc(origin, aimRight, Vec2{ 60.3f, 80.4f }, 100.0f, kArc));

        // 角度在弧内（0°）但距离恰超半径：不命中（半径优先于角度短路）
        expectTrue("角度内距离外 (120,0) 不命中", !isPointInAttackArc(origin, aimRight, Vec2{ 120.0f, 0.0f }, kRadius, kArc));
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 零向量与退化防御：中轴零向量 / 圆心点 / 半径非正 / 张角归一化
    // ------------------------------------------------------------------
    beginCase(5, "零向量与退化防御 -> 不崩、行为可预期");
    p0 = gPass; f0 = gFail;
    {
        // 中轴为零向量：没有方向，任何点都不命中（含看似位于正前方的点）
        const Vec2 zeroAim{ 0.0f, 0.0f }; // 零向量中轴
        expectTrue("零向量中轴-正前方不命中", !isPointInAttackArc(origin, zeroAim, Vec2{ 50.0f, 0.0f }, kRadius, kArc));
        expectTrue("零向量中轴-圆心点也不命中", !isPointInAttackArc(origin, zeroAim, origin, kRadius, kArc));

        // 半径非正：没有可命中的面积（含圆心点）
        expectTrue("半径 0 不命中", !isPointInAttackArc(origin, aimRight, Vec2{ 50.0f, 0.0f }, 0.0f, kArc));
        expectTrue("半径 0-圆心点不命中", !isPointInAttackArc(origin, aimRight, origin, 0.0f, kArc));
        expectTrue("半径 -10 不命中", !isPointInAttackArc(origin, aimRight, Vec2{ 50.0f, 0.0f }, -10.0f, kArc));

        // 张角归一化：
        // 360 度 = 整圆——背后与垂直方向的点都命中（距离内）
        expectTrue("张角 360-背后点命中", isPointInAttackArc(origin, aimRight, Vec2{ -50.0f, 0.0f }, kRadius, 360.0f));
        expectTrue("张角 360-垂直点命中", isPointInAttackArc(origin, aimRight, Vec2{ 0.0f, 50.0f }, kRadius, 360.0f));
        // 400 度按 360 处理：同上
        expectTrue("张角 400-背后点命中", isPointInAttackArc(origin, aimRight, Vec2{ -50.0f, 0.0f }, kRadius, 400.0f));
        // 0 度（半张角 0）：仅中轴线上（夹角 0）的点命中，垂直点不命中
        expectTrue("张角 0-中轴点命中", isPointInAttackArc(origin, aimRight, Vec2{ 50.0f, 0.0f }, kRadius, 0.0f));
        expectTrue("张角 0-垂直点不命中", !isPointInAttackArc(origin, aimRight, Vec2{ 0.0f, 50.0f }, kRadius, 0.0f));
        // -30 度按 0 处理：与张角 0 行为一致
        expectTrue("张角 -30-中轴点命中", isPointInAttackArc(origin, aimRight, Vec2{ 50.0f, 0.0f }, kRadius, -30.0f));
        expectTrue("张角 -30-垂直点不命中", !isPointInAttackArc(origin, aimRight, Vec2{ 0.0f, 50.0f }, kRadius, -30.0f));

        // 张角 360 但距离超半径：仍然不命中（半径是硬边界）
        expectTrue("张角 360-超半径不命中", !isPointInAttackArc(origin, aimRight, Vec2{ 150.0f, 0.0f }, kRadius, 360.0f));
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // CASE 6 扫程缓出曲线（手感快修）：端点/采样值/单调/前快后慢/越界防御
    // ------------------------------------------------------------------
    beginCase(6, "缓出曲线 -> 0->0、1->1、采样值、单调、前快后慢、越界夹取");
    p0 = gPass; f0 = gFail;
    {
        // 端点精确：0 处停在起点、1 处到达终点（曲线两端不漂移）
        expectNear("缓出(0) = 0", easeOutSwingProgress(0.0f), 0.0f);
        expectNear("缓出(1) = 1", easeOutSwingProgress(1.0f), 1.0f);

        // 采样值：二次缓出 1-(1-t)^2 的解析结果（手算校验曲线实现）
        expectNear("缓出(0.1) = 0.19", easeOutSwingProgress(0.1f), 0.19f);
        expectNear("缓出(0.25) = 0.4375", easeOutSwingProgress(0.25f), 0.4375f);
        expectNear("缓出(0.5) = 0.75（半程已扫 3/4）", easeOutSwingProgress(0.5f), 0.75f);
        expectNear("缓出(0.75) = 0.9375", easeOutSwingProgress(0.75f), 0.9375f);
        expectNear("缓出(0.9) = 0.99", easeOutSwingProgress(0.9f), 0.99f);

        // 单调不减：100 等分采样，后一点不小于前一点（浮点容差内）
        bool monotonic = true; // 单调性总判定
        float previous = easeOutSwingProgress(0.0f); // 前一点（从 0 起步）
        for (int i = 1; i <= 100; ++i)
        {
            const float current = easeOutSwingProgress(static_cast<float>(i) / 100.0f); // 当前采样点
            if (current < previous - 1e-6f)
            {
                monotonic = false; // 出现回退：记录后仍继续采完（只为完整遍历）
            }
            previous = current; // 推进前一点
        }
        expectTrue("缓出曲线单调不减（100 采样）", monotonic);

        // 前快后慢（曲线核心手感特征）：
        //   1) 前半段已完成超过一半扫程（t=0.5 时位置 > 0.5）；
        //   2) 前 10% 的位移大于最后 10% 的位移（起动快、收尾慢）
        expectTrue("前快：缓出(0.5) > 0.5", easeOutSwingProgress(0.5f) > 0.5f);
        const float earlySpeed = easeOutSwingProgress(0.1f) - easeOutSwingProgress(0.0f); // 前 10% 位移
        const float lateSpeed = easeOutSwingProgress(1.0f) - easeOutSwingProgress(0.9f);  // 后 10% 位移
        expectTrue("后慢：前 10% 位移 > 后 10% 位移", earlySpeed > lateSpeed);

        // 越界与 NaN 防御：负值/NaN 按 0、超 1 按 1（夹到端点，不越值域）
        expectNear("缓出(-0.5) = 0（负值夹到起点）", easeOutSwingProgress(-0.5f), 0.0f);
        expectNear("缓出(1.5) = 1（超过 1 夹到终点）", easeOutSwingProgress(1.5f), 1.0f);
        expectNear("缓出(NaN) = 0（NaN 按起点处理）",
                   easeOutSwingProgress(std::nanf("")), 0.0f);
    }
    endCase(6, p0, f0);

    // ------------------------------------------------------------------
    // CASE 7 刀锋角映射：首角/末角/缓出中点、张角归一化、跨 π、NaN
    // ------------------------------------------------------------------
    beginCase(7, "刀锋角 -> 首末角映射、缓出插值、张角归一化、跨 π 连续");
    p0 = gPass; f0 = gFail;
    {
        // 基准：瞄准角 0°、张角 120°（半张角 60°）——刀锋首角 −60°、末角 +60°
        expectNear("首角 = 瞄准 − 半张角（-π/3）", bladeAngleAt(0.0f, 0.0f, 120.0f), -1.0471976f, 1e-4f);
        expectNear("末角 = 瞄准 + 半张角（+π/3）", bladeAngleAt(0.0f, 1.0f, 120.0f), 1.0471976f, 1e-4f);

        // 中点：progress=0.5 时缓出位置 0.75 —— 已越过后半段，接近末角
        //（0 → +60° 的 75% 处 = +30°，即 π/6）
        expectNear("中点 = -π/3 + 120° x 0.75 = +30°", bladeAngleAt(0.0f, 0.5f, 120.0f), 0.5235988f, 1e-4f);

        // 缓出同步：progress=0.25 对应缓出位置 0.4375：-60° + 120° x 0.4375 = -7.5°
        expectNear("progress 0.25 -> -7.5°（与缓出曲线同步）",
                   bladeAngleAt(0.0f, 0.25f, 120.0f), toRad(-7.5f), 1e-4f);

        // 张角参数化：90° 张角 → 首末角 ±45°；0° → 恒为中轴角
        expectNear("弧 90：首角 -45°", bladeAngleAt(0.0f, 0.0f, 90.0f), toRad(-45.0f), 1e-4f);
        expectNear("弧 90：末角 +45°", bladeAngleAt(0.0f, 1.0f, 90.0f), toRad(45.0f), 1e-4f);
        expectNear("弧 0：恒为中轴角", bladeAngleAt(0.0f, 0.5f, 0.0f), 0.0f);
        expectNear("弧 -30（按 0）：恒为中轴角", bladeAngleAt(0.0f, 1.0f, -30.0f), 0.0f);

        // 张角超整圈按 360 归一化：末角 = 瞄准 + 180°
        expectNear("弧 400（按 360）：末角 = 瞄准 + 180°", bladeAngleAt(0.0f, 1.0f, 400.0f), 3.1415927f, 1e-4f);

        // 跨 π 的正定性：瞄准 170°、弧 120 时首角 110°、末角 230°——
        // 返回值刻意不做 ±π 归一化（230° 不折叠成 -130°），供「上一角 ->
        // 本角」的区间判定直接相减使用
        expectNear("aim 170°：首角 = 110°", bladeAngleAt(toRad(170.0f), 0.0f, 120.0f), toRad(110.0f), 1e-4f);
        expectNear("aim 170°：末角 = 230°（不折叠，保持连续）", bladeAngleAt(toRad(170.0f), 1.0f, 120.0f), toRad(230.0f), 1e-4f);

        // NaN 进度防御：按「仍在起点」处理（缓出函数把 NaN 压到 0）
        expectNear("progress NaN -> 停在首角", bladeAngleAt(0.0f, std::nanf(""), 120.0f), -1.0471976f, 1e-4f);
    }
    endCase(7, p0, f0);

    // ------------------------------------------------------------------
    // CASE 8 扫过角度区间：普通/±π 环绕/跨 0/负角/零宽、含边界与容差
    // ------------------------------------------------------------------
    beginCase(8, "角度区间 -> 正向区间判定、环绕与跨 0、含边界、窄区间");
    p0 = gPass; f0 = gFail;
    {
        // 普通区间 [10°, 50°]：内部命中、两端含边界、外部不命中
        expectTrue("[10,50] 内 30° 命中", isAngleInSweep(toRad(30.0f), toRad(10.0f), toRad(50.0f)));
        expectTrue("[10,50] 起点 10° 命中（含边界）", isAngleInSweep(toRad(10.0f), toRad(10.0f), toRad(50.0f)));
        expectTrue("[10,50] 终点 50° 命中（含边界）", isAngleInSweep(toRad(50.0f), toRad(10.0f), toRad(50.0f)));
        expectTrue("[10,50] 0° 不命中（起点之前）", !isAngleInSweep(toRad(0.0f), toRad(10.0f), toRad(50.0f)));
        expectTrue("[10,50] 60° 不命中（终点之后）", !isAngleInSweep(toRad(60.0f), toRad(10.0f), toRad(50.0f)));
        // 贴近边界的浮点夹缝：内侧 0.01° 命中、外侧 0.01° 落空
        expectTrue("[10,50] 49.99° 命中", isAngleInSweep(toRad(49.99f), toRad(10.0f), toRad(50.0f)));
        expectTrue("[10,50] 50.01° 不命中", !isAngleInSweep(toRad(50.01f), toRad(10.0f), toRad(50.0f)));
        expectTrue("[10,50] 9.99° 不命中", !isAngleInSweep(toRad(9.99f), toRad(10.0f), toRad(50.0f)));

        // ±π 环绕：从 170° 扫到 190°（数值上跨过 180°，即 -170° 一侧）
        expectTrue("[170,190] 180° 命中（跨 π 中部）", isAngleInSweep(toRad(180.0f), toRad(170.0f), toRad(190.0f)));
        expectTrue("[170,190] -175°（=185°）命中", isAngleInSweep(toRad(-175.0f), toRad(170.0f), toRad(190.0f)));
        expectTrue("[170,190] -170°（=190°）命中（终点边界负值表达）",
                   isAngleInSweep(toRad(-170.0f), toRad(170.0f), toRad(190.0f)));
        expectTrue("[170,190] 165° 不命中（起点之前）", !isAngleInSweep(toRad(165.0f), toRad(170.0f), toRad(190.0f)));
        expectTrue("[170,190] -160°（=200°）不命中（终点之后）",
                   !isAngleInSweep(toRad(-160.0f), toRad(170.0f), toRad(190.0f)));

        // 跨 0（±π 另一侧）：从 350°（=-10°）扫到 370°（=+10°）
        expectTrue("[350,370] 0° 命中（跨 0 中部）", isAngleInSweep(toRad(0.0f), toRad(350.0f), toRad(370.0f)));
        expectTrue("[350,370] -10°（=350°）命中（起点边界负值表达）",
                   isAngleInSweep(toRad(-10.0f), toRad(350.0f), toRad(370.0f)));
        expectTrue("[350,370] 345° 不命中（起点之前）", !isAngleInSweep(toRad(345.0f), toRad(350.0f), toRad(370.0f)));
        expectTrue("[350,370] 15° 不命中（终点之后）", !isAngleInSweep(toRad(15.0f), toRad(350.0f), toRad(370.0f)));

        // 负角区间 [-30°, +30°]：横跨 0 的对称区间
        expectTrue("[-30,30] 0° 命中", isAngleInSweep(toRad(0.0f), toRad(-30.0f), toRad(30.0f)));
        expectTrue("[-30,30] 30° 命中（终点边界）", isAngleInSweep(toRad(30.0f), toRad(-30.0f), toRad(30.0f)));
        expectTrue("[-30,30] -30° 命中（起点边界）", isAngleInSweep(toRad(-30.0f), toRad(-30.0f), toRad(30.0f)));
        expectTrue("[-30,30] -45° 不命中", !isAngleInSweep(toRad(-45.0f), toRad(-30.0f), toRad(30.0f)));

        // 零宽区间（from == to，如扫程刚开始的一步）：只有恰好压在该角上命中
        expectTrue("[30,30] 30° 命中（零宽含边界）", isAngleInSweep(toRad(30.0f), toRad(30.0f), toRad(30.0f)));
        expectTrue("[30,30] 30.002° 命中（容差内）", isAngleInSweep(toRad(30.002f), toRad(30.0f), toRad(30.0f)));
        expectTrue("[30,30] 31° 不命中", !isAngleInSweep(toRad(31.0f), toRad(30.0f), toRad(30.0f)));
        expectTrue("[30,30] 29° 不命中（正向语义：未到即不算）",
                   !isAngleInSweep(toRad(29.0f), toRad(30.0f), toRad(30.0f)));
    }
    endCase(8, p0, f0);

    // ------------------------------------------------------------------
    const int total = gPass + gFail;
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 条断言 | 通过 %d | 失败 %d\n", total, gPass, gFail);
    std::printf(" 结论: %s\n", (gFail == 0)
        ? "全部通过 —— 扇形判定的弧内/弧外/边界/防御行为符合预期。"
        : "存在失败用例，请检查上方 [FAIL] 行。");
    std::printf(" main 返回值（失败用例数）: %d\n", gFail);
    std::printf("==============================================================\n");

    return gFail; // 失败数作为退出码：全部通过时为 0
}
