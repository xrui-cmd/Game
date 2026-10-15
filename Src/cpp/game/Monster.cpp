#include "game/Monster.h"
#include "game/World.h"     // World：移动的分轴碰撞滑动（slideMove）与落点可走查询
#include "engine/Camera.h"  // Camera：渲染时「世界坐标 -> 屏幕坐标」的换算
#include <algorithm> // std::clamp / std::max：闪白计时、滑移权重与战斗数值的上下界
#include <cmath>     // std::sqrt：归一化方向、距离与侧滑权重的计算

// 本编译单元私有的变体数值表与工具（不对外暴露）
namespace
{

// AI 随机源适配（批三）：MonsterAi 只认 [0,1) 的函数指针接口（纯逻辑
// 不依赖 SDL），这里把 SDL_randf 包一层交给它——游戏内随机、单测内可注入
// 固定序列，随机源的可控性由此而来
float monsterAiRandom(void* /*context*/)
{
    return SDL_randf(); // 复用 SDL 全局随机（与刷新/出生取点同一随机流）
}

// 变体规格：外观纹理 + 战斗数值 + 击杀掉落。三变体共用同一套 AI，
// 差异只体现在这张表里（换平衡先改这里）
struct VariantSpec
{
    const char* name;       // 变体名（日志展示用）
    const char* sheetPath;  // 精灵表相对路径（4 帧横向排列，每帧 48x48）
    const char* singlePath; // 单帧回退相对路径（精灵表缺失时的降级素材）
    int hp;                 // 出生生命值（点）
    float baseSpeed;        // 基础追击速度（像素/秒）：难度成长在此之上叠加
    int coinDrop;           // 击杀掉落金币数（枚）——局内货币的经济常量
    SDL_Color fallback;     // 程序化回退颜色（两种素材都缺失时的兜底方块色）
};

// 变体数值表（下标 = MonsterType 枚举值，顺序必须与枚举一致）。
// 数值为「代码常量、硬编码并注释」——本批定案不走配置文件：
//   - 史莱姆：慢而脆（生命 20 / 速度 90 / 掉落金币 1）
//   - 哥布林：基准线（生命 30 / 速度 110 / 掉落金币 2，与旧单怪时代数值一致）
//   - 骷髅  ：快而脆（生命 18 / 速度 140 / 掉落金币 1，追逐压力来源）
// 颜色取自各素材的主色系（回退方块与变体观感相近，一眼能认出是哪只）
constexpr VariantSpec kVariants[] = {
    // 史莱姆（swampy）：绿色软泥怪，慢吞吞的低压目标（赏金 1 枚）
    { "史莱姆", "assets/monster_slime_sheet.png", "assets/monster_slime.png",
      20, 90.0f, 1, SDL_Color{124, 200, 74, 255} },
    // 哥布林（goblin）：基准变体，数值与旧单怪时代一致（赏金 2 枚——最「值钱」）
    { "哥布林", "assets/monster_goblin_sheet.png", "assets/monster_goblin.png",
      30, 110.0f, 2, SDL_Color{74, 142, 64, 255} },
    // 骷髅（skelet）：白骨骷髅，快而脆（赏金 1 枚）
    { "骷髅", "assets/monster_skelet_sheet.png", "assets/monster_skelet.png",
      18, 140.0f, 1, SDL_Color{230, 228, 210, 255} },
};

// 变体枚举 -> 数值表下标（Count 哨兵值不在表内，做一个防御性夹取）
int variantIndex(MonsterType type)
{
    // 枚举值直接作为下标使用；越界（未来加枚举忘加表项）夹到最后一项，
    // 宁可外观/数值不符也绝不越界读表
    return std::clamp(static_cast<int>(type), 0, static_cast<int>(MonsterType::Count) - 1);
}

} // 匿名命名空间结束

