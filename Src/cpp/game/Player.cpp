#include "game/Player.h"
#include "game/AttackArc.h" // bladeAngleAt：刀锋角共享曲线（判定与渲染同款）；缓出注释见该文件
#include "game/World.h"     // World：移动的分轴碰撞滑动（slideMove/slideMoveEx）与可走查询
#include "engine/Camera.h"  // Camera：渲染时「世界坐标 -> 屏幕坐标」的换算
#include "engine/Input.h"   // Input：经 ctx.input 查询移动动作的接口定义
#include "engine/Rect.h"    // rectsOverlap：额外实心盒（怪物盒）的重叠判定
#include <SDL3_image/SDL_image.h> // IMG_LoadTexture：加载武器素材（可选增强）
#include <algorithm>      // std::min / std::max / std::clamp：速度逼近与边界
#include <cmath>          // std::sqrt / std::exp / std::cos / std::sin / std::atan2：方向归一化、击退衰减与横扫曲线
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

// slideMoveEx 回调：把「额外实心盒」判定接给世界（批四·体积碰撞）。
// context = 玩家自身（静态成员满足 C 函数指针签名，私有状态经 this 访问）
bool Player::solidBlockedCallback(void* context, const SDL_FRect& rect)
{
    const Player& self = *static_cast<const Player*>(context); // 还原玩家自身

    // 未注入/空数组（防御路径）：没有实心体，任何候选都不被挡
    if (self.m_solidRects == nullptr || self.m_solidCount <= 0)
    {
        return false;
    }

    // 逐个实心盒做 AABB 相交：任一相交即挡（盒数 <= 12，线性扫描足够）。
    // 判据与引擎 rectsOverlap 同款：边界恰好贴合不算重叠——贴着怪物
    // 停下是合法站位（「撞上滑停」的落点语义）
    for (int i = 0; i < self.m_solidCount; ++i)
    {
        if (rectsOverlap(rect, self.m_solidRects[i]))
        {
            return true; // 与某个实心盒重叠：该候选不可走
        }
    }
    return false; // 全部不相交：候选可走（额外实心体不挡）
}

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
            // 素材缺失/解码失败：记录原因后继续（横扫画面跳过，命中照常）
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "加载武器素材 %s 失败（%s），横扫表现将跳过", weaponPath.c_str(), SDL_GetError());
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

// 设置攻击冷却：配置驱动入口（对应 attack_cooldown）
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

    m_attackCooldown = seconds; // 覆盖冷却总时长：其后的出手（tryAttack）使用新值
}

// 设置扫程时长：配置驱动入口（对应 attack_swing_time）
void Player::setSwingTime(float seconds)
{
    // 非正时长（含 0）属于配置错误：0 会让扫程进度归一化（swingProgress01）
    // 失去分母、负数则让表现窗口永不成立。忽略并警告，保留原时长继续游戏
    if (!(seconds > 0.0f))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "玩家扫程时长必须为正数（收到 %f），忽略该配置", seconds);
        return;
    }

    m_swingSeconds = seconds; // 覆盖扫程总时长：其后的出手与进度换算使用新值
}

// 设置扫角：配置驱动入口（对应 attack_arc，与场景扇形判定的张角同值）
void Player::setAttackArc(float degrees)
{
    // 负扫角会让刀锋反向扫（起点在终点之后），没有玩法语义：忽略并警告；
    // 0 允许（刀锋不扫，判定退化为中轴线上的点——显式配置的语义）
    if (degrees < 0.0f)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "玩家扫角不能为负数（收到 %f），忽略该配置", degrees);
        return;
    }

    m_sweepArcDegrees = degrees; // 覆盖扫角：其后的渲染与刀锋角计算使用新值
}

