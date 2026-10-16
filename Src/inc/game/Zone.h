#ifndef __ZONE_H__
#define __ZONE_H__

#include "game/World.h" // World::kChunkCells / kChunkPixels：区块几何常量的唯一来源（分区按区块划界）
#include <SDL3/SDL.h>   // SDL_FRect：矩形版分区查询的参数类型（经 World.h 已引入，此处显式写明意图）
#include <cmath>        // std::floor：世界坐标 -> 区块索引换算（负值同样向下取整）

// ============================================================
// Zone：世界分区表与查询（批六甲·双区世界「草地 / 雪山」）
// ============================================================
//
// 背景（批六甲·世界重构）：世界从 4x4 区块扩大为 8x4 区块
// （12288x6144 像素；每区块 32x32 格 x 48px = 1536x1536 像素），并按
// x 轴切成两个风格区：
//   - 西侧 4x4 区块 = 草地（初级区，怪物等级区间 1~20；沿用现有地牢素材）；
//   - 东侧 4x4 区块 = 雪山（高级区，怪物等级区间 21~30；冰霜重染色素材）。
//
// 本批（甲）落地内容：
//   1. 分区数据：区矩形（按区块）、区名、等级区间、复活点、区内素材集、
//      玩家 debuff（雪地移速乘区）——全部收进下面的 ZoneDef 表（代码表，
//      与卡表/法术表同一纪律：可单测、单一事实来源）；
//   2. 查询 API：zoneAtWorld / zoneAtChunk / zoneAtRect——「任一点/矩形
//      属于哪个区」的唯一入口（World 烘焙选素材、PlayScene 横幅/日志/
//      debuff 都经它）；边界（分界线上）归雪山，世界外按最近区兜底；
//   3. 消费方：World 按「本格所属区」挑地砖/墙/装饰素材表；PlayScene
//      进入新区时弹区名横幅 + 打「进入区域」日志 + 注入移速乘区。
//
// 复活点说明（批六乙·复活流预留）：东（雪）区复活点取「靠草地一侧
// 中部」（雪山西缘半个区块处、纵向正中）；草地复活点即草地区中心
// （同时是出生保护区所在——玩家出生点与复活点同址）。
// 怪物等级区间本批仅作数据储备（怪物等级/生态分化归批六乙）。
//
// 「y 轴不分区」：两个区都是纵向铺满世界的竖带；ZoneDef 只存 x 区间。
// 自定义 world_chunks 尺寸时查询仍按 x 分界（东侧一律雪山）——
// 布局以默认 8x4 为准，尺寸改动不改变分区语义。
// ============================================================

// 区间标识：顺序即表顺序（草地在前——世界西侧；哨兵 Count 供循环/防越界）
enum class ZoneId
{
    Grass = 0, // 草地（西侧 4x4 区块；初级区 1~20 级）
    Snow,      // 雪山（东侧 4x4 区块；高级区 21~30 级）
    Count      // 哨兵：区数（= 2）；不是真实的区
};

// 区数常量（数组尺寸、循环上界与越界防御的统一口径）
inline constexpr int kZoneCount = static_cast<int>(ZoneId::Count);

// 区内素材集：World 烘焙区块时按「本格所属区」选素材表——
//   Dungeon = 现有地牢素材（tiles_floor / tiles_wall / decor_sheet）；
//   Snow    = 冰霜重染色版（*_snow 后缀三张，组装脚本程序化染色产出）。
enum class ZoneTileSet
{
    Dungeon = 0, // 地牢原色素材（草地区）
    Snow         // 冰霜重染色素材（雪山区）
};

// 单个区的静态定义（代码表项；数值尽量少而集中，注释即「区表」的报告口径）
struct ZoneDef
{
    const char* name;    // 区名（横幅/日志显示，如「雪山」）
    int chunkMinX;       // 区矩形：x 区间下界（区块索引，含端点）
    int chunkMaxX;       // 区矩形：x 区间上界（含端点）；纵向覆盖全世界
    int levelMin;        // 怪物等级区间下界（批六乙消费；本批仅数据）
    int levelMax;        // 怪物等级区间上界（同上）
    ZoneTileSet tileSet; // 区内素材集（World 烘焙选表依据）
    float moveSpeedMultiplier; // 玩家移速乘区（草=1.0 无影响 / 雪=0.85 减速 15%）
    float respawnX;      // 复活点世界坐标 x（批六乙复活流消费；本批仅数据）
    float respawnY;      // 复活点世界坐标 y（同上）
};