// 变体名查询：日志展示用（「怪物出现（第 1 只，骷髅）」这类文案）
const char* monsterTypeName(MonsterType type)
{
    // 查表返回变体名；越界情形由 variantIndex 夹取保护（见上）
    return kVariants[variantIndex(type)].name;
}

// 按指定变体加载/重载纹理
bool Monster::loadVariant(SDL_Renderer* renderer, MonsterType type)
{
    // 记录变体类型：baseSpeed()/spawnAt 的出生生命都从它查表
    m_type = type;

    // 取出本变体的规格（路径、帧参数外的数值、回退色）
    const VariantSpec& spec = kVariants[variantIndex(type)];

    // 换皮前先释放旧纹理（重点）：loadTextureWithFallback 每次都会新建纹理
    // 并覆写 m_texture——不先释放的话，对象池槽位反复换变体时旧纹理的显存
    // 会持续泄漏。release 幂等，首次加载（m_texture 为空）调用也无害
    release();

    // 三级回退加载本变体素材：精灵表（4 帧）-> 单帧图 -> 程序化纯色
    return loadTextureWithFallback(renderer,
                                   spec.sheetPath,     // 首选：本变体精灵表相对路径
                                   spec.singlePath,    // 次选：本变体单帧精灵相对路径
                                   kFrameCount,        // 精灵表帧数：4 帧
                                   kDefaultFrameTime,  // 每帧时长：默认 0.2 秒
                                   spec.fallback);     // 回退颜色：变体主色系方块
}

// 本变体的基础追击速度（像素/秒）：难度成长在其上叠加（由场景完成）
float Monster::baseSpeed() const
{
    // 查表返回基础速度；越界情形由 variantIndex 夹取保护
    return kVariants[variantIndex(m_type)].baseSpeed;
}

// 本变体的击杀金币掉落数（枚）：场景在击杀结算时读取（史莱姆 1 / 哥布林 2 / 骷髅 1）
int Monster::coinDrop() const
{
    // 查表返回掉落数；越界情形由 variantIndex 夹取保护
    return kVariants[variantIndex(m_type)].coinDrop;
}

// 设置追踪目标：只存目标矩形的中心点——AI 的仇恨判定与追迹都以此为玩家位置
void Monster::setTarget(const SDL_FRect& targetRect)
{
    m_targetX = targetRect.x + targetRect.w * 0.5f; // 目标中心 x：矩形左上角 + 半宽
    m_targetY = targetRect.y + targetRect.h * 0.5f; // 目标中心 y：矩形左上角 + 半高
    m_hasTarget = true;                             // 标记玩家可见：AI 开始做仇恨判定
}

// 受击：扣血 + 闪白；返回是否死亡（生命归零）
bool Monster::takeDamage(int amount)
{
    // 负数伤害是调用方错误（等价于治疗且逻辑不通）：警告并忽略，状态不变
    if (amount < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "怪物受击伤害不能为负（收到 %d），忽略本次受击", amount);
        return false; // 未死亡（也没有扣血）
    }

    // 扣血并把生命夹到 0：日志/血条永远看不到负数生命
    m_hp = std::max(m_hp - amount, 0);

    // 闪白计时重置为满值：连续受击时白光从本次受击重新起算（视觉连续）
    m_flashLeft = kHitFlashSeconds;

    // 生命归零：转入「未激活」——不再更新/渲染/碰撞，槽位等 spawnAt 复用
    if (m_hp == 0)
    {
        m_alive = false;     // 本对象退场（调用方负责击杀计数与掉落结算）
        m_hasTarget = false; // 清追踪目标：复用前不残留旧目标点
        return true;         // 通知调用方：本次受击致死，走死亡结算分支
    }

    return false; // 仍有生命：调用方通常接着施加击退
}

