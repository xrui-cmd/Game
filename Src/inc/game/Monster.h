#ifndef __MONSTER_H__
#define __MONSTER_H__

#include "engine/Entity.h"     // Entity：实体基类（空间/纹理/帧动画公共能力）
#include "game/MonsterAi.h"    // MonsterAi：AI 状态决策（纯逻辑，批三巡逻/仇恨/栓绳）

// 前置声明：世界（碰撞）与相机（渲染换算）在本类中只以指针/引用出现，
// 完整定义由 Monster.cpp 的包含获得（减少头依赖）
class World;
class Camera;

// ============================================================
// Monster：怪物实体（三变体：史莱姆 / 哥布林 / 骷髅）
// ============================================================
//
// 本批改造（批三·怪物生态）：行为升级为「巡逻/仇恨/栓绳回巢」状态机，
// 决策核心抽到 game/MonsterAi.h（纯逻辑、可单测），本类负责执行：
// 移动（含绕墙轻量改进）、动画、受击红染（批五A）、血条、日志与回血落账。
//
// 继承自 Entity 获得的能力（与 Player/XpGem 共用，不再各自复制）：
//   - 位置/尺寸数据与 rect()、setPosition()、width()/height() 查询
//   - 纹理生命周期：release() 幂等释放，析构函数兜底
//   - 三级回退纹理加载：精灵表 -> 单帧图 -> 程序化纯色（loadTextureWithFallback）
//
// 本类自己的规则：
//   1. 变体数值表（常量写死并注释，不走配置文件）：
//        - 史莱姆：生命 20 / 速度 90 / 掉落金币 1 —— 慢吞吞的软柿子
//        - 哥布林：生命 30 / 速度 110 / 掉落金币 2 —— 基准线（赏金略高）
//        - 骷髅  ：生命 18 / 速度 140 / 掉落金币 1 —— 快而脆的压力型目标
//      速度的实际值 = 变体基础速度 + 难度成长 x 局内秒数，封顶 260
//      （成长与封顶都由 PlayScene 注入，本类只提供 baseSpeed()）
//   2. 纹理：每个变体一套「精灵表 + 单帧回退」，4 帧 / 每帧 48x48
//      （16x16 像素 x 3 倍放大）：
//        monster_slime_sheet.png   史莱姆（原始帧：swampy 软泥怪动画）
//        monster_goblin_sheet.png  哥布林（原始帧：goblin run）
//        monster_skelet_sheet.png  骷髅（原始帧：skelet run）
//      loadVariant() 按需切换变体纹理：对象池槽位复用时先换皮再生成。
//      换变体前必须先释放旧纹理（本类在加载前调用 release()）——
//      loadTextureWithFallback 每次都会新建纹理并覆写指针，不先释放
//      会把旧纹理的显存泄漏掉
//   3. 行为（批三核心——「场景注入、实体执行」）：
//      怪物不持有玩家指针，也不查询全局状态；PlayScene 每个固定步在
//      update 前调用 setTarget()/setChaseSpeed()，把「玩家位置」与
//      「本步速度」注入进来，再调用 update。状态决策（巡逻/仇恨/栓绳/
//      回巢）由 MonsterAi 纯逻辑类完成：
//        - Patrol：绕巢（= 出生点）小范围游荡，游荡间停顿 1~2s；
//        - Chase：玩家进入仇恨半径（320px）后追击（无时限，直到栓绳或死亡）；
//        - Return：离巢超过栓绳半径（500px）后回巢，到巢转回 Patrol；
//        - 回巢回血：到巢事件挂钩，按注入的百分比回血（默认 0 = 关闭）；
//      位移经世界碰撞处理（分轴滑动 + 卡死检测 + 持久化侧滑，见 update
//      与 moveWithCollision 内注释）。巡逻速度 = 变体基础速度 x 0.5
//      （不随难度成长——挑战来自追击；难度曲线只作用于 Chase/Return）
//   4. 战斗反馈（批五A 改版，玩家/怪物同款）：受击红染（约 0.18s，render
//      叠加 ADD 红光——旧版白闪退役；与场景的玩家受击红染同技法同色调）
//      + 击退照旧（沿背离伤害来源的方向瞬移，落点做世界可走校验）；死亡
//      转「未激活」（对象池语义：不再更新/渲染/碰撞，槽位等下一次
//      spawnAt 复用）
//   5. 生成 / 击退 / 回收都是「状态突变类操作」：落点定稿后必须快照
//      prev=cur，否则渲染插值会从旧位置拉丝到新位置（延续既有纪律）
//   6. 动画节奏：4 帧循环、每帧 0.2 秒（代码常量），由基类 render 的
//      「累计时间 / 帧时长」换算，与渲染帧率解耦
//   7. 出生（批三正式版）：PlayScene 在「激活区块内随机 walkable 位置、
//      离玩家 >= 500px」选好落点后经 spawnAt 写入——出生点即「巢」，
//      怪物从此绕它巡逻（落点规则属玩法层，场景负责选择）
//   8. 头顶血条（批三新增）：受损后（hp < maxHp）在头顶绘制小血条
//      （40x5、深色底 + 红色按比例填充；屏幕空间绘制经相机换算）；
//      满血隐藏——「看一眼血量、不看面板」的轻量读血方式
//   9. 碰撞盒（批四·碰撞体积与视觉解耦）：构造时设置「脚部小盒」
//      36x30、底部内收 6px（setCollisionBox）——移动滑动、击退落点、
//      游荡点校验、玩家互碰全部改用碰撞盒；渲染/血条照旧用完整精灵
//      （48x48）。俯视角下「身体可叠压墙脚、脚盒不穿透」
//  10. 攻击（批五A 升级·蓄力前扑强化版，替换批四轻量版）：玩家进入接触
//      范围（双方碰撞盒触接 + 小余量）且冷却就绪时出招——
//        - 蓄力 0.35s：移速归零 + 后倾加深 8px（沿出招反方向后退，
//          给玩家躲避窗口；方向在出招瞬间朝玩家一次性锁定）+ 头顶警示
//          「!」（alert 纹理由场景注入，蓄力期间显示、前扑开始消失）；
//        - 前扑：朝锁定方向冲 24px / 0.12s（快出慢收曲线——初速为
//          2 倍平均、线性收势到 0），期间拖 1~2 重渐隐残影（复用武器
//          残影思路：半透明副本画在两个自然采样点上）；
//        - 结算：玩家仍在接触范围 = Hit、否则 Miss（蓄力落空）；
//        - 落空收势：Miss 后原地收势 0.2s 再回追击（扑空的恢复节奏）；
//      结果经 consumeAttackResult 交场景消费（扣血/无敌/回弹/音效/
//      命中爪痕特效都是场景职责）；出招后进入冷却（monster_attack_cooldown
//      注入）。出招瞬间的红色脉冲（0.1s）保留——素材复用不加新图
// ============================================================

