// ============================================================================
//  monster_level_test.cpp — 怪物等级与数值缩放单元测试（批六丙）
//
//  被测对象 : Src/inc/game/MonsterLevel.h（纯逻辑 header-only）——
//             ① 区最远距离常量（草地 4344.464 / 雪山 6191.814）；
//             ② monsterLevelForSpawn：出生点归区、距神龛归一、抖动、clamp；
//             ③ scaledMonsterHp / scaledMonsterDamage / monsterXpValue /
//                monsterCoinDrop：等级缩放公式与等级 1 恒等边界；
//             ④ 区界（区块 4 左缘 = x 6144）草地/雪山归属与等级区间；
//             ⑤ 越界 jitter / 越界等级 / 负基础值的确定性防御。
//
//  依赖 : MonsterLevel.h -> Zone.h -> World.h -> SDL.h；只用到 SDL 头文件
//  中的类型与内联函数（SDL_FRect），不调用任何 SDL 运行时函数——不链接
//  SDL3，任何机器上都能确定性运行（构建接线见 tests/CMakeLists.txt）。
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "game/MonsterLevel.h" // 被测：等级梯度 / 区最远距离 / 四个缩放公式
#include "game/Zone.h"         // zoneAtWorld：区块 4 左缘的归区断言

#include <cmath>   // std::fabs / std::sqrt：浮点近似与几何距离核对
#include <cstdio>  // std::printf
#include <cstdlib> // std::system（Windows 控制台切 UTF-8）

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
    std::printf(" MonsterLevel 怪物等级与缩放单元测试 —— 批六丙\n");
    std::printf(" 默认布局：草地 1~20 / 雪山 21~30；生命 +10%%/级、伤害 +6%%/级\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 区最远距离常量与区角几何自洽
    // ------------------------------------------------------------------
    beginCase(1, "区最远距离（草 4344.464 / 雪 6191.814）与角点几何自洽");
    int p0 = gPass, f0 = gFail;
    {
        // 草地：神龛 (3072,3072) 到草地区最远角（任一角落）的距离
        const float grassCorner = std::sqrt(3072.0f * 3072.0f + 3072.0f * 3072.0f); // sqrt(3072² + 3072²)
        expectNear("草地最远角距离 = sqrt(3072²+3072²)", kMonsterGrassMaxDistance, grassCorner, 0.01f);

        // 雪山：神龛 (6912,3072) 到雪区最远东角 (12288,0)/(12288,6144)
        const float snowCorner = std::sqrt(5376.0f * 5376.0f + 3072.0f * 3072.0f); // sqrt(5376² + 3072²)
        expectNear("雪山最远角距离 = sqrt(5376²+3072²)", kMonsterSnowMaxDistance, snowCorner, 0.01f);

        // 查询函数按区分发；Count 哨兵按草地兜底（防御）
        expectNear("monsterZoneMaxDistance(草地)", monsterZoneMaxDistance(ZoneId::Grass),
                   kMonsterGrassMaxDistance, 0.001f);
        expectNear("monsterZoneMaxDistance(雪山)", monsterZoneMaxDistance(ZoneId::Snow),
                   kMonsterSnowMaxDistance, 0.001f);
        expectNear("monsterZoneMaxDistance(Count 兜底草地)",
                   monsterZoneMaxDistance(ZoneId::Count), kMonsterGrassMaxDistance, 0.001f);

        // 雪区最远角确实比草区远（大区 + 神龛偏西）
        expectTrue("雪区最远距离 > 草区最远距离",
                   kMonsterSnowMaxDistance > kMonsterGrassMaxDistance);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 草地等级梯度（神龛 0 -> 1 级；最远角 -> 20 级；抖动/clamp）
    // ------------------------------------------------------------------
    beginCase(2, "草地 1~20：距离归一 + 四舍五入 + 抖动 + clamp");
    p0 = gPass; f0 = gFail;
    {
        const float shrineX = zoneDef(ZoneId::Grass).respawnX; // 草地神龛 x = 3072
        const float shrineY = zoneDef(ZoneId::Grass).respawnY; // 草地神龛 y = 3072

        // 神龛中心：t=0 -> 1 级；抖动 -1 仍夹回 1（不越下限）
        expectInt("神龛中心 jitter=0", monsterLevelForSpawn(ZoneId::Grass, shrineX, shrineY, 0), 1);
        expectInt("神龛中心 jitter=-1（clamp 下限）",
                  monsterLevelForSpawn(ZoneId::Grass, shrineX, shrineY, -1), 1);
        expectInt("神龛中心 jitter=+1", monsterLevelForSpawn(ZoneId::Grass, shrineX, shrineY, 1), 2);

        // 1/4 最远距离：19 x 0.25 = 4.75 -> round 5 -> 6 级
        expectInt("距神龛 1/4 最远距离（jitter=0）",
                  monsterLevelForSpawn(ZoneId::Grass, shrineX + kMonsterGrassMaxDistance * 0.25f,
                                       shrineY, 0), 6);

        // 1/2 最远距离 +1px：理论值 19 x 0.5 = 9.5 -> round 10 -> 11 级
        //（+1px 避开「恰好 .5」的浮点临界——公式本身在边界处允许 10/11 两值）
        expectInt("距神龛 1/2 最远距离 +1px（jitter=0）",
                  monsterLevelForSpawn(ZoneId::Grass, shrineX,
                                       shrineY - (kMonsterGrassMaxDistance * 0.5f + 1.0f), 0), 11);

        // 3/4 最远距离：19 x 0.75 = 14.25 -> round 14 -> 15 级
        expectInt("距神龛 3/4 最远距离（jitter=0）",
                  monsterLevelForSpawn(ZoneId::Grass, shrineX - kMonsterGrassMaxDistance * 0.75f,
                                       shrineY, 0), 15);

        // 最远角：19 -> 20 级；抖动 +1 夹回 20（不越上限）
        expectInt("最远距离（jitter=0）",
                  monsterLevelForSpawn(ZoneId::Grass, shrineX + kMonsterGrassMaxDistance,
                                       shrineY, 0), 20);
        expectInt("最远距离（jitter=+1，clamp 上限）",
                  monsterLevelForSpawn(ZoneId::Grass, shrineX + kMonsterGrassMaxDistance,
                                       shrineY, 1), 20);
        expectInt("最远距离（jitter=-1）",
                  monsterLevelForSpawn(ZoneId::Grass, shrineX + kMonsterGrassMaxDistance,
                                       shrineY, -1), 19);

        // 距离超过最远值（区外/自定义世界）：clamp 到 1.0，不越 20
        expectInt("超最远距离（x2，clamp 上界）",
                  monsterLevelForSpawn(ZoneId::Grass, shrineX + kMonsterGrassMaxDistance * 2.0f,
                                       shrineY, 0), 20);

        // 方向无关：四个正交方向同距离应同等级（欧氏距离而非单轴）
        const int east = monsterLevelForSpawn(ZoneId::Grass, shrineX + 1000.0f, shrineY, 0);
        const int west = monsterLevelForSpawn(ZoneId::Grass, shrineX - 1000.0f, shrineY, 0);
        const int north = monsterLevelForSpawn(ZoneId::Grass, shrineX, shrineY - 1000.0f, 0);
        const int south = monsterLevelForSpawn(ZoneId::Grass, shrineX, shrineY + 1000.0f, 0);
        expectTrue("等距离四方向等级一致", east == west && east == north && east == south);

        // 斜向 1000,1000 与单轴约 1414 同距离：验证欧氏距离（sqrt(dx²+dy²)）
        const int diagonal = monsterLevelForSpawn(ZoneId::Grass, shrineX + 1000.0f,
                                                  shrineY + 1000.0f, 0);
        const int axis = monsterLevelForSpawn(ZoneId::Grass,
                                              shrineX + std::sqrt(2.0f) * 1000.0f, shrineY, 0);
        expectInt("斜向距离按欧氏距离折算", diagonal, axis);
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 雪山等级梯度（神龛 21 级；最远角 30 级；抖动/clamp）
    // ------------------------------------------------------------------
    beginCase(3, "雪山 21~30：距离归一 + 抖动 + clamp");
    p0 = gPass; f0 = gFail;
    {
        const float shrineX = zoneDef(ZoneId::Snow).respawnX; // 雪山神龛 x = 6912
        const float shrineY = zoneDef(ZoneId::Snow).respawnY; // 雪山神龛 y = 3072

        // 神龛中心：21 级；抖动 -1/+1 分别夹回 21 / 22
        expectInt("神龛中心 jitter=0", monsterLevelForSpawn(ZoneId::Snow, shrineX, shrineY, 0), 21);
        expectInt("神龛中心 jitter=-1（clamp 下限）",
                  monsterLevelForSpawn(ZoneId::Snow, shrineX, shrineY, -1), 21);
        expectInt("神龛中心 jitter=+1", monsterLevelForSpawn(ZoneId::Snow, shrineX, shrineY, 1), 22);

        // 1/2 最远距离 +1px：理论值 9 x 0.5 = 4.5 -> round 5 -> 26 级
        //（+1px 避开「恰好 .5」的浮点临界）
        expectInt("距神龛 1/2 最远距离 +1px（jitter=0）",
                  monsterLevelForSpawn(ZoneId::Snow,
                                       shrineX + (kMonsterSnowMaxDistance * 0.5f + 1.0f),
                                       shrineY, 0), 26);

        // 最远东角 (12288, 0)/(12288, 6144)：30 级；抖动 +1 夹回 30
        const float eastEdgeX = 8.0f * World::kChunkPixels; // 世界右边界 x = 12288
        expectInt("最远东上角（jitter=0）",
                  monsterLevelForSpawn(ZoneId::Snow, eastEdgeX, 0.0f, 0), 30);
        expectInt("最远东下角（jitter=0）",
                  monsterLevelForSpawn(ZoneId::Snow, eastEdgeX, 4.0f * World::kChunkPixels, 0), 30);
        expectInt("最远角（jitter=+1，clamp 上限）",
                  monsterLevelForSpawn(ZoneId::Snow, eastEdgeX, 0.0f, 1), 30);
        expectInt("最远角（jitter=-1）",
                  monsterLevelForSpawn(ZoneId::Snow, eastEdgeX, 0.0f, -1), 29);

        // 区界（区块 4 左缘 = x 6144）：已归雪山；距神龛 768 -> 22 级
        expectInt("雪区西缘 x=6144（jitter=0）",
                  monsterLevelForSpawn(ZoneId::Snow, 4.0f * World::kChunkPixels, shrineY, 0), 22);

        // 超最远距离（防御）：夹到 1.0 -> 30 级
        expectInt("超最远距离（clamp 上界）",
                  monsterLevelForSpawn(ZoneId::Snow, shrineX + kMonsterSnowMaxDistance * 3.0f,
                                       shrineY, 0), 30);
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 区界归区 + 抖动全扫不越区
    // ------------------------------------------------------------------
    beginCase(4, "区块 4 左缘归区 + 抖动/距离全扫不越区");
    p0 = gPass; f0 = gFail;
    {
        // x=6143.9 属于区块 3（草地区），x=6144 属于区块 4（雪山区）
        const float boundaryX = 4.0f * World::kChunkPixels; // 分区界线 = 6144
        expectTrue("x=6143.9 -> 草地", zoneAtWorld(boundaryX - 0.1f, 3072.0f) == ZoneId::Grass);
        expectTrue("x=6144 -> 雪山", zoneAtWorld(boundaryX, 3072.0f) == ZoneId::Snow);

        // 草地区最东缘（x=6143.9，y=神龛高度）：距神龛 3071.9 -> t≈0.707
        // -> round(19 x 0.707) = 13 -> 14 级（真正最远点是区四角，不是东缘中点）
        expectInt("草地区最东缘等级（jitter=0，y=神龛高度）",
                  monsterLevelForSpawn(ZoneId::Grass, boundaryX - 0.1f, 3072.0f, 0), 14);

        // 全扫草地：距离 0..max（101 个点）x jitter -1/0/+1，全部落在 1..20
        bool grassInRange = true; // 草地越界标记
        for (int i = 0; i <= 100; ++i) // 距离采样 101 点
        {
            const float ratio = static_cast<float>(i) / 100.0f; // 0..1 归一化距离
            const float distance = ratio * kMonsterGrassMaxDistance; // 实际距离
            for (int jitter = -1; jitter <= 1; ++jitter) // 三档抖动
            {
                const int level = monsterLevelForSpawn(ZoneId::Grass,
                                                       zoneDef(ZoneId::Grass).respawnX + distance,
                                                       zoneDef(ZoneId::Grass).respawnY, jitter);
                if (level < 1 || level > 20) // 越出草地区间即失败
                {
                    grassInRange = false; // 记录越界
                }
            }
        }
        expectTrue("草地全距离 x 全抖动均在 1~20", grassInRange);

        // 全扫雪山：全部落在 21..30
        bool snowInRange = true; // 雪山越界标记
        for (int i = 0; i <= 100; ++i) // 距离采样 101 点
        {
            const float ratio = static_cast<float>(i) / 100.0f; // 0..1 归一化距离
            const float distance = ratio * kMonsterSnowMaxDistance; // 实际距离
            for (int jitter = -1; jitter <= 1; ++jitter) // 三档抖动
            {
                const int level = monsterLevelForSpawn(ZoneId::Snow,
                                                       zoneDef(ZoneId::Snow).respawnX + distance,
                                                       zoneDef(ZoneId::Snow).respawnY, jitter);
                if (level < 21 || level > 30) // 越出雪山区间即失败
                {
                    snowInRange = false; // 记录越界
                }
            }
        }
        expectTrue("雪山全距离 x 全抖动均在 21~30", snowInRange);

        // 越界 jitter 被夹到 -1/+1：极端值与合法端值结果一致
        expectInt("jitter=-99 等价 -1",
                  monsterLevelForSpawn(ZoneId::Grass, 3072.0f, 3072.0f + 2000.0f, -99),
                  monsterLevelForSpawn(ZoneId::Grass, 3072.0f, 3072.0f + 2000.0f, -1));
        expectInt("jitter=+99 等价 +1",
                  monsterLevelForSpawn(ZoneId::Grass, 3072.0f, 3072.0f + 2000.0f, 99),
                  monsterLevelForSpawn(ZoneId::Grass, 3072.0f, 3072.0f + 2000.0f, 1));
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 生命/伤害/经验/金币缩放公式与等级 1 恒等边界
    // ------------------------------------------------------------------
    beginCase(5, "缩放公式：生命 +10%/级、伤害 +6%/级、经验 +10%/级、金币每 10 级 +1");
    p0 = gPass; f0 = gFail;
    {
        // 等级 1：所有缩放恒等于基础值（规格硬边界）
        expectInt("生命 lv1（史莱姆 20）", scaledMonsterHp(20, 1), 20);
        expectInt("生命 lv1（哥布林 30）", scaledMonsterHp(30, 1), 30);
        expectInt("生命 lv1（骷髅 18）", scaledMonsterHp(18, 1), 18);
        expectInt("伤害 lv1（基础 10）", scaledMonsterDamage(10, 1), 10);
        expectInt("经验 lv1（基础 10）", monsterXpValue(10, 1), 10);
        expectInt("金币 lv1（基础 1）", monsterCoinDrop(1, 1), 1);
        expectInt("金币 lv1（基础 2）", monsterCoinDrop(2, 1), 2);

        // 生命：lv11 = 2.0x；lv20 = 2.9x；lv30 = 3.9x（四舍五入）
        expectInt("生命 lv11（20 -> 40）", scaledMonsterHp(20, 11), 40);
        expectInt("生命 lv11（30 -> 60）", scaledMonsterHp(30, 11), 60);
        expectInt("生命 lv11（18 -> 36）", scaledMonsterHp(18, 11), 36);
        expectInt("生命 lv20（20 -> 58）", scaledMonsterHp(20, 20), 58);
        expectInt("生命 lv20（30 -> 87）", scaledMonsterHp(30, 20), 87);
        expectInt("生命 lv20（18 -> 52，52.2 舍）", scaledMonsterHp(18, 20), 52);
        expectInt("生命 lv21（20 -> 60）", scaledMonsterHp(20, 21), 60);
        expectInt("生命 lv30（20 -> 78）", scaledMonsterHp(20, 30), 78);
        expectInt("生命 lv30（30 -> 117）", scaledMonsterHp(30, 30), 117);
        expectInt("生命 lv30（18 -> 70，70.2 舍）", scaledMonsterHp(18, 30), 70);

        // 伤害：1.6x / 2.2x / 2.74x（四舍五入）
        expectInt("伤害 lv11（10 -> 16）", scaledMonsterDamage(10, 11), 16);
        expectInt("伤害 lv21（10 -> 22）", scaledMonsterDamage(10, 21), 22);
        expectInt("伤害 lv30（10 -> 27，27.4 舍）", scaledMonsterDamage(10, 30), 27);
        expectInt("伤害 lv16（10 -> 19，19.0）", scaledMonsterDamage(10, 16), 19);

        // 经验：10/11/19/20/29/30/39 的关键节点（round(10 x (1+0.1(lv-1)))）
        expectInt("经验 lv2（10 -> 11）", monsterXpValue(10, 2), 11);
        expectInt("经验 lv10（10 -> 19）", monsterXpValue(10, 10), 19);
        expectInt("经验 lv11（10 -> 20）", monsterXpValue(10, 11), 20);
        expectInt("经验 lv20（10 -> 29）", monsterXpValue(10, 20), 29);
        expectInt("经验 lv21（10 -> 30）", monsterXpValue(10, 21), 30);
        expectInt("经验 lv30（10 -> 39）", monsterXpValue(10, 30), 39);

        // 金币：1~10 级基础、11~20 +1、21~30 +2（变体基础 1/2 各验一遍）
        expectInt("金币 lv10（基础 2 -> 2）", monsterCoinDrop(2, 10), 2);
        expectInt("金币 lv11（基础 2 -> 3）", monsterCoinDrop(2, 11), 3);
        expectInt("金币 lv20（基础 2 -> 3）", monsterCoinDrop(2, 20), 3);
        expectInt("金币 lv21（基础 2 -> 4）", monsterCoinDrop(2, 21), 4);
        expectInt("金币 lv30（基础 2 -> 4）", monsterCoinDrop(2, 30), 4);
        expectInt("金币 lv10（基础 1 -> 1）", monsterCoinDrop(1, 10), 1);
        expectInt("金币 lv11（基础 1 -> 2）", monsterCoinDrop(1, 11), 2);
        expectInt("金币 lv30（基础 1 -> 3）", monsterCoinDrop(1, 30), 3);

        // 防御：等级 0/负值按 1 处理（= 基础值）；负基础值按 0；HP 至少 1
        expectInt("生命 lv0 按 1（30）", scaledMonsterHp(30, 0), 30);
        expectInt("伤害 lv-5 按 1（10）", scaledMonsterDamage(10, -5), 10);
        expectInt("经验 lv0 按 1（10）", monsterXpValue(10, 0), 10);
        expectInt("金币 lv0 按 1（2）", monsterCoinDrop(2, 0), 2);
        expectInt("生命负基础按 0 -> 至少 1", scaledMonsterHp(-100, 5), 1);
        expectInt("伤害负基础按 0", scaledMonsterDamage(-5, 5), 0);
        expectInt("经验负基础按 0", monsterXpValue(-5, 5), 0);
        expectInt("金币负基础按 0", monsterCoinDrop(-5, 5), 0);
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // CASE 6 等级梯度关键区段（近/中/远）离散表——报告口径核对
    // ------------------------------------------------------------------
    beginCase(6, "梯度关键点表（草地 1/6/11/15/20；雪山 21/26/30）");
    p0 = gPass; f0 = gFail;
    {
        // 草地：按最远距离的 0 / 25% / 50% / 75% / 100% 采样
        const float gx = zoneDef(ZoneId::Grass).respawnX; // 草地神龛 x
        const float gy = zoneDef(ZoneId::Grass).respawnY; // 草地神龛 y
        expectInt("草地 0% -> 1", monsterLevelForSpawn(ZoneId::Grass, gx, gy, 0), 1);
        expectInt("草地 25% -> 6",
                  monsterLevelForSpawn(ZoneId::Grass, gx + kMonsterGrassMaxDistance * 0.25f, gy, 0), 6);
        expectInt("草地 50% +1px -> 11",
                  monsterLevelForSpawn(ZoneId::Grass,
                                       gx + (kMonsterGrassMaxDistance * 0.50f + 1.0f), gy, 0), 11);
        expectInt("草地 75% -> 15",
                  monsterLevelForSpawn(ZoneId::Grass, gx + kMonsterGrassMaxDistance * 0.75f, gy, 0), 15);
        expectInt("草地 100% -> 20",
                  monsterLevelForSpawn(ZoneId::Grass, gx + kMonsterGrassMaxDistance, gy, 0), 20);

        // 雪山：按最远距离的 0 / 50% / 100% 采样
        const float sx = zoneDef(ZoneId::Snow).respawnX; // 雪山神龛 x
        const float sy = zoneDef(ZoneId::Snow).respawnY; // 雪山神龛 y
        expectInt("雪山 0% -> 21", monsterLevelForSpawn(ZoneId::Snow, sx, sy, 0), 21);
        expectInt("雪山 50% +1px -> 26",
                  monsterLevelForSpawn(ZoneId::Snow,
                                       sx + (kMonsterSnowMaxDistance * 0.50f + 1.0f), sy, 0), 26);
        expectInt("雪山 100% -> 30",
                  monsterLevelForSpawn(ZoneId::Snow, sx + kMonsterSnowMaxDistance, sy, 0), 30);

        // 等级跨度与区表一致：草地 19、雪山 9（防未来改表漏改公式）
        expectInt("草地等级跨度 = 19",
                  zoneDef(ZoneId::Grass).levelMax - zoneDef(ZoneId::Grass).levelMin, 19);
        expectInt("雪山等级跨度 = 9",
                  zoneDef(ZoneId::Snow).levelMax - zoneDef(ZoneId::Snow).levelMin, 9);
    }
    endCase(6, p0, f0);

    // ------------------------------------------------------------------
    // 汇总
    // ------------------------------------------------------------------
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 通过 / %d 失败（共 %d 断言）\n", gPass, gFail, gPass + gFail);
    std::printf("==============================================================\n");
    return gFail; // 退出码 = 失败断言数（全过 = 0）
}
