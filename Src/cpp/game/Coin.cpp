#include "game/Coin.h"
#include "game/Homing.h" // advanceHomingSpeed / advanceHomingPoint：磁吸积分的纯函数

// 加载金币纹理：三级回退流程由基类统一实现，这里只提供本实体的参数
bool Coin::load(SDL_Renderer* renderer)
{
    // loadTextureWithFallback 依次尝试：
    //   1. assets/coin_sheet.png 旋转精灵表（4 帧，持续播动画，素材包原色）
    //   2. assets/coin.png 单帧精灵（动画退化为静止）
    //   3. 程序化 18x21 金黄色方块（素材缺失时兜底）
    return loadTextureWithFallback(renderer,
                                   "assets/coin_sheet.png",       // 首选：旋转精灵表相对路径
                                   "assets/coin.png",             // 次选：单帧精灵相对路径
                                   kFrameCount,                   // 精灵表帧数：4 帧
                                   kDefaultFrameTime,             // 每帧时长：默认 0.18 秒
                                   SDL_Color{255, 200, 60, 255}); // 回退颜色：不透明金黄色（与金币素材同色系）
}

// 覆写更新：旋转动画 + 磁吸追踪位移（批四）
void Coin::update(const UpdateContext& ctx)
{
    // 第一行固定动作：快照「本步开始前的位置」作为渲染插值起点
    //（覆写 update 会跳过 Entity::update，这条调用必须自己补上）
    snapshotPreviousPosition();

    // 帧动画无条件推进（与基类默认实现同款语句）：静置/飞行都持续旋转
    m_animTime += ctx.deltaTime;

    // 拾取延迟（M3·乙）：Boss 掉落的金币先在地上铺开一会儿再允许磁吸/
    // 拾取——延迟内仍播放旋转动画，但保持原位（落点可见）
    if (m_pickupDelayLeft > 0.0f)
    {
        m_pickupDelayLeft = std::max(m_pickupDelayLeft - ctx.deltaTime, 0.0f); // 递减并夹到 0
        return; // 延迟未结束：本步不移动
    }

    // 未磁化或未注入目标（范围外/防御路径）：静止不动——规格语义
    if (!m_magnetized || !m_hasTarget)
    {
        return;
    }

    // 磁吸速度推进：从 0 起步、按 2000px/s² 加速到 800 封顶（纯函数）
    m_magnetSpeed = advanceHomingSpeed(m_magnetSpeed, kMagnetAccel, kMagnetMaxSpeed, ctx.deltaTime);

    // 追踪位移：朝玩家盒中心移动「速度 x 固定步长」——直接写 m_x/m_y
    //（prev 快照保持，渲染插值照常平滑）。飞行忽略墙体（磁吸设计口径，
    // 见类头注释第 4 条：进入吸附半径即必有回报）
    const float centerX = m_x + m_w * 0.5f; // 自身中心 x（世界坐标）
    const float centerY = m_y + m_h * 0.5f; // 自身中心 y
    const HomingPoint next = advanceHomingPoint(centerX, centerY,
                                                m_magnetX, m_magnetY,
                                                m_magnetSpeed * ctx.deltaTime);
    m_x = next.x - m_w * 0.5f; // 左上角 = 中心 - 半宽
    m_y = next.y - m_h * 0.5f; // 左上角 = 中心 - 半高
}