// 怪物攻击结算结果（批四）：Monster 每步至多发布一次，场景消费一次
enum class MonsterAttackResult
{
    None, // 本步无结算（未构成一次完整出招）
    Miss, // 前扑结束、玩家已不在接触范围（蓄力落空——躲开了）
    Hit   // 前扑结束、玩家仍在接触范围（是否真扣血由场景按无敌状态裁决）
};

// 怪物变体枚举：等权随机刷新的取值范围（Count 恒为变体数量，用作随机上界）
enum class MonsterType
{
    Slime,  // 史莱姆：生命 20 / 速度 90 / 掉落金币 1（慢而拖延）
    Goblin, // 哥布林：生命 30 / 速度 110 / 掉落金币 2（基准线）
    Skelet, // 骷髅：生命 18 / 速度 140 / 掉落金币 1（快而脆）
    Count   // 变体总数（哨兵值）：SDL_rand(Count) 实现等权随机时的上界
};

// 变体名（日志展示用，如「怪物出现（第 1 只，骷髅）」）
const char* monsterTypeName(MonsterType type);

class Monster : public Entity
{
public:
    // 默认帧时长：怪物动画节奏固定 0.2 秒/帧（代码常量，不进配置文件）
    static constexpr float kDefaultFrameTime = 0.2f; // 默认每帧显示时长（秒）

    // 构造：固定怪物尺寸为 48x48（与精灵表的单帧尺寸一致）
    Monster()
    {
        m_w = kSize; // 宽 48 像素：变体帧宽 16px x 3 倍放大（见 tools/compose_sprites.ps1）
        m_h = kSize; // 高 48 像素：变体帧高 16px x 3 倍放大

        // 脚部碰撞盒（批四）：48x48 全身像中只取脚部 36x30 的小盒——
        // 宽居中（右移 6px）、底边内收 6px（上移 12px）；墙滑动/落点校验/
        // 玩家互碰都改用它，精灵身体可叠压墙脚而不穿透（判定/渲染解耦）
        setCollisionBox(kCollisionBoxW, kCollisionBoxH,
                        kCollisionBoxOffsetX, kCollisionBoxOffsetY);
    }

