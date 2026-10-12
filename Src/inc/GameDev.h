// GameDev.h: 标准系统包含文件的包含文件
// 或项目特定的包含文件。

#ifndef __GAMEDEV_H__
#define __GAMEDEV_H__


#include <SDL3/SDL.h>
#include <vector>
#include "ErrorCode.h"
#include "Player.h"
#include "Coin.h"


// 游戏状态机：把"进行中"和"本局结束"两个阶段显式区分开。
// 用枚举而不是一个 bool 标志，之后要扩展"暂停/开始菜单"等状态会更方便。
enum class GameState
{
    Playing,  // 正在游戏：接受输入、倒计时递减、检测金币碰撞
    GameOver  // 本局结束：世界冻结，等待玩家按 R 重新开始
};


class GameDev
{
public:
    GameDev() = default;
    ~GameDev();

    GameError initialize();
    GameError run();

private:
    void processEvents();
    void update();
    GameError render();
    void renderHud(); // 绘制 HUD 文字（分数、倒计时、结束界面）
    void restart();   // 重开一局：重置分数、倒计时、玩家位置与全部金币
    void shutdown();

private:
    static constexpr float kRoundSeconds = 30.0f; // 每局总时长（秒）
    static constexpr float kHudScale = 2.0f;      // HUD 文字放大倍数（8x8 字模太小）

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    bool m_running = false;
    float m_deltaTime = 0.0f;
    Uint64 m_lastTime = 0;
    Player m_player;

    // 窗口实际尺寸（由 SDL_GetWindowSize 查询得到）：
    // 玩家移动边界、金币重生范围、重开时归位都基于它计算，不再硬编码 1280x720
    int m_width = 0;
    int m_height = 0;

    // 场上同时存在的金币集合；数量固定为 kCoinCount 个
    std::vector<Coin> m_coins;
    static constexpr int kCoinCount = 5; // 金币数量（学习项目先用固定小数量）
    int m_score = 0;                     // 已收集的金币总数（跨帧累计）

    GameState m_state = GameState::Playing; // 当前游戏状态
    float m_timeLeft = kRoundSeconds;       // 本局剩余秒数（仅 Playing 状态递减）
};

#endif // !__GAMEDEV_H__