// 区表（下标与 ZoneId 一一对应）：
//   草地：区块 x 0..3（西 4x4）；等级 1~20；地牢素材；无 debuff；
//         复活点 = 草地区中心（像素 (4 x 1536)/2 = 3072,3072——即玩家
//         出生点，出生保护区也以它为中心）；
//   雪山：区块 x 4..7（东 4x4）；等级 21~30；冰霜素材；移速 x0.85；
//         复活点 = 靠草地一侧中部（雪山西缘深入半区块处 4x1536 + 768 =
//         6912，纵向正中 3072——离分界线一段距离，避免复活即压线）。
// 雪山移速乘区 0.85 为代码常量（批六规格：「可进 config 或常量」——
// 与卡表同口径放代码表，随分区语义集中可测；改值改这里一处）。
inline constexpr ZoneDef kZoneDefs[kZoneCount] = {
    // 名称     x 区间  等级区间   素材集          移速乘区  复活点 x      复活点 y
    { "草地", 0, 3, 1, 20, ZoneTileSet::Dungeon, 1.0f, 3072.0f, 3072.0f }, // 中心 = 4x1536/2
    { "雪山", 4, 7, 21, 30, ZoneTileSet::Snow, 0.85f, 6912.0f, 3072.0f },  // 4x1536 + 半区块 768
};

// 取区定义（防御：异常 id 回退第一项——与 cardDef/zoneDef 同口径，
// 不崩、结果可预测）
inline const ZoneDef& zoneDef(ZoneId id)
{
    const int index = static_cast<int>(id); // 区 id -> 表下标
    if (index < 0 || index >= kZoneCount)   // 越界（理论不可达）
    {
        return kZoneDefs[0]; // 回退草地（世界默认区）
    }
    return kZoneDefs[index]; // 正常路径：按 id 取表项
}

// 区块坐标 -> 区。分区只按 x 轴切分（两区纵向都铺满世界）：
//   - chunkX <= 草地区 x 上界 -> 草地；
//   - 其余（含分界线上的区块与超出世界的右/负值）-> 雪山/草地各自兜底：
//     负 index 自然 <= 草地上界 -> 草地（最近区）；超大 index -> 雪山。
// chunkY 本批不参与判定（接口保留完整签名，供未来纵向分区扩展）。
inline ZoneId zoneAtChunk(int chunkX, int chunkY)
{
    (void)chunkY; // 纵向不分区：显式忽略，避免「未使用参数」告警
    if (chunkX <= kZoneDefs[static_cast<int>(ZoneId::Grass)].chunkMaxX) // 草地上界 = 3
    {
        return ZoneId::Grass; // 西侧（含世界外左侧）：草地
    }
    return ZoneId::Snow; // 东侧（含世界外右侧）：雪山
}

// 世界坐标点 -> 区：像素先换算区块索引（floor 语义），再走 zoneAtChunk。
// 这是规格要求的 zoneAt(worldPos) 主入口（World 烘焙/PlayScene 都经它）
inline ZoneId zoneAtWorld(float x, float y)
{
    const int chunkX = static_cast<int>(std::floor(x / World::kChunkPixels)); // 像素 -> 区块 x
    const int chunkY = static_cast<int>(std::floor(y / World::kChunkPixels)); // 像素 -> 区块 y（本批不判定）
    return zoneAtChunk(chunkX, chunkY); // 区块 -> 区
}

// 矩形 -> 区：以矩形中心点判定（「矩形整体压在两区上」时按中心归区——
// 本批矩形用例是地形/落点查询，中心口径与点的语义最接近）
inline ZoneId zoneAtRect(const SDL_FRect& rect)
{
    return zoneAtWorld(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f); // 中心点查询
}

#endif // !__ZONE_H__