// 生成（批三正式版）：在调用方选好的可信落点上激活本对象
void Monster::spawnAt(float centerX, float centerY)
{
    // 落点换算：入参是「怪物中心」的世界坐标，转成左上角存内部
    //（位置成员一直是左上角语义——与 rect()/渲染同一口径）
    m_x = centerX - m_w * 0.5f; // 左上角 x = 中心 - 半宽
    m_y = centerY - m_h * 0.5f; // 左上角 y = 中心 - 半高

    // 激活 + 按变体数值表写入战斗数值：出生生命/上限都取自当前变体
    //（stale 防护：至少 1 点——表内数值恒为正，这里的 max 是防未来改表的兜底）
    const VariantSpec& spec = kVariants[variantIndex(m_type)]; // 本变体规格
    m_alive = true;                     // 本对象进入战场：更新/渲染/碰撞自此生效
    m_maxHp = std::max(spec.hp, 1);     // 生命上限（血条比例与回血夹取都读它）
    m_hp = m_maxHp;                     // 出生满血（0 血出生会在首帧判定为已死）
    m_flashLeft = 0.0f;                 // 新生怪物不带受击光效
    m_hasTarget = false;                // 玩家目标等场景下个固定步注入

    // AI 复位（批三）：随机源就位 -> 巢 = 出生点（出生点即巢）-> 状态全量
    // 复位（巡逻、无游荡点、无停顿）——池槽位复用时旧怪物的仇恨/回巢
    // 状态绝不带进新怪物
    m_ai.setRandom(&monsterAiRandom, nullptr); // 随机源：SDL 随机流适配
    m_ai.reset(centerX, centerY);              // 巢 = 本落点；状态清回巡逻

    // 瞬移类操作的插值收尾（重点）：出生是「瞬间换位」，必须把插值快照
    // 对齐到新落点，否则渲染会从上一位置向新落点拉丝（延续既有纪律）
    //（落点的「激活区块内 + 可走 + 离玩家 >= 500px」校验由调用方完成）
    snapshotPreviousPosition();
}

// 被撞开/被击退：从 (fromX, fromY)（伤害来源中心）指向怪物中心的方向
// 瞬移 distance 像素（落点经世界可走校验，逐级缩短重试）
void Monster::knockback(float fromX, float fromY, float distance)
{
    // 怪物当前中心：击退位移以「中心」为参照计算，避免用左上角坐标推位移时
    // 把尺寸差带进方向判定（两者对方向的影响相同，但对落点更直观）
    const float centerX = m_x + m_w * 0.5f;
    const float centerY = m_y + m_h * 0.5f;

    // 方向向量：伤害来源中心 -> 怪物中心（「远离来源」的方向）
    const float dx = centerX - fromX;
    const float dy = centerY - fromY;
    const float length = std::sqrt(dx * dx + dy * dy);

    // 归一化方向；两个中心几乎重合（距离过小）时退回默认向右，
    // 避免除以接近 0 的长度产生 NaN 或异常位移
    float dirX = 1.0f; // 默认向右
    float dirY = 0.0f; // 默认水平方向
    if (length > kArriveEpsilon)
    {
        dirX = dx / length; // 单位方向 x：dx 占全长的比例
        dirY = dy / length; // 单位方向 y：dy 占全长的比例
    }

    // 世界未注入（防御）：退化为无校验位移——直接按全距离瞬移
    if (m_world == nullptr)
    {
        m_x = centerX + dirX * distance - m_w * 0.5f; // 左上角 x = 新中心 - 半宽
        m_y = centerY + dirY * distance - m_h * 0.5f; // 左上角 y = 新中心 - 半高
        snapshotPreviousPosition();                    // 瞬移收尾：快照对齐防拉丝
        return;
    }

    // 落点校验（批二落地、批三沿用）：从全距离开始，被墙挡则按 1/2、1/4
    // 逐级缩短重试。「贴墙可接受」的口径下不允许落点推进墙内——否则怪物
    // 会卡在墙里两轴都出不来（而贴墙边站位是允许的：矩形与墙格相交才被拒）
    for (float factor = 1.0f; factor >= 0.25f; factor *= 0.5f)
    {
        // 候选落点（左上角坐标）：中心 + 方向 x 距离 x 比例 - 半尺寸
        const float candidateX = centerX + dirX * distance * factor - m_w * 0.5f;
        const float candidateY = centerY + dirY * distance * factor - m_h * 0.5f;

        // 候选矩形可走：采用并结束（候选矩形 = 怪物尺寸的 AABB）
        const SDL_FRect candidate{ candidateX, candidateY, m_w, m_h };
        if (m_world->isRectWalkable(candidate))
        {
            m_x = candidateX; // 采用候选落点 x
            m_y = candidateY; // 采用候选落点 y
            // 瞬移类操作的插值收尾：落点定稿后快照 prev=cur，渲染不拉丝
            snapshotPreviousPosition();
            return;
        }
    }

    // 全距离档位都不可走（贴墙死角/被包围）：原地不动——
    // 比「卡进墙里出不来」好；位置未变，插值区间保持原样（无需快照）
}

