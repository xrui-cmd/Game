#include "GameDev.h"
#include "game/scenes/MenuScene.h" // 初始场景：主菜单（Enter 进入对局）
#include <memory>                  // std::make_unique：构造场景管理器与场景

// GameDev 离开作用域时自动释放 SDL 资源。
// main() 不需要手动调用 shutdown()。
GameDev::~GameDev()
{
    shutdown();
}

GameError GameDev::initialize()
{
    // 初始化 SDL 视频子系统。
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        // SDL_LogError 由 SDL 统一处理输出（默认写入 stderr）
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL 初始化失败：%s", SDL_GetError());
        return GameError::SdlInitFailed;
    }

    // 窗口由 GameDev 持有：主循环每帧查询它的尺寸装配视口信息
    m_window = SDL_CreateWindow(
        "GameDev",
        1280,
        720,
        0
    );

    if (m_window == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "窗口创建失败：%s", SDL_GetError());
        return GameError::WindowCreationFailed;
    }

    // Renderer 依赖窗口，因此必须在窗口创建成功后创建。
    m_renderer = SDL_CreateRenderer(m_window, nullptr);

    if (m_renderer == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "渲染器创建失败：%s", SDL_GetError());
        return GameError::RendererCreationFailed;
    }

    // 场景管理器绑定 renderer：之后每次切换，新场景的 onEnter
    // 都能立即拿到它加载纹理资源
    m_sceneManager = std::make_unique<SceneManager>(m_renderer);

    // 初始场景：主菜单（按 Enter 进入对局；从菜单开局 = 每次全新一局）
    m_sceneManager->requestScene(std::make_unique<MenuScene>());
    // 立即结算一次：主循环开始前当前场景就位，避免第一帧空转
    m_sceneManager->applyPendingChange();

    // 所有资源初始化成功后，游戏才可以进入主循环。
    m_running = true;
    return GameError::None;
}

GameError GameDev::run()
{
    m_lastTime = SDL_GetPerformanceCounter();   // 初始化基准，避免首帧 delta 爆炸

    while (m_running)
    {
        // 1. 先算本帧 delta，让 update() 能用到正确的值
        Uint64 curTime = SDL_GetPerformanceCounter();
        m_deltaTime = (curTime - m_lastTime) / static_cast<float>(SDL_GetPerformanceFrequency());
        m_lastTime = curTime;

        // 2. 处理输入：QUIT 在应用层消化，其余事件转发给当前场景
        processEvents();

        // 3. 更新与绘制当前场景（内部由 SceneManager 转发）
        update();

        const GameError result = render();
        if (result != GameError::None)
        {
            return result; // 提交画面失败属于致命错误：立刻退出主循环
        }

        // 4. 帧末统一结算场景切换：
        //    此时场景的一切回调都已返回，析构旧场景不会踩到自身的调用栈
        m_sceneManager->applyPendingChange();
    }

    return GameError::None;
}

void GameDev::processEvents()
{
    SDL_Event event{};

    // 一帧内可能积累多个事件，因此需要全部取出处理。
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            // 退出请求在应用层处理：场景不感知「关窗」这类应用级事件
            m_running = false;
        }
        else
        {
            // 其余事件全部转发给当前场景（键盘等由场景自行解释）
            m_sceneManager->handleEvent(event);
        }
    }
}

void GameDev::update()
{
    // 每帧查询窗口实际尺寸：视口尺寸是玩家移动边界与金币重生范围的计算基准
    // （窗口一旦变化，本帧逻辑立即跟随，与改造前行为一致）
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(m_window, &width, &height);

    // 装配本帧的「世界上下文」：一次构造，转发给当前场景，
    // 再由场景分发给场上实体（玩家与全部金币共享同一份快照）
    const UpdateContext ctx{
        m_deltaTime,                   // 距上一帧的真实流逝秒数（动画/倒计时基准）
        SDL_GetKeyboardState(nullptr), // SDL 键盘状态数组（WASD 轮询用）
        static_cast<float>(width),     // 视口宽：移动边界 / 重生范围的计算基准
        static_cast<float>(height)     // 视口高：同上
    };
    m_sceneManager->update(ctx);
}

GameError GameDev::render()
{
    // 绘制当前场景（清屏/背景/实体/HUD 都由场景自己完成）
    m_sceneManager->render();

    // 将本帧绘制结果提交到窗口（提交动作由外壳统一负责）
    if (!SDL_RenderPresent(m_renderer))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "提交画面失败：%s", SDL_GetError());
        return GameError::RenderingFailed;
    }

    return GameError::None;
}

void GameDev::shutdown()
{
    m_running = false;

    // 关键顺序①：场景（及其纹理）必须先于 renderer 释放。
    // 场景纹理由 renderer 创建并在其内部持有引用；若 renderer 先被销毁，
    // 之后再销毁纹理会构成「销毁后使用」的未定义行为（可能崩溃或泄漏驱动资源）。
    // clear() 同时释放「当前场景」与「尚未结算的待切场景」
    if (m_sceneManager != nullptr)
    {
        m_sceneManager->clear(); // 场景析构 -> 纹理释放（renderer 尚在，安全）
        m_sceneManager.reset();  // 管理器本体也随之销毁
    }

    // 关键顺序②：按依赖关系的逆序释放：Renderer 依赖 Window。
    if (m_renderer != nullptr)
    {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }

    if (m_window != nullptr)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }

    SDL_Quit();
}
