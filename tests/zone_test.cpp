// ============================================================================
//  zone_test.cpp — 世界分区（Zone）单元测试（批六甲）
//
//  被测对象 : Src/inc/game/Zone.h（区表 / zoneAt 查询 / 玩家 debuff 数值）+
//             Src/inc/game/World.h 的世界几何常量（32 格区块、1536 像素）
//
//  覆盖场景 : ① 区表数值（区名 / 区块区间 / 等级区间 1~20 与 21~30 /
//                素材集 / 移速乘区 1.0 与 0.85 / 复活点坐标）
//             ② zoneAtChunk（区块查询：草地 0..3、雪山 4..7、世界外兜底）
//             ③ zoneAtWorld（世界坐标查询：分界线 6144 归雪山、负值归
//                草地、远界兜底——floor 语义）
//             ④ zoneAtRect（矩形按中心点归区）
//             ⑤ zoneDef 越界防御（异常 id 回退草地）
//             ⑥ 区几何与默认世界布局自洽（草地上界 x = 世界宽度一半；
//                草地区中心 = 世界「起始区」中心；雪山区复活点在区内且
//                靠草地一侧；两区纵向全高）
//
//  依赖 : Zone.h / World.h 只用到 SDL 头文件中的类型与内联函数
//  （SDL_FRect / SDL.h），不调用任何 SDL 运行时函数——不链接 SDL3，
//  任何机器上都能确定性运行（构建接线见 tests/CMakeLists.txt）。
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/Zone.h" // 被测：ZoneId / ZoneDef / kZoneDefs / zoneAt* / zoneDef

