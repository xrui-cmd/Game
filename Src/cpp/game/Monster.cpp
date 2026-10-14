#include "game/Monster.h"
#include <algorithm> // std::clamp / std::max：移动边界、出生范围与闪白计时的上下界
#include <cmath>     // std::sqrt：归一化方向与距离计算

// 加载怪物纹理：三级回退流程由基类统一实现，这里只提供本实体的参数
bool Monster::load(SDL_Renderer* renderer)
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
void Monster::setTarget(const SDL_FRect& targetRect)
{
    m_targetX = targetRect.x + targetRect.w * 0.5f; // 目标中心 x：矩形左上角 + 半宽
    m_targetY = targetRect.y + targetRect.h * 0.5f; // 目标中心 y：矩形左上角 + 半高
    m_hasTarget = true;                             // 标记本步有目标：update 将执行追踪
}

// 受击：扣血 + 闪白；返回是否死亡（生命归零）
bool Monster::takeDamage(int amount)
{
    // 负数伤害是调用方错误（等价于治疗且逻辑不通）：警告并忽略，状态不变
    if (amount < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "怪物受击伤害不能为负（收到 %d），忽略本次受击", amount);
        return false; // 未死亡（也没有扣血）
    }

    // 扣血并把生命夹到 0：日志/HUD 永远看不到负数生命
    m_hp = std::max(m_hp - amount, 0);

    // 闪白计时重置为满值：连续受击时白光从本次受击重新起算（视觉连续）
    m_flashLeft = kHitFlashSeconds;

    // 生命归零：转入「未激活」——不再更新/渲染/碰撞，槽位等 spawnEdge 复用
    if (m_hp == 0)
    {
        m_alive = false;     // 本对象退场（调用方负责击杀计数与掉落结算）
        m_hasTarget = false; // 清追踪目标：复用前不残留旧目标点
        return true;         // 通知调用方：本次受击致死，走死亡结算分支
    }

    return false; // 仍有生命：调用方通常接着施加击退
}

// 生成（边缘刷新）：窗口四边随机一边的内侧落位，激活并写入生命值
void Monster::spawnEdge(int winW, int winH, const SDL_FRect& avoidRect, int hp)
{
    // 玩家矩形中心：出生避让的参照点（与 setTarget 同一取中心算法）
    const float avoidCenterX = avoidRect.x + avoidRect.w * 0.5f;
    const float avoidCenterY = avoidRect.y + avoidRect.h * 0.5f;

    // 随机坐标的上界：窗口尺寸减去怪物自身大小。用 std::max 兜底，
    // 防止窗口异常小时出现「上界小于下界」（与既有生成逻辑同一防护写法）
    const float maxX = std::max(static_cast<float>(winW) - m_w, 0.0f);
    const float maxY = std::max(static_cast<float>(winH) - m_h, 0.0f);

    // 最多尝试 kSpawnMaxTries 次：某次落点与玩家中心距离达标即采用——
    // 避免玩家站在屏幕边缘时被「贴脸刷新」（刷新点纯粹随机的公平性保护）
    for (int tryCount = 0; tryCount < kSpawnMaxTries; ++tryCount)
    {
        // 四条边随机挑一条：0=上边 1=下边 2=左边 3=右边（SDL_rand(4) 取 [0,4)）
        const int edge = static_cast<int>(SDL_rand(4));

        if (edge == 0)
        {
            // 上边：x 随机、y 贴边内侧（kEdgeInset 向内收，保证整只可见）
            m_x = SDL_randf() * maxX;
            m_y = kEdgeInset;
        }
        else if (edge == 1)
        {
            // 下边：x 随机、y 贴底边内侧
            m_x = SDL_randf() * maxX;
            m_y = maxY - kEdgeInset;
        }
        else if (edge == 2)
        {
            // 左边：y 随机、x 贴左边内侧
            m_x = kEdgeInset;
            m_y = SDL_randf() * maxY;
        }
        else
        {
            // 右边：y 随机、x 贴右边内侧
            m_x = maxX - kEdgeInset;
            m_y = SDL_randf() * maxY;
        }

        // 怪物中心到玩家中心的位移：用「距离平方」比较，省一次开方
        //（两侧同为非负值，比值关系不变）
        const float dx = m_x + m_w * 0.5f - avoidCenterX;
        const float dy = m_y + m_h * 0.5f - avoidCenterY;
        if (dx * dx + dy * dy >= kMinSpawnDistance * kMinSpawnDistance)
        {
            break; // 距离达标，采用该位置并结束重试
        }
    }
    // 若重试耗尽仍靠得太近（玩家恰好卡在四边中央等极端情况），接受最后一次结果：
    // 设计目标是「尽量远离」而非「绝对远离」，避免极端情况下的死循环

    // 落在窗口内：随机范围本身已在窗口内，这行是防御「未来改动破坏
    // 前置假设」的兜底（与既有生成逻辑同一纪律）
    m_x = std::clamp(m_x, 0.0f, maxX);
    m_y = std::clamp(m_y, 0.0f, maxY);

    // 激活 + 写入战斗数值：生命值由场景按配置注入（monster_hp）
    m_alive = true;             // 本对象进入战场：更新/渲染/碰撞自此生效
    m_hp = std::max(hp, 1);     // 至少 1 点：0 血出生会在首帧判定为已死，属配置错误兜底
    m_flashLeft = 0.0f;         // 新生怪物不带受击光效
    m_hasTarget = false;        // 追踪目标等场景下个固定步注入

    // 瞬移类操作的插值收尾（重点）：出生是「瞬间换位」，必须把插值快照
    // 对齐到新落点，否则渲染会从上一位置向新落点拉丝（延续既有纪律）
    snapshotPreviousPosition();
}

