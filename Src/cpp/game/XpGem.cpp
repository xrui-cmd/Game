#include "game/XpGem.h"
#include "game/Homing.h" // advanceHomingSpeed / advanceHomingPoint：追踪积分的纯函数

// 加载宝石纹理：三级回退流程由基类统一实现，这里只提供本实体的参数
bool XpGem::load(SDL_Renderer* renderer)
{
    // loadTextureWithFallback 依次尝试：
    //   1. assets/gem_sheet.png 旋转精灵表（4 帧，持续播动画；已染青蓝调）
    //   2. assets/gem.png 单帧精灵（动画退化为静止；同样染色）
    //   3. 程序化 18x21 青蓝色方块（素材缺失时兜底——与染色观感一致，
    //      与金币的「金黄色」回退一眼可分）
    return loadTextureWithFallback(renderer,
                                   "assets/gem_sheet.png",        // 首选：旋转精灵表相对路径
                                   "assets/gem.png",              // 次选：单帧精灵相对路径
                                   kFrameCount,                   // 精灵表帧数：4 帧
                                   kDefaultFrameTime,             // 每帧时长：默认 0.18 秒
                                   SDL_Color{80, 210, 250, 255}); // 回退颜色：青蓝调（与金币金色区分）
}

// 覆写更新：旋转动画 + 自动吸收的追踪位移（批四）
void XpGem::update(const UpdateContext& ctx)
{
    // 第一行固定动作：快照「本步开始前的位置」作为渲染插值起点
    //（覆写 update 会跳过 Entity::update，这条调用必须自己补上）
    snapshotPreviousPosition();

    // 帧动画无条件推进（与基类默认实现同款语句）：飞行与否都持续旋转
    m_animTime += ctx.deltaTime;

    // 未注入吸收目标（防御路径）：只播动画、不位移
    if (!m_hasHoming)
    {
        return;
    }

    // 速度推进：起步 700、按 800/s² 加速到 900 封顶（纯函数，见 Homing.h）
    m_homingSpeed = advanceHomingSpeed(m_homingSpeed, kHomingAccel, kHomingSpeedCap, ctx.deltaTime);

    // 追踪位移：朝玩家盒中心移动「速度 x 固定步长」——直接写 m_x/m_y
    //（起飞点的 prev 快照保持不动，渲染插值照常平滑，无拉丝）。
    // 飞行忽略墙体（批四设计口径）：吸收是奖励结算的视觉化，玩家无需
    // 走动、宝石穿墙也要飞到手上——「掉落必有回报」不因地形打折
    const float centerX = m_x + m_w * 0.5f; // 自身中心 x（世界坐标）
    const float centerY = m_y + m_h * 0.5f; // 自身中心 y
    const HomingPoint next = advanceHomingPoint(centerX, centerY,
                                                m_homingX, m_homingY,
                                                m_homingSpeed * ctx.deltaTime);
    m_x = next.x - m_w * 0.5f; // 左上角 = 中心 - 半宽
    m_y = next.y - m_h * 0.5f; // 左上角 = 中心 - 半高
}
