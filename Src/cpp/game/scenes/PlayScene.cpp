#include "game/scenes/PlayScene.h"
#include "game/scenes/MenuScene.h" // MenuScene：GameOver 按 M 时要切换回主菜单
#include "game/Separation.h"       // computeSeparation：重叠兜底与怪怪软分离的最小分离量（批四）
#include "game/CardLayout.h"       // cardPanelRectAt / cardPanelHitTest：卡面板布局共享计算（批五A）
#include "engine/DebugText.h"      // DebugText：仅用于左下角 FPS/帧时间调试行（ASCII）
#include "engine/Input.h"          // Input：ctx.input->pressed 查询暂停/重开/回菜单边缘
#include "engine/Rect.h"           // rectsOverlap：接触伤害/拾取的 AABB 判定
#include "engine/SceneManager.h"   // SceneManager：m_sceneManager->requestScene 需要完整类型
#include <SDL3_image/SDL_image.h>  // IMG_LoadTexture：加载攻击反馈素材（批五A：alert/hit_fx）
#include <algorithm>               // std::max / std::min：生命夹取、难度曲线封顶
#include <cmath>                   // std::fmod / std::cos / std::sin：等级瞬闪相位与出生环取点
#include <memory>                  // std::make_unique：构造 MenuScene
#include <string>                  // std::string：拼接素材与字体的绝对路径

// 本编译单元私有的格式化工具（不对外暴露）
namespace
{

// 把存活秒数格式化为「存活: mm:ss」文案（分为两位整数、秒两位补零）。
// 分钟数不设上限（长局显示 12:05 这类自然结果）；负秒按 0 处理（防御性，
// 计时只做加法不可能为负，但格式化工具不假设调用方永远正确）
void formatSurvivalText(char* buffer, size_t size, float seconds)
{
    const int total = (seconds > 0.0f) ? static_cast<int>(seconds) : 0; // 秒 -> 整秒
    SDL_snprintf(buffer, size, "存活: %02d:%02d", total / 60, total % 60);
}

} // 匿名命名空间结束

// 析构（批五A 起恢复自定义实现）：除成员 RAII 的既有释放之外（世界区块/
// 素材纹理由 World 兜底、玩家/怪物/宝石纹理由各 Entity 兜底、升级音效
// 播放器随 LevelupSfx 收尾），新增的攻击反馈纹理（alert/hit_fx）由本类
// 自持——在此显式释放。关键时机不变：SceneManager 先析构旧场景、后销毁
// renderer，上述释放发生时 renderer 尚在，安全
PlayScene::~PlayScene()
{
    releaseFeedbackTextures(); // 攻击反馈纹理释放（幂等；见实现）
}

// 释放攻击反馈纹理（批五A）：先判空、再销毁、最后置空——幂等，
// 供析构兜底调用（renderer 销毁前必然已执行）
void PlayScene::releaseFeedbackTextures()
{
    if (m_alertTexture != nullptr)
    {
        SDL_DestroyTexture(m_alertTexture); // 释放蓄力警示「!」纹理
        m_alertTexture = nullptr;           // 置空：重复调用安全
    }
    if (m_monsterHitFxTexture != nullptr)
    {
        SDL_DestroyTexture(m_monsterHitFxTexture); // 释放命中爪痕纹理
        m_monsterHitFxTexture = nullptr;           // 置空：重复调用安全
    }
}

// 进入场景：保存 renderer 与音效系统、读取配置、加载资源、布置初始局面。
// 每次进入都是新构造的 PlayScene 对象 => 天然是「全新一局」
void PlayScene::onEnter(SDL_Renderer* renderer, const Config& config, AudioSystem& audio)
{
    // 1) 保存 renderer：后续每帧渲染、资源加载都用它（只借用，不负责销毁）
    m_renderer = renderer;

    // 2) 保存音效系统指针：拾取/被撞/结束/暂停四类事件用它触发播放。
    //    所有权在 GameDev，受 SceneManager 管理期间必然非空（与 Config 同一管线）
    m_audio = &audio;

    // 3) 由 renderer 反查窗口：玩法边界/刷新范围需要窗口整数尺寸
    m_window = SDL_GetRenderWindow(renderer);

    // 4) 读取本场景相关的配置项。getFloat/getInt 在缺键或值非法时会记录
    //    警告并回退到内置默认值，因此没有 config/game.ini 时游戏照常可玩：
    //      player_speed            玩家移动速度（像素/秒）
    //      player_accel            玩家加速度（像素/秒²）
    //      player_friction         玩家摩擦减速度（像素/秒²）
    //      player_anim_interval    玩家行走动画每帧时长（秒）
    //      player_hp               玩家生命上限
    //      attack_cooldown         攻击冷却（秒）——左键按住时的横扫节奏
    //      attack_swing_time       横扫表现时长（秒）——刀锋扫完全程的总时间
    //      attack_damage           横扫伤害（点，弧内每只各扣）
    //      attack_range            横扫半径（像素）
    //      attack_arc              横扫总张角（度）
    //      attack_knockback        横扫命中击退（像素）
    //      monster_speed_growth    怪物速度成长（像素/秒²，按局内秒数叠加）
    //      monster_spawn_interval  刷新间隔初始值（秒，随局内时间收紧）
    //      monster_max_count       数量上限初始值（只，随局内时间 +2）
    //      monster_damage          怪物攻击伤害（攻击命中时扣除的生命值）
    //      monster_attack_cooldown 怪物攻击冷却（秒；两次出招的最小间隔）
    //      coin_magnet_radius      金币磁吸半径（像素；进入后朝玩家吸引）
    //      monster_patrol_radius   巡逻半径（像素；绕巢游荡范围）
    //      monster_aggro_radius    仇恨半径（像素；玩家进入即追击）
    //      monster_leash_radius    栓绳半径（像素；离巢超过即回巢）
    //      monster_return_heal_percent 回巢回血百分比（0=关闭；预留键）
    //      xp_level_base           升级曲线基数（第 1 级升第 2 级的需求）
    //      xp_level_growth         升级曲线倍率（需求逐级倍率）
    //（M2 起升级成长由三选一卡牌提供——卡池/数值为代码常量，见
    //  game/UpgradeCard.h 的卡表；旧 levelup_* 自动强化键已退役删除）
    //      world_chunks_x/y        世界区块数（横向/纵向；世界 = 区块 x 768px）
    //      world_seed              世界种子（0 = 每局随机；非 0 = 固定可复现）
    //      camera_smooth_time      相机平滑时间常数（秒）
    const float playerSpeed = config.getFloat("player_speed", Player::kDefaultSpeed);
    const float playerAccel = config.getFloat("player_accel", Player::kDefaultAccel);
    const float playerFriction = config.getFloat("player_friction", Player::kDefaultFriction);
    const float playerAnimInterval =
        config.getFloat("player_anim_interval", Player::kDefaultFrameTime);
    const float attackCooldown =
        config.getFloat("attack_cooldown", Player::kDefaultAttackCooldown);
    const float attackSwingTime =
        config.getFloat("attack_swing_time", Player::kDefaultSwingTime);
    // 横扫半径直接读进「基准」字段（M2：卡牌从基准重算当前值——长臂加范围）；
    // 张角与击退距离做下限夹取（负张角/负距离没有玩法语义）；张角上界交由
    // AttackArc 归一化（超整圆按整圆处理）
    m_attackRangeBase = config.getFloat("attack_range", kDefaultAttackRange);
    m_attackArcBase = std::max(0.0f, config.getFloat("attack_arc", kDefaultAttackArc));
    m_attackKnockbackBase = std::max(0.0f, config.getFloat("attack_knockback", kDefaultAttackKnockback));
    m_monsterSpeedGrowth = config.getFloat("monster_speed_growth", kDefaultMonsterSpeedGrowth);
    m_monsterSpawnInterval =
        config.getFloat("monster_spawn_interval", kDefaultMonsterSpawnInterval);
    m_monsterDamage = config.getInt("monster_damage", kDefaultMonsterDamage);
    // 批四新增：怪物攻击冷却（秒）——负值夹到 0（0 = 连续出招的显式语义，
    // 与 attack_cooldown 的防御口径一致）；注入时机见下方怪物池装配
    m_monsterAttackCooldown =
        std::max(0.0f, config.getFloat("monster_attack_cooldown", kDefaultMonsterAttackCooldown));

    // 批四新增：金币磁吸半径（像素）读进「基准」字段（M2：聚宝从基准加半径）
    // ——负值夹到 0（0 = 只在完全贴合时吸附的显式语义；范围外金币静止不动，
    // 可走过去拾取）
    m_coinMagnetRadiusBase = std::max(0.0f, config.getFloat("coin_magnet_radius", kDefaultCoinMagnetRadius));

    // 批三新增：怪物生态参数（AI 决策半径 + 回巢回血预留键）。
    // 半径做下界夹取（负半径没有玩法语义——宁可按 0 处理，也不让
    // 负值进 AI 的时间/距离计算）；回血百分比同样夹到 0 起
    //（0 = 关闭；负数等价关闭，显式夹取让注入值可控）
    m_monsterPatrolRadius = std::max(0.0f, config.getFloat("monster_patrol_radius", kDefaultMonsterPatrolRadius));
    m_monsterAggroRadius = std::max(0.0f, config.getFloat("monster_aggro_radius", kDefaultMonsterAggroRadius));
    m_monsterLeashRadius = std::max(0.0f, config.getFloat("monster_leash_radius", kDefaultMonsterLeashRadius));
    m_monsterReturnHealPercent =
        std::max(0.0f, config.getFloat("monster_return_heal_percent", kDefaultMonsterReturnHealPercent));

    // 批二新增：世界与相机配置。
    // 世界尺寸做下界钳制（至少 1x1 区块——World::generate 内还有一道同样的
    // 防御）；种子 0 = 「每局随机」哨兵：在 1..INT32_MAX 取一个非 0 随机值
    // （非 0 值都是固定种子——同一布局完全可复现，供调试与确定性冒烟）；
    // 相机平滑时间夹到非负（0 = 立即到位——显式配置的「无平滑」语义）
    const int worldChunksX = std::max(1, config.getInt("world_chunks_x", kDefaultWorldChunks));
    const int worldChunksY = std::max(1, config.getInt("world_chunks_y", kDefaultWorldChunks));
    const int worldSeedConfig = config.getInt("world_seed", 0);
    // 种子换算：非 0 原样使用；0 时取 [1, INT32_MAX] 的随机值（+1 避开 0）
    const uint32_t worldSeed = (worldSeedConfig != 0)
        ? static_cast<uint32_t>(worldSeedConfig)
        : static_cast<uint32_t>(SDL_rand(0x7FFFFFFF)) + 1u;
    m_cameraSmoothTime =
        std::max(0.0f, config.getFloat("camera_smooth_time", kDefaultCameraSmoothTime));

    // 「基准值」= 配置里的初始值（M2：卡牌效果从「基准 + 层数」重算——
    // applyCardEffects 是唯一入口；restart 清层数后重算即复位）。
    // 「当前值」先行置为基准：让随后「生命回满」等流程读到合法初值；
    // 步骤 8 的 applyCardEffects 会再从基准/层数重算一遍（层数全 0 => 同值）
    m_playerSpeedBase = playerSpeed;                                         // 移速基准
    m_playerMaxHpBase = config.getInt("player_hp", kDefaultPlayerHp);        // 生命上限基准
    m_attackDamageBase = config.getInt("attack_damage", kDefaultAttackDamage); // 攻击伤害基准
    m_attackCooldownBase = attackCooldown;                                   // 攻击冷却基准（疾风乘算）
    m_playerSpeed = m_playerSpeedBase;   // 当前移速 = 基准（本局成长尚未发生）
    m_playerMaxHp = m_playerMaxHpBase;   // 当前生命上限 = 基准
    m_attackDamage = m_attackDamageBase; // 当前攻击伤害 = 基准

    // 升级曲线：参数直接注入 LevelCurve——它内部对 0/负基数、小于 1 或
    // NaN 的倍率有完整的边界夹取（见其头文件，有单测）。
    // M2：每级自动强化（旧 levelup_* 键）已退役——升级成长改由三选一
    // 卡牌提供（卡表与常量见 game/UpgradeCard.h）
    m_levelCurve.base = config.getInt("xp_level_base", kDefaultXpLevelBase);
    m_levelCurve.growth = config.getFloat("xp_level_growth", kDefaultXpLevelGrowth);

    // 负数会经 static_cast 变成巨大的 size_t 引发异常（与金币池时代的防护同款），
    // 所以先用 std::max 夹到 0 以上；0 只怪物（无敌人模式）是允许的合法配置。
    // 上限封顶在 currentMonsterCap 中再统一处理（kMaxMonsterCap = 池大小）
    m_monsterMaxCount = std::max(0, config.getInt("monster_max_count", kDefaultMonsterMaxCount));

    // 刷新时刻表从首刷延迟起算；玩家生命在步骤 8 的 applyCardEffects 之后
    // 回满（先把上限按卡牌层数重算到位——层数全 0 时即配置初值）
    m_monsterNextSpawnTime = kFirstSpawnDelay;   // 首只怪物：局内 2 秒后出现

    // 冒烟观测点：日志确认场景切换实际发生（自动化测试据此断言流程）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "进入场景: Play");

    // 5) 生成世界（批二核心）：默认 4x4 区块 x 768px = 3072x3072（config 可调）。
    //    确定性生成：同一 (种子, 区块) 永远得到同一布局；world_seed=0 时
    //    每局换新随机种子（R 重开不换——整个对局共享同一张图，「走远再
    //    回头内容一致」由此保证）。素材（地砖/墙/装饰）加载失败只降级为
    //    纯色色块，不阻塞可玩性（World::loadAssets 内部逐张警告）
    const char* basePath = SDL_GetBasePath(); // exe 目录：字体等素材路径的基准
    m_world.generate(worldChunksX, worldChunksY, worldSeed);
    m_world.loadAssets(m_renderer);
    // 冒烟观测点：世界生成日志（尺寸与种子——确定性复现与断言依据）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "世界生成（%d x %d 区块，%u 像素见方，种子 %u）",
                m_world.chunksX(), m_world.chunksY(),
                static_cast<unsigned>(m_world.pixelWidth()), m_world.seed());

    // 6) 加载 UI 字体（assets/fonts/SmileySans-Oblique.ttf，中文开源字体）。
    //    路径拼装以 exe 所在目录为基准（与其余素材一致）。
    //    HUD 与标题各加载一份（字号不同）；Font::load 内部会记录失败警告，
    //    失败后对应文字安全跳过，玩法完全不受影响
    if (basePath != nullptr)
    {
        // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
        const std::string fontPath = std::string(basePath) + "assets/fonts/SmileySans-Oblique.ttf";
        // 三个字号彼此独立：一份加载失败不影响其余（面板文字安全跳过）
        m_hudFont.load(fontPath, kHudPtSize);   // HUD/提示文字
        m_bigFont.load(fontPath, kBigPtSize);   // 暂停/结束标题
        m_cardFont.load(fontPath, kCardPtSize); // 升级三选一面板小字（描述/层数；M2）
    }

    // 7) 玩家纹理：三级回退（精灵表 -> 单帧图 -> 程序化纯色）在 Entity 内完成，
    //    这里失败只可能是最后的程序化兜底也失败（内存不足等极端情况）
    if (!m_player.load(m_renderer))
    {
        // 纹理创建失败：记录错误但不中断（该实体渲染时跳过，游戏不至于崩溃）
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "创建玩家纹理失败：%s", SDL_GetError());
    }

    // 7.5) 攻击反馈纹理（批五A，可选增强）：蓄力警示「!」与命中爪痕——
    //      与武器素材同一加载模式（IMG_LoadTexture + 最近邻采样），失败只
    //      警告、对应表现跳过（游戏照常）。alert 一份共享注入全部怪物
    //      槽位（见步骤 12）；hit_fx 由场景在命中结算时自绘
    if (basePath != nullptr)
    {
        const std::string alertPath = std::string(basePath) + "assets/alert.png"; // 警示素材绝对路径
        m_alertTexture = IMG_LoadTexture(m_renderer, alertPath.c_str()); // 读文件 -> 解码 -> 纹理
        if (m_alertTexture != nullptr)
        {
            SDL_SetTextureScaleMode(m_alertTexture, SDL_SCALEMODE_NEAREST); // 像素风：最近邻采样
        }
        else
        {
            // 素材缺失/解码失败：警告后继续（蓄力少了警示，游戏照常可玩）
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "加载警示素材 %s 失败（%s），蓄力警示将跳过", alertPath.c_str(), SDL_GetError());
        }

        const std::string hitFxPath = std::string(basePath) + "assets/monster_hit_fx.png"; // 爪痕素材绝对路径
        m_monsterHitFxTexture = IMG_LoadTexture(m_renderer, hitFxPath.c_str()); // 同一加载路径
        if (m_monsterHitFxTexture != nullptr)
        {
            SDL_SetTextureScaleMode(m_monsterHitFxTexture, SDL_SCALEMODE_NEAREST); // 最近邻采样
        }
        else
        {
            // 素材缺失/解码失败：警告后继续（命中少了爪痕，游戏照常可玩）
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "加载命中特效素材 %s 失败（%s），命中特效将跳过", hitFxPath.c_str(), SDL_GetError());
        }
    }

    // 8) 注入玩家配置：加减速/动画节奏/扫程时长（不受卡牌影响的四项），
    //    受卡牌影响的数值（移速/攻击冷却/扫角）与其余卡效果走统一入口
    //    applyCardEffects 重算注入。必须在 load 之后设置——load 会把内置
    //    默认帧时长先写进动画数据，配置值晚一步落定才能覆盖它（各 set
    //    对非法值会忽略并警告）。层数全 0 时 applyCardEffects 等价于写入
    //    各基准；扫角注入与扇形判定张角同值——判定与渲染同一条刀锋曲线
    //（画到哪、打到哪）
    m_player.setMotion(playerAccel, playerFriction);
    m_player.setFrameTime(playerAnimInterval);
    m_player.setSwingTime(attackSwingTime);
    applyCardEffects();         // 卡效果统一入口（移速/冷却/张角 + 全部数值；层数全 0 = 基准注入）
    m_playerHp = m_playerMaxHp; // 生命回满：全新一局（上限已按卡牌层数重算到位）

    // 9) 查询窗口实际尺寸并缓存：相机视口与后续换算都基于它
    SDL_GetWindowSize(m_window, &m_width, &m_height);

    // 10) 玩家出生在世界中心（批二：世界取代窗口成为边界）。出生点周围
    //     保护区由 World 生成时保证——该处（中心 ±3 格）绝无墙体、必定可走。
    //     每次进入 PlayScene 都从这里开始 => 与「按 R 重开」的初始状态一致
    const SDL_FPoint spawn = m_world.spawnPointFor(m_player.width(), m_player.height());
    m_player.reset(spawn.x, spawn.y);

    // 玩家注入世界指针：移动由「分轴位移 + 撞墙滑动」（World::slideMove）执行
    m_player.setWorld(&m_world);

    // 11) 相机（批二核心）：视口 = 窗口尺寸、世界尺寸来自 World、镜头立即
    //     对准玩家中心（进局不做「从别处滑过来」的动画——snapTo 瞬移对齐）。
    //     然后立即刷一次区块激活窗口：第一帧渲染前的 3x3 区块就位
    //     （主循环首帧可能不补步，不能依赖 update 里的那次刷新）
    m_camera.setViewport(static_cast<float>(m_width), static_cast<float>(m_height));
    m_camera.setWorldSize(m_world.pixelWidth(), m_world.pixelHeight());
    m_camera.snapTo(spawn.x + m_player.width() * 0.5f,
                    spawn.y + m_player.height() * 0.5f);
    m_world.updateActiveChunks(m_renderer, m_camera);

    // 12) 预创建怪物对象池：resize 按池大小（kMonsterPoolSize = 数量上限封顶 12）
    //     构造槽位（此刻纹理为空、均为未激活）。注意：纹理不在创建时预加载——
    //     变体在「刷新时刻」才等权随机决定，纹理随变体由 loadVariant 按需加载
    //     （单帧 PNG 解码开销很小）；未激活的槽位不更新、不渲染、不参与碰撞
    m_monsters.resize(static_cast<size_t>(kMonsterPoolSize));

    // 池内槽位统一注入世界指针（碰撞滑动与击退落点校验用）与批三 AI 参数：
    // 决策半径来自 config（上面刚读），停顿/判据等细节用 MonsterAi 的
    // 代码常量默认值（不进配置文件——「半径可调、手感固定」的本批取舍）
    MonsterAi::Config aiConfig; // AI 决策参数（半径类填配置值）
    aiConfig.patrolRadius = m_monsterPatrolRadius; // 巡逻半径（monster_patrol_radius）
    aiConfig.aggroRadius = m_monsterAggroRadius;   // 仇恨半径（monster_aggro_radius）
    aiConfig.leashRadius = m_monsterLeashRadius;   // 栓绳半径（monster_leash_radius）
    for (Monster& monster : m_monsters)
    {
        monster.setWorld(&m_world);                          // 世界（碰撞）注入
        monster.setAiConfig(aiConfig);                       // AI 决策参数注入
        monster.setReturnHealPercent(m_monsterReturnHealPercent); // 回巢回血（0=关闭）
        monster.setAttackCooldown(m_monsterAttackCooldown);  // 攻击冷却（批四；出招间隔）
        monster.setAlertTexture(m_alertTexture);             // 蓄力警示「!」（批五A；共享纹理、只借用）
    }

    // 13) 读取最佳战绩快照：本局的比较基准（缺失/旧格式/损坏自动按无记录处理，
    //     永不失败）。整局沿用这份快照——GameOver 结算时才比较与更新
    m_best = SaveData::load();

    // 14) 加载升级音效（可选增强）：素材缺失/无音频设备只警告，升级静默、
    //     流程照常（降级细节见 LevelupSfx.h 的契约）
    m_levelupSfx.load();

    // 15) 播种三选一升级卡的随机源（M2）：在 [1, INT32_MAX] 取随机种子——
    //     与 world_seed=0「每局随机」同款口径（每局卡序不同；种子采用
    //     非 0 值，0 会被 CardSeedRandom::seed 置换为固定常量）。
    //     R 重开不重新播种：层数清零后卡池全开，后续抽卡沿用同一序列
    m_cardRng.seed(static_cast<std::uint32_t>(SDL_rand(0x7FFFFFFF)) + 1u);
}

