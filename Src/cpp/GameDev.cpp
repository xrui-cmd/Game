#include "GameDev.h"
#include "game/scenes/MenuScene.h"  // 初始场景：主菜单（Enter 进入对局）
#include <SDL3_ttf/SDL_ttf.h>       // TTF_Init / TTF_Quit：SDL3_ttf 库级生命周期
#include <memory>                   // std::make_unique：构造场景管理器与场景
#include <string>                   // std::string：拼接配置文件的绝对路径

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

    // 初始化 SDL3_ttf 库（字形栅格化/文本渲染的全局状态）。
    // 它是引用计数的：这里成功调用一次，shutdown 时配对 TTF_Quit 一次。
    // 失败不中止启动——UI 文本会整体降级为「安全跳过」（Font::load 失败并警告），
    // 游戏逻辑照常可玩，符合本项目「资源缺失不崩」的一贯策略
    if (!TTF_Init())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TTF 初始化失败：%s", SDL_GetError());
    }

    // 加载配置（可选步骤）：路径沿用 assets 的模式——以 exe 所在目录为基准，
    // 拼接 <exe目录>/config/game.ini。文件缺失/损坏都不致命：loadFromFile
    // 已记录警告，getInt/getFloat 会各自回退内置默认值，游戏照常运行
    const char* basePath = SDL_GetBasePath();
    if (basePath != nullptr)
    {
        // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
        const std::string configPath = std::string(basePath) + "config/game.ini";
        // 返回值刻意不判断「失败即中止」：加载失败是允许的降级路径
        m_config.loadFromFile(configPath);
    }

    // 窗口尺寸由配置决定（window_width / window_height，默认 1280x720）：
    // 改配置后重启即生效。极端非法尺寸（如 0）由 SDL_CreateWindow 的
    // 失败路径兜底，不会崩溃，只是初始化报错退出
    const int windowWidth = m_config.getInt("window_width", kDefaultWindowWidth);
    const int windowHeight = m_config.getInt("window_height", kDefaultWindowHeight);

    // 窗口由 GameDev 持有：主循环每帧查询它的尺寸装配视口信息。
    // 标题用 UTF-8 中英混排（源码按 /utf-8 编译，SDL 接受 UTF-8 字符串）
    m_window = SDL_CreateWindow(
        "COIN RUSH 金币冲刺",
        windowWidth,
        windowHeight,
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

    // 垂直同步：按配置启用/禁用（vsync=1 开启 / 0 关闭，默认 1）。
    // 两种取值都显式设置，不依赖 SDL 创建渲染器时的默认值——行为只由配置决定。
    // 开启后：渲染提交与显示器刷新对齐（画面无撕裂、CPU 占用低）；
    // 关闭后：帧率不设上限，便于观察固定步长在无 vsync 下的表现
    const int vsync = m_config.getInt("vsync", kDefaultVsync);
    // 配置归一化：非 0 一律映射到 SDL 的「开启」(1)，避免把非法值透传给 SDL
    const int vsyncMode = (vsync != 0) ? 1 : SDL_RENDERER_VSYNC_DISABLED;

    // 设置失败（驱动不支持等）只警告不致命：没有 vsync 游戏照常运行
    if (!SDL_SetRenderVSync(m_renderer, vsyncMode))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "设置垂直同步失败（%s），继续运行", SDL_GetError());
    }

    // 初始化音效系统（可选增强）：MIX_Init + 打开默认播放设备 + 预加载
    // 四个 WAV。任何环节失败（如系统无音频设备）都只在内部警告一次并
    // 整体降级——游戏静默运行，不因此中止启动，因此返回值刻意不判断
    m_audio.initialize();

    // 场景管理器绑定 renderer、配置与音效系统：之后每次切换，新场景的
    // onEnter 都能立即拿到它们加载纹理资源、读取配置数值、触发播放
    m_sceneManager = std::make_unique<SceneManager>(m_renderer, m_config, m_audio);

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
        // 1. 先算本渲染帧的真实间隔（秒）：性能计数器差值 / 频率。
        //    这是「真实时间」的唯一来源，逻辑步数由它累加而来
        const Uint64 curTime = SDL_GetPerformanceCounter();
        float frameDelta = (curTime - m_lastTime) / static_cast<float>(SDL_GetPerformanceFrequency());
        m_lastTime = curTime;

        // 钳制上限（防死亡螺旋第一道防线）：断点调试/系统休眠/拖动窗口
        // 会让间隔膨胀到几秒，不钳制的话累加器暴涨、单帧要补上千步，
        // 「补步越慢 -> 间隔越大 -> 补步更多」的雪球就此滚起来
        if (frameDelta > kMaxFrameDelta)
        {
            frameDelta = kMaxFrameDelta; // 超出部分直接丢弃，按 0.25 秒处理
        }

        // 2. 处理输入：QUIT 在应用层消化，其余事件转发给当前场景。
        //    事件按「渲染帧」粒度处理，本帧的所有固定步共享同一份键盘快照
        processEvents();

        // 3. 固定步长补步：把本帧时间余额换成一个或多个固定步的 update
        update(frameDelta);

        // 4. 插值渲染：用余额换算 alpha，在两次固定步位置之间平滑绘制
        const GameError result = render();
        if (result != GameError::None)
        {
            return result; // 提交画面失败属于致命错误：立刻退出主循环
        }

        // 5. 帧末统一结算场景切换：
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

