#ifndef __COIN_H__
#define __COIN_H__

#include <SDL3/SDL.h>

// ============================================================
// Coin：可被玩家收集的金币实体（带旋转帧动画）
// ============================================================
//
// 纹理来源分三级：
//   1. 首选：exe 同目录 assets/coin_sheet.png 旋转精灵表（6 帧横向排列），播动画
//   2. 次选：assets/coin.png 单帧静态精灵（旧版素材，动画退化为静止画面）
//   3. 回退：程序化生成纯黄色 Surface（素材缺失时兜底，保证游戏可运行）
//
// 动画原理（固定帧时长与渲染帧率解耦）：
//   帧计时器 m_animTime 每帧累加 deltaTime；每攒满一个固定帧时长 kFrameTime
//   秒，画面就切换到精灵表的下一帧。动画进度只由真实流逝时间决定，
//   因此即使渲染帧率变化（60Hz/144Hz/偶发卡顿），金币旋转速度也保持不变。
//
// 生命周期：
//   load() 创建纹理 ——> update() 推进动画 ——> render()/rect() 使用
//   ——> release() 释放纹理（幂等），析构函数里会再兜底调用一次。
// ============================================================

class Coin
{
public:
    Coin() = default; // 默认构造：位置在 (0,0)，纹理为空，等待 load 初始化
    ~Coin();          // 析构时释放纹理，避免显存泄漏

    // 加载金币纹理：优先 assets/coin_sheet.png 旋转精灵表，
    // 失败再尝试 assets/coin.png 单帧精灵（动画退化为静止），
    // 全部失败时回退到程序化 24x24 黄色方块（素材缺失也不至于崩）
    bool load(SDL_Renderer* renderer);

    // 推进旋转动画：把本帧真实流逝的秒数累加进帧计时器；
    // 帧号换算发生在 render 里，本函数只负责守护时间轴
    void update(float deltaTime);

    // 把金币精灵表的当前帧绘制到当前位置；不修改自身数据，因此是 const 方法
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

    // 动画帧计时器（秒）：持续累加 deltaTime，render 用它换算当前帧号。
    // 存"时间"而不是"帧序号"，是动画与渲染帧率解耦的关键
    float m_animTime = 0.0f;

    // 当前纹理实际包含的帧数：精灵表为 kFrameCount(6)；
    // 单帧图片或程序化回退纹理为 1（动画退化为静止画面）
    int m_frameCount = 1;

    static constexpr float kSize = 24.0f;        // 金币边长（正方形），也是单帧宽高
    static constexpr float kSpawnMargin = 40.0f; // 翻新位置与窗口边缘保留的边距
    static constexpr int kMaxTries = 20;         // 重生时"避开玩家"的最大重试次数

    static constexpr int kFrameCount = 6;      // 旋转精灵表总帧数（6 帧 x 24px = 144px 宽）
    static constexpr float kFrameTime = 0.12f; // 每帧显示时长（秒），约 8.3 帧/秒
};

#endif // !__COIN_H__
