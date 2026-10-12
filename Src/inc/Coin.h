#ifndef __COIN_H__
#define __COIN_H__

#include <SDL3/SDL.h>

// ============================================================
// Coin：可被玩家收集的金币实体
// ============================================================
//
// 与 Player 一样采用"程序化生成纹理"的做法：
// 在内存里创建一张纯黄色 Surface，再转成 GPU 纹理，
// 不依赖任何图片资源文件，学习项目开箱即可运行。
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

    // 程序化创建金币纹理（黄色纯色方块），需要外部传入创建好的 renderer
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
