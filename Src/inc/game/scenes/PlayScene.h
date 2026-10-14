#ifndef __PLAYSCENE_H__
#define __PLAYSCENE_H__

#include "engine/Scene.h"       // Scene：场景基类
#include "engine/AudioSystem.h" // AudioSystem：音效系统（拾取/受击/结束/暂停触发播放）
#include "engine/Font.h"        // Font：TTF 字体资源（HUD/叠层文字，支持中文）
#include "game/Player.h"        // Player：玩家实体（移动 + 挥击 + 生命）
#include "game/Monster.h"       // Monster：追击怪物（对象池）
#include "game/XpGem.h"         // XpGem：怪物掉落的经验宝石
#include <list>                 // std::list：经验宝石集合（节点式容器，元素地址稳定）
#include <vector>               // std::vector：怪物对象池

// ============================================================
// PlayScene：对局场景（Roguelike 战斗核心）
// ============================================================
//
// 职责：
//   1. 资源：onEnter 加载背景图、挥击弧光、玩家/怪物纹理与字体
//      （背景与弧光可选，失败兜底；实体纹理各自析构兜底）；
//      析构时释放场景自持的背景与弧光纹理
//   2. 玩法（本批由 COIN RUSH 重构为战斗骨架）：
//      - WASD 移动、J 挥击（面朝方向近战：冷却 0.4s、伤害 25、约 64x56 判定）；
//      - 怪物从窗口四边内侧持续刷新并随时间增强（数量上限初始 4、每 20s +2、
//        封顶 12；刷新间隔初始 2.5s、随局内时间收紧、下限 1s；首刷延迟 2s），
//        追踪玩家、接触伤害 10；
//      - 怪物 30 点生命：受击扣血 + 闪白 + 击退；死亡 +1 击杀并原地掉落经验宝石；
//      - 玩家 100 点生命：被撞扣血 + 0.8s 无敌闪烁 + 怪物弹开；拾取宝石累积经验
//        （升级系统留待下一批）；生命归零 -> GameOver；
//      - ESC 暂停/恢复、R 重开、M 回主菜单（输入系统 pressed 边缘，可改键）。
//   3. 音效：拾取播 coin、被撞播 hit、生命归零播 gameover、
//      ESC 暂停/恢复播 pause（经 onEnter 注入的 AudioSystem；降级环境安全空操作）
//   4. 配置：onEnter 时读取玩家速度/生命/攻击参数与怪物速度曲线/刷新参数/
//      生命/伤害；缺键/坏值自动回退内置默认值
//   5. 渲染：背景 -> 玩家 -> 怪物 -> 挥击弧光 -> 经验宝石 -> 生存 HUD
//      （左上：HP 血条+文字；其下：击杀/存活/经验；左下角 FPS/帧时间，
//      F3 切换显隐——调试开关维持事件式）
//
// 「每次进入 = 全新一局」：每局都是新构造的 PlayScene 对象，
// 数据成员的默认值就是初始状态；onEnter 把玩家与怪物池布置到位，
// R 重开走 restart 复位运行时状态
//
// 状态机（PlayState）：
//   Playing  ：接受输入、推进战斗、检测碰撞与刷新、存活时间正计时
//   Paused   ：世界冻结（含存活计时），ESC 恢复、M 回主菜单
//   GameOver ：世界冻结（仅因玩家生命归零触发），显示击杀/存活结算，
//              等待 R 重开或 M 回主菜单
// ============================================================

// 对局状态机：用枚举而不是布尔标志，状态互斥关系一眼可见
enum class PlayState
{
    Playing,  // 正在游戏：接受输入、推进战斗、检测碰撞与刷新
    Paused,   // 暂停：世界冻结，ESC 恢复、M 回主菜单
    GameOver  // 本局结束：世界冻结，等待玩家按 R 重开或按 M 回主菜单
};

class PlayScene : public Scene
{
public:
    PlayScene() = default; // 成员默认值即初始局面，无需自定义构造
    ~PlayScene() override;  // 析构释放场景自持纹理（此时 renderer 尚在，安全）

    // 进入场景：保存 renderer 与音效系统、读取配置、加载全部纹理、
    // 布置玩家与怪物对象池
    void onEnter(SDL_Renderer* renderer, const Config& config, AudioSystem& audio) override;

