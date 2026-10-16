#include "game/World.h"
#include "engine/Camera.h"        // Camera：激活窗口跟随相机视图中心（position/viewport 查询）
#include <SDL3_image/SDL_image.h> // IMG_LoadTexture：加载世界素材（地砖/墙/装饰）
#include <algorithm>              // std::clamp / std::min / std::max：尺寸防御与窗口夹取
#include <cmath>                  // std::floor：像素 -> 格索引的向下取整；std::fabs / std::ceil：子步拆分
#include <string>                 // std::string：拼接素材文件的绝对路径

// 本编译单元私有的确定性生成常量（不对外暴露）
namespace
{

// 哈希盐：同一 (种子, x, y) 在不同「用途」下应取到不同随机序列——
// 地板变体、区块序列、装饰各自带盐，避免相同坐标的不同用途产生关联
constexpr uint64_t kSaltFloor = 0xA24BAED4963EE407ull;  // 地板变体用途
constexpr uint64_t kSaltChunk = 0x9FB21C651E98DF25ull;  // 区块随机结构/装饰的序列种子

// 每区块随机结构数量范围（含两端）：3~5 个——16x16 格里的墙密度约
// 5%~10%，与「密度适中不堵死」对应（密度是结构数与结构长度的乘积效应）
constexpr int kStructuresMin = 3; // 每区块最少结构数
constexpr int kStructuresMax = 5; // 每区块最多结构数

// 结构尺寸范围：线段长 2~4 格；结构起点在区块内 2..13（天然避开边缘两圈）
constexpr int kSegmentLenMin = 2; // 线段最短（格）
constexpr int kSegmentLenMax = 4; // 线段最长（格）
constexpr int kLocalMin = 2;      // 区块内坐标下界（含）
constexpr int kLocalSpan = 12;    // 区块内坐标取值范围（2..13 共 12 个取值）

// 每区块装饰数量范围（含两端）：1~3 个纯视觉摆件
constexpr int kDecorMin = 1; // 每区块最少装饰数
constexpr int kDecorMax = 3; // 每区块最多装饰数

// 滑坡（slideMove）的单子步位移上限：半格（24px）——单子步小于一格宽，
// 保证任何速度下位移都不会「跳过」一整格墙（验收项：高速贴墙不穿透）
constexpr float kMaxSubStep = World::kCellSize * 0.5f;

} // 匿名命名空间结束

// 析构：兜底释放全部纹理（区块纹理 + 素材纹理；幂等）
World::~World()
{
    release(); // 任何销毁路径都不泄漏纹理
}

// ============================================================
// 生成（纯逻辑，无 SDL 运行期依赖）
// ============================================================
void World::generate(int chunksX, int chunksY, uint32_t seed)
{
    // 尺寸防御：至少 1x1（0 或负数区块的世界没有意义，会被后续除零类
    // 换算拖垮；夹到 1 让错误配置退化为「最小可玩世界」而不是崩溃）
    m_chunksX = std::max(chunksX, 1); // 横向区块数（夹取后）
    m_chunksY = std::max(chunksY, 1); // 纵向区块数（夹取后）
    m_seed = seed;                    // 记录种子（查询/复现/日志用）

    // 重新生成前丢弃旧区块纹理（内容已作废；区块数可能变化，缓存重建）
    for (SDL_Texture*& texture : m_chunkTextures)
    {
        if (texture != nullptr)
        {
            SDL_DestroyTexture(texture); // 释放旧区块占用的显存
            texture = nullptr;           // 置空：下面的 assign 会整体重建
        }
    }
    m_chunkTextures.assign(static_cast<size_t>(m_chunksX) * static_cast<size_t>(m_chunksY),
                           nullptr); // 与区块数同长的新缓存（全部未加载）

    // 激活窗口复位为空窗口：生成后必须先经 updateActiveWindow 才有激活区
    m_activeMinX = 0;  // 空窗口约定：max < min
    m_activeMaxX = -1;
    m_activeMinY = 0;
    m_activeMaxY = -1;

    // 格数据整体重算（一维行主序数组；尺寸 = cellsX x cellsY）
    const size_t cellCount = static_cast<size_t>(cellsX()) * static_cast<size_t>(cellsY());
    m_walls.assign(cellCount, 0); // 墙体位图：从全空开始，按规则逐层写入
    m_floor.assign(cellCount, 0); // 地板变体：逐格哈希填充
    m_decor.assign(cellCount, 0); // 装饰：区块生成阶段填充

    // 出生保护区中心 = 世界中心格（玩家出生点所在的格；3072 世界即格 32）
    m_spawnCellX = cellsX() / 2; // 中心格 x
    m_spawnCellY = cellsY() / 2; // 中心格 y

    // ---- 第 1 层：地板变体（逐格哈希，确定性）----
    // 每格独立哈希取 0..7 的变体索引——布局由 (格子坐标, 种子) 唯一决定，
    // 「同一局内卸载再载入内容不变」的基础
    for (int cellY = 0; cellY < cellsY(); ++cellY)
    {
        for (int cellX = 0; cellX < cellsX(); ++cellX)
        {
            // 格哈希 -> 8 个变体之一（% 8 只需低位，splitmix64 输出位均匀性足够）
            const uint64_t hash = hashCell(m_seed, cellX, cellY, kSaltFloor);
            m_floor[static_cast<size_t>(cellY) * static_cast<size_t>(cellsX()) + static_cast<size_t>(cellX)] =
                static_cast<uint8_t>(hash % 8); // 变体索引 0..7（对应素材表 8 帧）
        }
    }

    // ---- 第 2 层：世界边界一圈墙 ----
    // 最外圈格恒为墙：世界的物理边界由墙自然形成（实体活动范围 = 墙内），
    // 相机钳制 + 边界墙双保险——玩家永远走不出世界，也看不到世界外的虚空
    for (int cellY = 0; cellY < cellsY(); ++cellY)
    {
        for (int cellX = 0; cellX < cellsX(); ++cellX)
        {
            // 处于最外圈（四边任一）：置墙
            if (cellX == 0 || cellY == 0 || cellX == cellsX() - 1 || cellY == cellsY() - 1)
            {
                m_walls[static_cast<size_t>(cellY) * static_cast<size_t>(cellsX()) + static_cast<size_t>(cellX)] = 1;
            }
        }
    }

    // ---- 第 3 层：每区块的随机墙结构 + 装饰 ----
    // 按区块派生独立随机序列（区块间互不影响：某区块的结构改动不会
    // 改变其他区块的布局——「卸载再载入一致」还叠加了「逐区块独立」）
    generateChunkStructures();
}