// 覆写更新：AI 状态决策 + 帧动画 + 闪白计时 + 朝目标点移动（移动经世界碰撞）。
// 分工：AI（MonsterAi）只做状态/目标决策；本函数把决策执行成位移，
// 并在状态切换/到巢事件上打日志、结回血——「场景注入、实体执行」的延续
void Monster::update(const UpdateContext& ctx)
{
    // 第一行固定动作：快照「本步开始前的位置」作为渲染插值起点。
    // 覆写 update 会跳过 Entity::update，这条调用必须自己补上
    snapshotPreviousPosition();

    // 帧动画无条件推进（基类默认实现的同款语句）：移动与否不影响动画节奏，
    // 跑动/蠕动动画在巡逻/追击全程持续播放
    m_animTime += ctx.deltaTime;

    // 受击闪白计时递减并夹到 0：与固定步长同源，暂停时随 update 门控冻结
    if (m_flashLeft > 0.0f)
    {
        m_flashLeft = std::max(m_flashLeft - ctx.deltaTime, 0.0f);
    }

    // 怪物中心（世界坐标）：AI 判定的自身位置基准
    const float centerX = m_x + m_w * 0.5f; // 中心 x
    const float centerY = m_y + m_h * 0.5f; // 中心 y

    // ---- AI 状态决策（纯逻辑）：巡逻/仇恨/栓绳回巢/到巢 ----
    // 玩家位置 = 场景每步注入的追踪目标（m_hasTarget=false 时 AI 视为
    // 玩家不可见——不仇恨；防御路径，正常流程每步都会注入）
    const MonsterAi::StepResult ai = m_ai.update(centerX, centerY,
                                                 m_hasTarget, m_targetX, m_targetY,
                                                 ctx.deltaTime);

    // ---- 状态事件日志（批三冒烟断言点，含变体名）----
    if (ai.enteredChase)
    {
        // 冒烟观测点：进入仇恨（自动化测试据此断言「靠近才追」）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "进入仇恨（%s）", monsterTypeName(m_type));
    }
    if (ai.enteredReturn)
    {
        // 冒烟观测点：触发栓绳回巢（自动化测试据此断言「拉怪过线即返」）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "触发栓绳回巢（%s）", monsterTypeName(m_type));
    }
    if (ai.returnedHome)
    {
        // 到巢事件（冒烟观测点）：先按注入的百分比结算回血（0=关闭），
        // 再打「回到巢穴」日志；真回血了补一条「回巢回血」明细日志
        const int hpBefore = m_hp; // 回血前生命（对比日志用）
        m_hp = MonsterAi::healedHpAfterReturn(m_hp, m_maxHp, m_ai.returnHealPercent());
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "回到巢穴（%s）", monsterTypeName(m_type));
        if (m_hp > hpBefore)
        {
            // 冒烟观测点：回血明细（monster_return_heal_percent>0 时的验证点）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "回巢回血（%s：%d -> %d / %d）",
                        monsterTypeName(m_type), hpBefore, m_hp, m_maxHp);
        }
    }

    // ---- 巡逻点供水（宿主职责）：AI 索要游荡点时，在世界内挑一个
    //「怪物矩形可走」的随机点写回（AI 不知道墙的存在）----
    if (m_ai.wantsPatrolTarget())
    {
        const float nestX = m_ai.nestX(); // 巢心 x：取点圆心
        const float nestY = m_ai.nestY(); // 巢心 y
        const float patrolRadius = m_ai.patrolRadius(); // 巡逻半径（配置注入）
        bool supplied = false; // 是否成功挑到有效游荡点

        for (int attempt = 0; attempt < kPatrolTargetAttempts; ++attempt)
        {
            // 圆内均匀取点：方位角均匀 + 半径取平方根（面积均匀分布——
            // 直接线性取半径会让点集中在圆心附近）
            const float angle = SDL_randf() * 6.283185307179586f; // 随机方位角（弧度）
            const float radius = std::sqrt(SDL_randf()) * patrolRadius; // 均匀半径
            const float targetX = nestX + std::cos(angle) * radius; // 候选点 x
            const float targetY = nestY + std::sin(angle) * radius; // 候选点 y

            // 候选点可走校验：怪物矩形（48x48）在候选点必须无墙。
            // 无世界（防御路径）直接接受——没有墙的概念就没有校验
            const SDL_FRect candidate{ targetX - m_w * 0.5f, // 候选矩形左上角 x
                                       targetY - m_h * 0.5f, // 左上角 y
                                       m_w, m_h };           // 怪物尺寸
            if (m_world == nullptr || m_world->isRectWalkable(candidate))
            {
                m_ai.setPatrolTarget(targetX, targetY); // 供水：开始朝它游荡
                supplied = true;                        // 标记成功
                break;                                  // 结束重试
            }
        }

        if (!supplied)
        {
            // 重试耗尽（屋子角落贴满墙等极端情形）：退化为「回巢心」——
            // 巢必可走（出生落点校验保证），不会死循环也不会卡进墙
            m_ai.setPatrolTarget(nestX, nestY);
        }
    }

    // 无移动目标（巡逻停顿/等供水）：原地只播动画（防御性分支）
    if (!m_ai.hasGoal())
    {
        return;
    }

    // 从怪物中心指向目标点（AI 决策的目标：游荡点/玩家/巢心）的向量
    const float dx = m_ai.goalX() - centerX; // 目标向量 x
    const float dy = m_ai.goalY() - centerY; // 目标向量 y
    const float distance = std::sqrt(dx * dx + dy * dy); // 到目标距离

    // 已到达目标附近：原地不动。阈值判定避免目标点附近的来回抖动，
    // 也排除距离为 0 时归一化「除以零」的退化情形
    if (distance <= kArriveEpsilon)
    {
        return;
    }

    // 本步速度：巡逻用「变体基础速度 x 0.5」（不随难度成长——闲逛节奏
    // 与追兵压迫感解耦）；追击/回巢用场景注入的难度速度
    const float speed = (m_ai.state() == MonsterAiState::Patrol)
        ? baseSpeed() * kPatrolSpeedFactor // 巡逻速度（慢半拍，肉眼可辨状态）
        : m_chaseSpeed;                    // 追击/回巢速度（难度曲线注入）

    // 沿归一化方向 x 速度的「期望位移」（固定步长积分）。归一化保证
    // 任意方向合速度恒等于本轮速度值
    const float stepDX = (dx / distance) * speed * ctx.deltaTime; // 期望水平位移
    const float stepDY = (dy / distance) * speed * ctx.deltaTime; // 期望垂直位移

    // 位移统一出口：世界碰撞（分轴滑动 + 子步防穿透）+ 卡死检测 + 持久化侧滑
    moveWithCollision(stepDX, stepDY, ctx.deltaTime);
}

