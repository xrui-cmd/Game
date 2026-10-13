#ifndef __PLAYSCENE_H__
#define __PLAYSCENE_H__

#include "engine/Scene.h"       // Scene：场景基类
#include "engine/AudioSystem.h" // AudioSystem：音效系统（收币/结束/暂停/被撞触发播放）
#include "engine/Font.h"        // Font：TTF 字体资源（HUD/叠层文字，支持中文）
#include "game/Player.h"        // Player：玩家实体
#include "game/Coin.h"          // Coin：金币实体
#include "game/Enemy.h"         // Enemy：追击敌人的实体
#include <vector>               // std::vector：金币与敌人集合

// ============================================================
// PlayScene：对局场景（原 GameDev 的全部玩法逻辑迁入）
// ============================================================
//
// 职责：
//   1. 资源：在 onEnter 加载背景图（可选，失败兜底）、玩家、金币与敌人纹理；
//      析构时释放背景纹理（玩家/金币/敌人纹理由各自 Entity 析构兜底）
//   2. 玩法：WASD 移动、金币碰撞计分、倒计时、ESC 暂停/恢复、
//      R 重开、M 回主菜单；敌人按难度曲线出现并追踪玩家（速度随局内
//      时间爬升、按间隔增援），碰撞扣时、击退与无敌保护
//   3. 音效：收币播 coin、倒计时归零播 gameover、ESC 暂停/恢复播 pause、
//      被敌人撞中播 hit（经 onEnter 注入的 AudioSystem；降级环境下为安全空操作）
//   4. 配置：onEnter 时从注入的 Config 读取对局时长、金币数量、
//      玩家速度与动画节奏、敌人速度与生成节奏；缺键/坏值自动回退内置默认值
//   5. 渲染：背景 -> 玩家 -> 敌人 -> 金币 -> HUD 文字叠层（含 FPS/帧时间显示，
//      左下角常显，F3 切换显隐——学习项目默认打开便于观察渲染表现）
//
// 「每次进入 = 全新一局」：每局都是新构造的 PlayScene 对象，
// 数据成员的默认值就是初始状态（分数 0、倒计时满值）；onEnter 里
// 把玩家、金币与敌人对象池布置到位即可，不需要额外的「重置场景」逻辑
//
// 状态机（PlayState）：
//   Playing  ：接受输入、倒计时递减、检测金币/敌人碰撞
//   Paused   ：世界与倒计时冻结，ESC 恢复、M 回主菜单
//   GameOver ：世界冻结，等待玩家按 R 重开或按 M 回主菜单
// ============================================================

// 对局状态机：用枚举而不是布尔标志，扩展「暂停」等状态后依然直观
enum class PlayState
{
    Playing,  // 正在游戏：接受输入、倒计时递减、检测金币/敌人碰撞
    Paused,   // 暂停：世界与倒计时冻结，ESC 恢复、M 回主菜单
    GameOver  // 本局结束：世界冻结，等待玩家按 R 重开或按 M 回主菜单
};

class PlayScene : public Scene
{
public:
    PlayScene() = default; // 成员默认值即初始局面，无需自定义构造
    ~PlayScene() override;  // 析构释放背景纹理（此时 renderer 尚在，安全）

    // 进入场景：保存 renderer 与音效系统、读取配置、加载全部纹理、
    // 布置玩家、金币与敌人对象池
    void onEnter(SDL_Renderer* renderer, const Config& config, AudioSystem& audio) override;

    // 事件：ESC 暂停/恢复、GameOver 下按 R 重开、Paused/GameOver 下按 M
    // 回主菜单、F3 切换 FPS 显示（任何状态都响应）
    void handleEvent(const SDL_Event& event) override;

    // 每帧更新：仅 Playing 状态推进移动/碰撞/倒计时（Paused/GameOver 冻结）。
    // 冻结时会把各实体的插值快照对齐到当前位置，保证画面完全静止不漂移
    void update(const UpdateContext& ctx) override;

    // 每帧渲染：清屏、背景、实体、HUD。alpha 是插值系数，原样转发给实体
    void render(float alpha) override;

private:
    // 重开一局：重置分数、倒计时、玩家位置、全部金币与敌人状态
    void restart();

    // 绘制 HUD 文字（分数、倒计时、FPS、暂停叠层、结束界面）
    void renderHud();

    // 采样本渲染帧的间隔时间并做 EMA 平滑（render 每帧开头调用）。
    // FPS 反映的是渲染性能，与固定步长逻辑无关，暂停时也照常采样
    void sampleFrameTime();

private:
    // 配置项缺省值：与改造前的硬编码常量逐一相同。
    // 配置文件缺失或对应键缺失时回退到它们，行为与改造前完全一致
    static constexpr float kDefaultRoundSeconds = 30.0f; // 默认每局总时长（秒）
    static constexpr int kDefaultCoinCount = 5;          // 默认场上金币数量

    // 丁阶段（敌人/难度曲线）配置项缺省值：与 config/game.ini 的对应键取值一致，
    // 配置文件缺失时敌人按「基础 110 像素/秒、每秒 +2.5、5 秒首袭、每 10 秒
    // 增援、最多 3 只、撞中扣 5 秒」的完整难度曲线运行
    static constexpr float kDefaultEnemySpeed = 110.0f;          // 敌人基础移动速度（像素/秒）
    static constexpr float kDefaultEnemySpeedGrowth = 2.5f;      // 敌人速度成长（像素/秒²）
    static constexpr float kDefaultEnemyFirstSpawn = 5.0f;       // 第 1 只敌人的出现时间（局内秒）
    static constexpr float kDefaultEnemySpawnInterval = 10.0f;   // 增援间隔（秒）
    static constexpr int kDefaultEnemyMaxCount = 3;              // 场上敌人数量上限
    static constexpr float kDefaultEnemyHitPenalty = 5.0f;       // 被敌人撞中的时间惩罚（秒）

