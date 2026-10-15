#ifndef __MONSTERAI_H__
#define __MONSTERAI_H__

#include <algorithm> // std::min / std::max：回血公式与参数夹取的上下界
#include <cmath>     // std::sqrt / std::lround：距离与百分比取整

// ============================================================
// MonsterAi：怪物 AI 状态决策（纯逻辑小类，不依赖 SDL / World）
// ============================================================
//
// 职责（批三·怪物生态）：把「巡逻 → 仇恨追击 → 栓绳回巢 → 回巢恢复」
// 的状态决策收敛成一个可单测的纯逻辑类（tests/monster_ai_test.cpp 直测）。
// 「场景注入、实体执行」的分工延续：世界校验（游荡点可走性）、随机源、
// 日志与真实位移都在 Monster / PlayScene 层完成，本类只做状态与目标决策。
//
// 状态机（规格定稿，批三）：
//   - Patrol（= Idle 与巡逻合并）：绕「巢」（出生点）小范围游荡；
//     每走到一个游荡点后停顿 1~2s（停顿即 Idle 表现，不单列状态）；
//   - Chase（仇恨）：玩家中心进入仇恨半径（<= aggroRadius）即转入；
//     无时限——直到栓绳或死亡；「攻击」不单列状态（接触伤害由场景
//     碰撞结算，「贴脸」本身就是追击行为的终点，故不设 Attack 态）；
//   - Return（栓绳回巢）：离巢距离 > leashRadius 触发；回巢途中玩家
//     再入仇恨（<= aggroRadius）立即重新 Chase；到巢（<= 
//     homeArriveEpsilon）转回 Patrol——「到巢」事件供宿主挂钩回巢回血。
//
// 边界约定（单测覆盖；恰好等于半径的行为必须明确）：
//   - 进入仇恨：distToPlayer <= aggroRadius（恰好等于也追）；
//   - 触发栓绳：distToNest   >  leashRadius（恰好等于仍追，不提前回巢）；
//   - 到巢判据：distToNest   <= homeArriveEpsilon（恰好等于算到家）；
//   - 优先级：Return 中玩家更近时先追玩家（仇恨优先于到巢）。
//
// 随机源注入：游荡点方位与停顿 1~2s 需要随机数，但本类不依赖 SDL——
// 宿主注入 [0,1) 随机函数（游戏内传 SDL_randf 的包装、单测传固定序列）；
// 未注入时退化为常量 0.5（确定性兜底：不崩、不挂、可预测）。
//
// 游荡点的可走校验在宿主：本类经 wantsPatrolTarget() 向宿主索要目标点，
// Monster 在世界内随机挑「怪物矩形可走」的点后经 setPatrolTarget 写回
// （本类不知道墙与世界边界的存在——纯逻辑与世界的边界在此）。
// ============================================================

// 怪物 AI 状态（三个独立状态；「攻击」并入 Chase 的说明见文件头）
enum class MonsterAiState
{
    Patrol, // 巡逻/游荡（含停顿——规格里的 Idle 表现）
    Chase,  // 仇恨追击（无时限：直到栓绳或死亡）
    Return  // 栓绳回巢（到巢转回 Patrol）
};

// 宿主注入的随机源：[0,1) 均匀分布；context 为宿主自备上下文（可空）
using MonsterAiRandomFn = float (*)(void* context);

class MonsterAi
{
public:
    // 决策参数（半径类来自 PlayScene 读取的配置键；其余为代码常量默认值）
    struct Config
    {
        float patrolRadius = 96.0f;       // 巡逻半径（像素）：monster_patrol_radius
        float aggroRadius = 320.0f;       // 仇恨半径（像素）：monster_aggro_radius
        float leashRadius = 500.0f;       // 栓绳半径（像素）：monster_leash_radius
        float homeArriveEpsilon = 8.0f;   // 到巢判据（像素）：距离不超过即算回到家
        float patrolArriveEpsilon = 4.0f; // 游荡点到达判据（像素）
        float pauseMinSeconds = 1.0f;     // 游荡间停顿时长下限（秒）
        float pauseMaxSeconds = 2.0f;     // 游荡间停顿时长上限（秒）
        float patrolGiveUpSeconds = 3.0f; // 单个游荡点的追赶时限（秒；走不到的兜底）
    };