    // 按指定变体加载/重载纹理：设置变体类型 -> 释放旧纹理 -> 三级回退加载
    // 该变体的「精灵表 -> 单帧图 -> 程序化纯色」。对象池槽位复用时由
    // PlayScene 在 spawnAt 之前调用（换皮）；重复加载同变体也无害
    bool loadVariant(SDL_Renderer* renderer, MonsterType type);

    // 当前变体类型（日志/查询用）
    MonsterType type() const { return m_type; }

    // 本变体的基础追击速度（像素/秒）：PlayScene 在它之上叠加难度成长
    // （monster_speed_growth x 局内秒数）并封顶，再经 setChaseSpeed 注入
    float baseSpeed() const;

    // 本变体的击杀金币掉落数（枚）：史莱姆 1 / 哥布林 2 / 骷髅 1。
    // 查变体表（唯一数据源）；场景在击杀结算时按它掉落
    int coinDrop() const;

    // 设置本步的追踪目标点（传玩家「碰撞盒」，内部取中心存储；批四起
    // 同时留存完整盒——攻击接触范围判定用）。PlayScene 每个固定步在
    // update 前调用——「场景注入、实体执行」的分工：目标切换、难度曲线
    // 等玩法规则全部留在场景层。它同时是 AI「仇恨判定」的玩家位置来源
    // （玩家可见 = hasTarget）
    void setTarget(const SDL_FRect& targetRect);

    // 清除追踪目标：之后 AI 视为「玩家不可见」（不仇恨；追击防御性
    // 走回巢心）；场景正常运行期每步都会重新注入，故不会长期为空
    void clearTarget() { m_hasTarget = false; }

    // 设置本步移动速度（像素/秒）：PlayScene 按难度曲线算好后每步注入。
    // 作用于 Chase 与 Return 两个「执行位移」的状态；巡逻速度由本类
    // 按变体基础速度换算（不随难度成长）
    void setChaseSpeed(float speed) { m_chaseSpeed = speed; }

    // 注入 AI 决策参数（场景 onEnter 对所有槽位调用一次；只存引用拷贝）。
    // 半径类字段来自配置键的读取结果（缺键回退在 PlayScene）
    void setAiConfig(const MonsterAi::Config& config) { m_ai.setConfig(config); }

    // 注入回巢回血百分比（monster_return_heal_percent；0 = 关闭——默认）。
    // 逻辑完整（MonsterAi::healedHpAfterReturn），本批默认不生效
    void setReturnHealPercent(float percent) { m_ai.setReturnHealPercent(percent); }

    // 注入世界指针（场景在 onEnter 时对各槽位调用一次；只借用不持有）：
    // 移动碰撞（分轴滑动 + 侧滑）与击退落点校验都经它执行。
    // 传 nullptr 视为「无墙世界」（防御——未注入时位移不限制、不崩溃）
    void setWorld(const World* world) { m_world = world; }

    // 注入玩家碰撞盒（批四·体积碰撞，场景每固定步刷新）：怪物移动把它
    // 当「实心」——撞上滑停、不可重合（由 World::slideMoveEx 的额外
    // 实心判据执行）；兜底分离（nudge）与攻击动作同样消费它
    void setPlayerBlocker(const SDL_FRect& box)
    {
        m_playerBox = box;          // 玩家脚盒快照（本步位置）
        m_hasPlayerBlocker = true;  // 标记已注入（此后移动对玩家盒实心）
    }

    // 清除玩家盒实心（回收/池复用与「玩家目标撤销」时的防御路径）：
    // 未注入 = 移动只对墙体实心（等价旧行为）
    void clearPlayerBlocker() { m_hasPlayerBlocker = false; }

    // 是否在场（存活）：true = 已生成且未死亡；false = 空槽位（池中待复用）。
    // 场景用它门控 update / render / 碰撞判定
    bool isAlive() const { return m_alive; }

    // 当前生命值（受击扣减；场景用于受击日志与头顶血条绘制判定）
    int hp() const { return m_hp; }

    // 当前生命上限（出生时按变体数值表写入；血条比例与回血夹取都读它）
    int maxHp() const { return m_maxHp; }

    // 受击：扣血 + 重置红染计时。返回本次受击是否导致死亡：
    // true = 生命归零、本对象转入「未激活」（调用方负责击杀计数/掉落）；
    // false = 仍存活（调用方通常再施加击退）。
    // 负数伤害属于调用方错误：忽略并警告，不改变任何状态
    bool takeDamage(int amount);

