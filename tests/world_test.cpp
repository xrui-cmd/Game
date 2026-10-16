// ============================================================================
//  world_test.cpp — 世界（World）与相机（Camera）单元测试
//
//  被测对象 : Src/inc/game/World.h + Src/cpp/game/World.cpp（游戏层世界系统）
//             libs/GameEngine/include/engine/Camera.h（引擎相机，header-only）
//  覆盖场景 : ① 坐标 <-> 区块索引换算（floor 语义、负值与世界外）
//             ② 激活窗口计算（中心 ±1、边界 clamp、点是否在激活区）
//             ③ 确定性生成（同种子逐格相等 / 不同种子存在差异 / 结构规则：
//                边界一圈墙、区块交界无随机墙、出生保护区无墙、装饰不落墙）
//             ④ 格级碰撞（isRectWalkable / isPointWalkable / slideMove：
//                撞墙停轴、贴墙滑动、大位移不穿透）
//             ⑤ 相机：worldToScreen/screenToWorld 往返、边界钳制、
//                世界小于视口时居中、follow 指数平滑的步进与收敛
//
//  与其余测试的差异 : World.cpp 引用了 SDL（日志与纹理 API），因此本测试
//  编译 World.cpp 并链接 SDL3 与 SDL3_image 导入库；实际跑到的路径全部是
//  纯计算（不创建窗口、不加载素材），任何机器上都能确定性运行
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/World.h"     // World：生成 / 碰撞 / 激活窗口（被测）
#include "game/Zone.h"      // 分区表/查询：出生点与分区规则断言（批六甲）
#include "engine/Camera.h"  // Camera：坐标换算与平滑跟随（被测）

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
void expectNear(const char* label, float got, float expect, float eps = 0.01f)
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

} // namespace

