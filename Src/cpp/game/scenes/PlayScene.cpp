#include "game/scenes/PlayScene.h"
#include "game/scenes/MenuScene.h" // MenuScene：GameOver 按 M 时要切换回主菜单
#include "engine/DebugText.h"      // DebugText：仅用于左下角 FPS/帧时间调试行（ASCII）
#include "engine/Input.h"          // Input：ctx.input->pressed 查询暂停/重开/回菜单边缘
#include "engine/Rect.h"           // rectsOverlap：接触伤害/拾取的 AABB 判定
#include "engine/SceneManager.h"   // SceneManager：m_sceneManager->requestScene 需要完整类型
#include <SDL3_image/SDL_image.h>  // IMG_LoadTexture：加载 assets/background.png
#include <algorithm>               // std::max / std::min：生命夹取、难度曲线封顶
#include <cmath>                   // std::fmod：无敌闪烁的相位取模
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

// 析构：释放场景自持的背景纹理（本批起挥击弧光已退役——攻击表现改由
// Player 自持的武器素材绘制）。
// 关键时机：SceneManager 先 onExit + 析构旧场景、后销毁 renderer，
// 因此这里释放纹理时 renderer 尚在，安全；
// 玩家/怪物/宝石的纹理由各自 Entity 的析构函数兜底释放；
// 升级音效播放器（LevelupSfx 成员）与本类其余成员一样随析构自动收尾
// （关闭音频流、释放 PCM——同为 RAII，见该类头文件）
PlayScene::~PlayScene()
{
    // 只有纹理确实存在时才销毁，避免对空指针调用 SDL_DestroyTexture
    if (m_background != nullptr)
    {
        SDL_DestroyTexture(m_background); // 释放背景图占用的显存
        m_background = nullptr;           // 置空：即使重复调用也不会二次销毁
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
    //      attack_damage           横扫伤害（点，弧内每只各扣）
    //      attack_range            横扫半径（像素）
    //      attack_arc              横扫总张角（度）
    //      attack_knockback        横扫命中击退（像素）
    //      monster_speed_growth    怪物速度成长（像素/秒²，按局内秒数叠加）
    //      monster_spawn_interval  刷新间隔初始值（秒，随局内时间收紧）
    //      monster_max_count       数量上限初始值（只，随局内时间 +2）
    //      monster_damage          怪物接触伤害
    //      xp_level_base           升级曲线基数（第 1 级升第 2 级的需求）
    //      xp_level_growth         升级曲线倍率（需求逐级倍率）
    //      levelup_max_hp_bonus    每级生命上限强化（并即时回血等值）
    //      levelup_damage_bonus    每级攻击伤害强化（点）
    //      levelup_speed_bonus     每级移速强化（像素/秒）
    const float playerSpeed = config.getFloat("player_speed", Player::kDefaultSpeed);
    const float playerAccel = config.getFloat("player_accel", Player::kDefaultAccel);
    const float playerFriction = config.getFloat("player_friction", Player::kDefaultFriction);
    const float playerAnimInterval =
        config.getFloat("player_anim_interval", Player::kDefaultFrameTime);
    const float attackCooldown =
        config.getFloat("attack_cooldown", Player::kDefaultAttackCooldown);
    m_attackRange = config.getFloat("attack_range", kDefaultAttackRange);
    // 张角与击退距离做下限夹取（负张角/负距离没有玩法语义，同 levelup_* 的
    // 防御写法）；张角上界交由 AttackArc 归一化（超整圆按整圆处理）
    m_attackArc = std::max(0.0f, config.getFloat("attack_arc", kDefaultAttackArc));
    m_attackKnockback = std::max(0.0f, config.getFloat("attack_knockback", kDefaultAttackKnockback));
    m_monsterSpeedGrowth = config.getFloat("monster_speed_growth", kDefaultMonsterSpeedGrowth);
    m_monsterSpawnInterval =
        config.getFloat("monster_spawn_interval", kDefaultMonsterSpawnInterval);
    m_monsterDamage = config.getInt("monster_damage", kDefaultMonsterDamage);

    // 三项「基准值」= 配置里的初始值（restart 复位目标）；三项「当前值」
    // 从基准起步，局内升级由 grantXp 逐级追加，重开时再回到基准重新累积
    m_playerSpeedBase = playerSpeed;                                         // 移速基准
    m_playerMaxHpBase = config.getInt("player_hp", kDefaultPlayerHp);        // 生命上限基准
    m_attackDamageBase = config.getInt("attack_damage", kDefaultAttackDamage); // 攻击伤害基准
    m_playerSpeed = m_playerSpeedBase;   // 当前移速 = 基准（本局成长尚未发生）
    m_playerMaxHp = m_playerMaxHpBase;   // 当前生命上限 = 基准
    m_attackDamage = m_attackDamageBase; // 当前攻击伤害 = 基准

    // 升级曲线与每级强化：曲线参数直接注入 LevelCurve——它内部对
    // 0/负基数、小于 1 或 NaN 的倍率有完整的边界夹取（见其头文件，有单测）；
    // 强化值负数无玩法语义（升级反而倒扣属性），沿用 monster_max_count
    // 同款的防御写法：夹到 0 起（静默，注释即约定）
    m_levelCurve.base = config.getInt("xp_level_base", kDefaultXpLevelBase);
    m_levelCurve.growth = config.getFloat("xp_level_growth", kDefaultXpLevelGrowth);
    m_levelupHpBonus = std::max(0, config.getInt("levelup_max_hp_bonus", kDefaultLevelupHpBonus));
    m_levelupDamageBonus =
        std::max(0, config.getInt("levelup_damage_bonus", kDefaultLevelupDamageBonus));
    m_levelupSpeedBonus =
        std::max(0.0f, config.getFloat("levelup_speed_bonus", kDefaultLevelupSpeedBonus));

    // 负数会经 static_cast 变成巨大的 size_t 引发异常（与金币池时代的防护同款），
    // 所以先用 std::max 夹到 0 以上；0 只怪物（无敌人模式）是允许的合法配置。
    // 上限封顶在 currentMonsterCap 中再统一处理（kMaxMonsterCap = 池大小）
    m_monsterMaxCount = std::max(0, config.getInt("monster_max_count", kDefaultMonsterMaxCount));

    // 玩家生命从满值开始（restart 复用同一初值）；刷新时刻表从首刷延迟起算
    m_playerHp = m_playerMaxHp;                  // 生命回满：全新一局
    m_monsterNextSpawnTime = kFirstSpawnDelay;   // 首只怪物：局内 2 秒后出现

    // 冒烟观测点：日志确认场景切换实际发生（自动化测试据此断言流程）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "进入场景: Play");

    // 5) 加载可选的场景级纹理：背景图（拼 <exe目录>/assets/... 路径）。
    //    它不是游戏必需品——加载失败只记录警告并继续：背景缺失露出
    //    清屏深蓝兜底（玩家的武器素材由 Player::load 自行加载管理）
    const char* basePath = SDL_GetBasePath();
    if (basePath != nullptr)
    {
        // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
        const std::string bgPath = std::string(basePath) + "assets/background.png";
        // IMG_LoadTexture：读文件 -> 解码 PNG -> 一步创建 GPU 纹理
        m_background = IMG_LoadTexture(m_renderer, bgPath.c_str());
    }
    // 背景失败（拿不到基路径或图片读取失败）统一走这条警告
    if (m_background == nullptr)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "加载背景图失败（%s），使用纯色背景兜底", SDL_GetError());
    }

    // 6) 加载 UI 字体（assets/fonts/SmileySans-Oblique.ttf，中文开源字体）。
    //    路径拼装模式与背景图一致：以 exe 所在目录为基准。
    //    HUD 与标题各加载一份（字号不同）；Font::load 内部会记录失败警告，
    //    失败后对应文字安全跳过，玩法完全不受影响
    if (basePath != nullptr)
    {
        // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
        const std::string fontPath = std::string(basePath) + "assets/fonts/SmileySans-Oblique.ttf";
        // 两个字号彼此独立：一份加载失败不影响另一份
        m_hudFont.load(fontPath, kHudPtSize); // HUD/提示文字
        m_bigFont.load(fontPath, kBigPtSize); // 暂停/结束标题
    }

    // 7) 玩家纹理：三级回退（精灵表 -> 单帧图 -> 程序化纯色）在 Entity 内完成，
    //    这里失败只可能是最后的程序化兜底也失败（内存不足等极端情况）
    if (!m_player.load(m_renderer))
    {
        // 纹理创建失败：记录错误但不中断（该实体渲染时跳过，游戏不至于崩溃）
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "创建玩家纹理失败：%s", SDL_GetError());
    }

    // 8) 注入玩家配置：速度、加减速、行走动画节奏与攻击冷却。
    //    必须在 load 之后设置——load 会把内置默认帧时长先写进动画数据，
    //    配置值晚一步落定才能覆盖它（各 set 对非法值会忽略并警告）。
    //    速度注入的是「当前值」字段（= 基准起步；升级成长改的也是它）
    m_player.setSpeed(m_playerSpeed);
    m_player.setMotion(playerAccel, playerFriction);
    m_player.setFrameTime(playerAnimInterval);
    m_player.setAttackCooldown(attackCooldown);

    // 9) 查询窗口实际尺寸并缓存：刷新出生和后续边界计算都基于它
    SDL_GetWindowSize(m_window, &m_width, &m_height);

    // 10) 玩家归位到窗口中心：坐标指左上角，所以要减去自身宽高的一半。
    //     每次进入 PlayScene 都从这里开始 => 与「按 R 重开」的初始状态一致
    m_player.reset(m_width / 2.0f - m_player.width() / 2.0f,
                   m_height / 2.0f - m_player.height() / 2.0f);

    // 11) 预创建怪物对象池：resize 按池大小（kMonsterPoolSize = 数量上限封顶 12）
    //     构造槽位（此刻纹理为空、均为未激活）。注意：纹理不在创建时预加载——
    //     变体在「刷新时刻」才等权随机决定，纹理随变体由 loadVariant 按需加载
    //     （单帧 PNG 解码开销很小）；未激活的槽位不更新、不渲染、不参与碰撞
    m_monsters.resize(static_cast<size_t>(kMonsterPoolSize));

    // 12) 读取最佳战绩快照：本局的比较基准（缺失/旧格式/损坏自动按无记录处理，
    //     永不失败）。整局沿用这份快照——GameOver 结算时才比较与更新
    m_best = SaveData::load();

    // 13) 加载升级音效（可选增强）：素材缺失/无音频设备只警告，升级静默、
    //     流程照常（降级细节见 LevelupSfx.h 的契约）
    m_levelupSfx.load();
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
// 调用位置：update 的最开头——暂停/重开/回菜单在任何状态下都要能响应
//（包括 Paused/GameOver 的冻结分支；PlayScene 世界冻结但输入始终可用）
void PlayScene::handleInputEdges(const UpdateContext& ctx)
{
    // 本步的边沿采样（ctx.input 为 nullptr 时按「都没按」安全降级）。
    // 三个动作的语义与物理键位的对应关系由 config 的 bind_* 决定
    const bool pausePressed = ctx.input != nullptr && ctx.input->pressed("pause");
    const bool restartPressed = ctx.input != nullptr && ctx.input->pressed("restart");
    const bool menuPressed = ctx.input != nullptr && ctx.input->pressed("menu");

    // 暂停/恢复：在 Playing 与 Paused 之间切换（GameOver 下不响应）。
    // 去重标记：同一帧补多个固定步时 pressed 恒 true，只在「首次见到上升沿」
    // 的那一步执行切换，避免一帧内反复横跳
    if (pausePressed && !m_pauseHandled)
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

// 每帧更新：先处理输入动作边缘（任何状态都响应），再按状态推进战斗。
// Paused（暂停）与 GameOver（结束）都冻结玩家、怪物、宝石与计时——暂停
// 由此天然实现了「世界与计时冻结」（实体 update 的统一门控就在本函数）
void PlayScene::update(const UpdateContext& ctx)
{
    // 输入动作边缘：暂停/重开/回菜单（与状态无关，先于冻结分支处理——
    // 否则暂停后按 ESC/M 将无法恢复/回菜单）
    handleInputEdges(ctx);

    // 非 Playing 状态：世界冻结。
    if (m_state != PlayState::Playing)
    {
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
        return;
    }

    // 每帧查询窗口实际尺寸并缓存：玩家边界、刷新范围都基于它，
    // 窗口尺寸一旦变化，本帧逻辑立即跟随
    SDL_GetWindowSize(m_window, &m_width, &m_height);

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

    // 玩家使用上下文更新：内部按 ctx.input 的移动动作渐近移动、按
    // ctx.viewport* 做边界 clamp、按水平移动切换朝向翻转，并推进攻击计时
    m_player.update(ctx);

    // 玩家矩形只取一次：本步的碰撞检测与怪物追踪共用一个快照
    const SDL_FRect playerRect = m_player.rect();

    // 主动攻击结算：每个固定步按「左键按住 + 冷却就绪」尝试一次横扫——
    // 方向取鼠标瞄准（不索敌、挥空也出招），命中由扇形判定逐只结算
    resolvePlayerAttack(ctx, playerRect);

    // 怪物刷新（边缘持续刷新 + 随时间增强）：局内秒数累计 + 到点生成。
    // 存活计时与怪物难度计时共用同一时间基准（固定步长，暂停时冻结）
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

                // 等权随机变体（Slime/Goblin/Skelet 各 1/3）：SDL_rand(n) 取
                // [0,n) 的均匀整数——先定变体、再换皮、最后落位生成
                const auto variant = static_cast<MonsterType>(
                    SDL_rand(static_cast<int>(MonsterType::Count)));

                // 槽位复用换皮：按变体加载对应纹理（内部先释放旧纹理，
                // 无论上一个住户是哪个变体都不会泄漏）；失败自动走三级回退
                monster.loadVariant(m_renderer, variant);

                // 边缘内侧随机落位（避让玩家近旁），出生生命取自变体数值表
                monster.spawnEdge(m_width, m_height, playerRect);
                ++m_monsterSpawnedTotal; // 累计生成数 +1（日志编号自增）

                // 冒烟观测点：怪物出现日志（含变体名，自动化测试据此断言
                // 等权随机与三变体生效）
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "怪物出现（第 %d 只，%s）",
                            m_monsterSpawnedTotal, monsterTypeName(variant));
                break; // 本步至多生成一只
            }
        }
    }

    // 难度曲线（速度爬升）：每只怪物的本步追击速度 = 自身变体基础速度
    // （史莱姆 90 / 哥布林 110 / 骷髅 140） + 每秒成长 x 局内已过秒数，
    // 并统一封顶 kMonsterMaxSpeed（260——低于玩家速度，保证跑得掉）

    // 怪物追踪：每个存活怪物注入「本步目标 + 本步速度」后更新。
    // 「场景注入、实体执行」的分工让难度规则完全留在本层
    for (Monster& monster : m_monsters)
    {
        if (!monster.isAlive())
        {
            continue; // 空槽位不更新（也不渲染/碰撞）
        }

        // 本步速度 = 变体基础速度 + 成长 x 局内秒数（逐只计算：三变体基础值不同），
        // 封顶 kMonsterMaxSpeed 后注入
        const float speed = std::min(
            monster.baseSpeed() + m_monsterSpeedGrowth * m_monsterElapsed,
            kMonsterMaxSpeed);

        monster.setTarget(playerRect); // 注入追踪目标（玩家矩形 → 中心点）
        monster.setChaseSpeed(speed);  // 注入本步速度（难度曲线：随时间爬升）
        monster.update(ctx);           // 执行：移动 + 动画 + 闪白计时
    }

    // 经验宝石：推进旋转动画 -> 拾取判定 -> 统一清理
    for (XpGem& gem : m_gems)
    {
        // 播放旋转动画（基类默认 update：推进帧时间轴，与渲染帧率解耦）
        gem.update(ctx);

        // AABB 相交即拾取：标记 + 累计经验 + 播放「叮」音效；
        // 实际移除放在循环之后统一做（避免边遍历边删除）
        if (!gem.isPicked() && rectsOverlap(playerRect, gem.rect()))
        {
            gem.markPicked(); // 标记：本次遍历结束后统一清理

            // 经验结算（本批新增）：进度累计 + 按曲线循环结算 + 逐级强化与
            // 反馈（可能连升多级——grantXp 内部循环，日志逐级记录）
            grantXp(kGemXpValue);

            // 拾取音效沿用 coin.wav（与旧金币收集同一听觉反馈）
            m_audio->play(SoundId::Coin);

            // 冒烟观测点：拾取日志（自动化测试据此断言掉落-拾取链路）。
            // 经验展示为「当前级进度/本级需求」——升级后的进度重置肉眼可查
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "拾取经验宝石（进度 %d/%d，等级 %d）",
                        m_xp, m_levelCurve.needed(m_level), m_level);
        }
    }
    // 统一清理已被拾取的宝石（保持其余元素的地址不变）
    std::erase_if(m_gems, [](const XpGem& gem) { return gem.isPicked(); });

    // 接触伤害：仅玩家非无敌时整段检测（无敌期 = 保护窗口，跳过判定）。
    // 命中结算：扣生命、播 hit 音效、玩家进入无敌、怪物弹开；
    // 同一步至多结算一次（结算后玩家立即无敌，其余怪物不再判定）
    if (m_playerInvincibleLeft <= 0.0f)
    {
        for (Monster& monster : m_monsters)
        {
            // 空槽位或与本步玩家矩形不相交：检查下一只（逐只独立判定）
            if (!monster.isAlive() || !rectsOverlap(playerRect, monster.rect()))
            {
                continue;
            }

            // 1) 扣生命（夹到 0：日志展示永远不为负）
            m_playerHp = std::max(m_playerHp - m_monsterDamage, 0);
            // 冒烟观测点：受击日志（自动化测试据此断言扣血链路）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "被怪物击中，扣 %d 点生命（剩余 %d/%d）",
                        m_monsterDamage, m_playerHp, m_playerMaxHp);

            // 2) 打击音效（低频「闷响」）：与渲染闪烁共同构成「被撞」反馈
            m_audio->play(SoundId::Hit);

            // 3) 玩家无敌：碰撞检测从下一步起跳过；渲染上每 0.1s 显/隐交替
            m_playerInvincibleLeft = kPlayerInvincibleSeconds;

            // 4) 怪物弹开：沿「玩家中心 -> 怪物中心」方向瞬移 150px（沿用机制）。
            //    knockback 是瞬移类操作，内部已快照 prev=cur 防止插值拉丝
            const float playerCenterX = playerRect.x + playerRect.w * 0.5f;
            const float playerCenterY = playerRect.y + playerRect.h * 0.5f;
            monster.knockback(playerCenterX, playerCenterY,
                              static_cast<float>(m_width), static_cast<float>(m_height),
                              kContactKnockbackDistance);

            break; // 玩家已进入无敌：本步的其余怪物不再判定
        }
    }

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