    // 生成（批三正式版）：在「调用方选好的可信落点」上激活本对象——
    // 落点以怪物中心给出（世界坐标），由场景侧按出生规则选择并完成
    // 「激活区块内 + 可走 + 离玩家 >= 500px」校验（落点规则属玩法层）。
    // 出生生命按变体数值表写入；本落点同时登记为「巢」（出生点即巢，
    // 巡逻/栓绳都以它为圆心）。瞬移类操作：落点定稿后同步插值快照（防拉丝）；
    // AI 状态全量复位（池复用语义：旧怪物的仇恨/游荡状态不带进新怪物）
    void spawnAt(float centerX, float centerY);

    // 回收：从场上移除本怪物（重开清场/未来清屏技使用），槽位可被再次
    // spawnAt 复用。清掉追踪目标、红染状态与 AI/侧滑运行时——
    // 复用前的残留数据（追踪目标/状态机/卡死检测窗口）全部归零
    void despawn()
    {
        m_alive = false;     // 未激活：不再更新/渲染/碰撞
        m_hasTarget = false; // 清追踪目标：下次激活等场景重新注入
        m_hitTintLeft = 0.0f; // 清受击红染：新生成的怪物不带受击光效
        m_ai.clear();        // AI 状态回巡逻（巢保留——下次 spawnAt 会重写）
        clearMotionState();  // 清持久化侧滑与卡死检测窗口
        clearAttackState();  // 清攻击状态（批四：出招阶段/冷却/脉冲不跨生命周期）
        clearPlayerBlocker(); // 清玩家盒实心（批四：池复用不带旧玩家位置）
    }

    // 被撞开/被击退：沿「(fromX, fromY) -> 怪物中心」的方向（即远离伤害
    // 来源的方向）瞬移 distance 像素。落点做世界可走校验（被墙挡则按
    // 1/2、1/4 逐级缩短重试；全被挡则原地不动——「贴墙可接受」的放宽
    // 口径下不允许推进墙内，否则怪物会卡在墙里永远出不来）。
    // 距离由调用方给出：接触伤害用较远的弹开（拉开保护距离），
    // 横扫命中用较短的微击退（保持追击压力、方便连扫补刀）。
    // 瞬移类操作：内部同步插值快照（防拉丝）
    void knockback(float fromX, float fromY, float distance);

    // 小幅推动（批四·体积碰撞的兜底与软分离共用）：把怪物沿 (dx, dy)
    // 移动，「墙可走 + 不推入玩家」的候选才采用。候选尝试次序：
    // 完整位移 -> 仅 x -> 仅 y（一侧被挡时保留另一侧的分离量）；
    // 全部被挡返回 false（原地等下一帧，绝不动玩家——玩家优先不动的延伸）。
    // 公开给场景：重叠兜底（把怪物推出玩家）与怪怪软分离（各退一半）调用
    bool nudge(float dx, float dy);

    // ---- 攻击（批四·蓄力前扑）----

    // 命中回弹距离（像素）：攻击命中时怪物沿「玩家 -> 怪物」方向弹开的
    // 距离（约 40，替换旧版接触 150px 弹开）。公开常量：由场景在命中
    // 结算时经 knockback 调用——「回弹是攻击的一部分」，数值与攻击
    // 其余常量（蓄力/前冲）集中在本类维护
    static constexpr float kAttackRecoilDistance = 40.0f;

    // 设置攻击冷却（秒）：PlayScene 在 onEnter 时用配置值注入
    // （monster_attack_cooldown，默认 1.2 秒）——两次出招之间的最小间隔。
    // 负数属于配置错误：忽略并警告，保留原冷却；0 允许（连续出招）
    void setAttackCooldown(float seconds);

    // 注入蓄力警示「!」纹理（批五A；场景加载一份后注入全部槽位——只借用
    // 不持有，纹理生命周期归场景）。传 nullptr = 无警示（加载失败降级：
    // 蓄力表现只剩后倾与警示缺失，游戏照常）；render 在蓄力期间绘制
    void setAlertTexture(SDL_Texture* texture) { m_alertTexture = texture; }

    // 消费本步攻击结算结果（场景在每个 update 之后调用一次）：返回并
    // 立即清除——Hit/Miss 各自只发布一次，绝不重复结算
    MonsterAttackResult consumeAttackResult();

    // 攻击动作是否进行中（蓄力 / 前扑 / 落空收势）：冒烟与单测的查询点
    bool attacking() const { return m_attackPhase != AttackPhase::None; }

