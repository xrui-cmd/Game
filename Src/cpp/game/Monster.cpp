#include "game/Monster.h"
#include "game/MonsterLevel.h" // scaledMonsterHp / monsterCoinDrop：等级数值缩放（批六丙纯逻辑）
#include "game/Zone.h"      // ZoneId / zoneAtWorld：生成分表按出生点所在区选变体（批六丁）
#include "game/World.h"     // World：移动的分轴碰撞滑动（slideMove）与落点可走查询
#include "engine/Camera.h"  // Camera：渲染时「世界坐标 -> 屏幕坐标」的换算
#include "engine/Rect.h"    // rectsOverlap：攻击接触范围（碰撞盒 + 余量）的 AABB 判定
#include <algorithm> // std::clamp / std::max：红染计时、滑移权重与战斗数值的上下界
#include <cmath>     // std::sqrt：归一化方向、距离与侧滑权重的计算

// 本编译单元私有的变体数值表与工具（不对外暴露）
namespace
{

// AI 随机源适配（批三）：MonsterAi 只认 [0,1) 的函数指针接口（纯逻辑
// 不依赖 SDL），这里把 SDL_randf 包一层交给它——游戏内随机、单测内可注入
// 固定序列，随机源的可控性由此而来
float monsterAiRandom(void* /*context*/)
{
    return SDL_randf(); // 复用 SDL 全局随机（与刷新/出生取点同一随机流）
}

// 变体规格：外观纹理 + 战斗数值 + 击杀掉落。三变体共用同一套 AI，
// 差异只体现在这张表里（换平衡先改这里）
struct VariantSpec
{
    const char* name;       // 变体名（日志展示用）
    const char* sheetPath;  // 精灵表相对路径（4 帧横向排列，每帧 48x48）
    const char* singlePath; // 单帧回退相对路径（精灵表缺失时的降级素材）
    int hp;                 // 出生生命值（点）
    float baseSpeed;        // 基础追击速度（像素/秒）：难度成长在此之上叠加
    int coinDrop;           // 击杀掉落金币数（枚）——局内货币的经济常量
    SDL_Color fallback;     // 程序化回退颜色（两种素材都缺失时的兜底方块色）
};

// 变体数值表（下标 = MonsterType 枚举值，顺序必须与枚举一致）。
// 数值为「代码常量、硬编码并注释」——本批定案不走配置文件：
//   - 史莱姆系：慢而脆（生命 20 / 速度 90 / 掉落金币 1）
//   - 哥布林系：基准线（生命 30 / 速度 110 / 掉落金币 2）
//   - 骷髅系  ：快而脆（生命 18 / 速度 140 / 掉落金币 1，追逐压力来源）
// 雪山三型 = 同族原色版的冰霜重染色外观（compose_sprites.ps1 产线），
// 数值完全沿用原色版——差异只在进攻模组附加的元素效果（AttackModule）。
// 颜色取自各素材的主色系（回退方块与变体观感相近，一眼能认出是哪只）
constexpr VariantSpec kVariants[] = {
    // 史莱姆（swampy）：绿色软泥怪，慢吞吞的低压目标（赏金 1 枚）
    { "史莱姆", "assets/monster_slime_sheet.png", "assets/monster_slime.png",
      20, 90.0f, 1, SDL_Color{124, 200, 74, 255} },
    // 哥布林（goblin）：基准变体，数值与旧单怪时代一致（赏金 2 枚——最「值钱」）
    { "哥布林", "assets/monster_goblin_sheet.png", "assets/monster_goblin.png",
      30, 110.0f, 2, SDL_Color{74, 142, 64, 255} },
    // 骷髅（skelet）：白骨骷髅，快而脆（赏金 1 枚）
    { "骷髅", "assets/monster_skelet_sheet.png", "assets/monster_skelet.png",
      18, 140.0f, 1, SDL_Color{230, 228, 210, 255} },
    // 冰史莱姆（swampy 冰霜重染色）：雪山低级目标；数值同史莱姆，命中减速
    { "冰史莱姆", "assets/monster_ice_slime_sheet.png", "assets/monster_ice_slime.png",
      20, 90.0f, 1, SDL_Color{118, 190, 232, 255} },
    // 霜哥布林（goblin 冰霜重染色）：雪山基准目标；数值同哥布林，命中 +25 冻结条
    { "霜哥布林", "assets/monster_frost_goblin_sheet.png", "assets/monster_frost_goblin.png",
      30, 110.0f, 2, SDL_Color{80, 154, 170, 255} },
    // 霜骷髅（skelet 冰霜重染色）：雪山快速目标；数值同骷髅，命中 +20 冻结条
    { "霜骷髅", "assets/monster_frost_skelet_sheet.png", "assets/monster_frost_skelet.png",
      18, 140.0f, 1, SDL_Color{208, 230, 244, 255} },
};

// 变体枚举 -> 数值表下标（Count 哨兵值不在表内，做一个防御性夹取）
int variantIndex(MonsterType type)
{
    // 枚举值直接作为下标使用；越界（未来加枚举忘加表项）夹到最后一项，
    // 宁可外观/数值不符也绝不越界读表
    return std::clamp(static_cast<int>(type), 0, static_cast<int>(MonsterType::Count) - 1);
}

// 前扑「快出慢收」缓出映射（批五A）：把归一化进度 p ∈ [0,1] 映射成
// 位移分配位置 f(p) = 1 - (1-p)²——
//   f'(0) = 2：出手速度为平均速度的 2 倍（规格「快出」的数学表达）；
//   f'(1) = 0：收势速度归零，冲到位时没有急刹感。
// 与 AttackArc 的挥砍缓出（1-(1-t)²）同族——全项目运动观感一致；
// 边界防御：非正（含 NaN）按 0、达到/超过 1 按 1（夹到端点，绝不外溢）
float easeOutLunge(float p)
{
    // 非正与 NaN 防御：!(p > 0) 一并接住 0、负值与 NaN（NaN 比较恒假）
    if (!(p > 0.0f))
    {
        return 0.0f; // 未开始：停在起点
    }
    // 达到/超过 1（含 +inf）：停在终点（冲量已全部释放）
    if (p >= 1.0f)
    {
        return 1.0f; // 已完成：位移分配到位
    }
    // 二次缓出主体：1 - (1-p)² = 2p - p²，前快后慢
    const float inv = 1.0f - p; // 剩余进度（1-p）
    return 1.0f - inv * inv;    // 缓出后的位移分配比例
}

// 跳跃位移的平滑缓动（批六丁）：smoothstep 3p²-2p³——
// 起跳与落地速度都归零，配合正弦纵向弧线读作「蹲 -> 跃 -> 落」。
// 边界防御与 easeOutLunge 同款：非正按 0、达到/超过 1 按 1。
float smoothJump(float p)
{
    if (!(p > 0.0f)) // 未开始（含 NaN）：停在起点
    {
        return 0.0f; // 位移分配为 0
    }
    if (p >= 1.0f) // 已完成：停在终点
    {
        return 1.0f; // 位移分配为满
    }
    return p * p * (3.0f - 2.0f * p); // smoothstep 曲线
}

} // 匿名命名空间结束

// 变体名查询：日志展示用（「怪物出现（第 1 只，骷髅）」这类文案）
const char* monsterTypeName(MonsterType type)
{
    // 查表返回变体名；越界情形由 variantIndex 夹取保护（见上）
    return kVariants[variantIndex(type)].name;
}

// 生成分表（批六丁）：草地原色三型 / 雪山冰霜三型，区内等权。
// roll 由调用方以 SDL_rand(3) 等权产生；越界夹到 0..2（防御）。
MonsterType monsterVariantForZone(ZoneId zone, int roll)
{
    const int safeRoll = std::clamp(roll, 0, 2); // roll 夹到三型范围
    if (zone == ZoneId::Snow) // 雪山：冰霜版三型
    {
        static constexpr MonsterType kSnowVariants[3] = {
            MonsterType::IceSlime,    // 0 冰史莱姆
            MonsterType::FrostGoblin, // 1 霜哥布林
            MonsterType::FrostSkelet, // 2 霜骷髅
        };
        return kSnowVariants[safeRoll]; // 按等权下标取雪山变体
    }
    // 草地（含未判定哨兵/世界外兜底）：原色三型
    static constexpr MonsterType kGrassVariants[3] = {
        MonsterType::Slime,  // 0 史莱姆
        MonsterType::Goblin, // 1 哥布林
        MonsterType::Skelet, // 2 骷髅
    };
    return kGrassVariants[safeRoll]; // 按等权下标取草地变体
}

