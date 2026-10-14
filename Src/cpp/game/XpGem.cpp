#include "game/XpGem.h"

// 加载宝石纹理：三级回退流程由基类统一实现，这里只提供本实体的参数
bool XpGem::load(SDL_Renderer* renderer)
{
    // loadTextureWithFallback 依次尝试：
    //   1. assets/gem_sheet.png 旋转精灵表（6 帧，持续播动画）
    //   2. assets/gem.png 单帧精灵（动画退化为静止）
    //   3. 程序化 24x24 青绿色方块（素材缺失时兜底）
    return loadTextureWithFallback(renderer,
                                   "assets/gem_sheet.png",        // 首选：旋转精灵表相对路径
                                   "assets/gem.png",              // 次选：单帧精灵相对路径
                                   kFrameCount,                   // 精灵表帧数：6 帧
                                   kDefaultFrameTime,             // 每帧时长：默认 0.12 秒
                                   SDL_Color{64, 224, 208, 255}); // 回退颜色：不透明青绿色（绿松石色系）
}
