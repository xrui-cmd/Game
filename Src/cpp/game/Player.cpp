#include "game/Player.h"
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

// 加载玩家纹理，分三级策略：
//   1. 首选——assets/player_sheet.png 行走精灵表（2 帧，SDL3_image 解码）
//   2. 次选——assets/player.png 单帧静态精灵（旧素材仍可用，动画退化为静止）
//   3. 回退——程序化生成 50x50 绿色方块（素材缺失也不至于崩，游戏照常可玩）
bool Player::load(SDL_Renderer* renderer)
{
    // 先按"单帧"假设：若最后加载到精灵表，再把帧数改写为 kFrameCount
    m_frameCount = 1;

    // SDL_GetBasePath() 返回可执行文件所在目录的绝对路径（结尾自带路径分隔符）。
    // 用它拼素材路径，无论从哪个工作目录启动游戏，都能找到 exe 旁边的 assets/。
    // 注意版本差异：SDL2 中该函数返回需要 SDL_free 释放的 char*；SDL3 改为返回
    // const char*，字符串由 SDL 内部缓存持有，调用方既不能释放也不能修改其内容
    const char* basePath = SDL_GetBasePath();

    // 只有成功拿到基路径才尝试读文件；拿不到就跳过图片加载直接走回退
    if (basePath != nullptr)
    {
        // 第一优先：拼出精灵表完整路径 <exe目录>/assets/player_sheet.png
        // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
        std::string sheetPath = std::string(basePath) + "assets/player_sheet.png";

        // IMG_LoadTexture 是 SDL3_image 的 API：读文件 -> 解码 -> 一步创建 GPU 纹理
        m_texture = IMG_LoadTexture(renderer, sheetPath.c_str());

        // 精灵表加载成功：把帧数设为 2，行走动画数据完整，首选路径达成
        if (m_texture != nullptr)
        {
            m_frameCount = kFrameCount;
            return true;
        }

        // 精灵表没加载成功（文件缺失/解码失败）：警告后继续尝试旧版单帧素材
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "加载玩家精灵表失败（%s），尝试单帧素材", SDL_GetError());

        // 第二优先：拼出旧版单帧精灵路径 <exe目录>/assets/player.png
        std::string spritePath = std::string(basePath) + "assets/player.png";

        // 单帧素材加载成功：帧数保持 1，动画自然退化为静止画面后返回
        m_texture = IMG_LoadTexture(renderer, spritePath.c_str());
        if (m_texture != nullptr)
        {
            return true;
        }
    }

    // 走到这里说明所有图片都没加载成功（含拿不到基路径的情形）：
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

    // 纹理指针有效即本函数成功；回退纹理只有 1 帧（m_frameCount 保持 1），
    // 动画会自动退化为单帧静止绘制，无需在此额外标记
    return m_texture != nullptr;
}

void Player::update(float deltaTime, const bool* keyboard, float winW, float winH)
{
    // 本帧是否有移动输入：任一方向键按下即视为"要走动"，用于驱动行走动画
    bool isMoving = false;

    // WASD 控制方向，注意 SDL 中 y 轴向下为正
    if (keyboard[SDL_SCANCODE_W])
    {
        m_y -= m_speed * deltaTime;   // 上
        isMoving = true;              // 记录本帧有移动输入（动画需要）
    }
    if (keyboard[SDL_SCANCODE_S])
    {
        m_y += m_speed * deltaTime;   // 下
        isMoving = true;              // 记录本帧有移动输入（动画需要）
    }
    if (keyboard[SDL_SCANCODE_A])
    {
        m_x -= m_speed * deltaTime;   // 左
        isMoving = true;              // 记录本帧有移动输入（动画需要）
    }
    if (keyboard[SDL_SCANCODE_D])
    {
        m_x += m_speed * deltaTime;   // 右
        isMoving = true;              // 记录本帧有移动输入（动画需要）
    }

    // 边界限制基于调用方传入的窗口实际尺寸（不再硬编码 1280/720）：
    // 右边界 = 窗口宽 - 自身宽，下边界 = 窗口高 - 自身高
    m_x = std::clamp(m_x, 0.0f, winW - m_width);
    m_y = std::clamp(m_y, 0.0f, winH - m_height);

    // 行走动画推进（固定帧时长与渲染帧率解耦，原理同 Coin）：
    // 移动时累加计时器，每攒满 kFrameTime 秒 render 就切到下一帧；
    // 静止时清零，让画面立即固定回第 1 帧（站姿），不会停在半途的迈步姿势
    if (isMoving)
    {
        m_animTime += deltaTime; // 累积行走时间，供 render 换算帧号
    }
    else
    {
        m_animTime = 0.0f; // 清零计时器 -> 帧号换算结果恒为 0（第 1 帧站姿）
    }
}

void Player::render(SDL_Renderer* renderer) const
{
    if (m_texture == nullptr)
    {
        return;      // 纹理还没创建成功，直接跳过，避免空指针崩溃
    }

    // 目标矩形（浮点），控制纹理绘制到屏幕上的位置和大小
    const SDL_FRect dst{ m_x, m_y, m_width, m_height };

    // 帧号换算：累计行走时间 / 每帧时长，取整后对总帧数取模实现 2 帧循环。
    // 静止站立时 m_animTime 为 0 -> 帧号 0（第 1 帧站姿）；
    // 回退纹理（m_frameCount == 1）时帧号恒为 0，动画退化为单帧绘制
    const int frameIndex = static_cast<int>(m_animTime / kFrameTime) % m_frameCount;

    // srcRect 裁剪精灵表当前帧：第 f 帧横向起点 = f * 帧宽，纵向恒为第一行
    const SDL_FRect src{ static_cast<float>(frameIndex) * m_width, 0.0f, m_width, m_height };

    // SDL_RenderTexture 用 src 从整张精灵表裁出当前帧，再画到 dst 位置
    SDL_RenderTexture(renderer, m_texture, &src, &dst);
}