// 覆写更新：渐近加减速移动 + 朝向跟随鼠标 + 攻击计时与横扫表现推进 + 动画切换。
// 速度模型：目标速度 = 输入方向 x m_speed；当前速度以 accel（有输入）/
// friction（无输入）逐轴逼近；位移 = 速度 x 固定步长（与帧率解耦）
void Player::update(const UpdateContext& ctx)
{
    // 第一行固定动作：快照「本步开始前的位置」作为渲染插值起点。
    // 覆写 update 会跳过 Entity::update，这条调用必须自己补上
    snapshotPreviousPosition();

    // 攻击计时第一步收尾：冷却与横扫表现剩余时长递减（夹到 0）——
    // 与固定步长同源，暂停时随 update 门控冻结
    if (m_attackCooldownLeft > 0.0f)
    {
        m_attackCooldownLeft = std::max(m_attackCooldownLeft - ctx.deltaTime, 0.0f);
    }
    if (m_swingLeft > 0.0f)
    {
        m_swingLeft = std::max(m_swingLeft - ctx.deltaTime, 0.0f);
    }

    // 受击红染计时递减（批五A）：纯渲染反馈的剩余时长，与固定步长同源
    // （暂停时随 update 门控冻结，画面保持静止的既有承诺不变）
    if (m_hitTintLeft > 0.0f)
    {
        m_hitTintLeft = std::max(m_hitTintLeft - ctx.deltaTime, 0.0f);
    }

    // 冻结锁定（批六丁）：不可移动（输入与击退全部停用）、只保留站立动画。
    // 冻结时长由场景的 FreezeMeter 推进；本类只消费布尔状态——速度/击退
    // 残余清零，避免解冻瞬间带着冻结前的惯性滑出去
    if (m_frozen)
    {
        m_velocityX = 0.0f;    // 水平速度清零
        m_velocityY = 0.0f;    // 垂直速度清零
        m_knockbackVX = 0.0f;  // 击退速度 x 清零
        m_knockbackVY = 0.0f;  // 击退速度 y 清零
        m_knockbackLeft = 0.0f; // 击退窗口关闭
        m_animTime = 0.0f;     // 动画计时清零（站立帧）
        setAnimationRange(kIdleFrame, kIdleFrame); // 定格站立帧
        return; // 本步不移动（玩家位置保持不变）
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

    // 击退窗口内抑制移动输入（批五A）：受击后约 0.18s 内 WASD 方向输入
    // 视为 0——击退位移接管移动、操作权短暂交还物理；窗口结束的下一步
    // 输入自动恢复（玩家从静止重新加速）。攻击输入不在此列（左键横扫
    // 照常可出——「被打了还能反击」的既有手感保留）
    if (m_knockbackLeft > 0.0f)
    {
        inputX = 0.0f; // 抑制水平输入
        inputY = 0.0f; // 抑制垂直输入
    }

    // 2) 朝向翻转（批二改版）：跟随鼠标——指针在玩家中心右侧朝右、左侧
    //    朝左；贴近中心线（±kFlipDeadZone 像素死区）时保持上一次朝向，
    //    避免指针压在中线上时来回抖动。setFlipped 是纯渲染层标记：
    //    碰撞/插值/扇形判定都按未翻转的数据计算，不影响任何玩法判定。
    //    鼠标位置由场景注入「世界坐标」（setAimTarget）——屏幕->世界的
    //    换算收敛在场景层；从未注入过（无输入系统等）时保持朝向不变
    if (m_hasAim)
    {
        const float centerX = m_x + m_w * 0.5f;               // 玩家几何中心 x（世界坐标）
        const float mouseOffsetX = m_aimX - centerX;          // 指针相对中心的横向差（世界坐标）
        if (mouseOffsetX > kFlipDeadZone)
        {
            setFlipped(false); // 指针在右侧：朝右（素材原始朝向）
        }
        else if (mouseOffsetX < -kFlipDeadZone)
        {
            setFlipped(true); // 指针在左侧：朝左（水平镜像绘制）
        }
        // 死区内：维持上一次朝向（不更新 = 不抖动）。
        // 注：m_aimY（纵轴分量）当前不参与翻转判定——水平镜像只需要横向
        // 差值；保留完整点位是「瞄准点」语义的完整性（后续朝向细节可用）
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

    // 4) 位置积分：本步位移 = 当前速度 x 固定步长（速度单位：像素/秒）。
    //    自批二起改为「世界碰撞滑动」：分轴尝试 + 撞墙停该轴 + 子步防穿透
    //    （取代旧版「窗口边框 clamp」——玩家活动范围 = 世界边界，由边界墙
    //    自然形成；判定在实体内部、按注入的 World 执行）。
    //    自批四（碰撞体积与视觉解耦）：滑动判定输入改用「脚部碰撞盒」
    //    （左上角 = 精灵左上角 + 偏移）——身体可叠压墙脚、脚盒不穿透。
    //    批五A：击退速度并入同一位移提交（窗口外恒为 0）——击退同样走
    //    碰撞滑动，「撞墙滑动不穿墙」由同一条路径保证
    const float stepDX = m_velocityX * dt + m_knockbackVX * dt; // 本步期望水平位移（像素）
    const float stepDY = m_velocityY * dt + m_knockbackVY * dt; // 本步期望垂直位移（像素）

    if (m_world != nullptr)
    {
        // 世界在位：整段位移交给纯函数（内部拆分小步、逐轴试走）——
        // 一轴被挡、另一轴照常推进，天然产生「贴墙滑动」的手感。
        // 批四：除墙体之外，场景注入的「实心盒」（存活怪物们）同样实心——
        // 玩家撞上怪物滑停、不可重合（额外实心判据由回调提供）
        const SDL_FRect col = collisionRect(); // 本步起点的碰撞盒（世界坐标）
        // 批六乙：玩家专用入口——水域可通行（入水由场景触发落水），
        // 墙与怪物实心判据完全不变
        const SDL_FPoint moved = m_world->slideMovePlayer(col.x, col.y, col.w, col.h,
                                                          stepDX, stepDY,
                                                          &Player::solidBlockedCallback, this);
        m_x += moved.x - col.x; // 落定：盒的位移量直接作用于精灵左上角（偏移恒定）
        m_y += moved.y - col.y; // 同上（y 轴）
    }
    else
    {
        // 世界未注入（防御分支）：退化为无碰撞位移——不限制、不崩溃
        m_x += stepDX; // 直接推进 x
        m_y += stepDY; // 直接推进 y
    }

    // 4.5) 击退推进收尾（批五A）：本步位移已按衰减前的速度完整计入（上一步），
    //      现在做指数衰减并递减窗口计时；计时归零时残余低速直接清零——击退
    //      历时恰为 kHitKnockbackSeconds（0.18s ≈ 22 个固定步；末速约 98px/s
    //      被截断，对应亚像素级的瞬时收尾，肉眼不可辨）。顺序说明：先位移、
    //      后衰减，保证「计时归零的那一步」仍贡献完整位移（总位移口径稳定）
    if (m_knockbackLeft > 0.0f)
    {
        const float decay = std::exp(-dt / kHitKnockbackTau); // 本步指数衰减因子（固定步下为常量）
        m_knockbackVX *= decay;                                 // 衰减 x 速度（供下一步位移使用）
        m_knockbackVY *= decay;                                 // 衰减 y 速度（同上）
        m_knockbackLeft = std::max(m_knockbackLeft - dt, 0.0f); // 窗口计时递减（夹到 0）
        if (m_knockbackLeft <= 0.0f)
        {
            m_knockbackVX = 0.0f; // 窗口结束：残余速度清零（不再产生位移）
            m_knockbackVY = 0.0f; // 同上（y 轴）
        }
    }

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

// 覆写渲染：本体（含翻转/插值/帧区间）之后叠加横扫表现
//（主体剑 + 3 道渐隐残影；扫程位置走共享缓出曲线，与判定同一相位）
void Player::render(SDL_Renderer* renderer, float alpha, const Camera& camera) const
{
    // 落水表现期（批六乙）：下沉 + 淡出的专用绘制——不画武器与受击红染
    //（死亡过场优先于战斗反馈；alpha 归零后玩家完全不可见）
    if (m_drownAlpha < 255 || m_drownSinkY > 0.0f)
    {
        renderDrownBody(renderer, alpha, camera); // 本体按落水视觉参数绘制
        return;                                   // 落水过场期间不再叠加武器/红染
    }

    // 基础绘制（含未加载纹理时的安全跳过）：按插值位置画当前动画帧
    //（基类内部完成世界坐标 -> 屏幕坐标的相机换算）
    Entity::render(renderer, alpha, camera);

    // 受击红染（批五A）：受击后 kHitTintSeconds 内把同一帧以「红光叠加」
    // 再画一遍——ADD 混合 + 颜色调制压掉大部分绿蓝通道（与怪物红染/出招
    // 脉冲同一技法：素材复用、不加新图）。强度 = kHitTintGreenBlue
    //（0~255，越低越红——可调常量）。旧版无敌闪烁（渲染跳隐）已移除：
    // 受击的可读反馈由「击退位移 + 红染」承担，不再有逐帧显隐
    if (m_hitTintLeft > 0.0f && m_texture != nullptr)
    {
        SDL_SetTextureColorMod(m_texture, 255,
                               static_cast<Uint8>(kHitTintGreenBlue),
                               static_cast<Uint8>(kHitTintGreenBlue)); // 偏红：只留少量绿蓝参与加色
        SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_ADD);         // 叠加混合：在底色上「加光」
        Entity::render(renderer, alpha, camera);                       // 第二次绘制：红光叠加
        SDL_SetTextureColorMod(m_texture, 255, 255, 255);              // 还原颜色调制（防污染后续绘制）
        SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_BLEND);       // 还原混合模式
    }

    // 冻结裹冰视觉（批六丁）：蓝白冰罩盖在本体之上；冻结时横扫已取消，
    // 画完直接结束（不再绘制武器）
    if (m_frozen)
    {
        renderFreezeOverlay(renderer, alpha, camera); // 半透明冰罩 + 白边 + 冰纹
        return; // 冻结期间没有武器表现
    }

    // 只有「横扫表现期内」且武器纹理已加载时才画剑；其余时刻直接结束
    //（早退不触碰纹理 alpha 状态——上一帧的绘制末尾已恢复 255）
    if (m_swingLeft <= 0.0f || m_weaponTexture == nullptr)
    {
        return;
    }

    // 横扫进度 [0,1]：0（出手瞬间）-> 1（表现结束）。判定侧（PlayScene）
    // 每个固定步查询同一个值——两侧由此共用这一条缓出曲线
    const float progress = swingProgress01();

    // 本次挥砍的瞄准角（弧度）：由挥砍方向单位向量换算（atan2 在
    // 屏幕坐标系下顺时针为正，与扫角的方向一致）
    constexpr float kRadiansToDegrees = 57.29577951308232f; // 弧度 -> 度（180/π 数值）
    const float aimAngleRad = std::atan2(m_swingDirY, m_swingDirX);

    // 本帧刀锋角与扫程起点角：共享曲线（AttackArc.h 的 bladeAngleAt）——
    // 判定侧用同一函数取相邻两步的角求「本步扫过区间」，画到哪、打到哪
    const float bladeAngleRad = bladeAngleAt(aimAngleRad, progress, m_sweepArcDegrees);
    const float startAngleRad = bladeAngleAt(aimAngleRad, 0.0f, m_sweepArcDegrees);

    // 旋转锚点（批二：双层换算）：先在「世界坐标」下取玩家插值中心、
    // 下移 kOrbitPivotOffsetY（支点修正——剑绕「略低于身体中心」的点扫过，
    // 观感上更接近手持剑的挥动支点），再经相机换算到屏幕。
    // 武器与本体共用同一条插值公式 + 同一台相机——快速移动不脱节、
    // 相机平滑跟随时武器也不会与本体漂移分离
    SDL_FPoint pivot = interpolatedCenter(alpha); // 玩家插值中心（世界坐标）
    pivot.y += kOrbitPivotOffsetY;                // 世界坐标下移到旋转锚点
    const Vec2 pivotScreen = camera.worldToScreen(pivot.x, pivot.y); // 锚点的屏幕坐标
    pivot.x = pivotScreen.x; // 锚点横坐标切换为屏幕系（后续绘制全用屏幕系）
    pivot.y = pivotScreen.y; // 锚点纵坐标切换为屏幕系

    // 单道剑的绘制（主体与残影共用）：给定刀锋角与透明度，把武器中心
    // 摆在「锚点 + 刀锋方向 x 轨道半径」处、绕矩形自身中心旋转绘制
    const auto drawBlade = [&](float angleRad, int alphaByte)
    {
        // 武器中心 = 旋转锚点 + 刀锋方向 x 轨道半径（剑身绕锚点扫过）
        const float weaponX = pivot.x + std::cos(angleRad) * kWeaponOrbit; // 武器中心 x
        const float weaponY = pivot.y + std::sin(angleRad) * kWeaponOrbit; // 武器中心 y

        // 目标矩形：把武器中心作为矩形中心摆放（宽高 = 素材绘制尺寸常量）
        const SDL_FRect dst{ weaponX - kWeaponWidth * 0.5f,  // 左上角 x = 中心 - 半宽
                             weaponY - kWeaponHeight * 0.5f, // 左上角 y = 中心 - 半高
                             kWeaponWidth,                    // 绘制宽（像素）
                             kWeaponHeight };                 // 绘制高（像素）

        // 旋转角度 = 刀锋角 + 90 度：素材剑尖朝上（基准姿态），旋转
        // 90 度后剑尖指向刀锋方向（屏幕坐标系、顺时针为正）
        const double angle = angleRad * kRadiansToDegrees + 90.0;

        // 透明度调制：残影半透明、主体 255 全不透明（纹理级状态，
        // 每道绘制前显式设置——不同剑道之间互不串扰）
        SDL_SetTextureAlphaMod(m_weaponTexture, static_cast<Uint8>(alphaByte));

        // 旋转绘制：center 传 nullptr = 绕目标矩形自身中心旋转（武器中心即挂点）
        SDL_RenderTextureRotated(renderer, m_weaponTexture, nullptr, &dst,
                                 angle, nullptr, SDL_FLIP_NONE);
    };

    // 残影轨迹：从最旧（落后 24°）到最近（落后 8°）逐道绘制在主体之下。
    // 残影角 = 当前刀锋角 − 落后量，并夹在「扫程起点 -> 当前刀锋」区间内：
    // 扫程刚开始时刀锋尚未扫过 24°，残影不会出现在未扫过的区域上；
    // 每道的透明度由常量表给出（越远越淡），形成渐隐拖尾
    for (int i = kTrailCount - 1; i >= 0; --i)
    {
        // 本道残影的落后量（度 -> 弧度）
        const float lagRad = kTrailLagDegrees[i] * kAttackArcDegreesToRadians;
        // 残影角：不早于起点、不晚于当前刀锋（clamp 保证只画已扫过区间）
        const float trailAngleRad = std::max(bladeAngleRad - lagRad, startAngleRad);
        // 绘制本道残影：透明度查表（0.55/0.30/0.15 换算成 0~255 字节）
        drawBlade(trailAngleRad, static_cast<int>(kTrailAlphas[i] * 255.0f + 0.5f));
    }

    // 主体剑最后绘制（压在所有残影之上）：alpha 恢复 255 全不透明，
    // 纹理级状态回到中性——下一帧无论画不画剑都不受影响
    drawBlade(bladeAngleRad, 255);
}

// 设置落水表现（批六乙）：参数夹取到合法区间后写入纯渲染状态。
// 下沉量不允许为负（向上浮起没有落水语义）；不透明度夹到 [0,1]。
// 复活/重开/进入新对局时调用 setDrownVisual(0.0f, 1.0f) 复位
void Player::setDrownVisual(float sinkPixels, float opacity01)
{
    m_drownSinkY = std::max(0.0f, sinkPixels); // 下沉偏移：负值按 0（不向上浮）
    m_drownAlpha = static_cast<Uint8>(
        std::clamp(opacity01, 0.0f, 1.0f) * 255.0f + 0.5f); // 不透明度 -> 8 位 alpha
}

// 立即取消横扫表现（批六乙）：只清表现计时，战斗数值/冷却/方向不动
void Player::cancelSwingVisual()
{
    m_swingLeft = 0.0f; // 横扫表现窗口关闭：下一帧 render 不再画剑/残影
}

// 落水表现期的本体绘制（批六乙）：与 Entity::render 同一帧号/插值/翻转
// 算法，额外叠加「世界坐标下沉偏移」与「纹理 alpha」。只在 Drowning 期间
// 由 render 分支调用；绘制结束后恢复纹理 alpha（防污染后续绘制）
void Player::renderDrownBody(SDL_Renderer* renderer, float alpha, const Camera& camera) const
{
    // 纹理未加载（极端降级）：没有可画内容
    if (m_texture == nullptr)
    {
        return;
    }

    // 插值位置 + 下沉偏移：先按固定步插值，再让画面整体向下没入水中。
    // 逻辑位置 m_x/m_y 不变——碰撞/落水判定仍按水面位置执行
    const float renderX = m_prevX + (m_x - m_prevX) * alpha;      // 插值后的世界 x
    const float renderY = m_prevY + (m_y - m_prevY) * alpha + m_drownSinkY; // 插值 y + 下沉量

    // 世界 -> 屏幕换算：与基类绘制同一条相机路径
    const Vec2 screenPos = camera.worldToScreen(renderX, renderY); // 屏幕绘制点

    // 目标矩形：完整精灵（与 Entity::render 的视觉口径一致）
    const SDL_FRect dst{ screenPos.x, screenPos.y, m_w, m_h }; // 左上角 + 完整尺寸

    // 帧号换算：防御性夹取子区间（与 Entity::render 逐行同式）
    const int firstFrame = std::clamp(m_animFirst, 0, m_frameCount - 1); // 区间起点
    const int lastFrame = (m_animLast < 0)
        ? m_frameCount - 1
        : std::clamp(m_animLast, firstFrame, m_frameCount - 1); // 区间终点
    const int rangeLength = lastFrame - firstFrame + 1; // 子区间长度（含两端）
    const int frameIndex =
        firstFrame + static_cast<int>(m_animTime / m_frameTime) % rangeLength; // 当前帧号

    // 源矩形：从精灵表裁出当前帧（单帧图/程序化纹理同样适用）
    const SDL_FRect src{ static_cast<float>(frameIndex) * m_w, 0.0f, m_w, m_h };

    // 纹理透明度调制：落水淡出只作用于本体（不改变其他实体纹理状态）
    SDL_SetTextureAlphaMod(m_texture, m_drownAlpha);
    if (m_flipped) // 朝向翻转：与基类同一路径（旋转 0 度 + 水平镜像）
    {
        SDL_RenderTextureRotated(renderer, m_texture, &src, &dst, 0.0, nullptr,
                                 SDL_FLIP_HORIZONTAL);
    }
    else // 常规朝向：快路径直接绘制
    {
        SDL_RenderTexture(renderer, m_texture, &src, &dst);
    }
    SDL_SetTextureAlphaMod(m_texture, 255); // 还原不透明：防污染后续绘制
}

// 冻结裹冰视觉（批六丁）：本体之上叠一层「蓝白冰罩」——
// 半透明冰蓝填充 + 亮白粗边 + 三道斜冰纹；纯 SDL 图元、无新素材。
// 位置与本体共用插值公式，快速位移/相机滚动都不脱离。
void Player::renderFreezeOverlay(SDL_Renderer* renderer, float alpha, const Camera& camera) const
{
    const float renderX = m_prevX + (m_x - m_prevX) * alpha; // 插值左上角 x
    const float renderY = m_prevY + (m_y - m_prevY) * alpha; // 插值左上角 y
    const Vec2 screen = camera.worldToScreen(renderX, renderY); // 世界 -> 屏幕
    const SDL_FRect block{ screen.x - 3.0f, screen.y - 3.0f, // 冰罩：向外扩 3px
                           m_w + 6.0f, m_h + 6.0f }; // 覆盖整个精灵
    const SDL_FRect inner{ block.x + 3.0f, block.y + 3.0f, // 内层亮边
                           block.w - 6.0f, block.h - 6.0f }; // 与本体同尺寸

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); // 半透明叠加
    SDL_SetRenderDrawColor(renderer, 140, 205, 255, 105); // 冰蓝填充（约四成不透明）
    SDL_RenderFillRect(renderer, &block); // 冰罩主体

    SDL_SetRenderDrawColor(renderer, 232, 248, 255, 235); // 近白亮边
    SDL_RenderRect(renderer, &block); // 外框
    SDL_RenderRect(renderer, &inner); // 内框（双线冰壳观感）

    // 三道斜冰纹：从左上到右下贯穿冰罩，读作「冰层里的裂纹」
    SDL_SetRenderDrawColor(renderer, 220, 242, 255, 190); // 冰白半透明
    SDL_RenderLine(renderer, block.x + block.w * 0.18f, block.y + 2.0f, // 第一道起点
                   block.x + block.w * 0.42f, block.y + block.h - 2.0f); // 第一道终点
    SDL_RenderLine(renderer, block.x + block.w * 0.46f, block.y + 2.0f, // 第二道起点
                   block.x + block.w * 0.70f, block.y + block.h - 2.0f); // 第二道终点
    SDL_RenderLine(renderer, block.x + block.w * 0.74f, block.y + 2.0f, // 第三道起点
                   block.x + block.w * 0.94f, block.y + block.h - 2.0f); // 第三道终点

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE); // 恢复默认混合
}

