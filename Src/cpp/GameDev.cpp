#include "../inc/GameDev.h"


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
        std::cerr << "SDL 初始化失败：" << SDL_GetError() << '\n';
        return GameError::SdlInitFailed; // 按现有错误码名称调整
    }

    // 窗口由 GameDev 持有，供事件循环和渲染阶段使用。
    m_window = SDL_CreateWindow(
        "GameDev",
        1280,
        720,
        0
    );

    if (m_window == nullptr)
    {
        std::cerr << "窗口创建失败：" << SDL_GetError() << '\n';
        return GameError::WindowCreationFailed;
    }

    // Renderer 依赖窗口，因此必须在窗口创建成功后创建。
    m_renderer = SDL_CreateRenderer(m_window, nullptr);

    if (m_renderer == nullptr)
    {
        std::cerr << "渲染器创建失败：" << SDL_GetError() << '\n';
        return GameError::RendererCreationFailed;
    }


    if (!m_player.load(m_renderer))
    {
        OutputDebugStringA("创建玩家纹理失败：");
        OutputDebugStringA(SDL_GetError());
        OutputDebugStringA("\n");
        return GameError::RenderingFailed;
    }

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

        // 2. 再处理输入、更新、渲染
        processEvents();
        update();

        const GameError result = render();
        if (result != GameError::None)
        {
            return result;
        }
    }

    return GameError::None;
}

void GameDev::processEvents()
{
    SDL_Event event{};

    const bool* keyboard = SDL_GetKeyboardState(nullptr);

    // 一帧内可能积累多个事件，因此需要全部取出处理。
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            m_running = false;
        }
    }
}

void GameDev::update()
{
    const bool* keyboard = SDL_GetKeyboardState(nullptr);
    m_player.update(m_deltaTime, keyboard);
}

GameError GameDev::render()
{
    // 设置清屏颜色。这里使用固定深蓝色作为背景。
    if (!SDL_SetRenderDrawColor(m_renderer, 20, 30, 50, 255))
    {
        std::cerr << "设置绘制颜色失败：" << SDL_GetError() << '\n';
        return GameError::RenderingFailed;
    }

    // 清除上一帧内容，避免旧画面残留。
    if (!SDL_RenderClear(m_renderer))
    {
        std::cerr << "清屏失败：" << SDL_GetError() << '\n';
        return GameError::RenderingFailed;
    }

    m_player.render(m_renderer);


    // 将本帧绘制结果提交到窗口。
    if (!SDL_RenderPresent(m_renderer))
    {
        std::cerr << "提交画面失败：" << SDL_GetError() << '\n';
        return GameError::RenderingFailed;
    }


    return GameError::None;
}

void GameDev::shutdown()
{
    m_running = false;

    // 按依赖关系的逆序释放：Renderer 依赖 Window。
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


