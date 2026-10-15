#ifndef __FLOATINGTEXT_H__
#define __FLOATINGTEXT_H__

#include <SDL3/SDL.h> // SDL_Color / SDL_Renderer：颜色与绘制目标的类型
#include <string>     // std::string：文案存储（spawn 时拷贝，不依赖调用方缓冲的生命周期）

// ============================================================
// FloatingText：漂浮文字（game 层，轻量上浮+淡出小类）
// ============================================================
//
// 用途（本批两种反馈共用同一套实现）：
//   - 伤害飘字：自动攻击命中时在怪物位置弹出「-25」（当前实际伤害值）；
//   - 升级飘字：升级时在玩家上方弹出「升级！」。
// 共用一套的意义：行为（上浮速度/淡出曲线/生命周期）只定义一次，
// 后续加新的飘字反馈（如拾取 +10 经验）不需要再写一套动画。
//
// 渲染方式：交给场景的 Font（TTF 中文字体）绘制——「升级！」是中文，
// 必须走 TTF；伤害数字也沿用同一字体，版式统一、无需第二套字体资源。
// 颜色由生成方指定（SDL_Color 含 RGBA）：淡出所需的 alpha 直接来自
// 本类按剩余寿命换算，Font 取渲染器当前绘制颜色时一并取走。
//
// 生命周期（由场景驱动，与固定步长同源）：
//   spawn -> 每固定步 update(剩余寿命递减、位置按进度上浮) -> 寿命耗尽被清理。
//   暂停/结束时场景不调用 update，飘字随世界一起冻结（渲染保持静止画面）。
//
// 上浮与淡出曲线（全程线性，简单可预期）：
//   进度 progress = 1 - 剩余/总寿命（0 -> 1）：
//   - 纵坐标：从出生点上浮 kRiseDistance x progress；
//   - 透明度：alpha = 255 x 剩余/总寿命（出生全亮，到点恰好淡尽）。
// ============================================================

// 前置声明：头文件里 Font 只以引用形式出现，不需要完整定义（减少头依赖）；
// 完整定义由 FloatingText.cpp 包含 "engine/Font.h" 获得
class Font;

class FloatingText
{
public:
    // 默认存活时长（秒）：伤害飘字的节奏——够看清、不占屏（升级飘字会传更长值）
    static constexpr float kDefaultLifeSeconds = 0.8f;

    // 全程上浮距离（像素）：飘字从出生点向上漂移的总位移
    static constexpr float kRiseDistance = 48.0f;

    // 生成（可复用一个实例槽位）：text 为 UTF-8 文案（拷贝保存——「-25」这类
    // 文案来自栈上缓冲，不能只存指针）；centerX 为文字水平中心；topY 为文字
    // 顶边出生位置；color 为基础颜色（alpha 分量在渲染时按寿命另行换算）；
    // lifeSeconds 小于等于 0 时回退默认值（防御调用方传坏值）
    void spawn(const char* text, float centerX, float topY, SDL_Color color,
               float lifeSeconds = kDefaultLifeSeconds);

    // 推进一个固定步：剩余寿命递减（夹到 0）。位置与透明度在渲染时按
    // 「寿命进度」即时换算，因此本函数只维护时间轴、不做多余状态更新
    void update(float deltaTime);

    // 是否已到期（场景据此统一清理）
    bool isExpired() const { return m_lifeLeft <= 0.0f; }

    // 绘制：按寿命进度计算上浮后的位置与 alpha，设置渲染器绘制颜色后
    // 交给 Font 水平居中绘制。字体未加载时 Font 内部安全跳过（不崩）
    void render(SDL_Renderer* renderer, const Font& font) const;

private:
    std::string m_text;              // 文案（UTF-8；spawn 时拷贝保存）
    float m_centerX = 0.0f;          // 文字水平中心（屏幕像素）
    float m_topY = 0.0f;             // 出生时文字顶边（屏幕像素）
    float m_lifeLeft = 0.0f;         // 剩余寿命（秒；<=0 表示未激活/已到期）
    float m_lifeTotal = kDefaultLifeSeconds; // 本次生成的总寿命（秒）：淡出与上浮的分母
    SDL_Color m_color{ 255, 255, 255, 255 }; // 基础颜色（alpha 在渲染时被替换为寿命换算值）
};

#endif // !__FLOATINGTEXT_H__