// 受击击退（批五A）：沿「伤害来源 -> 玩家」方向被推开 + 红染 + 输入抑制
//（契约见头文件；PlayScene 在怪物攻击命中时调用）
void Player::applyHitKnockback(float fromCenterX, float fromCenterY)
{
    // 方向 = 来源中心 -> 玩家中心（「远离伤害来源」的被推开方向）。
    // 用中心点计算：与击退/飘字等既有方向换算同口径，尺寸差不会偏方向
    const float centerX = m_x + m_w * 0.5f; // 玩家中心 x（世界坐标）
    const float centerY = m_y + m_h * 0.5f; // 玩家中心 y（世界坐标）
    const float dx = centerX - fromCenterX; // 水平分量（正右负左）
    const float dy = centerY - fromCenterY; // 垂直分量（正下负上）
    const float length = std::sqrt(dx * dx + dy * dy); // 方向长度（像素）

    // 归一化后乘以初速；两中心几乎重合（长度过小）时回退朝 +x——
    // 与击退/挥砍等既有方向零向量防御同一口径：确定性方向、绝不 NaN
    if (length > kDirectionEpsilon)
    {
        m_knockbackVX = dx / length * kHitKnockbackSpeed; // 单位方向 x x 初速
        m_knockbackVY = dy / length * kHitKnockbackSpeed; // 单位方向 y x 初速
    }
    else
    {
        m_knockbackVX = kHitKnockbackSpeed; // 重合退化：默认朝右（确定性）
        m_knockbackVY = 0.0f;               // 水平方向
    }

    // 受击冲击：常规速度清零——击退窗口内输入被抑制、速度保持 0，
    // 窗口结束后玩家从静止重新加速（「被打退又站稳」的手感顺序）
    m_velocityX = 0.0f; // 水平速度清零
    m_velocityY = 0.0f; // 垂直速度清零

    // 击退/输入抑制窗口与红染计时置满（连续受击时各自重新计时）
    m_knockbackLeft = kHitKnockbackSeconds; // 击退窗口（约 0.18s）
    m_hitTintLeft = kHitTintSeconds;        // 红染（约 0.3s，比击退略长）
}

