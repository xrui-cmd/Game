#include "game/Enemy.h"
#include <algorithm> // std::clamp / std::max：移动边界与出生范围的上界保护
#include <cmath>     // std::sqrt：归一化方向与距离计算

// 加载敌人纹理：三级回退流程由基类统一实现，这里只提供本实体的参数
bool Enemy::load(SDL_Renderer* renderer)
{
    // loadTextureWithFallback 依次尝试：
    //   1. assets/enemy_sheet.png 逼近精灵表（2 帧，持续播动画）
    //   2. assets/enemy.png 单帧精灵（动画退化为静止）
    //   3. 程序化 32x32 红紫色方块（素材缺失时兜底）
    return loadTextureWithFallback(renderer,
                                   "assets/enemy_sheet.png",      // 首选：逼近精灵表相对路径
                                   "assets/enemy.png",            // 次选：单帧精灵相对路径
                                   kFrameCount,                   // 精灵表帧数：2 帧
                                   kDefaultFrameTime,             // 每帧时长：默认 0.2 秒
                                   SDL_Color{186, 64, 148, 255}); // 回退颜色：不透明红紫色（与素材同色系）
}

// 设置追踪目标：只存目标矩形的中心点——追踪是「朝点直线行进」，
// 不需要每步保存整块矩形（本步速度另有 setChaseSpeed 注入）
void Enemy::setTarget(const SDL_FRect& targetRect)
{
    m_targetX = targetRect.x + targetRect.w * 0.5f; // 目标中心 x：矩形左上角 + 半宽
    m_targetY = targetRect.y + targetRect.h * 0.5f; // 目标中心 y：矩形左上角 + 半高
    m_hasTarget = true;                             // 标记本步有目标：update 将执行追踪
}

// 在窗口内随机找一个出生点；「尽量」远离 avoidRect 中心（通常传玩家矩形）
void Enemy::spawn(int winW, int winH, const SDL_FRect& avoidRect)
{
    // 玩家矩形中心：生成距离的参照点（与 setTarget 同一取中心算法）
    const float avoidCenterX = avoidRect.x + avoidRect.w * 0.5f;
    const float avoidCenterY = avoidRect.y + avoidRect.h * 0.5f;

    // 随机坐标的上界：窗口尺寸减去敌人自身大小。用 std::max 兜底，
    // 防止窗口异常小时出现「上界小于下界」（与 Coin::respawn 同一防护写法）
    const float maxX = std::max(static_cast<float>(winW) - m_w, 0.0f);
    const float maxY = std::max(static_cast<float>(winH) - m_h, 0.0f);

    // 最多尝试 kSpawnMaxTries 次；只要某次位置与玩家中心的距离达到
    // kMinSpawnDistance（约 300px）就立即采用——避免生成在玩家脸上
    for (int tryCount = 0; tryCount < kSpawnMaxTries; ++tryCount)
    {
        // SDL_randf() 返回 [0,1) 的随机浮点数，映射到 [0, maxX] 区间
        m_x = SDL_randf() * maxX;
        // Y 方向同理，独立取随机数，避免敌人总出现在对角线上
        m_y = SDL_randf() * maxY;

        // 敌人中心到玩家中心的位移：用「距离平方」比较，
        // 省一次开方（两侧同为非负值，比值关系不变）
        const float dx = m_x + m_w * 0.5f - avoidCenterX;
        const float dy = m_y + m_h * 0.5f - avoidCenterY;
        if (dx * dx + dy * dy >= kMinSpawnDistance * kMinSpawnDistance)
        {
            break; // 距离达标，采用该位置并结束重试
        }
    }
    // 若重试耗尽仍靠得太近（窗口极小等极端情况），接受最后一次结果：
    // 设计目标是「尽量远离」而非「绝对远离」，避免极端情况下的死循环

    // 落在窗口内：随机范围本身已在窗口内，这行是防御「未来改动破坏
    // 前置假设」的兜底（如外界直接修改位置后再 spawn 的调用方式）
    m_x = std::clamp(m_x, 0.0f, maxX);
    m_y = std::clamp(m_y, 0.0f, maxY);

    // 瞬移类操作的插值收尾（重点）：出生是「瞬间换位」，必须把插值快照
    // 对齐到新落点，否则渲染会从上一位置向新落点拉丝（同 Coin::respawn）
    snapshotPreviousPosition();
}

