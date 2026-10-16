#ifndef __CHOICEDRAW_H__
#define __CHOICEDRAW_H__

// ============================================================
// ChoiceDraw：通用「不重复抽取」纯逻辑（批五B：升级卡/法术池共用）
// ============================================================
//
// 职责：把 M2 用在升级卡池的「洗牌式逐张抽取」算法抽出成通用件——
// 调用方负责构建「候选索引池」（按各自系统的满层排除等规则过滤后，
// 依法序写入索引），本模块从池中均匀抽最多 kChoiceDrawMax 个不重复项。
// 本批（批五B）的两处使用：
//   1) game/UpgradeCard.h 的 drawUpgradeCards（数值卡池——现由商店发放，
//      抽取逻辑保留供回归与未来复用）；
//   2) game/Spell.h 的 drawSpellChoices（升级三选一法术池）。
//
// 泛化的边界：本模块只认识 int 索引与「池大小」——不认识 CardId/SpellId
// 等具体类型（类型到索引的换算留在各调用方，结构上正是「抽取纯逻辑」
// 与「业务池定义」的解耦）。
//
// 行为口径（与 M2 原实现逐字节一致——card_test 的 133 断言回归即检验）：
//   - 逐张抽取：剩余池中按注入随机源均匀取一、抽走处用池尾填补——
//     不重复且池内等权；
//   - 池不足 kChoiceDrawMax 张时按实际张数返回（count = 池大小）；
//   - 池空（或未提供池）返回 count = 0——调用方据它走「无选项」流程；
//   - 随机源经函数指针注入：游戏内传种子化流（可复现），单测传固定序列
//     （确定性）；fn 为 nullptr 时退化常量 0.5（确定性兜底，不崩）；
//     fn 的非法输出（NaN/负/≥1）一律夹到合法索引（防御，单测覆盖）。
// ============================================================

// 一次抽取的最大张数：三选一面板（升级卡/法术）的栏位数
inline constexpr int kChoiceDrawMax = 3;

// 候选池容量上限（防御）：超过此数的池按前 kChoiceDrawPoolCap 项截断——
// 本项目最大池为升级卡 9 张、法术 6 张，16 是留有余量的硬边界
//（截断语义 = 「宁可少入池、绝不越界」，单测覆盖边界）
inline constexpr int kChoiceDrawPoolCap = 16;

// 宿主注入的随机源：[0,1) 均匀分布；context 为宿主自备上下文（可空）。
// 与 CardRandomFn / MonsterAiRandomFn 同款设计——同一函数指针类型贯通
// 「单测固定序列」与「游戏种子流」两种注入方式
using ChoiceRandomFn = float (*)(void* context);

// 抽取结果：索引数组 + 实际张数（池不足时按实际；0 = 池空/无候选）
struct ChoiceDrawResult
{
    int indices[kChoiceDrawMax] = {}; // 抽到的候选索引（仅前 count 个有效）
    int count = 0;                    // 实际张数（0 ~ kChoiceDrawMax）
};

// 从候选索引池洗牌式抽最多 kChoiceDrawMax 个不重复项（契约见文件头）。
// pool/poolSize：候选索引数组与其张数（调用方保证索引本身合法——
// 本函数只做搬运与洗牌，不解释索引含义）；fn/context：随机源与上下文
inline ChoiceDrawResult drawDistinctFromPool(const int* pool, int poolSize,
                                             ChoiceRandomFn fn, void* context)
{
    ChoiceDrawResult result; // 抽取结果（默认 count = 0——池空语义）

    // 池未提供或空池（防御路径）：无候选可抽，直接交还空结果
    if (pool == nullptr || poolSize <= 0)
    {
        return result; // 0 张：调用方走「无选项」流程
    }

    // 池大小防御截断：超过容量上限按上限计（本项目池远小于此值）
    const int size = (poolSize < kChoiceDrawPoolCap) ? poolSize : kChoiceDrawPoolCap;

    // 候选池本地拷贝：抽取需要原地洗牌（被抽走的槽位用池尾填补），
    // 不能改动调用方的数组（其内存可能来自只读上下文）
    int local[kChoiceDrawPoolCap] = {}; // 本地池（容量上限即尺寸上界）
    for (int i = 0; i < size; ++i)      // 逐项拷贝
    {
        local[i] = pool[i]; // 复制候选索引
    }

    // 实际张数：不足 kChoiceDrawMax 按实际（池不足的语义在张数上直接体现）
    const int drawCount = (size < kChoiceDrawMax) ? size : kChoiceDrawMax;
    result.count = drawCount; // 落定张数

    for (int k = 0; k < drawCount; ++k) // 逐张抽取（位序 k = 展示位序）
    {
        const int remaining = size - k; // 剩余候选数（本次抽取的分母）

        // 采样随机源；fn 为空指针时退化常量 0.5（确定性兜底，不崩）
        float u = (fn != nullptr) ? fn(context) : 0.5f;

        // 非法输出统一夹到合法域：NaN（比较恒假）与负值 -> 0；见下再防上溢
        if (!(u >= 0.0f))
        {
            u = 0.0f; // 压到 0（确定性防御）
        }

        // [0,1) x 剩余数 -> 截断取整（u < 1 时 idx 必 < remaining；见下防边界）
        int idx = static_cast<int>(u * static_cast<float>(remaining));

        if (idx >= remaining) // u 接近 1 的舍入上溢：夹到末位
        {
            idx = remaining - 1; // 末位合法
        }
        if (idx < 0) // 极端浮点情形再防一次（截断为负的理论边界）
        {
            idx = 0; // 夹到首位
        }

        result.indices[k] = local[idx]; // 本张落位（展示位序 k）
        local[idx] = local[remaining - 1]; // 池尾填补被抽走的洞，
        // 下一轮 remaining 收缩——本轮抽中的索引绝不再入（不重复的保证）
    }

    return result; // 交还抽取结果
}

#endif // !__CHOICEDRAW_H__