// 确定性哈希：splitmix64 终混合（雪崩性好——相邻坐标/种子得到完全不同的
// 序列，「同输入同输出」是确定性生成的地基；不同用途经 salt 分隔）
uint64_t World::hashCell(uint32_t seed, int cellX, int cellY, uint64_t salt)
{
    // 初始状态：盐作基座，依次混入种子与两个格坐标（乘奇数常量打散）
    uint64_t h = salt;                                             // 用途盐：基座
    h ^= static_cast<uint64_t>(seed) * 0xC2B2AE3D27D4EB4Full;      // 混入世界种子
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(cellX)) * 0xBF58476D1CE4E5B9ull; // 混入格 x
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(cellY)) * 0x94D049BB133111EBull; // 混入格 y

    // splitmix64 终混合三步（移位异或 + 乘法交替）：把输入差异雪崩到全 64 位
    h ^= h >> 30;                     // 高位扰动低位
    h *= 0xBF58476D1CE4E5B9ull;       // 乘法扩散
    h ^= h >> 27;                     // 再次扰动
    h *= 0x94D049BB133111EBull;       // 再扩散
    h ^= h >> 31;                     // 收尾混合
    return h;                         // 确定性的 64 位随机数
}

// 逐区块生成随机墙结构与装饰
void World::generateChunkStructures()
{
    for (int chunkY = 0; chunkY < m_chunksY; ++chunkY)
    {
        for (int chunkX = 0; chunkX < m_chunksX; ++chunkX)
        {
            // 区块随机序列：种子 = (世界种子, 区块坐标) 的哈希；后续用
            // splitmix64 逐步推进（每步一个确定性随机数，顺序固定 = 结果固定）
            uint64_t rngState = hashCell(m_seed, chunkX, chunkY, kSaltChunk);

            // 单步随机数生成（lambda 捕获状态）：splitmix64 推进 + 终混合
            const auto nextRandom = [&rngState]() -> uint64_t
            {
                rngState += 0x9E3779B97F4A7C15ull; // 黄金比例增量推进状态
                uint64_t z = rngState;             // 本步状态副本
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; // 混合第一轮
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; // 混合第二轮
                return z ^ (z >> 31);              // 输出 64 位随机数
            };

            // 本区块的墙结构数量：3~5（kStructuresMax - kStructuresMin + 1 = 3 种取值）
            const int structureCount =
                kStructuresMin + static_cast<int>(nextRandom() % (kStructuresMax - kStructuresMin + 1));

            for (int structure = 0; structure < structureCount; ++structure)
            {
                // 结构类型：0=水平短墙段 1=垂直短墙段 2=2x2 小块
                const int type = static_cast<int>(nextRandom() % 3);

                // 结构起点（区块内局部坐标 2..13——避开边缘两圈，交界通行
                // 由 setRandomWall 的边缘豁免再保一道）
                const int localX = kLocalMin + static_cast<int>(nextRandom() % kLocalSpan);
                const int localY = kLocalMin + static_cast<int>(nextRandom() % kLocalSpan);

                // 线段长度：2~4 格（type 2 的 2x2 块不使用长度）
                const int length =
                    kSegmentLenMin + static_cast<int>(nextRandom() % (kSegmentLenMax - kSegmentLenMin + 1));

                // 结构起点换算到全局格坐标（区块基址 = 区块索引 x 16 格）
                const int baseCellX = chunkX * kChunkCells + localX;
                const int baseCellY = chunkY * kChunkCells + localY;

                if (type == 2)
                {
                    // 2x2 小块：四格整体置墙（小体量障碍，绕行成本低）
                    for (int dy = 0; dy < 2; ++dy)
                    {
                        for (int dx = 0; dx < 2; ++dx)
                        {
                            setRandomWall(baseCellX + dx, baseCellY + dy); // 逐格尝试置墙
                        }
                    }
                }
                else if (type == 0)
                {
                    // 水平短墙段：沿 x 方向铺 length 格
                    for (int i = 0; i < length; ++i)
                    {
                        setRandomWall(baseCellX + i, baseCellY); // 水平推进
                    }
                }
                else
                {
                    // 垂直短墙段：沿 y 方向铺 length 格
                    for (int i = 0; i < length; ++i)
                    {
                        setRandomWall(baseCellX, baseCellY + i); // 垂直推进
                    }
                }
            }

            // ---- 本区块的装饰（纯视觉；地板之上、墙之下的摆件）----
            // 数量 1~3；位置与类型继续用同一随机序列（顺序固定 -> 确定）
            const int decorCount = kDecorMin + static_cast<int>(nextRandom() % (kDecorMax - kDecorMin + 1));
            for (int decor = 0; decor < decorCount; ++decor)
            {
                // 装饰格（区块内 2..13，与结构同域——避开边缘与出生区更省心）
                const int localX = kLocalMin + static_cast<int>(nextRandom() % kLocalSpan);
                const int localY = kLocalMin + static_cast<int>(nextRandom() % kLocalSpan);
                const int cellX = chunkX * kChunkCells + localX; // 全局格 x
                const int cellY = chunkY * kChunkCells + localY; // 全局格 y

                // 装饰类型：1=骷髅头 2=木箱（nextRandom 的消耗无条件执行——
                // 跳过与否只影响本格，不影响后续随机序列的推进顺序）
                const uint8_t decorType = static_cast<uint8_t>(1 + nextRandom() % 2);

                // 出生保护区不放装饰（出生点近旁保持干净）、墙上不放
                // （装饰只摆在地板上）；其余照摆
                if (inSpawnSafeZone(cellX, cellY) || wallAt(cellX, cellY))
                {
                    continue; // 本装饰作废（随机序列已正常推进，确定性不受影响）
                }
                m_decor[static_cast<size_t>(cellY) * static_cast<size_t>(cellsX()) + static_cast<size_t>(cellX)] = decorType;
            }
        }
    }
}