    // 覆写更新：AI 状态决策（MonsterAi）+ 帧动画 + 受击红染/残影计时 +
    // 朝目标移动（移动经世界碰撞与绕墙轻量改进）；状态切换/回巢事件的
    // 日志也在此处（含变体名——冒烟断言用）。签名与基类保持一致
    void update(const UpdateContext& ctx) override;

    // 覆写渲染：前扑残影（1~2 重渐隐副本）-> 基础绘制（经相机世界->屏幕
    // 换算）-> 出招染色脉冲（出招瞬间 0.1s）/ 受击红染（批五A：红光叠加，
    // 与玩家同款）-> 蓄力警示「!」（蓄力期间显示）-> 受损后（hp < maxHp）
    // 在头顶绘制屏幕空间小血条（满血隐藏）
    void render(SDL_Renderer* renderer, float alpha, const Camera& camera) const override;

private:
    static constexpr float kSize = 48.0f;           // 怪物边长（正方形），也是单帧宽高
    static constexpr float kHitTintSeconds = 0.18f; // 受击红染时长（秒；规格约 0.18——代码常量，不进配置）
    static constexpr float kHitTintGreenBlue = 60;  // 红染强度：ADD 叠加前的绿/蓝通道（0~255，越低越红）——与玩家红染同款

    // ---- 脚部碰撞盒（批四）：判定的「体积」与 48x48 的视觉解耦 ----
    // 取值（像素）：36x30 的小盒摆在精灵底部——宽居中（偏移 (48-36)/2 = 6）、
    // 底边内收 6px（偏移 48-30-6 = 12）。可调：改这四个常量即改判定体积
    static constexpr float kCollisionBoxW = 36.0f;       // 碰撞盒宽（像素）
    static constexpr float kCollisionBoxH = 30.0f;       // 碰撞盒高（像素）
    static constexpr float kCollisionBoxOffsetX = 6.0f;  // 盒左上角相对精灵左上角的 x 偏移
    static constexpr float kCollisionBoxOffsetY = 12.0f; // 同上 y 偏移（下移到脚部）

    // 「已到达」判定的距离阈值（像素）：小于它就原地不动。
    // 双重作用：避免目标点附近的来回抖动，也排除归一化除以零的退化情形
    static constexpr float kArriveEpsilon = 1.0f;

    // 巡逻速度系数：巡逻/游荡速度 = 变体基础速度 x 本系数（0.5）——
    // 闲逛比追击慢一半，肉眼可辨「它在巡逻还是在追我」；
    // 刻意不叠难度成长（成长只服务追兵压迫感）
    static constexpr float kPatrolSpeedFactor = 0.5f;

    // 游荡点随机取点的重试上限（次）：候选点撞墙/撞世界外时换点重试；
    // 上限内找不到就退化为「回巢心」（巢必可走——出生校验保证），不死循环
    static constexpr int kPatrolTargetAttempts = 8;

    // ---- 绕墙轻量改进（批三）：卡死检测与持久化侧滑 ----

    // 卡死检测阈值（单步，实际位移² / 期望位移² 的比值）：低于它视为
    // 「几乎没动」（正面撞墙时两轴都被挡 ≈ 0；斜向贴墙有一轴在滑动
    // ≈ 0.5，不算卡住），触发一次即时侧滑探测（见 moveWithCollision）
    static constexpr float kStuckMoveRatioSquared = 0.25f;

    // 卡死检测窗口（秒）与窗口判据（实际/期望 位移比）：
    // 窗口内比值低于 kStuckWindowRatio = 持续性卡死（凹角滞留），
    // 重新探测并刷新侧滑方向；不低于 kFreeWindowRatio = 移动自由，
    // 提前结束侧滑（不需要帮忙绕墙）
    static constexpr float kStuckWindowSeconds = 0.5f; // 检测窗口时长（秒）
    static constexpr float kStuckWindowRatio = 0.3f;   // 持续性卡死判据
    static constexpr float kFreeWindowRatio = 0.9f;    // 移动自由判据

    // 持久化侧滑：选定侧滑方向后维持的时长（秒）与权重衰减下限——
    // 维持期内主方向与垂直方向按权重混合（权重从 1 缓慢衰减到下限），
    // 「一段时间内坚持从同一侧绕」比每步独立掷骰子更容易走出凹角
    static constexpr float kSlideHoldSeconds = 1.2f;  // 侧滑方向维持时长（秒）
    static constexpr float kSlideWeightFloor = 0.35f; // 衰减下限（维持期末尾）

