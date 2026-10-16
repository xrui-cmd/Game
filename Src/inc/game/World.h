#ifndef __WORLD_H__
#define __WORLD_H__

#include <SDL3/SDL.h> // SDL_Renderer / SDL_Texture / SDL_FRect：烘焙与碰撞查询的基础类型
#include <cstdint>    // uint32_t / uint64_t：世界种子与确定性哈希的整数类型
#include <vector>     // std::vector：格数据与区块纹理缓存（定长，下标即区块编号）

// 前置声明：激活窗口按「相机视图中心」计算（完整定义见 engine/Camera.h）；
// 本头文件只以引用接收相机，不引入完整定义
class Camera;

// ============================================================
// World：游戏世界（确定性生成 + 区块激活窗口 + 格级碰撞查询）
// ============================================================
//
// 本批（批六甲·世界重构）把「4x4 区块 x 16 格」扩大为「8x4 区块 x 32 格」，
// 世界从 3072x3072 升级为 12288x6144 像素并切分为两个风格区（西草地 /
// 东雪山，分区语义见 game/Zone.h）。世界是纯数据 + 纹理烘焙的管理者，
// 不持有任何玩法实体：
//
//   1. 世界结构：chunksX x chunksY 个区块，每区块 32x32 格、每格 48px
//      （默认 8x4 区块 = 12288x6144 像素 = 256x128 格）；区块纹理
//      1536x1536。激活窗口最多 3x3 块 => 显存占用约 3x3x1536x1536x4B
//      ≈ 85MB（Debug 量化评估见批六甲报告；如后续有压力，优化路径：
//      ①区块纹理减半采样（768² 烘焙 @2x 最近邻放大，显存约 21MB，
//      像素风观感损失极小）；②把 ±1 窗口改为按需增长）；
//   2. 确定性生成：地板变体 / 墙体布局 / 装饰全部由「格坐标 + 世界种子」
//      哈希产生——同样的种子逐格一致（同一局内卸载再载入内容不变），
//      不同种子得到不同布局（0 种子 = 每局随机，由调用方换一个非 0 种子）；
//   3. 墙体规则（批六甲·区块放大后按面积调整）：
//      - 世界最外圈格恒为墙（世界边界由墙自然形成，实体活动范围 = 墙内；
//        批六甲后续一笔将改为海洋水域——本笔先落区块放大与分区）；
//      - 每区块确定性随机生成 6~10 个「短墙段 / 直角块 / 2x2 块」结构
//        （线段 3~7 格；32 格区块的密度观感与旧 16 格区块 3~5 个相当），
//        密度适中不堵死；
//      - 区块边缘一圈格不放随机墙（相邻区块交界因此永远可通行）；
//      - 玩家出生点（草地区中心）周围 ±3 格为保护区（出生点绝不卡墙）；
//   4. 激活窗口：以相机视图中心所在区块为基准 ±1（最多 3x3）。激活时把
//      「地板 + 装饰 + 墙」烘焙成一张 1536x1536 区块纹理（构建一次）；
//      滑出窗口的区块释放纹理。实体（怪物）是否更新/渲染由场景按
//      isPointInActiveChunks 门控——非激活区暂停但不删除；
//   5. 格级碰撞：isRectWalkable 查「矩形覆盖的全部格是否无墙」；
//      slideMove 提供「分轴位移 + 撞墙滑动」的纯函数——玩家与怪物共用。
//
// 坐标约定（与 Camera 一致）：
//   - 世界坐标（像素）：世界左上角为原点，y 向下；格索引 = floor(像素 / 48)；
//   - 区块索引 = floor(像素 / kChunkPixels)；区块与格皆可为负（表示世界外）——
//     wallAt 对世界外统一按「墙」处理（实体永远出不去）。
//
// 素材（由 tools/compose_sprites.ps1 从 0x72 素材包组装）：
//   assets/tiles_floor.png  地板变体表（8 帧 x 48px，横向排列）
//   assets/tiles_wall.png   墙块单帧（48x48）
//   assets/decor_sheet.png  装饰表（skull 48x48 @x=0；crate 48x72 @x=48）
//   任何素材缺失都不崩溃：烘焙退化为纯色色块（地板深灰、墙更深）——
//   延续「资源问题不让游戏崩溃」的全局降级契约。
// ============================================================