// 尝试把格 (cellX, cellY) 设为随机墙（带三重豁免）
void World::setRandomWall(int cellX, int cellY)
{
    // 豁免一：世界最外圈（边界墙由独立步骤恒置，随机结构不触碰它的语义）
    if (cellX <= 0 || cellY <= 0 || cellX >= cellsX() - 1 || cellY >= cellsY() - 1)
    {
        return; // 世界边界格：不参与随机结构
    }

    // 豁免二：区块边缘一圈（local 0 与 15）——相邻区块的交界因此永远
    // 不放随机墙，「区块交界保证可通行」的结构性保证
    if (cellX % kChunkCells == 0 || cellX % kChunkCells == kChunkCells - 1 ||
        cellY % kChunkCells == 0 || cellY % kChunkCells == kChunkCells - 1)
    {
        return; // 区块交界格：不参与随机结构
    }

    // 豁免三：出生保护区（世界中心 ±kSpawnSafeRadius 格）——玩家出生
    // 点与近旁绝不被墙封堵
    if (inSpawnSafeZone(cellX, cellY))
    {
        return; // 保护区内：不参与随机结构
    }

    // 通过全部豁免：置墙（已是墙则重复写入，无副作用）
    m_walls[static_cast<size_t>(cellY) * static_cast<size_t>(cellsX()) + static_cast<size_t>(cellX)] = 1;
}

// 是否处于出生保护区（世界中心格 ±kSpawnSafeRadius 的方形区域）
bool World::inSpawnSafeZone(int cellX, int cellY) const
{
    // 两轴分别判断闭区间 [中心-半径, 中心+半径]：都命中才算保护区内
    return cellX >= m_spawnCellX - kSpawnSafeRadius &&
           cellX <= m_spawnCellX + kSpawnSafeRadius &&
           cellY >= m_spawnCellY - kSpawnSafeRadius &&
           cellY <= m_spawnCellY + kSpawnSafeRadius;
}