int main()
{
#if defined(_WIN32)
    // 控制台代码页切到 UTF-8：中文输出不乱码（失败也不影响测试）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" World / Camera 单元测试 —— 世界生成 / 碰撞 / 激活窗口 / 相机\n");
    std::printf(" 世界默认 8x4 区块 x 1536px = 12288x6144；每区块 32x32 格 x 48px（批六甲）\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 坐标 <-> 区块索引（floor 语义、边界值、负值）
    // ------------------------------------------------------------------
    beginCase(1, "坐标 <-> 区块索引换算");
    int p0 = gPass, f0 = gFail;
    {
        World w;                 // 空世界：索引换算不依赖生成数据
        w.generate(8, 4, 12345); // 8x4 区块 = 12288x6144（与默认配置一致）

        // 横向：每 1536px 一个区块，边界值归入右侧区块（floor 语义）
        expectInt("chunkX(0) = 0", w.chunkXFromWorldX(0.0f), 0);
        expectInt("chunkX(1535.9) = 0", w.chunkXFromWorldX(1535.9f), 0);
        expectInt("chunkX(1536) = 1（边界归右）", w.chunkXFromWorldX(1536.0f), 1);
        expectInt("chunkX(3072) = 2（草地区中心线）", w.chunkXFromWorldX(3072.0f), 2);
        expectInt("chunkX(6144) = 4（草地/雪山分界线）", w.chunkXFromWorldX(6144.0f), 4);
        expectInt("chunkX(12287.9) = 7（最右下沿）", w.chunkXFromWorldX(12287.9f), 7);
        expectInt("chunkX(-0.1) = -1（世界外左）", w.chunkXFromWorldX(-0.1f), -1);
        expectInt("chunkX(12288) = 8（世界外右）", w.chunkXFromWorldX(12288.0f), 8);

        // 纵向：与横向同一公式（抽查边界）
        expectInt("chunkY(0) = 0", w.chunkYFromWorldY(0.0f), 0);
        expectInt("chunkY(1536) = 1", w.chunkYFromWorldY(1536.0f), 1);
        expectInt("chunkY(6143.9) = 3（最下沿）", w.chunkYFromWorldY(6143.9f), 3);

        // 尺寸换算：区块数 / 格数 / 像素数三者自洽（批六甲：32 格/区块）
        expectInt("chunksX = 8", w.chunksX(), 8);
        expectInt("chunksY = 4", w.chunksY(), 4);
        expectInt("cellsX = 8x32 = 256", w.cellsX(), 256);
        expectInt("cellsY = 4x32 = 128", w.cellsY(), 128);
        expectNear("pixelWidth = 256x48 = 12288", w.pixelWidth(), 12288.0f);
        expectNear("pixelHeight = 128x48 = 6144", w.pixelHeight(), 6144.0f);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 激活窗口计算（中心 ±1、边界 clamp、激活点判定）
    // ------------------------------------------------------------------
    beginCase(2, "激活窗口（相机所在区块 ±1）");
    p0 = gPass; f0 = gFail;
    {
        World w;
        w.generate(8, 4, 12345);

        // 相机视图中心在草地区中心 (3072,3072) -> 区块 (2,2) -> 窗口 (1,1)~(3,3)
        w.updateActiveWindow(3072.0f, 3072.0f);
        expectInt("草地中心窗口 minX = 1", w.activeMinChunkX(), 1);
        expectInt("草地中心窗口 maxX = 3", w.activeMaxChunkX(), 3);
        expectInt("草地中心窗口 minY = 1", w.activeMinChunkY(), 1);
        expectInt("草地中心窗口 maxY = 3", w.activeMaxChunkY(), 3);
        expectTrue("草地区中心点在激活区", w.isPointInActiveChunks(3072.0f, 3072.0f));
        expectTrue("区块 (0,0) 不在激活区", !w.isPointInActiveChunks(100.0f, 100.0f));
        expectTrue("区块 (1,1) 在激活区", w.isPointInActiveChunks(1600.0f, 1600.0f));

        // 跨区边界（x=6144）：区块 (4,2) -> 窗口 (3,1)~(5,3)——分界两侧相邻
        w.updateActiveWindow(6144.0f, 3072.0f);
        expectInt("跨区窗口 minX = 3", w.activeMinChunkX(), 3);
        expectInt("跨区窗口 maxX = 5", w.activeMaxChunkX(), 5);

        // 相机在左上角 (0,0) -> 区块 (0,0) -> 窗口 (0,0)~(1,1)（clamp 左/上）
        w.updateActiveWindow(0.0f, 0.0f);
        expectInt("角落窗口 minX = 0（clamp）", w.activeMinChunkX(), 0);
        expectInt("角落窗口 maxX = 1", w.activeMaxChunkX(), 1);
        expectInt("角落窗口 maxY = 1", w.activeMaxChunkY(), 1);

        // 相机在右下角 -> 区块 (7,3) -> 窗口 (6,2)~(7,3)（clamp 右/下）
        w.updateActiveWindow(12287.0f, 6143.0f);
        expectInt("右下窗口 minX = 6", w.activeMinChunkX(), 6);
        expectInt("右下窗口 maxX = 7（clamp）", w.activeMaxChunkX(), 7);
        expectInt("右下窗口 minY = 2", w.activeMinChunkY(), 2);
        expectInt("右下窗口 maxY = 3（clamp）", w.activeMaxChunkY(), 3);

        // 相机中心在世界外（异常输入）：clamp 到最近区块，窗口仍合法
        w.updateActiveWindow(99999.0f, -99999.0f);
        expectInt("世界外输入 -> minX = 6", w.activeMinChunkX(), 6);
        expectInt("世界外输入 -> maxX = 7", w.activeMaxChunkX(), 7);
        expectInt("世界外输入 -> minY = 0", w.activeMinChunkY(), 0);
        expectInt("世界外输入 -> maxY = 1", w.activeMaxChunkY(), 1);
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 确定性生成（同种子逐格相等 / 不同种子存在差异）
    // ------------------------------------------------------------------
    beginCase(3, "确定性生成（同种子一致、异种子不同）");
    p0 = gPass; f0 = gFail;
    {
        World a;
        World b;
        World c;
        a.generate(8, 4, 777); // 种子 777：左侧样本
        b.generate(8, 4, 777); // 种子 777：右侧样本（应与左侧逐格相等）
        c.generate(8, 4, 778); // 种子 778：对照样本（应与 777 存在差异）

        // 逐格比对三项数据：墙 / 地板变体 / 装饰
        int wallDiff = 0;  // 墙差异计数
        int floorDiff = 0; // 地板差异计数
        int decorDiff = 0; // 装饰差异计数
        for (int cellY = 0; cellY < a.cellsY(); ++cellY)
        {
            for (int cellX = 0; cellX < a.cellsX(); ++cellX)
            {
                // A vs B（同种子）：每一项都应完全相同
                if (a.wallAt(cellX, cellY) != b.wallAt(cellX, cellY)) { ++wallDiff; }
                if (a.floorVariantAt(cellX, cellY) != b.floorVariantAt(cellX, cellY)) { ++floorDiff; }
                if (a.decorAt(cellX, cellY) != b.decorAt(cellX, cellY)) { ++decorDiff; }
            }
        }
        expectInt("同种子 777：墙逐格相等", wallDiff, 0);
        expectInt("同种子 777：地板逐格相等", floorDiff, 0);
        expectInt("同种子 777：装饰逐格相等", decorDiff, 0);

        // A vs C（异种子）：至少存在可见差异（差异数 > 0）
        int otherWallDiff = 0;  // 777 vs 778 的墙差异
        int otherFloorDiff = 0; // 777 vs 778 的地板差异
        for (int cellY = 0; cellY < a.cellsY(); ++cellY)
        {
            for (int cellX = 0; cellX < a.cellsX(); ++cellX)
            {
                // 统计两个种子之间的布局差异
                if (a.wallAt(cellX, cellY) != c.wallAt(cellX, cellY)) { ++otherWallDiff; }
                if (a.floorVariantAt(cellX, cellY) != c.floorVariantAt(cellX, cellY)) { ++otherFloorDiff; }
            }
        }
        expectTrue("异种子 777/778：墙布局存在差异", otherWallDiff > 0);
        expectTrue("异种子 777/778：地板变体存在差异", otherFloorDiff > 0);

        // 确定性再验：同一对象的重复生成（模拟「卸载再载入」的数据侧）
        World d;
        d.generate(8, 4, 777); // 重新生成同一世界
        int regenDiff = 0;     // 重生成差异计数
        for (int cellY = 0; cellY < a.cellsY(); ++cellY)
        {
            for (int cellX = 0; cellX < a.cellsX(); ++cellX)
            {
                // 墙与地板（含装饰抽查）重生成后必须逐格一致
                if (a.wallAt(cellX, cellY) != d.wallAt(cellX, cellY)) { ++regenDiff; }
                if (a.decorAt(cellX, cellY) != d.decorAt(cellX, cellY)) { ++regenDiff; }
            }
        }
        expectInt("重生成（同种子）：逐格一致", regenDiff, 0);
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 生成的结构规则（边界墙 / 区块交界 / 出生保护区 / 装饰合法性）
    // ------------------------------------------------------------------
    beginCase(4, "结构规则（边界一圈墙、交界与保护区可通行）");
    p0 = gPass; f0 = gFail;
    {
        World w;
        w.generate(8, 4, 2026); // 任意种子：规则必须恒成立（与种子无关）

        // 规则一：世界最外圈格全部是墙（世界的物理边界）
        bool borderAllWall = true; // 边界圈是否全墙
        for (int x = 0; x < w.cellsX(); ++x)
        {
            // 上下两行逐格检查
            if (!w.wallAt(x, 0) || !w.wallAt(x, w.cellsY() - 1)) { borderAllWall = false; }
        }
        for (int y = 0; y < w.cellsY(); ++y)
        {
            // 左右两列逐格检查
            if (!w.wallAt(0, y) || !w.wallAt(w.cellsX() - 1, y)) { borderAllWall = false; }
        }
        expectTrue("世界最外圈全部是墙", borderAllWall);

        // 规则二：区块交界一圈格没有随机墙（交界永远可通行）
        //（排除世界最外圈——那里本来就是边界墙）
        bool chunkEdgeClear = true; // 是否全部无随机墙
        for (int cellY = 1; cellY < w.cellsY() - 1; ++cellY)
        {
            for (int cellX = 1; cellX < w.cellsX() - 1; ++cellX)
            {
                // 本格是否位于区块边缘一圈（局部坐标 0 或 31——批六甲 32 格区块）
                const bool onEdge = (cellX % World::kChunkCells == 0) ||
                                    (cellX % World::kChunkCells == World::kChunkCells - 1) ||
                                    (cellY % World::kChunkCells == 0) ||
                                    (cellY % World::kChunkCells == World::kChunkCells - 1);
                if (onEdge && w.wallAt(cellX, cellY))
                {
                    chunkEdgeClear = false; // 交界格出现墙：违例
                }
            }
        }
        expectTrue("区块交界格无随机墙（可通行）", chunkEdgeClear);

        // 规则三：出生保护区（草地区中心格 ±3）无墙无装饰。
        // 批六甲：中心 = 草地区复活点 (3072,3072) -> 格 (64,64)——不是
        // 几何世界中心（8x4 世界几何中心恰在草地/雪山分界线上）
        const int spawnCellX = static_cast<int>(zoneDef(ZoneId::Grass).respawnX / World::kCellSize); // 64
        const int spawnCellY = static_cast<int>(zoneDef(ZoneId::Grass).respawnY / World::kCellSize); // 64
        expectInt("出生保护区中心格 x = 64", spawnCellX, 64);
        bool safeZoneClear = true; // 保护区是否干净
        for (int cellY = spawnCellY - World::kSpawnSafeRadius;
             cellY <= spawnCellY + World::kSpawnSafeRadius; ++cellY)
        {
            for (int cellX = spawnCellX - World::kSpawnSafeRadius;
                 cellX <= spawnCellX + World::kSpawnSafeRadius; ++cellX)
            {
                // 保护区内不许有墙，装饰也不落（保持出生点视野干净）
                if (w.wallAt(cellX, cellY) || w.decorAt(cellX, cellY) != 0)
                {
                    safeZoneClear = false; // 保护区内出现墙/装饰：违例
                }
            }
        }
        expectTrue("出生保护区（中心 ±3 格）无墙无装饰", safeZoneClear);

        // 规则四：装饰只出现在非墙格；地板变体全部落在 [0,7]
        bool decorAndFloorLegal = true; // 合法性总旗标
        for (int cellY = 0; cellY < w.cellsY(); ++cellY)
        {
            for (int cellX = 0; cellX < w.cellsX(); ++cellX)
            {
                // 装饰不在墙上（装饰是地板摆件）；地板变体在素材表范围内
                if (w.decorAt(cellX, cellY) != 0 && w.wallAt(cellX, cellY))
                {
                    decorAndFloorLegal = false;
                }
                const int variant = w.floorVariantAt(cellX, cellY); // 本格地板变体
                if (variant < 0 || variant > 7)
                {
                    decorAndFloorLegal = false;
                }
            }
        }
        expectTrue("装饰不落墙格、地板变体在 [0,7]", decorAndFloorLegal);
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 格级碰撞与滑动（撞墙停轴 / 贴墙滑动 / 不穿透）
    // ------------------------------------------------------------------
    beginCase(5, "格级碰撞与 slideMove 滑动");
    p0 = gPass; f0 = gFail;
    {
        World w;
        w.generate(8, 4, 555);

        // 出生点总是可走（保护区保证）——碰撞测试的出发位置
        const SDL_FPoint spawn = w.spawnPointFor(48.0f, 48.0f); // 测试矩形 48x48
        expectTrue("出生点矩形可走", w.isRectWalkable(SDL_FRect{ spawn.x, spawn.y, 48.0f, 48.0f }));

        // 世界外点位不可走（出界即墙）
        expectTrue("世界外 (-5,-5) 不可走", !w.isPointWalkable(-5.0f, -5.0f));

        // 用「小步探测」找到 +x 方向的第一面墙（与随机布局无关的稳健做法）：
        // 每次只滑 1px，直到位置不再变化 == 前方被墙挡住
        float x = spawn.x; // 测试矩形左上角 x
        float y = spawn.y; // 测试矩形左上角 y
        bool wallFound = false; // 是否在某步停住
        for (int step = 0; step < 12000; ++step) // 12288 宽足够走到边界墙
        {
            // 单步向右滑动 1px；被挡时返回原位置
            const SDL_FPoint next = w.slideMove(x, y, 48.0f, 48.0f, 1.0f, 0.0f);
            if (next.x == x)
            {
                wallFound = true; // 前方有墙：探测完成
                break;
            }
            x = next.x; // 推进到新位置
        }
        expectTrue("能探测到 +x 方向的墙（生成布局必然有）", wallFound);
        expectTrue("停住的位置仍可走", w.isRectWalkable(SDL_FRect{ x, y, 48.0f, 48.0f }));

        // 撞墙停轴：对同一位置施加「+1000px」的大位移也不得前进（不穿透）
        const SDL_FPoint blocked = w.slideMove(x, y, 48.0f, 48.0f, 1000.0f, 0.0f);
        expectNear("大位移撞墙：x 不再前进", blocked.x, x);

        // 贴墙滑动：x 被挡时同一步给 y 分量——x 保持、y 照常推进
        const SDL_FPoint slid = w.slideMove(x, y, 48.0f, 48.0f, 1000.0f, 60.0f);
        expectNear("滑动：x 被墙挡住（保持）", slid.x, x);
        expectTrue("滑动：y 轴继续推进", slid.y > y);
        expectTrue("滑动后位置可走", w.isRectWalkable(SDL_FRect{ slid.x, slid.y, 48.0f, 48.0f }));

        // 大位移全方向乱滑：最终位置永远可走（子步防穿透的性质抽查）
        bool neverInsideWall = true; // 是否从未落进墙里
        const float probes[4][2] = { { 9999.0f, 0.0f }, { -9999.0f, 0.0f },
                                     { 0.0f, 9999.0f }, { -1234.0f, 2345.0f } };
        for (const auto& probe : probes)
        {
            // 从出生点分别向四个方向做大位移滑动
            const SDL_FPoint moved = w.slideMove(spawn.x, spawn.y, 48.0f, 48.0f,
                                                 probe[0], probe[1]);
            if (!w.isRectWalkable(SDL_FRect{ moved.x, moved.y, 48.0f, 48.0f }))
            {
                neverInsideWall = false; // 落点不可走：穿透违例
            }
        }
        expectTrue("大位移滑动后落点永远可走（不穿透）", neverInsideWall);
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // CASE 6 相机：坐标往返 / 边界钳制 / 世界小于视口居中
    // ------------------------------------------------------------------
    beginCase(6, "相机坐标换算与钳制");
    p0 = gPass; f0 = gFail;
    {
        Camera cam;
        cam.setViewport(1280.0f, 720.0f); // 与默认窗口一致
        cam.setWorldSize(3072.0f, 3072.0f);

        // snapTo 世界中心：位置 = 中心 - 视口一半 = (896, 1176)
        cam.snapTo(1536.0f, 1536.0f);
        expectNear("snapTo 中心：position.x = 896", cam.position().x, 896.0f);
        expectNear("snapTo 中心：position.y = 1176", cam.position().y, 1176.0f);

        // worldToScreen / screenToWorld 往返（浮点加减互逆，误差应 ~1e-3 内）
        const Vec2 screen = cam.worldToScreen(1000.0f, 1200.0f);    // 世界 -> 屏幕
        const Vec2 back = cam.screenToWorld(screen.x, screen.y);    // 屏幕 -> 世界
        expectNear("往返：x 还原", back.x, 1000.0f, 0.001f);
        expectNear("往返：y 还原", back.y, 1200.0f, 0.001f);

        // 钳制左/上：目标中心在 (0,0) 超界，位置钉在 (0,0)
        cam.snapTo(0.0f, 0.0f);
        expectNear("左上越界：位置钉在 0", cam.position().x, 0.0f);
        expectNear("左上越界：位置钉在 0", cam.position().y, 0.0f);

        // 钳制右/下：位置上限 = (3072-1280, 3072-720) = (1792, 2352)
        cam.snapTo(3072.0f, 3072.0f);
        expectNear("右下越界：位置上限 x = 1792", cam.position().x, 1792.0f);
        expectNear("右下越界：位置上限 y = 2352", cam.position().y, 2352.0f);

        // 世界小于视口：该轴居中（位置可能为负）
        cam.setWorldSize(800.0f, 400.0f);
        cam.snapTo(400.0f, 200.0f);
        expectNear("世界<视口：x 轴居中 = (800-1280)/2", cam.position().x, -240.0f);
        expectNear("世界<视口：y 轴居中 = (400-720)/2", cam.position().y, -160.0f);

        // setViewport 后立即回钳：世界换回大尺寸、位置越界会被拉回
        cam.setWorldSize(3072.0f, 3072.0f);
        cam.snapTo(1536.0f, 1536.0f);      // 合法位置 (896, 1176)
        expectNear("回调后位置回到合法", cam.position().x, 896.0f);
    }
    endCase(6, p0, f0);

    // ------------------------------------------------------------------
    // CASE 7 相机：follow 指数平滑（步进量公式 / 长期收敛 / 零平滑）
    // ------------------------------------------------------------------
    beginCase(7, "相机 follow 指数平滑");
    p0 = gPass; f0 = gFail;
    {
        Camera cam;
        cam.setViewport(1280.0f, 720.0f);
        cam.setWorldSize(4000.0f, 4000.0f); // 足够大：本用例不触发边界钳制

        // 初始：中心 (1000,1000) -> 位置 (360, 640)
        cam.snapTo(1000.0f, 1000.0f);

        // 单步步进量 = (目标位置 - 当前位置) x (1 - exp(-dt/tau))。
        // 目标中心 2600 -> 目标左上角 = 2600 - 640 = 1960；当前位置 360
        const float dt = 1.0f / 120.0f; // 固定步长
        const float tau = 0.12f;        // 平滑时间常数（默认配置值）
        const float expectedFactor = 1.0f - std::exp(-dt / tau); // 期望平滑因子
        const float expectedStep = (1960.0f - 360.0f) * expectedFactor; // 期望单步位移
        cam.follow(2600.0f, 1000.0f, dt, tau);
        expectNear("单步步进量符合公式", cam.position().x, 360.0f + expectedStep, 0.01f);

        // 长期收敛：反复 follow 足够多步后逼近目标（稳态误差 -> 0）
        for (int i = 0; i < 2000; ++i)
        {
            cam.follow(2600.0f, 1000.0f, dt, tau); // 持续向同一目标平滑
        }
        expectNear("长期收敛到目标 x = 1960", cam.position().x, 1960.0f, 0.5f);

        // smoothTime = 0（显式「不平滑」）：单步立即到位（与 snapTo 等价）
        cam.snapTo(1000.0f, 1000.0f);           // 先回初始位置
        cam.follow(2000.0f, 1000.0f, dt, 0.0f); // 零平滑：一步到位
        expectNear("smoothTime=0 单步到位 x = 1360", cam.position().x, 2000.0f - 640.0f);

        // dt = 0（异常时间步）：不推进
        cam.snapTo(1000.0f, 1000.0f);
        cam.follow(2000.0f, 1000.0f, 0.0f, tau);
        expectNear("dt=0 不推进", cam.position().x, 360.0f);
    }
    endCase(7, p0, f0);

    // ------------------------------------------------------------------
    // CASE 8 出生点换算与可走断言
    // ------------------------------------------------------------------
    beginCase(8, "出生点（草地区中心）换算");
    p0 = gPass; f0 = gFail;
    {
        World w;
        w.generate(8, 4, 999);

        // spawnPointFor：实体中心落在草地区中心 (3072,3072)（批六甲）
        const SDL_FPoint sp = w.spawnPointFor(48.0f, 84.0f); // 玩家尺寸 48x84
        expectNear("出生点 x = 3072 - 24 = 3048", sp.x, 3048.0f);
        expectNear("出生点 y = 3072 - 42 = 3030", sp.y, 3030.0f);
        expectTrue("玩家出生矩形可走", w.isRectWalkable(SDL_FRect{ sp.x, sp.y, 48.0f, 84.0f }));

        // 世界外：点/矩形两条路径一致不可走
        expectTrue("世界外矩形不可走", !w.isRectWalkable(SDL_FRect{ -100.0f, -100.0f, 48.0f, 48.0f }));
    }
    endCase(8, p0, f0);

    // ------------------------------------------------------------------
    // 总结
    // ------------------------------------------------------------------
    std::printf("\n==============================================================\n");
    std::printf(" 总结: %d 通过, %d 失败\n", gPass, gFail);
    std::printf("==============================================================\n");
    return gFail; // 退出码 = 失败数（0 = 全部通过）
}