    // 事件：仅 F3 切换 FPS 显示（维持事件式调试开关）。
    // 暂停/重开/回菜单已迁移为 update 中的 pressed 动作边缘（见 handleInputEdges）
    void handleEvent(const SDL_Event& event) override;

    // 每帧更新：先处理输入动作边缘（暂停/重开/回菜单，任何状态都响应），
    // 再仅 Playing 状态推进战斗（Paused/GameOver 冻结）。
    // 冻结时会把各实体的插值快照对齐到当前位置，保证画面完全静止不漂移
    void update(const UpdateContext& ctx) override;

    // 每帧渲染：清屏、背景、实体、挥击弧光、HUD。alpha 是插值系数，原样转发给实体
    void render(float alpha) override;

private:
    // 重开一局：生命/击杀/经验复位、玩家归位、回收全部怪物与宝石、
    // 刷新计时重排
    void restart();

    // 输入动作的按下边缘处理：暂停/恢复（Playing <-> Paused）、
    // 重开（GameOver 下 R）、回菜单（Paused/GameOver 下 M）。
    // pressed 在整帧的所有固定步都为 true，用「本帧已处理」标记去重，
    // 保证每次按下恰好触发一次（行为与事件式完全一致）
    void handleInputEdges(const UpdateContext& ctx);

    // 结算一次挥击：对判定矩形内的每只存活怪物逐只结算（扣血/死亡/掉宝）。
    // 挥击是「单次判定」而非持续伤害——每只怪物每次挥击至多结算一次
    void resolveAttack(const SDL_FRect& playerRect);

    // 掉落一颗经验宝石到 (x, y)（通常为怪物中心）：创建对象、加载纹理、落位
    void dropGemAt(float x, float y);

    // 当前存活的怪物数（对象池中 alive 的槽位数；刷新上限的门控依据）
    int aliveMonsterCount() const;

    // 当前怪物数量上限：初始值（monster_max_count）+ 每 20s +2，封顶 12
    int currentMonsterCap() const;

    // 当前刷新间隔（秒）：初始值（monster_spawn_interval）随局内时间收紧，
    // 下限 1s
    float currentSpawnInterval() const;

    // 绘制 HUD 文字（生存 HUD：HP/击杀/存活/经验；FPS 调试行；暂停/结束叠层）
    void renderHud();

    // 采样本渲染帧的间隔时间并做 EMA 平滑（render 每帧开头调用）。
    // FPS 反映的是渲染性能，与固定步长逻辑无关，暂停时也照常采样
    void sampleFrameTime();

private:
    // ---- 配置项缺省值（与 config/game.ini 对应键一致；缺键/坏值回退）----
    static constexpr int kDefaultPlayerHp = 100;                // 玩家生命上限
    static constexpr int kDefaultAttackDamage = 25;             // 玩家挥击伤害
    static constexpr float kDefaultMonsterSpeed = 110.0f;       // 怪物基础移动速度（像素/秒）
    static constexpr float kDefaultMonsterSpeedGrowth = 2.5f;   // 怪物速度成长（像素/秒²，按局内秒数叠加）
    static constexpr float kDefaultMonsterSpawnInterval = 2.5f; // 刷新间隔初始值（秒）
    static constexpr int kDefaultMonsterMaxCount = 4;           // 数量上限初始值（只）
    static constexpr int kDefaultMonsterHp = 30;                // 怪物生命值
    static constexpr int kDefaultMonsterDamage = 10;            // 怪物接触伤害

    // ---- 玩法常量（代码固定，不进配置；改动须连同注释一起审视）----
    static constexpr int kMonsterPoolSize = 12;          // 怪物对象池槽位数 = 数量上限封顶值
    static constexpr float kFirstSpawnDelay = 2.0f;      // 首刷延迟（秒）：开局 2 秒后第 1 只出现
    static constexpr float kCapGrowthPeriod = 20.0f;     // 数量上限成长周期（秒）：每 20 秒 +2
    static constexpr int kCapGrowthStep = 2;             // 每次成长的增量（只）
    static constexpr int kMaxMonsterCap = 12;            // 数量上限封顶（只；与池大小一致）
    static constexpr float kSpawnIntervalFloor = 1.0f;   // 刷新间隔下限（秒）：收紧到此为止
    static constexpr float kSpawnIntervalDecay = 0.025f; // 间隔收紧速率（秒/秒）：默认约 60 秒从 2.5 收到 1.0