// ============================================================
// 格查询
// ============================================================
bool World::wallAt(int cellX, int cellY) const
{
    // 未生成：无墙世界（防御——生成前查询不把实体卡死；调用方本就应
    // 先完成生成再布置实体）
    if (m_walls.empty())
    {
        return false;
    }

    // 世界外（含负值）：一律视为墙——「世界外即墙」是实体出不去的第一保证
    if (cellX < 0 || cellX >= cellsX() || cellY < 0 || cellY >= cellsY())
    {
        return true;
    }

    // 常规路径：行主序查位图（非 0 即墙）
    return m_walls[static_cast<size_t>(cellY) * static_cast<size_t>(cellsX()) + static_cast<size_t>(cellX)] != 0;
}

// 地板变体索引查询（0..7）；越界或未生成返回 0（防御——烘焙前会先判墙）
int World::floorVariantAt(int cellX, int cellY) const
{
    // 未生成或越界：返回 0（第 1 号变体；调用方按安全默认值处理）
    if (m_floor.empty() || cellX < 0 || cellX >= cellsX() || cellY < 0 || cellY >= cellsY())
    {
        return 0;
    }
    // 常规路径：行主序取变体索引
    return m_floor[static_cast<size_t>(cellY) * static_cast<size_t>(cellsX()) + static_cast<size_t>(cellX)];
}

// 装饰类型查询（0=无 1=骷髅头 2=木箱）；越界或未生成返回 0
int World::decorAt(int cellX, int cellY) const
{
    // 未生成或越界：返回 0（无装饰）
    if (m_decor.empty() || cellX < 0 || cellX >= cellsX() || cellY < 0 || cellY >= cellsY())
    {
        return 0;
    }
    // 常规路径：行主序取装饰类型
    return m_decor[static_cast<size_t>(cellY) * static_cast<size_t>(cellsX()) + static_cast<size_t>(cellX)];
}

// ============================================================
// 碰撞查询（格级）
// ============================================================
bool World::isRectWalkable(const SDL_FRect& rect) const
{
    // 亚像素容差：右/下边缘恰好压在格线上时不算「碰到下一格」——与引擎
    // rectsOverlap「刚好贴合不算碰撞」的约定一致（否则贴墙站位会有半像素
    // 级的伪碰撞，滑动观感变差）
    constexpr float kEdgeEpsilon = 0.01f;

    // 矩形覆盖的格范围：左上角向下取整；右下角减去容差再向下取整
    const int minCellX = cellFloored(rect.x);                        // 左边界格
    const int minCellY = cellFloored(rect.y);                        // 上边界格
    const int maxCellX = cellFloored(rect.x + rect.w - kEdgeEpsilon); // 右边界格（含）
    const int maxCellY = cellFloored(rect.y + rect.h - kEdgeEpsilon); // 下边界格（含）

    // 覆盖区域内任一格是墙 -> 不可走（全部无墙才可走）
    for (int cellY = minCellY; cellY <= maxCellY; ++cellY)
    {
        for (int cellX = minCellX; cellX <= maxCellX; ++cellX)
        {
            if (wallAt(cellX, cellY))
            {
                return false; // 命中墙体：矩形不可走
            }
        }
    }
    return true; // 覆盖范围全部无墙：可走
}

// 单点可走查询（出生/掉落落点的快速校验）
bool World::isPointWalkable(float x, float y) const
{
    // 点所在格无墙即可走（点正好在格线上归入右侧/下侧格——floor 语义）
    return !wallAt(cellFloored(x), cellFloored(y));
}

// 分轴位移 + 撞墙滑动（无额外实心体的便捷包装；行为与拆分前完全一致）
SDL_FPoint World::slideMove(float x, float y, float w, float h, float dx, float dy) const
{
    // 直接转发完整版：extraSolid = nullptr = 只查世界墙体（旧调用点零改动）
    return slideMoveEx(x, y, w, h, dx, dy, nullptr, nullptr);
}

