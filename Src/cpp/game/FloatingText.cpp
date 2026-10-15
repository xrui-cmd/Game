#include "game/FloatingText.h"
#include "engine/Font.h" // Font：TTF 绘制/测量（drawCentered）
#include <algorithm>     // std::clamp：alpha 换算的边界夹取

// ============================================================
// FloatingText 的实现：时间轴推进 + 按进度上浮/淡出渲染。
// 关键点（对应头文件的约定）：
//   - update 只维护剩余寿命，位置/透明度都在 render 时按寿命即时换算，
//     避免「推导状态」两份数据不同步；
//   - alpha 直接写进渲染器绘制颜色——Font::draw 会把它作为文字颜色
//     的一部分（TTF_RenderText_Blended 支持半透明），因此是真淡出而非
//     到点消失；
//   - 文本为空/字体未加载等异常输入都由 Font 内部安全跳过。
// ============================================================

// 生成：记录文案、出生点、颜色与寿命（复用实例时覆盖全部字段）
void FloatingText::spawn(const char* text, float centerX, float topY, SDL_Color color,
                         float lifeSeconds)
{
    // 文案判空后拷贝保存：调用方的缓冲（如栈上 snprintf 结果）随时会失效，
    // 本类必须自己持有一份——nullptr 防御性转为空串（渲染时安全跳过）
    m_text = (text != nullptr) ? text : "";

    // 出生点与颜色：直接覆盖（复用槽位时旧值全部作废）
    m_centerX = centerX; // 水平中心（绘制时居中锚点）
    m_topY = topY;       // 顶边出生位置（绘制时再按进度上浮）
    m_color = color;     // 基础颜色（alpha 分量稍后按寿命换算）

    // 寿命防御：非正值回退默认时长——否则生成即到期，飘字永远看不到
    m_lifeTotal = (lifeSeconds > 0.0f) ? lifeSeconds : kDefaultLifeSeconds;
    m_lifeLeft = m_lifeTotal; // 剩余寿命从满值开始
}

// 推进一个固定步：剩余寿命递减并夹到 0（到达 0 即到期，等待场景清理）
void FloatingText::update(float deltaTime)
{
    // 递减后与 0 取大：负 deltaTime（调用方错误）也不会把寿命推到负值之外
    m_lifeLeft = std::max(m_lifeLeft - deltaTime, 0.0f);
}

// 绘制：按「寿命进度」计算上浮位置与淡出透明度，再交给 Font 居中绘制
void FloatingText::render(SDL_Renderer* renderer, const Font& font) const
{
    // 未激活/已到期（寿命为 0）或空文案：不产生任何绘制
    if (m_lifeLeft <= 0.0f || m_text.empty())
    {
        return;
    }

    // 进度 progress：0（刚出生）-> 1（寿命耗尽）。用于上浮与淡出的统一时间参数
    const float progress = 1.0f - m_lifeLeft / m_lifeTotal;

    // 当前顶边 y：从出生点线性上浮（进度越大、位置越高）
    const float currentY = m_topY - kRiseDistance * progress;

    // 淡出透明度：剩余寿命比例 x 255（出生时全亮、到点恰好淡尽），
    // 夹到 [0,255] 后转字节——浮点舍入不会产生越界的颜色分量
    const float alphaFloat = 255.0f * (m_lifeLeft / m_lifeTotal);
    const Uint8 alpha = static_cast<Uint8>(std::clamp(alphaFloat, 0.0f, 255.0f));

    // 设置渲染器绘制颜色（RGB 来自生成方，alpha 为寿命换算值）。
    // 失败只跳过本帧绘制：飘字是纯装饰，不因绘制错误影响玩法
    if (!SDL_SetRenderDrawColor(renderer, m_color.r, m_color.g, m_color.b, alpha))
    {
        return;
    }

    // Font 水平居中绘制（字体未加载时其内部安全跳过，不崩）
    font.drawCentered(renderer, m_text.c_str(), m_centerX, currentY);
}
