#ifndef __MONSTER_H__
#define __MONSTER_H__

#include "engine/Entity.h" // Entity：实体基类（空间/纹理/帧动画公共能力）

// ============================================================
// Monster：追击玩家的怪物实体（追踪移动 + 帧动画 + 战斗数值）
// ============================================================
//
// 名称说明：本类由既有 Enemy 演进更名而来（文件/类同步改名）——「怪物」
// 比「敌人」更贴近 Roguelike「打怪升级」的语义；职责在原有「追踪移动」
// 之上扩展为完整战斗实体（生命值/受击/死亡）。纹理素材沿用 enemy_* 名
// （素材改名留待资源管理器批次，避免本批牵动素材管线）。
//
// 继承自 Entity 获得的能力（与 Player/XpGem 共用，不再各自复制）：
//   - 位置/尺寸数据与 rect()、setPosition()、width()/height() 查询
//   - 纹理生命周期：release() 幂等释放，析构函数兜底
//   - 三级回退纹理加载：精灵表 -> 单帧图 -> 程序化纯色（loadTextureWithFallback）
//
// 本类自己的规则：
//   1. 纹理来源（三级回退，逐级降级的细节见基类）：
//      首选 assets/enemy_sheet.png 2 帧逼近精灵表（32x32，持续播动画）；
//      次选 assets/enemy.png 单帧静态精灵（动画退化为静止）；
//      回退为程序化 32x32 红紫色方块（素材缺失时兜底，保证游戏可运行）
//   2. 行为（AI 驱动方式——「场景注入、实体执行」）：
//      怪物不持有玩家指针，也不查询全局状态；PlayScene 每个固定步在
//      update 前调用 setTarget()/setChaseSpeed()，把「本步的追踪目标」
//      （玩家矩形中心）与「本步速度」注入进来，再调用 update：
//        - 有目标：沿「怪物中心 -> 目标点」的归一化方向 x 速度移动，
//          并把左上角 clamp 在视口内（先注入、后更新的顺序让难度曲线
//          之类的玩法规则完全留在场景层）
//        - 无目标：仅推进帧动画（防御性分支）
//   3. 战斗数值（本批新增）：
//      - 生命值 m_hp：spawnEdge 时由场景注入（monster_hp 配置），受击扣减；
//      - 受击反馈：闪白 m_flashLeft（约 0.1s，render 叠加一次加色绘制实现）
//        与击退（knockback，沿背离伤害来源的方向瞬移）；
//      - 死亡：takeDamage 返回 true 并把对象标记为「未激活」——不再更新/
//        渲染/参与碰撞，槽位等下一次 spawnEdge 复用（对象池语义）
//   4. 生成 / 击退 / 回收都是「状态突变类操作」：落点定稿后必须快照
//      prev=cur，否则渲染插值会从旧位置拉丝到新位置（延续既有纪律）
//   5. 动画节奏：2 帧循环、默认每帧 0.2 秒（缓慢蠕动感），由基类
//      render 的「累计时间 / 帧时长」换算，与渲染帧率解耦
// ============================================================

class Monster : public Entity
{
public:
    // 默认帧时长：本阶段怪物动画节奏固定 0.2 秒/帧（代码常量，不进配置文件）
    static constexpr float kDefaultFrameTime = 0.2f; // 默认每帧显示时长（秒）

    // 构造：固定怪物尺寸为 32x32（与精灵表的单帧尺寸一致）
    Monster()
    {
        m_w = kSize; // 宽 32 像素：逼近精灵表每帧宽 32px
        m_h = kSize; // 高 32 像素：逼近精灵表每帧高 32px
    }

    // 加载怪物纹理：只声明素材路径、帧参数与回退颜色，
    // 三级回退（精灵表 -> 单帧图 -> 程序化纯色）流程全部收敛在基类
    bool load(SDL_Renderer* renderer);

