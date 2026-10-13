#include "game/Player.h"
#include <algorithm> // std::clamp：把坐标限制在视口范围内

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

// 覆写更新：移动 + 行走动画。与改造前行为完全一致：
// 50x50 方块、300 像素/秒、边界 clamp、移动才播动画、静止清零归位第 1 帧
void Player::update(const UpdateContext& ctx)
{
    // 本帧是否有移动输入：任一方向键按下即视为「要走动」，用于驱动行走动画
    bool isMoving = false;

    // WASD 控制方向，注意 SDL 中 y 轴向下为正
    if (ctx.keyboard[SDL_SCANCODE_W])
    {
        m_y -= m_speed * ctx.deltaTime; // 上：本帧位移 = 速度 x 帧间隔
        isMoving = true;                // 记录本帧有移动输入（动画需要）
    }
    if (ctx.keyboard[SDL_SCANCODE_S])
    {
        m_y += m_speed * ctx.deltaTime; // 下
        isMoving = true;                // 记录本帧有移动输入（动画需要）
    }
    if (ctx.keyboard[SDL_SCANCODE_A])
    {
        m_x -= m_speed * ctx.deltaTime; // 左
        isMoving = true;                // 记录本帧有移动输入（动画需要）
    }
    if (ctx.keyboard[SDL_SCANCODE_D])
    {
        m_x += m_speed * ctx.deltaTime; // 右
        isMoving = true;                // 记录本帧有移动输入（动画需要）
    }

    // 边界限制基于上下文里的视口实际尺寸（不再硬编码 1280/720）：
    // 右边界 = 视口宽 - 自身宽，下边界 = 视口高 - 自身高
    m_x = std::clamp(m_x, 0.0f, ctx.viewportWidth - m_w);
    m_y = std::clamp(m_y, 0.0f, ctx.viewportHeight - m_h);

    // 行走动画推进（固定帧时长与渲染帧率解耦，原理同基类默认实现）：
    // 移动时累加计时器，每攒满 kFrameTime 秒 render 就切到下一帧；
    // 静止时清零，让画面立即固定回第 1 帧（站姿），不会停在半途的迈步姿势
    if (isMoving)
    {
        m_animTime += ctx.deltaTime; // 累积行走时间，供 render 换算帧号
    }
    else
    {
        m_animTime = 0.0f; // 清零计时器 -> 帧号换算结果恒为 0（第 1 帧站姿）
    }
}

// 重开归位：委托基类的 setPosition 瞬间覆盖坐标（不经过速度/时间累积）
void Player::reset(float x, float y)
{
    setPosition(x, y); // 复用基类实现，避免重复维护坐标写入逻辑
}
