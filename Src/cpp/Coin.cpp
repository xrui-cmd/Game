#include "../inc/Coin.h"
#include "../inc/Rect.h"
#include <algorithm>

// 析构：统一在这里释放纹理。
// release() 是幂等的，即使 GameDev::shutdown() 已经提前调用过也安全。
Coin::~Coin()
{
    release(); // 保证对象无论以何种路径销毁，纹理都不会泄漏
}

// 释放纹理资源：先判空、再销毁、最后置空，保证可重复调用
void Coin::release()
{
    // 只有纹理确实存在时才销毁，避免对空指针调用 SDL_DestroyTexture
    if (m_texture != nullptr)
    {
        SDL_DestroyTexture(m_texture); // 通知 SDL 释放这张纹理占用的显存
        m_texture = nullptr;           // 置空后再次调用会直接跳过，实现幂等
    }
}

// 程序化创建金币纹理：流程与 Player::load 相同，只是尺寸和颜色不同
bool Coin::load(SDL_Renderer* renderer)
{
    // 1. 创建一张 24x24 的内存表面（Surface 是 CPU 侧的像素数据）
    SDL_Surface* surface = SDL_CreateSurface(
        static_cast<int>(kSize),   // 表面宽度取整为 24 像素
        static_cast<int>(kSize),   // 表面高度取整为 24 像素
        SDL_PIXELFORMAT_RGBA8888); // 每像素 32 位 RGBA，通用直观

    // 表面创建失败通常意味着内存不足，直接把失败交给调用方处理
    if (surface == nullptr)
    {
        return false; // 纹理尚未创建，调用方负责报错
    }

    // 2. 把整张表面填充成黄色（R=255, G=255, B=0, A=255）
    //    MapSurfaceRGBA 会按表面像素格式生成颜色值，比手算位运算更稳
    const Uint32 color = SDL_MapSurfaceRGBA(surface, 255, 255, 0, 255);
    // dstrect 传 nullptr 表示填充整个表面区域
    SDL_FillSurfaceRect(surface, nullptr, color);

    // 3. 从表面创建 GPU 纹理；renderer 会复制一份像素数据到纹理里
    m_texture = SDL_CreateTextureFromSurface(renderer, surface);

    // 4. 像素数据已复制进纹理，表面完成使命，立刻销毁避免内存泄漏
    SDL_DestroySurface(surface);

    // 纹理指针有效即本函数成功
    return m_texture != nullptr;
}

// 返回金币当前矩形：AABB 判定和"重生时避开自己"都依赖它
SDL_FRect Coin::rect() const
{
    // SDL_FRect 用 x,y（左上角）+ w,h（宽高）描述轴对齐矩形
    return SDL_FRect{ m_x, m_y, kSize, kSize };
}

// 在窗口内随机挑一个出生点；"尽量"避开 avoidRect（通常传玩家矩形）
void Coin::respawn(int winW, int winH, const SDL_FRect& avoidRect)
{
    // 随机坐标的上界：窗口尺寸减去金币自身大小和边距。
    // 用 std::max 兜底，防止窗口异常小时出现"上界小于下界"、
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

        // 当前位置与玩家矩形不重叠，则采用该位置并结束重试
        if (!rectsOverlap(rect(), avoidRect))
        {
            break; // 找到合格位置，不再继续尝试
        }
    }
    // 若 kMaxTries 次都恰好落在玩家身上（概率极低），也接受最后一次结果：
    // 设计目标是"尽量避开"而非"绝对避开"，避免极端情况下的死循环
}

// 把金币纹理绘制到 m_x, m_y 处
void Coin::render(SDL_Renderer* renderer) const
{
    // 纹理还没有加载成功时直接返回，避免空指针导致崩溃
    if (m_texture == nullptr)
    {
        return; // 未加载就没有可绘制的内容
    }

    // 目标矩形决定纹理画在屏幕上的位置和尺寸
    const SDL_FRect dst{ m_x, m_y, kSize, kSize };

    // srcRect 传 nullptr 表示绘制整张纹理
    SDL_RenderTexture(renderer, m_texture, nullptr, &dst);
}
