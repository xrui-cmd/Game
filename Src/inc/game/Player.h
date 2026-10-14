#ifndef __PLAYER_H__
#define __PLAYER_H__

#include "engine/Entity.h" // Entity：实体基类（空间/纹理/帧动画公共能力）

// ============================================================
// Player：玩家实体（WASD 移动 + 行走帧动画）
// ============================================================
//
// 继承自 Entity 获得的能力（与 Coin 共用，不再各自复制）：
//   - 位置/尺寸数据与 rect()、setPosition()、width()/height() 查询
//   - 纹理生命周期：release() 幂等释放，析构函数兜底
//   - 三级回退纹理加载：精灵表 -> 单帧图 -> 程序化纯色（loadTextureWithFallback）
//   - render()：按当前帧从精灵表裁帧绘制（本类无需覆写）
//
// 本类自己的规则：
//   1. 纹理来源（三级回退，逐级降级的细节见基类）：
//      首选 assets/player_sheet.png 2 帧行走精灵表（100x50），移动时播放；
//      次选 assets/player.png 单帧静态精灵（旧版素材，动画退化为静止）；
//      回退为程序化 50x50 绿色方块（素材缺失时兜底，保证游戏可运行）
//   2. 移动（渐近速度模型 + 动作查询，替掉此前的「输入直驱位移」）：
//      - 方向输入来自引擎输入系统的语义动作（move_up/down/left/right，
//        经 config 的 bind_* 绑定到物理按键；改键对玩法透明），用 held 查询
//      - 目标速度 = 输入方向 × player_speed（各轴独立，符号 -1/0/+1）
//      - 当前速度以加速度 accel 逼近目标：有输入用 accel、无输入用
//        friction 减速到 0。好处：起步/停步都有短促过渡，手感不再生硬
//      - 位移 = 当前速度 × 固定步长；稳态速度仍等于 player_speed，
//        且边界 clamp 逻辑与原实现保持一致
//   3. 动画：移动才播、静止归位第 1 帧——「移动」的判据是速度非零，
//      因此松键后的滑行段动画自然延续，完全停下才回站姿
//   4. 攻击（本批新增）：按攻击动作（attack，bind_attack，默认 J）朝
//      「最近移动方向」近战挥击（面朝方向，初始向右）：
//      - 冷却 m_attackCooldown（默认 0.4s，配置 player_attack_cooldown 可调），
//        冷却期间按下不生效；
//      - 起手脉冲 m_attackStarted：起手所在固定步为 true，供场景做命中结算
//        （「每次挥击对范围内每只怪物结算一次」——判定是一次性的，不是持续伤害）；
//      - 挥击弧光显示 m_swingLeft（约 0.1s），由场景按面朝角度旋转绘制；
//      - 判定矩形 attackRect()：从玩家中心沿面朝方向伸出的长条（约 64x56）
// ============================================================

class Player : public Entity
{
public:
    // 配置项缺省值：与改造前的硬编码常量逐一相同。
    // PlayScene 读取 player_speed / player_anim_interval / player_accel /
    // player_friction 时以它们作为回退
    static constexpr float kDefaultSpeed = 300.0f;     // 默认移动速度（像素/秒）
    static constexpr float kDefaultFrameTime = 0.15f;  // 默认行走动画每帧时长（秒）
    static constexpr float kDefaultAccel = 1800.0f;    // 默认加速度（像素/秒²）：0->300 用时约 0.17 秒
    static constexpr float kDefaultFriction = 2400.0f; // 默认摩擦减速度（像素/秒²）：300->0 用时约 0.13 秒
    static constexpr float kDefaultAttackCooldown = 0.4f; // 默认挥击冷却（秒）：两次挥击的最小间隔

    // 构造：固定玩家方块尺寸为 50x50（与精灵表单帧尺寸一致）
    Player()
    {
        m_w = 50.0f; // 宽 50 像素：行走精灵表每帧宽 50px
        m_h = 50.0f; // 高 50 像素：行走精灵表每帧高 50px
    }

    // 加载玩家纹理：只声明素材路径、帧参数与回退颜色，
    // 三级回退（精灵表 -> 单帧图 -> 程序化纯色）流程全部收敛在基类
    bool load(SDL_Renderer* renderer);

    // 设置移动速度（像素/秒）：PlayScene 在 onEnter 时用配置值注入。
    // 负数会让 WASD 方向整体反转、属于配置错误，忽略并警告；0 允许（静止）
    void setSpeed(float speed);

    // 设置加减速参数（像素/秒²）：PlayScene 在 onEnter 时用配置值注入。
    // accel   ：有输入时当前速度向目标速度靠拢的加速度；
    // friction：无输入时当前速度衰减到 0 的减速度。
    // 任一值为负属于配置错误：忽略整组并警告（加减速是一对参数，
    // 不作部分应用）；0 是合法值（无加速/无摩擦，显式配置的物理语义）
    void setMotion(float accel, float friction);