// 按指定变体加载/重载纹理
bool Monster::loadVariant(SDL_Renderer* renderer, MonsterType type)
{
    // 记录变体类型：baseSpeed()/spawnAt 的出生生命都从它查表
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

// 击杀金币掉落数（枚）：变体基础 + 等级加成 floor((lv-1)/10)（批六丙）。
// 场景在击杀结算时读取（史莱姆 1 / 哥布林 2 / 骷髅 1 —— 1~10 级为原值）
int Monster::coinDrop() const
{
    // 查表取变体基础枚数；越界情形由 variantIndex 夹取保护
    const int baseCoins = kVariants[variantIndex(m_type)].coinDrop;
    // 按当前等级套金币公式（纯函数：每满 10 级 +1 枚）
    return monsterCoinDrop(baseCoins, m_level);
}

// 设置追踪目标：存完整目标盒（接触范围判定用）与中心点（AI 追迹用）。
// 批四起场景传入的是「玩家碰撞盒」——AI 距离、攻击距离与移动判定同一口径
void Monster::setTarget(const SDL_FRect& targetRect)
{
    m_targetBox = targetRect;                       // 完整盒留存：接触范围判定的空间基准
    m_targetX = targetRect.x + targetRect.w * 0.5f; // 目标中心 x：矩形左上角 + 半宽
    m_targetY = targetRect.y + targetRect.h * 0.5f; // 目标中心 y：矩形左上角 + 半高
    m_hasTarget = true;                             // 标记玩家可见：AI 开始做仇恨判定
}

// 施加减速（批五B：冰霜新星入口）：契约见头文件。乘区与时长都被本次
// 施放覆盖（后施放为准——与「连续受击全部状态重新计时」同款口径）
void Monster::applySlow(float factor, float seconds)
{
    // 非正时长（调用方错误/0 秒）：忽略并警告——不产生任何状态
    if (seconds <= 0.0f)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "减速时长必须为正（收到 %.2f），忽略本次减速", seconds);
        return;
    }

    // 乘区夹到 [0,1]：0 = 完全冻结（显式语义）；>1 是「加速」误用（夹回 1）；
    // 负值等价 0（防御——异常输入不能反过来加速怪物）
    float clamped = (factor > 0.0f) ? factor : 0.0f; // 负值压到 0
    if (clamped > 1.0f)
    {
        clamped = 1.0f; // 上界：减速不变成加速
    }

    m_slowFactor = clamped; // 乘区写入（覆盖前值）
    m_slowLeft = seconds;   // 时长写入（覆盖前值）

    // 冒烟观测点：减速施加（含变体名与时长——冒烟断言「冰霜新星命中即减速」）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "冰霜减速（%s：速度 x%.2f，持续 %.1f 秒）",
                monsterTypeName(m_type), m_slowFactor, m_slowLeft);
}

// 受击：扣血 + 红染；返回是否死亡（生命归零）
bool Monster::takeDamage(int amount)
{
    // 负数伤害是调用方错误（等价于治疗且逻辑不通）：警告并忽略，状态不变
    if (amount < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "怪物受击伤害不能为负（收到 %d），忽略本次受击", amount);
        return false; // 未死亡（也没有扣血）
    }

    // 扣血并把生命夹到 0：日志/血条永远看不到负数生命
    m_hp = std::max(m_hp - amount, 0);

    // 受击红染计时重置为满值（批五A）：连续受击时红光从本次受击重新
    // 起算（视觉连续）；旧版白闪（ADD 全白提亮）已退役——玩家/怪物
    // 统一为「红染」受击语言
    m_hitTintLeft = kHitTintSeconds;

    // 生命归零：转入「未激活」——不再更新/渲染/碰撞，槽位等 spawnAt 复用
    if (m_hp == 0)
    {
        m_alive = false;     // 本对象退场（调用方负责击杀计数与掉落结算）
        m_hasTarget = false; // 清追踪目标：复用前不残留旧目标点
        return true;         // 通知调用方：本次受击致死，走死亡结算分支
    }

    return false; // 仍有生命：调用方通常接着施加击退
}

// 生成（批三正式版；批六丙加等级）：在调用方选好的可信落点上激活本对象
void Monster::spawnAt(float centerX, float centerY, int level)
{
    // 落点换算：入参是「怪物中心」的世界坐标，转成左上角存内部
    //（位置成员一直是左上角语义——与 rect()/渲染同一口径）
    m_x = centerX - m_w * 0.5f; // 左上角 x = 中心 - 半宽
    m_y = centerY - m_h * 0.5f; // 左上角 y = 中心 - 半高

    // 等级：出生时定稿并伴随本对象（批六丙）；至少 1 级（调用方/改表防御）
    m_level = std::max(1, level); // 当前怪物等级（血条显示/伤害/经验/金币共用）

    // 激活 + 按变体数值表 x 等级缩放写入战斗数值：出生生命/上限都取自
    // 当前变体，再乘等级系数（生命 x(1+10%/级)，见 MonsterLevel.h）
    const VariantSpec& spec = kVariants[variantIndex(m_type)]; // 本变体规格
    m_alive = true;                     // 本对象进入战场：更新/渲染/碰撞自此生效
    m_maxHp = scaledMonsterHp(spec.hp, m_level); // 生命上限（等级 1 = 基础值）
    m_hp = m_maxHp;                     // 出生满血（0 血出生会在首帧判定为已死）
    m_hitTintLeft = 0.0f;               // 新生怪物不带受击红染
    m_hasTarget = false;                // 玩家目标等场景下个固定步注入

    // AI 复位（批三）：随机源就位 -> 巢 = 出生点（出生点即巢）-> 状态全量
    // 复位（巡逻、无游荡点、无停顿）——池槽位复用时旧怪物的仇恨/回巢
    // 状态绝不带进新怪物
    m_ai.setRandom(&monsterAiRandom, nullptr); // 随机源：SDL 随机流适配
    m_ai.reset(centerX, centerY);              // 巢 = 本落点；状态清回巡逻
    clearMotionState();                        // 侧滑/卡死检测窗口清零（新怪的干净起点）
    clearSlowState();                          // 减速乘区清零（批五B：池复用不带旧减速）
    clearAttackState();                        // 攻击状态清零（批四：池复用不带旧出招）
    clearPlayerBlocker();                      // 玩家盒实心清零（批四：等场景本步重新注入）

    // 瞬移类操作的插值收尾（重点）：出生是「瞬间换位」，必须把插值快照
    // 对齐到新落点，否则渲染会从上一位置向新落点拉丝（延续既有纪律）
    //（落点的「激活区块内 + 可走 + 离玩家 >= 500px」校验由调用方完成）
    snapshotPreviousPosition();
}

// 被撞开/被击退：从 (fromX, fromY)（伤害来源中心）指向怪物中心的方向
// 瞬移 distance 像素（落点经世界可走校验，逐级缩短重试）
void Monster::knockback(float fromX, float fromY, float distance)
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

    // 击退是瞬移：位置突变后旧位置的卡死统计/侧滑方向不再有意义——清零。
    //（清在位移之前：各分支（含世界未注入的防御分支）都已受益）
    clearMotionState();

    // 世界未注入（防御）：退化为无校验位移——直接按全距离瞬移
    if (m_world == nullptr)
    {
        m_x = centerX + dirX * distance - m_w * 0.5f; // 左上角 x = 新中心 - 半宽
        m_y = centerY + dirY * distance - m_h * 0.5f; // 左上角 y = 新中心 - 半高
        snapshotPreviousPosition();                    // 瞬移收尾：快照对齐防拉丝
        return;
    }

    // 落点校验（批二落地、批三沿用；批四改碰撞盒口径）：从全距离开始，
    // 被墙挡则按 1/2、1/4 逐级缩短重试。「贴墙可接受」的口径下不允许
    // 脚盒推进墙内——否则怪物会卡在墙里两轴都出不来（而贴墙边站位是
    // 允许的：碰撞盒与墙格相交才被拒；精灵身体叠压墙脚属正常画面）
    for (float factor = 1.0f; factor >= 0.25f; factor *= 0.5f)
    {
        // 候选落点（精灵左上角坐标）：中心 + 方向 x 距离 x 比例 - 半尺寸
        const float candidateX = centerX + dirX * distance * factor - m_w * 0.5f;
        const float candidateY = centerY + dirY * distance * factor - m_h * 0.5f;

        // 候选可走：采用并结束——候选盒 = 精灵左上角代入的脚部碰撞盒
        //（collisionRectAt 与移动判定同一条偏移换算，口径一致）
        const SDL_FRect candidate = collisionRectAt(candidateX, candidateY);
        if (m_world->isRectWalkable(candidate))
        {
            m_x = candidateX; // 采用候选落点 x
            m_y = candidateY; // 采用候选落点 y
            // 瞬移类操作的插值收尾：落点定稿后快照 prev=cur，渲染不拉丝
            snapshotPreviousPosition();
            return;
        }
    }

    // 全距离档位都不可走（贴墙死角/被包围）：原地不动——
    // 比「卡进墙里出不来」好；位置未变，插值区间保持原样（无需快照）
}

// ============================================================
// 攻击（批六丁·六型模组）：状态机与结算
// ============================================================

// 设置攻击冷却：配置驱动的入口（对应 monster_attack_cooldown，默认 1.2s）
void Monster::setAttackCooldown(float seconds)
{
    // 负冷却无意义（会让冷却剩余出现反向拉扯）：忽略并警告，保留原值；
    // 0 允许（无冷却——连续出招的显式调试语义）
    if (seconds < 0.0f)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "怪物攻击冷却不能为负数（收到 %f），忽略该配置", seconds);
        return;
    }

    m_attackCooldown = seconds; // 覆盖冷却总时长：其后的出招使用新值
}

// 消费攻击结算结果：返回并立即清除——每次 Hit/Miss 只发布一次
MonsterAttackResult Monster::consumeAttackResult()
{
    const MonsterAttackResult result = m_attackResult; // 取走待消费结果
    m_attackResult = MonsterAttackResult::None;        // 清除：杜绝重复结算
    return result;                                     // 交场景做伤害/日志/回弹
}

