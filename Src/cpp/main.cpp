#include "main.h"

int main()
{
    // GameDev 负责整个应用的 SDL 生命周期。
    GameDev game;

    GameError result = game.initialize();
    if (result != GameError::None)
    {
        return static_cast<int>(result);
    }

    result = game.run();

    // main 结束时自动调用 GameDev 析构函数并释放资源。
    return static_cast<int>(result);
}