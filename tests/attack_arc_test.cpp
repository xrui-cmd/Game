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