    // 设置挥击冷却（秒）：PlayScene 在 onEnter 时用配置值注入
    // （player_attack_cooldown，默认 0.4 秒）。负数属于配置错误：忽略并
    // 警告，保留原冷却；0 允许（每次按下都能挥击的「无冷却」玩法）
    void setAttackCooldown(float seconds);

    // 覆写更新：动作查询驱动的渐近加减速移动 + 攻击输入 + 「移动才播、
    // 静止归位第 1 帧」的动画。方向与攻击都来自 ctx.input 的动作查询
    //（held 移动 / pressed 攻击）；签名与基类保持一致：世界上下文打包传入
    void update(const UpdateContext& ctx) override;

    // 重开归位：把玩家瞬间移动到 (x, y) 并清零速度与攻击状态
    //（内部委托基类 setPosition，同时同步插值快照）；保留这个语义化名字
    // 让调用处（PlayScene::restart / onEnter）读起来更直观
    void reset(float x, float y);

    // ---- 战斗查询接口（PlayScene 结算挥击命中/绘制挥击特效时使用）----

    // 本固定步是否「刚起手」挥击（单步脉冲）：场景在 player.update 之后
    // 查询一次，为 true 则结算本次挥击的命中（范围伤害 + 击退）
    bool attackStarted() const { return m_attackStarted; }

    // 挥击弧光剩余显示时长（秒）：>0 时场景绘制旋转弧光（约 0.1 秒）
    float swingLeft() const { return m_swingLeft; }

    // 面朝单位向量（最近一次有效移动方向；初始向右）
    float facingX() const { return m_facingX; } // 面朝 x 分量（-1~1）
    float facingY() const { return m_facingY; } // 面朝 y 分量（-1~1，屏幕 y 向下为正）

    // 面朝角度（度）：atan2 换算，供 SDL_RenderTextureRotated 旋转绘制使用
    //（SDL 的旋转角以度为单位、顺时针为正，与屏幕坐标系一致）
    float facingAngleDegrees() const;

    // 挥击判定矩形：从玩家中心沿面朝方向伸出的长条（沿面朝约 64 像素、
    // 垂直约 56 像素；正交方向为规整矩形，斜向取包围盒近似）。
    // 用逻辑位置计算——命中判定在固定步内完成，与渲染插值无关
    SDL_FRect attackRect() const;

    // 插值后的玩家中心点（渲染坐标）：挥击弧光挂点用——与实体绘制使用
    // 同一条「prev -> cur 按 alpha 线性插值」公式，特效跟随玩家平滑移动
    SDL_FPoint interpolatedCenter(float alpha) const;

private:
    float m_speed = kDefaultSpeed;         // 移动速度（像素/秒）：稳态速度上限，onEnter 时由配置覆盖
    float m_accel = kDefaultAccel;         // 加速度（像素/秒²）：有输入时的速度逼近速率
    float m_friction = kDefaultFriction;   // 摩擦减速度（像素/秒²）：无输入时的速度衰减速率

    // 当前速度（像素/秒），逐轴独立渐近：斜向输入时两轴各取自己的目标，
    // 与改造前「未归一化直驱」的斜向速度语义保持一致（合速度约 1.41 倍）
    float m_velocityX = 0.0f; // 水平速度：正右负左
    float m_velocityY = 0.0f; // 垂直速度：正下负上（SDLy 轴向下为正）

    // ---- 攻击状态（本批新增）----

    // 挥击判定的几何常量（「约 64x56 的矩形」——正交方向精确、斜向取包围盒）
    static constexpr float kAttackReach = 64.0f;        // 判定沿面朝方向伸出的长度（像素）
    static constexpr float kAttackWidth = 56.0f;        // 判定垂直于面朝方向的宽度（像素）
    static constexpr float kSwingDisplaySeconds = 0.1f; // 挥击弧光显示时长（秒；代码常量）

    // 面朝单位向量：最近一次有效移动方向（归一化存储，斜向保留斜角）；
    // 初始向右 —— 玩家不动就能向右挥击（第一条怪通常从右侧追来）
    float m_facingX = 1.0f; // 面朝方向 x 分量（初始 1 = 向右）
    float m_facingY = 0.0f; // 面朝方向 y 分量（初始 0 = 水平）

    float m_attackCooldown = kDefaultAttackCooldown; // 挥击冷却总时长（秒）：onEnter 时由配置覆盖
    float m_attackCooldownLeft = 0.0f; // 冷却剩余（秒）：>0 时攻击按下不生效
    float m_swingLeft = 0.0f;          // 弧光剩余显示时长（秒）：>0 时场景绘制旋转弧光
    bool m_attackStarted = false;      // 本步起手脉冲：update 开头清零、起手时置位

    static constexpr int kFrameCount = 2; // 行走精灵表总帧数（2 帧 x 50px = 100px 宽）
};

#endif // !__PLAYER_H__