// 事件处理：只保留 F3（帧率显示切换）。
// 迁移说明：暂停/重开/回菜单原先在这里按 SDL_EVENT_KEY_DOWN 处理，
// 本批统一迁移为 update 中的 pressed 动作边缘（见 handleInputEdges）——
// 好处是这些动作走 config 的 bind_* 配置、可改键，且与移动同一套查询；
// F3 刻意维持事件式：它是调试开关而非玩法动作，不需要参与动作绑定
void PlayScene::handleEvent(const SDL_Event& event)
{
    // 只关心按键按下事件；其余事件一律忽略
    if (event.type != SDL_EVENT_KEY_DOWN)
    {
        return;
    }

    // F3：切换左下角 FPS/帧时间显示（任何对局状态下都响应）。
    // 它不影响对局状态，只控制显示
    if (event.key.scancode == SDL_SCANCODE_F3)
    {
        m_fpsVisible = !m_fpsVisible; // 开 <-> 关
        // 冒烟观测点：日志确认切换实际发生（自动化测试据此断言）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "帧率显示: %s", m_fpsVisible ? "开" : "关");
    }
}

// 输入动作的按下边缘处理（每个上升沿恰好响应一次）。
// 调用位置：update 的最开头——暂停/重开/回菜单/商店在任何状态下都要能响应
//（包括 Paused/GameOver 的冻结分支；PlayScene 世界冻结但输入始终可用）
// 例外（M2/批五B）：LevelUp（升级三选一面板）期间暂停/重开/回菜单不响应
//（防误触）——各分支的状态判据（Playing/Paused/GameOver）天然排除 LevelUp，
// 无需额外分支；去重标记照常记录，面板期间按下的键不会在回到 Playing 后
// 迟到触发。批五B 新增 shop 动作：Playing 按 B 打开商店、Shop 中 B/ESC
// 关闭（ESC 的语义在暂停分支内按 Shop 状态分流——见下）
void PlayScene::handleInputEdges(const UpdateContext& ctx)
{
    // 本步的边沿采样（ctx.input 为 nullptr 时按「都没按」安全降级）。
    // 四个动作的语义与物理键位的对应关系由 config 的 bind_* 决定
    const bool pausePressed = ctx.input != nullptr && ctx.input->pressed("pause");
    const bool restartPressed = ctx.input != nullptr && ctx.input->pressed("restart");
    const bool menuPressed = ctx.input != nullptr && ctx.input->pressed("menu");
    const bool shopPressed = ctx.input != nullptr && ctx.input->pressed("shop");

    // ---- 商店开关（批五B；先于暂停处理：Shop 中 ESC 的语义是「关闭商店」，
    //      须在暂停分支看到它之前先把状态算清楚）----
    // 「随时可开」= 战斗进行中（Playing）按 B 一拍即开（世界因此冻结、同
    // 暂停语义）；Paused/LevelUp/GameOver 下不响应（防误触——与 LevelUp
    // 的既有纪律一致）。shopToggled：本步 B 已切换过商店状态——同帧恰好
    // 又按到 ESC 时暂停分支据此跳过，两个关闭/切换键不互相打架
    //（单独按 B / 单独按 ESC 各自都能关闭商店——「互不冲突」的口径）
    bool shopToggled = false; // 本步商店状态是否已由 B 切换
    if (shopPressed && !m_shopHandled)
    {
        if (m_state == PlayState::Playing)
        {
            openShop();           // 打开：切 Shop + 面板基线 + 日志（见实现）
            shopToggled = true;   // 标记：本步已切换
        }
        else if (m_state == PlayState::Shop)
        {
            closeShop();          // B 关闭：切回 Playing + 日志（见实现）
            shopToggled = true;   // 标记：本步已切换
        }
    }
    m_shopHandled = shopPressed; // 记录本步采样：边沿消失前不再重复处理

    // 暂停/恢复：在 Playing 与 Paused 之间切换（GameOver 下不响应）。
    // 批五B：Shop 中按 ESC = 关闭商店（不进入暂停——ESC 在商店语境下是
    // 「退出面板」）；本步 B 已关闭过商店（shopToggled）时整段跳过，
    // 保证同帧双键也只产生一次关闭。去重标记：同一帧补多个固定步时
    // pressed 恒 true，只在「首次见到上升沿」的那一步执行切换，
    // 避免一帧内反复横跳
    if (pausePressed && !m_pauseHandled && !shopToggled)
    {
        if (m_state == PlayState::Playing)
        {
            // 进入暂停：世界与计时从此冻结（update 对非 Playing 直接返回）
            m_state = PlayState::Paused;
            // 播放暂停轻音（pause）：与恢复共用同一个音效，切换动作的听觉确认
            m_audio->play(SoundId::Pause);
            // 冒烟观测点：确认暂停触发（自动化测试据此断言）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "对局暂停（ESC 恢复，M 回菜单）");
        }
        else if (m_state == PlayState::Paused)
        {
            // 从暂停处继续：实体状态原样保留，继续推进
            m_state = PlayState::Playing;
            // 播放恢复轻音（同样是 pause 音效：暂停/恢复切换共用）
            m_audio->play(SoundId::Pause);
            // 冒烟观测点：确认恢复触发（自动化测试据此断言）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "对局恢复");
        }
        else if (m_state == PlayState::Shop)
        {
            // ESC 关闭商店：与 B 关闭同一落点（回 Playing、打关闭日志）——
            // 批五B 规格「B 或 ESC 关闭」的 ESC 一侧
            closeShop(); // 内部实现：状态切换 + 日志
        }
    }
    m_pauseHandled = pausePressed; // 记录本步采样：边沿消失前不再重复处理

    // 重开：仅 GameOver 下响应（Playing/Paused 按 R 无效果，与旧行为一致）
    if (restartPressed && !m_restartHandled && m_state == PlayState::GameOver)
    {
        restart(); // 重开一局（内部重置全部局面数据并打日志）
    }
    m_restartHandled = restartPressed; // 记录采样：上升沿去重

    // 回主菜单：Paused 与 GameOver 下按 M 生效（Playing 下不响应）。
    // 请求只登记，帧末由 SceneManager 结算，避免在自身回调里析构自己
    if (menuPressed && !m_menuHandled &&
        (m_state == PlayState::Paused || m_state == PlayState::GameOver))
    {
        m_sceneManager->requestScene(std::make_unique<MenuScene>());
    }
    m_menuHandled = menuPressed; // 记录采样：上升沿去重
}

// 打开（或再弹）升级三选一面板（M2）：抽一组卡 -> 有卡进入 LevelUp 展示；
// 池空（防御路径：卡池含两张无限卡、正常局永不空）时逐笔消费欠账并记录
// 跳过，欠账清完后回到 Playing——「无卡可选」绝不把流程卡死。
// 调用时机：升级（grantXp 记完欠账）与选择后仍有欠账时
void PlayScene::openCardPanel()
{
    // 防误触基线置位（批五A）：面板打开后的第一个固定步将采样「左键是否
    // 按着」（见 handleCardChoice 第 0 步）——连升连选换组时同样重置，
    // 与键盘「同一次按住不会连选下一组」的去重语义对齐
    m_cardPanelFresh = true;

    // 欠账 > 0 才需要面板；循环处理「抽到卡」与「池空跳过」两种结果。
    // 正常流程第一轮就抽到卡返回；池空的循环只是防御兜底（不会死循环：
    // 每轮要么抽到卡返回、要么消费一笔欠账直到清零）
    while (m_choiceQueue.pending() > 0)
    {
        // 抽取一组：三张不重复、排除已满层、池不足按实际（纯逻辑，见 UpgradeCard.h）
        m_offeredCards = drawUpgradeCards(m_cardLevels, &CardSeedRandom::nextFrom, &m_cardRng);

        // 抽到至少一张：进入 LevelUp 状态（世界从下一固定步起冻结）并打日志
        if (m_offeredCards.count > 0)
        {
            m_state = PlayState::LevelUp; // 切换状态：update 的冻结分支接管

            // 冒烟观测点：面板日志（含实际张数与卡名——「连升连选」断言点：
            // 连续两组面板会连续出现两条本日志）
            std::string panelText = "升级选择面板（"; // 拼接「（A / B / C）」样式
            for (int i = 0; i < m_offeredCards.count; ++i) // 逐张追加卡名
            {
                if (i > 0) // 非首张：先加分隔符
                {
                    panelText += " / "; // 卡名分隔符
                }
                panelText += cardDef(m_offeredCards.cards[i]).name; // 追加本张卡名
            }
            panelText += "）"; // 收尾括号
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", panelText.c_str()); // 面板日志
            return; // 面板就位：本次打开结束
        }

        // 池空（防御路径）：本笔欠账没有卡可选——消费并记录，继续尝试下一笔
        m_choiceQueue.consume(); // 消费一笔欠账（无卡可选的欠账直接作废）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "升级选择跳过（卡池已满）"); // 观测点
    }

    // 欠账全部清空（全部因池空跳过）：确保回到 Playing 并记录恢复
    m_state = PlayState::Playing; // 恢复对局（正常流程不会走到这里）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "升级选择完毕，恢复对局"); // 观测点
}

// 升级选择输入处理（LevelUp 状态每固定步调用，M2；批五A：鼠标点击）。
// 完整契约（两路并存/防误触/悬停刷新）见头文件声明处
void PlayScene::handleCardChoice(const UpdateContext& ctx)
{
    // ---- 0) 防误触基线（批五A）：面板打开后的第一个固定步采样 ----
    // 规格：点击以「按下边缘」为准；若面板弹出时玩家已按着左键（战斗中
    // 连扫的持续按住），须松开后新的一次点击才生效。「打开时是否按着」
    // 在面板弹出后的第一个固定步读取——openCardPanel 无输入上下文，
    // 此处即最早可读的稳定状态（两者相隔恰好 1 个固定步，语义等价）
    if (m_cardPanelFresh)
    {
        m_cardPanelFresh = false; // 基线只采样一次（后续步骤不再重置）
        m_cardClickNeedRelease = ctx.input != nullptr && ctx.input->held("attack"); // 按着 -> 等松开
    }

    // ---- 1) 悬停刷新（批五A）：每固定步用共享布局计算命中哪张卡 ----
    // 渲染（renderHud 的高亮分支）直接消费本值；无输入系统时按「无悬停」
    if (ctx.input != nullptr)
    {
        int outW = 0; // 渲染输出宽（物理像素——与 renderHud 的布局同一条数源）
        int outH = 0; // 渲染输出高（同上）
        SDL_GetRenderOutputSize(m_renderer, &outW, &outH);
        const Vec2 mouse = ctx.input->mousePosition(); // 鼠标屏幕坐标（窗口客户区快照）
        m_hoverCard = cardPanelHitTest(mouse.x, mouse.y, m_offeredCards.count,
                                       static_cast<float>(outW), static_cast<float>(outH));
    }
    else
    {
        m_hoverCard = -1; // 无输入系统：无悬停高亮
    }

    // ---- 2) 键盘选择（照常）：1/2/3 动作的按下边缘（去重标记与暂停同款）----
    const bool choice1Pressed = ctx.input != nullptr && ctx.input->pressed("choice_1");
    const bool choice2Pressed = ctx.input != nullptr && ctx.input->pressed("choice_2");
    const bool choice3Pressed = ctx.input != nullptr && ctx.input->pressed("choice_3");

    // 选中栏位：1/2/3 中恰好一个「未被本帧消费过」的上升沿；-1 = 本步无选择
    int chosen = -1; // 选中的展示位序（0/1/2）
    if (choice1Pressed && !m_choiceHandled[0])
    {
        chosen = 0; // 按 1：第 1 张
    }
    else if (choice2Pressed && !m_choiceHandled[1])
    {
        chosen = 1; // 按 2：第 2 张
    }
    else if (choice3Pressed && !m_choiceHandled[2])
    {
        chosen = 2; // 按 3：第 3 张
    }

    // ---- 3) 鼠标点击选择（批五A）：「左键按下边缘」+ 卡片命中 ----
    // 左键按下边缘（pressed）：单击与「按住连扫」的持续按住天然区分
    //（按住期间 pressed 只在最初一帧为真——面板弹出前早已消耗）
    const bool clickPressed = ctx.input != nullptr && ctx.input->pressed("attack");

    // 等待松开（防误触）：先在「面板打开时按着」的状态下观测到一次
    // released（松开下降沿），之后的新点击才允许生效；等待期内点击
    //（含恰好重叠的按下边缘）一律忽略——规格「须松开后新的一次点击」
    if (m_cardClickNeedRelease)
    {
        if (ctx.input != nullptr && ctx.input->released("attack"))
        {
            m_cardClickNeedRelease = false; // 已松开：解锁点击
        }
    }

    // 点击生效条件（全部满足才处理）：键盘本步未选中（键盘优先）+ 按下
    // 边缘未被本帧消费过（同帧多固定步去重）+ 已解锁 + 命中某张卡
    //（卡间隙/面板外 = -1 -> 无效点击：不选、不消费，面板继续等待）
    if (chosen < 0 && clickPressed && !m_cardClickHandled && !m_cardClickNeedRelease)
    {
        int outW = 0; // 渲染输出宽（与悬停检测同一数源——同一次布局）
        int outH = 0; // 渲染输出高
        SDL_GetRenderOutputSize(m_renderer, &outW, &outH);
        const Vec2 mouse = ctx.input->mousePosition(); // 鼠标屏幕坐标快照
        const int hit = cardPanelHitTest(mouse.x, mouse.y, m_offeredCards.count,
                                         static_cast<float>(outW), static_cast<float>(outH));
        if (hit >= 0)
        {
            chosen = hit; // 命中卡片：等价按对应数字键（走同一套选择生效流程）
        }
    }

    // 记录本步采样（去重基线）：上升沿消失前不再重复响应——面板换组后，
    // 同一次按住不会连选下一组（与暂停/重开的去重语义完全一致）；
    // 鼠标按下边缘同款去重（批五A）
    m_choiceHandled[0] = choice1Pressed; // 1 号采样记录
    m_choiceHandled[1] = choice2Pressed; // 2 号采样记录
    m_choiceHandled[2] = choice3Pressed; // 3 号采样记录
    m_cardClickHandled = clickPressed;   // 点击采样记录

    // 无有效选择（没按 / 按的是空位——不足三张时按 3 等 / 点击在卡外）：
    // 面板继续等待
    if (chosen < 0 || chosen >= m_offeredCards.count)
    {
        return; // 本步不做任何事
    }

    // ---- 卡牌生效：层数 +1（封顶防御——已满层不会再入池，这里是第二道闸）----
    const CardId id = m_offeredCards.cards[chosen];       // 选中的卡 id
    const int cardIndex = static_cast<int>(id);           // 层数数组下标（与 CardId 对应）
    m_cardLevels[cardIndex] = gainCardLevel(id, m_cardLevels[cardIndex]); // 层数 +1（有限卡封顶）

    // 效果立即落地：从「基准 + 层数」重算全部受卡牌影响的数值并注入
    //（幂等入口；例如伤害/移速/冷却/张角/击退/磁吸/生命上限一步到位）
    applyCardEffects();

    // 体魄的一次性效果：即时回血 20（上限刚在 applyCardEffects 里 +20）。
    // 与重算路径分离的原因：applyCardEffects 幂等、不重复回血——「获得
    // 该卡那一刻」的加血只在本次选择发放（restart 重算不会凭空加血）
    if (id == CardId::Vitality) // 选中的是体魄？
    {
        m_playerHp = std::min(m_playerHp + kCardVitalityHealPerLevel, m_playerMaxHp); // 回血 20 并夹上限
    }

    // ---- 冒烟观测点：升级选择日志（规格文案「升级选择：卡片名（层数 x/上限）」；
    //      无限卡上限写作「无限」）----
    const CardDef& def = cardDef(id); // 卡定义（名称与上限文本的来源）
    char limitText[16];               // 上限文本：数字或「无限」（16 字节绰绰有余）
    if (def.maxLevel > 0)             // 有限卡：上限写数字
    {
        SDL_snprintf(limitText, sizeof(limitText), "%d", def.maxLevel); // 如 "5"
    }
    else                              // 无限卡：上限写「无限」
    {
        SDL_strlcpy(limitText, "无限", sizeof(limitText)); // 文案（UTF-8 6 字节）
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "升级选择：%s（层数 %d/%s）",
                def.name, m_cardLevels[cardIndex], limitText); // 选择日志

    // ---- 欠账消费与后续：未清再弹一组（连升连选）/ 清完回 Playing ----
    const bool hasMore = m_choiceQueue.consume(); // 消费一笔：返回是否还有欠账
    if (hasMore)
    {
        openCardPanel(); // 欠账未清：立刻抽下一组（换面板继续选）
    }
    else
    {
        m_state = PlayState::Playing; // 欠账清完：恢复对局（update 下一步起推进世界）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "升级选择完毕，恢复对局"); // 恢复日志
    }
}

