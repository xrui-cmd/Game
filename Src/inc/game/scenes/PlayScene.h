#ifndef __PLAYSCENE_H__
#define __PLAYSCENE_H__

#include "engine/Scene.h" // Scene：场景基类
#include "game/Player.h"  // Player：玩家实体
#include "game/Coin.h"    // Coin：金币实体
#include <vector>         // std::vector：金币集合

// ============================================================
// PlayScene：对局场景（原 GameDev 的全部玩法逻辑迁入）
// ============================================================
//
// 职责：
//   1. 资源：在 onEnter 加载背景图（可选，失败兜底）、玩家与金币纹理；
//      析构时释放背景纹理（玩家/金币纹理由各自 Entity 析构兜底）
//   2. 玩法：WASD 移动、金币碰撞计分、倒计时、ESC 暂停/恢复、
//      R 重开、M 回主菜单
//   3. 配置：onEnter 时从注入的 Config 读取对局时长、金币数量、
//      玩家速度与动画节奏；缺键/坏值自动回退内置默认值
//   4. 渲染：背景 -> 玩家 -> 金币 -> HUD 文字叠层
//
// 「每次进入 = 全新一局」：每局都是新构造的 PlayScene 对象，
// 数据成员的默认值就是初始状态（分数 0、倒计时满值）；onEnter 里
// 把玩家与金币布置到位即可，不需要额外的「重置场景」逻辑
//
// 状态机（PlayState）：
//   Playing  ：接受输入、倒计时递减、检测金币碰撞
//   Paused   ：世界与倒计时冻结，ESC 恢复、M 回主菜单
//   GameOver ：世界冻结，等待玩家按 R 重开或按 M 回主菜单
// ============================================================

// 对局状态机：用枚举而不是布尔标志，扩展「暂停」等状态后依然直观
enum class PlayState
{
    Playing,  // 正在游戏：接受输入、倒计时递减、检测金币碰撞
    Paused,   // 暂停：世界与倒计时冻结，ESC 恢复、M 回主菜单
    GameOver  // 本局结束：世界冻结，等待玩家按 R 重开或按 M 回主菜单
};

class PlayScene : public Scene
{
public:
    PlayScene() = default; // 成员默认值即初始局面，无需自定义构造
    ~PlayScene() override;  // 析构释放背景纹理（此时 renderer 尚在，安全）

    // 进入场景：保存 renderer、读取配置、加载全部纹理、布置玩家与金币
    void onEnter(SDL_Renderer* renderer, const Config& config) override;

    // 事件：ESC 暂停/恢复、GameOver 下按 R 重开、Paused/GameOver 下按 M 回主菜单
    void handleEvent(const SDL_Event& event) override;

    // 每帧更新：仅 Playing 状态推进移动/碰撞/倒计时（Paused/GameOver 冻结）。
    // 冻结时会把各实体的插值快照对齐到当前位置，保证画面完全静止不漂移
    void update(const UpdateContext& ctx) override;

    // 每帧渲染：清屏、背景、实体、HUD。alpha 是插值系数，原样转发给实体
    void render(float alpha) override;

private:
    // 重开一局：重置分数、倒计时、玩家位置与全部金币
    void restart();

    // 绘制 HUD 文字（分数、倒计时、暂停叠层、结束界面）
    void renderHud();

private:
    // 配置项缺省值：与改造前的硬编码常量逐一相同。
    // 配置文件缺失或对应键缺失时回退到它们，行为与改造前完全一致
    static constexpr float kDefaultRoundSeconds = 30.0f; // 默认每局总时长（秒）
    static constexpr int kDefaultCoinCount = 5;          // 默认场上金币数量

    static constexpr float kHudScale = 2.0f;        // HUD 文字放大倍数（8x8 字模太小）
    static constexpr float kPauseTitleScale = 4.0f; // 暂停标题 (PAUSED) 放大倍数

    SDL_Renderer* m_renderer = nullptr; // onEnter 保存的渲染器（只借用，不持有所有权）

    // 由 renderer 反查的窗口指针：玩法边界/金币重生需要窗口整数尺寸
    // （与渲染基元无关的「逻辑尺寸」沿用窗口坐标系，保持改造前的数值）
    SDL_Window* m_window = nullptr;

    // 背景纹理：加载失败时为 nullptr，render 阶段会跳过，
    // 露出清屏深蓝色兜底（背景不是游戏必需品，加载失败不阻塞启动）
    SDL_Texture* m_background = nullptr;

    Player m_player;           // 玩家实体
    std::vector<Coin> m_coins; // 场上同时存在的金币集合
    int m_score = 0;           // 已收集的金币总数（跨帧累计）

    // 本局总时长（秒）：onEnter 时由配置 round_seconds 覆盖默认值。
    // 按 R 重开时用它重置倒计时，保证整局时长一致
    float m_roundSeconds = kDefaultRoundSeconds;

    float m_timeLeft = kDefaultRoundSeconds; // 本局剩余秒数（仅 Playing 状态递减）

    PlayState m_state = PlayState::Playing; // 当前对局状态

    // 窗口实际尺寸缓存（每次查询窗口后更新）：
    // 金币重生范围这类需要整数的场合直接用它
    int m_width = 0;
    int m_height = 0;
};

#endif // !__PLAYSCENE_H__