// 主动攻击结算（每个固定步调用一次）：「左键按住 -> 方向 -> 出手 -> 扇形命中」。
// 「场景读输入/算方向、玩家管出手、场景做命中结算」的分工：
//   1. 左键没按住：本步不结算（主动攻击——不按不挥）；
//   2. 瞄准方向 = 玩家中心 -> 鼠标；零向量（鼠标压在玩家中心）时回退到
//      玩家当前面向（左/右），保证方向可预期、不出 NaN；
//   3. Player::tryAttack 出手（冷却门控在玩家侧；挥空也出招）；
//   4. 出手成功才做扇形结算：对每只存活怪物跑 AttackArc 判定，命中者
//      各自扣血/飘字/掉落或微击退（本批核心的「横扫多目标」）
void PlayScene::resolvePlayerAttack(const UpdateContext& ctx, const SDL_FRect& playerRect)
{
    // 无输入系统（旧调用方）：既没有鼠标位置也没有攻击动作，本步不结算
    if (ctx.input == nullptr)
    {
        return;
    }

    // 主动攻击门控：左键没按住就不出手——「不按不挥」是本批与自动攻击的
    // 根本区别。held 同时覆盖「单击挥一次」（按住的那一个固定步）与
    // 「按住连扫」（节奏由 Player 的冷却门控制）
    if (!ctx.input->held("attack"))
    {
        return;
    }

    // 玩家中心：瞄准向量的起点，也是扇形判定的圆心
    const float centerX = playerRect.x + playerRect.w * 0.5f; // 中心 x = 左上角 + 半宽
    const float centerY = playerRect.y + playerRect.h * 0.5f; // 中心 y = 左上角 + 半高

    // 瞄准向量 = 玩家中心 -> 鼠标位置（鼠标为窗口客户区坐标，与游戏世界
    // 同一坐标系；ctx.input 的位置是「本渲染帧的采样快照」，整帧共享）
    const Vec2 mouse = ctx.input->mousePosition();
    float aimX = mouse.x - centerX; // 水平分量（正右负左）
    float aimY = mouse.y - centerY; // 垂直分量（正下负上）

    // 零向量防御（鼠标恰好压在玩家中心）：没有方向可言，回退到玩家当前
    // 面向（右为 +1、左为 -1），保证横扫方向可预期且不产生 NaN
    if (aimX * aimX + aimY * aimY < kAimEpsilonSquared)
    {
        aimX = m_player.flipped() ? -1.0f : 1.0f; // 面向左侧/右侧
        aimY = 0.0f;                              // 水平方向
    }

    // 出手尝试：冷却未到返回 false（不结算）；「按住 + 冷却就绪」才走到
    // 下面的扇形结算——有没有怪物不在这次判断里（挥空也出招）
    if (!m_player.tryAttack(aimX, aimY))
    {
        return;
    }

    // 判定中轴取「表现所用的同一方向」（归一化单位向量）：tryAttack 内已
    // 归一化并保存，判定与画面因此严格同轴，不会因两次计算产生偏差
    const Vec2 swingDir = m_player.swingDirection();

    // 弧内判定参数：圆心 = 玩家中心；半径 = attack_range、总张角 =
    // attack_arc（纯函数见 game/AttackArc.h，边界行为有单测覆盖）
    const Vec2 attackOrigin{ centerX, centerY };

    // 命中统计：全弧扫完统一报告（挥空也记日志——「挥空也出招」的证据点）
    int hitCount = 0;

    // 逐只扫描存活怪物：命中弧内「所有」目标（本批多目标语义——不再是
    // 旧版的最近单体；每只独立结算，互不影响）
    for (Monster& monster : m_monsters)
    {
        if (!monster.isAlive())
        {
            continue; // 空槽位跳过
        }

        // 怪物矩形只取一次：本只的中心点与后续掉落位置共用同一份快照
        const SDL_FRect monsterRect = monster.rect();
        const float monsterCenterX = monsterRect.x + monsterRect.w * 0.5f; // 中心 x
        const float monsterCenterY = monsterRect.y + monsterRect.h * 0.5f; // 中心 y

        // 弧内判定：距离 ≤ 半径 且 与中轴夹角 ≤ 半张角（含边界；零向量
        // 防御等边界行为都在纯函数内，见 AttackArc.h）
        if (!isPointInAttackArc(attackOrigin, swingDir,
                                Vec2{ monsterCenterX, monsterCenterY },
                                m_attackRange, m_attackArc))
        {
            continue; // 弧外：本只不结算，继续检查下一只
        }

        ++hitCount; // 命中一只（同一只怪在一次出手里至多命中一次）

        // 1) 扣血（闪白在 takeDamage 内自动触发；返回是否致死）
        const bool died = monster.takeDamage(m_attackDamage);

        // 2) 伤害飘字：在怪物位置弹出「-当前伤害值」（上浮+淡出）。
        //    多目标同帧命中时逐只各弹一条——同帧多飘字正是横扫的观感
        char damageText[32]; // 容纳 "-" + 最长伤害数字，绰绰有余
        SDL_snprintf(damageText, sizeof(damageText), "-%d", m_attackDamage);
        spawnFloatingText(damageText, monsterCenterX, monsterCenterY - 20.0f,
                          SDL_Color{ 255, 255, 255, 255 }); // 白色：与金色升级飘字区分

        // 冒烟观测点：横扫命中日志（含剩余生命与局内时间——自动化测试据
        // 它断言多目标命中与连扫节奏；t 为局内秒数，同一次出手共享同一 t）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "横扫命中（剩余生命 %d，t=%.2f）", monster.hp(), m_survivalSeconds);

        if (died)
        {
            // 击杀结算：计数 +1、原地掉落经验宝石（位置取目标怪物的中心）
            ++m_killCount; // 本局击杀 +1（HUD/结算使用）
            dropGemAt(monsterCenterX, monsterCenterY); // 掉落：坐标即怪物中心

            // 冒烟观测点：击杀日志（自动化测试据此断言击杀链路）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "击杀怪物（累计 %d 杀）", m_killCount);
        }
        else
        {
            // 未死：微击退（沿「玩家中心 -> 怪物中心」方向远离玩家）。
            // 距离来自 attack_knockback（默认 30——由原 60 减半的「微」
            // 击退，保持追击压力、方便下一轮连扫补刀）
            monster.knockback(centerX, centerY,
                              static_cast<float>(m_width), static_cast<float>(m_height),
                              m_attackKnockback);
        }
    }

    // 挥空也出招的日志证据：本次出手成功但一只也没命中怪（此时也不索敌、
    // 不做任何命中结算——挥砍表现已在 Player 内照常播放）
    if (hitCount == 0)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "横扫挥空（未命中任何怪物，t=%.2f）", m_survivalSeconds);
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