// ============================================================
// 商店（批五B：九强化迁移、金币购买、鼠标/键盘交互）
// ============================================================

// 打开商店（Playing 按 B）：切到 Shop 状态（世界从下一固定步起冻结）+
// 清提示消息 + 面板防误触基线置位 + 打「商店开启」日志
void PlayScene::openShop()
{
    m_state = PlayState::Shop; // 状态切换：update 的冻结分支接管（世界暂停）

    // 提示消息清空：新打开的面板不带上一次的「已购买/金币不足」残留
    m_shopMessage[0] = '\0'; // 空串 = 无消息（渲染据此跳过）

    // 面板防误触基线置位（与升级卡面板同款）：打开后的第一个固定步
    // 采样「左键是否按着」（战斗中连扫的持续按住）——须先松开再点
    m_shopPanelFresh = true; // handleShopInput 首个固定步消费本标记

    // 冒烟观测点：商店开启（含当前金币——自动化测试据此断言状态切换）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "商店开启（金币 %d）", m_coinCount);
}

// 关闭商店（B 或 ESC）：切回 Playing（世界恢复推进）并打「商店关闭」日志
void PlayScene::closeShop()
{
    m_state = PlayState::Playing; // 恢复对局：下一固定步起世界继续推进

    // 冒烟观测点：商店关闭（与开启成对——冒烟断言开/关各恰好一次）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "商店关闭");
}

// 商店面板输入处理（Shop 状态每固定步调用）：悬停刷新 + 1~9 购买 +
// 鼠标点击购买。完整契约（两路并存/防误触/悬停刷新）见头文件声明处
void PlayScene::handleShopInput(const UpdateContext& ctx)
{
    // ---- 0) 防误触基线（与升级卡面板同款）：面板打开后的第一个固定步采样 ----
    // 规格：点击以「按下边缘」为准；若面板弹出时玩家已按着左键（战斗中
    // 连扫的持续按住），须松开后新的一次点击才生效。「打开时是否按着」
    // 在面板弹出后的第一个固定步读取——openShop 无输入上下文，
    // 此处即最早可读的稳定状态（两者相隔恰好 1 个固定步，语义等价）
    if (m_shopPanelFresh)
    {
        m_shopPanelFresh = false; // 基线只采样一次（后续步骤不再重置）
        m_shopClickNeedRelease = ctx.input != nullptr && ctx.input->held("attack"); // 按着 -> 等松开
    }

    // ---- 1) 悬停刷新：每固定步用共享布局计算命中哪一项 ----
    // 渲染（renderHud 的商店分支）直接消费本值；无输入系统时按「无悬停」
    if (ctx.input != nullptr)
    {
        int outW = 0; // 渲染输出宽（物理像素——与 renderHud 的布局同一条数源）
        int outH = 0; // 渲染输出高（同上）
        SDL_GetRenderOutputSize(m_renderer, &outW, &outH);
        const Vec2 mouse = ctx.input->mousePosition(); // 鼠标屏幕坐标（窗口客户区快照）
        m_hoverShopItem = shopPanelHitTest(mouse.x, mouse.y,
                                           static_cast<float>(outW), static_cast<float>(outH));
    }
    else
    {
        m_hoverShopItem = -1; // 无输入系统：无悬停高亮
    }

    // ---- 2) 数字键 1~9 购买（动作 shop_1~shop_9 的按下边缘，去重与暂停同款）----
    int chosen = -1; // 选中的商品下标（0~8）；-1 = 本步无购买
    for (int i = 0; i < kShopItemCount; ++i) // 完整遍历全部九项（采样必须全覆盖）
    {
        char action[16]; // 动作名缓冲（"shop_1"~"shop_9"，最长 6 字节）
        SDL_snprintf(action, sizeof(action), "shop_%d", i + 1); // 商品序号 -> 动作名

        // 本步该动作的按下边缘；去重标记保证同帧多固定步只响应一次。
        // 多个键同帧按下时只取序号最小的一个（chosen < 0 门控）；
        // 全项都要记录采样——否则被跳过项的边沿会推迟到下一帧误触发
        const bool pressed = ctx.input != nullptr && ctx.input->pressed(action);
        if (pressed && !m_shopKeyHandled[i] && chosen < 0)
        {
            chosen = i; // 本项按下：进入购买流程
        }
        m_shopKeyHandled[i] = pressed; // 记录本步采样（边沿去重基线，全项更新）
    }

    // ---- 3) 鼠标点击购买：「左键按下边缘」+ 命中 + 防误触解锁 ----
    // 左键按下边缘（pressed）：单击与「按住连扫」的持续按住天然区分
    const bool clickPressed = ctx.input != nullptr && ctx.input->pressed("attack");

    // 等待松开（防误触）：先在「面板打开时按着」的状态下观测到一次
    // released（松开下降沿），之后的新点击才允许生效；等待期内点击
    //（含恰好重叠的按下边缘）一律忽略
    if (m_shopClickNeedRelease)
    {
        if (ctx.input != nullptr && ctx.input->released("attack"))
        {
            m_shopClickNeedRelease = false; // 已松开：解锁点击
        }
    }

    // 点击生效条件（全部满足才处理）：键盘本步未选中（键盘优先）+ 按下
    // 边缘未被本帧消费过（同帧多固定步去重）+ 已解锁 + 命中某项
    //（卡间隙/面板外 = -1 -> 无效点击：不买、不消费，面板继续等待）
    if (chosen < 0 && clickPressed && !m_shopClickHandled && !m_shopClickNeedRelease)
    {
        int outW = 0; // 渲染输出宽（与悬停检测同一数源——同一次布局）
        int outH = 0; // 渲染输出高
        SDL_GetRenderOutputSize(m_renderer, &outW, &outH);
        const Vec2 mouse = ctx.input->mousePosition(); // 鼠标屏幕坐标快照
        const int hit = shopPanelHitTest(mouse.x, mouse.y,
                                         static_cast<float>(outW), static_cast<float>(outH));
        if (hit >= 0)
        {
            chosen = hit; // 命中商品：等价按对应数字键（走同一套购买流程）
        }
    }
    m_shopClickHandled = clickPressed; // 点击采样记录（边沿去重基线）

    // ---- 4) 购买：有选中就走购买流程（满层/金币不足/成交三分支在内部）----
    if (chosen >= 0)
    {
        tryPurchaseShopItem(chosen); // 购买结算与提示/日志都在内部完成
    }
}

// 商店购买（批五B）：契约见头文件声明处。购买成功日志为规格文案
//「商店购买：卡片名（层数 x/上限，花费 N 金币）」——冒烟断言点
void PlayScene::tryPurchaseShopItem(int itemIndex)
{
    // 下标越界（理论不可达——chosen 由布局/循环产生）：静默忽略（防御）
    if (itemIndex < 0 || itemIndex >= kShopItemCount)
    {
        return; // 不崩、不消费
    }

    const CardId id = static_cast<CardId>(itemIndex); // 商品下标 -> 强化 id（顺序一致）
    const int level = m_cardLevels[itemIndex];        // 当前层数
    const CardDef& def = cardDef(id);                 // 强化定义（名称/上限）

    // ---- 分支 1：已满层（层级上限照旧——不再售卖）----
    if (isCardFull(id, level))
    {
        SDL_strlcpy(m_shopMessage, "已达上限", sizeof(m_shopMessage)); // 面板提示（常驻至下次操作）
        // 冒烟观测点：满层购买被拒（不扣金币）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "商店购买失败：已达上限（%s）", def.name);
        return; // 不扣金币、不加层
    }

    const int price = shopPrice(id, level); // 当前价格 = 基础价 x（层数 + 1）

    // ---- 分支 2：金币不足（恰好等于价格可以买——差 1 即不足）----
    if (m_coinCount < price)
    {
        SDL_strlcpy(m_shopMessage, "金币不足", sizeof(m_shopMessage)); // 面板提示（灰显之外的文字提示）
        // 冒烟观测点：金币不足（需要/持有——冒烟据它断言「差 1 不成交」）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "商店购买失败：金币不足（需要 %d，持有 %d）", price, m_coinCount);
        return; // 不扣金币、不加层
    }

    // ---- 分支 3：成交——扣金币 -> 层数 +1（封顶防御）-> 效果重算 ----
    m_coinCount -= price;                             // 金币扣减（本局货币的唯一消费点）
    m_cardLevels[itemIndex] = gainCardLevel(id, level); // 层数 +1（有限卡封顶——第二道闸）

    // 效果立即落地：从「基准 + 层数」重算全部受强化影响的数值并注入
    //（幂等入口；与升级选择走同一条路径——applyCardEffects）
    applyCardEffects();

    // 体魄的一次性效果：即时回血 20（上限刚在 applyCardEffects 里 +20）。
    // 与重算路径分离的原因：applyCardEffects 幂等、不重复回血——「购买
    // 该强化那一刻」的加血只在本次成交发放（restart 重算不会凭空加血）
    if (id == CardId::Vitality) // 买到的是体魄？
    {
        m_playerHp = std::min(m_playerHp + kCardVitalityHealPerLevel, m_playerMaxHp); // 回血 20 并夹上限
    }

    // ---- 冒烟观测点：商店购买日志（规格文案「商店购买：卡片名（层数 x/上限，
    //      花费 N 金币）」；无限卡上限写作「无限」）----
    char limitText[16];           // 上限文本：数字或「无限」（16 字节绰绰有余）
    if (def.maxLevel > 0)         // 有限卡：上限写数字
    {
        SDL_snprintf(limitText, sizeof(limitText), "%d", def.maxLevel); // 如 "5"
    }
    else                          // 无限卡：上限写「无限」
    {
        SDL_strlcpy(limitText, "无限", sizeof(limitText)); // 文案（UTF-8 6 字节）
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "商店购买：%s（层数 %d/%s，花费 %d 金币）",
                def.name, m_cardLevels[itemIndex], limitText, price); // 购买日志

    // 面板提示（常驻至下次操作/关闭）与成交音效（升级音同款的正反馈）
    SDL_snprintf(m_shopMessage, sizeof(m_shopMessage), "已购买：%s", def.name); // 提示文案
    m_levelupSfx.play(); // 成交音效（游戏层直读 levelup.wav；不可用安全静默）
}

// 强化效果应用（M2 升级卡；批五B 起是商店购买的生效入口）。
// 调用处：onEnter / 升级选择生效 / 商店购买成交 / restart 四处统一入口：
// 从「基准值 + 当前层数」重算全部受强化影响的数值并注入（幂等——重复调用
// 结果一致；层数清零后调用即等价于「效果全部撤销」）。
// 「即时回血」等一次性效果不在此列（选择/购买流程单独发放——重算路径绝不
// 重复回血）
void PlayScene::applyCardEffects()
{
    // 利刃：伤害 = 基准 + 5 x 层数（横扫结算与飘字读取同一个字段）
    m_attackDamage = cardBladeDamage(
        m_attackDamageBase, m_cardLevels[static_cast<int>(CardId::Blade)]);

    // 疾风：冷却 = 基准 x 0.88^层数（乘算）——注入 Player（出手冷却的实际门控值）
    m_player.setAttackCooldown(cardSwiftCooldown(
        m_attackCooldownBase, m_cardLevels[static_cast<int>(CardId::Swift)]));

    // 长臂：攻击范围 = 基准 + 15 x 层数（扇形判定的半径）
    m_attackRange = cardReachRange(
        m_attackRangeBase, m_cardLevels[static_cast<int>(CardId::Reach)]);

    // 横扫：张角 = 基准 + 15 度 x 层数——同步注入 Player（画到哪、打到哪的同一数据源）
    m_attackArc = cardSweepArc(
        m_attackArcBase, m_cardLevels[static_cast<int>(CardId::Sweep)]);
    m_player.setAttackArc(m_attackArc); // 武器扫角 = 判定张角（结构性共享）

    // 疾行：移速 = 基准 + 20 x 层数——注入 Player（下一固定步的移动立即使用）
    m_playerSpeed = cardDashSpeed(
        m_playerSpeedBase, m_cardLevels[static_cast<int>(CardId::Dash)]);
    m_player.setSpeed(m_playerSpeed); // 移速注入（负数防御在 Player::setSpeed 内部）

    // 体魄：生命上限 = 基准 + 20 x 层数；当前生命夹到新上限（重算路径不回血——
    // 即时 +20 只在「选择体魄那一刻」发放，见 handleCardChoice）
    m_playerMaxHp = cardVitalityMaxHp(
        m_playerMaxHpBase, m_cardLevels[static_cast<int>(CardId::Vitality)]);
    m_playerHp = std::min(m_playerHp, m_playerMaxHp); // 上限下调时（重开/异常）夹住当前生命

    // 重击：击退 = 基准 x (1 + 0.5 x 层数)（百分比加法叠加）
    m_attackKnockback = cardHeavyKnockback(
        m_attackKnockbackBase, m_cardLevels[static_cast<int>(CardId::Heavy)]);

    // 聚宝：金币磁吸范围 = 基准 + 60 x 层数
    m_coinMagnetRadius = cardTreasureMagnet(
        m_coinMagnetRadiusBase, m_cardLevels[static_cast<int>(CardId::Treasure)]);

    // 慧眼：没有常驻数值可注入——经验乘数在宝石吸收结算时按层数实时计算
    //（见宝石吸收分支的 cardInsightXpValue 调用；此处无需处理）
}

