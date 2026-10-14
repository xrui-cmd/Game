#include "game/Player.h"
#include "engine/Input.h" // Input：经 ctx.input 查询移动/攻击动作的接口定义
#include <algorithm>      // std::min / std::max / std::clamp：速度逼近、边界与判定矩形
#include <cmath>          // std::sqrt / std::atan2：面朝方向归一化与角度换算

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

// 设置挥击冷却：配置驱动入口（对应 player_attack_cooldown）
void Player::setAttackCooldown(float seconds)
{
    // 负冷却无意义（会让冷却剩余出现反向拉扯），属于配置错误：忽略并警告；
    // 0 允许（每次按下都能挥击的「无冷却」玩法，显式配置的语义）
    if (seconds < 0.0f)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "玩家挥击冷却不能为负数（收到 %f），忽略该配置", seconds);
        return;
    }

    m_attackCooldown = seconds; // 覆盖冷却总时长：其后起手的挥击使用新值
}

// 覆写更新：动作查询驱动的渐近加减速移动 + 行走动画。
// 速度模型（沿用上一版）：目标速度 = 输入方向 x m_speed；当前速度以 accel
// （有输入）/ friction（无输入）逐轴逼近；位移 = 速度 x 固定步长。
// 本批改造点：方向输入从「键位直读」迁移到「语义动作查询」——移动绑在
// 哪些键上由 config 的 bind_move_* 决定，本函数对改键完全透明
void Player::update(const UpdateContext& ctx)
{
    // 第一行固定动作：快照「本步开始前的位置」作为渲染插值起点。
    // 覆写 update 会跳过 Entity::update，这条调用必须自己补上
    snapshotPreviousPosition();

    // 攻击状态第一步收尾：清除上一步的「起手脉冲」（本步若起手会在末尾重新置位）
    m_attackStarted = false;

    // 冷却与弧光计时递减（夹到 0）：与固定步长同源，暂停时随 update 门控冻结
    if (m_attackCooldownLeft > 0.0f)
    {
        m_attackCooldownLeft = std::max(m_attackCooldownLeft - ctx.deltaTime, 0.0f);
    }
    if (m_swingLeft > 0.0f)
    {
        m_swingLeft = std::max(m_swingLeft - ctx.deltaTime, 0.0f);
    }

    // 1) 读取输入方向（逐轴独立，取值 -1 / 0 / +1）。
    //    每一轴 = 正方向动作按下（+1）与负方向动作按下（-1）的合成，
    //    同时按下相反方向时两值互相抵消（净输入为 0，视为无输入）。
    //    input 为空指针（旧调用方未装配输入系统）时按「无输入」安全降级
    float inputX = 0.0f; // 水平输入累积：正右负左
    float inputY = 0.0f; // 垂直输入累积：正下负上（SDL 的 y 轴向下为正）

    if (ctx.input != nullptr)
    {
        if (ctx.input->held("move_up"))
        {
            inputY -= 1.0f; // 上
        }
        if (ctx.input->held("move_down"))
        {
            inputY += 1.0f; // 下
        }
        if (ctx.input->held("move_left"))
        {
            inputX -= 1.0f; // 左
        }
        if (ctx.input->held("move_right"))
        {
            inputX += 1.0f; // 右
        }
    }

    // 1.5) 面朝方向：最近一次有效移动方向（归一化后存储，斜向保留斜角）。
    //      输入为零（含对向抵消）时不更新——面向保持上一次的方向；
    //      初始向右，保证站在原地也能向右挥击
    if (inputX != 0.0f || inputY != 0.0f)
    {
        // 归一化：把 (1,1) 这类斜向输入换算为单位方向（长度恰为 1）
        const float inputLength = std::sqrt(inputX * inputX + inputY * inputY);
        m_facingX = inputX / inputLength; // 面朝 x 分量（斜向时为 ±0.707 这类值）
        m_facingY = inputY / inputLength; // 面朝 y 分量（同上）
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

    // 6) 攻击：pressed 边缘 + 冷却门控（「按一次挥一次」，且两次起手的
    //    间隔不小于冷却时长）。放在移动之后：本步读取到的面朝方向最新。
    //    起手只置状态；命中结算由场景在 player.update 之后读取 attackStarted()
    //    一次性完成（「每次挥击对范围内每只怪物结算一次」）
    const bool attackPressed = ctx.input != nullptr && ctx.input->pressed("attack");
    if (attackPressed && m_attackCooldownLeft <= 0.0f)
    {
        m_attackCooldownLeft = m_attackCooldown; // 冷却开始走：期间按下不生效
        m_swingLeft = kSwingDisplaySeconds;      // 弧光从满时长开始显示
        m_attackStarted = true;                  // 起手脉冲：场景据此结算本次挥击

        // 冒烟观测点：挥击日志（自动化测试据此断言攻击输入链路生效）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "挥击（面朝方向 x=%.2f y=%.2f）", m_facingX, m_facingY);
    }
}

// 重开归位：委托基类的 setPosition 瞬间覆盖坐标（不经过速度/时间累积），
// 并清零速度与攻击状态——否则重开后玩家会带着上一局的动量/冷却/弧光继续
void Player::reset(float x, float y)
{
    m_velocityX = 0.0f; // 水平速度清零：全新一局从静止开始
    m_velocityY = 0.0f; // 垂直速度清零：同上

    // 攻击状态复位：冷却立即转好、弧光清除、脉冲标记清除——
    // 新一局第一帧就能挥击，且不残留上一局未播完的特效
    m_attackCooldownLeft = 0.0f; // 冷却清零：新一局立即可以挥击
    m_swingLeft = 0.0f;          // 弧光清零：清除未播完的挥击表现
    m_attackStarted = false;     // 脉冲清零：防止上一局的起手标记误触发结算

    setPosition(x, y); // 复用基类实现：写入坐标并同步插值快照（防拉丝）
}

// 面朝角度（度）：把面朝单位向量换算成旋转绘制所需的角度
float Player::facingAngleDegrees() const
{
    // 弧度 -> 度的换算常量（180/π，直接写数值：不引用 M_PI 这类需要宏
    // 定义的常量，避免不同标准库的开关差异）。atan2(y, x) 以 x 轴正方向
    // 为 0 度、顺时针为正——与屏幕坐标系及 SDL 旋转绘制语义一致
    constexpr float kRadiansToDegrees = 57.29577951308232f;
    return std::atan2(m_facingY, m_facingX) * kRadiansToDegrees;
}

// 挥击判定矩形：从玩家中心沿面朝方向伸出 kAttackReach、垂直方向宽 kAttackWidth
SDL_FRect Player::attackRect() const
{
    // 玩家中心：判定矩形的基准点（与渲染/碰撞的左上角坐标换算同式）
    const float centerX = m_x + m_w * 0.5f; // 中心 x = 左上角 + 半宽
    const float centerY = m_y + m_h * 0.5f; // 中心 y = 左上角 + 半高

    // 垂直方向单位向量：面朝方向旋转 90 度（perp = (-fy, fx)）
    const float perpX = -m_facingY; // 垂直分量 x
    const float perpY = m_facingX;  // 垂直分量 y

    // 几何参数：垂直半宽与远端中心（远端 = 中心 + 面朝 x 伸出长度）
    const float halfWidth = kAttackWidth * 0.5f;              // 垂直方向半宽（像素）
    const float farX = centerX + m_facingX * kAttackReach;    // 远端中心 x
    const float farY = centerY + m_facingY * kAttackReach;    // 远端中心 y

    // 长条四角坐标：近端两点（中心 ± 垂直半宽）+ 远端两点（远端 ± 垂直半宽）
    const float near1X = centerX + perpX * halfWidth; // 近端角 1 x
    const float near1Y = centerY + perpY * halfWidth; // 近端角 1 y
    const float near2X = centerX - perpX * halfWidth; // 近端角 2 x
    const float near2Y = centerY - perpY * halfWidth; // 近端角 2 y
    const float far1X = farX + perpX * halfWidth;     // 远端角 1 x
    const float far1Y = farY + perpY * halfWidth;     // 远端角 1 y
    const float far2X = farX - perpX * halfWidth;     // 远端角 2 x
    const float far2Y = farY - perpY * halfWidth;     // 远端角 2 y

    // 包围盒组装：四角的 min/max 构成轴对齐矩形。
    // 正交方向（右/左/上/下）时恰好是 64x56 或 56x64 的规整矩形；
    // 斜向时取包围盒近似（斜向覆盖略大，属于可接受的手感近似）
    SDL_FRect rect{};
    rect.x = std::min(std::min(near1X, near2X), std::min(far1X, far2X)); // 左边界 = 四角最小 x
    rect.y = std::min(std::min(near1Y, near2Y), std::min(far1Y, far2Y)); // 上边界 = 四角最小 y
    rect.w = std::max(std::max(near1X, near2X), std::max(far1X, far2X)) - rect.x; // 宽 = 最大 x - 左边界
    rect.h = std::max(std::max(near1Y, near2Y), std::max(far1Y, far2Y)) - rect.y; // 高 = 最大 y - 上边界
    return rect;
}

// 插值后的玩家中心点（渲染坐标）：挥击弧光的挂点
SDL_FPoint Player::interpolatedCenter(float alpha) const
{
    // 渲染位置 = 上一固定步位置与当前位置按 alpha 线性插值（与基类 render 同式），
    // 再换算到中心点——弧光因此与玩家本体画在同一个平滑位置上
    const float renderX = m_prevX + (m_x - m_prevX) * alpha; // 插值后的绘制 x
    const float renderY = m_prevY + (m_y - m_prevY) * alpha; // 插值后的绘制 y
    return SDL_FPoint{ renderX + m_w * 0.5f, renderY + m_h * 0.5f }; // 中心点 = 渲染位置 + 半尺寸
}