// 出招开始：朝玩家一次性锁定方向、启动蓄力计时与染色脉冲、打出招日志
void Monster::beginWindup()
{
    // 方向锁定在「出招瞬间」：蓄力期间玩家移动 = 躲避窗口——前扑朝旧
    // 位置方向、躲开即落空（规格「给玩家躲避窗口」的实现口径，报告注明）
    const float centerX = m_x + m_w * 0.5f; // 自身中心 x（世界坐标）
    const float centerY = m_y + m_h * 0.5f; // 自身中心 y
    const float dx = m_targetX - centerX;   // 指向玩家中心的向量 x
    const float dy = m_targetY - centerY;   // 指向玩家中心的向量 y
    const float length = std::sqrt(dx * dx + dy * dy); // 方向长度

    // 归一化；长度过小（完全重合等退化情形）退回默认向右——确定性方向，
    // 绝不产生 NaN（与击退/挥砍等既有方向零向量防御同一口径）
    if (length > kAttackDirEpsilon)
    {
        m_attackDirX = dx / length; // 单位方向 x：占全长的比例
        m_attackDirY = dy / length; // 单位方向 y
    }
    else
    {
        m_attackDirX = 1.0f; // 默认向右（确定性，便于观察与复现）
        m_attackDirY = 0.0f; // 水平方向
    }

    // 本次出招的模组快照（蓄力期间类型不会变；三型参数全部来自数据表）
    const AttackModule& module = attackModule(); // 当前变体的进攻模组
    m_attackKind = module.kind;                  // 攻击方式（跳跃/前扑/突刺）
    m_attackPhase = AttackPhase::Windup;         // 进入蓄力（本步起常规移动被接管）
    m_attackPhaseLeft = module.windupSeconds;    // 蓄力倒计时（模组数据驱动）
    m_strikeMoved = 0.0f;                        // 攻击位移进度清零（进入位移后按曲线分配）
    clearLungeGhosts();                          // 清旧残影（连续出招时不留上一轮的影）
    m_attackPulseLeft = kAttackPulseSeconds;     // 出招瞬间染色脉冲（0.1s）
    m_landingFxLeft = 0.0f;                      // 新动作不带上一轮的落地冰环

    // 冒烟观测点：出招日志（追加模组名——六型攻击的区分依据）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "怪物出招（%s：%s）",
                monsterTypeName(m_type), module.name);
}

// 玩家是否处于接触范围（双方碰撞盒触接 + 小余量）：出招与命中共用判据
bool Monster::playerInContactRange() const
{
    // 玩家目标未注入（防御路径）：视为不在范围（不可能出招）
    if (!m_hasTarget)
    {
        return false;
    }

    // 判定 = 「玩家碰撞盒外扩余量」与「自身碰撞盒」的 AABB 相交：
    // 外扩把「触接（恰好贴合，rectsOverlap 约定不算相交）」也纳入范围，
    // 再放宽 kAttackContactMargin（6px）——「接触范围 = 触接 + 小余量」
    const SDL_FRect expanded{ m_targetBox.x - kAttackContactMargin,        // 左边界外扩
                              m_targetBox.y - kAttackContactMargin,        // 上边界外扩
                              m_targetBox.w + kAttackContactMargin * 2.0f, // 宽：两侧各扩
                              m_targetBox.h + kAttackContactMargin * 2.0f }; // 高：上下各扩
    return rectsOverlap(expanded, collisionRect()); // 与自身脚盒相交 = 在范围内
}

// 玩家是否处于「落点圆形范围」（跳跃砸击的 40px 判定）：
// 圆心 = 怪物中心，半径 = radius；与玩家碰撞盒最近点比较（含边界）。
bool Monster::playerInHitRadius(float radius) const
{
    if (!m_hasTarget || radius <= 0.0f) // 无目标/非法半径（防御）：不命中
    {
        return false; // 明确视为范围外
    }
    const float centerX = m_x + m_w * 0.5f; // 怪物中心 x（世界坐标）
    const float centerY = m_y + m_h * 0.5f; // 怪物中心 y
    // 玩家碰撞盒上离怪物中心最近的点（逐轴夹取到盒区间）
    const float nearestX = std::clamp(centerX, m_targetBox.x,
                                      m_targetBox.x + m_targetBox.w); // 最近点 x
    const float nearestY = std::clamp(centerY, m_targetBox.y,
                                      m_targetBox.y + m_targetBox.h); // 最近点 y
    const float dx = centerX - nearestX; // 圆心到最近点的横向差
    const float dy = centerY - nearestY; // 纵向差
    return dx * dx + dy * dy <= radius * radius; // 平方比较（含边界）
}

// 跳跃弧线纵向偏移：仅「跳跃位移阶段」返回负值（屏幕坐标向上为负）。
// sin(pi x 进度) 在起跳/落地为 0、中途为峰值——只看画面，不参与判定。
float Monster::jumpVisualOffsetY() const
{
    if (m_attackPhase != AttackPhase::Strike || m_attackKind != AttackKind::JumpSmash)
    {
        return 0.0f; // 非跳跃位移：没有弧线偏移
    }
    const float duration = attackModule().duration; // 模组位移时长（秒）
    if (!(duration > 0.0f)) // 异常模组（防御）：不加偏移
    {
        return 0.0f; // 保持地面位置
    }
    const float progress = 1.0f - std::max(m_attackPhaseLeft, 0.0f) / duration; // 进度 [0,1]
    constexpr float kPi = 3.14159265358979323846f; // 圆周率（避免平台 M_PI 差异）
    return -std::sin(progress * kPi) * kJumpArcHeight; // 向上为负：抛物线视觉
}

// 按当前动作的视觉姿态绘制本体：普通状态与 Entity::render 同帧同插值；
// 跳跃蓄力做深蹲压扁（底边锚定）、跳跃位移叠加纵向弧线偏移。
// 前扑/突刺残影不调用本函数（残影保持记录点原样）。
void Monster::drawBody(SDL_Renderer* renderer, float alpha, const Camera& camera,
                       float offsetY, float scaleX, float scaleY) const
{
    if (m_texture == nullptr) // 纹理未加载（防御）：无可绘制内容
    {
        return; // 安全跳过
    }

    const float renderX = m_prevX + (m_x - m_prevX) * alpha; // 插值左上角 x
    const float renderY = m_prevY + (m_y - m_prevY) * alpha + offsetY; // 插值左上角 y + 姿态偏移
    const Vec2 screenPos = camera.worldToScreen(renderX, renderY); // 世界 -> 屏幕（含偏移）

    // 缩放锚点：水平居中、底边保持原位（压扁时脚不离地）
    const float drawW = m_w * scaleX; // 绘制宽
    const float drawH = m_h * scaleY; // 绘制高
    const SDL_FRect dst{ screenPos.x + (m_w - drawW) * 0.5f, // 水平居中
                         screenPos.y + (m_h - drawH),       // 底边锚定
                         drawW, drawH };                     // 绘制尺寸
    const int frameIndex = (m_frameTime > 0.0f) // 帧时长有效才换算帧号
        ? static_cast<int>(m_animTime / m_frameTime) % std::max(m_frameCount, 1) // 循环帧号
        : 0; // 异常帧时长（防御）：停在首帧
    const SDL_FRect src{ static_cast<float>(frameIndex) * m_w, 0.0f, m_w, m_h }; // 精灵表当前帧
    SDL_RenderTexture(renderer, m_texture, &src, &dst); // 绘制本体（单帧裁切）
}

