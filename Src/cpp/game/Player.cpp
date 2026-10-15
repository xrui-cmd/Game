#include "game/Player.h"
#include "engine/Input.h" // Input：经 ctx.input 查询移动动作的接口定义
#include <SDL3_image/SDL_image.h> // IMG_LoadTexture：加载武器素材（可选增强）
#include <algorithm>      // std::min / std::max / std::clamp：速度逼近与边界
#include <cmath>          // std::sqrt / std::sin / std::atan2：方向归一化与挥击曲线
#include <string>         // std::string：拼接武器素材的绝对路径

// 本编译单元私有的移动小工具（不对外暴露）
namespace
{

// 把 current 以「每步最多 rate x dt 的变化量」向 target 逼近，不产生过冲。
// 用途：把加速与减速统写成同一个收敛过程——有输入时 target 是目标速度、
// 无输入时 target 是 0，只是速率（加速度 accel / 摩擦减速度 friction）不同。
// 关键性质：收敛到目标时精确落在 target 上，速度归零不会有浮点残值，
// 动画与翻转的「是否在移动」判据因此可靠
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

// 析构：先释放本类自持的武器纹理，本体纹理由基类析构兜底释放
Player::~Player()
{
    releaseWeapon(); // 武器纹理（加载失败时为 nullptr，内部天然跳过）
}

// 释放武器纹理：先判空、再销毁、最后置空，保证可重复调用（幂等）
void Player::releaseWeapon()
{
    // 只有纹理确实存在时才销毁，避免对空指针调用 SDL_DestroyTexture
    if (m_weaponTexture != nullptr)
    {
        SDL_DestroyTexture(m_weaponTexture); // 通知 SDL 释放这张纹理占用的显存
        m_weaponTexture = nullptr;           // 置空后再次调用会直接跳过，实现幂等
    }
}

// 加载玩家纹理与武器素材
bool Player::load(SDL_Renderer* renderer)
{
    // 本体纹理：三级回退流程由基类统一实现，这里只提供本实体的参数——
    //   1. assets/player_sheet.png 行走精灵表（5 帧：0~3 跑动 + 4 站立）
    //   2. assets/player.png 单帧精灵（动画退化为静止）
    //   3. 程序化 48x84 绿色方块（素材缺失时兜底）
    const bool bodyLoaded = loadTextureWithFallback(renderer,
                                                    "assets/player_sheet.png",   // 首选：行走精灵表相对路径
                                                    "assets/player.png",         // 次选：单帧精灵相对路径
                                                    kFrameCount,                 // 精灵表帧数：5 帧
                                                    kDefaultFrameTime,           // 每帧时长：默认 0.15 秒
                                                    SDL_Color{0, 255, 0, 255});  // 回退颜色：不透明绿色

    // 武器素材（攻击挥动表现用）：可选增强——失败只警告，攻击结算不受影响。
    // 它不走基类三级回退（没有单帧/程序化替代），是玩家专属的附加资源
    const char* basePath = SDL_GetBasePath(); // 可执行文件所在目录（拼接素材绝对路径的基准）
    if (basePath != nullptr)
    {
        // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
        const std::string weaponPath = std::string(basePath) + "assets/weapon.png";
        // IMG_LoadTexture：读文件 -> 解码 PNG -> 一步创建 GPU 纹理
        m_weaponTexture = IMG_LoadTexture(renderer, weaponPath.c_str());
        if (m_weaponTexture != nullptr)
        {
            // 像素风必需：与基类三级加载同一采样口径——武器素材会旋转绘制，
            // 若按默认线性采样，旋转时边缘会被插值糊化，最近邻保持像素锐利
            SDL_SetTextureScaleMode(m_weaponTexture, SDL_SCALEMODE_NEAREST);
        }
        else
        {
            // 素材缺失/解码失败：记录原因后继续（挥剑画面跳过，命中照常）
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "加载武器素材 %s 失败（%s），挥剑表现将跳过", weaponPath.c_str(), SDL_GetError());
        }
    }

    return bodyLoaded; // 本体加载成功与否是返回值：武器是可选增强不参与判定
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

// 设置自动攻击冷却：配置驱动入口（对应 attack_cooldown）
void Player::setAttackCooldown(float seconds)
{
    // 负冷却无意义（会让冷却剩余出现反向拉扯），属于配置错误：忽略并警告；
    // 0 允许（每个固定步都能出手的「无冷却」玩法，显式配置的语义）
    if (seconds < 0.0f)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "玩家攻击冷却不能为负数（收到 %f），忽略该配置", seconds);
        return;
    }

    m_attackCooldown = seconds; // 覆盖冷却总时长：其后的自动出手使用新值
}