// 每帧更新：先处理输入动作边缘（任何状态都响应），再按状态推进战斗。
// Paused（暂停）与 GameOver（结束）都冻结玩家、怪物、宝石与计时——暂停
// 由此天然实现了「世界与计时冻结」（实体 update 的统一门控就在本函数）
void PlayScene::update(const UpdateContext& ctx)
{
    // 输入动作边缘：暂停/重开/回菜单（与状态无关，先于冻结分支处理——
    // 否则暂停后按 ESC/M 将无法恢复/回菜单）。LevelUp 下三个动作不响应
    //（各分支状态判据天然排除——防误触；见 handleInputEdges 说明）
    handleInputEdges(ctx);

    // 非 Playing 状态：世界冻结。
    if (m_state != PlayState::Playing)
    {
        // LevelUp（M2 三选一）：先处理 1/2/3 选择——选择可能消费最后一笔
        // 欠账并切回 Playing，但本步仍按冻结处理（世界下一固定步才恢复）
        if (m_state == PlayState::LevelUp)
        {
            handleCardChoice(ctx); // 选择生效/换面板/恢复对局都在内部完成
        }
        // Shop（批五B）：处理购买输入（悬停/1~9/点击）——面板操作只改
        // 局面数值（金币/层数/效果）与面板自身状态，世界本体（实体/计时/
        // 特效）保持冻结；关店（B/ESC）在上方 handleInputEdges 完成，
        // 本步因此仍按冻结处理（世界下一固定步才恢复）
        else if (m_state == PlayState::Shop)
        {
            handleShopInput(ctx); // 悬停刷新/购买结算/提示消息都在内部完成
        }

        // 冻结期间的插值自查（重要）：把各实体的插值快照对齐到当前位置。
        // 若不对齐，最后一次 update 留下的 prev->cur 区间会随渲染帧的
        // alpha 波动来回插值，暂停画面上会出现亚像素级漂移/抖动；
        // 对齐后 prev==cur，无论 alpha 取何值，渲染位置恒等于当前位置
        m_player.snapshotPreviousPosition(); // 玩家：prev=cur
        for (Monster& monster : m_monsters)
        {
            monster.snapshotPreviousPosition(); // 怪物（含空槽位，统一处理更稳妥）
        }
        for (XpGem& gem : m_gems)
        {
            gem.snapshotPreviousPosition(); // 经验宝石：逐颗对齐
        }
        for (Coin& coin : m_coins)
        {
            coin.snapshotPreviousPosition(); // 金币：逐枚对齐（批三）
        }
        return;
    }

    // 每帧查询窗口实际尺寸并缓存，并喂给相机视口（窗口尺寸一旦变化，
    // 本帧的相机换算与区块裁剪立即跟随；相机自身位置由 setViewport
    // 内部的回钳保证仍合法）
    SDL_GetWindowSize(m_window, &m_width, &m_height);
    m_camera.setViewport(static_cast<float>(m_width), static_cast<float>(m_height));

    // 玩家无敌计时：递减剩余无敌时间并夹到 0。受击瞬间会在下面的碰撞
    // 分支重置为满值；暂停/结束时本函数提前返回，计时自然冻结——恢复后
    // 无敌不会「凭空流逝」
    if (m_playerInvincibleLeft > 0.0f)
    {
        m_playerInvincibleLeft = std::max(m_playerInvincibleLeft - ctx.deltaTime, 0.0f);
    }

    // 等级瞬闪计时与漂浮文字推进（仅 Playing 分支能走到这里）：暂停/结束时
    // 本函数提前返回，计时与飘字寿命随世界一起冻结——暂停画面完全静止，
    // 与「世界与计时冻结」的既有承诺一致
    if (m_levelFlashLeft > 0.0f)
    {
        m_levelFlashLeft = std::max(m_levelFlashLeft - ctx.deltaTime, 0.0f);
    }
    for (FloatingText& text : m_floatingTexts)
    {
        text.update(ctx.deltaTime); // 寿命递减；位置与透明度在渲染时按进度即时换算
    }
    // 统一清理已到期的飘字（寿命归零 = 已淡尽，保留其余元素的遍历语义）
    std::erase_if(m_floatingTexts, [](const FloatingText& text) { return text.isExpired(); });

    // 命中特效寿命递减与清理（批五A）：与飘字同一条门控——暂停/结束时
    // 本函数提前返回，特效随世界一起冻结；寿命归零即移除（渲染渐隐）
    for (MonsterHitFx& fx : m_monsterHitFxs)
    {
        fx.left = std::max(fx.left - ctx.deltaTime, 0.0f); // 递减并夹到 0
    }
    std::erase_if(m_monsterHitFxs, [](const MonsterHitFx& fx) { return fx.left <= 0.0f; });

    // 鼠标世界坐标（批二）：把本帧鼠标的「屏幕位置」经相机换算到世界，
    // 注入玩家作为朝向翻转的基准——屏幕->世界的换算收敛在场景层，
    // 玩家与相机互不认识（无输入系统时保持朝向不动，与旧行为等价）
    if (ctx.input != nullptr)
    {
        const Vec2 mouseScreen = ctx.input->mousePosition();                           // 鼠标屏幕坐标快照
        const Vec2 mouseWorld = m_camera.screenToWorld(mouseScreen.x, mouseScreen.y);  // 换算到世界坐标
        m_player.setAimTarget(mouseWorld.x, mouseWorld.y);                             // 注入：翻转判定参考它
    }

    // 刷新「怪物实心盒」快照并注入玩家（批四·体积碰撞）：玩家移动把存活
    // 怪物的碰撞盒当墙——撞上滑停、不可重合（额外实心判据见 slideMoveEx）。
    // 快照取本步开头位置：怪物随后才更新，玩家面向的阻挡以本步初始局面为准
    m_monsterSolidBoxes.clear();
    for (const Monster& monster : m_monsters)
    {
        if (monster.isAlive())
        {
            m_monsterSolidBoxes.push_back(monster.collisionRect()); // 脚盒快照（世界坐标）
        }
    }
    m_player.setSolidRects(m_monsterSolidBoxes.data(),
                           static_cast<int>(m_monsterSolidBoxes.size()));

    // 玩家使用上下文更新：内部按 ctx.input 的移动动作渐近移动（批二起经
    // World::slideMove 的「分轴 + 撞墙滑动」，不再做窗口 clamp）、按注入的
    // 瞄准点切换朝向翻转，并推进攻击计时
    m_player.update(ctx);

    // 玩家矩形只取一次：本步的碰撞检测、怪物追踪与相机跟随共用一个快照
    const SDL_FRect playerRect = m_player.rect();

    // 相机（批二核心）：平滑跟随玩家中心（指数平滑 camera_smooth_time）。
    // 位置在固定步更新（渲染直接用当前相机位置）；暂停/结束时本函数提前
    // 返回，相机随世界一起冻结——规格的「冻结（相机不更新）」由此保证
    m_camera.follow(playerRect.x + playerRect.w * 0.5f, // 目标中心 x（玩家中心）
                    playerRect.y + playerRect.h * 0.5f, // 目标中心 y
                    ctx.deltaTime,                      // 固定步长（秒）
                    m_cameraSmoothTime);                // 平滑时间常数（秒）

    // 区块激活窗口推进（相机移动后）：进入窗口的区块立即烘焙（日志
    // 「区块 (x,y) 加载」）、滑出窗口的区块释放（日志「区块 (x,y) 卸载」）
    m_world.updateActiveChunks(m_renderer, m_camera);

    // 主动攻击结算：每个固定步按「左键按住 + 冷却就绪」尝试一次横扫——
    // 方向取鼠标瞄准（不索敌、挥空也出招），命中由扇形判定逐只结算
    resolvePlayerAttack(ctx, playerRect);

    // 怪物刷新（批三正式版：激活区块内随机 walkable 点持续刷新 + 随时间增强）：
    // 局内秒数累计 + 到点生成。存活计时与怪物难度计时共用同一时间基准
    // （固定步长，暂停时冻结）
    m_survivalSeconds += ctx.deltaTime; // 存活时间正计时（HUD/结算展示）
    m_monsterElapsed += ctx.deltaTime;  // 局内秒数：难度曲线与刷新节奏的计时基准
    if (m_monsterElapsed >= m_monsterNextSpawnTime)
    {
        // 排定下一次尝试：间隔按当前局内时间收紧（下限 1s）。
        // 固定步长（1/120s）保证一步至多跨过一个刷新点，无需 while 循环
        m_monsterNextSpawnTime += currentSpawnInterval();

        // 数量上限当前值：初始值 + 每 20s +2，封顶 12（池大小）
        if (aliveMonsterCount() < currentMonsterCap())
        {
            // 从池中找一个空槽位（优先复用已死亡怪物的槽位）
            for (Monster& monster : m_monsters)
            {
                if (monster.isAlive())
                {
                    continue; // 在场的槽位跳过，继续找空位
                }

                // 找落点 + 换皮 + 落位（规则见 spawnMonsterAtSlot 注释）。
                // 找不到落点（重试上限内）：本步不生——刷新时刻表已推进，
                // 不会因此卡住（下一次刷新时刻自然再试）
                if (spawnMonsterAtSlot(monster))
                {
                    ++m_monsterSpawnedTotal; // 累计生成数 +1（日志编号自增）

                    // 冒烟观测点：怪物出现日志（含变体名，自动化测试据此断言
                    // 等权随机与三变体生效）
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "怪物出现（第 %d 只，%s）",
                                m_monsterSpawnedTotal, monsterTypeName(monster.type()));
                }
                break; // 本步至多生成一只（无论成功与否）
            }
        }
    }

    // 难度曲线（速度爬升）：每只怪物的本步追击速度 = 自身变体基础速度
    // （史莱姆 90 / 哥布林 110 / 骷髅 140） + 每秒成长 x 局内已过秒数，
    // 并统一封顶 kMonsterMaxSpeed（260——低于玩家速度，保证跑得掉）

    // 玩家碰撞盒（脚部小盒，批四）：玩家已 update 完、本步位置不再变化——
    // 怪物追踪/攻击接触判定与体积碰撞共用这一份快照（与渲染用的
    // playerRect 分开：这里是物理口径）
    const SDL_FRect playerBox = m_player.collisionRect();

    // 把玩家盒注入怪物（批四·体积碰撞）：怪物移动把玩家当实心——
    // 追击/回巢/游荡的滑动判定与攻击动作都不与其重合。
    // 注入覆盖全部存活槽位（含本步冻结的非激活怪：冻结期间不动，
    // 但保证下次激活第一步起就有正确的实心口径）
    for (Monster& monster : m_monsters)
    {
        if (monster.isAlive())
        {
            monster.setPlayerBlocker(playerBox); // 本步玩家脚盒快照
        }
    }

    // 怪物追踪：每个存活怪物注入「本步目标 + 本步速度」后更新。
    // 「场景注入、实体执行」的分工让难度规则完全留在本层。
    // 批二门控：非激活区块内的怪物「暂停更新」（不删除、位置冻结——
    // 玩家回到该区域时恢复；激活窗口最多 3x3，遍历 12 槽位的开销可忽略）
    for (Monster& monster : m_monsters)
    {
        if (!monster.isAlive())
        {
            continue; // 空槽位不更新（也不渲染/碰撞）
        }

        // 怪物中心：激活门控与命中判定的共同参照
        const SDL_FRect monsterRect = monster.rect();                        // 本步怪物矩形快照
        const float monsterCenterX = monsterRect.x + monsterRect.w * 0.5f;   // 中心 x
        const float monsterCenterY = monsterRect.y + monsterRect.h * 0.5f;   // 中心 y

        // 非激活区：跳过本步更新（动画/移动/红染计时全部冻结——暂停语义）
        if (!m_world.isPointInActiveChunks(monsterCenterX, monsterCenterY))
        {
            continue; // 非激活：本步不推进（渲染端同样跳过）
        }

        // 本步速度 = 变体基础速度 + 成长 x 局内秒数（逐只计算：三变体基础值不同），
        // 封顶 kMonsterMaxSpeed 后注入
        const float speed = std::min(
            monster.baseSpeed() + m_monsterSpeedGrowth * m_monsterElapsed,
            kMonsterMaxSpeed);

        monster.setTarget(playerBox); // 注入追踪目标（批四：玩家碰撞盒 → 中心点 + 完整盒）
        monster.setChaseSpeed(speed); // 注入本步速度（难度曲线：随时间爬升）
        monster.update(ctx);          // 执行：移动（碰撞滑动）+ 动画 + 红染/残影 + 攻击状态机

        // ---- 攻击结算消费（批四·蓄力前扑，替换旧「纯接触即伤」）----
        // 每只怪物每步至多发布一条结果（consumeAttackResult 返回并清除）
        const MonsterAttackResult attack = monster.consumeAttackResult();
        if (attack == MonsterAttackResult::Miss)
        {
            // 冒烟观测点：蓄力落空（玩家在躲避窗口内离开了接触范围）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "怪物攻击落空（%s）", monsterTypeName(monster.type()));
        }
        else if (attack == MonsterAttackResult::Hit)
        {
            // 玩家无敌期内：本次打击无效（无伤害/无日志/无击退/无特效/无
            // 回弹）——「受击保护窗口」的语义由「跳过结算」表达（怪物照常
            // 进冷却）。批五A：静默保护——跳过即完全无反馈，不再有闪烁
            if (m_playerInvincibleLeft <= 0.0f)
            {
                // 1) 扣生命（夹到 0：日志展示永远不为负）
                m_playerHp = std::max(m_playerHp - m_monsterDamage, 0);
                // 冒烟观测点：攻击命中（含变体名与扣血明细——断言点）
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "怪物攻击命中（%s，扣 %d 点生命，剩余 %d/%d）",
                            monsterTypeName(monster.type()),
                            m_monsterDamage, m_playerHp, m_playerMaxHp);

                // 2) 打击音效（低频闷响）：受击反馈的听觉层（视觉 = 击退 + 红染）
                m_audio->play(SoundId::Hit);

                // 3) 玩家无敌：从下一步起攻击结算跳过（批五A：静默——无任何闪烁）
                m_playerInvincibleLeft = kPlayerInvincibleSeconds;

                // 4) 玩家受击击退 + 红染（批五A）：沿「怪物中心 -> 玩家中心」
                //    方向被推开（初速 480px/s 指数衰减、约 42px / 0.18s，
                //    期间输入被抑制；位移走碰撞滑动不穿墙）。怪物中心取
                //    「回弹之前」的本步位置——打击来源的真实落点
                const SDL_FRect monsterCol = monster.collisionRect();              // 怪物脚盒（本步位置）
                const float monsterCenterX = monsterCol.x + monsterCol.w * 0.5f;   // 怪物盒中心 x
                const float monsterCenterY = monsterCol.y + monsterCol.h * 0.5f;   // 怪物盒中心 y
                m_player.applyHitKnockback(monsterCenterX, monsterCenterY);        // 以怪物为来源推开玩家

                // 5) 玩家盒中心快照（以下两处消费：特效中点与回弹方向）——
                //    都取「命中瞬间」的接触口径（回弹瞬移之后位置已变）
                const float playerCenterX = playerBox.x + playerBox.w * 0.5f; // 玩家盒中心 x
                const float playerCenterY = playerBox.y + playerBox.h * 0.5f; // 玩家盒中心 y

                // 6) 接触点命中特效（批五A）：爪痕/爆点在双方中心的中点浮现
                //    （接触处的近似——贴身对碰时中点即接触带），0.15s 渐隐
                spawnMonsterHitFx((playerCenterX + monsterCenterX) * 0.5f,
                                  (playerCenterY + monsterCenterY) * 0.5f);

                // 7) 怪物小幅回弹（约 40px；批四口径）：沿「玩家盒中心 ->
                //    怪物中心」方向瞬移；knockback 内部做世界可走校验
                //    （被墙挡逐级缩短）并快照防拉丝
                monster.knockback(playerCenterX, playerCenterY,
                                  Monster::kAttackRecoilDistance);

                // 8) 生命归零：本步提前结束怪物遍历（GameOver 由循环后的
                //    统一检查收口；后续怪物不再留下会结算的 Hit）
                if (m_playerHp <= 0)
                {
                    break;
                }
            }
        }
    }

    // ---- 体积碰撞收尾（批四）----
    // 1) 兜底分离：任何来源产生的「怪物与玩家重叠」（正常热更顺序之外——
    //    如击退/回弹瞬移落点恰好压在玩家上、上一帧的遗留）按最小分离量
    //    把怪物推出（玩家优先不动；被墙挤死的死角原地等下一帧）
    pushMonstersOutOfPlayer(playerBox);

    // 2) 怪物↔怪物轻量软分离：两两重叠的按最小分离量各退一半——
    //    「不完美但稳定」的轻量口径（实现自定见头文件注释，报告说明）
    separateOverlappingMonsters();

    // 经验宝石（批四·自动吸收）：注入吸收目标 -> 追踪位移 -> 玩家盒
    // 重叠即吸收（+10 经验）。玩家无需走动——掉落即朝玩家飞
    const float playerCenterX = playerBox.x + playerBox.w * 0.5f; // 吸收目标 x（玩家脚盒中心）
    const float playerCenterY = playerBox.y + playerBox.h * 0.5f; // 吸收目标 y
    for (XpGem& gem : m_gems)
    {
        // 注入本步吸收目标（玩家在移动——每步刷新，目标恒为当前脚下）
        gem.setHomingTarget(playerCenterX, playerCenterY);

        // 推进旋转动画 + 追踪位移（速度/加速度曲线在 XpGem 内，纯函数积分）
        gem.update(ctx);

        // 抵达判定：与玩家碰撞盒（脚盒）相交即吸收——宝石朝脚下飞，
        // 必然命中（单步位移 5.8~7.5px，不会穿透 32x26 的盒）
        if (!gem.isPicked() && rectsOverlap(playerBox, gem.rect()))
        {
            gem.markPicked(); // 标记：本次遍历结束后统一清理

            // 经验结算：进度累计 + 按曲线循环结算 + 升级反馈与卡牌欠账
            //（可能连升多级——grantXp 内部循环处理）。
            // M2：经验值经「慧眼」卡乘算（1 + 20% x 层数，四舍五入；层数 0
            // 时原值——纯函数见 UpgradeCard.h，选择面板里选到慧眼后立即生效）
            grantXp(cardInsightXpValue(kGemXpValue, m_cardLevels[static_cast<int>(CardId::Insight)]));

            // 吸收音效沿用 coin.wav（与旧金币收集同一听觉反馈）
            m_audio->play(SoundId::Coin);

            // 冒烟观测点：吸收日志（批四口径：由「拾取经验宝石」改为
            // 「经验被吸收」——自动吸收语义；进度/需求/等级字段不变）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "经验被吸收（进度 %d/%d，等级 %d）",
                        m_xp, m_levelCurve.needed(m_level), m_level);
        }
    }
    // 统一清理已被吸收的宝石（保持其余元素的地址不变）
    std::erase_if(m_gems, [](const XpGem& gem) { return gem.isPicked(); });

    // 金币（批三掉落、批四磁吸版）：范围外静止；玩家中心进入
    // coin_magnet_radius 即磁化吸附，触及（玩家盒重叠）即拾取
    for (Coin& coin : m_coins)
    {
        // 磁吸触发判定（未磁化时）：硬币中心与玩家盒中心距离 <= 半径
        //（含边界）即磁化；一次触发、永久保持（跑出范围也不半路丢下）
        if (!coin.magnetized())
        {
            const SDL_FRect coinRect = coin.rect();                             // 硬币快照
            const float dx = coinRect.x + coinRect.w * 0.5f - playerCenterX;    // 与玩家中心横向差
            const float dy = coinRect.y + coinRect.h * 0.5f - playerCenterY;    // 纵向差
            // 平方比较（省一次开方）；半径 0 时只有完全贴中心才触发（显式语义）
            if (dx * dx + dy * dy <= m_coinMagnetRadius * m_coinMagnetRadius)
            {
                coin.beginMagnet(); // 磁化：此后每步追踪玩家脚下
            }
        }

        // 磁化后注入本步目标（玩家会移动——每步刷新）再推进
        if (coin.magnetized())
        {
            coin.setMagnetTarget(playerCenterX, playerCenterY); // 目标 = 当前脚下
        }
        coin.update(ctx); // 旋转动画 + （磁化时）追踪位移（速度曲线在 Coin 内）

        // 触及即拾取：与玩家碰撞盒（脚盒）相交 -> 标记 + 局内货币累计 +
        // 播放「叮」音效；实际移除放在循环之后统一做（避免边遍历边删除）
        if (!coin.isPicked() && rectsOverlap(playerBox, coin.rect()))
        {
            coin.markPicked(); // 标记：本次遍历结束后统一清理

            // 局内货币累计：本局 +1（本批只储备不消费——为将来商店/
            // 强化体系铺路；restart 清零）
            ++m_coinCount;

            // 拾取音效沿用 coin.wav（与宝石吸收同一听觉反馈，语义恰好吻合）
            m_audio->play(SoundId::Coin);

            // 冒烟观测点：拾取金币日志（本局累计——冒烟断言 HUD 计数增长）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "拾取金币（本局累计 %d）", m_coinCount);
        }
    }
    // 统一清理已被拾取的金币（保持其余元素的地址不变）
    std::erase_if(m_coins, [](const Coin& coin) { return coin.isPicked(); });

    // 说明（批四）：旧「纯接触即伤」的接触伤害块已删除——怪物攻击改为
    // 「蓄力前扑」状态机（见上方怪物循环内的结果消费）：接触范围的伤害
    // 不再由「一步一碰」直接结算，而是怪物出招命中才扣血

    // 生命归零：进入 GameOver（唯一入口——GameOver 仅因生命归零触发）
    if (m_playerHp <= 0)
    {
        m_state = PlayState::GameOver; // 冻结世界，等待玩家按 R 重开
        // 播放结束下行音（gameover）：与状态切换一一对应（R 重开不重复触发）
        m_audio->play(SoundId::GameOver);
        // 冒烟观测点：日志标记本局结束（自动化测试据此确认 GameOver 已到达）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "生命归零，本局结束");

        // 最佳战绩结算（仅完整对局走到这里）：比较三项纪录，破了就更新并
        // 立即写盘、播 record 音效。中途 M 退出（Paused 分支）不经过本函数
        settleRecords();
    }
}

