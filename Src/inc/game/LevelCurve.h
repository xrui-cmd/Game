#ifndef __LEVELCURVE_H__
#define __LEVELCURVE_H__

#include <cmath> // std::pow / std::llround：幂增长曲线与四舍五入取整

// ============================================================
// LevelCurve：升级经验曲线（game 层，纯逻辑——无 SDL、无 IO、无全局状态）
// ============================================================
//
// 设计背景：本批之前经验只累积不升级（占位）；升级落地的同时把「曲线算法」
// 从场景代码里抽成独立小类，带来两个收益：
//   1. 可单测：输入/输出全是纯数据（整数 + 浮点），测试不需要窗口与 SDL
//      运行时——tests/xp_test.cpp 直接 include 本头文件断言曲线值与边界；
//   2. 配置注入：曲线参数（base/growth）由 PlayScene 从 config 读取后写入，
//      本类不接触 Config、不接触任何引擎类型。
//
// 曲线定义（与 config 键一一对应）：
//   needed(level) = 四舍五入( xp_level_base × xp_level_growth^(level-1) )
//     - 「从 level 升到 level+1」所需经验 = 上式；
//     - 默认参数（base=30、growth=1.35）下：30 / 41 / 55 / 74 / 100 / 135 ...
//
// 经验结算（apply）：
//   - 把一枚经验宝石的 gained 点加到当前进度上，然后「循环结算」——只要
//     进度不小于当前级需求就扣减并升 1 级，直到不足以再升；因此一枚高价值
//     宝石（或边界情况）可以连升多级；
//   - 返回结算后的等级 / 当前级进度 / 连升级数：场景据此逐级发放强化与反馈。
//
// 边界防御（配置写坏不能崩、更不能让升级循环卡死；全部有单测覆盖）：
//   - base  < 1   → 按 1 处理：0/负需求会让「xp >= need」恒成立、循环永不收敛；
//   - growth < 1  → 按 1 处理：含负数与 NaN（NaN 参与比较恒 false，同样被夹住）；
//   - level < 1   → 按 1 处理：0/负级只可能出现在调用方出错时，取第 1 级需求；
//   - 幂值超出封顶（配置异常 / 天文等级）→ 返回 kMaxNeeded，杜绝 double
//     转 int 的溢出未定义行为（std::pow 的 inf 直接转 int 是 UB）；
//   - gained < 0  → 按 0 处理（负经验没有玩法语义，忽略而不是倒扣等级）。
// ============================================================

// 一次经验结算的结果（值语义：调用方拿到后可直接写回自己的运行时状态）
struct LevelUpResult
{
    int level = 1;        // 结算后的等级（最低为 1）
    int xp = 0;           // 结算后「当前级已积累的进度经验」（恒小于本级需求）
    int levelsGained = 0; // 本次连升的级数（0 = 未升级；一枚宝石可能 >1）
};

class LevelCurve
{
public:
    // 曲线参数：由 PlayScene 从 config 注入（缺键时这两个默认值与 config 回退一致）
    int base = 30;        // 第 1 级所需经验（config 键 xp_level_base）
    float growth = 1.35f; // 需求逐级倍率（config 键 xp_level_growth）

    // 从 level 升到 level+1 所需经验（点）。level < 1 按第 1 级处理；
    // 计算全程用 double，最后四舍五入回整数，避免 float 在中间步骤掉精度
    int needed(int level) const
    {
        // 等级下限夹取：0/负级没有语义，防御性按第 1 级算（不崩是硬契约）
        const int safeLevel = (level < 1) ? 1 : level;

        // 基数下限夹取：0/负基数会让升级循环永不收敛（单测覆盖该边界）
        const int safeBase = (base < 1) ? 1 : base;

        // 倍率下限夹取：<1 与 NaN 都归一到 1——写法利用「NaN 比较恒 false」，
        // 取反后 NaN 也会落入「不小于 1」的失败侧被夹住
        const float safeGrowth = (growth >= 1.0f) ? growth : 1.0f;

        // 幂增长：base × growth^(level-1)。先升 double 再算，避免 float 精度
        // 在 level 稍大时把四舍五入的临界值（如 40.5）推错方向
        const double value =
            static_cast<double>(safeBase) *
            std::pow(static_cast<double>(safeGrowth), static_cast<double>(safeLevel - 1));

        // 溢出防御：inf/NaN 或超过封顶一律返回封顶值——double 越界值直接
        // 转 int 是未定义行为，这里必须先夹住（!(v <= cap) 的写法同时兜住 NaN）
        if (!(value <= static_cast<double>(kMaxNeeded)))
        {
            return kMaxNeeded;
        }

        // 四舍五入到整数：llround 是「半个单位远离零」的规则（40.5 -> 41），
        // 与规格「四舍五入」一致；结果再夹回 [1, 封顶] 双保险
        const long long rounded = std::llround(value);
        if (rounded < 1)
        {
            return 1;
        }
        return (rounded > kMaxNeeded) ? kMaxNeeded : static_cast<int>(rounded);
    }

    // 经验结算：把 gained 点经验加到进度上并循环升级（溢出一枚可连升多级）。
    // 返回结算后的等级 / 进度 / 连升级数；调用方据此逐级发强化与反馈。
    // 全程用 long long 累计：gained 与 xp 均为 int，相加可溢出 int 范围
    LevelUpResult apply(int level, int xp, int gained) const
    {
        // 等级下限归一：与 needed 同一约定（0/负级按 1 处理，不崩）
        int resultLevel = (level < 1) ? 1 : level;

        // 进度与增量的下限归一：负经验无玩法语义，忽略而不是倒扣（见头文件说明）
        long long pool = (xp > 0) ? static_cast<long long>(xp) : 0LL;
        if (gained > 0)
        {
            pool += static_cast<long long>(gained);
        }

        // 连升计数：每完成一次「扣需求 + 升级」加 1
        int levelsGained = 0;

        // 循环结算：只要进度还够本级需求就继续升级。
        // 终止性说明：needed 恒 >= 1（needed 内已夹取），pool 有限且每圈严格
        // 减掉至少 1 点，因此循环必然终止——这正是「封顶 + 下限夹取」的意义
        for (;;)
        {
            // 本级需求（配置异常时也至少为 1，保证循环收敛）
            const int need = needed(resultLevel);

            // 进度不足以再升：结算完成，跳出
            if (pool < need)
            {
                break;
            }

            // 扣掉本级需求并升 1 级：溢出的经验进入下一级的进度池
            pool -= need;
            ++resultLevel;
            ++levelsGained;
        }

        // 组装结果：循环结束后 pool 必然小于「新等级的需求」，必然装得进 int
        LevelUpResult result;
        result.level = resultLevel;
        result.xp = static_cast<int>(pool);
        result.levelsGained = levelsGained;
        return result;
    }

private:
    // 单级需求封顶（点）：1e9 已远超任何现实对局的累计经验，
    // 存在的唯一目的是把「pow 溢出 / 配置异常」挡在 int 转换之前
    static constexpr int kMaxNeeded = 1000000000;
};

#endif // !__LEVELCURVE_H__
