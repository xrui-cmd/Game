#ifndef __MONSTER_H__
#define __MONSTER_H__

#include "engine/Entity.h" // Entity：实体基类（空间/纹理/帧动画公共能力）

// 前置声明：世界（碰撞）与相机（渲染换算）在本类中只以指针/引用出现，
// 完整定义由 Monster.cpp 的包含获得（减少头依赖）
class World;
class Camera;

// ============================================================
// Monster：追击玩家的怪物实体（三变体：史莱姆 / 哥布林 / 骷髅）
// ============================================================
//
// 本批改造（素材换血 + 自动战斗）：单一「敌人」拆为三种外观+数值变体，
// 共用同一套追击 AI——变体只影响「外观纹理」与「生命值/基础速度」两组
// 数值，行为逻辑完全一致。PlayScene 刷新时等权随机选择变体。
//
// 继承自 Entity 获得的能力（与 Player/XpGem 共用，不再各自复制）：
//   - 位置/尺寸数据与 rect()、setPosition()、width()/height() 查询
//   - 纹理生命周期：release() 幂等释放，析构函数兜底
//   - 三级回退纹理加载：精灵表 -> 单帧图 -> 程序化纯色（loadTextureWithFallback）
//
// 本类自己的规则：
//   1. 变体数值表（本批定案：常量写死并注释，不走配置文件）：
//        - 史莱姆：生命 20 / 速度 90  —— 慢吞吞的软柿子（最慢，血量居中）
//        - 哥布林：生命 30 / 速度 110 —— 基准线（与旧单怪时代的数值一致）
//        - 骷髅  ：生命 18 / 速度 140 —— 快而脆的压力型目标
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
//   3. 行为（AI 驱动方式——「场景注入、实体执行」）：
//      怪物不持有玩家指针，也不查询全局状态；PlayScene 每个固定步在
//      update 前调用 setTarget()/setChaseSpeed()，把「本步的追踪目标」
//      （玩家矩形中心）与「本步速度」注入进来，再调用 update：
//        - 有目标：沿「怪物中心 -> 目标点」的归一化方向 x 速度移动；
//          自批二起位移经世界碰撞处理（分轴滑动 + 卡住时轻量侧滑，
//          见 update 内注释）——完整绕墙/寻路留批三评估；
//        - 无目标：仅推进帧动画（防御性分支）
//      行为本质不变（巡逻/仇恨/栓绳留批三），仅碰撞与坐标体系升级
//   4. 战斗反馈（沿用上一版）：受击闪白（约 0.1s，render 叠加加色绘制）
//      + 击退（沿背离伤害来源的方向瞬移，落点做世界可走校验）；死亡转
//      「未激活」（对象池语义：不再更新/渲染/碰撞，槽位等下一次 spawnAt 复用）
//   5. 生成 / 击退 / 回收都是「状态突变类操作」：落点定稿后必须快照
//      prev=cur，否则渲染插值会从旧位置拉丝到新位置（延续既有纪律）
//   6. 动画节奏：4 帧循环、每帧 0.2 秒（代码常量），由基类 render 的
//      「累计时间 / 帧时长」换算，与渲染帧率解耦
//   7. 出生（批二过渡版）：不再从「窗口边缘」刷新——场景在玩家周围
//      550~750px 圆环内选「激活区块内且可走」的落点，经 spawnAt 写入
//      （落点规则属玩法层，场景负责选择；本类只负责可信落点的落位）
// ============================================================

// 怪物变体枚举：等权随机刷新的取值范围（Count 恒为变体数量，用作随机上界）
enum class MonsterType
{
    Slime,  // 史莱姆：生命 20 / 速度 90（慢而拖延）
    Goblin, // 哥布林：生命 30 / 速度 110（基准线）
    Skelet, // 骷髅：生命 18 / 速度 140（快而脆）
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

    // 设置本步的追踪目标点（传目标矩形，内部取中心存储）。
    // PlayScene 每个固定步在 update 前调用——「场景注入、实体执行」的分工：
    // 目标切换、难度曲线等玩法规则全部留在场景层
    void setTarget(const SDL_FRect& targetRect);

    // 清除追踪目标：之后 update 仅播动画不移动（防御性接口，当前场景不用）
    void clearTarget() { m_hasTarget = false; }

    // 设置本步移动速度（像素/秒）：PlayScene 按难度曲线算好后每步注入
    void setChaseSpeed(float speed) { m_chaseSpeed = speed; }

    // 注入世界指针（场景在 onEnter 时对各槽位调用一次；只借用不持有）：
    // 移动碰撞（分轴滑动 + 侧滑）与击退落点校验都经它执行。
    // 传 nullptr 视为「无墙世界」（防御——未注入时位移不限制、不崩溃）
    void setWorld(const World* world) { m_world = world; }