// 主动攻击入口（每个固定步调用一次）：「左键按住 -> 方向 -> 出手 -> 扫程推进」。
// 分工：本函数只处理「要不要出手、朝哪出手」；命中结算在 advanceSwingSweep
//（刀锋逐帧扫过判定）。注意：未出手时也要调用 advanceSwingSweep——已出的
// 刀必须扫完（表现与判定同生命周期，松开左键也不例外）
void PlayScene::resolvePlayerAttack(const UpdateContext& ctx, const SDL_FRect& playerRect)
{
    // 无输入系统（旧调用方）：既没有鼠标位置也没有攻击动作，本步不处理
    if (ctx.input == nullptr)
    {
        return;
    }

    // 主动攻击门控：左键没按住就不尝试出手——「不按不挥」是本批与自动
    // 攻击的根本区别。held 同时覆盖「单击挥一次」（按住的那一个固定步）
    // 与「按住连扫」（节奏由 Player 的冷却门控制）
    if (ctx.input->held("attack"))
    {
        // 玩家中心：瞄准向量的起点（扇形判定的圆心在扫程推进内按步取）
        const float centerX = playerRect.x + playerRect.w * 0.5f; // 中心 x（世界坐标）
        const float centerY = playerRect.y + playerRect.h * 0.5f; // 中心 y（世界坐标）

        // 鼠标世界坐标（批二）：输入系统给的是「屏幕坐标」，经相机
        // screenToWorld 换算到世界——「鼠标对准屏幕哪一点、刀就扫向
        // 世界上哪一点」，相机滚动多少都准确（瞄准与画面同一变换）
        const Vec2 mouseScreen = ctx.input->mousePosition();                          // 鼠标屏幕坐标快照
        const Vec2 mouseWorld = m_camera.screenToWorld(mouseScreen.x, mouseScreen.y); // 换算到世界坐标

        // 瞄准向量 = 玩家中心 -> 鼠标位置（世界坐标）：
        float aimX = mouseWorld.x - centerX; // 水平分量（正右负左）
        float aimY = mouseWorld.y - centerY; // 垂直分量（正下负上）

        // 零向量防御（鼠标恰好压在玩家中心）：没有方向可言，回退到玩家
        // 当前面向（右为 +1、左为 -1），保证横扫方向可预期且不产生 NaN
        if (aimX * aimX + aimY * aimY < kAimEpsilonSquared)
        {
            aimX = m_player.flipped() ? -1.0f : 1.0f; // 面向左侧/右侧
            aimY = 0.0f;                              // 水平方向
        }

        // 出手尝试：冷却未到返回 false（本步不出手；上一刀的扫程推进
        // 照常在下半段处理——它有自己的生命周期）
        if (m_player.tryAttack(aimX, aimY))
        {
            // ---- 新扫程开始：场景侧状态全部对齐到「刀锋起点」----
            m_sweepRunning = true;  // 收尾补段与清理的判据：扫程从现在开始
            m_sweepHitCount = 0;    // 本次扫程命中计数清零
            m_sweepHit.fill(false); // 本扫程命中记录清零（按怪物池槽位）

            // 刀锋起点角 = 缓出曲线在 progress=0 处的角（aim − 半张角）。
            // 方向取「出手后的挥砍方向」——Player 内刚归一化并保存，
            // 判定/渲染同一数据源（两次计算不会产生偏差）
            const Vec2 dir = m_player.swingDirection();         // 判定中轴（单位向量）
            const float aimRad = std::atan2(dir.y, dir.x);      // 瞄准角（弧度）
            m_sweepPrevAngleRad = bladeAngleAt(aimRad, 0.0f, m_attackArc); // 起点角

            // 冒烟观测点：出手日志（与命中日志配对做时序断言——命中 t
            // 必须晚于出手 t，证明伤害随刀锋到达而非出手瞬发）。
            // t 取三位小数：1/120 秒的固定步差异在日志上可分辨
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "横扫出手（t=%.3f）", m_survivalSeconds);
        }
    }

    // 扫程推进（与本次是否出手、左键是否仍按住无关）：出了手的刀必须
    // 扫完——每个固定步把「上一步刀锋角 -> 本步刀锋角」之间扫过的怪物结算
    advanceSwingSweep(playerRect);
}

// 扫程推进（每个固定步调用）：刀锋同步命中的核心。
// 扫程活跃期间，取「上一步刀锋角 -> 本步刀锋角」的扫过区间，对每只
// 存活且本扫程未命中的怪物：中心在扇形内（距离/中轴夹角）且角度落在
// 本步扫过区间内 -> 此刻结算（伤害/飘字/红染/击退/死亡掉落）。
// 收尾步（剩余归零后的第一次推进）把最后一段补扫到终点角、清理命中
// 记录并给出「挥空」观测点。多目标命中按刀锋先后自然错开若干固定步
void PlayScene::advanceSwingSweep(const SDL_FRect& playerRect)
{
    // 快照本步的扫程活跃状态：后面的收尾判断用它（不能用推进后的状态
    // 反推——那是「下一步」的视角）
    const bool active = m_player.swingActive(); // 本步扫程是否活跃

    // 既不在扫程中、也没有待收尾的扫程：本步无事（最常见路径，直接返回）
    if (!active && !m_sweepRunning)
    {
        return;
    }

    // 玩家中心：本步的判定圆心（与扇形判定的圆心同源——玩家中心）
    const float centerX = playerRect.x + playerRect.w * 0.5f; // 中心 x
    const float centerY = playerRect.y + playerRect.h * 0.5f; // 中心 y

    // 本步刀锋角：活跃中取当前进度；收尾步（剩余已归零）补扫到终点角
    //（progress=1）——刀锋绝不「差最后一段没扫完」。与渲染共用
    // bladeAngleAt / swingProgress01——画到哪、打到哪就是这条数据链
    const Vec2 dir = m_player.swingDirection();    // 判定中轴（单位向量）
    const float aimRad = std::atan2(dir.y, dir.x); // 瞄准角（弧度）
    const float bladeNow = bladeAngleAt(aimRad,
                                        active ? m_player.swingProgress01() : 1.0f,
                                        m_attackArc); // 本步刀锋角（收尾补到终点）

    // 本步扫过区间 = [上一步角, 本步角]。浮点抖动守卫：progress 单调，
    // bladeNow 理论上不小于上一步，但舍入可能造成微回退——取两者较大，
    // 把区间钳成零宽；否则 isAngleInSweep 的正向增量语义会把一个
    // 微小的负增量读成「几乎整圈扫过」，导致全场误命中
    const float bladeFrom = m_sweepPrevAngleRad;      // 区间起点（上一步刀锋角）
    const float bladeTo = std::max(bladeNow, bladeFrom); // 区间终点（抖动守卫）

    // 逐只扫描：存活 + 本扫程未命中 + 扇形内 + 角度在本步扫过区间内。
    // 带下标循环：命中记录与怪物池槽位一一对应
    for (size_t i = 0; i < m_monsters.size(); ++i)
    {
        // 槽位引用：命中记录、结算、击退都作用在同一只怪上
        Monster& monster = m_monsters[i];

        // 空槽位或本扫程已命中：跳过（一次刀锋对同一只怪至多结算一次）
        if (!monster.isAlive() || m_sweepHit[i])
        {
            continue;
        }

        // 怪物矩形只取一次：中心点与后续掉落位置共用同一份快照
        const SDL_FRect monsterRect = monster.rect();
        const float monsterCenterX = monsterRect.x + monsterRect.w * 0.5f; // 中心 x
        const float monsterCenterY = monsterRect.y + monsterRect.h * 0.5f; // 中心 y

        // 条件一：中心在扇形内（距离 ≤ attack_range 且与中轴夹角 ≤ 半张角；
        // 纯函数边界行为有单测覆盖，见 AttackArc.h）
        if (!isPointInAttackArc(Vec2{ centerX, centerY }, dir,
                                Vec2{ monsterCenterX, monsterCenterY },
                                m_attackRange, m_attackArc))
        {
            continue; // 弧外：本步不结算（刀锋扫到时还会再看——怪会移动）
        }

        // 条件二：怪物角度落在本步扫过的角区间内（刀锋此刻正好扫到它）。
        // 条件一已经保证「最终扫得到」，本条件决定「是不是这一步」
        const float monsterAngleRad = std::atan2(monsterCenterY - centerY,
                                                 monsterCenterX - centerX); // 怪物方位角
        if (!isAngleInSweep(monsterAngleRad, bladeFrom, bladeTo))
        {
            continue; // 刀锋本步还没扫到（或已扫过）：留给后续步骤
        }

        // ---- 此刻命中的完整结算（与「出手瞬发结算」同一套反馈）----
        m_sweepHit[i] = true; // 标记：本扫程不再重复命中该槽位
        ++m_sweepHitCount;    // 本次扫程命中计数 +1（收尾判挥空用）

        // 1) 扣血（红染在 takeDamage 内自动触发；返回是否致死）
        const bool died = monster.takeDamage(m_attackDamage);

        // 2) 伤害飘字：在怪物位置弹出「-当前伤害值」（上浮+淡出）。
        //    多目标按刀锋先后逐条弹出——命中时刻错开，飘字也自然错开
        char damageText[32]; // 容纳 "-" + 最长伤害数字，绰绰有余
        SDL_snprintf(damageText, sizeof(damageText), "-%d", m_attackDamage);
        spawnFloatingText(damageText, monsterCenterX, monsterCenterY - 20.0f,
                          SDL_Color{ 255, 255, 255, 255 }); // 白色：与金色升级飘字区分

        // 冒烟观测点：横扫命中日志（含剩余生命与局内时间——自动化测试
        // 据它断言「命中晚于出手」「多目标错开」；t 取三位小数以分辨
        // 1/120 秒固定步的时间差）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "横扫命中（剩余生命 %d，t=%.3f）", monster.hp(), m_survivalSeconds);

        if (died)
        {
            // 击杀结算：计数 +1、原地掉落经验宝石与金币（批三双掉落）。
            // 金币数按变体查表（史莱姆 1 / 哥布林 2 / 骷髅 1）
            ++m_killCount; // 本局击杀 +1（HUD/结算使用）
            dropGemAt(monsterCenterX, monsterCenterY);           // 掉落宝石：坐标即怪物中心
            const int coinCount = monster.coinDrop();            // 本变体的金币掉落数
            dropCoinsAt(monsterCenterX, monsterCenterY, coinCount); // 掉落金币（散开可见）

            // 冒烟观测点：击杀日志（自动化测试据此断言击杀链路）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "击杀怪物（累计 %d 杀）", m_killCount);
            // 冒烟观测点：金币掉落日志（数量按变体——冒烟对照断言用）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "金币掉落（%d 枚，%s）", coinCount, monsterTypeName(monster.type()));
        }
        else
        {
            // 未死：微击退（沿「玩家中心 -> 怪物中心」方向远离玩家）。
            // 距离来自 attack_knockback（默认 30——由原 60 减半的「微」
            // 击退，保持追击压力、方便下一轮连扫补刀）；落点经世界可走校验
            monster.knockback(centerX, centerY, m_attackKnockback);
        }
    }

    // 区间推进：本步终点成为下一步区间的起点（刀锋只前进、不回头）
    m_sweepPrevAngleRad = bladeTo;

    // 扫程收尾：本步剩余归零后的第一次推进——上面的补段已完成最后
    // 一段区间，现在清理状态并给出「挥空」观测点
    if (!active && m_sweepRunning)
    {
        m_sweepRunning = false; // 场景侧标记归还：下次出手重新开启

        // 整刀一只也没碰到：挥空日志（「挥空也出招」的证据点——出手仍
        // 消耗冷却、播放完整表现；冷却/表现由 Player 负责，与本清理无关）
        if (m_sweepHitCount == 0)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "横扫挥空（未命中任何怪物，t=%.3f）", m_survivalSeconds);
        }

        // 命中记录清零：为下一扫程腾出全新状态（也避免残留旧标记）
        m_sweepHit.fill(false); // 全槽位清空（与出手时的清零成对出现）
        m_sweepHitCount = 0;    // 命中计数清零
    }
}

// 掉落一颗经验宝石到 (x, y)：两个坐标指「怪物中心」，内部换算为宝石左上角
void PlayScene::dropGemAt(float x, float y)
{
    // 在链表尾部原位构造（节点式容器：插入不搬动既有元素，
    // 实体持有的纹理指针绝不会因扩容被复制——这是选 std::list 的原因）
    m_gems.emplace_back();
    XpGem& gem = m_gems.back(); // 引用新宝石：完成加载与落位

    // 纹理加载（三级回退在实体内部；失败只记录不中断——该宝石渲染时跳过）
    if (!gem.load(m_renderer))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "创建经验宝石纹理失败：%s", SDL_GetError());
    }

    // 落位：坐标以怪物中心给出，换算到宝石左上角；setPosition 内部同步插值快照
    gem.setPosition(x - gem.width() * 0.5f, y - gem.height() * 0.5f);
}

// 掉落 count 枚金币到 (x, y) 周围（批三）：坐标指「怪物中心」
void PlayScene::dropCoinsAt(float x, float y, int count)
{
    // 非正数（未来改表/调用方错误）：无掉落，直接返回（防御）
    if (count <= 0)
    {
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        // 在链表尾部原位构造（与宝石同一原因：节点式容器不搬动既有元素，
        // 实体的纹理指针绝不会因扩容被复制）
        m_coins.emplace_back();
        Coin& coin = m_coins.back(); // 引用新金币：完成加载与落位

        // 纹理加载（三级回退在实体内部；失败只记录不中断——该金币渲染时跳过）
        if (!coin.load(m_renderer))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "创建金币纹理失败：%s", SDL_GetError());
        }

        // 散开落位：多枚金币绕掉落中心均匀分布在小半径圆上（2 枚时
        // 左右各一、一眼数得清）；单枚（count==1）时半径取 0 = 落中心
        const float angle = 6.283185307179586f * static_cast<float>(i) / static_cast<float>(count); // 每枚的方位角
        const float offsetX = std::cos(angle) * kCoinDropScatter; // 横向偏移
        const float offsetY = std::sin(angle) * kCoinDropScatter; // 纵向偏移

        // 落位：坐标以怪物中心给出（含散开偏移），换算到金币左上角；
        // setPosition 内部同步插值快照（瞬移类操作防拉丝）
        coin.setPosition(x + offsetX - coin.width() * 0.5f,
                         y + offsetY - coin.height() * 0.5f);
    }
}

// 生成一处命中特效（批五A）：怪物攻击命中时由场景调用（契约见头文件）
void PlayScene::spawnMonsterHitFx(float centerX, float centerY)
{
    // 原位构造一条特效记录（vector 元素为纯数据，无指针成员——
    // 扩容搬移安全；频率低、容量自然稳定）
    m_monsterHitFxs.emplace_back();
    MonsterHitFx& fx = m_monsterHitFxs.back(); // 引用新记录：完成落位
    fx.x = centerX;                        // 特效中心 x（接触点近似）
    fx.y = centerY;                        // 特效中心 y（同上）
    fx.left = kMonsterHitFxSeconds;        // 寿命置满（0.15s 渐隐）

    // 冒烟观测点：命中特效日志（连拍核验的时序对齐依据——特效与
    // 「怪物攻击命中」日志同一步产生）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "命中特效（爪痕，位置 %.1f,%.1f）", centerX, centerY);
}