// 攻击状态机单步推进（批六丁：三型共用；差异全部来自 AttackModule）：
//   蓄力 -> 攻击位移（跳跃/前扑/突刺）-> 命中结算 -> 收势 -> 回归 AI。
// 位移统一走 applyAttackMotion（墙 + 玩家盒滑动），不参与卡死检测/侧滑。
void Monster::advanceAttack(float dt)
{
    const AttackModule& module = attackModule(); // 当前变体的模组（参数唯一来源）

    if (m_attackPhase == AttackPhase::Windup)
    {
        // 蓄力：移速归零（常规移动已被本状态接管）。
        // 前扑/突刺沿出招反方向后倾（真实位移，经碰撞滑动；总位移 8px）；
        // 跳跃砸击改为「原地深蹲」——位移为零，视觉压扁在 render 侧完成。
        if (m_attackKind == AttackKind::Lunge || m_attackKind == AttackKind::Thrust)
        {
            const float leanSpeed = kAttackLeanDistance / module.windupSeconds; // 后倾速度（px/s）
            applyAttackMotion(-m_attackDirX * leanSpeed * dt,  // 反向 x 位移
                              -m_attackDirY * leanSpeed * dt); // 反向 y 位移
        }

        m_attackPhaseLeft -= dt; // 蓄力倒计时（固定步递减，暂停时冻结）
        if (m_attackPhaseLeft <= 0.0f)
        {
            m_attackPhase = AttackPhase::Strike;      // 蓄力结束 -> 攻击位移
            m_attackPhaseLeft = module.duration;      // 位移时长（模组数据）
            m_strikeMoved = 0.0f;                     // 位移进度归零（曲线分配起点）
            recordLungeGhost();                       // 第 1 重残影：出招起点站位
        }
        return; // 本步只走蓄力阶段
    }

    if (m_attackPhase == AttackPhase::Strike)
    {
        // ---- 攻击位移：按模组曲线分配预算距离 ----
        // 每步增量 = 曲线在本步末的累计位移 - 上一步末的累计位移。
        // 前扑/突刺用「快出慢收」；跳跃用 smoothstep（起跳/落地都缓）。
        m_attackPhaseLeft -= dt; // 位移倒计时先走（本步按新剩余量换算进度）
        const float remaining = std::max(m_attackPhaseLeft, 0.0f); // 剩余时长（夹到 0）
        const float progress = (module.duration > 0.0f) // 归一化进度（防御除零）
            ? 1.0f - remaining / module.duration // 正常：0 -> 1
            : 1.0f; // 零时长（异常模组）：直接完成
        const float eased = (m_attackKind == AttackKind::JumpSmash) // 跳跃：平滑起落
            ? smoothJump(progress) // smoothstep 曲线
            : easeOutLunge(progress); // 前扑/突刺：快出慢收
        const float targetMoved = module.distance * eased; // 本步末应累计的位移
        const float step = targetMoved - m_strikeMoved;    // 本步位移增量（像素）

        const float beforeX = m_x; // 位移前精灵左上角 x（撞墙判据）
        const float beforeY = m_y; // 位移前精灵左上角 y
        applyAttackMotion(m_attackDirX * step, // 朝锁定方向推进（经碰撞滑动）
                          m_attackDirY * step);
        const float actualStep = std::sqrt((m_x - beforeX) * (m_x - beforeX) +
                                           (m_y - beforeY) * (m_y - beforeY)); // 实际位移长度
        // 进度落定：按「曲线分配量」而非实际位移记——被墙/玩家盒截停时
        // 实际位移更短，但曲线不停（不会突然补冲）
        m_strikeMoved = targetMoved;

        // 第 2 重残影：进度过半时记录在当前位置（位移路径的自然采样点）
        if (progress >= kLungeGhostSample)
        {
            recordLungeGhost(); // 内部自带录满检查：两重后为无操作
        }

        if (m_attackKind == AttackKind::JumpSmash)
        {
            // 跳跃砸击：落地瞬间结算「落点 40px 范围」并进入收势。
            // 冰史莱姆的落地冰环是纯视觉：无论命中与否都亮一圈（规格表现）
            if (m_attackPhaseLeft <= 0.0f)
            {
                if (module.frostVisual) // 冰霜系（冰史莱姆）：落地冰环
                {
                    m_landingFxLeft = kLandingFxSeconds; // 特效计时置满
                }
                // 落点范围判定：玩家碰撞盒最近点距怪物中心 <= hitRadius 即命中
                m_attackResult = playerInHitRadius(module.hitRadius)
                    ? MonsterAttackResult::Hit   // 范围内：命中
                    : MonsterAttackResult::Miss; // 范围外：落空
                m_attackPhase = AttackPhase::Recovery;      // 落地后固定收势
                m_attackPhaseLeft = kAttackRecoverySeconds; // 收势窗口（0.2s）
                m_attackCooldownLeft = m_attackCooldown;    // 出招后进入冷却
            }
            return; // 本步只走跳跃位移
        }

        if (m_attackKind == AttackKind::Thrust)
        {
            // 突刺冲锋：撞墙/命中即止 -> 收势。
            // 命中优先于撞墙（贴着玩家时 applyAttackMotion 被玩家盒截停，
            // 实际位移也会变小；此时应结算 Hit 而不是 Miss）。
            const bool hitNow = playerInContactRange(); // 本步是否已撞上玩家
            const bool wallStopped = step > 0.01f &&
                actualStep < step * kThrustWallStopRatio; // 实际位移远小于期望 = 撞墙/被挡
            if (hitNow || wallStopped || m_attackPhaseLeft <= 0.0f)
            {
                m_attackResult = hitNow
                    ? MonsterAttackResult::Hit   // 命中：立即止步
                    : MonsterAttackResult::Miss; // 撞墙/走完未命中：落空
                m_attackPhase = AttackPhase::Recovery;      // 收势（撞墙/命中即止）
                m_attackPhaseLeft = kAttackRecoverySeconds; // 收势窗口（0.2s）
                m_attackCooldownLeft = m_attackCooldown;    // 出招后进入冷却
            }
            return; // 本步只走突刺位移
        }

        // ---- 前扑（哥布林系，既有行为）：位移走完就地结算 ----
        if (m_attackPhaseLeft <= 0.0f)
        {
            // 结算：玩家是否仍在接触范围（碰撞盒触接 + 余量）
            if (playerInContactRange())
            {
                m_attackResult = MonsterAttackResult::Hit; // 命中：直接回归常规 AI
                m_attackPhase = AttackPhase::None;         // 命中不进收势
            }
            else
            {
                m_attackResult = MonsterAttackResult::Miss;  // 落空：先进收势
                m_attackPhase = AttackPhase::Recovery;       // 扑空惩罚：原地收势
                m_attackPhaseLeft = kAttackRecoverySeconds;  // 收势窗口（0.2s）
            }
            m_attackCooldownLeft = m_attackCooldown; // 出招后进入冷却（无论 Hit/Miss）
        }
        return; // 本步只走前扑阶段
    }

    // ---- Recovery（收势）：原地收招 0.2s 再回追击 ----
    // 移速归零（常规移动被本状态接管）、只走倒计时——落空/落地/撞墙后的
    // 动作停顿，让「躲开」获得可读的代价窗口
    m_attackPhaseLeft -= dt; // 收势倒计时（固定步递减）
    if (m_attackPhaseLeft <= 0.0f)
    {
        m_attackPhase = AttackPhase::None; // 收势结束：回归常规 AI 移动
    }
}

// 攻击动作的短位移出口（蓄力后倾 / 前扑共用）
void Monster::applyAttackMotion(float dx, float dy)
{
    // 世界未注入（防御）：退化为无碰撞位移（与 moveWithCollision 同款降级）
    if (m_world == nullptr)
    {
        m_x += dx; // 直接推进 x
        m_y += dy; // 直接推进 y
        return;
    }

    // 碰撞盒口径的分轴滑动（墙 + 玩家盒；批四·体积碰撞——怪物移动把
    // 玩家盒当实心，撞上滑停、不可重合）：
    // 短促动作不走卡死检测/侧滑——攻击有明确的时长与预算，被挡就等结算
    const SDL_FRect col = collisionRect(); // 本步起点的碰撞盒（世界坐标）
    const SDL_FPoint movedBox = m_world->slideMoveEx(col.x, col.y, col.w, col.h, dx, dy,
                                                     &Monster::playerBlockerCallback, this);
    m_x += movedBox.x - col.x; // 落点折算回精灵左上角（偏移恒定，位移量一致）
    m_y += movedBox.y - col.y; // 同上（y 轴）
}

// slideMoveEx 回调：把「玩家盒」判定接给世界（批四·体积碰撞）。
// context = 怪物自身（静态成员满足 C 函数指针签名，私有状态经 this 访问）
bool Monster::playerBlockerCallback(void* context, const SDL_FRect& rect)
{
    const Monster& self = *static_cast<const Monster*>(context); // 还原怪物自身

    // 未注入玩家盒（防御路径）：没有实心体，任何候选都不被挡
    if (!self.m_hasPlayerBlocker)
    {
        return false;
    }

    // 候选与玩家盒 AABB 相交即挡；恰好贴合不算（可贴着玩家停下）
    return rectsOverlap(rect, self.m_playerBox);
}

// 小幅推动（批四·体积碰撞的兜底与软分离共用）
bool Monster::nudge(float dx, float dy)
{
    // 候选位移表：完整 -> 仅 x -> 仅 y（一侧被挡时保留另一侧的分离量）
    const float candidates[3][2] = {
        { dx, dy },      // 候选 1：完整位移（常规情形一步到位）
        { dx, 0.0f },    // 候选 2：只退 x（y 向被墙/玩家挡死时的退化）
        { 0.0f, dy },    // 候选 3：只退 y（x 向被挡死的退化）
    };

    for (const auto& candidate : candidates)
    {
        // 零位移候选跳过（两个分量都 0 没有推动意义）
        if (candidate[0] == 0.0f && candidate[1] == 0.0f)
        {
            continue;
        }

        // 候选落点（精灵左上角 + 位移）与对应的碰撞盒
        const float newX = m_x + candidate[0]; // 候选精灵左上角 x
        const float newY = m_y + candidate[1]; // 候选精灵左上角 y
        const SDL_FRect box = collisionRectAt(newX, newY); // 候选脚盒（世界坐标）

        // 墙校验（无世界时跳过——防御：无墙世界不过滤）
        if (m_world != nullptr && !m_world->isRectWalkable(box))
        {
            continue; // 该候选会进墙：换下一个
        }
        // 玩家盒校验（已注入时）：不把怪物推入玩家——重叠候选不可用。
        // 完整分离量由 computeSeparation 保证「推出后恰好留缝」，正常通过；
        // 此处拒绝只发生在「分离方向同时被墙挤压」的死角情形
        if (m_hasPlayerBlocker && rectsOverlap(box, m_playerBox))
        {
            continue; // 该候选会把怪物压进玩家：换下一个
        }

        m_x = newX; // 采用候选落定（直接写位置：prev 快照保持，插值照常平滑）
        m_y = newY;
        return true; // 推动成功
    }

    return false; // 三个候选全被挡：原地不动（下一帧再试——绝不推动玩家）
}