    // 是否在场（存活）：true = 已生成且未死亡；false = 空槽位（池中待复用）。
    // 场景用它门控 update / render / 碰撞判定
    bool isAlive() const { return m_alive; }

    // 当前生命值（受击扣减；场景用于受击日志，HUD 扩展时可继续使用）
    int hp() const { return m_hp; }

    // 受击：扣血 + 重置闪白计时。返回本次受击是否导致死亡：
    // true = 生命归零、本对象转入「未激活」（调用方负责击杀计数/掉落）；
    // false = 仍存活（调用方通常再施加击退）。
    // 负数伤害属于调用方错误：忽略并警告，不改变任何状态
    bool takeDamage(int amount);

    // 生成（批二过渡版）：在「调用方选好的可信落点」上激活本对象——
    // 落点以怪物中心给出（世界坐标），由场景侧按出生规则选择并完成
    // 「激活区块内 + 可走」校验（落点规则属玩法层，见 PlayScene）。
    // 出生生命按变体数值表写入；瞬移类操作：落点定稿后同步插值快照（防拉丝）
    void spawnAt(float centerX, float centerY);

    // 回收：从场上移除本怪物（重开清场/未来清屏技使用），槽位可被再次
    // spawnAt 复用。清掉追踪目标与闪白状态，避免复用前残留旧数据
    void despawn()
    {
        m_alive = false;     // 未激活：不再更新/渲染/碰撞
        m_hasTarget = false; // 清追踪目标：下次激活等场景重新注入
        m_flashLeft = 0.0f;  // 清闪白：新生成的怪物不带受击光效
    }

    // 被撞开/被击退：沿「(fromX, fromY) -> 怪物中心」的方向（即远离伤害
    // 来源的方向）瞬移 distance 像素。落点做世界可走校验（被墙挡则按
    // 1/2、1/4 逐级缩短重试；全被挡则原地不动——「贴墙可接受」的放宽
    // 口径下不允许推进墙内，否则怪物会卡在墙里永远出不来）。
    // 距离由调用方给出：接触伤害用较远的弹开（拉开保护距离），
    // 横扫命中用较短的微击退（保持追击压力、方便连扫补刀）。
    // 瞬移类操作：内部同步插值快照（防拉丝）
    void knockback(float fromX, float fromY, float distance);

    // 覆写更新：推进帧动画 + 受击闪白计时 + 朝目标点追踪（移动经世界碰撞）。
    // 签名与基类保持一致：世界上下文（deltaTime/键盘/视口）打包传入
    void update(const UpdateContext& ctx) override;

    // 覆写渲染：基础绘制（经相机世界->屏幕换算）之外，
    // 受击闪白期间额外叠加一次加色绘制
    void render(SDL_Renderer* renderer, float alpha, const Camera& camera) const override;

private:
    static constexpr float kSize = 48.0f;           // 怪物边长（正方形），也是单帧宽高
    static constexpr float kHitFlashSeconds = 0.1f; // 受击闪白时长（秒；代码常量，不进配置）

    // 「已到达」判定的距离阈值（像素）：小于它就原地不动。
    // 双重作用：避免目标点附近的来回抖动，也排除归一化除以零的退化情形
    static constexpr float kArriveEpsilon = 1.0f;

    // 卡住检测阈值（实际位移² / 期望位移² 的比值）：低于它视为「几乎没动」
    //（正面撞墙时两轴都被挡 ≈ 0；斜向贴墙有一轴在滑动 ≈ 0.5，不算卡住），
    // 触发一次轻量侧滑尝试（见 update）——密度取 0.25 让两类情形的判别
    // 留有充分余量
    static constexpr float kStuckMoveRatioSquared = 0.25f;

    static constexpr int kFrameCount = 4; // 每个变体精灵表的总帧数（4 帧 x 48px = 192px 宽）

    MonsterType m_type = MonsterType::Goblin; // 当前变体（默认哥布林；刷新时由场景指定）

    bool m_alive = false;     // 是否在场：spawnAt 置 true，受击致死/回收置 false
    int m_hp = 0;             // 当前生命值（spawnAt 时按变体数值表写入）
    float m_flashLeft = 0.0f; // 受击闪白剩余时长（秒；>0 时 render 叠加提亮）

    bool m_hasTarget = false;  // 本步是否已注入追踪目标（false 时仅播动画）
    float m_targetX = 0.0f;    // 目标点 x（世界坐标，通常为玩家矩形中心）
    float m_targetY = 0.0f;    // 目标点 y（同上）
    float m_chaseSpeed = 0.0f; // 本步移动速度（像素/秒，由场景注入）

    // 世界指针（碰撞查询；monster 不持有所有权）。nullptr = 无墙世界（防御）
    const World* m_world = nullptr;
};

#endif // !__MONSTER_H__