// 经验结算：把 amount 点经验并入当前进度，按升级曲线循环结算（一枚宝石
// 可连升多级），并逐级发放强化与反馈。曲线计算走 LevelCurve 纯函数——
// 本函数只负责「把结果落实到局面」：三项强化、升级飘字、HUD 瞬闪、音效
void PlayScene::grantXp(int amount)
{
    // 曲线结算：返回新等级 / 当前级新进度 / 连升级数（纯函数，输入输出皆为数据）
    const LevelUpResult result = m_levelCurve.apply(m_level, m_xp, amount);

    // 记录升级前的等级：下面按「旧+1 .. 新」逐级发强化与反馈
    const int oldLevel = m_level;

    // 先落定结算结果：强化循环与结束界面读取的都是新状态
    m_xp = result.xp;       // 当前级进度（恒小于本级需求）
    m_level = result.level; // 新等级（等级只增不减）

    // 逐级发放（一枚宝石连升多级时每级都发一份——强化是「每级」语义）
    for (int levelReached = oldLevel + 1; levelReached <= m_level; ++levelReached)
    {
        // 1) 生命上限 +bonus 且即时回血等值（回血夹在上限内——从残血升级
        //    能立刻多出这一截生命，构成「升级即续航」的即时正反馈）
        m_playerMaxHp += m_levelupHpBonus; // 上限抬高
        m_playerHp = std::min(m_playerHp + m_levelupHpBonus, m_playerMaxHp); // 即时回血并夹取

        // 2) 攻击伤害 +bonus（此后横扫结算用新值，伤害飘字同步变大）
        m_attackDamage += m_levelupDamageBonus;

        // 3) 移速 +bonus 并注入玩家实体（下一固定步的移动立即使用新速度）
        m_playerSpeed += m_levelupSpeedBonus;
        m_player.setSpeed(m_playerSpeed);

        // 4) 冒烟观测点：升级日志（规格文案「升级！等级 N」——自动化测试断言点）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "升级！等级 %d", levelReached);

        // 5) 升级飘字（金色）挂在玩家上方；连升多级时逐条向上错开
        //    （每条偏移 26 像素，避免多级连升时文字完全重叠）
        const SDL_FRect playerRect = m_player.rect(); // 玩家当前位置（左上角）
        spawnFloatingText("升级！",
                          playerRect.x + playerRect.w * 0.5f,               // 水平中心
                          playerRect.y - 24.0f - (levelReached - oldLevel - 1) * 26.0f, // 头顶上方
                          SDL_Color{ 255, 215, 0, 255 },                    // 金色：升级高光
                          kLevelupTextLifeSeconds);                         // 比伤害数字停留更久

        // 6) 升级音效（游戏层直读 levelup.wav；音频不可用安全静默）
        m_levelupSfx.play();
    }

    // 有升级才触发 HUD 瞬闪：短暂窗口内等级数字金白交替（渲染端消费本计时）
    if (result.levelsGained > 0)
    {
        m_levelFlashLeft = kLevelFlashSeconds; // 重置为满值（连升多级只刷一次时长）
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

// 重开一局：把整局相关的数据全部恢复到初始状态
void PlayScene::restart()
{
    // 三项属性回基准：本局升级累积的成长全部撤销（基准值整局只读、从未被写）
    m_playerMaxHp = m_playerMaxHpBase;   // 生命上限回到配置初值
    m_attackDamage = m_attackDamageBase; // 攻击伤害回到配置初值
    m_playerSpeed = m_playerSpeedBase;   // 移速回到配置初值
    m_player.setSpeed(m_playerSpeed);    // 并重新注入玩家实体（立即生效）

    // 战斗数值复位：生命回满、击杀/等级/经验清零、无敌清零
    m_playerHp = m_playerMaxHp;    // 生命回满（上限已先回到基准）
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

    // 玩家归位到窗口中心：坐标指左上角，所以要减去自身宽高的一半。
    // reset 同时清零速度与攻击状态（冷却/挥剑表现），并把动画切回站姿
    m_player.reset(m_width / 2.0f - m_player.width() / 2.0f,
                   m_height / 2.0f - m_player.height() / 2.0f);

    // 怪物池全部回收：槽位标记未激活（不再更新/渲染/碰撞），
    // 等刷新时刻从空槽位重新生成；despawn 同时清掉追踪目标与闪白残留
    for (Monster& monster : m_monsters)
    {
        monster.despawn();
    }

    // 场上经验宝石清空（析构各自释放纹理；renderer 尚在，安全）
    m_gems.clear();

    // 场上漂浮文字清空：上一局未播完的伤害/升级飘字不跨局残留
    m_floatingTexts.clear();

    // 最后切回 Playing，新一局正式开始
    m_state = PlayState::Playing;

    // 冒烟观测点：确认 R 重开真正触发（自动化测试据此断言）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "重开一局");
}

// 每帧渲染：清屏 -> 背景 -> 玩家（含横扫表现）-> 怪物 -> 宝石 -> HUD。
// 不调用 SDL_RenderPresent（提交由应用外壳统一负责）。
// alpha 是插值系数，原样传给各实体——它们据此在上一固定步位置与当前
// 固定步位置之间平滑绘制
void PlayScene::render(float alpha)
{
    // 采样本渲染帧的间隔并更新 FPS 平滑值（只统计，不影响绘制内容）
    sampleFrameTime();

    // 设置清屏颜色。深蓝是背景图缺失时的兜底底色
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

    // 背景作为最底层画面绘制：目标矩形 = 渲染输出实际尺寸，保持尺寸无关——
    // 无论窗口多大，背景总是铺满。纹理缺失（加载失败）时整块跳过，
    // 清屏的深蓝色即为兜底背景
    if (m_background != nullptr)
    {
        // 查询渲染输出尺寸，得到背景目标矩形
        int outW = 0;
        int outH = 0;
        SDL_GetRenderOutputSize(m_renderer, &outW, &outH);
        // srcRect 传 nullptr 表示使用整张背景纹理
        const SDL_FRect bgDst{ 0.0f, 0.0f,
                               static_cast<float>(outW), static_cast<float>(outH) };
        SDL_RenderTexture(m_renderer, m_background, nullptr, &bgDst);
    }

    // 玩家按插值位置绘制；无敌期间每 0.1 秒显/隐交替（闪烁提示保护中）。
    // 相位判定：剩余无敌时间对「两倍半周期」取模，模值小于半周期时显示——
    // 随剩余时间递减自然交替。暂停/结束时剩余值冻结，相位一并冻结，
    // 画面保持静止（与「暂停=完全冻结」的承诺一致）
    const bool playerVisible =
        m_playerInvincibleLeft <= 0.0f ||
        std::fmod(m_playerInvincibleLeft, kPlayerBlinkHalfPeriod * 2.0f) < kPlayerBlinkHalfPeriod;
    if (playerVisible)
    {
        m_player.render(m_renderer, alpha);
    }

    // 怪物：只绘制存活槽位（与 update 同一门控——空槽位不可见）
    for (const Monster& monster : m_monsters)
    {
        if (monster.isAlive())
        {
            monster.render(m_renderer, alpha);
        }
    }

    // 说明：原「挥击弧光」特效已退役——攻击表现改由 Player 在自身 render
    // 里绘制武器素材（绕玩家中心 −60° 扫到 +60° 的横扫轨迹），
    // 此处不再有场景层特效

    // 经验宝石：逐颗按插值位置绘制（最上层——拾取目标永远清晰可辨）
    for (const XpGem& gem : m_gems)
    {
        gem.render(m_renderer, alpha);
    }

    // 漂浮文字（伤害/升级共用）：世界层最上层——叠加在实体之上、HUD 之下。
    // 暂停/结束时寿命已冻结，这里照常绘制即「静止画面」（位置/透明度不变）
    for (const FloatingText& text : m_floatingTexts)
    {
        text.render(m_renderer, m_hudFont);
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

    // Paused / GameOver 叠层加半透明暗化遮罩：把游戏世界（背景/玩家/怪物）
    // 整体压暗，让叠层文字从画面中浮出来。绘制顺序：遮罩放在最前——
    // 之后绘制的 FPS 行与叠层文字都压在遮罩之上，保持全亮
    if (m_state == PlayState::Paused || m_state == PlayState::GameOver)
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

    // ---- 生存 HUD（Playing 与 Paused 都显示）----
    // 暂停时保留完整 HUD：玩家能看清冻结时刻的生命/等级/经验/击杀/存活；
    // GameOver 不显示（结束界面有自己的结算排版）
    if (m_state == PlayState::Playing || m_state == PlayState::Paused)
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

        // 第 4/5 行：击杀 / 存活（白色，行距 44 像素——32 号字留 12 像素行间隔）
        char killText[48]; // 容纳 "击杀: " + 最长数字，绰绰有余
        SDL_snprintf(killText, sizeof(killText), "击杀: %d", m_killCount);
        m_hudFont.draw(m_renderer, killText, 20.0f, 146.0f);

        char survivalText[48]; // 容纳「存活: mm:ss」，绰绰有余
        formatSurvivalText(survivalText, sizeof(survivalText), m_survivalSeconds);
        m_hudFont.draw(m_renderer, survivalText, 20.0f, 190.0f);
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