// 分轴位移 + 撞墙滑动（带「额外实心体」的完整版，见头文件契约）
SDL_FPoint World::slideMoveEx(float x, float y, float w, float h, float dx, float dy,
                              ExtraSolidFn extraSolid, void* context) const
{
    // 零位移快速路径：不产生任何查询
    if (dx == 0.0f && dy == 0.0f)
    {
        return SDL_FPoint{ x, y }; // 原样返回
    }

    // 子步拆分：最大分量每超过半格拆一段（ceil 保证单子步 <= 半格 < 一格）——
    // 高速移动（如异常大的配置速度）不会一步「跳过」一整格墙
    const float maxComponent = std::max(std::fabs(dx), std::fabs(dy)); // 本步位移的最大轴分量
    int substeps = static_cast<int>(std::ceil(maxComponent / kMaxSubStep)); // 子步数（向上取整）
    if (substeps < 1)
    {
        substeps = 1; // 防御：极小位移也至少一段
    }

    // 每子步的轴位移（总位移均分；浮点除法不改变确定性）
    const float stepX = dx / static_cast<float>(substeps); // 子步 x 位移
    const float stepY = dy / static_cast<float>(substeps); // 子步 y 位移

    for (int i = 0; i < substeps; ++i)
    {
        // X 轴尝试：落点可走（且不与额外实心体重叠）才推进；被挡则停该轴
        const SDL_FRect nextX{ x + stepX, y, w, h }; // X 轴候选矩形
        // 墙判据 + 额外实心判据（extraSolid 为空 = 只查墙；短路顺序：
        // 先查墙——墙是静态数据、查询更便宜）
        const bool blockedX = !isRectWalkable(nextX) ||
                              (extraSolid != nullptr && extraSolid(context, nextX));
        if (!blockedX)
        {
            x += stepX; // 推进 X
        }

        // Y 轴尝试：用「X 推进后的位置」继续——一轴被挡、另一轴照走，
        // 天然产生「贴墙滑动」的手感（撞墙后沿墙滑行而不是原地卡死）
        const SDL_FRect nextY{ x, y + stepY, w, h }; // Y 轴候选矩形
        // 判据同 X 轴：墙 + 额外实心体（两条件都过才推进）
        const bool blockedY = !isRectWalkable(nextY) ||
                              (extraSolid != nullptr && extraSolid(context, nextY));
        if (!blockedY)
        {
            y += stepY; // 推进 Y
        }
    }

    return SDL_FPoint{ x, y }; // 子步收敛后的最终位置
}

// ============================================================
// 区块索引与激活窗口
// ============================================================
int World::chunkXFromWorldX(float x) const
{
    // 区块宽 = 768px：向下取整即区块索引（负值同样 floor 到更左的区块）
    return static_cast<int>(std::floor(x / kChunkPixels));
}

int World::chunkYFromWorldY(float y) const
{
    // 与横向同式：floor(y / 768)
    return static_cast<int>(std::floor(y / kChunkPixels));
}

// 重算激活窗口（纯逻辑）：中心所在区块 ±1，再 clamp 到世界内
void World::updateActiveWindow(float centerX, float centerY)
{
    // 中心点所在区块（越界先夹回世界——相机在极端位置也不产生非法窗口）
    const int centerChunkX = std::clamp(chunkXFromWorldX(centerX), 0, m_chunksX - 1); // 中心区块 x
    const int centerChunkY = std::clamp(chunkYFromWorldY(centerY), 0, m_chunksY - 1); // 中心区块 y

    // 窗口 = 中心 ±1（最多 3x3），再与世界边界交集
    m_activeMinX = std::max(centerChunkX - 1, 0);            // 左界不小于 0
    m_activeMaxX = std::min(centerChunkX + 1, m_chunksX - 1); // 右界不超过末区块
    m_activeMinY = std::max(centerChunkY - 1, 0);            // 上界不小于 0
    m_activeMaxY = std::min(centerChunkY + 1, m_chunksY - 1); // 下界不超过末区块
}

// 指定区块是否在激活窗口内（未计算窗口时窗口为空 -> 恒 false）
bool World::isChunkActive(int chunkX, int chunkY) const
{
    // 闭区间判断：两轴都命中才算激活
    return chunkX >= m_activeMinX && chunkX <= m_activeMaxX &&
           chunkY >= m_activeMinY && chunkY <= m_activeMaxY;
}

// 世界坐标点是否处于激活区块内（怪物更新/渲染/出生的门控查询）
bool World::isPointInActiveChunks(float x, float y) const
{
    // 点的区块索引直接进激活判断（世界外/未计算窗口都得到 false）
    return isChunkActive(chunkXFromWorldX(x), chunkYFromWorldY(y));
}

// 玩家出生点（让实体中心落在世界中心）
SDL_FPoint World::spawnPointFor(float entityWidth, float entityHeight) const
{
    // 左上角 = 世界中心 - 实体半尺寸（实体中心对齐世界中心）
    return SDL_FPoint{ pixelWidth() * 0.5f - entityWidth * 0.5f,
                       pixelHeight() * 0.5f - entityHeight * 0.5f };
}

