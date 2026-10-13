#ifndef __MENUSCENE_H__
#define __MENUSCENE_H__

#include "engine/Scene.h" // Scene：场景基类

// ============================================================
// MenuScene：主菜单场景
// ============================================================
//
// 职责：
//   1. 资源：背景图与 PlayScene 相同（background.png，加载失败容忍）
//   2. 交互：按 Enter 进入对局（请求 SceneManager 切换到 PlayScene）
//   3. 渲染：背景 + 三行文字——大号标题 / 开始提示 / 操作说明
//
// 切换时机：Enter 只是「请求」切换，旧场景的析构与新场景的加载
// 由 SceneManager 在帧末统一结算（原因见 SceneManager.h 的说明）
//
// 文字用 SDL 内置调试字体绘制，仅支持 ASCII，因此文案全部用英文
// ============================================================

class MenuScene : public Scene
{
public:
    MenuScene() = default; // 无初始状态，默认构造即可
    ~MenuScene() override;  // 析构释放背景纹理（此时 renderer 尚在，安全）

    // 进入场景：保存 renderer、加载背景图。
    // config 本阶段没有菜单可调项，签名与基类保持一致（暂不使用）
    void onEnter(SDL_Renderer* renderer, const Config& /*config*/) override;

    // 事件：按 Enter 请求进入对局
    void handleEvent(const SDL_Event& event) override;

    // 渲染：背景 + 标题 + 提示文字（无更新逻辑，不覆写 update）
    void render() override;

private:
    static constexpr float kTitleScale = 5.0f; // 标题文字放大倍数（大号观感）
    static constexpr float kHintScale = 2.0f;  // 提示文字放大倍数（与 HUD 一致）

    SDL_Renderer* m_renderer = nullptr; // onEnter 保存的渲染器（只借用，不持有所有权）

    // 背景纹理：加载失败时为 nullptr，render 阶段会跳过，
    // 露出清屏深蓝色兜底（与对局场景共用同一张 background.png）
    SDL_Texture* m_background = nullptr;
};

#endif // !__MENUSCENE_H__
