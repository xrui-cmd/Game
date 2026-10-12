// GameDev.h: 标准系统包含文件的包含文件
// 或项目特定的包含文件。

#ifndef __GAMEDEV_H__
#define __GAMEDEV_H__


#include <SDL3/SDL.h>
#include <iostream>
#include <windows.h>
#include "ErrorCode.h"
#include "Player.h"


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
};

#endif // !__GAMEDEV_H__





