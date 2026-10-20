#ifndef __MONSTER_H__
#define __MONSTER_H__

#include "engine/Entity.h"     // Entity：实体基类（空间/纹理/帧动画公共能力）
#include "game/MonsterAi.h"    // MonsterAi：AI 状态决策（纯逻辑，批三巡逻/仇恨/栓绳）
#include "game/AttackModule.h" // AttackModule：六型进攻模组数据表（批六丁·蓄力/距离/元素）
#include "game/Boss.h"         // Boss：双区 Boss 身份/属性/专属模组/奖励纯逻辑（M3·乙）

// 前置声明：世界（碰撞）与相机（渲染换算）在本类中只以指针/引用出现，
// 完整定义由 Monster.cpp 的包含获得（减少头依赖）
class World;
class Camera;
enum class ZoneId; // 分区 id（批六丁生成分表：草地原色 / 雪山冰霜版）

// ============================================================
// Monster：怪物实体（六变体：草地原色三型 + 雪山冰霜三型）
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
//        - 冰史莱姆/霜哥布林/霜骷髅：数值沿用同族原色版，差异只在冰霜
//          外观与命中元素（批六丁；见 AttackModule.h）
//      速度的实际值 = 变体基础速度 + 难度成长 x 局内秒数，封顶 260
//      （成长与封顶都由 PlayScene 注入，本类只提供 baseSpeed()）
//   2. 纹理：每个变体一套「精灵表 + 单帧回退」，4 帧 / 每帧 48x48
//      （16x16 像素 x 3 倍放大）：
//        monster_slime_sheet.png   史莱姆（原始帧：swampy 软泥怪动画）
//        monster_goblin_sheet.png  哥布林（原始帧：goblin run）
//        monster_skelet_sheet.png  骷髅（原始帧：skelet run）
//        monster_ice_slime_sheet.png / monster_frost_goblin_sheet.png /
//        monster_frost_skelet_sheet.png  雪山三型（同源帧冰霜重染色）
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
//   7. 出生（批三正式版；批六乙配平）：PlayScene 在「玩家周围 520~900px
//      环带内、激活区块、walkable 且避水」选好落点后经 spawnAt 写入——
//      出生点即「巢」，怪物从此绕它巡逻（落点规则属玩法层，场景负责选择）
//   8. 头顶血条（批三新增）：受损后（hp < maxHp）在头顶绘制小血条
//      （40x5、深色底 + 红色按比例填充；屏幕空间绘制经相机换算）；
//      满血隐藏——「看一眼血量、不看面板」的轻量读血方式
//   9. 碰撞盒（批四·碰撞体积与视觉解耦）：构造时设置「脚部小盒」
//      36x30、底部内收 6px（setCollisionBox）——移动滑动、击退落点、
//      游荡点校验、玩家互碰全部改用碰撞盒；渲染/血条照旧用完整精灵
//      （48x48）。俯视角下「身体可叠压墙脚、脚盒不穿透」
//  10. 攻击（批六丁·数据表驱动，替换单一蓄力前扑）：玩家进入接触范围
//      （双方碰撞盒触接 + 小余量）且冷却就绪时出招；六型参数全部来自
//      AttackModule 表（蓄力时长/位移/时长/命中半径/伤害倍率/元素）：
//        - 史莱姆/冰史莱姆：深蹲蓄力 0.45s -> 弧线跃出 80px/0.3s ->
//          落点 40px 范围结算（冰史莱姆落地冰环 + 命中减速）；
//        - 哥布林/霜哥布林：既有前扑（0.35s 后倾 -> 24px/0.12s；霜哥布林
//          冰爪视觉 + 冻结条 +35）；
//        - 骷髅/霜骷髅：后倾 0.3s -> 突刺 140px/0.25s，撞墙/命中即止
//          （霜骷髅冰痕视觉 + 冻结条 +30）；
//      玩家是否真扣血、元素是否累积由场景裁决（无敌期整段跳过）；
//      结果经 consumeAttackResult 交场景消费（扣血/无敌/回弹/音效/
//      命中爪痕特效都是场景职责）；出招后进入冷却（monster_attack_cooldown
//      注入）。出招瞬间的红色脉冲（0.1s）保留——素材复用不加新图
//  11. 等级（批六丙）：出生时由场景按「出生点所在区 + 距本区神龛距离」
//      算出并传入 spawnAt（公式与抖动都在 game/MonsterLevel.h，纯逻辑）；
//      本类存下等级并按它缩放出生生命上限、击杀金币数；攻击伤害与宝石
//      经验由场景用同一份纯函数按 level() 计算。等级不改变移速曲线。
// ============================================================

