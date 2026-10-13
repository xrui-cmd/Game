#include "game/Player.h"
#include <algorithm> // std::min / std::max / std::clamp：速度逼近与边界限制

// 本编译单元私有的移动小工具（不对外暴露）
namespace
{

// 把 current 以「每步最多 rate x dt 的变化量」向 target 逼近，不产生过冲。
// 用途：把加速与减速统写成同一个收敛过程——有输入时 target 是目标速度、
// 无输入时 target 是 0，只是速率（加速度 accel / 摩擦减速度 friction）不同。
// 关键性质：收敛到目标时精确落在 target 上，速度归零不会有浮点残值，
// 动画归位的「速度是否为 0」判据因此可靠
float approach(float current, float target, float rate, float dt)
{
    // 本步允许的最大速度变化量（离散积分步长 = 速率 x 时间）
    const float maxDelta = rate * dt;

    // current 低于目标：向上逼近；不足一步时直接落到 target（不过冲）
    if (current < target)
    {
        return std::min(current + maxDelta, target);
    }

    // current 不低于目标：向下逼近（含 current == target 的恒等情形）
    return std::max(current - maxDelta, target);
}

} // 匿名命名空间结束

// 加载玩家纹理：三级回退流程由基类统一实现，这里只提供本实体的参数
bool Player::load(SDL_Renderer* renderer)
{
    // loadTextureWithFallback 依次尝试：
    //   1. assets/player_sheet.png 行走精灵表（2 帧，移动时播放）
    //   2. assets/player.png 单帧精灵（动画退化为静止）
    //   3. 程序化 50x50 绿色方块（素材缺失时兜底）
    return loadTextureWithFallback(renderer,
                                   "assets/player_sheet.png",      // 首选：行走精灵表相对路径
                                   "assets/player.png",            // 次选：单帧精灵相对路径
                                   kFrameCount,                    // 精灵表帧数：2 帧
                                   kDefaultFrameTime,              // 每帧时长：默认 0.15 秒
                                   SDL_Color{0, 255, 0, 255});     // 回退颜色：不透明绿色
}

// 设置移动速度：配置驱动的速度入口（对应 player_speed 配置项）
void Player::setSpeed(float speed)
{
    // 负数速度会让 WASD 四方向整体反转，属于明显的配置错误：
    // 记录警告并忽略，保留原速度继续游戏；0 允许（玩家静止，可能是有意为之）
    if (speed < 0.0f)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "玩家速度不能为负数（收到 %f），忽略该配置", speed);
        return;
    }

    m_speed = speed; // 覆盖移动速度：下一次 update 的位移计算立即使用新值
}

// 设置加减速参数：配置驱动的手感入口（对应 player_accel / player_friction）
void Player::setMotion(float accel, float friction)
{
    // 负值没有物理意义（会让速度反向拉扯），属于配置错误：整组忽略并警告，
    // 保留原参数继续游戏；0 允许（accel=0 无法起步、friction=0 冰面滑行，
    // 都是显式配置的物理语义，写配置的人自己负责）
    if (accel < 0.0f || friction < 0.0f)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "玩家加减速不能为负数（收到 accel=%f, friction=%f），忽略该配置",
                    accel, friction);
        return;
    }

    m_accel = accel;       // 覆盖加速度：下一次 update 的逼近立即使用新值
    m_friction = friction; // 覆盖摩擦减速度：无输入时的衰减速率随之更新
}

