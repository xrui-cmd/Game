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
// 玩家/怪物/宝石的纹理由各自 Entity 的析构函数兜底释放
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
    //      attack_cooldown         自动攻击冷却（秒）
    //      attack_damage           自动攻击伤害（点）
    //      attack_range            自动攻击范围（像素；索敌距离）
    //      monster_speed_growth    怪物速度成长（像素/秒²，按局内秒数叠加）
    //      monster_spawn_interval  刷新间隔初始值（秒，随局内时间收紧）
    //      monster_max_count       数量上限初始值（只，随局内时间 +2）
    //      monster_damage          怪物接触伤害
    const float playerSpeed = config.getFloat("player_speed", Player::kDefaultSpeed);
    const float playerAccel = config.getFloat("player_accel", Player::kDefaultAccel);
    const float playerFriction = config.getFloat("player_friction", Player::kDefaultFriction);
    const float playerAnimInterval =
        config.getFloat("player_anim_interval", Player::kDefaultFrameTime);
    m_playerMaxHp = config.getInt("player_hp", kDefaultPlayerHp);
    const float attackCooldown =
        config.getFloat("attack_cooldown", Player::kDefaultAttackCooldown);
    m_attackDamage = config.getInt("attack_damage", kDefaultAttackDamage);
    m_attackRange = config.getFloat("attack_range", kDefaultAttackRange);
    m_monsterSpeedGrowth = config.getFloat("monster_speed_growth", kDefaultMonsterSpeedGrowth);
    m_monsterSpawnInterval =
        config.getFloat("monster_spawn_interval", kDefaultMonsterSpawnInterval);
    m_monsterDamage = config.getInt("monster_damage", kDefaultMonsterDamage);

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

    // 8) 注入玩家配置：速度、加减速、行走动画节奏与自动攻击冷却。
    //    必须在 load 之后设置——load 会把内置默认帧时长先写进动画数据，
    //    配置值晚一步落定才能覆盖它（各 set 对非法值会忽略并警告）
    m_player.setSpeed(playerSpeed);
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

    // 玩家使用上下文更新：内部按 ctx.input 的移动动作渐近移动、按
    // ctx.viewport* 做边界 clamp、按水平移动切换朝向翻转，并推进攻击计时
    m_player.update(ctx);

    // 玩家矩形只取一次：本步的碰撞检测与怪物追踪共用一个快照
    const SDL_FRect playerRect = m_player.rect();

    // 自动攻击结算：每个固定步尝试一次「索敌 -> 出手 -> 命中」——
    // 没有目标不发招、冷却未到不出手（两道门都在 resolveAutoAttack 内）
    resolveAutoAttack(playerRect);

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
            gem.markPicked();    // 标记：本次遍历结束后统一清理
            m_xp += kGemXpValue; // 本批只累积经验值（升级系统下一批）

            // 拾取音效沿用 coin.wav（与旧金币收集同一听觉反馈）
            m_audio->play(SoundId::Coin);

            // 冒烟观测点：拾取日志（自动化测试据此断言掉落-拾取链路）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "拾取经验宝石（经验 %d）", m_xp);
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
    }
}

