// ============================================================================
//  rect_collision_test.cpp — AABB（轴对齐包围盒）碰撞判定单元测试
//
//  被测函数 : libs/GameEngine/include/engine/Rect.h :: rectsOverlap(const SDL_FRect&, const SDL_FRect&)
//  项目约定 : 矩形边界"刚好贴合"不算碰撞（edge touch -> false）
//
//  ── 编译与运行（推荐：VS 开发者命令行 / vcvars64 环境） ──────────────────
//    cd /d D:\GameProjcet\GameDev
//    cl /nologo /std:c++20 /utf-8 /EHsc /Fo:tests\ /I vendor\SDL-main\include tests\rect_collision_test.cpp /Fe:tests\rect_test.exe
//    tests\rect_test.exe
//
//  说明：本测试只使用头文件（SDL_FRect 结构体 + Rect.h 内联函数），
//        不调用任何 SDL 运行时函数，因此编译时无需链接 SDL3 库。
//        /Fo:tests\ 让编译中间文件也落在 tests\ 目录下，不污染项目根。
//
//  ── 一键脚本 ──────────────────────────────────────────────────────────
//    直接运行（或双击）tests\build_and_run.bat 可自动完成上述步骤。
//
//  ── 备选方案（若 cl 直编失败，用独立 CMake 工程） ─────────────────────
//    cmake -S tests -B tests/build
//    cmake --build tests/build
//    tests\bin\rect_test.exe
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include <SDL3/SDL_rect.h>                             // SDL_FRect
#include "../libs/GameEngine/include/engine/Rect.h"    // 被测函数 rectsOverlap（独立引擎仓库，子模块接入）

#include <cstdio>
#include <cstdlib>
#include <random>

namespace
{

int gPass = 0;  // 通过的用例数
int gFail = 0;  // 失败的用例数（作为 main 返回值）

const char* bstr(bool value)
{
    return value ? "true" : "false";
}

SDL_FRect R(float x, float y, float w, float h)
{
    return SDL_FRect{ x, y, w, h };
}

// 输出一条"期望值 vs 实际值"用例结果
void expectOverlap(const char* label, const SDL_FRect& a, const SDL_FRect& b, bool expect)
{
    const bool got = rectsOverlap(a, b);
    const bool ok = (got == expect);
    if (ok) { ++gPass; } else { ++gFail; }

    std::printf("  [%s] %s | a=(%.0f,%.0f,%.0f,%.0f) b=(%.0f,%.0f,%.0f,%.0f) | 期望=%s 实际=%s\n",
                ok ? "PASS" : "FAIL", label,
                a.x, a.y, a.w, a.h, b.x, b.y, b.w, b.h,
                bstr(expect), bstr(got));
}

// 对称性用例：rectsOverlap(a,b) 必须与 rectsOverlap(b,a) 一致
void expectSymmetry(const char* label, const SDL_FRect& a, const SDL_FRect& b)
{
    const bool ab = rectsOverlap(a, b);
    const bool ba = rectsOverlap(b, a);
    const bool ok = (ab == ba);
    if (ok) { ++gPass; } else { ++gFail; }

    std::printf("  [%s] %s | rectsOverlap(a,b)=%s rectsOverlap(b,a)=%s\n",
                ok ? "PASS" : "FAIL", label, bstr(ab), bstr(ba));
}

void beginCase(int idx, const char* title)
{
    std::printf("\n[CASE %d] %s\n", idx, title);
}

void endCase(int idx, int passBefore, int failBefore)
{
    const int passed = gPass - passBefore;
    const int failed = gFail - failBefore;
    std::printf("  ---- CASE %d 小结: %d/%d 通过%s\n", idx, passed, passed + failed,
                failed ? "  <<<< 存在失败" : "");
}

} // namespace