// 经验结算：把 amount 点经验并入当前进度，按升级曲线循环结算（一枚宝石
// 可连升多级），并逐级发放升级反馈（飘字/瞬闪/音效）。曲线计算走
// LevelCurve 纯函数——本函数只负责「把结果落实到局面」。
// M2：每级自动强化（旧三项属性成长）已退役——升级成长改为登记欠账并弹出
// 三选一卡牌（选择生效后经 applyCardEffects 统一落地；见本函数末尾）
void PlayScene::grantXp(int amount)
{
    // 曲线结算：返回新等级 / 当前级新进度 / 连升级数（纯函数，输入输出皆为数据）
    const LevelUpResult result = m_levelCurve.apply(m_level, m_xp, amount);

    // 记录升级前的等级：下面按「旧+1 .. 新」逐级发反馈（飘字逐级错开）
    const int oldLevel = m_level;

    // 先落定结算结果：反馈循环与结束界面读取的都是新状态
    m_xp = result.xp;       // 当前级进度（恒小于本级需求）
    m_level = result.level; // 新等级（等级只增不减）

    // 逐级发放升级反馈（一枚宝石连升多级时每级都发一份）。说明（M2）：
    // 属性成长不在这里发生——只有反馈；成长全部来自卡牌选择
    for (int levelReached = oldLevel + 1; levelReached <= m_level; ++levelReached)
    {
        // 1) 冒烟观测点：升级日志（规格文案「升级！等级 N」——自动化测试断言点）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "升级！等级 %d", levelReached);

        // 2) 升级飘字（金色）挂在玩家上方；连升多级时逐条向上错开
        //    （每条偏移 26 像素，避免多级连升时文字完全重叠）
        const SDL_FRect playerRect = m_player.rect(); // 玩家当前位置（左上角）
        spawnFloatingText("升级！",
                          playerRect.x + playerRect.w * 0.5f,               // 水平中心
                          playerRect.y - 24.0f - (levelReached - oldLevel - 1) * 26.0f, // 头顶上方
                          SDL_Color{ 255, 215, 0, 255 },                    // 金色：升级高光
                          kLevelupTextLifeSeconds);                         // 比伤害数字停留更久
    }

    // 有升级才触发 HUD 瞬闪：短暂窗口内等级数字金白交替（渲染端消费本计时）。
    // 三选一升级卡（M2）同点接入：连升的每一级记一笔欠账（逐次弹选）、
    // 立即打开第一组选择面板（抽卡 + 进入 LevelUp——世界从下一固定步冻结）；
    // 升级音效在此按「本次升级」播放一次（连升多级不叠放）
    if (result.levelsGained > 0)
    {
        m_levelFlashLeft = kLevelFlashSeconds;  // 重置为满值（连升多级只刷一次时长）
        m_levelupSfx.play();                    // 升级音效（游戏层直读 levelup.wav；不可用安全静默）
        m_choiceQueue.add(result.levelsGained); // 欠账登记：每升一级欠一次选择
        openCardPanel();                        // 打开选择面板（正常路径：抽到卡并进入 LevelUp）
    }
}

// 生成一条漂浮文字：同屏超上限先移除最老的一条，再原位构造新的
void PlayScene::spawnFloatingText(const char* text, float centerX, float topY,
                                  SDL_Color color, float lifeSeconds)
{
    // 上限控制：超标时丢弃最老一条（链表头）——「大量同屏不崩」的兜底，
    // 正常节奏下（冷却 0.4s、寿命 0.8s）根本到不了上限
    if (m_floatingTexts.size() >= static_cast<size_t>(kMaxFloatingTexts))
    {
        m_floatingTexts.pop_front(); // 移除最老（先创建的在前，按时间序淘汰）
    }

    // 原位构造新飘字（链表节点式容器，插入不搬动既有元素）并初始化
    m_floatingTexts.emplace_back(); // 默认构造：寿命 0（未激活）
    m_floatingTexts.back().spawn(text, centerX, topY, color, lifeSeconds); // 写入全部字段
}

// 最佳战绩结算：仅完整对局（生命归零）调用一次。比较三项纪录，任一被破
// 就更新内存快照并立即写盘、播 record 音效；三项标记供结束界面逐项展示
void PlayScene::settleRecords()
{
    // 存活比较取整秒口径：与 HUD/结算的 mm:ss 展示一致（展示即截断），
    // 避免「界面还是 01:05、纪录却按 65.9 秒」这类不一致
    const int survivalSeconds = static_cast<int>(m_survivalSeconds);

    // 逐项比较：严格大于才算破纪录（平纪录不算）
    m_newKillRecord = m_killCount > m_best.kills;        // 破最高击杀？
    m_newLevelRecord = m_level > m_best.level;           // 破最高等级？
    m_newTimeRecord = survivalSeconds > m_best.seconds;  // 破最长存活？

    // 三项全平/未破：只记一条日志即可，不写盘、不播音效
    if (!m_newKillRecord && !m_newLevelRecord && !m_newTimeRecord)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "本局未破最佳战绩（击杀 %d · 等级 %d · 存活 %d 秒）",
                    m_killCount, m_level, survivalSeconds);
        return;
    }

    // 只更新被破的项（未破项保留历史值）；日志先于赋值——能打出旧纪录对照
    if (m_newKillRecord)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "新纪录！击杀 %d（旧纪录 %d）", m_killCount, m_best.kills);
        m_best.kills = m_killCount; // 写入新纪录
    }
    if (m_newLevelRecord)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "新纪录！等级 %d（旧纪录 %d）", m_level, m_best.level);
        m_best.level = m_level; // 写入新纪录
    }
    if (m_newTimeRecord)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "新纪录！存活 %d 秒（旧纪录 %d 秒）", survivalSeconds, m_best.seconds);
        m_best.seconds = survivalSeconds; // 写入新纪录
    }

    // 立即写盘（规格硬要求：破纪录即时落盘）。失败只警告并在内存里保留
    // 新纪录（SaveData::save 内部已记录原因），本局的展示不受影响
    SaveData::save(m_best);

    // 破纪录上行琶音（record，与游戏其他音效同一听觉体系）
    m_audio->play(SoundId::Record);
}

// 存活怪物计数：刷新上限的门控依据（池最多 12 个槽位，遍历成本可忽略）
int PlayScene::aliveMonsterCount() const
{
    int count = 0; // 存活计数从 0 起
    for (const Monster& monster : m_monsters)
    {
        if (monster.isAlive())
        {
            ++count; // 在场槽位 +1
        }
    }
    return count; // 返回存活总数
}

// 当前数量上限：初始值（monster_max_count）+ 每 20 秒 +2，封顶 kMaxMonsterCap（12）。
// 初始值高于封顶（配置异常/未来池扩容）时以封顶为准——池容量是硬边界
int PlayScene::currentMonsterCap() const
{
    // 按已过「整 20 秒」的段数计算成长（int 截断即段数，如 39s -> 1 段）
    const int grown = static_cast<int>(m_monsterElapsed / kCapGrowthPeriod) * kCapGrowthStep;
    return std::min(m_monsterMaxCount + grown, kMaxMonsterCap); // 封顶后返回
}

// 当前刷新间隔：初始值（monster_spawn_interval，默认 2.5s）随局内时间
// 线性收紧（kSpawnIntervalDecay），下限 kSpawnIntervalFloor（1s）。
// 默认参数下约 60 秒从 2.5 收紧到 1.0，此后维持 1s
float PlayScene::currentSpawnInterval() const
{
    return std::max(m_monsterSpawnInterval - m_monsterElapsed * kSpawnIntervalDecay,
                    kSpawnIntervalFloor);
}

// 为指定空槽位寻找落点并生成怪物（批三正式版出生规则，契约见头文件）
bool PlayScene::spawnMonsterAtSlot(Monster& monster)
{
    // 玩家中心：出生距离校验的圆心（每步重取——玩家在移动，距离随人走）。
    // 批四：取「碰撞盒（脚部）中心」——出生距离与物理判定同一口径
    const SDL_FRect playerBox = m_player.collisionRect();           // 玩家脚盒快照
    const float playerCenterX = playerBox.x + playerBox.w * 0.5f;   // 玩家盒中心 x（世界坐标）
    const float playerCenterY = playerBox.y + playerBox.h * 0.5f;   // 玩家盒中心 y

    // 采样区域 = 激活区块窗口（批三正式版：怪物在激活区块内随机点出生）。
    // 窗口已由 World 回钳在世界内；「区块索引 -> 像素」换算用区块边长
    const float areaMinX = static_cast<float>(m_world.activeMinChunkX()) * World::kChunkPixels; // 区域左边界
    const float areaMinY = static_cast<float>(m_world.activeMinChunkY()) * World::kChunkPixels; // 区域上边界
    const float areaMaxX = static_cast<float>(m_world.activeMaxChunkX() + 1) * World::kChunkPixels; // 区域右边界（含末区块）
    const float areaMaxY = static_cast<float>(m_world.activeMaxChunkY() + 1) * World::kChunkPixels; // 区域下边界

    for (int attempt = 0; attempt < kMonsterSpawnAttempts; ++attempt)
    {
        // 窗口矩形内均匀取点（x/y 各自独立均匀——矩形上面积均匀）
        const float spawnCenterX = areaMinX + SDL_randf() * (areaMaxX - areaMinX); // 候选中心 x
        const float spawnCenterY = areaMinY + SDL_randf() * (areaMaxY - areaMinY); // 候选中心 y

        // 条件一：离玩家中心 >= kMonsterSpawnMinPlayerDistance（防贴脸刷新；
        // 平方比较省一次开方）
        const float dx = spawnCenterX - playerCenterX; // 与玩家的水平差
        const float dy = spawnCenterY - playerCenterY; // 与玩家的垂直差
        if (dx * dx + dy * dy <
            kMonsterSpawnMinPlayerDistance * kMonsterSpawnMinPlayerDistance)
        {
            continue; // 离玩家太近：换一个候选
        }

        // 条件二：怪物「脚部碰撞盒」在候选点必须可走（批四起由整张精灵
        // 改为碰撞盒口径——与移动判定一致，不会把脚生进墙里；精灵身体
        // 贴墙叠压属正常画面）。
        // 说明：采样区域 = 激活窗口，故候选点必在激活区块内（无需另查）
        const SDL_FRect monsterBox =
            monster.collisionRectAt(spawnCenterX - monster.width() * 0.5f,  // 候选精灵左上角 x
                                    spawnCenterY - monster.height() * 0.5f); // 候选精灵左上角 y
        if (!m_world.isRectWalkable(monsterBox))
        {
            continue; // 脚盒落点是墙：换一个候选
        }

        // 两个校验全过：等权随机变体 -> 槽位换皮 -> 落位生成。
        // 落点同时登记为「巢」（spawnAt 内部写入 AI）——出生点即巢，
        // 怪物自此绕它巡逻、以它为栓绳圆心
        const auto variant = static_cast<MonsterType>(
            SDL_rand(static_cast<int>(MonsterType::Count))); // 等权三变体（各 1/3）
        monster.loadVariant(m_renderer, variant);            // 复用槽位换皮（先释放旧纹理）
        monster.spawnAt(spawnCenterX, spawnCenterY);         // 落位（写生命+巢，快照防拉丝）
        return true; // 生成成功：调用方接着写日志/累计
    }

    // 重试耗尽（少见：窗口内可走区域被墙占满等极端情形）：本步不生成。
    // 刷新时刻表已在调用方推进——下一次刷新时刻自然再试，不会死循环
    return false;
}

// 把与玩家碰撞盒重叠的怪物按最小分离量推出（批四·体积碰撞兜底）
void PlayScene::pushMonstersOutOfPlayer(const SDL_FRect& playerBox)
{
    for (Monster& monster : m_monsters)
    {
        // 空槽位跳过：未激活的怪物不参与物理
        if (!monster.isAlive())
        {
            continue;
        }

        // 最小分离量（MTV）：把怪物推出玩家盒的最小位移；未重叠则跳过
        const SeparationResult sep = computeSeparation(monster.collisionRect(), playerBox);
        if (!sep.overlapping)
        {
            continue;
        }

        // 只推怪物、绝不动玩家（「玩家优先不动」的兜底语义）：
        // nudge 内部做墙/玩家校验——被挤死则原地等下一帧
        monster.nudge(sep.pushX, sep.pushY);
    }
}

// 怪物↔怪物轻量软分离（批四）：重叠的按最小分离量各退一半
void PlayScene::separateOverlappingMonsters()
{
    // 两两组合（池 <= 12：最多 66 对，每步一轮的开销可忽略）
    for (size_t i = 0; i < m_monsters.size(); ++i)
    {
        Monster& a = m_monsters[i];
        if (!a.isAlive())
        {
            continue; // 空槽位不参与
        }

        for (size_t j = i + 1; j < m_monsters.size(); ++j)
        {
            Monster& b = m_monsters[j];
            if (!b.isAlive())
            {
                continue; // 空槽位不参与
            }

            // 最小分离量（a 相对 b 的推出向量）：未重叠则跳过
            const SeparationResult sep =
                computeSeparation(a.collisionRect(), b.collisionRect());
            if (!sep.overlapping)
            {
                continue;
            }

            // 各退一半：a 沿推出向量走一半、b 沿反方向走一半（合计 = 完整
            // 分离量）。各自经 nudge 做「墙 + 不推入玩家」校验——被挡的
            // 一侧少退不退（轻量口径：多怪挤压极端局面允许短暂残留）
            const float halfX = sep.pushX * 0.5f; // 各方水平退让量
            const float halfY = sep.pushY * 0.5f; // 各方垂直退让量
            a.nudge(halfX, halfY);   // a：沿推出方向退一半
            b.nudge(-halfX, -halfY); // b：反方向退一半
        }
    }
}

// 重开一局：把整局相关的数据全部恢复到初始状态
void PlayScene::restart()
{
    // 三选一升级卡清零（M2：「R 重开清零全部层数与效果」的层数侧）：
    // 层数与欠账全清、面板卡位清空、1/2/3 去重标记复位——新一局的卡池
    // 从零层开始。卡池随机源不重播种：层数清零后卡池全开，沿用同一序列即可
    for (int& level : m_cardLevels) // 逐卡遍历层数数组
    {
        level = 0; // 该卡层数归零
    }
    m_choiceQueue.clear();             // 欠账清零（未清完的选择不带进新一局）
    m_offeredCards = CardDrawResult{}; // 面板卡位清空（count = 0）
    m_choiceHandled[0] = false;        // 1 号去重标记复位
    m_choiceHandled[1] = false;        // 2 号去重标记复位
    m_choiceHandled[2] = false;        // 3 号去重标记复位

    // 鼠标选择状态复位（批五A）：悬停清空、防误触基线与点击去重标记回
    // 初始值——新一局的面板（首个）打开时重新采样
    m_hoverCard = -1;               // 无悬停高亮残留
    m_cardPanelFresh = true;        // 下个面板打开后采样防误触基线
    m_cardClickNeedRelease = false; // 解锁状态清零（新局从「未等待」起）
    m_cardClickHandled = false;     // 点击去重标记复位

    // 商店面板状态复位（批五B）：新一局的商店从干净状态开始——悬停清空、
    // 提示消息清空、防误触基线与全部去重标记回初始值（首个面板打开时重采样）
    m_shopMessage[0] = '\0';        // 提示消息清空（不带上一局的「已购买」）
    m_hoverShopItem = -1;           // 无悬停高亮残留
    m_shopPanelFresh = true;        // 下个面板打开后采样防误触基线
    m_shopClickNeedRelease = false; // 解锁状态清零（新局从「未等待」起）
    m_shopClickHandled = false;     // 点击去重标记复位
    for (bool& handled : m_shopKeyHandled) // 1~9 去重标记逐项复位
    {
        handled = false; // 该项采样回「未按」
    }
    m_shopHandled = false;          // B 动作去重标记复位

    // 卡牌效果撤销 + 全部数值复位（M2 统一入口）：层数已清零，重算即等价于
    // 「移速/攻击冷却/伤害/范围/张角/击退/生命上限/磁吸」全部回到基准，
    // 并重新注入 Player 实体（移速/冷却/扫角立即恢复配置初值）
    applyCardEffects();

    // 战斗数值复位：生命回满（上限已回基准）、击杀/等级/经验清零、无敌清零
    m_playerHp = m_playerMaxHp;    // 生命回满（上限已在 applyCardEffects 中回到基准）
    m_killCount = 0;               // 击杀清零
    m_level = 1;                   // 等级回到 1（新一局从第 1 级重新累积）
    m_xp = 0;                      // 当前级进度清零
    m_levelFlashLeft = 0.0f;       // 清掉上一局残留的等级瞬闪状态
    m_survivalSeconds = 0.0f;      // 存活计时清零：新一局从 00:00 起算
    m_playerInvincibleLeft = 0.0f; // 新一局从非无敌开始

    // 「新纪录」标记复位：上一局的破纪录展示不带到新一局。
    //（m_best 快照保留不动——它是整局的比较基准，只在结算成功时更新）
    m_newKillRecord = false;
    m_newLevelRecord = false;
    m_newTimeRecord = false;

    // 怪物刷新计时复位：局内秒清零、首刷时刻回到首刷延迟、累计生成数清零
    m_monsterElapsed = 0.0f;                   // 难度计时重新起算
    m_monsterNextSpawnTime = kFirstSpawnDelay; // 首只怪物重新按 2s 延迟出现
    m_monsterSpawnedTotal = 0;                 // 日志编号重新从第 1 只开始

    // 刷新窗口实际尺寸缓存：重开时窗口可能已被调整过大小
    SDL_GetWindowSize(m_window, &m_width, &m_height);

    // 玩家归位到世界中心（批二：世界取代窗口作边界；出生保护区保证可走）。
    // reset 同时清零速度与攻击状态（冷却/挥剑表现），并把动画切回站姿
    const SDL_FPoint spawn = m_world.spawnPointFor(m_player.width(), m_player.height());
    m_player.reset(spawn.x, spawn.y);

    // 相机对新玩家 + 立即刷新激活窗口（玩家可能死在远角，重开后镜头
    // 直接回世界中心；先把窗口刷干净——若等下一个固定步，第一帧可能
    // 画的是旧区域的区块）
    m_camera.setViewport(static_cast<float>(m_width), static_cast<float>(m_height));
    m_camera.snapTo(spawn.x + m_player.width() * 0.5f,
                    spawn.y + m_player.height() * 0.5f);
    m_world.updateActiveChunks(m_renderer, m_camera);

    // 怪物池全部回收：槽位标记未激活（不再更新/渲染/碰撞），
    // 等刷新时刻从空槽位重新生成；despawn 同时清掉追踪目标与红染残留
    for (Monster& monster : m_monsters)
    {
        monster.despawn();
    }

    // 场上经验宝石清空（析构各自释放纹理；renderer 尚在，安全）
    m_gems.clear();

    // 场上金币清空 + 局内货币清零（批三）：新一局的货币从 0 开始累计
    //（「本局累计、restart 清零」的规格语义）
    m_coins.clear();
    m_coinCount = 0;

    // 刀锋扫程状态清零：上一局未扫完的刀锋（含命中记录）不带进新一局——
    // Player::reset 已清零表现计时（swingActive 随之为 false），这里把
    // 场景侧推进标记与命中记录一并复位
    m_sweepRunning = false;     // 扫程推进标记归还（新一局首刀重新开启）
    m_sweepHitCount = 0;        // 本扫程命中计数清零
    m_sweepHit.fill(false);     // 命中记录清空（全槽位）
    m_sweepPrevAngleRad = 0.0f; // 区间起点角复位（新一局首刀出手时重新对齐）

    // 场上漂浮文字清空：上一局未播完的伤害/升级飘字不跨局残留
    m_floatingTexts.clear();

    // 场上命中特效清空（批五A）：上一局未播完的爪痕不跨局残留
    m_monsterHitFxs.clear();

    // 最后切回 Playing，新一局正式开始
    m_state = PlayState::Playing;

    // 冒烟观测点：确认 R 重开真正触发（自动化测试据此断言）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "重开一局");
}