class World
{
public:
    // ---- 几何常量（世界结构的一部分：代码固定，改动须连同注释一起审视）----
    static constexpr int kChunkCells = 32;    // 每区块边长（格）：32x32 格（批六甲：16 -> 32）
    static constexpr float kCellSize = 48.0f; // 每格边长（像素）：16px 素材 x 3 倍放大
    // 每区块边长（像素）：1536 = 32 格 x 48px。区块纹理尺寸、区块索引换算都用它
    static constexpr float kChunkPixels = kChunkCells * kCellSize;

    // 出生保护区半径（格）：以世界中心格为中心、±kSpawnSafeRadius 的方形区域
    // 内不放随机墙与装饰——玩家出生点与近旁永远开阔（半径 3 -> 7x7 格）
    static constexpr int kSpawnSafeRadius = 3;

    World() = default;

    // 析构：兜底释放全部纹理（释放区块纹理 + 素材纹理，幂等）
    ~World();

    // 生成世界（纯逻辑：不依赖 renderer，可在无窗口环境单测）：
    // 决定尺寸、逐格地板变体、边界墙、每区块随机结构、装饰与保护区。
    // 可重复调用（同一对象换种子重生成）：旧区块纹理会被丢弃，格数据重算。
    // chunksX/chunksY 至少为 1（防御夹取）；seed 为 0 时调用方应先换成
    // 非 0 随机值——本函数不知道「每局随机」的语义，只忠实使用传入值
    void generate(int chunksX, int chunksY, uint32_t seed);

    // ---- 尺寸查询（生成后有效；未生成时全部为 0）----
    int chunksX() const { return m_chunksX; }           // 横向区块数
    int chunksY() const { return m_chunksY; }           // 纵向区块数
    int cellsX() const { return m_chunksX * kChunkCells; } // 横向格数（= 区块数 x 16）
    int cellsY() const { return m_chunksY * kChunkCells; } // 纵向格数
    float pixelWidth() const { return cellsX() * kCellSize; }  // 世界总宽（像素）
    float pixelHeight() const { return cellsY() * kCellSize; } // 世界总高（像素）
    uint32_t seed() const { return m_seed; }            // 本局种子（日志/复现用）

    // ---- 格查询 ----
    // 指定格是否墙。语义约定（两个边界各有理由）：
    //   - 坐标越界（世界外）：恒 true——「世界外即墙」，实体永远出不去；
    //   - 未生成（维度为 0）：恒 false——防御性「无墙世界」，生成前查询
    //     不会把实体卡死（调用方本就应在布置实体前完成生成）
    bool wallAt(int cellX, int cellY) const;

    // 指定格的地板变体索引（0..7，对应 tiles_floor.png 的 8 帧）。
    // 越界返回 0（防御；渲染前会先做墙判断，不会真用到越界值）
    int floorVariantAt(int cellX, int cellY) const;

    // 指定格的装饰类型：0=无 1=骷髅头 2=木箱。越界返回 0
    int decorAt(int cellX, int cellY) const;

    // ---- 碰撞查询（格级；玩家与怪物共用）----

    // 矩形（世界坐标）是否可走：其覆盖的全部格都不是墙。
    // 边界贴合不算碰撞（右/下边缘减 0.01px 的亚像素容差——与引擎
    // rectsOverlap 的「刚好贴合不算碰撞」同一约定）。世界外视为墙
    bool isRectWalkable(const SDL_FRect& rect) const;

    // 单点是否可走（出生/掉落落点的快速校验）
    bool isPointWalkable(float x, float y) const;

    // 分轴位移 + 撞墙滑动（纯函数，不改内部状态）：
    //   1. 先把总位移按「单子步不超过半格」拆分（防高速穿透窄墙）；
    //   2. 每个子步内先试 X 轴、再试 Y 轴：该轴落点可走才推进，
    //      被墙挡则停该轴——「一轴被挡、另一轴照走」天然产生贴墙滑动；
    // 返回位移后的左上角坐标。玩家与怪物的移动都走这里（行为一致，
    // 单测覆盖「滑动」「不穿透」两个关键性质）。
    // 本批起它是 slideMoveEx 的「无额外实心」便捷包装（行为完全不变）
    SDL_FPoint slideMove(float x, float y, float w, float h, float dx, float dy) const;