// ============================================================
// 素材加载与区块烘焙（运行期路径；需要 renderer）
// ============================================================
bool World::loadAssets(SDL_Renderer* renderer)
{
    // 重复加载：先清旧素材（幂等；换关/换素材的场景）
    releaseAssets();

    // 素材路径以 exe 所在目录为基准（与引擎一致：SDL_GetBasePath 拼 assets/）
    const char* basePath = SDL_GetBasePath();
    if (basePath == nullptr)
    {
        // 拿不到基路径（极端环境）：警告并整体失败——烘焙退化为纯色色块
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "世界素材加载失败：无法获取可执行文件目录");
        return false;
    }
    const std::string base(basePath); // 拷贝基路径（后续不再依赖 SDL 内部缓存）

    // 逐张加载三份世界素材（失败非致命：对应层在烘焙时退化为色块）
    m_floorTiles = IMG_LoadTexture(renderer, (base + "assets/tiles_floor.png").c_str()); // 地板变体表
    m_wallTile = IMG_LoadTexture(renderer, (base + "assets/tiles_wall.png").c_str());   // 墙块单帧
    m_decorSheet = IMG_LoadTexture(renderer, (base + "assets/decor_sheet.png").c_str()); // 装饰表

    // 像素风必需：与实体素材同一采样口径——最近邻，放大保持像素锐利
    if (m_floorTiles != nullptr)
    {
        SDL_SetTextureScaleMode(m_floorTiles, SDL_SCALEMODE_NEAREST); // 地板：最近邻
    }
    else
    {
        // 单张失败单独警告（其余素材照常参与烘焙）
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "加载世界地砖 tiles_floor.png 失败（%s），地板将退化为纯色", SDL_GetError());
    }
    if (m_wallTile != nullptr)
    {
        SDL_SetTextureScaleMode(m_wallTile, SDL_SCALEMODE_NEAREST); // 墙块：最近邻
    }
    else
    {
        // 墙块缺失：烘焙时墙格画深色色块
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "加载世界墙块 tiles_wall.png 失败（%s），墙体将退化为纯色", SDL_GetError());
    }
    if (m_decorSheet != nullptr)
    {
        SDL_SetTextureScaleMode(m_decorSheet, SDL_SCALEMODE_NEAREST); // 装饰：最近邻
    }
    else
    {
        // 装饰缺失：直接不画装饰（纯视觉，缺了不影响玩法）
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "加载世界装饰 decor_sheet.png 失败（%s），装饰将跳过", SDL_GetError());
    }

    // 返回「三张全部就绪」与否（调用方可用于日志；游戏照常可玩）
    return m_floorTiles != nullptr && m_wallTile != nullptr && m_decorSheet != nullptr;
}

// 释放素材纹理（loadAssets 重加载前与 release 内部调用；幂等）
void World::releaseAssets()
{
    // 逐张判空销毁：三张素材彼此独立
    if (m_floorTiles != nullptr)
    {
        SDL_DestroyTexture(m_floorTiles); // 释放地板变体表
        m_floorTiles = nullptr;           // 置空：幂等
    }
    if (m_wallTile != nullptr)
    {
        SDL_DestroyTexture(m_wallTile); // 释放墙块
        m_wallTile = nullptr;           // 置空：幂等
    }
    if (m_decorSheet != nullptr)
    {
        SDL_DestroyTexture(m_decorSheet); // 释放装饰表
        m_decorSheet = nullptr;           // 置空：幂等
    }
}

// 释放某个区块的纹理并置空（下标越界安全；重复调用安全）
void World::destroyChunkTexture(int index)
{
    // 下标合法性：越界直接返回（调用方错误不崩）
    if (index < 0 || static_cast<size_t>(index) >= m_chunkTextures.size())
    {
        return;
    }
    // 非空才销毁（空槽位跳过）
    if (m_chunkTextures[static_cast<size_t>(index)] != nullptr)
    {
        SDL_DestroyTexture(m_chunkTextures[static_cast<size_t>(index)]); // 释放区块纹理显存
        m_chunkTextures[static_cast<size_t>(index)] = nullptr;           // 置空：等待下次激活重烘焙
    }
}

// 按相机推进激活窗口：进入窗口的区块烘焙、滑出窗口的区块卸载
void World::updateActiveChunks(SDL_Renderer* renderer, const Camera& camera)
{
    // 1) 以「相机视图中心」为基准重算窗口（相机位置 = 视口左上角）
    const Vec2 cameraCorner = camera.position();                                    // 相机左上角（世界坐标）
    const float centerX = cameraCorner.x + camera.viewportWidth() * 0.5f;           // 视图中心 x
    const float centerY = cameraCorner.y + camera.viewportHeight() * 0.5f;          // 视图中心 y
    updateActiveWindow(centerX, centerY);                                           // 窗口计算（纯逻辑）

    // 2) 卸载：滑出窗口且已加载的区块——释放纹理并记录日志
    for (int chunkY = 0; chunkY < m_chunksY; ++chunkY)
    {
        for (int chunkX = 0; chunkX < m_chunksX; ++chunkX)
        {
            if (isChunkActive(chunkX, chunkY))
            {
                continue; // 窗口内：保留
            }
            const int index = chunkY * m_chunksX + chunkX; // 缓存下标
            if (m_chunkTextures[static_cast<size_t>(index)] != nullptr)
            {
                destroyChunkTexture(index); // 释放纹理
                // 冒烟观测点：区块卸载日志（自动化测试据它断言卸载发生）
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "区块 (%d,%d) 卸载", chunkX, chunkY);
            }
        }
    }

    // 3) 加载：窗口内尚未烘焙的区块——烘焙并记录日志
    for (int chunkY = m_activeMinY; chunkY <= m_activeMaxY; ++chunkY)
    {
        for (int chunkX = m_activeMinX; chunkX <= m_activeMaxX; ++chunkX)
        {
            const int index = chunkY * m_chunksX + chunkX; // 缓存下标
            // renderer 为空或已加载：跳过（渲染器缺失时无从烘焙）
            if (renderer == nullptr || m_chunkTextures[static_cast<size_t>(index)] != nullptr)
            {
                continue;
            }
            m_chunkTextures[static_cast<size_t>(index)] = bakeChunk(renderer, chunkX, chunkY); // 烘焙
            if (m_chunkTextures[static_cast<size_t>(index)] != nullptr)
            {
                // 冒烟观测点：区块加载日志（自动化测试据它断言加载发生）
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "区块 (%d,%d) 加载", chunkX, chunkY);
            }
            // 烘焙失败（显存/渲染目标异常）：不缓存，下一帧自动重试
        }
    }
}

