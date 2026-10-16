#ifndef __SHOPLAYOUT_H__
#define __SHOPLAYOUT_H__

#include <SDL3/SDL_rect.h> // SDL_FRect / SDL_FPoint / SDL_PointInRectFloat：屏幕空间矩形与点测

// ============================================================
// ShopLayout：商店面板的九宫格布局（game 层纯函数，header-only）
// ============================================================
//
// 职责（批五B；与 CardLayout 同款纪律）：把「商店卡片摆在哪」的计算从
// 渲染里抽出来，供渲染（renderHud 画卡/悬停高亮/金币不足灰显）与鼠标
// 处理（悬停判定/点击购买）共用同一份公式——两处若各算各的，画出来的
// 卡与点得到的卡会随调参逐渐漂移（「防坐标漂移」的结构性保证）。
//
// 坐标系：屏幕空间（面板是 HUD 层，不参与相机换算）。屏幕尺寸由调用方
// 给出（PlayScene 取 SDL_GetRenderOutputSize 的实际输出尺寸——与渲染
// 同一条数源）；鼠标位置直接用 Input 的窗口客户区坐标点测。
//
// 布局规则（9 项 = 强化表顺序，行优先：0/1/2 第一行、3/4/5 第二行、
// 6/7/8 第三行；数值为像素）：
//   - 卡片 320x130、间距 24；
//   - 整组水平居中、上边界 = 屏高 x 16%（标题之下）。
//
// 越界/非法输入防御：index 不在 [0, kShopItemCount) 时返回空矩形
//（w = h = 0，绘制与点测均可据 w > 0 判有效性），绝不返回未定义数据。
// ============================================================

inline constexpr int kShopItemCount = 9;   // 商店项数（= 强化表张数）
inline constexpr int kShopColumns = 3;     // 网格列数（行数 = 项数 / 列数 = 3）

// 面板几何常量（绘制与命中共用的唯一数值来源；改动只改这一处）
inline constexpr float kShopCardW = 320.0f;    // 卡片宽（像素）
inline constexpr float kShopCardH = 130.0f;    // 卡片高（像素）
inline constexpr float kShopCardGap = 24.0f;   // 卡片间距（像素）
inline constexpr float kShopPanelTopRatio = 0.16f; // 面板上边界位置（屏高比例）

// 计算第 index 项的屏幕矩形（渲染与命中判定共用的同一份计算）。
// screenW/screenH 为屏幕尺寸；返回空矩形（w = h = 0）表示「无此项」
inline SDL_FRect shopPanelRectAt(int index, float screenW, float screenH)
{
    // 越界（无此项）：返回空矩形占位（调用方以 w > 0 判有效）
    if (index < 0 || index >= kShopItemCount)
    {
        return SDL_FRect{ 0.0f, 0.0f, 0.0f, 0.0f }; // 无效矩形
    }

    // 整组总宽/总高：kShopColumns 列（行数由项数推出——3x3）
    const float totalW = static_cast<float>(kShopColumns) * kShopCardW
                       + static_cast<float>(kShopColumns - 1) * kShopCardGap; // 宽 = 3 卡 + 2 隙
    const float totalH = 3.0f * kShopCardH + 2.0f * kShopCardGap;             // 高 = 3 卡 + 2 隙

    // 组内起始位置：水平居中（屏幕中心 - 半组宽）、上边界 = 屏高比例
    const float startX = screenW * 0.5f - totalW * 0.5f; // 首卡左边界
    const float startY = screenH * kShopPanelTopRatio;   // 首行上边界

    // 行优先换算：列 = index % 3、行 = index / 3
    const int col = index % kShopColumns; // 列号（0~2）
    const int row = index / kShopColumns; // 行号（0~2）

    // 第 index 项的左边界 = 首卡 + 列 x（卡宽 + 隙）；上边界同理
    return SDL_FRect{ startX + static_cast<float>(col) * (kShopCardW + kShopCardGap),
                      startY + static_cast<float>(row) * (kShopCardH + kShopCardGap),
                      kShopCardW, kShopCardH };
}

// 点 (x, y)（屏幕空间）命中哪一项：返回项下标（0 起）；未命中任何项
// 返回 -1（卡间隙/面板外同为此值）。悬停高亮（每固定步刷新）与点击
// 购买（按下边缘）共用——命中与绘制永远同源（都出自 shopPanelRectAt）
inline int shopPanelHitTest(float x, float y, float screenW, float screenH)
{
    for (int i = 0; i < kShopItemCount; ++i) // 逐项点测（9 项，线性扫描最直观）
    {
        // 取本项矩形（与渲染同一函数——面板画在哪就点测在哪）
        const SDL_FRect rect = shopPanelRectAt(i, screenW, screenH);
        // SDL 自带浮点矩形点测（边界均含的闭区间）；空矩形（w=0）恒不命中
        const SDL_FPoint point{ x, y };
        if (SDL_PointInRectFloat(&point, &rect))
        {
            return i; // 命中本项：返回下标
        }
    }
    return -1; // 卡外：未命中
}

#endif // !__SHOPLAYOUT_H__