// 覆写更新：渐近加减速移动 + 行走动画。
// 速度模型（本次改造）：目标速度 = 输入方向 x m_speed；当前速度以 accel
// （有输入）/ friction（无输入）逐轴逼近；位移 = 速度 x 固定步长。
// 稳态速度与改造前一致（300 像素/秒），只是起步/停步多了短促过渡
void Player::update(const UpdateContext& ctx)
{
    // 第一行固定动作：快照「本步开始前的位置」作为渲染插值起点。
    // 覆写 update 会跳过 Entity::update，这条调用必须自己补上
    snapshotPreviousPosition();

    // 1) 读取输入方向（逐轴独立，取值 -1 / 0 / +1）。
    //    同时按下相反方向时两值互相抵消（净输入为 0，视为无输入）
    float inputX = 0.0f; // 水平输入累积：正右负左
    float inputY = 0.0f; // 垂直输入累积：正下负上（SDL 的 y 轴向下为正）

    if (ctx.keyboard[SDL_SCANCODE_W])
    {
        inputY -= 1.0f; // 上
    }
    if (ctx.keyboard[SDL_SCANCODE_S])
    {
        inputY += 1.0f; // 下
    }
    if (ctx.keyboard[SDL_SCANCODE_A])
    {
        inputX -= 1.0f; // 左
    }
    if (ctx.keyboard[SDL_SCANCODE_D])
    {
        inputX += 1.0f; // 右
    }

    // 2) 逐轴渐近更新速度（固定步长下的速度积分，结果与帧率无关）：
    //    - 有输入：目标 = 方向 x 最大速度，以 accel 逼近（起步逐渐变快）
    //    - 无输入：目标 = 0，以 friction 逼近（松键滑行一小段后停住）
    const float dt = ctx.deltaTime; // 本固定步的时长（秒），目前恒为 1/120

    if (inputX != 0.0f)
    {
        // 水平轴加速：当前速度向目标速度靠拢（approach 保证不过冲）
        m_velocityX = approach(m_velocityX, inputX * m_speed, m_accel, dt);
    }
    else
    {
        // 水平轴减速：无输入，速度向 0 收敛，收敛时精确归零
        m_velocityX = approach(m_velocityX, 0.0f, m_friction, dt);
    }

    if (inputY != 0.0f)
    {
        // 垂直轴加速：与水平轴对称，两轴独立互不影响（斜向合速度语义不变）
        m_velocityY = approach(m_velocityY, inputY * m_speed, m_accel, dt);
    }
    else
    {
        // 垂直轴减速：同上，收敛到 0
        m_velocityY = approach(m_velocityY, 0.0f, m_friction, dt);
    }

    // 3) 位置积分：本步位移 = 当前速度 x 固定步长（速度单位：像素/秒）
    m_x += m_velocityX * dt;
    m_y += m_velocityY * dt;

    // 4) 边界限制基于上下文里的视口实际尺寸（与改造前完全一致）：
    //    右边界 = 视口宽 - 自身宽，下边界 = 视口高 - 自身高
    m_x = std::clamp(m_x, 0.0f, ctx.viewportWidth - m_w);
    m_y = std::clamp(m_y, 0.0f, ctx.viewportHeight - m_h);

    // 5) 行走动画推进（固定帧时长与渲染帧率解耦，原理同基类默认实现）：
    //    判据从「有输入」升级为「有速度」——松键后的滑行段动画自然延续；
    //    速度精确衰减到 0 后归位第 1 帧（站姿），不会停在半途的迈步姿势
    if (m_velocityX != 0.0f || m_velocityY != 0.0f)
    {
        m_animTime += dt; // 有速度：累积行走时间，供 render 换算帧号
    }
    else
    {
        m_animTime = 0.0f; // 速度归零：清零计时器 -> 帧号恒为 0（第 1 帧站姿）
    }
}

// 重开归位：委托基类的 setPosition 瞬间覆盖坐标（不经过速度/时间累积），
// 并清零速度——否则重开后玩家会带着上一局的动量继续滑行
void Player::reset(float x, float y)
{
    m_velocityX = 0.0f; // 水平速度清零：全新一局从静止开始
    m_velocityY = 0.0f; // 垂直速度清零：同上
    setPosition(x, y);  // 复用基类实现：写入坐标并同步插值快照（防拉丝）
}