// 烘焙单个区块：地板 -> 装饰 -> 墙，逐格绘制一次（之后每帧只画 1 张纹理）
SDL_Texture* World::bakeChunk(SDL_Renderer* renderer, int chunkX, int chunkY) const
{
    // renderer 缺失：无从烘焙（调用方已防御，这里再保一道）
    if (renderer == nullptr)
    {
        return nullptr;
    }

    // 创建 768x768 渲染目标纹理（一次创建、多次复用：区块不重烘焙）
    SDL_Texture* target = SDL_CreateTexture(renderer,
                                            SDL_PIXELFORMAT_RGBA8888,      // 通用 32 位 RGBA
                                            SDL_TEXTUREACCESS_TARGET,      // 可作渲染目标
                                            static_cast<int>(kChunkPixels), // 宽 768
                                            static_cast<int>(kChunkPixels)); // 高 768
    if (target == nullptr)
    {
        // 创建失败（显存不足等）：警告并放弃本次（下一帧重试）
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "区块 (%d,%d) 烘焙失败：%s", chunkX, chunkY, SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureScaleMode(target, SDL_SCALEMODE_NEAREST); // 像素风：最近邻（与实体一致）

    // 切换渲染目标；失败则销毁纹理并返回（renderer 目标未被改动，安全）
    if (!SDL_SetRenderTarget(renderer, target))
    {
        // 切换失败：警告、销毁、放弃
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "区块 (%d,%d) 烘焙失败（设置渲染目标）：%s", chunkX, chunkY, SDL_GetError());
        SDL_DestroyTexture(target); // 释放刚创建的纹理
        return nullptr;
    }

    // 区块的全局格基址（= 区块索引 x 16 格）
    const int baseCellX = chunkX * kChunkCells; // 本区块左上角格 x
    const int baseCellY = chunkY * kChunkCells; // 本区块左上角格 y

    // ---- 第 1 层：地板（铺满 16x16 格）----
    if (m_floorTiles == nullptr)
    {
        // 素材缺失兜底：深灰蓝色块（一次性设色，循环内只填充）
        SDL_SetRenderDrawColor(renderer, 45, 42, 60, 255); // 地板兜底色（深灰蓝）
    }
    for (int cellLY = 0; cellLY < kChunkCells; ++cellLY)
    {
        for (int cellLX = 0; cellLX < kChunkCells; ++cellLX)
        {
            // 本格在区块纹理内的目标矩形（局部格坐标 x 48px）
            const SDL_FRect dst{ cellLX * kCellSize, cellLY * kCellSize, kCellSize, kCellSize };

            if (m_floorTiles != nullptr)
            {
                // 变体帧的源矩形：第 variant 帧的横向起点 = variant x 48px
                const int variant = floorVariantAt(baseCellX + cellLX, baseCellY + cellLY); // 本格变体
                const SDL_FRect src{ variant * kCellSize, 0.0f, kCellSize, kCellSize };     // 源帧矩形
                SDL_RenderTexture(renderer, m_floorTiles, &src, &dst); // 裁帧绘制（1:1 无缩放）
            }
            else
            {
                // 兜底：纯色填充本格
                SDL_RenderFillRect(renderer, &dst);
            }
        }
    }

    // ---- 第 2 层：装饰（地板之上、墙之下；先画好让墙可以覆盖装饰溢出）----
    if (m_decorSheet != nullptr)
    {
        for (int cellLY = 0; cellLY < kChunkCells; ++cellLY)
        {
            for (int cellLX = 0; cellLX < kChunkCells; ++cellLX)
            {
                const int decor = decorAt(baseCellX + cellLX, baseCellY + cellLY); // 本格装饰类型
                if (decor == 0)
                {
                    continue; // 无装饰：跳过
                }

                // 装饰帧的源矩形与绘制尺寸（与组装脚本约定一致）：
                //   skull（类型 1）：源 x=0，48x48（恰好一格高，顶到格底）
                //   crate（类型 2）：源 x=48，48x72（比一格高 24px，底部对齐格底）
                const float srcX = (decor == 1) ? 0.0f : kCellSize;          // 帧横向偏移（两帧排布：骷髅在左、木箱在右）
                const float srcH = (decor == 1) ? kCellSize : kCellSize * 1.5f; // 帧高（48 或 72）
                const SDL_FRect src{ srcX, 0.0f, kCellSize, srcH };          // 源帧矩形（宽恒一格）

                // 目标：底部对齐格底（高装饰向上溢出，溢出区稍后被墙覆盖也不碍）
                const SDL_FRect dst{ cellLX * kCellSize,                          // 水平起点 = 格左缘
                                     cellLY * kCellSize + kCellSize - srcH,       // 底对齐格底
                                     kCellSize, srcH };                           // 宽高 = 帧尺寸
                SDL_RenderTexture(renderer, m_decorSheet, &src, &dst); // 绘制装饰
            }
        }
    }

    // ---- 第 3 层：墙（最上层）----
    if (m_wallTile == nullptr)
    {
        // 素材缺失兜底：更深一层的色块（与地板可区分）
        SDL_SetRenderDrawColor(renderer, 58, 52, 72, 255); // 墙兜底色（深紫灰）
    }
    for (int cellLY = 0; cellLY < kChunkCells; ++cellLY)
    {
        for (int cellLX = 0; cellLX < kChunkCells; ++cellLX)
        {
            if (!wallAt(baseCellX + cellLX, baseCellY + cellLY))
            {
                continue; // 非墙格：跳过
            }
            // 墙格的目标矩形（整格绘制）
            const SDL_FRect dst{ cellLX * kCellSize, cellLY * kCellSize, kCellSize, kCellSize };
            if (m_wallTile != nullptr)
            {
                // 墙块单帧纹理：src 传 nullptr = 使用整张纹理
                SDL_RenderTexture(renderer, m_wallTile, nullptr, &dst);
            }
            else
            {
                // 兜底：纯色填充墙格
                SDL_RenderFillRect(renderer, &dst);
            }
        }
    }

    // 恢复默认渲染目标（回到窗口）：烘焙完成后渲染链状态归位
    SDL_SetRenderTarget(renderer, nullptr);
    return target; // 烘焙完成：返回可缓存的区块纹理
}