    // 怪物速度上限（像素/秒）：不超过玩家默认速度 300，保证玩家永远可以甩开
    // 追兵（「可逃」是难度曲线的设计红线——速度成长不能把玩家逼进死局）。
    // 默认参数下约 60 秒到达上限，此后维持 260 不再增长
    static constexpr float kMonsterMaxSpeed = 260.0f;

    // 玩家被撞中后的无敌时长（秒）：期间跳过怪物碰撞检测、渲染每 0.1 秒闪烁。
    // 固定玩法规则（不进配置文件）：0.8 秒足够拉开与击退后怪物的距离
    static constexpr float kPlayerInvincibleSeconds = 0.8f;

    // 无敌闪烁的「半周期」（秒）：显 / 隐各占 0.1 秒，即每 0.1 秒切换一次
    static constexpr float kPlayerBlinkHalfPeriod = 0.1f;

    // 接触伤害的怪物弹开距离（像素）：沿用既有机制 150——把怪物弹远，
    // 配合 0.8s 无敌构成「受击保护窗口」
    static constexpr float kContactKnockbackDistance = 150.0f;

    // 挥击命中的怪物击退距离（像素）：比接触弹开短——保持追击压力，
    // 让玩家能跟上补刀（击杀需要两次命中）
    static constexpr float kAttackKnockbackDistance = 60.0f;

    // 每颗经验宝石的经验值（代码常量）：升级系统下一批落地，本批只累计与显示
    static constexpr int kGemXpValue = 10;

    // 挥击弧光素材的显示尺寸（像素）：与 scripts/generate_assets 生成的
    // slash.png 边长一致（96x96），以玩家中心为圆心旋转绘制
    static constexpr float kSlashSize = 96.0f;

    static constexpr float kHudPtSize = 32.0f; // HUD/提示文字字号（像素高，小数需求清晰）
    static constexpr float kBigPtSize = 64.0f; // 暂停/结束标题字号（大号观感）

    // FPS 调试行专用：仍走 SDL 内置调试字体（8x8 点阵，仅 ASCII），
    // 缩放 2 倍 => 16 屏幕像素高，与改造前的调试行观感完全一致
    static constexpr float kFpsScale = 2.0f;

    SDL_Renderer* m_renderer = nullptr; // onEnter 保存的渲染器（只借用，不持有所有权）

    // onEnter 注入的音效系统指针（所有权在 GameDev，受管理期间必然非空）。
    // 对局用它在四处触发：拾取（update）、被撞（update）、结束（update）、
    // 暂停/恢复（handleInputEdges）
    AudioSystem* m_audio = nullptr;

    // 由 renderer 反查的窗口指针：玩法边界/刷新范围需要窗口整数尺寸
    // （与渲染基元无关的「逻辑尺寸」沿用窗口坐标系，保持既有数值）
    SDL_Window* m_window = nullptr;

    // 背景纹理：加载失败时为 nullptr，render 阶段会跳过，
    // 露出清屏深蓝色兜底（背景不是游戏必需品，加载失败不阻塞启动）
    SDL_Texture* m_background = nullptr;

    // 挥击弧光纹理（assets/slash.png）：加载失败时为 nullptr，挥击特效跳过，
    // 命中判定与玩法完全不受影响
    SDL_Texture* m_slashTexture = nullptr;

    // 字体资源：一个实例对应一个字号，加载失败时绘制安全跳过（Font 内部已警告）
    Font m_hudFont; // HUD/提示文字（FPS 以外的文字、操作提示、结束列表）
    Font m_bigFont; // 大号标题（暂停 / 游戏结束）

    Player m_player;                 // 玩家实体
    std::vector<Monster> m_monsters; // 怪物对象池（预创建 kMonsterPoolSize 个槽位）
    std::list<XpGem> m_gems;         // 场上未被拾取的经验宝石