// 重开归位：委托基类的 setPosition 瞬间覆盖坐标（不经过速度/时间累积），
// 并清零速度与攻击状态——否则重开后玩家会带着上一局的动量/冷却/横扫继续
void Player::reset(float x, float y)
{
    m_velocityX = 0.0f; // 水平速度清零：全新一局从静止开始
    m_velocityY = 0.0f; // 垂直速度清零：同上

    // 攻击状态复位：冷却立即转好、横扫清除——新一局第一帧就能出手，
    // 且不残留上一局未播完的挥砍画面
    m_attackCooldownLeft = 0.0f; // 冷却清零：新一局立即可以出手
    m_swingLeft = 0.0f;          // 横扫表现清零：清除未播完的挥砍画面

    // 受击反馈状态清零（批五A）：上一局的击退/红染绝不带进新一局——
    // 新一局第一帧起输入立即可用、画面无红光残留
    m_knockbackVX = 0.0f;   // 击退速度 x 清零
    m_knockbackVY = 0.0f;   // 击退速度 y 清零
    m_knockbackLeft = 0.0f; // 击退窗口关闭（输入不再被抑制）
    m_hitTintLeft = 0.0f;   // 红染熄灭

    // 落水表现清零（批六乙）：重开/复活归位后本体恢复完整可见
    m_drownSinkY = 0.0f;    // 下沉偏移归零
    m_drownAlpha = 255;     // 不透明度恢复 255
    m_frozen = false;       // 冻结锁定解除（新一局/复活不带旧状态）

    // 挥砍朝向回初始（向右）：新一局的第一个固定步会按鼠标位置重新
    // 决定朝向与瞄准方向，这里只是把状态归到与构造时一致的起点
    m_swingDirX = 1.0f; // 单位方向 x：初始向右
    m_swingDirY = 0.0f; // 单位方向 y：初始水平

    // 动画区间先切回站立帧：重开后玩家静止，第一帧渲染就应显示站姿
    //（后续 update 会按实际速度重新切换区间）
    setAnimationRange(kIdleFrame, kIdleFrame);

    setPosition(x, y); // 复用基类实现：写入坐标并同步插值快照（防拉丝）
}