// 绘制激活窗口内、与相机视口相交的区块纹理
void World::render(SDL_Renderer* renderer, const Camera& camera) const
{
    // 视口尺寸（屏幕裁剪基准：与视口不相交的区块不绘制）
    const float viewW = camera.viewportWidth();  // 视口宽
    const float viewH = camera.viewportHeight(); // 视口高

    for (int chunkY = m_activeMinY; chunkY <= m_activeMaxY; ++chunkY)
    {
        for (int chunkX = m_activeMinX; chunkX <= m_activeMaxX; ++chunkX)
        {
            const int index = chunkY * m_chunksX + chunkX; // 缓存下标
            // 空槽位（未加载/卸载后）：跳过
            if (m_chunkTextures[static_cast<size_t>(index)] == nullptr)
            {
                continue;
            }

            // 区块左上角的世界坐标 -> 屏幕坐标（相机换算）
            const Vec2 screen = camera.worldToScreen(chunkX * kChunkPixels, chunkY * kChunkPixels);

            // 目标矩形（1:1 尺寸：区块 768px 对应屏幕 768px）
            const SDL_FRect dst{ screen.x, screen.y, kChunkPixels, kChunkPixels };

            // 视口裁剪：完全在视口外的区块不提交绘制（每帧最多 4 块可见，
            // 不必为不可见区块付填充率）
            if (dst.x + dst.w <= 0.0f || dst.y + dst.h <= 0.0f ||
                dst.x >= viewW || dst.y >= viewH)
            {
                continue; // 与视口无交集：跳过
            }

            // 提交绘制（整张区块纹理，无源裁剪）
            SDL_RenderTexture(renderer, m_chunkTextures[static_cast<size_t>(index)], nullptr, &dst);
        }
    }
}

// 释放全部纹理（区块 + 素材；幂等）
void World::release()
{
    // 区块纹理：逐个槽位释放（destroyChunkTexture 自身判空/判界）
    for (size_t i = 0; i < m_chunkTextures.size(); ++i)
    {
        destroyChunkTexture(static_cast<int>(i));
    }
    // 素材纹理：三张整体释放
    releaseAssets();
}

// 单轴格换算：像素 -> 格索引（floor 语义：负坐标也向下取整到更小格）
int World::cellFloored(float worldPixel)
{
    // 除以格宽向下取整即格索引（48px 一格）
    return static_cast<int>(std::floor(worldPixel / kCellSize));
}