void GameDev::update(float frameDelta)
{
    // 1) 把本渲染帧的真实时间存进累加器
    m_accumulator += frameDelta;

    // 2) 攒够一个固定步长才推进逻辑。高刷屏幕上可能连续几帧都不足一步
    //    （此时只做插值渲染），低帧率下一帧可能连续补多步
    if (m_accumulator >= kFixedDelta)
    {
        // 每渲染帧只查一次窗口尺寸：本帧的所有固定步共享同一视口快照。
        // 视口尺寸是玩家移动边界与金币重生范围的计算基准（沿用原行为：
        // 窗口一变化，本帧逻辑立即跟随）
        int width = 0;
        int height = 0;

        // 查询窗口实际尺寸（整数像素）
        SDL_GetWindowSize(m_window, &width, &height);

        // 装配固定步的「世界上下文」：一次构造，本帧所有固定步复用。
        // deltaTime 恒为 kFixedDelta（固定步长，与真实帧率无关）
        const UpdateContext fixedCtx{
            kFixedDelta,                   // 固定步长（秒）：逻辑与计时的唯一时间基准
            SDL_GetKeyboardState(nullptr), // SDL 键盘状态数组（事件已在帧首统一刷新）
            static_cast<float>(width),     // 视口宽：移动边界 / 重生范围的计算基准
            static_cast<float>(height)     // 视口高：同上
        };

        // 3) 固定步循环：每转一圈消耗一步余额，直到不足一步或达到上限
        int steps = 0; // 本帧已补步数（用于上限判断）
        while (m_accumulator >= kFixedDelta && steps < kMaxStepsPerFrame)
        {
            // 转发给当前场景：每步都恰好推进 kFixedDelta 的逻辑时间
            m_sceneManager->update(fixedCtx);
            m_accumulator -= kFixedDelta; // 消耗一步的时间余额
            ++steps;                      // 计数 +1，逼近单帧补步上限
        }

        // 4) 达补步上限且仍有余量：丢弃剩余余额（防死亡螺旋第二道防线）。
        //    正常情况（frameDelta 已被钳制在 0.25s）余额不会超出上限；
        //    这里是浮点边界与未来调整的兜底，保证欠账绝不留到下一帧
        if (steps >= kMaxStepsPerFrame && m_accumulator >= kFixedDelta)
        {
            m_accumulator = 0.0f; // 清零：不让补不完的时间滚雪球
        }
    }
}

GameError GameDev::render()
{
    // 插值系数 alpha：帧末余额换算成「距下一个固定步的进度」，范围 [0,1)。
    // 固定步循环结束后余额必然小于 kFixedDelta，所以自然落在区间内：
    //   alpha = 0   显示上一固定步的位置
    //   alpha -> 1  显示当前固定步的位置
    // 场景用它在「上一固定步位置 <-> 当前固定步位置」之间线性插值，
    // 让渲染帧率与逻辑步率不一致时画面依旧平滑
    const float alpha = m_accumulator / kFixedDelta;

    // 绘制当前场景（清屏/背景/实体/HUD 都由场景自己完成）
    m_sceneManager->render(alpha);

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

    // 关键顺序②：音效系统在场景之后（没有场景还会触发播放）、renderer
    // 之前（先于 SDL_Quit）释放：destroy() 内部关闭音频设备并退出 SDL
    // 音频子系统，与 GameDev::initialize 里的 initialize() 互为逆序。
    // 音效不依赖窗口/渲染器，放在这里即「消费方先释放、提供方后释放」
    m_audio.destroy();

    // 关键顺序③：按依赖关系的逆序释放：Renderer 依赖 Window。
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

    // SDL3_ttf 的库级收尾：此时所有 Font 对象已随场景析构释放（TTF_CloseFont
    // 均已完成），TTF_Quit 释放字体渲染的全局资源，与 initialize 的 TTF_Init 配对
    TTF_Quit();

    SDL_Quit();
}