// 自动攻击结算（每个固定步调用一次）：「索敌 -> 出手 -> 单体命中」。
// 「场景选目标、玩家管出手」的分工：玩家实体不知道怪物存在，本函数找出
// 攻击范围内「最近的存活怪物」后调用 Player::tryAutoAttack；出手成功
// （冷却就绪）才结算——每次出手只命中这一个目标（多目标扩展留后续批次）
void PlayScene::resolveAutoAttack(const SDL_FRect& playerRect)
{
    // 玩家中心：索敌距离的基准点（与怪物中心之间按「中心距」比较——
    // 距离既定的两个方块，中心距与边缘距只差一个常量偏移）
    const float playerCenterX = playerRect.x + playerRect.w * 0.5f; // 中心 x = 左上角 + 半宽
    const float playerCenterY = playerRect.y + playerRect.h * 0.5f; // 中心 y = 左上角 + 半高

    // 索敌半径的平方：比较用平方距离（每步都要遍历池，省一次 sqrt）
    const float rangeSq = m_attackRange * m_attackRange;

    Monster* nearest = nullptr; // 范围内最近的存活怪物（nullptr = 无目标不发招）
    float nearestDistSq = 0.0f; // 最近距离平方（仅当 nearest 非空时有意义）

    // 逐只扫描存活怪物找最近者；池上限 12，线性遍历成本可忽略
    for (Monster& monster : m_monsters)
    {
        if (!monster.isAlive())
        {
            continue; // 空槽位跳过
        }

        // 怪物中心到玩家中心的位移与平方距离（比较用平方，等效且更快）
        const float dx = monster.rect().x + monster.width() * 0.5f - playerCenterX;
        const float dy = monster.rect().y + monster.height() * 0.5f - playerCenterY;
        const float distSq = dx * dx + dy * dy;

        if (distSq > rangeSq)
        {
            continue; // 攻击范围外：不参与索敌（更近的候选也不可能是它）
        }

        // 首个范围内目标直接当选；其后只接受更近者（严格小于保证稳定）
        if (nearest == nullptr || distSq < nearestDistSq)
        {
            nearest = &monster;   // 记住这一只：当前最近候选
            nearestDistSq = distSq; // 更新最近距离
        }
    }

    // 无目标：本步不发招（「无目标不出手」的兑现点——冷却也不会被消耗）
    if (nearest == nullptr)
    {
        return;
    }

    // 目标中心：出手方向与掉宝位置的参照点
    const float targetCenterX = nearest->rect().x + nearest->width() * 0.5f; // 目标中心 x
    const float targetCenterY = nearest->rect().y + nearest->height() * 0.5f; // 目标中心 y

    // 出手尝试：冷却未到返回 false（不出手也不结算）；出手成功才继续
    if (!m_player.tryAutoAttack(targetCenterX, targetCenterY))
    {
        return;
    }

    // 命中结算（单体）：扣血（闪白在 takeDamage 内自动触发）+ 死亡判定
    const bool died = nearest->takeDamage(m_attackDamage);

    // 冒烟观测点：自动攻击命中日志（含剩余生命——击杀这一击记为 0）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "自动攻击命中（剩余生命 %d）", nearest->hp());

    if (died)
    {
        // 击杀结算：计数 +1、原地掉落经验宝石（位置取目标怪物的中心）
        ++m_killCount; // 本局击杀 +1（HUD/结算使用）
        dropGemAt(targetCenterX, targetCenterY); // 掉落：坐标即怪物中心

        // 冒烟观测点：击杀日志（自动化测试据此断言击杀链路）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "击杀怪物（累计 %d 杀）", m_killCount);
    }
    else
    {
        // 未死：短距击退（沿「玩家中心 -> 怪物中心」方向远离玩家），
        // 保持追击压力、给冷却窗口留出下一次出手的空间
        nearest->knockback(playerCenterX, playerCenterY,
                           static_cast<float>(m_width), static_cast<float>(m_height),
                           kAttackKnockbackDistance);
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
    // 战斗数值复位：生命回满、击杀/经验清零、无敌清零
    m_playerHp = m_playerMaxHp;    // 生命回满（上限用 onEnter 读到的配置值）
    m_killCount = 0;               // 击杀清零
    m_xp = 0;                      // 经验清零
    m_survivalSeconds = 0.0f;      // 存活计时清零：新一局从 00:00 起算
    m_playerInvincibleLeft = 0.0f; // 新一局从非无敌开始

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

    // 最后切回 Playing，新一局正式开始
    m_state = PlayState::Playing;

    // 冒烟观测点：确认 R 重开真正触发（自动化测试据此断言）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "重开一局");
}

// 每帧渲染：清屏 -> 背景 -> 玩家（含挥剑表现）-> 怪物 -> 宝石 -> HUD。
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

    // 说明：原「挥击弧光」特效已随本批改版退役——攻击表现改由 Player
    // 在自身 render 里绘制武器素材（挥剑前探+收回），此处不再有场景层特效

    // 经验宝石：逐颗按插值位置绘制（最上层——拾取目标永远清晰可辨）
    for (const XpGem& gem : m_gems)
    {
        gem.render(m_renderer, alpha);
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
    // 暂停时保留完整 HUD：玩家能看清冻结时刻的生命/击杀/存活/经验；
    // GameOver 不显示（结束界面有自己的结算排版）
    if (m_state == PlayState::Playing || m_state == PlayState::Paused)
    {
        // 血条几何常量（左上角第一块内容）
        constexpr float kBarX = 20.0f;    // 血条左边界（与后续文字同一条左边线）
        constexpr float kBarY = 22.0f;    // 血条上边界
        constexpr float kBarW = 200.0f;   // 血条满宽（像素）
        constexpr float kBarH = 22.0f;    // 血条高度（像素）

        // 血条底色（深红）：先铺满整条，剩余生命的亮红前景覆盖其上——
        // 即使生命很少，也能从底色的存在看出「总量的量级」
        SDL_SetRenderDrawColor(m_renderer, 90, 24, 24, 255);
        const SDL_FRect barBack{ kBarX, kBarY, kBarW, kBarH };
        SDL_RenderFillRect(m_renderer, &barBack);

        // 血条前景（亮红）：宽度 = 满宽 x 剩余生命比例（0 血时宽度为 0）。
        // 比例计算防御除零（上限配置为 0 的极端情形按空血条画）
        const float hpRatio = (m_playerMaxHp > 0)
            ? static_cast<float>(m_playerHp) / static_cast<float>(m_playerMaxHp)
            : 0.0f;
        SDL_SetRenderDrawColor(m_renderer, 220, 60, 60, 255);
        const SDL_FRect barFill{ kBarX, kBarY, kBarW * hpRatio, kBarH };
        SDL_RenderFillRect(m_renderer, &barFill);

        // 血条右侧：「HP 当前/上限」文字（白色，与血条纵向居中对齐）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        char hpText[48]; // 容纳 "HP 100/100"，绰绰有余
        SDL_snprintf(hpText, sizeof(hpText), "HP %d/%d", m_playerHp, m_playerMaxHp);
        m_hudFont.draw(m_renderer, hpText, kBarX + kBarW + 12.0f, 16.0f);

        // 血条下方三行：击杀 / 存活 / 经验（白色，行距 44 像素——
        // HUD 字号 32 留出 12 像素行间隔，三行互不重叠）
        char killText[48]; // 容纳 "击杀: " + 最长数字，绰绰有余
        SDL_snprintf(killText, sizeof(killText), "击杀: %d", m_killCount);
        m_hudFont.draw(m_renderer, killText, 20.0f, 64.0f);

        char survivalText[48]; // 容纳「存活: mm:ss」，绰绰有余
        formatSurvivalText(survivalText, sizeof(survivalText), m_survivalSeconds);
        m_hudFont.draw(m_renderer, survivalText, 20.0f, 108.0f);

        char xpText[48]; // 容纳 "经验: " + 最长数字，绰绰有余
        SDL_snprintf(xpText, sizeof(xpText), "经验: %d", m_xp);
        m_hudFont.draw(m_renderer, xpText, 20.0f, 152.0f);
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
        // GameOver 界面（结算）：游戏结束 + 击杀/存活两行成绩 + 两条操作提示，
        // 全部水平居中、围绕窗口中心排布（不显示等级——升级系统在下一批）

        // 水平中心：各行文字共用的对齐基准（垂直位置逐行下移）
        const float screenCenterX = screenW / 2.0f;

        // 第一行：游戏结束（白色大号标题）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        m_bigFont.drawCentered(m_renderer, "游戏结束", screenCenterX, screenH / 2.0f - 80.0f);

        // 第二行：本局击杀数（黄色）——冒烟观测点：GameOver 结算内容
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 0, 255);
        char killText[48]; // 容纳 "击杀: " + 最长数字，绰绰有余
        SDL_snprintf(killText, sizeof(killText), "击杀: %d", m_killCount);
        m_hudFont.drawCentered(m_renderer, killText, screenCenterX, screenH / 2.0f - 10.0f);

        // 第三行：本局存活时长（黄色，mm:ss）
        char survivalText[48]; // 容纳「存活: mm:ss」，绰绰有余
        formatSurvivalText(survivalText, sizeof(survivalText), m_survivalSeconds);
        m_hudFont.drawCentered(m_renderer, survivalText, screenCenterX, screenH / 2.0f + 40.0f);

        // 第四行：重开提示（白色）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        m_hudFont.drawCentered(m_renderer, "按 R 重新开始", screenCenterX, screenH / 2.0f + 100.0f);

        // 第五行：回菜单提示（白色）。行距与上一行保持一致
        m_hudFont.drawCentered(m_renderer, "按 M 返回菜单", screenCenterX, screenH / 2.0f + 160.0f);
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