    // 敌人速度上限（像素/秒）：不超过玩家默认速度 300，保证玩家永远可以甩开
    // 追兵（「可逃」是难度曲线的设计红线——速度成长不能把玩家逼进死局）。
    // 默认参数下约 60 秒到达上限，此后维持 260 不再增长
    static constexpr float kEnemyMaxSpeed = 260.0f;

    // 玩家被撞中后的无敌时长（秒）：期间跳过敌人碰撞检测、渲染每 0.1 秒闪烁。
    // 固定玩法规则（不进配置文件）：0.8 秒足够拉开与击退后敌人的距离
    static constexpr float kPlayerInvincibleSeconds = 0.8f;

    // 无敌闪烁的「半周期」（秒）：显 / 隐各占 0.1 秒，即每 0.1 秒切换一次
    static constexpr float kPlayerBlinkHalfPeriod = 0.1f;

    static constexpr float kHudPtSize = 32.0f; // HUD/提示文字字号（像素高，小数需求清晰）
    static constexpr float kBigPtSize = 64.0f; // 暂停/结束标题字号（大号观感）

    // FPS 调试行专用：仍走 SDL 内置调试字体（8x8 点阵，仅 ASCII），
    // 缩放 2 倍 => 16 屏幕像素高，与改造前的调试行观感完全一致
    static constexpr float kFpsScale = 2.0f;

    SDL_Renderer* m_renderer = nullptr; // onEnter 保存的渲染器（只借用，不持有所有权）

    // onEnter 注入的音效系统指针（所有权在 GameDev，受管理期间必然非空）。
    // 对局用它在四处触发：收币（update）、被撞中（update）、结束（update）、
    // 暂停/恢复（handleEvent）
    AudioSystem* m_audio = nullptr;

    // 由 renderer 反查的窗口指针：玩法边界/金币重生需要窗口整数尺寸
    // （与渲染基元无关的「逻辑尺寸」沿用窗口坐标系，保持改造前的数值）
    SDL_Window* m_window = nullptr;

    // 背景纹理：加载失败时为 nullptr，render 阶段会跳过，
    // 露出清屏深蓝色兜底（背景不是游戏必需品，加载失败不阻塞启动）
    SDL_Texture* m_background = nullptr;

    // 字体资源：一个实例对应一个字号，加载失败时绘制安全跳过（Font 内部已警告）
    Font m_hudFont; // HUD/提示文字（分数、时间、操作提示、GameOver 列表）
    Font m_bigFont; // 大号标题（暂停 / 游戏结束）

    Player m_player;              // 玩家实体
    std::vector<Coin> m_coins;    // 场上同时存在的金币集合
    std::vector<Enemy> m_enemies; // 敌人对象池（预创建；前 m_enemySpawned 个为已激活）
    int m_score = 0;              // 已收集的金币总数（跨帧累计）

    // ---- 丁阶段：敌人与难度曲线（配置 + 运行时状态） ----
    // 下列配置在 onEnter 时读取（缺键自动回退内置默认值）；R 重开不重读配置，
    // 只复位下面的运行时状态——保证整局沿用同一套调参
    float m_enemySpeed = kDefaultEnemySpeed;                   // 敌人基础速度（像素/秒）
    float m_enemySpeedGrowth = kDefaultEnemySpeedGrowth;       // 速度成长（像素/秒²，按局内秒数叠加）
    float m_enemyFirstSpawn = kDefaultEnemyFirstSpawn;         // 第 1 只敌人的出现时间（局内秒）
    float m_enemySpawnInterval = kDefaultEnemySpawnInterval;   // 增援间隔（秒）
    int m_enemyMaxCount = kDefaultEnemyMaxCount;               // 敌人数量上限（同时决定对象池大小）
    float m_enemyHitPenalty = kDefaultEnemyHitPenalty;         // 被撞中的时间惩罚（秒）

    float m_enemyElapsed = 0.0f;       // 局内已过秒数（仅 Playing 递增；难度计时基准）
    float m_enemyNextSpawnTime = 0.0f; // 下一只敌人的出现时刻（局内秒；onEnter 设为首只时间）
    int m_enemySpawned = 0;            // 本局已生成的敌人数（同时决定激活对象池的前几个）

    // 玩家剩余无敌时间（秒；0 = 未处于无敌）；被撞中时重置为满值，
    // 仅 Playing 状态递减——暂停/结束时冻结，闪烁相位随之冻结
    float m_playerInvincibleLeft = 0.0f;

    // 本局总时长（秒）：onEnter 时由配置 round_seconds 覆盖默认值。
    // 按 R 重开时用它重置倒计时，保证整局时长一致
    float m_roundSeconds = kDefaultRoundSeconds;

    float m_timeLeft = kDefaultRoundSeconds; // 本局剩余秒数（仅 Playing 状态递减）

    PlayState m_state = PlayState::Playing; // 当前对局状态

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
    // 金币重生范围这类需要整数的场合直接用它
    int m_width = 0;
    int m_height = 0;
};

#endif // !__PLAYSCENE_H__