// 每帧渲染：清屏 -> 世界区块 -> 玩家（含横扫表现）-> 怪物 -> 宝石 -> 金币 ->
// 漂浮文字 -> HUD。不调用 SDL_RenderPresent（提交由应用外壳统一负责）。
// alpha 是插值系数，原样传给各实体——它们据此在上一固定步位置与当前
// 固定步位置之间平滑绘制，再经相机换算到屏幕（批二）
void PlayScene::render(float alpha)
{
    // 采样本渲染帧的间隔并更新 FPS 平滑值（只统计，不影响绘制内容）
    sampleFrameTime();

    // 设置清屏颜色。深蓝是相机贴世界边缘时的窗外兜底色
    //（正常游玩看不到——相机钳制在世界内）
    if (!SDL_SetRenderDrawColor(m_renderer, 20, 30, 50, 255))
    {
        // 设置颜色失败不影响继续绘制：记录后继续（最终由提交环节兜底判定）
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "设置绘制颜色失败：%s", SDL_GetError());
    }

    // 清除上一帧内容，避免旧画面残留
    if (!SDL_RenderClear(m_renderer))
    {
        // 清屏失败同样只记录：避免单帧故障中断整局游戏
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "清屏失败：%s", SDL_GetError());
    }

    // 世界（批二）：激活窗口内的区块纹理（地板/墙/装饰已烘焙成整块）。
    // 「世界 -> 屏幕」换算与视口裁剪都在 World::render 内部完成；
    // 背景图自批二起退役（区块地板取代；MenuScene 仍继续使用 background.png）
    m_world.render(m_renderer, m_camera);

    // 玩家按插值位置绘制（经相机换算）。批五A：无敌期间不再闪烁跳隐——
    // 受击反馈由玩家自身的击退位移与红染（Player::render 内叠加）承担，
    // 每帧连续绘制保证「逐帧无跳隐」的画面（冒烟连拍核验点）
    m_player.render(m_renderer, alpha, m_camera);

    // 怪物：只绘制「存活 + 激活区」槽位（与 update 同一门控——空槽位与
    // 非激活区都不可见，渲染开销随活跃区域有界）
    for (const Monster& monster : m_monsters)
    {
        if (!monster.isAlive())
        {
            continue; // 空槽位：不可见
        }
        // 非激活区怪物：暂停渲染（update 侧也冻结——两侧门控一致）
        const SDL_FRect monsterRect = monster.rect(); // 怪物矩形快照
        if (!m_world.isPointInActiveChunks(monsterRect.x + monsterRect.w * 0.5f,
                                           monsterRect.y + monsterRect.h * 0.5f))
        {
            continue; // 激活窗口外：跳过绘制
        }
        monster.render(m_renderer, alpha, m_camera); // 存活且激活：绘制（含残影/警示/红染叠加）
    }

    // 命中特效（批五A）：接触点的爪痕/爆点——世界层绘制（怪物之上、
    // 飘字之下），经相机换算；透明度随剩余寿命线性渐隐（画完还原）
    if (m_monsterHitFxTexture != nullptr && !m_monsterHitFxs.empty())
    {
        for (const MonsterHitFx& fx : m_monsterHitFxs)
        {
            const Vec2 screen = m_camera.worldToScreen(fx.x, fx.y); // 特效中心的屏幕坐标
            const SDL_FRect dst{ screen.x - kMonsterHitFxSize * 0.5f, // 以接触点为绘制中心
                                 screen.y - kMonsterHitFxSize * 0.5f,
                                 kMonsterHitFxSize,                    // 绘制宽（与素材一致）
                                 kMonsterHitFxSize };                  // 绘制高（同上）
            const float ratio = fx.left / kMonsterHitFxSeconds; // 剩余寿命比例 [0,1]
            SDL_SetTextureAlphaMod(m_monsterHitFxTexture, static_cast<Uint8>(255.0f * ratio)); // 渐隐
            SDL_RenderTexture(m_renderer, m_monsterHitFxTexture, nullptr, &dst); // 整张绘制（单帧素材）
        }
        SDL_SetTextureAlphaMod(m_monsterHitFxTexture, 255); // 还原不透明（防污染后续绘制）
    }

    // 说明：原「挥击弧光」特效已退役——攻击表现改由 Player 在自身 render
    // 里绘制武器素材（绕玩家中心 −60° 扫到 +60° 的横扫轨迹），
    // 此处不再有场景层特效

    // 经验宝石：逐颗按插值位置绘制（经相机换算；青蓝调——与金币一眼可分）
    for (const XpGem& gem : m_gems)
    {
        gem.render(m_renderer, alpha, m_camera);
    }

    // 金币（批三）：逐枚按插值位置绘制（经相机换算；素材包原色——金黄色，
    // 与青蓝宝石同屏可辨）
    for (const Coin& coin : m_coins)
    {
        coin.render(m_renderer, alpha, m_camera);
    }

    // 漂浮文字（伤害/升级共用）：世界层最上层——叠加在实体之上、HUD 之下。
    // 批二起存世界坐标，绘制时经相机换算（相机滚动时飘字随世界点移动）；
    // 暂停/结束时寿命已冻结，这里照常绘制即「静止画面」（位置/透明度不变）
    for (const FloatingText& text : m_floatingTexts)
    {
        text.render(m_renderer, m_hudFont, m_camera);
    }

    // 游戏世界画完后再叠加 HUD 文字
    renderHud();
}

