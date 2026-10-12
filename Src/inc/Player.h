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