    // 单步决策结果：宿主据此打日志 / 触发回血表现（每个事件只在一帧内为 true）
    struct StepResult
    {
        MonsterAiState state = MonsterAiState::Patrol; // 决策后的状态
        bool enteredChase = false;  // 本步「进入仇恨」（Patrol/Return -> Chase）
        bool enteredReturn = false; // 本步「触发栓绳回巢」（Chase -> Return）
        bool returnedHome = false;  // 本步「回到巢穴」（Return -> Patrol）
    };

    // 设置决策参数（场景 onEnter 注入一次）。非法值就地夹取：
    // 负半径/负停顿没有玩法语义——宁可按 0 处理，也不让除零/负回血类
    // 异常值进入运行期（与 PlayScene 各配置项的防御口径一致）
    void setConfig(const Config& config)
    {
        m_config = config; // 先整体拷贝，再逐项夹取
        m_config.patrolRadius = std::max(m_config.patrolRadius, 0.0f);   // 半径不可为负
        m_config.aggroRadius = std::max(m_config.aggroRadius, 0.0f);     // 同上
        m_config.leashRadius = std::max(m_config.leashRadius, 0.0f);     // 同上
        m_config.homeArriveEpsilon = std::max(m_config.homeArriveEpsilon, 0.0f); // 判据不可为负
        m_config.patrolArriveEpsilon = std::max(m_config.patrolArriveEpsilon, 0.0f); // 同上
        m_config.pauseMinSeconds = std::max(m_config.pauseMinSeconds, 0.0f); // 停顿不为负
        // 停顿上限不低于下限（写反配置时不出现「倒转的随机区间」）
        m_config.pauseMaxSeconds = std::max(m_config.pauseMaxSeconds, m_config.pauseMinSeconds);
        // 追赶时限至少给出一小段：0/负值会让每个游荡点「一步即放弃」
        m_config.patrolGiveUpSeconds = std::max(m_config.patrolGiveUpSeconds, 0.1f);
    }

    // 注入随机源（见文件头说明）；fn 传 nullptr 视为退回常量 0.5
    void setRandom(MonsterAiRandomFn fn, void* context)
    {
        m_randomFn = fn;           // 随机函数（可为空——random01 有兜底）
        m_randomContext = context; // 宿主上下文原样保存（调用时回传）
    }

    // 注入回巢回血百分比（0 = 关闭——默认；>0 按最大生命百分比回血）
    void setReturnHealPercent(float percent) { m_returnHealPercent = percent; }

    // 当前回巢回血百分比（日志/查询用）
    float returnHealPercent() const { return m_returnHealPercent; }

    // ---- 生命周期 ----

    // 生成/池复用复位：记录新巢（= 出生点）并把状态、游荡目标与停顿
    // 全部清回初始——对象池槽位复用时「旧怪物的状态绝不带进新怪物」
    void reset(float nestX, float nestY)
    {
        m_nestX = nestX; // 巢中心 x（世界坐标）：出生点即巢
        m_nestY = nestY; // 巢中心 y（同上）
        clear();         // 复用同一套状态复位（巢坐标保留刚写入的值）
    }

    // 状态复位（回收/重开用）：状态回 Patrol、清游荡目标与停顿；
    // 巢坐标保留（下次 reset 会重写；保留只是避免「无巢」的中间态，
    // 也让 clear 后查询 nestX/Y 仍是安全数值）
    void clear()
    {
        m_state = MonsterAiState::Patrol; // 状态回巡逻
        m_hasPatrolTarget = false;        // 清游荡目标（等待宿主供水新的）
        m_wantPatrolTarget = false;       // 清「索要目标」标记
        m_pauseLeft = 0.0f;               // 无停顿：下一次 update 即可索要目标
        m_patrolElapsed = 0.0f;           // 游荡计时清零
        m_hasGoal = false;                // 本步无移动目标（等供水）
        m_goalX = m_nestX;                // 目标点回巢心（防御：无目标时也不误导）
        m_goalY = m_nestY;
    }

