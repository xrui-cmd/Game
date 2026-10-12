#include "../inc/Player.h"
#include <SDL3_image/SDL_image.h> // IMG_LoadTexture：SDL3_image 提供的图片文件加载 API
#include <algorithm>
#include <string> // std::string：拼接素材文件的绝对路径

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

// 加载玩家纹理，分两级策略：
//   1. 首选——从 exe 同目录的 assets/player.png 加载图片精灵（SDL3_image 解码）
//   2. 回退——程序化生成 50x50 绿色方块（素材缺失也不至于崩，游戏照常可玩）
bool Player::load(SDL_Renderer* renderer)
{
    // SDL_GetBasePath() 返回可执行文件所在目录的绝对路径（结尾自带路径分隔符）。
    // 用它拼素材路径，无论从哪个工作目录启动游戏，都能找到 exe 旁边的 assets/。
    // 注意版本差异：SDL2 中该函数返回需要 SDL_free 释放的 char*；SDL3 改为返回
    // const char*，字符串由 SDL 内部缓存持有，调用方既不能释放也不能修改其内容
    const char* basePath = SDL_GetBasePath();

    // 只有成功拿到基路径才尝试读文件；拿不到就跳过图片加载直接走回退
    if (basePath != nullptr)
    {
        // 拼出素材完整路径：<exe目录>/assets/player.png
        // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
        std::string spritePath = std::string(basePath) + "assets/player.png";

        // IMG_LoadTexture 是 SDL3_image 的 API：读文件 -> 解码 -> 一步创建 GPU 纹理
        m_texture = IMG_LoadTexture(renderer, spritePath.c_str());
    }

    // 图片加载成功：首选路径达成，直接返回成功
    if (m_texture != nullptr)
    {
        return true;
    }

    // 走到这里说明图片没加载成功（文件缺失/解码失败/拿不到基路径）：
    // 先用 SDL_LogWarn 输出具体原因，再回退到程序化纹理——素材缺失也不至于崩
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "加载玩家精灵失败（%s），回退到程序化纹理", SDL_GetError());

    // ============== 以下为回退路径：程序化生成 50x50 绿色方块 ==============

    // 1. 创建一张 50x50 的内存表面（Surface 是 CPU 侧的像素数据）
    SDL_Surface* surface = SDL_CreateSurface(
        static_cast<int>(m_width),
        static_cast<int>(m_height),
        SDL_PIXELFORMAT_RGBA8888);
    if (surface == nullptr)
    {
        return false; // 表面创建失败通常意味着内存不足，把失败交给调用方
    }

    // 2. 把表面内容填成绿色（R=0, G=255, B=0, A=255）
    //    MapSurfaceRGBA 会按表面像素格式计算颜色值，比手写位运算更稳
    Uint32 color = SDL_MapSurfaceRGBA(surface, 0, 255, 0, 255);
    SDL_FillSurfaceRect(surface, nullptr, color); // dstrect 传 nullptr 表示填满整张表面

    // 3. 从表面创建纹理：renderer 会把像素数据复制进自己的显存副本
    m_texture = SDL_CreateTextureFromSurface(renderer, surface);

    // 4. 像素已被纹理接管，表面完成使命，立即销毁避免内存泄漏
    SDL_DestroySurface(surface);

    // 纹理指针有效即本函数成功
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