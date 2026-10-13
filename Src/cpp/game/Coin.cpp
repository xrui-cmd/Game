#include "game/Coin.h"
#include "engine/Rect.h" // rectsOverlap：重生时「尽量避开玩家」的 AABB 判定
#include <algorithm>     // std::max：防止窗口异常小时随机上界小于下界

// 加载金币纹理：三级回退流程由基类统一实现，这里只提供本实体的参数
bool Coin::load(SDL_Renderer* renderer)
{
    // loadTextureWithFallback 依次尝试：
    //   1. assets/coin_sheet.png 旋转精灵表（6 帧，持续播动画）
    //   2. assets/coin.png 单帧精灵（动画退化为静止）
    //   3. 程序化 24x24 黄色方块（素材缺失时兜底）
    return loadTextureWithFallback(renderer,
                                   "assets/coin_sheet.png",      // 首选：旋转精灵表相对路径
                                   "assets/coin.png",            // 次选：单帧精灵相对路径
                                   kFrameCount,                  // 精灵表帧数：6 帧
                                   kDefaultFrameTime,            // 每帧时长：默认 0.12 秒
                                   SDL_Color{255, 255, 0, 255}); // 回退颜色：不透明黄色
}

// 在窗口内随机挑一个出生点；「尽量」避开 avoidRect（通常传玩家矩形）
void Coin::respawn(int winW, int winH, const SDL_FRect& avoidRect)
{
    // 随机坐标的上界：窗口尺寸减去金币自身大小和边距。
    // 用 std::max 兜底，防止窗口异常小时出现「上界小于下界」、
    // 导致随机出的坐标跑到窗口外。
    const float maxX = std::max(static_cast<float>(winW) - kSize - kSpawnMargin, kSpawnMargin);
    const float maxY = std::max(static_cast<float>(winH) - kSize - kSpawnMargin, kSpawnMargin);

    // 最多尝试 kMaxTries 次；只要某次位置与玩家不相交就立即采用
    for (int tryCount = 0; tryCount < kMaxTries; ++tryCount)
    {
        // SDL_randf() 返回 [0,1) 的随机浮点数，映射到 [kSpawnMargin, maxX] 区间
        m_x = kSpawnMargin + SDL_randf() * (maxX - kSpawnMargin);
        // Y 方向同理，独立取一个随机数，避免金币总出现在对角线上
        m_y = kSpawnMargin + SDL_randf() * (maxY - kSpawnMargin);

        // 当前位置与玩家矩形不重叠，则采用该位置并结束重试。
        // rect() 继承自 Entity：碰撞盒恒为 24x24，动画不改变碰撞语义
        if (!rectsOverlap(rect(), avoidRect))
        {
            break; // 找到合格位置，不再继续尝试
        }
    }
    // 若 kMaxTries 次都恰好落在玩家身上（概率极低），也接受最后一次结果：
    // 设计目标是「尽量避开」而非「绝对避开」，避免极端情况下的死循环
}
