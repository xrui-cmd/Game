#include "../inc/Player.h"
#include <algorithm>

Player::~Player()
{
    release();
}


// 释放纹理资源，幂等（可重复调用）
void Player::release()
{
    if (m_texture != nullptr)
    {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
}

// 创建一个 50x50 的白色纹理，作为方块的"底"
bool Player::load(SDL_Renderer* renderer)
{

  

    SDL_Surface* surface = SDL_CreateSurface(
        static_cast<int>(m_width),
        static_cast<int>(m_height),
        SDL_PIXELFORMAT_RGBA8888);
    if (surface == nullptr)
    {
        return false;
    }

    // 2. 把表面内容填成绿色
    Uint32 color = SDL_MapSurfaceRGBA(surface, 0, 255, 0, 255);
    SDL_FillSurfaceRect(surface, nullptr, color);

    // 3. 从表面创建纹理：纹理"接管"了像素数据
    m_texture = SDL_CreateTextureFromSurface(renderer, surface);

    // 4. 表面用完即可销毁，纹理已拥有自己的副本
    SDL_DestroySurface(surface);

    return m_texture != nullptr;
}

void Player::update(float deltaTime, const bool* keyboard, float winW, float winH)
{
    // WASD 控制方向，注意 SDL 中 y 轴向下为正
    if (keyboard[SDL_SCANCODE_W])
    {
        m_y -= m_speed * deltaTime;   // 上
    }
    if (keyboard[SDL_SCANCODE_S])
    {
        m_y += m_speed * deltaTime;   // 下
    }
    if (keyboard[SDL_SCANCODE_A])
    {
        m_x -= m_speed * deltaTime;   // 左
    }
    if (keyboard[SDL_SCANCODE_D])
    {
        m_x += m_speed * deltaTime;   // 右
    }

    // 边界限制基于调用方传入的窗口实际尺寸（不再硬编码 1280/720）：
    // 右边界 = 窗口宽 - 自身宽，下边界 = 窗口高 - 自身高
    m_x = std::clamp(m_x, 0.0f, winW - m_width);
    m_y = std::clamp(m_y, 0.0f, winH - m_height);
}

void Player::render(SDL_Renderer* renderer) const
{
    if (m_texture == nullptr)
    {
        return;      // 纹理还没创建成功，直接跳过，避免空指针崩溃
    }

    // 目标矩形（浮点），控制纹理绘制到屏幕上的位置和大小
    SDL_FRect dst{ m_x, m_y, m_width, m_height };

    // srcRect 传 nullptr 表示绘制整张纹理
    SDL_RenderTexture(renderer, m_texture, nullptr, &dst);
}