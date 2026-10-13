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
//   2. 移动（渐近速度模型，替掉此前的「输入直驱位移」）：
//      - 目标速度 = 输入方向 × player_speed（各轴独立，符号 -1/0/+1）
//      - 当前速度以加速度 accel 逼近目标：有输入用 accel、无输入用
//        friction 减速到 0。好处：起步/停步都有短促过渡，手感不再生硬
//      - 位移 = 当前速度 × 固定步长；稳态速度仍等于 player_speed，
//        且边界 clamp 逻辑与原实现保持一致
//   3. 动画：移动才播、静止归位第 1 帧——「移动」的判据是速度非零，
//      因此松键后的滑行段动画自然延续，完全停下才回站姿
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

    // 覆写更新：WASD 渐近加减速移动 + 「移动才播、静止归位第 1 帧」的动画。
    // 签名与基类保持一致：世界上下文（deltaTime/键盘/视口）打包传入
    void update(const UpdateContext& ctx) override;

    // 重开归位：把玩家瞬间移动到 (x, y) 并清零速度（内部委托基类
    // setPosition，同时同步插值快照）；保留这个语义化名字让调用处
    // （PlayScene::restart / onEnter）读起来更直观
    void reset(float x, float y);

private:
    float m_speed = kDefaultSpeed;         // 移动速度（像素/秒）：稳态速度上限，onEnter 时由配置覆盖
    float m_accel = kDefaultAccel;         // 加速度（像素/秒²）：有输入时的速度逼近速率
    float m_friction = kDefaultFriction;   // 摩擦减速度（像素/秒²）：无输入时的速度衰减速率

    // 当前速度（像素/秒），逐轴独立渐近：斜向输入时两轴各取自己的目标，
    // 与改造前「未归一化直驱」的斜向速度语义保持一致（合速度约 1.41 倍）
    float m_velocityX = 0.0f; // 水平速度：正右负左
    float m_velocityY = 0.0f; // 垂直速度：正下负上（SDLy 轴向下为正）

    static constexpr int kFrameCount = 2; // 行走精灵表总帧数（2 帧 x 50px = 100px 宽）
};

#endif // !__PLAYER_H__