// 覆写更新：渐近加减速移动 + 朝向翻转 + 攻击计时与挥击表现推进 + 动画切换。
// 速度模型：目标速度 = 输入方向 x m_speed；当前速度以 accel（有输入）/
// friction（无输入）逐轴逼近；位移 = 速度 x 固定步长（与帧率解耦）
void Player::update(const UpdateContext& ctx)
{
    // 第一行固定动作：快照「本步开始前的位置」作为渲染插值起点。
    // 覆写 update 会跳过 Entity::update，这条调用必须自己补上
    snapshotPreviousPosition();

    // 攻击计时第一步收尾：冷却与挥击表现剩余时长递减（夹到 0）——
    // 与固定步长同源，暂停时随 update 门控冻结
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

    // 2) 朝向翻转（本批新增）：水平输入决定左右镜像——向左翻转、向右原样；
    //    静止（水平输入为 0）时保持上一次朝向。setFlipped 是纯渲染层标记，
    //    碰撞/插值/攻击索敌都按未翻转的数据计算，不影响任何玩法判定
    if (inputX > 0.0f)
    {
        setFlipped(false); // 向右：原始朝向（素材默认面向）
    }
    else if (inputX < 0.0f)
    {
        setFlipped(true); // 向左：水平镜像绘制
    }

    // 3) 逐轴渐近更新速度（固定步长下的速度积分，结果与帧率无关）：
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

    // 4) 位置积分：本步位移 = 当前速度 x 固定步长（速度单位：像素/秒）
    m_x += m_velocityX * dt;
    m_y += m_velocityY * dt;

    // 边界限制基于上下文里的视口实际尺寸：右边界 = 视口宽 - 自身宽，
    // 下边界 = 视口高 - 自身高（与既有移动边界规则一致）
    m_x = std::clamp(m_x, 0.0f, ctx.viewportWidth - m_w);
    m_y = std::clamp(m_y, 0.0f, ctx.viewportHeight - m_h);

    // 5) 行走动画推进与子区间切换（固定帧时长与渲染帧率解耦）：
    //    判据是「有没有速度」——移动时循环跑动帧 [kRunFirst, kRunLast]；
    //    速度精确衰减到 0 后定格站立帧（kIdleFrame, kIdleFrame）。
    //    同一张精灵表承载两段动画：跑动 4 帧 + 站立 1 帧（帧序见组装脚本）
    if (m_velocityX != 0.0f || m_velocityY != 0.0f)
    {
        m_animTime += dt; // 有速度：累积行走时间，供 render 换算跑动帧号
        setAnimationRange(kRunFirst, kRunLast); // 动画区间切到跑动子区间（循环播放）
    }
    else
    {
        m_animTime = 0.0f; // 速度归零：清零计时器（下次移动从跑动首帧重新起跑）
        setAnimationRange(kIdleFrame, kIdleFrame); // 动画区间切到站立帧（区间长度 1 = 定格）
    }
}

// 覆写渲染：本体（含翻转/插值/帧区间）之后叠加挥剑表现
void Player::render(SDL_Renderer* renderer, float alpha) const
{
    // 基础绘制（含未加载纹理时的安全跳过）：按插值位置画当前动画帧
    Entity::render(renderer, alpha);

    // 只有「挥击表现期内」且武器纹理已加载时才画剑；其余时刻直接结束
    if (m_swingLeft <= 0.0f || m_weaponTexture == nullptr)
    {
        return;
    }

    // 挥击进度 t：0（出手瞬间）-> 1（表现结束）。
    // 位移曲线用 sin(pi x t)：t=0 时 0（收回状态）、t=0.5 时最大（前探峰值）、
    // t=1 时回到 0（收回）——一条曲线同时表达「前探 + 收回」的短促动作
    const float t = 1.0f - m_swingLeft / kSwingSeconds; // 归一化进度 [0,1]
    const float along = kWeaponAlong + kWeaponExtend * std::sin(3.1415926535897932f * t); // 本帧前移量

    // 挂点：玩家插值中心（与本体同一平滑位置，快速移动时武器不脱节）
    const SDL_FPoint center = interpolatedCenter(alpha);

    // 武器中心 = 玩家中心 + 挥击方向 x 前移量（从身体向外探出）
    const float weaponX = center.x + m_swingDirX * along; // 武器中心 x
    const float weaponY = center.y + m_swingDirY * along; // 武器中心 y

    // 目标矩形：把武器中心作为矩形中心摆放（宽高 = 素材绘制尺寸常量）
    const SDL_FRect dst{ weaponX - kWeaponWidth * 0.5f,  // 左上角 x = 中心 - 半宽
                         weaponY - kWeaponHeight * 0.5f, // 左上角 y = 中心 - 半高
                         kWeaponWidth,                    // 绘制宽（像素）
                         kWeaponHeight };                 // 绘制高（像素）

    // 旋转角度 = 挥击方向角 + 90 度 + 轻微扫动：
    //   - 方向角：atan2(y, x) 换算成度（屏幕坐标系、顺时针为正）；
    //   - +90 度：素材剑尖朝上（基准姿态），旋转 90 度后剑尖指向方向角；
    //   - 轻微扫动：+kSwingSway 度线性扫到 -kSwingSway 度（t: 0 -> 1），
    //     让前探动作带一点点挥抡的味道
    constexpr float kRadiansToDegrees = 57.29577951308232f; // 弧度 -> 度（180/π 数值）
    const double angle = std::atan2(m_swingDirY, m_swingDirX) * kRadiansToDegrees + 90.0
                         + kSwingSwayDegrees * (1.0 - 2.0 * t); // 角度（度）

    // 旋转绘制：center 传 nullptr = 绕目标矩形自身中心旋转（武器中心即挂点）
    SDL_RenderTextureRotated(renderer, m_weaponTexture, nullptr, &dst,
                             angle, nullptr, SDL_FLIP_NONE);
}