    // 「额外实心体」判定回调（批四·体积碰撞）：给定候选矩形（世界坐标），
    // 返回它是否与某个体积障碍（怪物碰撞盒 / 玩家碰撞盒）重叠。
    // context 由调用方原样透传（通常传实体自身，回调内还原后查内部状态）
    using ExtraSolidFn = bool (*)(void* context, const SDL_FRect& rect);

    // 分轴位移 + 撞墙滑动（带「额外实心体」的完整版）：
    // 与 slideMove 同一套子步/分轴算法，只是「可走」判据多一条——
    // 候选落点还不能与 extraSolid 指定的实心体重叠（不重叠才推进）。
    // 用途：玩家移动时把怪物碰撞盒当墙（撞上滑停）、怪物移动时把玩家
    // 碰撞盒当墙——实体互碰的「不可重合」由滑动截停自然保证。
    // extraSolid 传 nullptr = 只查世界墙体（等价于 slideMove）
    SDL_FPoint slideMoveEx(float x, float y, float w, float h, float dx, float dy,
                           ExtraSolidFn extraSolid, void* context) const;

    // ---- 区块索引与激活窗口 ----

    // 世界坐标 -> 区块索引（floor 语义：负坐标/世界外返回负值或超界值，
    // 调用方按需再 clamp）。格坐标也有对应换算（测试与生成内部使用）
    int chunkXFromWorldX(float x) const; // = floor(x / kChunkPixels)
    int chunkYFromWorldY(float y) const; // = floor(y / kChunkPixels)

    // 重算激活窗口（纯逻辑，不触碰纹理）：以 (centerX, centerY)（通常为
    // 相机视图中心）所在区块为基准取 ±1，再 clamp 到世界内。
    // 这是「窗口」的唯一计算点——updateActiveChunks 与测试都经它
    void updateActiveWindow(float centerX, float centerY);

    // 指定区块是否在激活窗口内（未计算窗口时恒 false）
    bool isChunkActive(int chunkX, int chunkY) const;

    // 世界坐标点是否处于激活区块内（怪物更新/渲染/出生的门控查询）
    bool isPointInActiveChunks(float x, float y) const;

    // 激活窗口范围查询（渲染遍历/日志用）
    int activeMinChunkX() const { return m_activeMinX; }
    int activeMaxChunkX() const { return m_activeMaxX; }
    int activeMinChunkY() const { return m_activeMinY; }
    int activeMaxChunkY() const { return m_activeMaxY; }

    // 玩家出生点（草地区中心）的左上角坐标（给定实体尺寸）：
    // 让实体「中心」落在草地区出生中心——批六甲起出生点不再取「几何
    // 世界中心」（8x4 世界的几何中心恰在草地/雪山分界线上），而是
    // zoneDef(Grass).respawn（草地区中心）；出生落点由保护区保证可走。
    // 自定义小世界（复活点落在世界外/边缘时）：中心点按「保护区半径 +
    // 2 格」的边距夹到世界内部（见实现），保证任何合法世界尺寸都有
    // 可用的出生点
    SDL_FPoint spawnPointFor(float entityWidth, float entityHeight) const;

    // ---- 素材与烘焙（需要 renderer 的运行期路径）----

    // 加载三张世界素材（相对 exe 目录，最近邻采样）。任一张失败只警告；
    // 返回是否全部成功（失败时烘焙退化为纯色色块，游戏照常可玩）
    bool loadAssets(SDL_Renderer* renderer);

    // 按相机推进激活窗口：进入窗口的区块立即烘焙（打日志「区块 (x,y) 加载」），
    // 滑出窗口的区块释放纹理（打日志「区块 (x,y) 卸载」）。
    // 每固定步在相机更新之后调用；内部先 updateActiveWindow 再 diff
    void updateActiveChunks(SDL_Renderer* renderer, const Camera& camera);

    // 绘制激活窗口内、与相机视口相交的区块纹理（世界 -> 屏幕换算 + 裁剪）
    void render(SDL_Renderer* renderer, const Camera& camera) const;

