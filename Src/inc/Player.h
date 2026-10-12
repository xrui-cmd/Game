#ifndef __PLAYER_H__
#define __PLAYER_H__

#include <SDL3/SDL.h>
#include <iostream>
#include <windows.h>

class Player
{
public:
    Player() = default;
    ~Player();                                    // 析构，销毁纹理

    // 创建纹理：需要 renderer 和窗口尺寸
    bool load(SDL_Renderer* renderer);

    void update(float deltaTime, const bool* keyboard);
    void render(SDL_Renderer* renderer) const;

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
    void release();                               // 释放纹理

private:
    float m_x = 640.0f;
    float m_y = 360.0f;
    float m_speed = 300.0f;

    SDL_Texture* m_texture = nullptr;
    float m_width = 50.0f;
    float m_height = 50.0f;
};


#endif // !__PLAYER_H__