    // ---- 每步推进 ----

    // 每固定步调用一次：先做状态转换（含边界判据），再推进巡逻计时。
    // 参数：self = 怪物中心（世界坐标）；player = 玩家中心（hasPlayer=false
    // 时忽略——玩家目标未注入的防御路径）；dt = 固定步长（秒；<=0 时
    // 只做状态转换、不推进计时，防御异常调用）
    StepResult update(float selfX, float selfY,
                      bool hasPlayer, float playerX, float playerY, float dt)
    {
        // 距离基准：一律取「中心 -> 中心」的欧氏距离（与移动方向同一口径）
        const float distNest = distance(selfX, selfY, m_nestX, m_nestY); // 离巢距离
        const float distPlayer = hasPlayer
            ? distance(selfX, selfY, playerX, playerY) // 玩家可见：真实距离
            : kNoPlayerDistance;                       // 玩家不可见：视为无穷远

        // 保存转换前状态：三个事件标志都由「前后对比」得出
        const MonsterAiState before = m_state;

        // ---- 状态转换（纯决策；边界语义见文件头）----
        MonsterAiState next = m_state; // 默认保持当前状态
        switch (m_state)
        {
        case MonsterAiState::Patrol:
            // 巡逻中：玩家进入仇恨半径 -> Chase；否则维持 Patrol
            next = (distPlayer <= m_config.aggroRadius)
                ? MonsterAiState::Chase  // 进入仇恨（恰好等于也追）
                : MonsterAiState::Patrol; // 玩家还远：继续游荡
            break;

        case MonsterAiState::Chase:
            // 追击中：离巢超过栓绳半径 -> Return；否则继续追（无时限）
            next = (distNest > m_config.leashRadius)
                ? MonsterAiState::Return // 触发栓绳（恰好等于仍追）
                : MonsterAiState::Chase; // 追击不设时限、不因玩家距离退出
            break;

        case MonsterAiState::Return:
            // 回巢中：玩家再入仇恨 -> 立即 Chase（仇恨优先于到巢）；
            // 否则到巢 -> Patrol（宿主在此事件上挂钩回巢回血）；
            // 再否则继续 Return
            if (distPlayer <= m_config.aggroRadius)
            {
                next = MonsterAiState::Chase; // 重新仇恨：回巢途中再追
            }
            else if (distNest <= m_config.homeArriveEpsilon)
            {
                next = MonsterAiState::Patrol; // 到巢：转回巡逻
            }
            else
            {
                next = MonsterAiState::Return; // 还没到家：继续走
            }
            break;
        }

        // 落定状态并生成事件标志（先对照 before 再写 m_state）
        m_state = next; // 状态落定
        StepResult result{}; // 结果对象：默认 Patrol、事件全 false
        result.state = next; // 决策后的状态（宿主查询用）
        // 三个事件都是「恰好在本步发生」的上升沿：flag = 新状态成立且旧状态不同
        result.enteredChase = (next == MonsterAiState::Chase) && (before != MonsterAiState::Chase);
        result.enteredReturn = (next == MonsterAiState::Return) && (before != MonsterAiState::Return);
        result.returnedHome = (next == MonsterAiState::Patrol) && (before == MonsterAiState::Return);

        // 任何状态切换都清掉游荡目标与停顿：旧目标只属于旧状态；
        // 回到 Patrol（含刚生成）时等宿主供水新游荡点
        if (before != next)
        {
            m_hasPatrolTarget = false;  // 旧游荡点作废
            m_wantPatrolTarget = false; // 索要标记复位（本步目标算法见下）
            m_pauseLeft = 0.0f;         // 停顿清零
            m_patrolElapsed = 0.0f;     // 追赶计时清零
        }

        // ---- 本步移动目标（由状态决定；hasGoal=false 表示原地）----
        m_hasGoal = true; // 先置位，再按状态细算
        switch (m_state)
        {
        case MonsterAiState::Patrol:
            // 巡逻：有游荡点才移动（停顿/等供水期间原地不动）
            m_hasGoal = m_hasPatrolTarget;
            if (m_hasPatrolTarget)
            {
                m_goalX = m_patrolX; // 目标 = 游荡点 x
                m_goalY = m_patrolY; // 目标 = 游荡点 y
            }
            break;

        case MonsterAiState::Chase:
            // 追击：目标是玩家；玩家目标缺失（防御路径）时退回巢心——
            // 不至于把怪物钉死原地（场景正常流程每步都会注入玩家位置）
            if (hasPlayer)
            {
                m_goalX = playerX; // 目标 = 玩家中心 x
                m_goalY = playerY; // 目标 = 玩家中心 y
            }
            else
            {
                m_goalX = m_nestX; // 缺目标：先往巢走（可被下一步注入覆盖）
                m_goalY = m_nestY;
            }
            break;

        case MonsterAiState::Return:
            // 回巢：目标是巢心
            m_goalX = m_nestX; // 目标 = 巢中心 x
            m_goalY = m_nestY; // 目标 = 巢中心 y
            break;
        }

        // ---- 巡逻推进（仅 Patrol 状态；dt<=0 防御直接跳过）----
        if (m_state == MonsterAiState::Patrol && dt > 0.0f)
        {
            updatePatrol(selfX, selfY, dt); // 到达判定/停顿倒计时/索要新目标
        }

        return result; // 交还本步决策
    }