// 位移推进（含墙体碰撞 + 撞墙时的轻量侧滑）：三种状态共用的移动出口
void Monster::moveWithCollision(float stepDX, float stepDY, float /*dt*/)
{
    // 世界未注入（防御分支）：无碰撞直接位移，不做任何阻挡
    if (m_world == nullptr)
    {
        m_x += stepDX; // 直接推进 x
        m_y += stepDY; // 直接推进 y
        return;        // 本步移动结束
    }

    // ---- 主方向：分轴滑动（与玩家移动共用同一个纯函数，行为一致）----
    // 一轴被墙挡住、另一轴照走——「贴墙滑行」的手感来源；内部还带子步
    // 拆分防高速穿透
    const SDL_FPoint moved = m_world->slideMove(m_x, m_y, m_w, m_h, stepDX, stepDY);
    const float movedDX = moved.x - m_x; // 碰撞后的实际水平位移
    const float movedDY = moved.y - m_y; // 碰撞后的实际垂直位移

    // 卡住检测：期望位移² vs 实际位移²（比较用平方，省一次开方）
    const float expectedSq = stepDX * stepDX + stepDY * stepDY;  // 期望位移平方
    const float movedSq = movedDX * movedDX + movedDY * movedDY; // 实际位移平方

    if (expectedSq > 0.0f && movedSq < expectedSq * kStuckMoveRatioSquared)
    {
        // ---- 轻量侧滑：正面撞墙「几乎没动」时，把期望位移旋转 90 度
        //（先右后左）再试——能滑就沿墙爬行、两侧皆堵就原地 ----
        const SDL_FPoint slideRight = m_world->slideMove(m_x, m_y, m_w, m_h, -stepDY, stepDX); // 右侧滑（顺时针 90°）
        const SDL_FPoint slideLeft = m_world->slideMove(m_x, m_y, m_w, m_h, stepDY, -stepDX);  // 左侧滑（逆时针 90°）

        // 两候选侧滑的实际位移平方：评判「哪一侧更能动」
        const float rightMovedSq = (slideRight.x - m_x) * (slideRight.x - m_x) +
                                   (slideRight.y - m_y) * (slideRight.y - m_y); // 右滑位移²
        const float leftMovedSq = (slideLeft.x - m_x) * (slideLeft.x - m_x) +
                                  (slideLeft.y - m_y) * (slideLeft.y - m_y);    // 左滑位移²

        // 选择规则：在「比主方向结果更能动」的候选中取更优者；
        // 两侧都不如主方向（凹角死角）则落回主方向结果（基本原地）
        if (rightMovedSq > movedSq && rightMovedSq >= leftMovedSq)
        {
            m_x = slideRight.x; // 采用右侧滑结果 x
            m_y = slideRight.y; // 采用右侧滑结果 y
        }
        else if (leftMovedSq > movedSq)
        {
            m_x = slideLeft.x; // 采用左侧滑结果 x
            m_y = slideLeft.y; // 采用左侧滑结果 y
        }
        else
        {
            m_x = moved.x; // 两侧皆堵：落回主方向结果（截停后的位置）
            m_y = moved.y;
        }
    }
    else
    {
        // 正常推进（含单轴贴墙滑动的常见情形）
        m_x = moved.x; // 采用主方向滑动结果 x
        m_y = moved.y; // 采用主方向滑动结果 y
    }
}