// 覆写更新：AI 状态决策 + 帧动画 + 受击红染/残影计时 + 朝目标点移动
//（移动经世界碰撞）。分工：AI（MonsterAi）只做状态/目标决策；本函数把
// 决策执行成位移，并在状态切换/到巢事件上打日志、结回血——「场景注入、
// 实体执行」的延续
void Monster::update(const UpdateContext& ctx)
{
    // 第一行固定动作：快照「本步开始前的位置」作为渲染插值起点。
    // 覆写 update 会跳过 Entity::update，这条调用必须自己补上
    snapshotPreviousPosition();

    // 帧动画无条件推进（基类默认实现的同款语句）：移动与否不影响动画节奏，
    // 跑动/蠕动动画在巡逻/追击全程持续播放
    m_animTime += ctx.deltaTime;

    // 受击红染计时递减并夹到 0（批五A）：与固定步长同源，暂停时随
    // update 门控冻结（红光相位一并冻结——画面静止承诺不变）
    if (m_hitTintLeft > 0.0f)
    {
        m_hitTintLeft = std::max(m_hitTintLeft - ctx.deltaTime, 0.0f);
    }

    // 减速剩余时长递减并夹到 0（批五B：冰霜新星）：与红染同款固定步
    // 递减——暂停/非激活区冻结时一并冻结（乘区在移动速度计算处消费）
    if (m_slowLeft > 0.0f)
    {
        m_slowLeft = std::max(m_slowLeft - ctx.deltaTime, 0.0f);
    }

    // 前扑残影寿命递减并夹到 0（批五A）：残影在前扑后自然渐隐熄灭；
    // 非激活区怪物的 update 冻结时残影一并冻结（与其余计时同一门控）
    for (LungeGhost& ghost : m_lungeGhosts)
    {
        if (ghost.left > 0.0f)
        {
            ghost.left = std::max(ghost.left - ctx.deltaTime, 0.0f);
        }
    }

    // 出招染色脉冲计时递减（批四）：与红染同款固定步递减——脉冲只亮
    // 出招瞬间的 0.1s，之后自然熄灭（render 据此决定是否叠加偏红）
    if (m_attackPulseLeft > 0.0f)
    {
        m_attackPulseLeft = std::max(m_attackPulseLeft - ctx.deltaTime, 0.0f);
    }

    // 落地冰环计时递减（批六丁）：与其余战斗特效用同一条固定步门控
    if (m_landingFxLeft > 0.0f)
    {
        m_landingFxLeft = std::max(m_landingFxLeft - ctx.deltaTime, 0.0f); // 夹到 0
    }

    // 怪物中心（世界坐标）：AI 判定的自身位置基准
    const float centerX = m_x + m_w * 0.5f; // 中心 x
    const float centerY = m_y + m_h * 0.5f; // 中心 y

    // ---- AI 状态决策（纯逻辑）：巡逻/仇恨/栓绳回巢/到巢 ----
    // 玩家位置 = 场景每步注入的追踪目标（m_hasTarget=false 时 AI 视为
    // 玩家不可见——不仇恨；防御路径，正常流程每步都会注入）
    const MonsterAi::StepResult ai = m_ai.update(centerX, centerY,
                                                 m_hasTarget, m_targetX, m_targetY,
                                                 ctx.deltaTime);

    // ---- 状态事件日志（批三冒烟断言点，含变体名）----
    if (ai.enteredChase)
    {
        // 冒烟观测点：进入仇恨（自动化测试据此断言「靠近才追」）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "进入仇恨（%s）", monsterTypeName(m_type));
    }
    if (ai.enteredReturn)
    {
        // 冒烟观测点：触发栓绳回巢（自动化测试据此断言「拉怪过线即返」）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "触发栓绳回巢（%s）", monsterTypeName(m_type));
    }
    if (ai.returnedHome)
    {
        // 到巢事件（冒烟观测点）：先按注入的百分比结算回血（0=关闭），
        // 再打「回到巢穴」日志；真回血了补一条「回巢回血」明细日志
        const int hpBefore = m_hp; // 回血前生命（对比日志用）
        m_hp = MonsterAi::healedHpAfterReturn(m_hp, m_maxHp, m_ai.returnHealPercent());
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "回到巢穴（%s）", monsterTypeName(m_type));
        if (m_hp > hpBefore)
        {
            // 冒烟观测点：回血明细（monster_return_heal_percent>0 时的验证点）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "回巢回血（%s：%d -> %d / %d）",
                        monsterTypeName(m_type), hpBefore, m_hp, m_maxHp);
        }
    }

    // ---- 巡逻点供水（宿主职责）：AI 索要游荡点时，在世界内挑一个
    //「怪物矩形可走」的随机点写回（AI 不知道墙的存在）----
    if (m_ai.wantsPatrolTarget())
    {
        const float nestX = m_ai.nestX(); // 巢心 x：取点圆心
        const float nestY = m_ai.nestY(); // 巢心 y
        const float patrolRadius = m_ai.patrolRadius(); // 巡逻半径（配置注入）
        bool supplied = false; // 是否成功挑到有效游荡点

        for (int attempt = 0; attempt < kPatrolTargetAttempts; ++attempt)
        {
            // 圆内均匀取点：方位角均匀 + 半径取平方根（面积均匀分布——
            // 直接线性取半径会让点集中在圆心附近）
            const float angle = SDL_randf() * 6.283185307179586f; // 随机方位角（弧度）
            const float radius = std::sqrt(SDL_randf()) * patrolRadius; // 均匀半径
            const float targetX = nestX + std::cos(angle) * radius; // 候选点 x
            const float targetY = nestY + std::sin(angle) * radius; // 候选点 y

            // 候选点可走校验（批四改碰撞盒口径）：按「脚部碰撞盒」在候选
            // 落点做可走查询——与移动判定同一口径（精灵可叠压墙脚，脚盒
            // 不许进墙）。无世界（防御路径）直接接受——没有墙就没有校验
            const SDL_FRect candidate = collisionRectAt(targetX - m_w * 0.5f,  // 候选精灵左上角 x
                                                        targetY - m_h * 0.5f); // 候选精灵左上角 y
            if (m_world == nullptr || m_world->isRectWalkable(candidate))
            {
                m_ai.setPatrolTarget(targetX, targetY); // 供水：开始朝它游荡
                supplied = true;                        // 标记成功
                break;                                  // 结束重试
            }
        }

        if (!supplied)
        {
            // 重试耗尽（屋子角落贴满墙等极端情形）：退化为「回巢心」——
            // 巢必可走（出生落点校验保证），不会死循环也不会卡进墙
            m_ai.setPatrolTarget(nestX, nestY);
        }
    }

    // ---- 攻击状态机（批四·蓄力前扑，替换旧「纯接触即伤」）----
    // 冷却推进（仅 Playing 分支能走到这里，暂停随世界冻结）
    if (m_attackCooldownLeft > 0.0f)
    {
        m_attackCooldownLeft = std::max(m_attackCooldownLeft - ctx.deltaTime, 0.0f);
    }

    // 平时（非攻击中）：接触范围 + 冷却就绪 -> 出招（锁定方向、进入蓄力）
    if (m_attackPhase == AttackPhase::None &&
        m_attackCooldownLeft <= 0.0f && playerInContactRange())
    {
        beginWindup(); // 出招：日志 + 染色脉冲 + 朝玩家锁定方向
    }

    // 攻击进行中：推进蓄力/前扑并跳过常规移动——「蓄力移速归零」由
    // 「常规移动被本分支接管」实现（AI 决策已在上方完成，状态日志不缺失）
    if (m_attackPhase != AttackPhase::None)
    {
        advanceAttack(ctx.deltaTime); // 蓄力后倾 -> 前扑前冲 -> Hit/Miss 结算
        return;                       // 本步不再执行常规移动
    }

    // 无移动目标（巡逻停顿/等供水）：原地只播动画（防御性分支）
    if (!m_ai.hasGoal())
    {
        return;
    }

    // 从怪物中心指向目标点（AI 决策的目标：游荡点/玩家/巢心）的向量
    const float dx = m_ai.goalX() - centerX; // 目标向量 x
    const float dy = m_ai.goalY() - centerY; // 目标向量 y
    const float distance = std::sqrt(dx * dx + dy * dy); // 到目标距离

    // 已到达目标附近：原地不动。阈值判定避免目标点附近的来回抖动，
    // 也排除距离为 0 时归一化「除以零」的退化情形
    if (distance <= kArriveEpsilon)
    {
        return;
    }

    // 本步速度：巡逻用「变体基础速度 x 0.5」（不随难度成长——闲逛节奏
    // 与追兵压迫感解耦）；追击/回巢用场景注入的难度速度；
    // 批五B：统一乘以减速乘区（冰霜新星——状态期间巡逻/追击/回巢全减速）
    const float speed = ((m_ai.state() == MonsterAiState::Patrol)
        ? baseSpeed() * kPatrolSpeedFactor // 巡逻速度（慢半拍，肉眼可辨状态）
        : m_chaseSpeed)                    // 追击/回巢速度（难度曲线注入）
        * speedFactor();                   // 减速乘区（无减速恒 1——零行为变化）

    // 沿归一化方向 x 速度的「期望位移」（固定步长积分）。归一化保证
    // 任意方向合速度恒等于本轮速度值
    const float stepDX = (dx / distance) * speed * ctx.deltaTime; // 期望水平位移
    const float stepDY = (dy / distance) * speed * ctx.deltaTime; // 期望垂直位移

    // 位移统一出口：世界碰撞（分轴滑动 + 子步防穿透）+ 卡死检测 + 持久化侧滑
    moveWithCollision(stepDX, stepDY, ctx.deltaTime);
}