// 怪物攻击结算结果（批四）：Monster 每步至多发布一次，场景消费一次
enum class MonsterAttackResult
{
    None, // 本步无结算（未构成一次完整出招）
    Miss, // 攻击结束、玩家不在命中范围（蓄力落空/落点未覆盖/突刺落空）
    Hit   // 攻击命中（是否真扣血/元素是否累积由场景按无敌状态裁决）
};

// 怪物变体枚举：生成分表按区取三型（草地原色 / 雪山冰霜版），区内等权。
// 顺序 = 进攻模组表 kAttackModules 的下标（见 game/AttackModule.h），不可交换。
enum class MonsterType
{
    Slime,       // 草地史莱姆：生命 20 / 速度 90 / 金币 1 / 跳跃砸击
    Goblin,      // 草地哥布林：生命 30 / 速度 110 / 金币 2 / 蓄力前扑
    Skelet,      // 草地骷髅：生命 18 / 速度 140 / 金币 1 / 突刺冲锋
    IceSlime,    // 雪山冰史莱姆：同史莱姆数值 / 跳跃砸击 + 落地冰环 + 减速
    FrostGoblin, // 雪山霜哥布林：同哥布林数值 / 前扑 + 冰爪 + 冻结条 +35
    FrostSkelet, // 雪山霜骷髅：同骷髅数值 / 突刺 + 冰痕 + 冻结条 +30
    Count        // 变体总数（哨兵值）：SDL_rand(Count) 的合法上界
};

// 变体名（日志展示用，如「怪物出现（第 1 只，霜骷髅 Lv.24，雪山…）」）
const char* monsterTypeName(MonsterType type);

// 是否为雪山冰霜变体（外观/命中元素共用判据）
inline bool monsterTypeIsFrost(MonsterType type)
{
    return type == MonsterType::IceSlime || // 冰史莱姆
           type == MonsterType::FrostGoblin || // 霜哥布林
           type == MonsterType::FrostSkelet; // 霜骷髅
}

// 按变体取进攻模组（数据表唯一入口；下标即 MonsterType 枚举值）
inline const AttackModule& attackModuleFor(MonsterType type)
{
    return attackModuleForIndex(static_cast<int>(type)); // 越界由表访问器夹取
}

// 编译期约束：变体数必须与进攻模组表行数一致（加怪忘加模组时直接编译失败）
static_assert(static_cast<int>(MonsterType::Count) == kAttackModuleCount,
              "MonsterType::Count 必须等于 kAttackModuleCount（六型各一行模组）");