    // ---- 战斗配置（onEnter 读取；R 重开不重读，整局沿用同一套调参）----
    int m_playerMaxHp = kDefaultPlayerHp;                 // 玩家生命上限（player_hp）
    int m_attackDamage = kDefaultAttackDamage;            // 挥击伤害（player_attack_damage）
    float m_monsterSpeed = kDefaultMonsterSpeed;          // 怪物基础速度（monster_speed）
    float m_monsterSpeedGrowth = kDefaultMonsterSpeedGrowth; // 速度成长（monster_speed_growth）
    float m_monsterSpawnInterval = kDefaultMonsterSpawnInterval; // 刷新间隔初始值（monster_spawn_interval）
    int m_monsterMaxCount = kDefaultMonsterMaxCount;      // 数量上限初始值（monster_max_count）
    int m_monsterHp = kDefaultMonsterHp;                  // 怪物生命值（monster_hp）
    int m_monsterDamage = kDefaultMonsterDamage;          // 接触伤害（monster_damage）

    // ---- 战斗运行时状态（restart 复位）----
    int m_playerHp = kDefaultPlayerHp;   // 玩家当前生命（归零 -> GameOver）
    int m_killCount = 0;                 // 本局累计击杀数（击杀日志/HUD/结算使用）
    int m_xp = 0;                        // 本局累计经验（升级逻辑下一批；HUD 展示）

    // 本局存活时间（秒）：仅 Playing 正计时（暂停/结束冻结），restart 清零。
    // 从「倒计时」改为「正向累积」——GameOver 仅由生命归零触发，存活时间
    // 成为结算展示的成就性数据（mm:ss 格式在 HUD/结束界面呈现）
    float m_survivalSeconds = 0.0f;

    // 玩家剩余无敌时间（秒；0 = 未处于无敌）；受击时重置为满值，
    // 仅 Playing 状态递减——暂停/结束时冻结，闪烁相位随之冻结
    float m_playerInvincibleLeft = 0.0f;

    // ---- 怪物刷新运行时（restart 复位）----
    float m_monsterElapsed = 0.0f;       // 局内已过秒数（仅 Playing 递增；难度计时基准）
    float m_monsterNextSpawnTime = 0.0f; // 下一只怪物的刷新时刻（局内秒）
    int m_monsterSpawnedTotal = 0;       // 本局累计生成数（日志「第 N 只」/冒烟断言用）

    PlayState m_state = PlayState::Playing; // 当前对局状态

    // ---- 输入动作边缘的去重标记 ----
    // 语义：记录「上一次固定步采样时该动作的 pressed 值」。
    // pressed 在整帧的所有固定步都为 true（引擎按渲染帧采样），若不做去重，
    // 一帧内补多个固定步会让暂停这类切换动作被响应多次；该标记把响应
    // 折叠为「每个上升沿恰好一次」。动作松开后标记随采样自然复位
    bool m_pauseHandled = false;   // 暂停/恢复动作本帧是否已处理
    bool m_restartHandled = false; // 重开动作本帧是否已处理
    bool m_menuHandled = false;    // 回菜单动作本帧是否已处理

    // ---- FPS / 帧时间显示 ----
    // 显示内容：左下角 "FPS: 59.9 | 16.7 ms"。数值来自渲染帧间隔的
    // 指数滑动平均（EMA），只反映渲染表现，不参与任何玩法逻辑
    bool m_fpsVisible = true;      // F3 切换显隐；默认显示（学习项目便于观察）
    bool m_hasFrameSample = false; // 是否已采到首个有效帧间隔（首帧无间隔可测）
    float m_smoothedFrameMs = 0.0f;  // 帧间隔 EMA（毫秒）；FPS = 1000 / 该值

    // 上次 render 的时刻（性能计数器读数）：两次读数之差即渲染帧间隔。
    // 初值 0 表示「尚无参照」，第一次 render 只记录不计算
    Uint64 m_lastFrameCounter = 0;

    // 窗口实际尺寸缓存（每次查询窗口后更新）：
    // 刷新范围这类需要整数的场合直接用它
    int m_width = 0;
    int m_height = 0;
};

#endif // !__PLAYSCENE_H__