#include <cmath>    // std::fabs：浮点近似断言
#include <cstdio>   // std::printf
#include <cstdlib>  // std::system（Windows 控制台切 UTF-8）
#include <cstring>  // std::strcmp：区名文案逐字核对

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
void expectNear(const char* label, float got, float expect, float eps = 0.01f)
{
    // 偏差不超过容差才算通过
    const bool ok = std::fabs(got - expect) <= eps;
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名 + 期望与实际
    std::printf("  [%s] %s | 期望≈%.3f 实际=%.3f\n", ok ? "PASS" : "FAIL", label, expect, got);
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

// 断言：字符串相等
void expectStr(const char* label, const char* got, const char* expect)
{
    // 逐字节比较（区名为固定文案）
    const bool ok = (std::strcmp(got, expect) == 0);
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名 + 期望与实际
    std::printf("  [%s] %s | 期望=\"%s\" 实际=\"%s\"\n", ok ? "PASS" : "FAIL", label, expect, got);
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
    std::printf(" Zone 分区单元测试 —— 区表 / 查询 / 几何自洽（批六甲）\n");
    std::printf(" 默认布局：8x4 区块（12288x6144）；西 4x4 草地 / 东 4x4 雪山\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 区表数值（名称 / 区块区间 / 等级区间 / 素材集 / 乘区 / 复活点）
    // ------------------------------------------------------------------
    beginCase(1, "区表数值（草地 1~20 / 雪山 21~30；乘区 1.0/0.85）");
    int p0 = gPass, f0 = gFail;
    {
        // 区数常量与表尺寸
        expectInt("kZoneCount = 2", kZoneCount, 2);

        const ZoneDef& grass = zoneDef(ZoneId::Grass); // 草地定义
        const ZoneDef& snow = zoneDef(ZoneId::Snow);   // 雪山定义

        // 区名（横幅/日志显示文案）
        expectStr("草地区名", grass.name, "草地");
        expectStr("雪山区名", snow.name, "雪山");

        // 区矩形（按区块；纵向全高——只存 x 区间）
        expectInt("草地区块 x 区间 = 0..3", grass.chunkMinX, 0);
        expectInt("草地上界 = 3", grass.chunkMaxX, 3);
        expectInt("雪山区块 x 区间 = 4..7", snow.chunkMinX, 4);
        expectInt("雪山上界 = 7", snow.chunkMaxX, 7);

        // 等级区间（批六乙消费的数据；本批为表值核对）
        expectInt("草地等级下界 = 1", grass.levelMin, 1);
        expectInt("草地等级上界 = 20", grass.levelMax, 20);
        expectInt("雪山等级下界 = 21", snow.levelMin, 21);
        expectInt("雪山等级上界 = 30", snow.levelMax, 30);

        // 素材集（World 烘焙选表依据）
        expectTrue("草地素材集 = 地牢原色", grass.tileSet == ZoneTileSet::Dungeon);
        expectTrue("雪山素材集 = 冰霜重染色", snow.tileSet == ZoneTileSet::Snow);

        // 玩家移速乘区（草=无影响 / 雪=0.85 减速 15%）
        expectNear("草地移速乘区 = 1.0", grass.moveSpeedMultiplier, 1.0f);
        expectNear("雪山移速乘区 = 0.85", snow.moveSpeedMultiplier, 0.85f);

        // 复活点（批六乙消费）：草地 = 区中心（4 区块 x 1536 / 2 = 3072）
        expectNear("草地复活点 x = 3072（区中心）", grass.respawnX, 3072.0f);
        expectNear("草地复活点 y = 3072（纵向中心）", grass.respawnY, 3072.0f);
        // 雪山 = 靠草地一侧中部（西缘深入半区块 = 4x1536 + 768 = 6912）
        expectNear("雪山复活点 x = 6912（靠草地一侧）", snow.respawnX, 6912.0f);
        expectNear("雪山复活点 y = 3072（纵向中部）", snow.respawnY, 3072.0f);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 zoneAtChunk（区块查询；世界外兜底）
    // ------------------------------------------------------------------
    beginCase(2, "zoneAtChunk（0..3 草地 / 4..7 雪山 / 越界兜底）");
    p0 = gPass; f0 = gFail;
    {
        // 草地四列（逐区块核对边界）
        expectTrue("区块 x=0 -> 草地", zoneAtChunk(0, 0) == ZoneId::Grass);
        expectTrue("区块 x=3 -> 草地（上界）", zoneAtChunk(3, 0) == ZoneId::Grass);
        // 雪山四列
        expectTrue("区块 x=4 -> 雪山（分界区块）", zoneAtChunk(4, 0) == ZoneId::Snow);
        expectTrue("区块 x=7 -> 雪山（上界）", zoneAtChunk(7, 0) == ZoneId::Snow);

        // 纵向不分区：同一 x 在任何 y 得到同一区
        expectTrue("区块 (2,0) 与 (2,3) 同为草地",
                   zoneAtChunk(2, 0) == ZoneId::Grass && zoneAtChunk(2, 3) == ZoneId::Grass);
        expectTrue("区块 (5,0) 与 (5,3) 同为雪山",
                   zoneAtChunk(5, 0) == ZoneId::Snow && zoneAtChunk(5, 3) == ZoneId::Snow);

        // 世界外兜底：负向归草地（最近区）、超界归雪山
        expectTrue("区块 x=-5 -> 草地（世界外左兜底）", zoneAtChunk(-5, 0) == ZoneId::Grass);
        expectTrue("区块 x=99 -> 雪山（世界外右兜底）", zoneAtChunk(99, 0) == ZoneId::Snow);
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 zoneAtWorld（世界坐标查询；分界与 floor 语义）
    // ------------------------------------------------------------------
    beginCase(3, "zoneAtWorld（分界 6144 归雪山 / 负值归草地）");
    p0 = gPass; f0 = gFail;
    {
        // 世界几何前提（与 World.h 常量自洽）：区块 1536px、分界线 6144px
        expectInt("World::kChunkCells = 32", World::kChunkCells, 32);
        expectNear("World::kChunkPixels = 1536", World::kChunkPixels, 1536.0f);

        // 草地区样本点
        expectTrue("(100,100) -> 草地", zoneAtWorld(100.0f, 100.0f) == ZoneId::Grass);
        expectTrue("(3072,3072) 草地区中心 -> 草地",
                   zoneAtWorld(3072.0f, 3072.0f) == ZoneId::Grass);
        // 分界线：x=6143.99 仍草地（floor 到区块 3）、x=6144 归雪山（区块 4）
        expectTrue("(6143.99) -> 草地（分界线左侧）",
                   zoneAtWorld(6143.99f, 3072.0f) == ZoneId::Grass);
        expectTrue("(6144) -> 雪山（分界线归右）",
                   zoneAtWorld(6144.0f, 3072.0f) == ZoneId::Snow);
        // 雪山区样本点
        expectTrue("(6912,3072) 雪山复活点 -> 雪山",
                   zoneAtWorld(6912.0f, 3072.0f) == ZoneId::Snow);
        expectTrue("(12287,6130) 右下角 -> 雪山",
                   zoneAtWorld(12287.0f, 6130.0f) == ZoneId::Snow);

        // 世界外：负值归草地、超界归雪山（与 zoneAtChunk 同兜底）
        expectTrue("(-10,-10) -> 草地（世界外左）", zoneAtWorld(-10.0f, -10.0f) == ZoneId::Grass);
        expectTrue("(99999,0) -> 雪山（世界外右）", zoneAtWorld(99999.0f, 0.0f) == ZoneId::Snow);
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 zoneAtRect（矩形按中心点归区）与 zoneDef 越界防御
    // ------------------------------------------------------------------
    beginCase(4, "zoneAtRect（中心口径）与 zoneDef 越界防御");
    p0 = gPass; f0 = gFail;
    {
        // 矩形横跨分界线：中心在 6144+ 一侧 -> 雪山（中心口径）
        const SDL_FRect crossing{ 6000.0f, 3000.0f, 400.0f, 100.0f }; // 中心 (6200,3050)
        expectTrue("跨线矩形（中心在雪山侧）-> 雪山",
                   zoneAtRect(crossing) == ZoneId::Snow);

        // 矩形完全在草地内
        const SDL_FRect grassRect{ 3000.0f, 3000.0f, 100.0f, 100.0f }; // 中心 (3050,3050)
        expectTrue("草地内矩形 -> 草地", zoneAtRect(grassRect) == ZoneId::Grass);

        // 矩形完全在雪山内
        const SDL_FRect snowRect{ 7000.0f, 1000.0f, 100.0f, 100.0f }; // 中心 (7050,1050)
        expectTrue("雪山内矩形 -> 雪山", zoneAtRect(snowRect) == ZoneId::Snow);

        // zoneDef 越界防御：异常 id 回退草地（不崩、结果可预测）
        expectStr("zoneDef(越界 id) 回退草地", zoneDef(static_cast<ZoneId>(99)).name, "草地");
        expectStr("zoneDef(负 id) 回退草地", zoneDef(static_cast<ZoneId>(-3)).name, "草地");
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 区几何与默认世界布局自洽（宽度比例 / 复活点落位 / 纵向全高）
    // ------------------------------------------------------------------
    beginCase(5, "区几何自洽（4+4 区块、复活点落位、纵向全高）");
    p0 = gPass; f0 = gFail;
    {
        const ZoneDef& grass = zoneDef(ZoneId::Grass); // 草地定义
        const ZoneDef& snow = zoneDef(ZoneId::Snow);   // 雪山定义

        // 区块数对称：西 4 + 东 4 = 8（默认 8x4 世界）
        const int grassChunks = grass.chunkMaxX - grass.chunkMinX + 1; // 草地 4
        const int snowChunks = snow.chunkMaxX - snow.chunkMinX + 1;    // 雪山 4
        expectInt("草地区 4 区块宽", grassChunks, 4);
        expectInt("雪山区 4 区块宽", snowChunks, 4);
        expectInt("两区合计 8 区块 = 默认世界宽", grassChunks + snowChunks, 8);

        // 草地复活点 = 草地区中心：x = 4 区块 x 1536 / 2 = 世界（该区）中心
        expectNear("草地复活点 = 草地区中心（4x1536/2）",
                   grass.respawnX, 4.0f * World::kChunkPixels * 0.5f);

        // 雪山复活点落位：在雪山 x 区间内（6144..12288）且「靠草地一侧」
        //（距分界线 < 1 区块宽——半区块 768 深入）
        const float snowZoneStartX = static_cast<float>(snow.chunkMinX) * World::kChunkPixels; // 6144
        expectTrue("雪山复活点在雪山区间内",
                   snow.respawnX >= snowZoneStartX &&
                   snow.respawnX < static_cast<float>(snow.chunkMaxX + 1) * World::kChunkPixels);
        expectTrue("雪山复活点距分界线 < 1 区块（靠草地一侧）",
                   (snow.respawnX - snowZoneStartX) < World::kChunkPixels);

        // 复活点所属区自洽：对复活点跑 zoneAtWorld 应得各自区
        expectTrue("草地复活点 -> 草地", zoneAtWorld(grass.respawnX, grass.respawnY) == ZoneId::Grass);
        expectTrue("雪山复活点 -> 雪山", zoneAtWorld(snow.respawnX, snow.respawnY) == ZoneId::Snow);

        // 移速乘区口径：雪山减速但不为零（0 < 0.85 < 1——「减速」而非「停滞」）
        expectTrue("雪山移速乘区在 (0,1)：减速但可动",
                   snow.moveSpeedMultiplier > 0.0f && snow.moveSpeedMultiplier < 1.0f);
        // 等级区间口径：雪山起点接续草地终点（21 = 20 + 1——两区不重叠不断档）
        expectInt("雪山等级下界接续草地上界", snow.levelMin, grass.levelMax + 1);
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // 总结
    // ------------------------------------------------------------------
    std::printf("\n==============================================================\n");
    std::printf(" 总结: %d 通过, %d 失败\n", gPass, gFail);
    std::printf("==============================================================\n");
    return gFail; // 退出码 = 失败数（0 = 全部通过）
}
