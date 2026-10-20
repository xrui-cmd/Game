#ifndef __MONSTERRECYCLE_H__
#define __MONSTERRECYCLE_H__

// ============================================================
// MonsterRecycle：远处休眠怪物的回收判据（批六补丁·跨区槽位解锁）
// ============================================================
//
// 背景（终验问题）：怪物对象池固定 12 槽位；非激活区块内的怪物只暂停
// 更新、不删除。玩家在草地存满 12 只后跨区进入雪山，旧区怪物继续占满
// 池子，新区刷新的「存活数 < 上限」门控永远不通过——槽位锁死。
//
// 回收规则（三个条件同时满足才回收，缺一不可）：
//   1. 所在区块非激活（怪物处于休眠，不会打断玩家眼前的战斗）；
//   2. AI 状态为巡逻（未仇恨、未回巢——追击/回巢中的怪物代表玩家正
//      在拉怪，回收会造成「怪凭空消失」的破绽，不回收）；
//   3. 距玩家严格大于阈值（默认 1800px——远超相机 3x3 窗口的可见
//      范围，玩家看不到它消失）。
//
// 本文件是 header-only 纯逻辑（不依赖 SDL / World / 场景），
// tests/monster_recycle_test.cpp 直接包含并覆盖三条件的组合边界。
// 场景层负责提供三个输入（区块激活查询 / Monster::isPatrolling /
// 中心距），并执行真正的 despawn 回池与日志。
// ============================================================

// 回收距离阈值（像素）：距玩家严格大于本值才可回收。
// 默认 1800 的依据：相机激活窗口最多 3x3 区块（4608x4608px，中心到
// 边缘 2304px），1800 已处于窗口边缘之外；玩家在正常视口内看不到
// 被回收的怪物，不会出现「眼前怪突然消失」的观感问题。
inline constexpr float kMonsterRecycleMinDistance = 1800.0f;

// 远处休眠怪物回收判据（纯函数）：
//   inActiveChunks   —— 怪物中心是否处于激活区块（true = 玩家附近，绝不回收）
//   patrolling       —— AI 是否处于巡逻状态（false = 仇恨/回巢中，不回收）
//   distanceToPlayer —— 怪物中心到玩家中心的距离（像素）
//   minDistance      —— 距离阈值（默认 1800px；单测可注入小值验证边界）
// 返回 true 表示「满足三条件、应回收回池」。
// 防御：距离为 NaN 时比较恒假 -> 返回 false（绝不让异常值误删怪物）。
inline bool shouldRecycleDormantMonster(bool inActiveChunks, bool patrolling,
                                        float distanceToPlayer,
                                        float minDistance = kMonsterRecycleMinDistance)
{
    if (inActiveChunks) // 条件一：激活区块内的怪物是玩家眼前的怪
    {
        return false; // 不回收（哪怕距离暂时很远——激活窗口保证它在视野附近）
    }
    if (!patrolling) // 条件二：仇恨追击 / 回巢途中的怪物不回收
    {
        return false; // 不回收（避免玩家拉怪时目标凭空消失）
    }
    if (!(distanceToPlayer > minDistance)) // 条件三：距离必须严格大于阈值
    {
        return false; // 恰好等于阈值不回收（边界口径：严格大于）；NaN 也走这里
    }
    return true; // 三个条件全部满足：回收（槽位回到对象池）
}

#endif // !__MONSTERRECYCLE_H__