// 生成分表（批六丁）：zone = 出生点所属区，roll = [0,3) 等权随机数。
// 草地 -> 史莱姆/哥布林/骷髅；雪山 -> 冰史莱姆/霜哥布林/霜骷髅。
// roll 越界（调用方错误/未来扩表）夹到 0..2——绝不越界、结果可预测。
MonsterType monsterVariantForZone(ZoneId zone, int roll);

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

    // 当前变体类型（日志/查询用；Boss 返回其外观复用的同族变体）
    MonsterType type() const { return m_type; }

    // ---- Boss（M3·乙）----

    // 按 Boss 身份加载外观并应用 Boss 属性：外观复用同区基础变体
    //（哥布林王 = 哥布林 / 霜爵 = 霜哥布林）——先 loadVariant（会清 Boss 态）、
    // 再 setBoss（放大体型/碰撞盒）。返回纹理是否加载成功（失败仍可玩）
    bool loadBoss(SDL_Renderer* renderer, BossType type);

    // 设置 Boss 身份与体型：视觉 72x72、脚盒 54x45（全部 x1.5）。
    // 只应在空槽位/加载后调用；普通怪物 spawn 路径会经 loadVariant 清回非 Boss
    void setBoss(BossType type);

    // 当前是否 Boss（场景用于：顶部血条、奖励、免疫、回收豁免、伤害倍率）
    bool isBoss() const { return m_isBoss; }

    // Boss 身份（非 Boss 时返回 GoblinKing 哨兵值；调用方先判 isBoss）
    BossType bossType() const { return m_bossType; }

    // 展示名：Boss = 哥布林王/霜爵；普通怪 = 变体名（所有日志的统一入口）
    const char* name() const;

    // 当前使用的 Boss 招式下标（0 近身 / 1 远距；-1 = 尚未出招）。
    // 场景在命中/落空结算后可据此选特效；普通怪恒为 -1
    int activeBossAttackIndex() const { return m_activeBossAttack; }

    // 当前是否处于「巡逻」AI 状态（批六补丁·回收判据）：true = 绕巢游荡
    // 或停顿（不含与玩家仇恨追击、也不含栓绳回巢）。场景的远处休眠怪
    // 回收只对巡逻状态的怪物生效——追击/回巢的怪物代表玩家正在拉怪，
    // 回收会让目标凭空消失（规则与三条件见 game/MonsterRecycle.h）
    bool isPatrolling() const
    {
        return !m_isBoss && m_ai.state() == MonsterAiState::Patrol; // Boss 永不巡逻（M3·乙）
    }

    // 基础追击速度（像素/秒）：普通怪 = 变体基础速度（场景再叠难度成长）；
    // Boss = 变体基础 x 0.9（99——略降且不叠成长，保持「可跑开」红线）
    float baseSpeed() const;

    // 击杀金币掉落数（枚）：变体基础（史莱姆 1 / 哥布林 2 / 骷髅 1）
    // + 等级加成 floor((lv-1)/10)（批六丙：1~10 级 +0、11~20 级 +1、
    // 21~30 级 +2）。场景在击杀结算时按它掉落
    int coinDrop() const;

    // 当前等级（批六丙：出生时按出生位置所在区与距神龛距离定，存于本对象）。
    // 场景用于：攻击伤害缩放、经验宝石价值、血条旁「Lv.n」显示与日志断言
    int level() const { return m_level; }

    // 当前进攻模组：普通怪 = 变体固定模组；Boss = 本次已选择的专属招式
    //（出招瞬间快照，结算/渲染/元素读取同一份）。Boss 尚未出招时退回其
    // 外观变体模组（仅用于防御性渲染查询，不会触发普通怪出招）
    const AttackModule& attackModule() const
    {
        if (m_isBoss && m_activeBossAttack >= 0) // Boss 已选招：用专属模组
        {
            return bossAttackDef(m_bossType, m_activeBossAttack).module; // 表驱动
        }
        return attackModuleFor(m_type); // 普通怪 / Boss 未选招：变体模组
    }

    // 注入法术冰环纹理（场景复用 spell_frost_nova.png）：冰史莱姆落地时
    // 绘制一圈冰环视觉。只借用不持有（纹理生命周期归场景）；nullptr = 不画
    void setFrostNovaTexture(SDL_Texture* texture) { m_frostNovaTexture = texture; }

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

    // ---- 减速乘区（批五B：冰霜新星）----

    // 施加减速：factor 为速度乘区（如 0.5 = 半速）、seconds 为持续时长。
    // 连续减速（同一法术反复命中）：以最后一次施放为准——乘区与时长都被
    // 覆盖刷新（与「连续受击全部状态重新计时」同款口径）。
    // 防御：seconds <= 0 忽略（不产生状态）；factor 夹到 [0,1]
    //（0 = 完全冻结的显式语义；>1 属于「加速」误用，夹回 1）
    void applySlow(float factor, float seconds);

    // 当前速度乘区（>0 的系数；无减速恒 1.0）：移动速度计算消费它，
    // 单测/冒烟用它断言「减速是否生效」而不必观测位移
    float speedFactor() const { return (m_slowLeft > 0.0f) ? m_slowFactor : 1.0f; }

    // 减速剩余时长（秒；冒烟/单测查询用）
    float slowLeft() const { return m_slowLeft; }

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

    // 生成（批三正式版；批六丙加等级）：在「调用方选好的可信落点」上激活
    // 本对象——落点以怪物中心给出（世界坐标），由场景侧按出生规则选择并完成
    // 「激活区块内 + 可走 + 环带距离」校验（落点规则属玩法层）。
    // level 由场景按「出生点所在区 + 距本区神龛的距离 + jitter」计算后传入
    // （公式见 game/MonsterLevel.h）；本类存下它并按等级缩放出生生命上限。
    // 缺省 1 保持既有调用（测试/防御）可直接 spawnAt(x, y)。
    // 本落点同时登记为「巢」（出生点即巢，巡逻/栓绳都以它为圆心）。
    // 瞬移类操作：落点定稿后同步插值快照（防拉丝）；
    // AI 状态全量复位（池复用语义：旧怪物的仇恨/游荡状态不带进新怪物）
    void spawnAt(float centerX, float centerY, int level = 1);

    // 回收：从场上移除本怪物（重开清场/未来清屏技使用），槽位可被再次
    // spawnAt 复用。清掉追踪目标、红染状态与 AI/侧滑运行时——
    // 复用前的残留数据（追踪目标/状态机/卡死检测窗口）全部归零
    void despawn()
    {
        m_alive = false;     // 未激活：不再更新/渲染/碰撞
        m_hasTarget = false; // 清追踪目标：下次激活等场景重新注入
        m_hitTintLeft = 0.0f; // 清受击红染：新生成的怪物不带受击光效
        clearSlowState();    // 清减速乘区（批五B：池复用不带旧减速）
        m_ai.clear();        // AI 状态回巡逻（巢保留——下次 spawnAt 会重写）
        clearMotionState();  // 清持久化侧滑与卡死检测窗口
        clearAttackState();  // 清攻击状态（批四：出招阶段/冷却/脉冲不跨生命周期）
        clearBossState();    // 清 Boss 身份/体型/冷却（M3·乙：池复用绝不留 Boss 态）
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

    // ---- 头顶血条（批三）：受伤后显示 ----
    // public：批六丙的「Lv.n」小字与血条同锚点绘制（场景层用同一份几何，
    // 避免两处各写一遍 40/5/6 后悄悄错位）
    static constexpr float kHpBarWidth = 40.0f;  // 血条宽（像素；约 40 与规格一致）
    static constexpr float kHpBarHeight = 5.0f;  // 血条高（像素；规格 5）
    static constexpr float kHpBarGapY = 6.0f;    // 头顶与血条底边的间隙（像素）

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

    // 渲染插值位置（世界坐标左上角）：与本体/血条/警示使用同一条
    // 「prev -> cur 按 alpha 线性插值」公式。批六丙供场景绘制血条旁
    // 「Lv.n」小字时定位——文字与血条贴合同一个插值锚点，快速移动不脱节
    SDL_FPoint interpolatedTopLeft(float alpha) const
    {
        return SDL_FPoint{ m_prevX + (m_x - m_prevX) * alpha, // 插值左上角 x
                           m_prevY + (m_y - m_prevY) * alpha }; // 插值左上角 y
    }

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

    // ---- 蓄力警示「!」（批五A）：素材尺寸与头顶锚位 ----
    static constexpr float kAlertWidth = 24.0f;   // 警示绘制宽（像素；与 alert.png 尺寸一致）
    static constexpr float kAlertHeight = 36.0f;  // 警示绘制高（像素；同上）
    static constexpr float kAlertOffsetY = 24.0f; // 警示底边相对精灵顶部上移量（像素；高于血条区，同屏不重叠）

    // ---- 攻击（批六丁·数据表驱动）：常量与内部状态机 ----
    // 蓄力/位移/时长/命中半径/伤害倍率/元素全部来自 AttackModule 表；本类只保留
    // 「所有模组共用」的表现常量。回弹距离公开见上方 kAttackRecoilDistance。
    static constexpr float kDefaultAttackCooldown = 1.2f;  // 缺省冷却（秒）：monster_attack_cooldown 缺键回退
    static constexpr float kAttackLeanDistance = 8.0f;     // 蓄力后倾距离（像素）：前扑/突刺沿反方向的后退总位移
    static constexpr float kAttackRecoverySeconds = 0.2f;  // 收势时长（秒）：落空/落地/撞墙后的原地恢复窗口
    static constexpr float kAttackPulseSeconds = 0.1f;     // 出招染色脉冲时长（秒；规格约 0.1）
    static constexpr float kAttackContactMargin = 6.0f;    // 接触范围余量（像素）：碰撞盒触接之外再放宽 6px
    static constexpr float kAttackDirEpsilon = 0.001f;     // 出招方向零向量阈值（长度小于它退回默认方向）
    static constexpr float kJumpArcHeight = 34.0f;         // 跳跃弧线视觉高度（像素；纵向偏移，不影响判定）
    static constexpr float kJumpCrouchScaleY = 0.78f;      // 深蹲压扁比例（视觉：高度压到 78%）
    static constexpr float kJumpCrouchScaleX = 1.10f;      // 深蹲压扁比例（视觉：宽度撑到 110%）
    static constexpr float kLandingFxSeconds = 0.35f;      // 落地冰环特效寿命（秒；展开 + 淡出）
    static constexpr float kLandingFxStartScale = 0.55f;   // 冰环起始缩放（相对落点直径）
    static constexpr float kLandingFxEndScale = 1.15f;     // 冰环结束缩放（略大于判定半径，观感饱满）
    static constexpr float kThrustWallStopRatio = 0.35f;   // 突刺撞墙判据：实际位移 < 期望 x 本比例即视为被挡
    static constexpr float kFrozenGhostGreenBlue = 220;    // 冰霜残影色（R=160/G=B=220：青蓝调残影）

    // ---- 前扑/突刺残影（批五A 起；批六丁扩展给突刺）：半透明副本 ----
    static constexpr int kLungeGhostCount = 2;             // 残影数量（重）：出招起点 1 重 + 过半 1 重
    static constexpr float kLungeGhostLifeSeconds = 0.18f; // 每重残影的渐隐寿命（秒）：覆盖位移全程并留一点尾巴
    static constexpr int kLungeGhostStartAlpha = 115;      // 残影起始透明度（0~255；约 0.45，越旧越淡）
    static constexpr float kLungeGhostSample = 0.5f;       // 第 2 重残影的采样进度（位移进度过半时记录）

    // 攻击阶段（内部状态机）：None 平时 / Windup 蓄力 / Strike 攻击位移 /
    // Recovery 收势。三种攻击方式共用同一阶段机，差异全部来自 AttackModule：
    //   - 前扑：位移结束就地判定 Hit/Miss（命中直接回 None，落空进 Recovery）；
    //   - 跳跃：落地时判定 40px 范围，固定进 Recovery（落地收拾姿态）；
    //   - 突刺：位移中命中/撞墙即止，进 Recovery（余下时长用于收势）。
    enum class AttackPhase { None, Windup, Strike, Recovery };

    static constexpr int kFrameCount = 4; // 每个变体精灵表的总帧数（4 帧 x 48px = 192px 宽）

    MonsterType m_type = MonsterType::Goblin; // 当前变体（默认哥布林；刷新时由场景指定）

    bool m_alive = false;     // 是否在场：spawnAt 置 true，受击致死/回收置 false
    int m_level = 1;          // 怪物等级（批六丙：spawnAt 时由场景按出生位置写入）
    int m_hp = 0;             // 当前生命值（spawnAt 时按变体数值表 x 等级缩放写入）
    int m_maxHp = 0;          // 当前生命上限（同上；血条比例与回血夹取读它）
    float m_hitTintLeft = 0.0f; // 受击红染剩余时长（秒；>0 时 render 叠加红光）

    // ---- 减速乘区运行时（批五B：冰霜新星）----
    float m_slowFactor = 1.0f; // 速度乘区（applySlow 写入；无减速 = 1）
    float m_slowLeft = 0.0f;   // 减速剩余时长（秒；>0 时乘区生效）

    bool m_hasTarget = false;  // 玩家目标是否已注入（false 时 AI 视为玩家不可见）
    float m_targetX = 0.0f;    // 目标点 x（世界坐标，玩家碰撞盒中心）
    float m_targetY = 0.0f;    // 目标点 y（同上）
    SDL_FRect m_targetBox{};   // 玩家碰撞盒快照（批四：接触范围判定用完整盒）
    float m_chaseSpeed = 0.0f; // 本步移动速度（像素/秒，由场景注入）

    // ---- 攻击运行时（批四；批六丁：模组数据 + 三种位移方式）----
    AttackPhase m_attackPhase = AttackPhase::None; // 当前攻击阶段（平时/蓄力/攻击位移/收势）
    AttackKind m_attackKind = AttackKind::Lunge;   // 本次出招的攻击方式（蓄力瞬间从模组快照）
    float m_attackCooldown = kDefaultAttackCooldown; // 冷却总时长（秒）：setAttackCooldown 注入
    float m_attackCooldownLeft = 0.0f; // 冷却剩余（秒）：<=0 且接触才可出招
    float m_attackPhaseLeft = 0.0f;    // 当前阶段剩余时长（秒）：蓄力/位移/收势共用倒计时
    float m_strikeMoved = 0.0f;        // 攻击位移已分配量（像素）：按曲线逼近模组 distance 预算
    float m_attackDirX = 1.0f;         // 出招锁定方向 x（单位向量；蓄力开始时朝玩家一次性锁定）
    float m_attackDirY = 0.0f;         // 出招锁定方向 y（同上）
    float m_attackPulseLeft = 0.0f;    // 出招染色脉冲剩余时长（秒）：>0 时 render 叠加偏红
    float m_landingFxLeft = 0.0f;      // 落地冰环剩余时长（秒；冰史莱姆落地时置满）
    MonsterAttackResult m_attackResult = MonsterAttackResult::None; // 待消费结果（每步至多一条）

    // ---- Boss 运行时（M3·乙）----
    bool m_isBoss = false;                          // 是否 Boss（体型/奖励/免疫/回收豁免的总开关）
    BossType m_bossType = BossType::GoblinKing;     // Boss 身份（非 Boss 时为默认哨兵）
    int m_activeBossAttack = -1;                    // 当前招式下标（0 近身 / 1 远距；-1 未出招）
    float m_bossCooldownLeft[kBossAttackCount] = {}; // 两招独立冷却剩余（秒；Boss 分支递减）

    // ---- 前扑/突刺残影运行时（批五A）----
    struct LungeGhost
    {
        float x = 0.0f;    // 残影位置 x（记录时的精灵左上角世界坐标）
        float y = 0.0f;    // 残影位置 y（同上）
        float left = 0.0f; // 剩余寿命（秒；>0 时 render 按比例渐隐绘制）
    };
    LungeGhost m_lungeGhosts[kLungeGhostCount]; // 残影槽位（记录顺序 0 -> 1）
    int m_lungeGhostNext = 0; // 下一个待记录的残影槽位（0..kLungeGhostCount；>= 数量 = 已录满）

    SDL_Texture* m_alertTexture = nullptr; // 蓄力警示「!」纹理（场景注入的共享资源；nullptr = 不绘制）
    SDL_Texture* m_frostNovaTexture = nullptr; // 落地冰环贴图（场景复用 spell_frost_nova.png；nullptr = 不绘制）

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

    // 清减速乘区（despawn 与 spawnAt 复用；批五B）：乘区回 1、时长归零——
    // 池槽位复用时旧怪物的减速绝不带进新怪物
    void clearSlowState()
    {
        m_slowFactor = 1.0f; // 乘区回 1（无减速）
        m_slowLeft = 0.0f;   // 时长归零
    }

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
        m_attackKind = AttackKind::Lunge;           // 攻击方式回默认（新怪出招时重新快照）
        m_attackCooldownLeft = 0.0f;                // 冷却清空：新怪接触即可出招
        m_attackPhaseLeft = 0.0f;                   // 阶段计时清零
        m_strikeMoved = 0.0f;                       // 攻击位移进度清零
        m_attackPulseLeft = 0.0f;                   // 染色脉冲熄灭
        m_landingFxLeft = 0.0f;                     // 落地冰环熄灭（不跨生命周期）
        m_attackResult = MonsterAttackResult::None; // 待消费结果丢弃（不跨生命周期发布）
        m_activeBossAttack = -1;                    // Boss 当前招式清空（池复用不带旧招）
        m_bossCooldownLeft[0] = 0.0f;               // 近身招冷却清零（新 Boss 可立即出招）
        m_bossCooldownLeft[1] = 0.0f;               // 远距招冷却清零（同上）
        clearLungeGhosts();                         // 前扑/突刺残影清空（不跨生命周期）
    }

    // 清 Boss 身份与体型（loadVariant / despawn 复用）：普通怪物绝不留
    // Boss 的 72x72 体型、免疫标记或专属模组；纹理尺寸回 48x48 基准
    void clearBossState()
    {
        m_isBoss = false;                       // 摘掉 Boss 标记
        m_bossType = BossType::GoblinKing;      // 身份回默认哨兵
        m_activeBossAttack = -1;                // 当前招式清空
        m_bossCooldownLeft[0] = 0.0f;           // 近身招冷却清零
        m_bossCooldownLeft[1] = 0.0f;           // 远距招冷却清零
        m_w = kSize;                            // 视觉宽回 48（普通怪基准）
        m_h = kSize;                            // 视觉高回 48
        setCollisionBox(kCollisionBoxW, kCollisionBoxH, // 脚盒回 36x30
                        kCollisionBoxOffsetX, kCollisionBoxOffsetY); // 偏移回 (6,12)
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

    // 攻击状态机单步推进（蓄力后倾 -> 前扑/冲锋/砸地/冰环 -> 结算）：仅
    // update 内调用。位移走 applyAttackMotion（墙 + 玩家盒的滑动），
    // 不参与卡死检测/侧滑
    void advanceAttack(float dt);

    // Boss 双招冷却推进（仅 Boss 分支调用）：两招各自独立倒计时
    void updateBossCooldowns(float dt);

    // Boss 出招：记录招式下标 -> 复用 beginWindup（方向锁定/脉冲/日志）
    void beginBossWindup(int attackIndex);

    // 当前目标中心与自身中心的距离（像素；无目标返回极大值——不触发攻击）
    float distanceToTarget() const;

    // 出招结算后的冷却落账：普通怪写单一冷却；Boss 写本招独立冷却
    void applyCooldownAfterAttack();

    // slideMoveEx 回调（静态成员满足 C 函数指针签名；context 传 this）：
    // 候选矩形与玩家盒重叠即不可推进——「怪物移动含玩家盒」的实现
    static bool playerBlockerCallback(void* context, const SDL_FRect& rect);

    // 出招开始：朝玩家一次性锁定方向（零向量退回向右）、启动蓄力计时与
    // 染色脉冲、打出招日志（冒烟观测点「怪物出招（变体名）」）
    void beginWindup();

    // 玩家是否处于接触范围（双方碰撞盒触接 + kAttackContactMargin 余量）：
    // 出招触发与命中结算共用的位置判据（玩家目标未注入时恒 false）
    bool playerInContactRange() const;

    // 玩家是否处于「落点圆形范围」：跳跃砸击的 40px 范围判定。
    // 以怪物中心为圆心、radius 为半径，与玩家碰撞盒最近点比较（含边界）；
    // 玩家目标未注入（防御路径）恒 false
    bool playerInHitRadius(float radius) const;

    // 攻击位移的短位移出口（蓄力后倾 / 前扑 / 跳跃 / 突刺共用）：
    // 碰撞盒口径的分轴滑动 + 位置落定。与 moveWithCollision 的差异：
    // 不做卡死检测/持久化侧滑——攻击是短促的脚本化动作，不走绕行策略、
    // 不污染追击的卡死统计窗口
    void applyAttackMotion(float dx, float dy);

    // 按「当前动作的视觉姿态」绘制本体（批六丁）：普通状态原样绘制；
    // 跳跃蓄力 = 深蹲压扁（底边锚定）、跳跃位移 = 纵向弧线偏移。
    // offsetY 为世界坐标的纵向偏移（负值向上），scaleX/scaleY 为绘制缩放。
    // 攻击脉冲/受击红染复用同一入口，保证偏移与压扁在叠加绘制时一致
    void drawBody(SDL_Renderer* renderer, float alpha, const Camera& camera,
                  float offsetY, float scaleX, float scaleY) const;

    // 当前跳跃弧线纵向偏移（像素；非跳跃位移阶段恒 0）：
    // 用 sin(pi x 进度) 做「起跳 -> 最高点 -> 落地」的视觉弧线
    float jumpVisualOffsetY() const;

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