    // 释放全部纹理（区块 + 素材；幂等）。析构函数兜底调用；
    // 场景 shutdown 阶段（renderer 销毁前）也可主动调用
    void release();

private:
    // 确定性哈希：把 (种子, 格 x, 格 y, 用途盐) 混合成 64 位随机数。
    // splitmix64 终混合（雪崩性好）——「同输入同输出」是确定性生成的地基；
    // 盐用于不同用途（地板/区块序列/装饰）在相同坐标下取到不同序列
    static uint64_t hashCell(uint32_t seed, int cellX, int cellY, uint64_t salt);

    // 逐个区块生成随机墙结构（内部按区块派生独立随机序列——区块间互不影响，
    // 「同一局内卸载再载入一致」由确定性直接保证）
    void generateChunkStructures();

    // 尝试把格 (cellX, cellY) 设为随机墙：跳过世界边界/区块边缘一圈/
    // 出生保护区（这些区域的通行性是硬保证），其余情形置墙（已墙则无操作）
    void setRandomWall(int cellX, int cellY);

    // 是否处于出生保护区（出生中心格 ±kSpawnSafeRadius；批六甲起中心为
    // 草地区中心，见 spawnCenter）
    bool inSpawnSafeZone(int cellX, int cellY) const;

    // 出生中心点（世界坐标）：= 草地区复活点（zoneDef(Grass).respawn），
    // 经「保护区半径 + 2 格」的边距夹到世界内部（自定义小世界防御——
    // 复活点可能落在世界外/边缘；夹取后仍保证够建保护区）。
    // 某轴不足以容纳两侧边距时退回该轴几何中心。
    // generate（保护区中心）与 spawnPointFor 共用本函数——两处永远同源
    SDL_FPoint spawnCenter() const;

    // 烘焙单个区块为 1536x1536 纹理（地板 -> 装饰 -> 墙，逐格绘制一次）。
    // 失败返回 nullptr（调用方下帧重试）；素材缺失时对应层退化为纯色色块
    SDL_Texture* bakeChunk(SDL_Renderer* renderer, int chunkX, int chunkY) const;

    // 释放某个区块的纹理并置空（下标 = by * chunksX + bx；越界安全）
    void destroyChunkTexture(int index);

    // 释放素材纹理（loadAssets 重新加载前也调用；幂等）
    void releaseAssets();

    // 单轴格换算：像素 -> 格索引（floor 语义，负值正确向下取整）
    static int cellFloored(float worldPixel);

private:
    // ---- 生成参数 ----
    int m_chunksX = 0;     // 横向区块数（generate 设置；0 = 未生成）
    int m_chunksY = 0;     // 纵向区块数
    uint32_t m_seed = 0;   // 世界种子（确定性来源；日志/复现查询）
    int m_spawnCellX = 0;  // 出生保护区中心格 x（世界中心格）
    int m_spawnCellY = 0;  // 出生保护区中心格 y

    // ---- 格数据（行主序：下标 = cellY * cellsX + cellX；尺寸 cellsX x cellsY）----

    // 墙体位图：非 0 = 墙。碰撞查询的唯一数据源（装饰不参与碰撞）
    std::vector<uint8_t> m_walls;

    // 地板变体索引（0..7）：烘焙时从变体表裁对应帧——逐格确定性挑选，
    // 避免大面积平铺同一块砖的死板重复
    std::vector<uint8_t> m_floor;

    // 装饰类型（0=无 1=骷髅头 2=木箱）：纯视觉摆件，不参与碰撞
    std::vector<uint8_t> m_decor;

    // ---- 激活窗口（含端点的闭区间；未计算时 max < min = 空窗口）----
    int m_activeMinX = 0; // 窗口左界（区块索引）
    int m_activeMaxX = -1;
    int m_activeMinY = 0; // 窗口上界（区块索引）
    int m_activeMaxY = -1;

    // ---- 纹理缓存（需要 renderer 的运行期资源）----

    // 区块纹理缓存：下标 = by * chunksX + bx；nullptr = 未激活/未烘焙。
    // 尺寸与区块数一致（区块数很小，定长向量 + 下标寻址足够直观）
    std::vector<SDL_Texture*> m_chunkTextures;

    SDL_Texture* m_floorTiles = nullptr; // 地板变体表（8 帧 x 48px 横向排列）
    SDL_Texture* m_wallTile = nullptr;   // 墙块单帧（48x48）
    SDL_Texture* m_decorSheet = nullptr; // 装饰表（skull 48x48 @0；crate 48x72 @48）
};

#endif // !__WORLD_H__