    // ---- 头顶血条（批三）：受伤后显示 ----
    static constexpr float kHpBarWidth = 40.0f;  // 血条宽（像素；约 40 与规格一致）
    static constexpr float kHpBarHeight = 5.0f;  // 血条高（像素；规格 5）
    static constexpr float kHpBarGapY = 6.0f;    // 头顶与血条底边的间隙（像素）

    // ---- 蓄力警示「!」（批五A）：素材尺寸与头顶锚位 ----
    static constexpr float kAlertWidth = 24.0f;   // 警示绘制宽（像素；与 alert.png 尺寸一致）
    static constexpr float kAlertHeight = 36.0f;  // 警示绘制高（像素；同上）
    static constexpr float kAlertOffsetY = 24.0f; // 警示底边相对精灵顶部上移量（像素；高于血条区，同屏不重叠）

    // ---- 攻击（批五A 升级·蓄力前扑强化版）：常量与内部状态机 ----
    // 蓄力/前扑/收势/回弹为「代码常量并注释」（规格口径）——回弹距离公开
    // 见上方 kAttackRecoilDistance（场景命中结算消费），其余仅本类内部使用
    static constexpr float kDefaultAttackCooldown = 1.2f;  // 缺省冷却（秒）：monster_attack_cooldown 缺键回退
    static constexpr float kAttackWindupSeconds = 0.35f;   // 蓄力时长（秒）——批五A：0.3 -> 0.35（躲避窗口加长、警示更从容）
    static constexpr float kAttackLeanDistance = 8.0f;     // 蓄力后倾距离（像素）：沿反方向的后退总位移——批五A：4 -> 8（蓄势更深）
    static constexpr float kAttackLungeDistance = 24.0f;   // 前冲距离（像素）——批五A：10 -> 24（大幅前扑；被玩家碰撞盒截停时实际更短）
    static constexpr float kAttackLungeSeconds = 0.12f;    // 前冲时长（秒）——批五A：0.1 -> 0.12；位移走「快出慢收」曲线（初速 2 倍平均）
    static constexpr float kAttackRecoverySeconds = 0.2f;  // 落空收势时长（秒）：Miss 后原地收势（扑空惩罚窗口）再回追击
    static constexpr float kAttackPulseSeconds = 0.1f;     // 出招染色脉冲时长（秒；规格约 0.1）
    static constexpr float kAttackContactMargin = 6.0f;    // 接触范围余量（像素）：碰撞盒触接之外再放宽 6px
    static constexpr float kAttackDirEpsilon = 0.001f;     // 出招方向零向量阈值（长度小于它退回默认方向）

    // ---- 前扑残影（批五A）：复用武器残影思路的半透明副本 ----
    static constexpr int kLungeGhostCount = 2;             // 残影数量（重）：前扑起点 1 重 + 过半 1 重
    static constexpr float kLungeGhostLifeSeconds = 0.18f; // 每重残影的渐隐寿命（秒）：覆盖前扑全程并留一点尾巴
    static constexpr int kLungeGhostStartAlpha = 115;      // 残影起始透明度（0~255；约 0.45，越旧越淡）
    static constexpr float kLungeGhostSample = 0.5f;       // 第 2 重残影的采样进度（前扑归一化进度过半时记录）

    // 攻击阶段（内部状态机）：None 平时 / Windup 蓄力 / Lunge 前扑 /
    // Recovery 落空收势（批五A 新增——Miss 后的原地收势窗口）。
    // 「命中结算」不单列阶段——前扑走完的那一步就地判定 Hit/Miss：
    // Hit 直接回 None；Miss 先进 Recovery 再回 None
    enum class AttackPhase { None, Windup, Lunge, Recovery };

    static constexpr int kFrameCount = 4; // 每个变体精灵表的总帧数（4 帧 x 48px = 192px 宽）

    MonsterType m_type = MonsterType::Goblin; // 当前变体（默认哥布林；刷新时由场景指定）

    bool m_alive = false;     // 是否在场：spawnAt 置 true，受击致死/回收置 false
    int m_hp = 0;             // 当前生命值（spawnAt 时按变体数值表写入）
    int m_maxHp = 0;          // 当前生命上限（同上；血条比例与回血夹取读它）
    float m_hitTintLeft = 0.0f; // 受击红染剩余时长（秒；>0 时 render 叠加红光）

    bool m_hasTarget = false;  // 玩家目标是否已注入（false 时 AI 视为玩家不可见）
    float m_targetX = 0.0f;    // 目标点 x（世界坐标，玩家碰撞盒中心）
    float m_targetY = 0.0f;    // 目标点 y（同上）
    SDL_FRect m_targetBox{};   // 玩家碰撞盒快照（批四：接触范围判定用完整盒）
    float m_chaseSpeed = 0.0f; // 本步移动速度（像素/秒，由场景注入）

