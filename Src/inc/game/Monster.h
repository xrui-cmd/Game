#ifndef __ENEMY_H__
#define __ENEMY_H__

#include "engine/Entity.h" // Entity：实体基类（空间/纹理/帧动画公共能力）

// ============================================================
// Enemy：追击玩家的敌人实体（追踪移动 + 逼近帧动画）
// ============================================================
//
// 继承自 Entity 获得的能力（与 Player/Coin 共用，不再各自复制）：
//   - 位置/尺寸数据与 rect()、setPosition()、width()/height() 查询
//   - 纹理生命周期：release() 幂等释放，析构函数兜底
//   - 三级回退纹理加载：精灵表 -> 单帧图 -> 程序化纯色（loadTextureWithFallback）
//   - render()：按当前帧从精灵表裁帧绘制（本类无需覆写）
//
// 本类自己的规则：
//   1. 纹理来源（三级回退，逐级降级的细节见基类）：
//      首选 assets/enemy_sheet.png 2 帧逼近精灵表（32x32，持续播动画）；
//      次选 assets/enemy.png 单帧静态精灵（动画退化为静止）；
//      回退为程序化 32x32 红紫色方块（素材缺失时兜底，保证游戏可运行）
//   2. 行为（AI 驱动方式——「场景注入、实体执行」）：
//      敌人不持有玩家指针，也不查询全局状态；PlayScene 每个固定步在
//      update 前调用 setTarget()/setChaseSpeed()，把「本步的追踪目标」
//      （玩家矩形中心）与「本步速度」注入进来，再调用 update：
//        - 有目标：沿「敌人中心 -> 目标点」的归一化方向 x 速度移动，
//          并把左上角 clamp 在视口内（先注入、后更新的顺序让难度曲线
//          之类的玩法规则完全留在场景层，敌人自身只负责执行）
//        - 无目标：仅推进帧动画（防御性分支）
//   3. 生成 / 击退都是「瞬移类操作」：落点定稿后必须快照 prev=cur，
//      否则渲染插值会从旧位置拉丝到新位置（延续 Coin::respawn 的纪律）
//   4. 动画节奏：2 帧循环、默认每帧 0.2 秒（缓慢蠕动感），由基类
//      render 的「累计时间 / 帧时长」换算，与渲染帧率解耦
// ============================================================

class Enemy : public Entity
{
public:
    // 默认帧时长：本阶段敌人动画节奏固定 0.2 秒/帧（代码常量，不进配置文件）
    static constexpr float kDefaultFrameTime = 0.2f; // 默认每帧显示时长（秒）

    // 构造：固定敌人尺寸为 32x32（与精灵表的单帧尺寸一致）
    Enemy()
    {
        m_w = kSize; // 宽 32 像素：逼近精灵表每帧宽 32px
        m_h = kSize; // 高 32 像素：逼近精灵表每帧高 32px
    }

    // 加载敌人纹理：只声明素材路径、帧参数与回退颜色，
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

    // 生成本敌人：窗口内随机位置、距 avoidRect（通常为玩家矩形）中心
    // 至少 kMinSpawnDistance 像素（最多重试 kSpawnMaxTries 次），clamp 窗口内。
    // 瞬移类操作：落点定稿后同步插值快照（防拉丝）
    void spawn(int winW, int winH, const SDL_FRect& avoidRect);

    // 被撞开：沿「(fromX, fromY) -> 敌人中心」的方向（即远离玩家的方向）
    // 瞬移 kKnockbackDistance 像素，并 clamp 在视口内。
    // 瞬移类操作：内部同步插值快照（防拉丝）
    void knockback(float fromX, float fromY, float viewportW, float viewportH);

    // 覆写更新：推进动画 + 朝目标点直线追踪。
    // 签名与基类保持一致：世界上下文（deltaTime/键盘/视口）打包传入
    void update(const UpdateContext& ctx) override;

private:
    static constexpr float kSize = 32.0f;               // 敌人边长（正方形），也是单帧宽高
    static constexpr float kMinSpawnDistance = 300.0f;  // 出生点距玩家中心的最小距离（像素）
    static constexpr int kSpawnMaxTries = 20;           // 「距玩家足够远」的最大重试次数
    static constexpr float kKnockbackDistance = 150.0f; // 被撞开时沿反方向退出的距离（像素）

    // 「已到达」判定的距离阈值（像素）：小于它就原地不动。
    // 双重作用：避免目标点附近的来回抖动，也排除归一化除以零的退化情形
    static constexpr float kArriveEpsilon = 1.0f;

    static constexpr int kFrameCount = 2; // 逼近精灵表总帧数（2 帧 x 32px = 64px 宽）

    bool m_hasTarget = false;  // 本步是否已注入追踪目标（false 时仅播动画）
    float m_targetX = 0.0f;    // 目标点 x（屏幕像素，通常为玩家矩形中心）
    float m_targetY = 0.0f;    // 目标点 y（同上）
    float m_chaseSpeed = 0.0f; // 本步移动速度（像素/秒，由场景注入）
};

#endif // !__ENEMY_H__
