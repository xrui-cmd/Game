#ifndef __PLAYER_H__
#define __PLAYER_H__

#include <SDL3/SDL.h>

// ============================================================
// Player：玩家实体（WASD 移动 + 行走帧动画）
// ============================================================
//
// 纹理来源分三级（与 Coin 相同的策略）：
//   1. 首选：assets/player_sheet.png 2 帧行走精灵表（100x50），移动时播放
//   2. 次选：assets/player.png 单帧静态精灵（旧版素材，动画退化为静止）
//   3. 回退：程序化 50x50 绿色方块（素材缺失时兜底，保证游戏可运行）
//
// 动画规则：
//   移动时帧计时器累加 deltaTime，每 0.15 秒切换下一帧（2 帧循环 = 迈步起伏）；
//   静止时计时器清零，画面固定回第 1 帧（站姿）。
// ============================================================

class Player
{
public:
    Player() = default;
    ~Player();                                    // 析构，销毁纹理

    // 加载玩家纹理：优先 assets/player_sheet.png 行走精灵表，
    // 失败再尝试 assets/player.png 单帧精灵（动画退化为静止），
    // 全部失败时回退到程序化 50x50 绿色方块（素材缺失也不至于崩）
    bool load(SDL_Renderer* renderer);

    // 更新玩家位置与行走动画：deltaTime 为帧间隔秒数，
    // keyboard 为 SDL 键盘状态数组，winW/winH 是窗口实际宽高
    // （由 GameDev 查询后传入，边界不再写死）
    void update(float deltaTime, const bool* keyboard, float winW, float winH);
    void render(SDL_Renderer* renderer) const;

    // 释放纹理：公开给 GameDev::shutdown 提前调用，
    // 保证纹理先于 renderer 销毁；函数幂等，析构时会再兜底调用一次
    void release();

    float width() const { return m_width; }       // 供外部查询方块宽高
    float height() const { return m_height; }

    // 返回玩家方块当前的轴对齐矩形，用于和其他实体做 AABB 碰撞检测
    SDL_FRect rect() const
    {
        // 用当前位置和固定的宽高拼出一个 SDL_FRect 返回（副本，外部修改不影响玩家）
        return SDL_FRect{ m_x, m_y, m_width, m_height };
    }

    // 把玩家左上角瞬间移动到 (x, y)：重开游戏时用来归位到窗口中心
    void reset(float x, float y)
    {
        m_x = x; // 直接覆盖 x 坐标，不经过速度/时间累积
        m_y = y; // 直接覆盖 y 坐标，保证重开后位置确定
    }

private:
    float m_x = 640.0f;
    float m_y = 360.0f;
    float m_speed = 300.0f;

    SDL_Texture* m_texture = nullptr;
    float m_width = 50.0f;
    float m_height = 50.0f;

    // 行走动画帧计时器（秒）：仅移动时累加，静止时清零。
    // render 用它换算当前帧号，实现与渲染帧率无关的动画速度
    float m_animTime = 0.0f;

    // 当前纹理实际包含的帧数：精灵表为 kFrameCount(2)；
    // 单帧图片或程序化回退纹理为 1（动画退化为静止画面）
    int m_frameCount = 1;

    static constexpr int kFrameCount = 2;      // 行走精灵表总帧数（2 帧 x 50px = 100px 宽）
    static constexpr float kFrameTime = 0.15f; // 每帧显示时长（秒）
};


#endif // !__PLAYER_H__
