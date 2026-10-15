#include "game/Coin.h"

// 加载金币纹理：三级回退流程由基类统一实现，这里只提供本实体的参数
bool Coin::load(SDL_Renderer* renderer)
{
    // loadTextureWithFallback 依次尝试：
    //   1. assets/coin_sheet.png 旋转精灵表（4 帧，持续播动画，素材包原色）
    //   2. assets/coin.png 单帧精灵（动画退化为静止）
    //   3. 程序化 18x21 金黄色方块（素材缺失时兜底）
    return loadTextureWithFallback(renderer,
                                   "assets/coin_sheet.png",       // 首选：旋转精灵表相对路径
                                   "assets/coin.png",             // 次选：单帧精灵相对路径
                                   kFrameCount,                   // 精灵表帧数：4 帧
                                   kDefaultFrameTime,             // 每帧时长：默认 0.18 秒
                                   SDL_Color{255, 200, 60, 255}); // 回退颜色：不透明金黄色（与金币素材同色系）
}