// 被撞开：从 (fromX, fromY)（玩家中心）指向敌人中心的方向瞬移 kKnockbackDistance 像素
void Enemy::knockback(float fromX, float fromY, float viewportW, float viewportH)
{
    // 敌人当前中心：击退位移以「中心」为参照计算，避免用左上角坐标推位移时
    // 把尺寸差带进方向判定（两者对方向的影响相同，但对落点更直观）
    const float centerX = m_x + m_w * 0.5f;
    const float centerY = m_y + m_h * 0.5f;

    // 方向向量：玩家中心 -> 敌人中心（「远离玩家」的方向）
    const float dx = centerX - fromX;
    const float dy = centerY - fromY;
    const float length = std::sqrt(dx * dx + dy * dy);

    // 归一化方向；两个中心几乎重合（距离过小）时退回默认向右，
    // 避免除以接近 0 的长度产生 NaN 或异常位移
    float dirX = 1.0f; // 默认向右
    float dirY = 0.0f; // 默认水平方向
    if (length > kArriveEpsilon)
    {
        dirX = dx / length; // 单位方向 x：dx 占全长的比例
        dirY = dy / length; // 单位方向 y：dy 占全长的比例
    }

    // 新中心 = 现中心 + 单位方向 x 击退距离；再换算回左上角坐标写入落点
    m_x = centerX + dirX * kKnockbackDistance - m_w * 0.5f;
    m_y = centerY + dirY * kKnockbackDistance - m_h * 0.5f;

    // clamp 到视口内：贴边被撞时敌人会退到「边界上最近的有效点」，
    // 不会飞出屏幕（用 max 兜底窗口异常小的情形，同 spawn）
    const float maxX = std::max(viewportW - m_w, 0.0f);
    const float maxY = std::max(viewportH - m_h, 0.0f);
    m_x = std::clamp(m_x, 0.0f, maxX);
    m_y = std::clamp(m_y, 0.0f, maxY);

    // 瞬移类操作的插值收尾：与 spawn 同理，落点定稿后快照 prev=cur 防拉丝
    snapshotPreviousPosition();
}

// 覆写更新：帧动画 + 朝目标点的直线追踪
void Enemy::update(const UpdateContext& ctx)
{
    // 第一行固定动作：快照「本步开始前的位置」作为渲染插值起点。
    // 覆写 update 会跳过 Entity::update，这条调用必须自己补上
    snapshotPreviousPosition();

    // 帧动画无条件推进（基类默认实现的同款语句）：移动与否不影响动画节奏，
    // 蠕动动画在追踪全程持续播放
    m_animTime += ctx.deltaTime;

    // 无目标：仅播动画（防御性分支——PlayScene 在 Playing 状态每步都会注入目标）
    if (!m_hasTarget)
    {
        return;
    }

    // 从敌人中心指向目标的向量：追踪以「中心 -> 中心」的连线为准
    const float centerX = m_x + m_w * 0.5f;
    const float centerY = m_y + m_h * 0.5f;
    const float dx = m_targetX - centerX;
    const float dy = m_targetY - centerY;
    const float distance = std::sqrt(dx * dx + dy * dy);

    // 已到达目标附近：原地不动。阈值判定避免目标点附近的来回抖动，
    // 也排除距离为 0 时归一化「除以零」的退化情形
    if (distance <= kArriveEpsilon)
    {
        return;
    }

    // 沿归一化方向 x 速度移动（固定步长积分）。与玩家「逐轴独立」不同：
    // 敌人是直线追击，归一化保证任意方向合速度恒等于注入的速度值。
    // 速度由 PlayScene 按难度曲线注入（基础速度 + 成长 x 局内秒数）
    m_x += (dx / distance) * m_chaseSpeed * ctx.deltaTime; // 水平分量：单位方向 x 速度 x 步长
    m_y += (dy / distance) * m_chaseSpeed * ctx.deltaTime; // 垂直分量：同上（y 轴向下为正）

    // 左上角 clamp 在视口内（与玩家边界规则一致：右/下界 = 视口尺寸 - 自身尺寸）
    m_x = std::clamp(m_x, 0.0f, ctx.viewportWidth - m_w);
    m_y = std::clamp(m_y, 0.0f, ctx.viewportHeight - m_h);
}
