#ifndef __CARDLAYOUT_H__
#define __CARDLAYOUT_H__

#include <SDL3/SDL_rect.h> // SDL_FRect / SDL_FPoint / SDL_PointInRectFloat：屏幕空间矩形与点测

// ============================================================
// CardLayout：升级三选一面板的卡片布局（game 层纯函数，header-only）
// ============================================================
//
// 职责（批五A·卡牌鼠标选择）：把「面板卡片摆在哪」的计算从渲染里抽出来，
// 供渲染（renderHud 画卡/高亮）与鼠标处理（悬停判定/点击命中）共用同一份
// 公式——两处若各算各的，画出来的卡与点得到的卡会随调参逐渐漂移
//（「防坐标漂移」的结构性保证）。
//
// 坐标系：屏幕空间（面板是 HUD 层，不参与相机换算）。屏幕尺寸由调用方
// 给出（PlayScene 取 SDL_GetRenderOutputSize 的实际输出尺寸——与渲染
// 同一条数源）；鼠标位置直接用 Input 的窗口客户区坐标点测。
//
// 布局规则（与 M2 面板观感一致，数值为像素）：
//   - 卡片横排、间距 30、卡宽高 300；
//   - 整行水平居中（按实际张数排布——不足三张也居中）；
//   - 卡片行上边界 = 屏高 x 30%（标题之下）。
//
// 越界/非法输入防御：index 不在 [0, count) 或 count <= 0 时返回空矩形
//（w = h = 0，绘制与点测均可据 w > 0 判有效性），绝不返回未定义数据。
// ============================================================

// 面板卡片几何常量（绘制与命中共用的唯一数值来源；改动只改这一处）
inline constexpr float kCardPanelCardW = 300.0f;   // 卡宽（像素）
inline constexpr float kCardPanelCardH = 300.0f;   // 卡高（像素）
inline constexpr float kCardPanelCardGap = 30.0f;  // 卡间距（像素）
inline constexpr float kCardPanelTopRatio = 0.30f; // 卡片行上边界位置（屏高比例）

// 计算第 index 张卡的屏幕矩形（渲染与命中判定共用的同一份计算）。
// count 为实际张数（1~3），screenW/screenH 为屏幕尺寸；
// 返回空矩形（w = h = 0）表示「无此卡」——调用方以 w > 0 判有效
inline SDL_FRect cardPanelRectAt(int index, int count, float screenW, float screenH)
{
    // 位序/张数越界（含 count<=0 的池空防御）：没有对应的卡，返回空矩形
    if (index < 0 || index >= count || count <= 0)
    {
        return SDL_FRect{ 0.0f, 0.0f, 0.0f, 0.0f }; // 空矩形：无效占位
    }

    // 卡片行总宽（含间距）：count 张卡 + (count-1) 个间隙
    const float totalW = static_cast<float>(count) * kCardPanelCardW
                       + static_cast<float>(count - 1) * kCardPanelCardGap;
    // 首卡左边界：整行水平居中（屏幕中心 - 半行宽）
    const float startX = screenW * 0.5f - totalW * 0.5f;
    // 卡片行上边界：屏高比例（与标题的排版关系随窗口等比缩放）
    const float cardY = screenH * kCardPanelTopRatio;

    // 第 index 张的左边界 = 首卡 + index x（卡宽 + 间距）
    return SDL_FRect{ startX + static_cast<float>(index) * (kCardPanelCardW + kCardPanelCardGap),
                      cardY, kCardPanelCardW, kCardPanelCardH };
}

// 点 (x, y)（屏幕空间）命中哪张卡：返回展示位序（0 起）；未命中任何卡
// 返回 -1（卡间隙/面板外同为此值）。悬停高亮（每固定步刷新）与点击选择
//（按下边缘）共用——命中与绘制永远同源（都出自 cardPanelRectAt）
inline int cardPanelHitTest(float x, float y, int count, float screenW, float screenH)
{
    // 逐张点测：卡数最多 3，线性扫描最直观（也便于未来“卡重叠”扩展）
    for (int i = 0; i < count; ++i)
    {
        // 取本卡矩形（与渲染同一函数——面板画在哪就点测在哪）
        const SDL_FRect rect = cardPanelRectAt(i, count, screenW, screenH);
        // SDL 自带浮点矩形点测（边界均含的闭区间：SDL3 实现为
        // p->x <= r->x + r->w 之类）；空矩形（w=0）恒不命中
        const SDL_FPoint point{ x, y };
        if (SDL_PointInRectFloat(&point, &rect))
        {
            return i; // 命中本卡：返回其位序
        }
    }
    return -1; // 卡外：未命中
}

#endif // !__CARDLAYOUT_H__