// 被撞开/被击退：从 (fromX, fromY)（伤害来源中心）指向怪物中心的方向
// 瞬移 distance 像素，并把左上角 clamp 在视口内
void Monster::knockback(float fromX, float fromY, float viewportW, float viewportH, float distance)
{
    // 怪物当前中心：击退位移以「中心」为参照计算，避免用左上角坐标推位移时
    // 把尺寸差带进方向判定（两者对方向的影响相同，但对落点更直观）
    const float centerX = m_x + m_w * 0.5f;
    const float centerY = m_y + m_h * 0.5f;

    // 方向向量：伤害来源中心 -> 怪物中心（「远离来源」的方向）
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
    m_x = centerX + dirX * distance - m_w * 0.5f;
    m_y = centerY + dirY * distance - m_h * 0.5f;

    // clamp 到视口内：贴边被撞时怪物会退到「边界上最近的有效点」，
    // 不会飞出屏幕（用 max 兜底窗口异常小的情形，同 spawnEdge）
    const float maxX = std::max(viewportW - m_w, 0.0f);
    const float maxY = std::max(viewportH - m_h, 0.0f);
    m_x = std::clamp(m_x, 0.0f, maxX);
    m_y = std::clamp(m_y, 0.0f, maxY);

    // 瞬移类操作的插值收尾：落点定稿后快照 prev=cur，渲染不拉丝（同 spawnEdge）
    snapshotPreviousPosition();
}

// 覆写更新：帧动画 + 受击闪白计时 + 朝目标点的直线追踪
void Monster::update(const UpdateContext& ctx)
{
    // 第一行固定动作：快照「本步开始前的位置」作为渲染插值起点。
    // 覆写 update 会跳过 Entity::update，这条调用必须自己补上
    snapshotPreviousPosition();

    // 帧动画无条件推进（基类默认实现的同款语句）：移动与否不影响动画节奏，
    // 蠕动动画在追踪全程持续播放
    m_animTime += ctx.deltaTime;

    // 受击闪白计时递减并夹到 0：与固定步长同源，暂停时随 update 门控冻结
    if (m_flashLeft > 0.0f)
    {
        m_flashLeft = std::max(m_flashLeft - ctx.deltaTime, 0.0f);
    }

    // 无目标：仅播动画（防御性分支——PlayScene 在 Playing 状态每步都会注入目标）
    if (!m_hasTarget)
    {
        return;
    }

    // 从怪物中心指向目标的向量：追踪以「中心 -> 中心」的连线为准
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
    // 怪物是直线追击，归一化保证任意方向合速度恒等于注入的速度值。
    // 速度由 PlayScene 按难度曲线注入（基础速度 + 成长 x 局内秒数）
    m_x += (dx / distance) * m_chaseSpeed * ctx.deltaTime; // 水平分量：单位方向 x 速度 x 步长
    m_y += (dy / distance) * m_chaseSpeed * ctx.deltaTime; // 垂直分量：同上（y 轴向下为正）

    // 左上角 clamp 在视口内（与玩家边界规则一致：右/下界 = 视口尺寸 - 自身尺寸）
    m_x = std::clamp(m_x, 0.0f, ctx.viewportWidth - m_w);
    m_y = std::clamp(m_y, 0.0f, ctx.viewportHeight - m_h);
}

// 覆写渲染：常规帧与受击闪白帧
void Monster::render(SDL_Renderer* renderer, float alpha) const
{
    // 基础绘制（含未加载纹理时的安全跳过）：按插值位置画当前动画帧
    Entity::render(renderer, alpha);

    // 闪白叠加：只有「正在闪白」且纹理已加载时执行
    if (m_flashLeft <= 0.0f || m_texture == nullptr)
    {
        return;
    }

    // 受击闪白 = 用加色混合（ADD）把同一帧再画一遍：像素颜色与自身相加后
    // 整体提亮，红紫本体被推亮成偏白的高光。
    // 为什么不用颜色调制（SDL_SetTextureColorMod）：乘法调制只能压暗，
    // 无法把本体「提亮成白光」；叠加混合才能做出闪光效果
    SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_ADD);
    Entity::render(renderer, alpha); // 第二次绘制：叠加提亮
    // 还原默认混合模式：纹理状态若残留 ADD，会污染后续普通绘制
    SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_BLEND);
}