// 位移推进（含墙体碰撞 + 卡死检测 + 持久化侧滑）：三种状态共用的移动出口
void Monster::moveWithCollision(float stepDX, float stepDY, float dt)
{
    // 世界未注入（防御分支）：无碰撞直接位移，不做任何检测
    if (m_world == nullptr)
    {
        m_x += stepDX; // 直接推进 x
        m_y += stepDY; // 直接推进 y
        return;        // 本步移动结束
    }

    // ---- 持久化侧滑混合（批三）----
    // 侧滑维持期内：实际尝试方向 = 期望方向 + 垂直方向 x 权重。
    // 权重随时间从 1 缓慢衰减到下限（「维持一段时间并缓慢衰减」），
    // 使怪物坚持从同一侧绕墙，而不是每步重新掷骰子（减少凹角滞留）
    float useDX = stepDX; // 实际尝试的水平位移（默认 = 期望位移）
    float useDY = stepDY; // 实际尝试的垂直位移
    if (m_slideSign != 0 && m_slideHoldLeft > 0.0f)
    {
        const float weight = slideWeight(); // 本步权重（随维持时间衰减）
        // 垂直方向 = 期望位移旋转 90° x 侧滑方向符号：
        // 只旋转不改变长度，把它作为「绕行分量」叠加到期望方向上
        const float perpX = -stepDY * static_cast<float>(m_slideSign); // 垂直分量 x
        const float perpY = stepDX * static_cast<float>(m_slideSign);  // 垂直分量 y
        useDX = stepDX + perpX * weight; // 混合后的水平尝试
        useDY = stepDY + perpY * weight; // 混合后的垂直尝试

        // 长度夹回：混合后合成向量长于原位移，不夹会「斜走加速」——
        // 除零防御：原位移恒不为零（调用方已过滤到达/无目标）
        const float baseLen = std::sqrt(stepDX * stepDX + stepDY * stepDY); // 期望长度
        const float useLen = std::sqrt(useDX * useDX + useDY * useDY);      // 混合长度
        if (useLen > 0.0001f)
        {
            const float scale = baseLen / useLen; // 夹回比例
            useDX *= scale; // 水平位移夹回
            useDY *= scale; // 垂直位移夹回
        }

        // 维持计时递减；到期清方向（下一次卡死会重新探测选取）
        m_slideHoldLeft = std::max(m_slideHoldLeft - dt, 0.0f);
        if (m_slideHoldLeft <= 0.0f)
        {
            m_slideSign = 0; // 维持期结束：不再侧滑，回归直线行进
        }
    }

    // ---- 主方向分轴滑动（与玩家移动共用同一个纯函数，行为一致）----
    // 一轴被墙挡住、另一轴照走——「贴墙滑行」的手感来源；内部还带子步
    // 拆分防高速穿透。批四：输入改用「脚部碰撞盒」——原点与位移量都按
    // 盒口径计算，落点折算回精灵左上角（偏移恒定，位移量一致）；同时
    // 把「玩家盒」作为额外实心体传入——怪物移动撞上玩家滑停、不可重合
    const SDL_FRect col = collisionRect(); // 本步起点的碰撞盒（世界坐标）
    const SDL_FPoint movedBox = m_world->slideMoveEx(col.x, col.y, col.w, col.h,
                                                     useDX, useDY,
                                                     &Monster::playerBlockerCallback, this);
    const float movedDX = movedBox.x - col.x; // 碰撞后的实际水平位移
    const float movedDY = movedBox.y - col.y; // 碰撞后的实际垂直位移

    // 盒落点 -> 精灵左上角语义（侧滑候选与主结果的比较/赋值都按精灵位置）；
    // 盒到精灵的换算 = 减偏移，与 collisionRectAt 的加偏移互为逆运算
    const SDL_FPoint moved{ movedBox.x - kCollisionBoxOffsetX, // 精灵左上角逐点 x
                            movedBox.y - kCollisionBoxOffsetY }; // 精灵左上角 y

    // 期望/实际位移的平方（比较用平方，省一次开方）
    const float expectedSq = useDX * useDX + useDY * useDY;      // 期望位移平方
    const float movedSq = movedDX * movedDX + movedDY * movedDY; // 实际位移平方

    // ---- 卡死检测窗口统计（批三）：累计本窗口的期望/实际位移长度 ----
    m_stuckWindowTime += dt;                        // 窗口计时
    m_stuckWindowExpected += std::sqrt(expectedSq); // 期望位移累计（像素）
    m_stuckWindowActual += std::sqrt(movedSq);      // 实际位移累计（像素）

    // ---- 即时探测（单步几乎没动）：正面撞墙的快速响应 ----
    if (expectedSq > 0.0f && movedSq < expectedSq * kStuckMoveRatioSquared)
    {
        // 本步被挡得很死：立即尝试左右侧滑并择优采用（含持久方向设置）
        applyImmediateSideSlide(useDX, useDY, moved, movedSq);
    }
    else
    {
        // 正常推进（含单轴贴墙滑动的常见情形与侧滑混合路径）。
        // moved 已是「精灵左上角」语义（批四起由盒落点折算）——直接赋值
        m_x = moved.x; // 采用主方向滑动结果 x
        m_y = moved.y; // 采用主方向滑动结果 y
    }

    // ---- 窗口级评估（每 kStuckWindowSeconds 一次）----
    if (m_stuckWindowTime >= kStuckWindowSeconds)
    {
        // 窗口位移比 = 实际累计 / 期望累计（期望为 0 不会进入本函数，
        // 仍做防御：无期望按「自由」处理，避免除零）
        const float ratio = (m_stuckWindowExpected > 0.0f)
            ? (m_stuckWindowActual / m_stuckWindowExpected)
            : 1.0f;

        if (ratio < kStuckWindowRatio)
        {
            // 持续性卡死（凹角滞留）：重探左右两侧并刷新持久化侧滑方向——
            // 上一段维持期选的方向走不动时，这里有机会换边
            refreshSlideSide(stepDX, stepDY);
        }
        else if (ratio >= kFreeWindowRatio)
        {
            // 移动自由：提前结束侧滑（不需要帮忙绕墙，回归直线追击）
            m_slideSign = 0;        // 清方向
            m_slideHoldLeft = 0.0f; // 清维持计时
        }
        // 中间地带（0.3 ~ 0.9）：维持既有侧滑状态不动——贴墙爬行中，
        // 侧滑仍在帮忙；让维持计时自然衰减

        // 窗口清零：下一段重新统计
        m_stuckWindowTime = 0.0f;     // 计时归零
        m_stuckWindowExpected = 0.0f; // 期望累计归零
        m_stuckWindowActual = 0.0f;   // 实际累计归零
    }
}

// 侧滑权重：维持期内从 1 缓慢衰减到 kSlideWeightFloor（随时间线性）
float Monster::slideWeight() const
{
    // 归一化剩余维持时间 [0,1]（0 = 恰好到期；越接近 1 说明刚触发）
    const float t = std::clamp(m_slideHoldLeft / kSlideHoldSeconds, 0.0f, 1.0f);
    // 权重 = 下限 + (1 - 下限) x t：t=1 时为 1（全力侧滑），
    // t=0 时为 0.35（仅保留部分绕行倾向），全线线性衰减
    return kSlideWeightFloor + (1.0f - kSlideWeightFloor) * t;
}

// 计算左右侧滑的候选结果（从当前位置、以期望位移旋转 ±90° 试走；
// 批四起按「脚部碰撞盒」试走，返回值折算回精灵左上角语义）
void Monster::sideSlideCandidates(float stepDX, float stepDY,
                                  SDL_FPoint& rightPoint, float& rightMovedSq,
                                  SDL_FPoint& leftPoint, float& leftMovedSq) const
{
    // 当前位置的碰撞盒（世界坐标）：两侧候选都以它为试走起点
    const SDL_FRect col = collisionRect();

    // 右侧滑（顺时针 90°）：(dx, dy) -> (-dy, dx)；盒落点 -> 精灵左上角。
    // 试走同样带「玩家盒实心」判据（与主方向同一口径：贴玩家滑停）。
    // const 方法里 this 是 const 指针：cast 掉 const 仅为本回调的只读语义
    // 服务（回调不写自身状态，纯比较）
    const SDL_FPoint rightBox = m_world->slideMoveEx(col.x, col.y, col.w, col.h,
                                                     -stepDY, stepDX,
                                                     &Monster::playerBlockerCallback,
                                                     const_cast<Monster*>(this));
    rightPoint = SDL_FPoint{ rightBox.x - kCollisionBoxOffsetX, // 精灵系候选 x
                             rightBox.y - kCollisionBoxOffsetY }; // 精灵系候选 y
    // 左侧滑（逆时针 90°）：(dx, dy) -> (dy, -dx)；同样折算回精灵系
    const SDL_FPoint leftBox = m_world->slideMoveEx(col.x, col.y, col.w, col.h,
                                                    stepDY, -stepDX,
                                                    &Monster::playerBlockerCallback,
                                                    const_cast<Monster*>(this));
    leftPoint = SDL_FPoint{ leftBox.x - kCollisionBoxOffsetX, // 精灵系候选 x
                            leftBox.y - kCollisionBoxOffsetY }; // 精灵系候选 y

    // 各自的实际位移平方：评判「哪一侧更能动」的度量
    const float rightDX = rightPoint.x - m_x; // 右滑实际水平位移
    const float rightDY = rightPoint.y - m_y; // 右滑实际垂直位移
    rightMovedSq = rightDX * rightDX + rightDY * rightDY; // 右滑位移平方
    const float leftDX = leftPoint.x - m_x;   // 左滑实际水平位移
    const float leftDY = leftPoint.y - m_y;   // 左滑实际垂直位移
    leftMovedSq = leftDX * leftDX + leftDY * leftDY;      // 左滑位移平方
}