// 尝试朝瞄准方向出手：冷却门控 + 横扫表现启动（命中结算在场景层）
bool Player::tryAttack(float aimDirX, float aimDirY)
{
    // 冷却未到：本次不出手（不重置任何状态，冷却继续走）
    if (m_attackCooldownLeft > 0.0f)
    {
        return false;
    }

    // 方向归一化：传入向量不要求是单位长度（场景直接传未归一化的
    // 「玩家中心 -> 鼠标」位移）。长度过小（零向量退化；场景已有一道
    // 回退，这里是第二道防御）时保留上一次的挥砍方向——避免除以接近 0
    // 的长度产生 NaN，污染旋转角与判定中轴
    const float length = std::sqrt(aimDirX * aimDirX + aimDirY * aimDirY); // 方向长度（像素）
    if (length > kDirectionEpsilon)
    {
        m_swingDirX = aimDirX / length; // 单位方向 x：分量占全长的比例
        m_swingDirY = aimDirY / length; // 单位方向 y：分量占全长的比例
    }

    // 出手：重置冷却门 + 启动横扫表现计时（render 据此绘制挥砍画面）。
    // 不索敌：出手与场上有没有怪物无关——挥空也出招、也消耗冷却
    m_attackCooldownLeft = m_attackCooldown; // 冷却开始走：届时的后续出手被它门控
    m_swingLeft = m_swingSeconds;            // 横扫表现从满时长开始（−半张角 -> +半张角）

    return true; // 通知场景：本次出手成功（其逐帧扫程判定以 swingActive 为准）
}