    // 设置本步的追踪目标点（传目标矩形，内部取中心存储）。
    // PlayScene 每个固定步在 update 前调用——「场景注入、实体执行」的分工：
    // 目标切换、难度曲线等玩法规则全部留在场景层
    void setTarget(const SDL_FRect& targetRect);

    // 清除追踪目标：之后 update 仅播动画不移动（防御性接口，当前场景不用）
    void clearTarget() { m_hasTarget = false; }

    // 设置本步移动速度（像素/秒）：PlayScene 按难度曲线算好后每步注入
    void setChaseSpeed(float speed) { m_chaseSpeed = speed; }

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

    // 生成（边缘刷新）：在窗口四条边的随机一边内侧落位（怪物从屏幕边缘
    // 走入战场），激活本对象并写入出生生命值 hp。
    // avoidRect（通常为玩家矩形）近旁会重试避让，避免「贴脸刷新」；
    // 瞬移类操作：落点定稿后同步插值快照（防拉丝）
    void spawnEdge(int winW, int winH, const SDL_FRect& avoidRect, int hp);

    // 回收：从场上移除本怪物（重开清场/未来清屏技使用），槽位可被再次
    // spawnEdge 复用。清掉追踪目标与闪白状态，避免复用前残留旧数据
    void despawn()
    {
        m_alive = false;     // 未激活：不再更新/渲染/碰撞
        m_hasTarget = false; // 清追踪目标：下次激活等场景重新注入
        m_flashLeft = 0.0f;  // 清闪白：新生成的怪物不带受击光效
    }

    // 被撞开/被击退：沿「(fromX, fromY) -> 怪物中心」的方向（即远离伤害
    // 来源的方向）瞬移 distance 像素，并 clamp 在视口内。
    // 距离由调用方给出：接触伤害用较远的弹开（拉开保护距离），
    // 挥击命中用较短的击退（保持追击压力、方便补刀）。
    // 瞬移类操作：内部同步插值快照（防拉丝）
    void knockback(float fromX, float fromY, float viewportW, float viewportH, float distance);

    // 覆写更新：推进帧动画 + 受击闪白计时 + 朝目标点直线追踪。
    // 签名与基类保持一致：世界上下文（deltaTime/键盘/视口）打包传入
    void update(const UpdateContext& ctx) override;

    // 覆写渲染：基础绘制之外，受击闪白期间额外叠加一次加色绘制
    void render(SDL_Renderer* renderer, float alpha) const override;

private:
    static constexpr float kSize = 32.0f;              // 怪物边长（正方形），也是单帧宽高
    static constexpr float kMinSpawnDistance = 150.0f; // 出生点距玩家中心的最小距离（像素）
    static constexpr int kSpawnMaxTries = 20;          // 「距玩家足够远」的最大重试次数
    static constexpr float kEdgeInset = 4.0f;          // 边缘刷新点从窗口边向内收的像素（完整可见）
    static constexpr float kHitFlashSeconds = 0.1f;    // 受击闪白时长（秒；代码常量，不进配置）

    // 「已到达」判定的距离阈值（像素）：小于它就原地不动。
    // 双重作用：避免目标点附近的来回抖动，也排除归一化除以零的退化情形
    static constexpr float kArriveEpsilon = 1.0f;

    static constexpr int kFrameCount = 2; // 逼近精灵表总帧数（2 帧 x 32px = 64px 宽）

    bool m_alive = false;     // 是否在场：spawnEdge 置 true，受击致死/回收置 false
    int m_hp = 0;             // 当前生命值（spawnEdge 时由场景按配置注入）
    float m_flashLeft = 0.0f; // 受击闪白剩余时长（秒；>0 时 render 叠加提亮）

    bool m_hasTarget = false;  // 本步是否已注入追踪目标（false 时仅播动画）
    float m_targetX = 0.0f;    // 目标点 x（屏幕像素，通常为玩家矩形中心）
    float m_targetY = 0.0f;    // 目标点 y（同上）
    float m_chaseSpeed = 0.0f; // 本步移动速度（像素/秒，由场景注入）
};

#endif // !__MONSTER_H__
