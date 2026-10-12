#include "../inc/GameDev.h"
#include "../inc/Rect.h"


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

    // 查询窗口尺寸：金币出生需要知道可放置范围（不硬编码 1280x720）
    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(m_window, &winW, &winH);

    // 预创建全部金币对象；resize 按 kCoinCount 构造对象，此时纹理还是空的
    m_coins.resize(kCoinCount);

    // 玩家当前矩形：金币出生点尽量避开玩家所站的位置
    const SDL_FRect playerRect = m_player.rect();

    for (Coin& coin : m_coins)
    {
        // 为每个金币程序化创建黄色纹理，任一失败都视为渲染初始化失败
        if (!coin.load(m_renderer))
        {
            OutputDebugStringA("创建金币纹理失败：");
            OutputDebugStringA(SDL_GetError());
            OutputDebugStringA("\n");
            return GameError::RenderingFailed;
        }

        // 立即给一个随机出生点，避免所有金币叠在 (0,0)
        coin.respawn(winW, winH, playerRect);
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

    // 玩家矩形只取一次：本帧所有金币都对它做碰撞检测
    const SDL_FRect playerRect = m_player.rect();

    // 查询窗口尺寸，供金币重生时限定随机范围（跟随窗口实际大小）
    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(m_window, &winW, &winH);

    // 逐个检测金币与玩家是否重叠：重叠即视为"收集成功"
    for (Coin& coin : m_coins)
    {
        // AABB 相交检测；返回 true 说明玩家和这枚金币碰到了
        if (rectsOverlap(playerRect, coin.rect()))
        {
            m_score += 1; // 每枚金币计 1 分，累加到总分里

            // 立刻把这枚金币传送到新的随机位置，实现"无限收集"的循环
            coin.respawn(winW, winH, playerRect);
        }
    }
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

    // 遍历绘制所有金币；数量少，直接循环即可（金币画在玩家之后）
    for (const Coin& coin : m_coins)
    {
        coin.render(m_renderer); // 每个金币把自身纹理画到当前位置
    }


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