// 本次扫程的归一化进度 [0,1]（判定与渲染共用的相位；见头文件契约）
float Player::swingProgress01() const
{
    // 不在扫程中（含剩余归零的收尾步）：按「已完成」返回 1
    if (!(m_swingLeft > 0.0f))
    {
        return 1.0f;
    }

    // 进度 = 1 - 剩余/总时长。setSwingTime 保证总时长为正；
    // 逐帧递减的浮点误差用 clamp 收进 [0,1]，保证是合法进度
    const float progress = 1.0f - m_swingLeft / m_swingSeconds; // 未夹取的原始进度
    return std::clamp(progress, 0.0f, 1.0f); // 夹取：0=刚出手、1=扫程结束
}

// 插值后的玩家中心点（世界坐标）：横扫表现挂点
SDL_FPoint Player::interpolatedCenter(float alpha) const
{
    // 插值位置 = 上一固定步位置与当前位置按 alpha 线性插值（与基类 render 同式），
    // 再换算到中心点——武器因此与玩家本体定位于同一个平滑世界点
    //（后续由 render 统一经相机换算到屏幕）
    const float renderX = m_prevX + (m_x - m_prevX) * alpha; // 插值后的世界 x
    const float renderY = m_prevY + (m_y - m_prevY) * alpha; // 插值后的世界 y
    return SDL_FPoint{ renderX + m_w * 0.5f, renderY + m_h * 0.5f }; // 中心点 = 插值位置 + 半尺寸
}