int main()
{
#if defined(_WIN32)
    // 把控制台代码页切到 UTF-8，保证中文输出不乱码（此调用失败也不影响测试本身）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" AABB 碰撞判定单元测试 —— rectsOverlap（libs/GameEngine/include/engine/Rect.h）\n");
    std::printf(" 判定约定: 边界刚好贴合不算碰撞（touch -> false）\n");
    std::printf(" 随机性质测试: 10000 组，每值范围 0.00~200.00，固定种子 20261012\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 完全分离（左、右、上、下、斜对角）
    // ------------------------------------------------------------------
    beginCase(1, "完全分离 -> 期望 false");
    int p0 = gPass, f0 = gFail;
    expectOverlap("A 在 B 左侧（X 轴分离）",     R(0, 0, 10, 10),  R(40, 0, 10, 10),  false);
    expectOverlap("A 在 B 右侧（X 轴分离）",     R(40, 0, 10, 10), R(0, 0, 10, 10),   false);
    expectOverlap("A 在 B 上方（Y 轴分离）",     R(0, 0, 10, 10),  R(0, 40, 10, 10),  false);
    expectOverlap("A 在 B 下方（Y 轴分离）",     R(0, 40, 10, 10), R(0, 0, 10, 10),   false);
    expectOverlap("斜对角完全分离（两轴均分离）", R(0, 0, 10, 10),  R(40, 40, 10, 10), false);
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 部分重叠
    // ------------------------------------------------------------------
    beginCase(2, "部分重叠 -> 期望 true");
    p0 = gPass; f0 = gFail;
    expectOverlap("角部部分重叠",                R(0, 0, 10, 10),  R(5, 5, 10, 10),  true);
    expectOverlap("水平方向边缘重叠 1 个单位",    R(0, 0, 10, 10),  R(9, 0, 10, 10),  true);
    expectOverlap("垂直方向边缘重叠 1 个单位",    R(0, 0, 10, 10),  R(0, 9, 10, 10),  true);
    expectOverlap("十字交叉重叠（互不包含）",     R(0, 10, 30, 10), R(10, 0, 10, 30), true);
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 包含（大包小）
    // ------------------------------------------------------------------
    beginCase(3, "包含（大包小）-> 期望 true");
    p0 = gPass; f0 = gFail;
    expectOverlap("大包小：小矩形居中",           R(0, 0, 100, 100), R(30, 30, 20, 20), true);
    expectOverlap("大包小：小矩形偏在一角",       R(0, 0, 100, 100), R(75, 60, 20, 20), true);
    expectOverlap("大包小：参数顺序对调",         R(30, 30, 20, 20), R(0, 0, 100, 100), true);
    expectOverlap("大包小：小矩形贴大矩形内角",   R(0, 0, 100, 100), R(0, 0, 10, 10),   true);
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 边界刚好贴合（项目约定：贴合不算碰撞 -> false）
    // ------------------------------------------------------------------
    beginCase(4, "边界刚好贴合 -> 期望 false（贴合不算碰撞）");
    p0 = gPass; f0 = gFail;
    expectOverlap("A 右边贴 B 左边",         R(0, 0, 10, 10),  R(10, 0, 10, 10),  false);
    expectOverlap("A 右边贴 B 左边（反向）",  R(10, 0, 10, 10), R(0, 0, 10, 10),   false);
    expectOverlap("A 下边贴 B 上边",         R(0, 0, 10, 10),  R(0, 10, 10, 10),  false);
    expectOverlap("A 下边贴 B 上边（反向）",  R(0, 10, 10, 10), R(0, 0, 10, 10),   false);
    expectOverlap("仅角点贴合",              R(0, 0, 10, 10),  R(10, 10, 10, 10), false);
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 对称性
    // ------------------------------------------------------------------
    beginCase(5, "对称性：rectsOverlap(a,b) == rectsOverlap(b,a)");
    p0 = gPass; f0 = gFail;
    expectSymmetry("部分重叠对",   R(0, 0, 10, 10),   R(5, 5, 10, 10));
    expectSymmetry("完全分离对",   R(0, 0, 10, 10),   R(50, 50, 10, 10));
    expectSymmetry("边界贴合对",   R(0, 0, 10, 10),   R(10, 0, 10, 10));
    expectSymmetry("包含对",       R(0, 0, 100, 100), R(20, 20, 10, 10));
    expectSymmetry("仅角点贴合对", R(0, 0, 10, 10),   R(10, 10, 10, 10));
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // CASE 6 随机性质测试：与参照公式比对
    // 参照公式: (a.x < b.x + b.w) && (b.x < a.x + a.w) &&
    //           (a.y < b.y + b.h) && (b.y < a.y + a.h)
    // ------------------------------------------------------------------
    beginCase(6, "随机性质测试：10000 组随机矩形 vs 参照公式");
    p0 = gPass; f0 = gFail;
    {
        std::mt19937 rng(20261012u);
        std::uniform_int_distribution<int> dist(0, 20000); // 单位 0.01，范围 0.00 ~ 200.00
        auto rnd = [&dist, &rng]() { return static_cast<float>(dist(rng)) / 100.0f; };

        auto refOverlap = [](const SDL_FRect& a, const SDL_FRect& b) -> bool
        {
            return (a.x < b.x + b.w) && (b.x < a.x + a.w) &&
                   (a.y < b.y + b.h) && (b.y < a.y + a.h);
        };

        const int kCount = 10000;
        int mismatch = 0;
        int overlapping = 0;
        int shown = 0;

        for (int i = 0; i < kCount; ++i)
        {
            // 逐条语句生成，保证求值顺序固定、结果可复现
            const float ax = rnd();
            const float ay = rnd();
            const float aw = rnd();
            const float ah = rnd();
            const float bx = rnd();
            const float by = rnd();
            const float bw = rnd();
            const float bh = rnd();

            const SDL_FRect a = R(ax, ay, aw, ah);
            const SDL_FRect b = R(bx, by, bw, bh);

            const bool got = rectsOverlap(a, b);
            const bool exp = refOverlap(a, b);

            if (exp) { ++overlapping; }

            if (got != exp)
            {
                ++mismatch;
                if (shown < 5) // 最多展示 5 组失败样例
                {
                    ++shown;
                    std::printf("  [不一致] a=(%.2f,%.2f,%.2f,%.2f) b=(%.2f,%.2f,%.2f,%.2f) | 被测=%s 参照=%s\n",
                                a.x, a.y, a.w, a.h, b.x, b.y, b.w, b.h, bstr(got), bstr(exp));
                }
            }
        }

        if (mismatch == 0)
        {
            ++gPass;
            std::printf("  [PASS] %d 组随机数据全部一致（其中相交 %d 组 / 分离 %d 组）\n",
                        kCount, overlapping, kCount - overlapping);
        }
        else
        {
            ++gFail;
            std::printf("  [FAIL] %d 组随机数据中发现 %d 组不一致（上方最多展示 5 组样例）\n",
                        kCount, mismatch);
        }
    }
    endCase(6, p0, f0);

    // ------------------------------------------------------------------
    const int total = gPass + gFail;
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 条用例 | 通过 %d | 失败 %d\n", total, gPass, gFail);
    std::printf(" 结论: %s\n", (gFail == 0)
        ? "全部通过 —— rectsOverlap 行为符合项目约定。"
        : "存在失败用例，请检查上方 [FAIL] 行。");
    std::printf(" main 返回值（失败用例数）: %d\n", gFail);
    std::printf("==============================================================\n");

    return gFail;
}
