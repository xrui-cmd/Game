#include "game/scenes/MenuScene.h"
#include "game/scenes/PlayScene.h" // PlayScene：Enter 后要切换过去的对局场景
#include "engine/DebugText.h"      // DebugText：菜单文字的缩放封装
#include "engine/SceneManager.h"   // SceneManager：m_sceneManager->requestScene 需要完整类型
#include <SDL3_image/SDL_image.h>  // IMG_LoadTexture：加载 assets/background.png 背景图
#include <memory>                  // std::make_unique：构造 PlayScene
#include <string>                  // std::string：拼接背景素材的绝对路径

// 析构：释放背景纹理。
// 关键时机与 PlayScene 相同：SceneManager 先析构旧场景、后销毁 renderer，
// 因此这里释放纹理时 renderer 尚在，安全
MenuScene::~MenuScene()
{
    // 只有纹理确实存在时才销毁，避免对空指针调用 SDL_DestroyTexture
    if (m_background != nullptr)
    {
        SDL_DestroyTexture(m_background); // 释放背景图占用的显存
        m_background = nullptr;           // 置空：即使重复调用也不会二次销毁
    }
}

// 进入场景：保存 renderer、加载背景图。
// config 由 SceneManager 注入，本阶段菜单没有可配置项，参数暂不使用
void MenuScene::onEnter(SDL_Renderer* renderer, const Config& /*config*/)
{
    // 1) 保存 renderer：后续渲染与资源加载都用它（只借用，不负责销毁）
    m_renderer = renderer;

    // 冒烟观测点：日志确认场景切换实际发生（自动化测试据此断言流程）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "进入场景: Menu");

    // 2) 加载背景图（可选资源，与对局共用同一张 background.png）：
    //    拼出 <exe目录>/assets/background.png 交给 SDL3_image 解码；
    //    加载失败只记录警告并继续，渲染时跳过背景露出深蓝色兜底
    const char* basePath = SDL_GetBasePath();
    if (basePath != nullptr)
    {
        // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
        std::string bgPath = std::string(basePath) + "assets/background.png";
        // IMG_LoadTexture：读文件 -> 解码 PNG -> 一步创建 GPU 纹理
        m_background = IMG_LoadTexture(m_renderer, bgPath.c_str());
    }
    // 无论失败原因是「拿不到基路径」还是「图片读取失败」，都统一走这条警告
    if (m_background == nullptr)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "加载背景图失败（%s），使用纯色背景兜底", SDL_GetError());
    }
}

// 事件处理：按 Enter 开始游戏。
// 用「按键按下」事件而不是每帧轮询键盘状态，天然保证按一次只触发一次
void MenuScene::handleEvent(const SDL_Event& event)
{
    // 只关心按键按下事件；其余事件一律忽略
    if (event.type != SDL_EVENT_KEY_DOWN)
    {
        return;
    }

    // 主键盘 Enter 与小键盘 Enter 都接受（SDL 把它们区分为两个键码）
    if (event.key.scancode == SDL_SCANCODE_RETURN ||
        event.key.scancode == SDL_SCANCODE_KP_ENTER)
    {
        // 请求切换到对局场景：只登记，帧末由 SceneManager 统一结算。
        // 每次按 Enter 都构造一个新的 PlayScene —— 「每次进入 = 全新一局」
        m_sceneManager->requestScene(std::make_unique<PlayScene>());
    }
}

// 每帧渲染：清屏 -> 背景 -> 标题与提示文字。
// 不调用 SDL_RenderPresent（提交由应用外壳统一负责）。
// 菜单是静态界面，没有可插值实体，alpha 参数不使用
void MenuScene::render(float /*alpha*/)
{
    // 设置清屏颜色。深蓝是背景图缺失时的兜底底色（与对局场景一致）
    if (!SDL_SetRenderDrawColor(m_renderer, 20, 30, 50, 255))
    {
        // 设置颜色失败不影响继续绘制：记录后继续（最终由提交环节兜底判定）
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "设置绘制颜色失败：%s", SDL_GetError());
    }

    // 清除上一帧内容，避免旧画面残留
    if (!SDL_RenderClear(m_renderer))
    {
        // 清屏失败同样只记录：避免单帧故障中断菜单显示
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "清屏失败：%s", SDL_GetError());
    }

    // 查询渲染输出尺寸：背景铺满与文字居中都以它为基准
    int outW = 0;
    int outH = 0;
    SDL_GetRenderOutputSize(m_renderer, &outW, &outH);

    // 背景铺满整个渲染输出（尺寸无关：窗口多大，背景就多大）
    if (m_background != nullptr)
    {
        // srcRect 传 nullptr 表示使用整张背景纹理
        const SDL_FRect bgDst{ 0.0f, 0.0f,
                               static_cast<float>(outW), static_cast<float>(outH) };
        SDL_RenderTexture(m_renderer, m_background, nullptr, &bgDst);
    }

    // 屏幕宽高转浮点：文字布局统一按「屏幕像素」表达
    const float screenW = static_cast<float>(outW); // 屏幕宽（像素）
    const float screenH = static_cast<float>(outH); // 屏幕高（像素）

    // 第一行：大号标题（金黄色，与金币呼应）
    SDL_SetRenderDrawColor(m_renderer, 255, 215, 0, 255);
    DebugText::drawCentered(m_renderer, kTitleScale, screenW / 2.0f, screenH * 0.28f,
                            "COIN RUSH");

    // 第二行：开始提示（白色）
    SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
    DebugText::drawCentered(m_renderer, kHintScale, screenW / 2.0f, screenH * 0.55f,
                            "Press ENTER to Start");

    // 第三行：操作说明（白色小字）
    DebugText::drawCentered(m_renderer, kHintScale, screenW / 2.0f, screenH * 0.68f,
                            "WASD Move / ESC Pause");
}