    // ---- 查询 ----

    MonsterAiState state() const { return m_state; } // 当前状态

    float nestX() const { return m_nestX; } // 巢中心 x（世界坐标）
    float nestY() const { return m_nestY; } // 巢中心 y

    // 巡逻半径查询（像素）：宿主挑游荡点时的取样半径（夹取后的有效值）
    float patrolRadius() const { return m_config.patrolRadius; }

    // 宿主是否需要为巡逻提供一个新游荡点（世界可走校验由宿主完成）。
    // 返回 true 期间宿主每个固定步都可以重试（挑点失败不丢请求）；
    // 经 setPatrolTarget 成功补给后自动复位
    bool wantsPatrolTarget() const { return m_wantPatrolTarget; }

    // 写入宿主挑好的游荡点（世界坐标）：怪物开始朝它游荡
    void setPatrolTarget(float x, float y)
    {
        m_wantPatrolTarget = false; // 已供水：停止索要
        m_hasPatrolTarget = true;   // 有目标：随后的 update 将走向它
        m_patrolX = x;              // 目标 x（世界坐标）
        m_patrolY = y;              // 目标 y
        m_patrolElapsed = 0.0f;     // 追赶计时重新起算
    }

    // 本步是否有移动目标（false = 原地：巡逻停顿或等待宿主供水）
    bool hasGoal() const { return m_hasGoal; }

    float goalX() const { return m_goalX; } // 本步目标点 x（世界坐标）
    float goalY() const { return m_goalY; } // 本步目标点 y

    // ---- 纯函数：回巢回血 ----

    // 回到巢穴时的回血结算（规格：monster_return_heal_percent，0=关闭）：
    //   新生命 = min(hp + round(maxHp x percent / 100), maxHp)
    // 百分比 <=0（关闭）或 maxHp <=0（无上限信息）：原样返回；
    // 结果永不低于原生命（回血只会加血），超出上限的部分丢弃。
    // 独立成纯函数：单测覆盖「>0 生效 / =0 关闭 / 溢出夹取」三组边界
    static int healedHpAfterReturn(int hp, int maxHp, float healPercent)
    {
        if (maxHp <= 0 || healPercent <= 0.0f)
        {
            return hp; // 关闭或上限异常：不改动
        }
        // 回血量 = 最大生命 x 百分比（四舍五入到整点——生命是整数量）
        const int heal = static_cast<int>(std::lround(maxHp * healPercent / 100.0f));
        return std::min(hp + heal, maxHp); // 加上回血并夹到上限（溢出丢弃）
    }

private:
    // 两点距离（中心 -> 中心欧氏距离）：AI 全部判据的度量口径
    static float distance(float ax, float ay, float bx, float by)
    {
        const float dx = bx - ax; // 横向差
        const float dy = by - ay; // 纵向差
        return std::sqrt(dx * dx + dy * dy); // 欧氏距离
    }

