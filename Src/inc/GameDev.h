// GameDev.h: 标准系统包含文件的包含文件
// 或项目特定的包含文件。

#ifndef __GAMEDEV_H__
#define __GAMEDEV_H__


#include <SDL3/SDL.h>
#include <iostream>
#include <windows.h>
#include <vector>
#include "ErrorCode.h"
#include "Player.h"
#include "Coin.h"


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
    void shutdown();

private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    bool m_running = false;
    float m_deltaTime = 0.0f;
    Uint64 m_lastTime = 0;
    Player m_player;

    // 场上同时存在的金币集合；数量固定为 kCoinCount 个
    std::vector<Coin> m_coins;
    static constexpr int kCoinCount = 5; // 金币数量（学习项目先用固定小数量）
    int m_score = 0;                     // 已收集的金币总数（跨帧累计）
};

#endif // !__GAMEDEV_H__