// 覆写渲染：常规帧 + 受击闪白帧 + 头顶血条（均经相机世界->屏幕换算）
void Monster::render(SDL_Renderer* renderer, float alpha, const Camera& camera) const
{
    // 基础绘制（含未加载纹理时的安全跳过）：按插值位置画当前动画帧
    //（基类内部完成世界坐标 -> 屏幕坐标的相机换算）
    Entity::render(renderer, alpha, camera);

    // 闪白叠加：只有「正在闪白」且纹理已加载时执行
    if (m_flashLeft > 0.0f && m_texture != nullptr)
    {
        // 受击闪白 = 用加色混合（ADD）把同一帧再画一遍：像素颜色与自身相加后
        // 整体提亮，各变体本体色被推亮成偏白的高光（史莱姆的绿、骷髅的白骨
        // 都会明显「发光」一下）。
        // 为什么不用颜色调制（SDL_SetTextureColorMod）：乘法调制只能压暗，
        // 无法把本体「提亮成白光」；叠加混合才能做出闪光效果
        SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_ADD);
        Entity::render(renderer, alpha, camera); // 第二次绘制：叠加提亮（同一相机换算）
        // 还原默认混合模式：纹理状态若残留 ADD，会污染后续普通绘制
        SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_BLEND);
    }

    // ---- 头顶血条（批三）：受损后显示、满血隐藏 ----
    // 满血（或异常上限）不画：轻量读血只在「需要关心血量」时占用画面
    if (m_hp >= m_maxHp || m_maxHp <= 0)
    {
        return; // 满血：无血条
    }

    // 插值位置：与本体绘制使用同一条「prev -> cur 按 alpha 线性插值」公式
    //（血条跟着精灵走，快速移动/相机跟随都不脱节）
    const float interpX = m_prevX + (m_x - m_prevX) * alpha; // 插值左上角 x
    const float interpY = m_prevY + (m_y - m_prevY) * alpha; // 插值左上角 y

    // 血条几何（世界坐标）：水平居中于怪物、悬在头顶上方（留 kHpBarGapY 间隙）
    const float barCenterX = interpX + m_w * 0.5f;             // 血条水平中心
    const float barTopY = interpY - kHpBarGapY - kHpBarHeight; // 血条顶边 y

    // 世界 -> 屏幕换算（血条是屏幕空间绘制：在当前无缩放的相机下与
    // 世界空间尺寸一致，换算后直接按像素绘制矩形）
    const Vec2 screen = camera.worldToScreen(barCenterX, barTopY);

    // 血条外沿（近黑色描边）：让血条在深色地板/墙上仍有轮廓可辨
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // 近黑：描边色
    const SDL_FRect barOutline{ screen.x - kHpBarWidth * 0.5f - 1.0f, // 外扩 1 像素
                                screen.y - 1.0f,                       // 上沿外扩 1 像素
                                kHpBarWidth + 2.0f,                    // 宽度两侧各 1 像素
                                kHpBarHeight + 2.0f };                 // 高度上下各 1 像素
    SDL_RenderFillRect(renderer, &barOutline);

    // 血条底色（深色底）：满宽铺底——剩余生命的比例对照背景读出
    SDL_SetRenderDrawColor(renderer, 45, 32, 34, 255); // 深红灰：底色
    const SDL_FRect barBack{ screen.x - kHpBarWidth * 0.5f, // 左边界（居中）
                             screen.y,                       // 顶边界
                             kHpBarWidth,                    // 满宽 40
                             kHpBarHeight };                 // 高 5
    SDL_RenderFillRect(renderer, &barBack);

    // 血条前景（红色）：宽度 = 满宽 x 生命比例（比例夹到 [0,1] 防御异常值）
    const float ratio = std::clamp(static_cast<float>(m_hp) / static_cast<float>(m_maxHp),
                                   0.0f, 1.0f); // 生命比例
    SDL_SetRenderDrawColor(renderer, 220, 60, 60, 255); // 亮红：生命前景
    const SDL_FRect barFill{ barBack.x,        // 与底色同左边界
                             barBack.y,        // 与底色同顶边界
                             barBack.w * ratio, // 按比例缩宽
                             barBack.h };      // 与底色同高
    SDL_RenderFillRect(renderer, &barFill);
}
