#ifndef __MENUSCENE_H__
#define __MENUSCENE_H__

#include "engine/Scene.h"       // Scene：场景基类
#include "engine/AudioSystem.h" // AudioSystem：音效系统（按 Enter 开始时播 start）
#include "engine/Font.h"        // Font：TTF 字体资源（标题/提示文字，支持中文）

// ============================================================
// MenuScene：主菜单场景
// ============================================================
//
// 职责：
//   1. 资源：背景图与 PlayScene 相同（background.png，加载失败容忍）；
//      标题/提示字体（assets/fonts/SmileySans-Oblique.ttf，失败降级跳过文字）
//   2. 交互：按 Enter 进入对局（先播 start 音效，再请求 SceneManager
//      切换到 PlayScene）
//   3. 渲染：背景 + 三行文字——大号标题 / 开始提示 / 操作说明
//
// 切换时机：Enter 只是「请求」切换，旧场景的析构与新场景的加载
// 由 SceneManager 在帧末统一结算（原因见 SceneManager.h 的说明）
//
// 文字用 TTF 字体绘制（引擎层 Font 封装），支持 UTF-8 中文；
// FPS 等调试行仍用 SDL 内置调试字体（仅 ASCII，见 DebugText.h）
// ============================================================

class MenuScene : public Scene
{
public:
    MenuScene() = default; // 无初始状态，默认构造即可
    ~MenuScene() override;  // 析构释放背景纹理与字体（此时 renderer 尚在，安全）

    // 进入场景：保存 renderer 与音效系统、加载背景图与字体。
    // config 本阶段没有菜单可调项，签名与基类保持一致（暂不使用）
    void onEnter(SDL_Renderer* renderer, const Config& /*config*/, AudioSystem& audio) override;

    // 事件：按 Enter 请求进入对局
    void handleEvent(const SDL_Event& event) override;

    // 渲染：背景 + 标题 + 提示文字（无更新逻辑，不覆写 update）。
    // 菜单是纯静态界面，没有可插值的实体，alpha 参数忽略
    void render(float alpha) override;

private:
    static constexpr float kTitlePtSize = 96.0f; // 标题字号（像素高，大号观感）
    static constexpr float kHintPtSize = 32.0f;  // 提示文字字号（与 HUD 同级）

    SDL_Renderer* m_renderer = nullptr; // onEnter 保存的渲染器（只借用，不持有所有权）

    // onEnter 注入的音效系统指针（所有权在 GameDev，受管理期间必然非空）。
    // 菜单只用它播一个音效：按 Enter 开始时的 start 提示音
    AudioSystem* m_audio = nullptr;

    // 背景纹理：加载失败时为 nullptr，render 阶段会跳过，
    // 露出清屏深蓝色兜底（与对局场景共用同一张 background.png）
    SDL_Texture* m_background = nullptr;

    // 字体资源：一个实例对应一个字号。加载失败时 draw/measure 安全跳过
    // （Font 内部已记录警告），菜单仍然可玩（背景 + 无文字）
    Font m_titleFont; // 大号标题字体（"COIN RUSH"）
    Font m_hintFont;  // 提示文字字体（开始提示 / 操作说明）
};

#endif // !__MENUSCENE_H__
