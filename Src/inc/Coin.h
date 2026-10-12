#ifndef __COIN_H__
#define __COIN_H__

#include <SDL3/SDL.h>

// ============================================================
// Coin：可被玩家收集的金币实体
// ============================================================
//
// 纹理来源分两级（与 Player 相同）：
//   1. 首选：exe 同目录 assets/coin.png 图片精灵，由 SDL3_image 解码加载
//   2. 回退：程序化生成纯黄色 Surface（素材缺失时兜底，保证游戏可运行）
//
// 生命周期：
//   load() 创建纹理 ——> render()/rect() 使用 ——> release() 释放纹理
//   release() 是幂等的，析构函数里会再兜底调用一次。
// ============================================================

class Coin
{
public:
    Coin() = default; // 默认构造：位置在 (0,0)，纹理为空，等待 load 初始化
    ~Coin();          // 析构时释放纹理，避免显存泄漏

    // 加载金币纹理：优先 exe 同目录 assets/coin.png 图片精灵，
    // 加载失败时回退到程序化 24x24 黄色方块（素材缺失也不至于崩）
    bool load(SDL_Renderer* renderer);

    // 把金币纹理绘制到当前位置；不修改自身数据，因此是 const 方法
    void render(SDL_Renderer* renderer) const;

    // 释放纹理资源；幂等（重复调用安全），供 shutdown 阶段提前调用
    void release();

    // 返回金币当前占用的矩形区域，供 AABB 碰撞检测使用
    SDL_FRect rect() const;

    // 在 (winW, winH) 窗口范围内随机重生，尽量避开 avoidRect（通常传玩家矩形）
    void respawn(int winW, int winH, const SDL_FRect& avoidRect);

private:
    SDL_Texture* m_texture = nullptr; // 金币纹理；load 成功后非空

    float m_x = 0.0f; // 金币左上角 x 坐标（像素）
    float m_y = 0.0f; // 金币左上角 y 坐标（像素）

    static constexpr float kSize = 24.0f;        // 金币边长（正方形）
    static constexpr float kSpawnMargin = 40.0f; // 翻新位置与窗口边缘保留的边距
    static constexpr int kMaxTries = 20;         // 重生时"避开玩家"的最大重试次数
};

#endif // !__COIN_H__