    // ---- 攻击运行时（批四）；批五A：前扑进度 + 残影 + 收势 ----
    AttackPhase m_attackPhase = AttackPhase::None; // 当前攻击阶段（平时/蓄力/前扑/落空收势）
    float m_attackCooldown = kDefaultAttackCooldown; // 冷却总时长（秒）：setAttackCooldown 注入
    float m_attackCooldownLeft = 0.0f; // 冷却剩余（秒）：<=0 且接触才可出招
    float m_attackPhaseLeft = 0.0f;    // 当前阶段剩余时长（秒）：蓄力/前扑/收势共用倒计时
    float m_lungeMoved = 0.0f;         // 前扑已分配位移（像素）：按「快出慢收」曲线逐步逼近 24px 预算
    float m_attackDirX = 1.0f;         // 出招锁定方向 x（单位向量；蓄力开始时朝玩家一次性锁定）
    float m_attackDirY = 0.0f;         // 出招锁定方向 y（同上）
    float m_attackPulseLeft = 0.0f;    // 出招染色脉冲剩余时长（秒）：>0 时 render 叠加偏红
    MonsterAttackResult m_attackResult = MonsterAttackResult::None; // 待消费结果（每步至多一条）

    // ---- 前扑残影运行时（批五A）----
    struct LungeGhost
    {
        float x = 0.0f;    // 残影位置 x（记录时的精灵左上角世界坐标）
        float y = 0.0f;    // 残影位置 y（同上）
        float left = 0.0f; // 剩余寿命（秒；>0 时 render 按比例渐隐绘制）
    };
    LungeGhost m_lungeGhosts[kLungeGhostCount]; // 残影槽位（记录顺序 0 -> 1）
    int m_lungeGhostNext = 0; // 下一个待记录的残影槽位（0..kLungeGhostCount；>= 数量 = 已录满）

    SDL_Texture* m_alertTexture = nullptr; // 蓄力警示「!」纹理（场景注入的共享资源；nullptr = 不绘制）

    // AI 状态机（决策核心；参数/巢/回血百分比都封装在内部，见 MonsterAi）
    MonsterAi m_ai;

    // ---- 持久化侧滑运行时（卡死检测窗口 + 选定方向）----
    int m_slideSign = 0;          // 侧滑方向：+1 右侧（顺时针）、-1 左侧、0 无
    float m_slideHoldLeft = 0.0f; // 侧滑维持剩余时长（秒；<=0 表示未激活）
    float m_stuckWindowTime = 0.0f;     // 卡死检测窗口已累计时长（秒）
    float m_stuckWindowExpected = 0.0f; // 窗口内期望位移累计（像素）
    float m_stuckWindowActual = 0.0f;   // 窗口内实际位移累计（像素）

    // 世界指针（碰撞查询；monster 不持有所有权）。nullptr = 无墙世界（防御）
    const World* m_world = nullptr;

    // ---- 玩家盒实心（批四·体积碰撞）：场景每步注入的玩家碰撞盒快照 ----
    SDL_FRect m_playerBox{};          // 玩家脚盒（世界坐标；setPlayerBlocker 写入）
    bool m_hasPlayerBlocker = false;  // 是否已注入（false = 移动只对墙实心）

    // 清侧滑/卡死检测运行时（despawn 与 spawnAt 复用；落点/状态突变后
    // 旧窗口数据不再有意义——清零避免把旧怪的卡死统计带进新怪）
    void clearMotionState()
    {
        m_slideSign = 0;          // 无侧滑方向
        m_slideHoldLeft = 0.0f;   // 无维持计时
        m_stuckWindowTime = 0.0f;     // 窗口清零
        m_stuckWindowExpected = 0.0f; // 期望累计清零
        m_stuckWindowActual = 0.0f;   // 实际累计清零
    }

    // 清攻击运行时（despawn 与 spawnAt 复用）：池槽位复用时旧怪物的
    // 出招阶段/冷却/脉冲/待消费结果/残影绝不带进新怪物——「池复用后
    // 新怪不带旧攻击状态」的判据（冒烟与单测覆盖）
    void clearAttackState()
    {
        m_attackPhase = AttackPhase::None;          // 阶段回平时
        m_attackCooldownLeft = 0.0f;                // 冷却清空：新怪接触即可出招
        m_attackPhaseLeft = 0.0f;                   // 阶段计时清零
        m_lungeMoved = 0.0f;                        // 前扑位移进度清零
        m_attackPulseLeft = 0.0f;                   // 染色脉冲熄灭
        m_attackResult = MonsterAttackResult::None; // 待消费结果丢弃（不跨生命周期发布）
        clearLungeGhosts();                         // 前扑残影清空（批五A：不跨生命周期）
    }