// 立即应用较优侧滑（单步几乎没动时的即时探测）
void Monster::applyImmediateSideSlide(float stepDX, float stepDY,
                                      const SDL_FPoint& mainPoint, float mainMovedSq)
{
    // 两侧候选（从当前位置试走，纯查询）
    SDL_FPoint rightPoint{};   // 右侧滑候选落点
    float rightMovedSq = 0.0f; // 右侧滑位移平方
    SDL_FPoint leftPoint{};    // 左侧滑候选落点
    float leftMovedSq = 0.0f;  // 左侧滑位移平方
    sideSlideCandidates(stepDX, stepDY, rightPoint, rightMovedSq, leftPoint, leftMovedSq);

    // 选择规则（沿用批二口径）：在「比主方向结果更能动」的候选中取更优者
    if (rightMovedSq > mainMovedSq && rightMovedSq >= leftMovedSq)
    {
        m_x = rightPoint.x; // 采用右侧滑结果 x
        m_y = rightPoint.y; // 采用右侧滑结果 y
        m_slideSign = 1;    // 记录持久化侧滑方向：右侧（顺时针）
        m_slideHoldLeft = kSlideHoldSeconds; // 刷新维持窗口（后续步继续绕）
        return; // 本步处理完毕
    }
    if (leftMovedSq > mainMovedSq)
    {
        m_x = leftPoint.x; // 采用左侧滑结果 x
        m_y = leftPoint.y; // 采用左侧滑结果 y
        m_slideSign = -1;  // 记录持久化侧滑方向：左侧（逆时针）
        m_slideHoldLeft = kSlideHoldSeconds; // 刷新维持窗口
        return; // 本步处理完毕
    }

    // 两侧都不如主方向结果（深凹角死角）：落回主方向结果（基本原地），
    // 但把侧滑方向设为「默认/既有」并刷新维持期——维持期内混合垂直
    // 分量持续尝试，等缺口出现时立即滑出（凹角滞留的软化）
    m_x = mainPoint.x; // 落回主结果 x（截停后的位置）
    m_y = mainPoint.y; // 落回主结果 y
    if (m_slideSign == 0)
    {
        m_slideSign = 1; // 无既有方向：默认取右侧（确定性，便于观察与复现）
    }
    m_slideHoldLeft = kSlideHoldSeconds; // 刷新维持窗口
}

// 窗口级重探（持续性卡死时调用）：重新选定持久化侧滑方向并刷新维持窗口
void Monster::refreshSlideSide(float stepDX, float stepDY)
{
    // 两侧候选（纯查询；本步位置已由主流程落定，这里只更新方向）
    SDL_FPoint rightPoint{};   // 右侧滑候选落点
    float rightMovedSq = 0.0f; // 右侧滑位移平方
    SDL_FPoint leftPoint{};    // 左侧滑候选落点
    float leftMovedSq = 0.0f;  // 左侧滑位移平方
    sideSlideCandidates(stepDX, stepDY, rightPoint, rightMovedSq, leftPoint, leftMovedSq);

    // 侧滑可忽略判据（像素²）：两侧位移都小于它 = 全堵死（凹角底部）
    constexpr float kNegligibleSq = 0.25f; // (0.5px)²——半个像素内视为没动

    if (rightMovedSq <= kNegligibleSq && leftMovedSq <= kNegligibleSq)
    {
        // 全堵死：翻转既有方向再试一段（换边探索）；无方向时取右侧
        m_slideSign = (m_slideSign == 0) ? 1 : -m_slideSign;
    }
    else
    {
        // 有一侧能走：取更能动的一侧（重新判定，可能与上一段不同）
        m_slideSign = (rightMovedSq >= leftMovedSq) ? 1 : -1;
    }
    m_slideHoldLeft = kSlideHoldSeconds; // 刷新维持窗口（新方向又维持一段）
}