    // 随机 [0,1)：宿主未注入随机源时返回常量 0.5（确定性兜底）
    float random01() const
    {
        return (m_randomFn != nullptr) ? m_randomFn(m_randomContext) : 0.5f;
    }

    // 巡逻推进（每步）：
    //   有游荡点：走。到达（<= 到达判据）或超过追赶时限（被墙阻隔、
    //   凹角走不到的兜底）后进入 1~2s 停顿；
    //   停顿中：倒计时；
    //   其余（停顿结束或刚生成）：向宿主索要下一个游荡点。
    // 注意：本函数不知道「移动是否成功」——位移由宿主执行，游荡点的
    // 到达/放弃用「距离 + 时限」两个纯数据判据（世界细节留在宿主）。
    void updatePatrol(float selfX, float selfY, float dt)
    {
        if (m_hasPatrolTarget)
        {
            // 追赶计时（兜底：不可达目标也能结束本段游荡，不无限滞留）
            m_patrolElapsed += dt;

            // 到达判定：怪物中心与游荡点的距离平方比较（省一次开方）
            const float dx = m_patrolX - selfX; // 到游荡点的横向差
            const float dy = m_patrolY - selfY; // 纵向差
            const float arriveEps = m_config.patrolArriveEpsilon; // 到达判据（像素）
            const bool arrived = (dx * dx + dy * dy) <= (arriveEps * arriveEps); // 已到达

            // 到达或超时：结束本段游荡，进入 1~2s 停顿
            if (arrived || m_patrolElapsed >= m_config.patrolGiveUpSeconds)
            {
                m_hasPatrolTarget = false; // 本段游荡结束（目标作废）
                // 停顿时长 = 下限 + 随机 x 区间宽（下限 1s、上限 2s 默认）
                m_pauseLeft = m_config.pauseMinSeconds +
                    random01() * (m_config.pauseMaxSeconds - m_config.pauseMinSeconds);
            }
            return; // 本步只处理「是否结束游荡」
        }

        if (m_pauseLeft > 0.0f)
        {
            m_pauseLeft -= dt; // 停顿倒计时（<=0 后下一步索要新目标）
            return;
        }

        // 停顿结束（或初生/复位后）：向宿主索要下一个游荡点。
        // 宿主未补给前每个 update 都会保持 true（重复请求无害）
        m_wantPatrolTarget = true;
    }

private:
    // 「玩家不可见」时的替代距离：远大于任何世界的直径（哨兵值）
    static constexpr float kNoPlayerDistance = 1.0e9f;

    Config m_config{}; // 决策参数（场景注入；默认值 = config 缺键回退值）

    MonsterAiState m_state = MonsterAiState::Patrol; // 当前状态（初值巡逻）

    float m_nestX = 0.0f; // 巢中心 x（reset 写入；0 为未设置的安全默认）
    float m_nestY = 0.0f; // 巢中心 y

    // ---- 巡逻/游荡 ----
    bool m_hasPatrolTarget = false;  // 是否已有游荡点（等待走达）
    bool m_wantPatrolTarget = false; // 是否正在向宿主索要游荡点
    float m_patrolX = 0.0f;          // 游荡点 x（世界坐标）
    float m_patrolY = 0.0f;          // 游荡点 y
    float m_patrolElapsed = 0.0f;    // 本游荡点已追赶时长（秒；放弃判据）
    float m_pauseLeft = 0.0f;        // 停顿时长剩余（秒；<=0 表示可索要新目标）

    // ---- 本步移动目标 ----
    bool m_hasGoal = false; // 本步是否有目标（false = 原地不动）
    float m_goalX = 0.0f;   // 目标 x（世界坐标）
    float m_goalY = 0.0f;   // 目标 y

    // ---- 宿主注入 ----
    MonsterAiRandomFn m_randomFn = nullptr; // 随机源（可空——random01 兜底）
    void* m_randomContext = nullptr;        // 随机源上下文（原样回传）

    float m_returnHealPercent = 0.0f; // 回巢回血百分比（0=关闭——默认）
};

#endif // !__MONSTERAI_H__