// 绘制 HUD 文字（左下角 FPS、暂停叠层、结束界面）。
// 游戏文案统一走 Font（TTF 中文字体）：传入屏幕像素坐标，文字颜色由调用
// 前的 SDL_SetRenderDrawColor 决定；左下角 FPS 调试行例外，仍用 DebugText
// （SDL 内置调试字体，仅 ASCII，观感与改造前一致）
void PlayScene::renderHud()
{
    // 查询渲染器输出尺寸（物理像素）。HUD 的布局定位都基于这个实际尺寸，
    // 而不是把 1280x720 硬编码进来
    int outW = 0;
    int outH = 0;
    SDL_GetRenderOutputSize(m_renderer, &outW, &outH);

    // 换成浮点别名：后续所有文字定位都用「屏幕像素」表达
    const float screenW = static_cast<float>(outW); // 屏幕宽（像素）
    const float screenH = static_cast<float>(outH); // 屏幕高（像素）

    // Paused / LevelUp / Shop / GameOver 叠层加半透明暗化遮罩：把游戏
    //世界（区块/玩家/怪物）整体压暗，让叠层文字从画面中浮出来（LevelUp/
    // Shop 的面板同样悬浮在压暗的世界之上）。绘制顺序：遮罩放在最前——
    // 之后绘制的 FPS 行与叠层文字都压在遮罩之上，保持全亮
    if (m_state == PlayState::Paused || m_state == PlayState::LevelUp ||
        m_state == PlayState::Shop || m_state == PlayState::GameOver)
    {
        // 打开混合模式：颜色按 alpha 与画面混合（默认模式会直接覆盖）
        if (!SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND))
        {
            // 设置失败只记录：遮罩退化为不透明填充也在可接受范围
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "设置混合模式失败：%s", SDL_GetError());
        }
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 140); // 半透明黑：保留世界轮廓

        // 目标矩形传 nullptr：填充整个渲染目标（全屏遮罩）
        if (!SDL_RenderFillRect(m_renderer, nullptr))
        {
            // 绘制失败只记录：遮罩是纯装饰，缺失不影响玩法
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "绘制遮罩失败：%s", SDL_GetError());
        }

        // 恢复默认混合模式：避免后续绘制（及下一帧世界绘制）被混合模式影响
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    }

    // ---- 左下角：FPS / 帧时间（灰白色小字，F3 控制显隐）----
    // 放在状态分支之前：无论 Playing/Paused/GameOver 都显示，方便对比
    // 三种状态下渲染性能的差异（数值来自 EMA，会逐帧连续变化）。
    // 这行是调试信息而非游戏 UI 文案，保持 SDL 内置调试字体（ASCII）
    if (m_fpsVisible)
    {
        // 灰白色：不抢游戏内容的视觉焦点
        SDL_SetRenderDrawColor(m_renderer, 180, 180, 180, 255);

        // 左对齐 x=20；文字高 = 8 逻辑像素 x kFpsScale = 16 屏幕像素，
        // 再留 24 像素底边距 => y = 屏高 - 40
        const float fpsY = screenH - 40.0f;

        if (m_hasFrameSample)
        {
            // FPS 与帧时间同源换算（FPS = 1000 / 毫秒），两者必然自洽
            const float fps = 1000.0f / m_smoothedFrameMs;
            DebugText::drawFormat(m_renderer, kFpsScale, 20.0f, fpsY,
                                  "FPS: %.1f | %.1f ms", fps, m_smoothedFrameMs);
        }
        else
        {
            // 首帧还没有有效间隔：显示占位符，下一帧起给出真实数值
            DebugText::drawFormat(m_renderer, kFpsScale, 20.0f, fpsY,
                                  "FPS: -- | -- ms");
        }
    }

    // ---- 生存 HUD（Playing / LevelUp / Shop / Paused 都显示）----
    // 暂停/升级/商店时保留完整 HUD：玩家能看清冻结时刻的生命/等级/经验/
    // 击杀/存活（升级与商店面板悬浮其上）；GameOver 不显示（结束界面有自己的排版）
    if (m_state == PlayState::Playing || m_state == PlayState::Paused ||
        m_state == PlayState::LevelUp || m_state == PlayState::Shop)
    {
        // 条状 HUD 几何常量（左上角第一块内容）
        constexpr float kBarX = 20.0f;    // 条左边界（与后续文字同一条左边线）
        constexpr float kBarW = 200.0f;   // 条满宽（像素）
        constexpr float kHpBarY = 22.0f;  // 血条上边界
        constexpr float kHpBarH = 22.0f;  // 血条高度（像素）
        constexpr float kXpBarY = 112.0f; // 经验条上边界（等级行之下）
        constexpr float kXpBarH = 16.0f;  // 经验条高度（像素）

        // 血条底色（深红）：先铺满整条，剩余生命的亮红前景覆盖其上——
        // 即使生命很少，也能从底色的存在看出「总量的量级」
        SDL_SetRenderDrawColor(m_renderer, 90, 24, 24, 255);
        const SDL_FRect hpBarBack{ kBarX, kHpBarY, kBarW, kHpBarH };
        SDL_RenderFillRect(m_renderer, &hpBarBack);

        // 血条前景（亮红）：宽度 = 满宽 x 剩余生命比例（0 血时宽度为 0）。
        // 比例计算防御除零（上限配置为 0 的极端情形按空血条画）
        const float hpRatio = (m_playerMaxHp > 0)
            ? static_cast<float>(m_playerHp) / static_cast<float>(m_playerMaxHp)
            : 0.0f;
        SDL_SetRenderDrawColor(m_renderer, 220, 60, 60, 255);
        const SDL_FRect hpBarFill{ kBarX, kHpBarY, kBarW * hpRatio, kHpBarH };
        SDL_RenderFillRect(m_renderer, &hpBarFill);

        // 血条右侧：「HP 当前/上限」文字（白色，与血条纵向居中对齐）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        char hpText[48]; // 容纳 "HP 100/100"，绰绰有余
        SDL_snprintf(hpText, sizeof(hpText), "HP %d/%d", m_playerHp, m_playerMaxHp);
        m_hudFont.draw(m_renderer, hpText, kBarX + kBarW + 12.0f, 16.0f);

        // 等级行（第 2 行）：升级瞬闪窗口内金/白交替闪烁（高光反馈），
        // 其余时刻白色。相位 = 剩余时长对闪烁周期取模，前 0.08s 一亮一暗
        const bool levelFlashOn = m_levelFlashLeft > 0.0f &&
            std::fmod(m_levelFlashLeft, kLevelFlashBlinkPeriod) < kLevelFlashBlinkPeriod * 0.5f;
        if (levelFlashOn)
        {
            SDL_SetRenderDrawColor(m_renderer, 255, 215, 0, 255); // 金：高光相位
        }
        else
        {
            SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255); // 白：常规（含窗口结束）
        }
        char levelText[48]; // 容纳 "等级: " + 最长数字，绰绰有余
        SDL_snprintf(levelText, sizeof(levelText), "等级: %d", m_level);
        m_hudFont.draw(m_renderer, levelText, kBarX, 64.0f);

        // 经验条（第 3 行）：深金底 + 亮金前景（当前级进度/本级需求）。
        // 升级瞬间进度被扣减并重置——条长回缩即「经验条重置」的视觉证据
        const int xpNeeded = m_levelCurve.needed(m_level); // 本级需求（契约恒 >= 1）
        const float xpRatio = (xpNeeded > 0)
            ? static_cast<float>(m_xp) / static_cast<float>(xpNeeded)
            : 0.0f;
        SDL_SetRenderDrawColor(m_renderer, 70, 58, 30, 255); // 深金底：空白部分
        const SDL_FRect xpBarBack{ kBarX, kXpBarY, kBarW, kXpBarH };
        SDL_RenderFillRect(m_renderer, &xpBarBack);
        SDL_SetRenderDrawColor(m_renderer, 230, 190, 60, 255); // 亮金前景：已积累部分
        const SDL_FRect xpBarFill{ kBarX, kXpBarY,
                                   kBarW * std::min(xpRatio, 1.0f), kXpBarH };
        SDL_RenderFillRect(m_renderer, &xpBarFill);

        // 经验条右侧：「经验 当前/所需」文字（白色）——数字与条同源同义，
        // 取代旧版的「经验: N」累计值展示（现在 N 是当前级进度）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        char xpText[64]; // 容纳 "经验 999/999"，绰绰有余
        SDL_snprintf(xpText, sizeof(xpText), "经验 %d/%d", m_xp, xpNeeded);
        m_hudFont.draw(m_renderer, xpText, kBarX + kBarW + 12.0f, 100.0f);

        // 第 4/5/6 行：击杀 / 存活 / 金币（白色，行距 44 像素——32 号字留 12 像素行间隔）
        char killText[48]; // 容纳 "击杀: " + 最长数字，绰绰有余
        SDL_snprintf(killText, sizeof(killText), "击杀: %d", m_killCount);
        m_hudFont.draw(m_renderer, killText, 20.0f, 146.0f);

        char survivalText[48]; // 容纳「存活: mm:ss」，绰绰有余
        formatSurvivalText(survivalText, sizeof(survivalText), m_survivalSeconds);
        m_hudFont.draw(m_renderer, survivalText, 20.0f, 190.0f);

        // 金币行（批三局内货币）：本局累计的拾取金币数——「击杀—掉落—
        // 拾取—计数」闭环的可视终点（冒烟观测点）
        char coinText[48]; // 容纳 "金币: " + 最长数字，绰绰有余
        SDL_snprintf(coinText, sizeof(coinText), "金币: %d", m_coinCount);
        m_hudFont.draw(m_renderer, coinText, 20.0f, 234.0f);
    }

    if (m_state == PlayState::Paused)
    {
        // 暂停叠层：大号「暂停」+ 两条操作提示。
        // 世界画面保持冻结在叠层之下（update 不推进，render 每帧重画同一画面）

        // 水平中心：叠层文字共用的对齐基准
        const float screenCenterX = screenW / 2.0f;

        // 大号「暂停」（黄色，kBigPtSize 字号，强调暂停状态）。
        // 纵向位置取 0.30 屏高而非 0.38：玩家通常停在窗口中心附近，
        // 标题上移一段后与玩家精灵保持明显间隙，暂停画面更整洁
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 0, 255);
        m_bigFont.drawCentered(m_renderer, "暂停", screenCenterX, screenH * 0.30f);

        // 恢复提示（白色，常规 HUD 字号）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        m_hudFont.drawCentered(m_renderer, "按 ESC 继续", screenCenterX, screenH * 0.56f);

        // 回菜单提示（白色，常规 HUD 字号）
        m_hudFont.drawCentered(m_renderer, "按 M 返回菜单", screenCenterX, screenH * 0.66f);
    }
    else if (m_state == PlayState::GameOver)
    {
        // GameOver 界面（结算）：游戏结束 + 本局成绩（等级/击杀/存活）+
        // 三项最佳战绩（被破项旁标「新纪录！」）+ 两条操作提示，
        // 全部水平居中、围绕窗口中心排布

        // 水平中心与基准纵坐标：各行文字共用（基准取屏幕中心，行距 44 像素）
        const float screenCenterX = screenW / 2.0f;
        const float centerY = screenH / 2.0f;

        // 第一行：游戏结束（白色大号标题）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        m_bigFont.drawCentered(m_renderer, "游戏结束", screenCenterX, centerY - 150.0f);

        // 第二行：本局等级（黄色）——冒烟观测点：结束界面的「等级: N」
        char levelText[48]; // 容纳 "等级: " + 最长数字，绰绰有余
        SDL_snprintf(levelText, sizeof(levelText), "等级: %d", m_level);
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 0, 255);
        m_hudFont.drawCentered(m_renderer, levelText, screenCenterX, centerY - 60.0f);

        // 第三行：本局击杀与存活（黄色，mm:ss）——保留旧结算信息
        char runText[64];      // 容纳 "击杀: 999 · 存活: mm:ss"，绰绰有余
        char survivalText[48]; // 容纳「存活: mm:ss」，绰绰有余
        formatSurvivalText(survivalText, sizeof(survivalText), m_survivalSeconds);
        SDL_snprintf(runText, sizeof(runText), "击杀: %d · %s", m_killCount, survivalText);
        m_hudFont.drawCentered(m_renderer, runText, screenCenterX, centerY - 10.0f);

        // 第四~六行：三项最佳战绩（白色）；被破的项旁配金色「新纪录！」。
        // 行内布局：先量正文宽度，正文居中绘制后把标记放在其右缘空 12 像素处
        // ——「该项旁」的逐项标记（而不是全局一条）
        const auto drawRecordLine = [&](const char* label, float y, bool newRecord)
        {
            // 正文（该项纪录）：白色绘制
            SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
            m_hudFont.drawCentered(m_renderer, label, screenCenterX, y);

            // 未破的项：不画标记，直接结束本行
            if (!newRecord)
            {
                return;
            }

            // 量出正文宽度：标记紧贴正文右侧（正文居中的右缘坐标 = 中心 + 半宽）
            const SDL_FPoint size = m_hudFont.measure(label);
            SDL_SetRenderDrawColor(m_renderer, 255, 215, 0, 255); // 金色：与升级飘字同一高光色
            m_hudFont.draw(m_renderer, "新纪录！", screenCenterX + size.x / 2.0f + 12.0f, y);
        };

        // 最高击杀（纪录值来自 m_best——结算后已更新为最新值）
        char bestKillText[64]; // 容纳 "最高击杀: " + 最长数字，绰绰有余
        SDL_snprintf(bestKillText, sizeof(bestKillText), "最高击杀: %d", m_best.kills);
        drawRecordLine(bestKillText, centerY + 50.0f, m_newKillRecord);

        // 最高等级
        char bestLevelText[64]; // 容纳 "最高等级: " + 最长数字，绰绰有余
        SDL_snprintf(bestLevelText, sizeof(bestLevelText), "最高等级: %d", m_best.level);
        drawRecordLine(bestLevelText, centerY + 94.0f, m_newLevelRecord);

        // 最长存活（整秒 -> mm:ss；与 HUD 的存活展示同口径）
        char bestTimeText[64]; // 容纳 "最长存活: mm:ss"，绰绰有余
        SDL_snprintf(bestTimeText, sizeof(bestTimeText), "最长存活: %02d:%02d",
                     m_best.seconds / 60, m_best.seconds % 60);
        drawRecordLine(bestTimeText, centerY + 138.0f, m_newTimeRecord);

        // 第七行：重开提示（白色）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        m_hudFont.drawCentered(m_renderer, "按 R 重新开始", screenCenterX, centerY + 200.0f);

        // 第八行：回菜单提示（白色）。行距与上一行保持一致
        m_hudFont.drawCentered(m_renderer, "按 M 返回菜单", screenCenterX, centerY + 244.0f);
    }
    else if (m_state == PlayState::LevelUp)
    {
        // 升级三选一面板（M2；批五A：鼠标悬停高亮与点击选择）：
        // 标题 + 卡片横排 + 操作提示。世界画面冻结在遮罩之下；卡片 =
        // 色块底板 + 金色边框 + 编号/卡名/描述/层数（简洁版——观感美化
        // 留 M3）。卡片矩形来自 CardLayout 的共享布局计算（渲染与鼠标
        // 命中判定同一份——面板画在哪、就能在哪点，防坐标漂移）
        const float centerX = screenW / 2.0f; // 水平中心：标题对齐基准

        // 标题（金色，屏幕上缘 11% 处）
        SDL_SetRenderDrawColor(m_renderer, 255, 215, 0, 255); // 金色：与升级高光同族
        m_hudFont.drawCentered(m_renderer, "升级！选择强化（1/2/3）", centerX, screenH * 0.11f);

        const int cardNum = m_offeredCards.count; // 实际张数（1~3）

        for (int i = 0; i < cardNum; ++i) // 逐卡绘制
        {
            const CardId id = m_offeredCards.cards[i]; // 本卡的卡 id
            const CardDef& def = cardDef(id);          // 本卡定义（名称/描述/上限）
            // 本卡矩形：共享布局计算（与 handleCardChoice 的命中判定同源）
            const SDL_FRect cardRect = cardPanelRectAt(i, cardNum, screenW, screenH);
            const bool hovered = (i == m_hoverCard); // 是否鼠标悬停（每固定步刷新）：高亮判据

            // 底板：深色半透明（世界画面隐约透出）——填充需要混合模式；
            // 悬停时底色变亮一档（「微亮」高亮的底板侧）
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND); // 打开混合
            if (hovered)
            {
                SDL_SetRenderDrawColor(m_renderer, 52, 58, 82, 244); // 悬停：亮一档的蓝灰
            }
            else
            {
                SDL_SetRenderDrawColor(m_renderer, 28, 32, 46, 232); // 常规：深蓝灰底（近乎不透明）
            }
            SDL_RenderFillRect(m_renderer, &cardRect);                   // 填充底板
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);  // 恢复默认混合

            // 边框：金色描边（SDL_RenderRect 画空心矩形）；悬停时换亮金
            // 并再套一圈外扩 3px 的描边（「金色边框加粗」的悬停高亮）。
            // 外扩量远小于卡间隙 30，不与相邻卡重叠
            SDL_SetRenderDrawColor(m_renderer, 205, 175, 70, 255); // 金色（常规）
            SDL_RenderRect(m_renderer, &cardRect);                 // 画边框
            if (hovered)
            {
                SDL_SetRenderDrawColor(m_renderer, 255, 225, 120, 255); // 亮金：悬停主色
                SDL_RenderRect(m_renderer, &cardRect);                  // 原框重描（提亮）
                const SDL_FRect hoverFrame{ cardRect.x - 3.0f,          // 外扩 3px 的加粗圈
                                            cardRect.y - 3.0f,
                                            cardRect.w + 6.0f,          // 宽：两侧各扩 3px
                                            cardRect.h + 6.0f };        // 高：上下各扩 3px
                SDL_RenderRect(m_renderer, &hoverFrame);                // 外圈：加粗观感
            }

            // 编号（左上角，金色）：与 1/2/3 按键一一对应
            char indexText[8]; // 容纳 "1"~"3"，绰绰有余
            SDL_snprintf(indexText, sizeof(indexText), "%d", i + 1); // 位序即按键号
            SDL_SetRenderDrawColor(m_renderer, 255, 215, 0, 255);    // 金色
            m_hudFont.draw(m_renderer, indexText, cardRect.x + 16.0f, cardRect.y + 14.0f); // 左上角绘制

            // 卡名（居中，黄色，HUD 字号）
            SDL_SetRenderDrawColor(m_renderer, 255, 215, 0, 255); // 黄色：与编号同族
            m_hudFont.drawCentered(m_renderer, def.name,
                                   cardRect.x + cardRect.w * 0.5f, cardRect.y + 76.0f);

            // 效果描述（居中，白色，小字号）
            SDL_SetRenderDrawColor(m_renderer, 235, 235, 235, 255); // 近白
            m_cardFont.drawCentered(m_renderer, def.desc,
                                    cardRect.x + cardRect.w * 0.5f, cardRect.y + 158.0f);

            // 当前层数（居中，浅灰小字）：有限卡「层数 x/上限」、无限卡「层数 x/无限」
            const int level = m_cardLevels[static_cast<int>(id)]; // 本卡当前层数
            char levelText[64]; // 容纳「层数 99/无限」，绰绰有余
            if (def.maxLevel > 0) // 有限卡：上限写数字
            {
                SDL_snprintf(levelText, sizeof(levelText), "层数 %d/%d", level, def.maxLevel);
            }
            else // 无限卡：上限写「无限」
            {
                SDL_snprintf(levelText, sizeof(levelText), "层数 %d/无限", level);
            }
            SDL_SetRenderDrawColor(m_renderer, 190, 190, 190, 255); // 浅灰
            m_cardFont.drawCentered(m_renderer, levelText,
                                    cardRect.x + cardRect.w * 0.5f, cardRect.y + 228.0f);
        }

        // 底部操作提示（浅灰，居中，卡片行之下）——批五A：补「点击卡片」
        // 文案（键盘与鼠标两路并存；位置取共享布局的首卡底边，任何张数
        // 都对齐同一行列）
        SDL_SetRenderDrawColor(m_renderer, 200, 200, 200, 255); // 浅灰
        const SDL_FRect firstCard = cardPanelRectAt(0, cardNum, screenW, screenH); // 首卡矩形
        m_hudFont.drawCentered(m_renderer, "按对应数字键或点击卡片选择", centerX,
                               firstCard.y + firstCard.h + 40.0f);
    }
    else if (m_state == PlayState::Shop)
    {
        // 商店面板（批五B）：标题（金币）+ 3x3 强化卡 + 提示消息 + 底部
        // 操作提示。世界画面冻结在遮罩之下；卡片 = 色块底板 + 金色边框 +
        // 名称/层数/价格（悬停高亮；金币不足灰显 + 价格红字；满层灰显
        // 「已满级」）。卡片矩形来自 ShopLayout 的共享布局计算（渲染与
        // 鼠标命中同一份——面板画在哪、就能在哪点，防坐标漂移）
        const float centerX = screenW / 2.0f; // 水平中心：标题对齐基准

        // 标题（金色，屏幕上缘 7.5% 处）：「商店（金币: N）」——金币余额
        // 是购买决策的第一信息（规格：面板显示金币与各项层数/价格）
        SDL_SetRenderDrawColor(m_renderer, 255, 215, 0, 255); // 金色：财富主题
        char shopTitle[64]; // 容纳「商店（金币: 99999）」，绰绰有余
        SDL_snprintf(shopTitle, sizeof(shopTitle), "商店（金币: %d）", m_coinCount);
        m_hudFont.drawCentered(m_renderer, shopTitle, centerX, screenH * 0.075f);

        for (int i = 0; i < kShopItemCount; ++i) // 逐项绘制（0~8，行优先）
        {
            const CardId id = static_cast<CardId>(i); // 本项强化 id（与表顺序一致）
            const CardDef& def = cardDef(id);         // 本项定义（名称/上限）
            const int level = m_cardLevels[i];        // 当前层数
            const bool full = isCardFull(id, level);  // 已满层（灰显「已满级」）
            const int price = shopPrice(id, level);   // 当前价格（层数夹取后——与购买同一函数）
            const bool affordable = shopCanBuy(id, level, m_coinCount); // 可买？（满层/金币双判据）
            const SDL_FRect cardRect = shopPanelRectAt(i, screenW, screenH); // 共享布局
            const bool hovered = (i == m_hoverShopItem); // 悬停高亮（每固定步刷新）

            // 底板：深色半透明（世界画面隐约透出）；悬停时底色变亮一档
            //（与升级卡面板同款观感——两处面板的高亮语言一致）
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND); // 打开混合
            if (hovered)
            {
                SDL_SetRenderDrawColor(m_renderer, 52, 58, 82, 244); // 悬停：亮一档的蓝灰
            }
            else
            {
                SDL_SetRenderDrawColor(m_renderer, 28, 32, 46, 232); // 常规：深蓝灰底（近乎不透明）
            }
            SDL_RenderFillRect(m_renderer, &cardRect);                  // 填充底板
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE); // 恢复默认混合

            // 边框：可买金色 / 不可买（满层或金币不足）暗金——「灰显」的
            // 边框侧信号；悬停时亮金重描 + 外扩 3px 加粗（与升级卡同款）
            if (affordable)
            {
                SDL_SetRenderDrawColor(m_renderer, 205, 175, 70, 255); // 金：可买
            }
            else
            {
                SDL_SetRenderDrawColor(m_renderer, 110, 100, 70, 255); // 暗金：不可买
            }
            SDL_RenderRect(m_renderer, &cardRect); // 画边框
            if (hovered)
            {
                SDL_SetRenderDrawColor(m_renderer, 255, 225, 120, 255); // 亮金：悬停主色
                SDL_RenderRect(m_renderer, &cardRect);                  // 原框重描（提亮）
                const SDL_FRect hoverFrame{ cardRect.x - 3.0f,          // 外扩 3px 的加粗圈
                                            cardRect.y - 3.0f,
                                            cardRect.w + 6.0f,          // 宽：两侧各扩 3px
                                            cardRect.h + 6.0f };        // 高：上下各扩 3px
                SDL_RenderRect(m_renderer, &hoverFrame);                // 外圈：加粗观感
            }

            // 名称（左上，HUD 字号）：可买金色 / 不可买灰（灰显的主体信号）
            if (affordable)
            {
                SDL_SetRenderDrawColor(m_renderer, 255, 215, 0, 255); // 金色：可买
            }
            else
            {
                SDL_SetRenderDrawColor(m_renderer, 130, 130, 130, 255); // 灰色：灰显
            }
            m_hudFont.draw(m_renderer, def.name, cardRect.x + 18.0f, cardRect.y + 10.0f); // 名称

            // 层数行（左对齐第二行，卡片字号）：「层数 x/上限」（无限卡写「无限」）
            const int levelIndex = static_cast<int>(id); // 层数数组下标（与 id 一致）
            char levelText[64]; // 容纳「层数 99/无限」，绰绰有余
            if (def.maxLevel > 0) // 有限卡：上限写数字
            {
                SDL_snprintf(levelText, sizeof(levelText), "层数 %d/%d",
                             m_cardLevels[levelIndex], def.maxLevel);
            }
            else // 无限卡：上限写「无限」
            {
                SDL_snprintf(levelText, sizeof(levelText), "层数 %d/无限", m_cardLevels[levelIndex]);
            }
            if (affordable) // 可买：近白；不可买：暗灰
            {
                SDL_SetRenderDrawColor(m_renderer, 235, 235, 235, 255); // 近白
            }
            else
            {
                SDL_SetRenderDrawColor(m_renderer, 120, 120, 120, 255); // 暗灰
            }
            m_cardFont.draw(m_renderer, levelText, cardRect.x + 18.0f, cardRect.y + 56.0f); // 层数行

            // 价格行（左对齐第三行，卡片字号）：满层「已满级」（灰）；
            // 金币不足「价格 N 金币」（红字——灰显之外的第二提示信号）；
            // 可买「价格 N 金币」（金色）
            char priceText[64]; // 容纳「价格 99999 金币」，绰绰有余
            if (full) // 满层：不再有价格
            {
                SDL_strlcpy(priceText, "已满级", sizeof(priceText)); // 文案
                SDL_SetRenderDrawColor(m_renderer, 120, 120, 120, 255); // 暗灰
            }
            else if (affordable) // 可买：金色价格
            {
                SDL_snprintf(priceText, sizeof(priceText), "价格 %d 金币", price); // 价格文案
                SDL_SetRenderDrawColor(m_renderer, 255, 215, 0, 255); // 金色
            }
            else // 金币不足：红字价格（差多少一目了然）
            {
                SDL_snprintf(priceText, sizeof(priceText), "价格 %d 金币", price); // 价格文案
                SDL_SetRenderDrawColor(m_renderer, 220, 90, 90, 255); // 红色：不可负担
            }
            m_cardFont.draw(m_renderer, priceText, cardRect.x + 18.0f, cardRect.y + 90.0f); // 价格行
        }

        // 提示消息（上一操作的结果：已购买/金币不足/已达上限；常驻不淡出
        // ——「冻结画面像素完全静止」的承诺不被消息动画破坏）——
        // 位置：底部操作提示之上
        if (m_shopMessage[0] != '\0') // 空串 = 无消息
        {
            SDL_SetRenderDrawColor(m_renderer, 255, 220, 120, 255); // 浅黄：提示语
            m_hudFont.drawCentered(m_renderer, m_shopMessage, centerX, screenH * 0.885f);
        }

        // 底部操作提示（浅灰，居中）——键盘与鼠标两路并存 + 关闭键说明
        SDL_SetRenderDrawColor(m_renderer, 200, 200, 200, 255); // 浅灰
        m_hudFont.drawCentered(m_renderer, "按 1~9 或点击购买 · B/ESC 关闭", centerX,
                               screenH * 0.93f);
    }
}

// 采样渲染帧间隔并做 EMA 平滑：FPS 显示的数据来源。
// 与固定步长逻辑完全无关——它统计的是「渲染帧」的频率，
// 因此放在 render 里（每渲染帧一次），而不是 update 里（每固定步一次）
void PlayScene::sampleFrameTime()
{
    const Uint64 now = SDL_GetPerformanceCounter(); // 本帧时刻（性能计数器）

    // 第一次调用没有「上一帧」可参照：只记录时刻，间隔从下一次开始算
    if (m_lastFrameCounter != 0)
    {
        // 渲染帧间隔（毫秒）= 计数器差值 / 频率 x 1000
        const float frameMs = static_cast<float>(now - m_lastFrameCounter) * 1000.0f /
                              static_cast<float>(SDL_GetPerformanceFrequency());

        if (!m_hasFrameSample)
        {
            // 首个有效间隔直接作为初值：避免从 0 缓慢爬升，显示假数字
            m_smoothedFrameMs = frameMs;
            m_hasFrameSample = true; // 之后所有采样都走 EMA 分支
        }
        else
        {
            // 指数滑动平均（EMA）：新值权重 0.05 —— 约等于最近 20 帧的
            // 滑动窗口（60fps 下约 0.33 秒）。选它而非「固定 1 秒窗口」：
            // 数字连续不跳变，且一行公式完成，无需时间桶与计数
            constexpr float kEmaWeight = 0.05f;
            m_smoothedFrameMs = m_smoothedFrameMs * (1.0f - kEmaWeight) + frameMs * kEmaWeight;
        }
    }

    m_lastFrameCounter = now; // 记录本帧时刻，供下一次调用计算间隔
}