// 覆写渲染（批六丁扩展）：残影 -> 落地冰环 -> 冰爪/冰痕 -> 本体
//（跳跃弧线/深蹲）-> 出招脉冲 -> 受击红染 -> 蓄力警示 -> 血条。
// 均经相机世界->屏幕换算；冰霜系视觉只在霜变体上出现。
void Monster::render(SDL_Renderer* renderer, float alpha, const Camera& camera) const
{
    // 跳跃姿态参数：深蹲压扁只在蓄力阶段，弧线偏移只在位移阶段
    float drawScaleX = 1.0f; // 本体绘制宽缩放（普通状态 = 1）
    float drawScaleY = 1.0f; // 本体绘制高缩放（普通状态 = 1）
    if (m_attackPhase == AttackPhase::Windup && m_attackKind == AttackKind::JumpSmash)
    {
        const float windup = attackModule().windupSeconds; // 深蹲总时长（模组）
        const float progress = (windup > 0.0f) // 进度（防御除零）
            ? 1.0f - std::max(m_attackPhaseLeft, 0.0f) / windup // 0 -> 1
            : 1.0f; // 异常模组：直接蹲到底
        drawScaleY = 1.0f - (1.0f - kJumpCrouchScaleY) * progress; // 高度逐渐压扁
        drawScaleX = 1.0f + (kJumpCrouchScaleX - 1.0f) * progress; // 宽度逐渐撑开
    }
    const float jumpOffsetY = jumpVisualOffsetY(); // 跳跃位移的纵向弧线偏移（否则 0）

    // ---- 残影（批五A；批六丁：霜系染成青蓝）----
    // 残影 = 记录位置的半透明副本：透明度随剩余寿命线性衰减；源矩形取第 0 帧
    if (m_texture != nullptr)
    {
        const bool frostGhost = monsterTypeIsFrost(m_type); // 霜系残影：青蓝调
        if (frostGhost) // 只对霜系改色（草地原色保持原样）
        {
            SDL_SetTextureColorMod(m_texture, 160, // 压红：残影偏青
                                   static_cast<Uint8>(kFrozenGhostGreenBlue), // 保留绿
                                   static_cast<Uint8>(kFrozenGhostGreenBlue)); // 提蓝
        }
        bool anyGhost = false; // 本帧是否画过残影（决定是否需要还原纹理状态）
        for (const LungeGhost& ghost : m_lungeGhosts)
        {
            if (ghost.left <= 0.0f) // 已熄灭的残影槽位
            {
                continue; // 跳过
            }
            const Vec2 screen = camera.worldToScreen(ghost.x, ghost.y); // 记录点 -> 屏幕
            const SDL_FRect dst{ screen.x, screen.y, m_w, m_h }; // 整帧矩形（同尺寸）
            const SDL_FRect src{ 0.0f, 0.0f, m_w, m_h };         // 精灵表第 0 帧
            const float ratio = ghost.left / kLungeGhostLifeSeconds; // 剩余寿命比例 [0,1]
            SDL_SetTextureAlphaMod(m_texture, // 渐隐：按剩余寿命调透明度
                                   static_cast<Uint8>(kLungeGhostStartAlpha * ratio)); // 0~115
            SDL_RenderTexture(renderer, m_texture, &src, &dst); // 半透明副本
            anyGhost = true; // 标记：纹理状态已被改写
        }
        if (anyGhost || frostGhost) // 有残影或改过色：必须还原
        {
            SDL_SetTextureAlphaMod(m_texture, 255); // 还原不透明
            SDL_SetTextureColorMod(m_texture, 255, 255, 255); // 还原原色
        }
    }

    // ---- 落地冰环（冰史莱姆）：画在本体之下，从 0.55 倍展开到 1.15 倍并淡出 ----
    if (m_landingFxLeft > 0.0f && m_frostNovaTexture != nullptr)
    {
        const float progress = 1.0f - std::clamp(m_landingFxLeft / kLandingFxSeconds, // 特效进度
                                                 0.0f, 1.0f); // 0 = 刚落地，1 = 结束
        const float scale = kLandingFxStartScale + // 起始缩放
            (kLandingFxEndScale - kLandingFxStartScale) * progress; // 展开到结束缩放
        const float diameter = attackModule().hitRadius * 2.0f * scale; // 绘制直径（相对 40px 判定半径）
        const float interpX = m_prevX + (m_x - m_prevX) * alpha; // 插值左上角 x
        const float interpY = m_prevY + (m_y - m_prevY) * alpha; // 插值左上角 y
        const Vec2 screen = camera.worldToScreen(interpX + m_w * 0.5f, // 落点中心 x
                                                 interpY + m_h * 0.5f); // 落点中心 y
        const SDL_FRect dst{ screen.x - diameter * 0.5f, // 左上角 x（居中）
                             screen.y - diameter * 0.5f, // 左上角 y
                             diameter, diameter };       // 冰环绘制尺寸
        SDL_SetTextureAlphaMod(m_frostNovaTexture, // 淡出：结束前线性衰减
                               static_cast<Uint8>((1.0f - progress) * 235.0f)); // 0~235
        SDL_RenderTexture(renderer, m_frostNovaTexture, nullptr, &dst); // 整张冰环贴图
        SDL_SetTextureAlphaMod(m_frostNovaTexture, 255); // 还原不透明（防污染后续绘制）
    }

    // ---- 冰爪 / 冰痕（纯代码线条，无新素材）----
    // 霜哥布林前扑期间在正前方画三道冰爪；霜骷髅突刺期间在身后画冰痕。
    if (m_attackPhase == AttackPhase::Strike && attackModule().frostVisual)
    {
        const float interpX = m_prevX + (m_x - m_prevX) * alpha; // 插值左上角 x
        const float interpY = m_prevY + (m_y - m_prevY) * alpha; // 插值左上角 y
        const Vec2 center = camera.worldToScreen(interpX + m_w * 0.5f, // 本体中心屏幕 x
                                                 interpY + m_h * 0.5f); // 本体中心屏幕 y
        const float perpX = -m_attackDirY; // 方向垂直分量 x（用于三道平行线）
        const float perpY = m_attackDirX;  // 方向垂直分量 y
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); // 半透明线条
        SDL_SetRenderDrawColor(renderer, 205, 245, 255, 210); // 冰白偏青
        if (m_attackKind == AttackKind::Lunge) // 冰爪：三道平行短线在正前方
        {
            for (int i = -1; i <= 1; ++i) // 三道：-1 / 0 / +1
            {
                const float offset = static_cast<float>(i) * 9.0f; // 横向错开 9px
                const float startX = center.x + m_attackDirX * 18.0f + perpX * offset; // 起点 x
                const float startY = center.y + m_attackDirY * 18.0f + perpY * offset; // 起点 y
                const float endX = center.x + m_attackDirX * 44.0f + perpX * offset * 1.4f; // 终点 x
                const float endY = center.y + m_attackDirY * 44.0f + perpY * offset * 1.4f; // 终点 y
                SDL_RenderLine(renderer, startX, startY, endX, endY); // 一道冰爪
                SDL_RenderLine(renderer, startX + 1.0f, startY, // 加粗第二笔（x 偏移）
                               endX + 1.0f, endY); // 同一条线右移 1px
            }
        }
        else if (m_attackKind == AttackKind::Thrust) // 冰痕：三道拖尾在身后
        {
            for (int i = -1; i <= 1; ++i) // 三道：-1 / 0 / +1
            {
                const float offset = static_cast<float>(i) * 7.0f; // 横向错开 7px
                const float startX = center.x - m_attackDirX * 8.0f + perpX * offset; // 起点 x
                const float startY = center.y - m_attackDirY * 8.0f + perpY * offset; // 起点 y
                const float endX = center.x - m_attackDirX * 34.0f + perpX * offset; // 终点 x
                const float endY = center.y - m_attackDirY * 34.0f + perpY * offset; // 终点 y
                SDL_RenderLine(renderer, startX, startY, endX, endY); // 一道冰痕
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE); // 恢复默认混合
    }

    // 本体绘制：跳跃蓄力压扁、跳跃位移叠加弧线；其余状态与基类同款
    drawBody(renderer, alpha, camera, jumpOffsetY, drawScaleX, drawScaleY);

    // 出招染色脉冲：出招瞬间 0.1s 内再画一遍偏红加色（与红染同一套 ADD 技法）
    if (m_attackPulseLeft > 0.0f && m_texture != nullptr)
    {
        SDL_SetTextureColorMod(m_texture, 255, 60, 60);        // 偏红：只留少量绿蓝参与加色
        SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_ADD); // 叠加混合：在底色上「加光」
        drawBody(renderer, alpha, camera, jumpOffsetY, drawScaleX, drawScaleY); // 第二次绘制
        SDL_SetTextureColorMod(m_texture, 255, 255, 255);      // 还原颜色调制
        SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_BLEND); // 还原混合模式
    }

    // 受击红染：红染计时内再画一遍红光叠加（与玩家同款）
    if (m_hitTintLeft > 0.0f && m_texture != nullptr)
    {
        SDL_SetTextureColorMod(m_texture, 255, // 偏红：只留少量绿蓝参与加色
                               static_cast<Uint8>(kHitTintGreenBlue), // 绿通道压低
                               static_cast<Uint8>(kHitTintGreenBlue)); // 蓝通道压低
        SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_ADD); // 叠加混合
        drawBody(renderer, alpha, camera, jumpOffsetY, drawScaleX, drawScaleY); // 第二次绘制
        SDL_SetTextureColorMod(m_texture, 255, 255, 255);      // 还原颜色调制
        SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_BLEND); // 还原混合模式
    }

    // ---- 蓄力警示「!」（批五A）：蓄力期间显示、攻击位移开始消失 ----
    // 阶段判据即规格：只有 Windup 阶段绘制（进 Strike 自然消失）；
    // 画在怪物头顶上方（底边高于血条区，同屏不重叠）。纹理由场景注入
    // 共享（nullptr = 加载失败：警示缺失、其余表现照常）
    if (m_attackPhase == AttackPhase::Windup && m_alertTexture != nullptr)
    {
        // 插值位置：与本体绘制使用同一条「prev -> cur 按 alpha 线性插值」
        // 公式（怪物移动/后倾时警示贴着头顶走，不脱节）
        const float interpX = m_prevX + (m_x - m_prevX) * alpha; // 插值左上角 x
        const float interpY = m_prevY + (m_y - m_prevY) * alpha; // 插值左上角 y
        // 屏幕锚点：怪物水平中心、精灵顶部再上移 kAlertOffsetY（警示底边）
        const Vec2 screen = camera.worldToScreen(interpX + m_w * 0.5f,
                                                 interpY - kAlertOffsetY);
        const SDL_FRect dst{ screen.x - kAlertWidth * 0.5f, // 水平居中于怪物
                             screen.y - kAlertHeight,        // 底边锚在头顶上方
                             kAlertWidth,                    // 绘制宽（与素材一致）
                             kAlertHeight };                 // 绘制高（同上）
        SDL_RenderTexture(renderer, m_alertTexture, nullptr, &dst); // 整张绘制（单帧素材）
    }

    // ---- 头顶血条（批三）：受损后显示、满血隐藏 ----
    // 满血（或异常上限）不画：轻量读血只在「需要关心血量」时占用画面
    if (m_hp >= m_maxHp || m_maxHp <= 0)
    {
        return; // 满血：无血条
    }

    // 插值位置：与本体绘制使用同一条「prev -> cur 按 alpha 线性插值」公式
    //（血条跟着精灵走，快速移动/相机跟随都不脱节）
    const float interpX = m_prevX + (m_x - m_prevX) * alpha; // 插值左上角 x
    const float interpY = m_prevY + (m_y - m_prevY) * alpha; // 插值左上角 y

    // 血条几何（世界坐标）：水平居中于怪物、悬在头顶上方（留 kHpBarGapY 间隙）
    const float barCenterX = interpX + m_w * 0.5f;             // 血条水平中心
    const float barTopY = interpY - kHpBarGapY - kHpBarHeight; // 血条顶边 y

    // 世界 -> 屏幕换算（血条是屏幕空间绘制：在当前无缩放的相机下与
    // 世界空间尺寸一致，换算后直接按像素绘制矩形）
    const Vec2 screen = camera.worldToScreen(barCenterX, barTopY);

    // 血条外沿（近黑色描边）：让血条在深色地板/墙上仍有轮廓可辨
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // 近黑：描边色
    const SDL_FRect barOutline{ screen.x - kHpBarWidth * 0.5f - 1.0f, // 外扩 1 像素
                                screen.y - 1.0f,                       // 上沿外扩 1 像素
                                kHpBarWidth + 2.0f,                    // 宽度两侧各 1 像素
                                kHpBarHeight + 2.0f };                 // 高度上下各 1 像素
    SDL_RenderFillRect(renderer, &barOutline);

    // 血条底色（深色底）：满宽铺底——剩余生命的比例对照背景读出
    SDL_SetRenderDrawColor(renderer, 45, 32, 34, 255); // 深红灰：底色
    const SDL_FRect barBack{ screen.x - kHpBarWidth * 0.5f, // 左边界（居中）
                             screen.y,                       // 顶边界
                             kHpBarWidth,                    // 满宽 40
                             kHpBarHeight };                 // 高 5
    SDL_RenderFillRect(renderer, &barBack);

    // 血条前景（红色）：宽度 = 满宽 x 生命比例（比例夹到 [0,1] 防御异常值）
    const float ratio = std::clamp(static_cast<float>(m_hp) / static_cast<float>(m_maxHp),
                                   0.0f, 1.0f); // 生命比例
    SDL_SetRenderDrawColor(renderer, 220, 60, 60, 255); // 亮红：生命前景
    const SDL_FRect barFill{ barBack.x,        // 与底色同左边界
                             barBack.y,        // 与底色同顶边界
                             barBack.w * ratio, // 按比例缩宽
                             barBack.h };      // 与底色同高
    SDL_RenderFillRect(renderer, &barFill);
}
