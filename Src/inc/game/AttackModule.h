#ifndef __ATTACKMODULE_H__
#define __ATTACKMODULE_H__

#include <algorithm> // std::clamp：下标越界时的防御性夹取

// ============================================================
// AttackModule：六型怪物进攻模组数据表（批六丁·数据表驱动）
// ============================================================
//
// 设计口径（规格依据 docs/批六待办-乙丙丁.md「三、批六·丁」）：
//   - 每只怪物只认「模组」：蓄力时长 / 攻击方式 / 位移距离 / 持续时间 /
//     命中半径 / 伤害倍率 / 元素效果全部收进本表；
//   - 加怪 = 在 MonsterType 枚举与 Monster.cpp 的变体表各加一行，再在
//     本表加一行同下标模组——攻击代码不再出现「如果是史莱姆就……」的
//     分支（唯一的 kind 分支是三种动作的通用执行器）；
//   - 本文件是 header-only 纯逻辑（不依赖 SDL / Zone / Monster），
//     tests/attack_element_test.cpp 可直接包含并断言参数范围。
//
// 表下标约定：与 MonsterType 枚举顺序严格一致（0 史莱姆 / 1 哥布林 /
// 2 骷髅 / 3 冰史莱姆 / 4 霜哥布林 / 5 霜骷髅）。Monster.h 提供
// attackModuleFor(MonsterType) 做类型 -> 表项映射；本文件只认整数下标，
// 避免纯逻辑头反向依赖怪物实体头。
// ============================================================

// 攻击方式：三种通用执行器（Monster::advanceAttack 按它分支）
enum class AttackKind
{
    Lunge,     // 前扑：蓄力后倾 -> 朝锁定方向短冲（哥布林，既有行为）
    JumpSmash, // 跳跃砸击：深蹲蓄力 -> 弧线跃向玩家 -> 落点范围伤害（史莱姆）
    Thrust     // 突刺冲锋：后倾蓄力 -> 高速直线突刺，撞墙/命中即止（骷髅）
};

// 元素效果：命中后由场景结算（怪物自身只出招，不改玩家状态）
enum class AttackElement
{
    None,  // 无元素：只造成伤害
    Slow,  // 减速：玩家移速乘区 + 持续时长（冰史莱姆）
    Freeze // 冻结条：命中累积点数，满 100 触发冻结（霜哥布林 / 霜骷髅）
};

// 单个进攻模组的完整参数（所有时长单位为秒、距离/半径单位为像素）
struct AttackModule
{
    const char* name;          // 模组名（日志/冒烟展示用，如「跳跃砸击」）
    AttackKind kind;           // 攻击方式（三种通用执行器之一）
    float windupSeconds;       // 蓄力时长（深蹲/后倾，移速归零的躲避窗口）
    float distance;            // 攻击位移距离（前扑/跳跃/突刺的峰值预算）
    float duration;            // 攻击位移时长（位移按曲线在该窗口内分配）
    float hitRadius;           // 落点命中半径；0 = 沿用接触范围（碰撞盒 + 余量）
    float damageMultiplier;    // 伤害倍率（场景在等级缩放之后乘算，再四舍五入）
    AttackElement element;     // 命中附带的元素效果
    float slowFactor;          // 减速乘区（仅 Slow 有效；0.8 = 八折移速）
    float slowSeconds;         // 减速持续时长（仅 Slow 有效；重复命中刷新）
    int freezeAmount;          // 冻结条累积点数（仅 Freeze 有效；霜哥布林 25 / 霜骷髅 20）
    bool frostVisual;          // 是否绘制冰霜系命中视觉（冰环/冰爪/冰痕）
};

// 六型进攻模组数据表（下标 = MonsterType 枚举值；顺序不可交换）。
// 参数口径：
//   - 史莱姆系：蓄力 0.45s、跃出 80px/0.3s、落点 40px 范围；
//   - 哥布林系：沿用既有蓄力前扑（0.35s / 24px / 0.12s，接触判定）；
//   - 骷髅系：后倾 0.3s、突刺 140px/0.25s，撞墙/命中即止；
//   - 霜系只加元素与冰霜视觉：冰史莱姆减速 x0.8 / 3s；
//     霜哥布林冻结条 +25；霜骷髅冻结条 +20。
// 伤害倍率本批统一 1.0（保持批六丙等级伤害口径不变；字段留作后续调平）。
inline constexpr AttackModule kAttackModules[] = {
    // 0 史莱姆（草地）：跳跃砸击——深蹲 0.45s -> 弧线跃出 80px/0.3s -> 落点 40px
    { "跳跃砸击", AttackKind::JumpSmash, 0.45f, 80.0f, 0.30f, 40.0f,
      1.0f, AttackElement::None, 0.0f, 0.0f, 0, false },
    // 1 哥布林（草地）：既有蓄力前扑（保持批五A 手感：0.35s / 24px / 0.12s）
    { "蓄力前扑", AttackKind::Lunge, 0.35f, 24.0f, 0.12f, 0.0f,
      1.0f, AttackElement::None, 0.0f, 0.0f, 0, false },
    // 2 骷髅（草地）：突刺冲锋——后倾 0.3s -> 高速突刺 140px/0.25s
    { "突刺冲锋", AttackKind::Thrust, 0.30f, 140.0f, 0.25f, 0.0f,
      1.0f, AttackElement::None, 0.0f, 0.0f, 0, false },
    // 3 冰史莱姆（雪山）：跳跃砸击 + 落地冰环 + 命中减速 x0.8 / 3s
    { "跳跃砸击（冰环）", AttackKind::JumpSmash, 0.45f, 80.0f, 0.30f, 40.0f,
      1.0f, AttackElement::Slow, 0.8f, 3.0f, 0, true },
    // 4 霜哥布林（雪山）：前扑 + 冰爪视觉 + 冻结条 +25
    { "蓄力前扑（冰爪）", AttackKind::Lunge, 0.35f, 24.0f, 0.12f, 0.0f,
      1.0f, AttackElement::Freeze, 0.0f, 0.0f, 25, true },
    // 5 霜骷髅（雪山）：突刺 + 冰痕视觉 + 冻结条 +20
    { "突刺冲锋（冰痕）", AttackKind::Thrust, 0.30f, 140.0f, 0.25f, 0.0f,
      1.0f, AttackElement::Freeze, 0.0f, 0.0f, 20, true },
};

// 表项数量（MonsterType::Count 的镜像；单测断言两者一致的依据）
inline constexpr int kAttackModuleCount =
    static_cast<int>(sizeof(kAttackModules) / sizeof(kAttackModules[0]));

// 按整数下标取模组（防御：越界夹到表尾——宁可参数不符也绝不越界读表）
inline const AttackModule& attackModuleForIndex(int index)
{
    const int safeIndex = std::clamp(index, 0, kAttackModuleCount - 1); // 下标夹取
    return kAttackModules[safeIndex]; // 返回表项（生命周期 = 程序全程）
}

#endif // !__ATTACKMODULE_H__
