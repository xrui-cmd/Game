#ifndef __MENUSCENE_H__
#define __MENUSCENE_H__

#include "engine/Scene.h"       // Scene：场景基类
#include "engine/AudioSystem.h" // AudioSystem：音效系统（按 Enter 开始时播 start）
#include "engine/Font.h"        // Font：TTF 字体资源（标题/提示文字，支持中文）
#include "game/SaveData.h"      // SaveData / BestRecord：菜单展示最佳战绩（只读）

// ============================================================
// MenuScene：主菜单场景
// ============================================================
//
// 职责：
//   1. 资源：背景图与 PlayScene 相同（background.png，加载失败容忍）；
//      标题/提示字体（assets/fonts/SmileySans-Oblique.ttf，失败降级跳过文字）
//   2. 交互：按 Enter（确认动作，可在 config 改键）进入对局——先播 start
//      音效，再请求 SceneManager 切换到 PlayScene。输入走引擎输入系统的
//      pressed 边缘（update 内查询），与对局内其他动作同一套体系
//   3. 渲染：背景 + 四行文字——大号标题（工作名「ROGUE ARENA」，占位、
//      易改）/ 开始提示 / 操作说明 / 最佳战绩
//
// 历史注记：菜单曾展示「历史最高」小字（经 SaveData 读取）；改版中存档链路
// 一度整体停用；本批 SaveData 重构为「最佳战绩」三项纪录后回归——每次进入
// 菜单时读档一次（onEnter），无记录时展示「暂无」
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

    // 每帧更新：查询确认动作的按下边缘（Enter -> 进入对局）。
    // pressed 在整帧的所有固定步都为 true，用去重标记保证按一次只触发一次
    void update(const UpdateContext& ctx) override;

    // 渲染：背景 + 标题 + 提示文字（无其它更新逻辑）。
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

    // 确认动作的去重标记：记录「上一次更新采样时 pressed 的值」。
    // pressed 在整帧的所有固定步都为 true（引擎按渲染帧采样），
    // 若不去重，一帧内补多个固定步会请求切换多次（构造多个 PlayScene）；
    // 该标记把响应折叠为「每个上升沿恰好一次」
    bool m_confirmHandled = false;

    // 字体资源：一个实例对应一个字号。加载失败时 draw/measure 安全跳过
    // （Font 内部已记录警告），菜单仍然可玩（背景 + 无文字）
    Font m_titleFont; // 大号标题字体（"ROGUE ARENA"）
    Font m_hintFont;  // 提示文字字体（开始提示 / 操作说明 / 最佳战绩）

    // 最佳战绩快照：onEnter 时经 SaveData 读档一次（文件缺失/损坏自动按
    // 无记录处理，永不失败）。菜单只做只读展示——纪录的比较与写盘
    // 全部发生在对局侧（PlayScene 的 GameOver 结算），菜单不写档
    BestRecord m_best;
};

#endif // !__MENUSCENE_H__
