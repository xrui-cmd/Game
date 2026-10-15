#include "game/Monster.h"
#include <algorithm> // std::clamp / std::max：移动边界、出生范围与闪白计时的上下界
#include <cmath>     // std::sqrt：归一化方向与距离计算

// 本编译单元私有的变体数值表与工具（不对外暴露）
namespace
{

// 变体规格：外观纹理 + 战斗数值。三变体共用同一套追击 AI，
// 差异只体现在这张表里（换平衡先改这里）
struct VariantSpec
{
    const char* name;       // 变体名（日志展示用）
    const char* sheetPath;  // 精灵表相对路径（4 帧横向排列，每帧 48x48）
    const char* singlePath; // 单帧回退相对路径（精灵表缺失时的降级素材）
    int hp;                 // 出生生命值（点）
    float baseSpeed;        // 基础追击速度（像素/秒）：难度成长在此之上叠加
    SDL_Color fallback;     // 程序化回退颜色（两种素材都缺失时的兜底方块色）
};

// 变体数值表（下标 = MonsterType 枚举值，顺序必须与枚举一致）。
// 数值为「代码常量、硬编码并注释」——本批定案不走配置文件：
//   - 史莱姆：慢而脆（生命 20 / 速度 90）
//   - 哥布林：基准线（生命 30 / 速度 110，与旧单怪时代数值一致）
//   - 骷髅  ：快而脆（生命 18 / 速度 140，追逐压力来源）
// 颜色取自各素材的主色系（回退方块与变体观感相近，一眼能认出是哪只）
constexpr VariantSpec kVariants[] = {
    // 史莱姆（swampy）：绿色软泥怪，慢吞吞的低压目标
    { "史莱姆", "assets/monster_slime_sheet.png", "assets/monster_slime.png",
      20, 90.0f, SDL_Color{124, 200, 74, 255} },
    // 哥布林（goblin）：基准变体，数值与旧单怪时代一致
    { "哥布林", "assets/monster_goblin_sheet.png", "assets/monster_goblin.png",
      30, 110.0f, SDL_Color{74, 142, 64, 255} },
    // 骷髅（skelet）：白骨骷髅，快而脆
    { "骷髅", "assets/monster_skelet_sheet.png", "assets/monster_skelet.png",
      18, 140.0f, SDL_Color{230, 228, 210, 255} },
};

// 变体枚举 -> 数值表下标（Count 哨兵值不在表内，做一个防御性夹取）
int variantIndex(MonsterType type)
{
    // 枚举值直接作为下标使用；越界（未来加枚举忘加表项）夹到最后一项，
    // 宁可外观/数值不符也绝不越界读表
    return std::clamp(static_cast<int>(type), 0, static_cast<int>(MonsterType::Count) - 1);
}

} // 匿名命名空间结束

// 变体名查询：日志展示用（「怪物出现（第 1 只，骷髅）」这类文案）
const char* monsterTypeName(MonsterType type)
{
    // 查表返回变体名；越界情形由 variantIndex 夹取保护（见上）
    return kVariants[variantIndex(type)].name;
}

// 按指定变体加载/重载纹理
bool Monster::loadVariant(SDL_Renderer* renderer, MonsterType type)
{
    // 记录变体类型：baseSpeed()/spawnEdge 的出生生命都从它查表
    m_type = type;

    // 取出本变体的规格（路径、帧参数外的数值、回退色）
    const VariantSpec& spec = kVariants[variantIndex(type)];

    // 换皮前先释放旧纹理（重点）：loadTextureWithFallback 每次都会新建纹理
    // 并覆写 m_texture——不先释放的话，对象池槽位反复换变体时旧纹理的显存
    // 会持续泄漏。release 幂等，首次加载（m_texture 为空）调用也无害
    release();

    // 三级回退加载本变体素材：精灵表（4 帧）-> 单帧图 -> 程序化纯色
    return loadTextureWithFallback(renderer,
                                   spec.sheetPath,     // 首选：本变体精灵表相对路径
                                   spec.singlePath,    // 次选：本变体单帧精灵相对路径
                                   kFrameCount,        // 精灵表帧数：4 帧
                                   kDefaultFrameTime,  // 每帧时长：默认 0.2 秒
                                   spec.fallback);     // 回退颜色：变体主色系方块
}

// 本变体的基础追击速度（像素/秒）：难度成长在其上叠加（由场景完成）
float Monster::baseSpeed() const
{
    // 查表返回基础速度；越界情形由 variantIndex 夹取保护
    return kVariants[variantIndex(m_type)].baseSpeed;
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

// 生成（边缘刷新）：窗口四边随机一边的内侧落位，激活并按变体写入生命值
void Monster::spawnEdge(int winW, int winH, const SDL_FRect& avoidRect)
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

    // 激活 + 按变体数值表写入战斗数值：出生生命取自当前变体
    //（stale 防护：至少 1 点——表内数值恒为正，这里的 max 是防未来改表的兜底）
    m_alive = true;                                            // 本对象进入战场：更新/渲染/碰撞自此生效
    m_hp = std::max(kVariants[variantIndex(m_type)].hp, 1);    // 出生生命（变体表；0 血出生会在首帧判定为已死）
    m_flashLeft = 0.0f;                                        // 新生怪物不带受击光效
    m_hasTarget = false;                                       // 追踪目标等场景下个固定步注入

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
    // 跑动/蠕动动画在追踪全程持续播放
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
    // 速度由 PlayScene 按「变体基础速度 + 难度成长」注入
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
    // 整体提亮，各变体本体色被推亮成偏白的高光（史莱姆的绿、骷髅的白骨
    // 都会明显「发光」一下）。
    // 为什么不用颜色调制（SDL_SetTextureColorMod）：乘法调制只能压暗，
    // 无法把本体「提亮成白光」；叠加混合才能做出闪光效果
    SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_ADD);
    Entity::render(renderer, alpha); // 第二次绘制：叠加提亮
    // 还原默认混合模式：纹理状态若残留 ADD，会污染后续普通绘制
    SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_BLEND);
}