    // 清前扑残影（批五A）：寿命清零（render 不再绘制）并复位记录槽位——
    // despawn/spawnAt/每次出招开始时调用（旧残影绝不出现在新动作里）
    void clearLungeGhosts()
    {
        for (LungeGhost& ghost : m_lungeGhosts)
        {
            ghost.left = 0.0f; // 寿命归零：该槽位不再绘制
        }
        m_lungeGhostNext = 0; // 记录槽位回起点（两重可重新记录）
    }

    // 记录一重前扑残影（批五A）：把当前精灵左上角写入下一个空槽位、
    // 寿命置满（render 按剩余寿命渐隐绘制）；两重录满后本调用为无操作
    void recordLungeGhost()
    {
        if (m_lungeGhostNext >= kLungeGhostCount)
        {
            return; // 已录满（本动作残影数量上限）
        }
        LungeGhost& ghost = m_lungeGhosts[m_lungeGhostNext]; // 本重写入的槽位
        ghost.x = m_x;                    // 记录当前 x（世界坐标左上角）
        ghost.y = m_y;                    // 记录当前 y（同上）
        ghost.left = kLungeGhostLifeSeconds; // 寿命置满（渐隐起算）
        ++m_lungeGhostNext;               // 槽位推进（下一重写下一个）
    }

    // 攻击状态机单步推进（蓄力后倾 -> 前扑前冲 -> 结算）：仅 update 内调用。
    // 位移走 applyAttackMotion（墙 + 玩家盒的滑动），不参与卡死检测/侧滑
    void advanceAttack(float dt);

    // slideMoveEx 回调（静态成员满足 C 函数指针签名；context 传 this）：
    // 候选矩形与玩家盒重叠即不可推进——「怪物移动含玩家盒」的实现
    static bool playerBlockerCallback(void* context, const SDL_FRect& rect);

    // 出招开始：朝玩家一次性锁定方向（零向量退回向右）、启动蓄力计时与
    // 染色脉冲、打出招日志（冒烟观测点「怪物出招（变体名）」）
    void beginWindup();

    // 玩家是否处于接触范围（双方碰撞盒触接 + kAttackContactMargin 余量）：
    // 出招触发与命中结算共用的位置判据（玩家目标未注入时恒 false）
    bool playerInContactRange() const;

    // 攻击动作的短位移出口（蓄力后倾 / 前扑共用）：碰撞盒口径的分轴滑动
    // + 位置落定。与 moveWithCollision 的差异：不做卡死检测/持久化侧滑——
    // 攻击是短促的脚本化动作，不走绕行策略、不污染追击的卡死统计窗口
    void applyAttackMotion(float dx, float dy);

    // 位移推进（含墙体碰撞 + 卡死检测 + 持久化侧滑）：本类移动的唯一出口，
    // 三种状态（巡逻/追击/回巢）共用。行为与参数说明见实现（Monster.cpp）
    void moveWithCollision(float stepDX, float stepDY, float dt);

    // 侧滑权重：维持期内从 1 缓慢衰减到 kSlideWeightFloor（随时间线性）
    float slideWeight() const;

    // 计算左右侧滑的候选结果（从当前位置、以期望位移旋转 ±90° 试走）：
    // 输出候选落点与各自的实际位移量平方。纯查询——不改状态；
    // 即时探测与窗口重探共用（世界未注入时不调用）
    void sideSlideCandidates(float stepDX, float stepDY,
                             SDL_FPoint& rightPoint, float& rightMovedSq,
                             SDL_FPoint& leftPoint, float& leftMovedSq) const;

    // 立即应用较优侧滑（单步几乎没动时的即时探测）：更优侧存在的场合
    // 直接采用其位置并在其方位上开启/刷新持久化侧滑；两侧都不如主结果
    //（深凹角）时保留主结果并把方向设为「默认/既有」的一次尝试
    void applyImmediateSideSlide(float stepDX, float stepDY,
                                 const SDL_FPoint& mainPoint, float mainMovedSq);

    // 窗口级重探（持续性卡死时调用）：根据左右候选重新选定持久化侧滑
    // 方向并刷新维持窗口（不移动位置——本步位置已由主流程落定）
    void refreshSlideSide(float stepDX, float stepDY);
};

#endif // !__MONSTER_H__