// 重开归位：委托基类的 setPosition 瞬间覆盖坐标（不经过速度/时间累积），
// 并清零速度与攻击状态——否则重开后玩家会带着上一局的动量/冷却/挥剑继续
void Player::reset(float x, float y)
{
    m_velocityX = 0.0f; // 水平速度清零：全新一局从静止开始
    m_velocityY = 0.0f; // 垂直速度清零：同上

    // 攻击状态复位：冷却立即转好、挥剑清除——新一局第一帧就能出手，
    // 且不残留上一局未播完的挥击表现
    m_attackCooldownLeft = 0.0f; // 冷却清零：新一局立即可以出手
    m_swingLeft = 0.0f;          // 挥击表现清零：清除未播完的挥剑画面

    // 动画区间先切回站立帧：重开后玩家静止，第一帧渲染就应显示站姿
    //（后续 update 会按实际速度重新切换区间）
    setAnimationRange(kIdleFrame, kIdleFrame);

    setPosition(x, y); // 复用基类实现：写入坐标并同步插值快照（防拉丝）
}

// 尝试对目标自动出手：冷却门控 + 挥击表现启动（命中结算在场景层）
bool Player::tryAutoAttack(float targetCenterX, float targetCenterY)
{
    // 冷却未到：本次不出手（不重置任何状态，冷却继续走）
    if (m_attackCooldownLeft > 0.0f)
    {
        return false;
    }

    // 玩家当前中心：挥击方向的起点（与索敌用的中心算法一致）
    const float centerX = m_x + m_w * 0.5f; // 中心 x = 左上角 + 半宽
    const float centerY = m_y + m_h * 0.5f; // 中心 y = 左上角 + 半高

    // 指向目标的位移向量与长度
    const float dx = targetCenterX - centerX; // 水平分量（正右负左）
    const float dy = targetCenterY - centerY; // 垂直分量（正下负上）
    const float length = std::sqrt(dx * dx + dy * dy); // 两点距离（像素）

    // 归一化为单位方向；距离过近（退化情形）退回默认向右——
    // 避免除以接近 0 的长度产生 NaN 方向，污染旋转角度
    if (length > kDirectionEpsilon)
    {
        m_swingDirX = dx / length; // 单位方向 x：dx 占全长的比例
        m_swingDirY = dy / length; // 单位方向 y：dy 占全长的比例
    }
    else
    {
        m_swingDirX = 1.0f; // 默认向右：与初始朝向一致
        m_swingDirY = 0.0f; // 默认水平方向
    }

    // 出手：重置冷却门 + 启动挥击表现计时（render 据此绘制挥剑）
    m_attackCooldownLeft = m_attackCooldown; // 冷却开始走：届时的后续出手被它门控
    m_swingLeft = kSwingSeconds;             // 挥击表现从满时长开始（前探->收回）

    return true; // 通知场景：本次出手成功，可以对目标结算命中
}

// 插值后的玩家中心点（渲染坐标）：挥剑表现挂点
SDL_FPoint Player::interpolatedCenter(float alpha) const
{
    // 渲染位置 = 上一固定步位置与当前位置按 alpha 线性插值（与基类 render 同式），
    // 再换算到中心点——武器因此与玩家本体画在同一个平滑位置上
    const float renderX = m_prevX + (m_x - m_prevX) * alpha; // 插值后的绘制 x
    const float renderY = m_prevY + (m_y - m_prevY) * alpha; // 插值后的绘制 y
    return SDL_FPoint{ renderX + m_w * 0.5f, renderY + m_h * 0.5f }; // 中心点 = 渲染位置 + 半尺寸
}
