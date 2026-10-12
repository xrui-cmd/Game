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
        // SDL_LogError 由 SDL 统一处理输出（默认写入 stderr），不再依赖 windows.h 的调试输出
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL 初始化失败：%s", SDL_GetError());
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


    if (!m_player.load(m_renderer))
    {
        // 纹理创建失败：用 SDL_LogError 输出原因，格式串里用 %s 拼接 SDL_GetError
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "创建玩家纹理失败：%s", SDL_GetError());
        return GameError::RenderingFailed;
    }

    // 查询窗口实际尺寸并缓存到成员：金币出生和后续边界计算都基于它
    SDL_GetWindowSize(m_window, &m_width, &m_height);

    // 预创建全部金币对象；resize 按 kCoinCount 构造对象，此时纹理还是空的
    m_coins.resize(kCoinCount);

    // 玩家当前矩形：金币出生点尽量避开玩家所站的位置
    const SDL_FRect playerRect = m_player.rect();

    for (Coin& coin : m_coins)
    {
        // 为每个金币程序化创建黄色纹理，任一失败都视为渲染初始化失败
        if (!coin.load(m_renderer))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "创建金币纹理失败：%s", SDL_GetError());
            return GameError::RenderingFailed;
        }

        // 立即给一个随机出生点，避免所有金币叠在 (0,0)
        coin.respawn(m_width, m_height, playerRect);
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

    // 一帧内可能积累多个事件，因此需要全部取出处理。
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            m_running = false;
        }
        // 结束状态下按 R 重开：用"按键按下"事件而不是每帧轮询键盘状态，
        // 天然保证按一次只触发一次，长按不会反复重开
        else if (event.type == SDL_EVENT_KEY_DOWN &&
                 event.key.scancode == SDL_SCANCODE_R &&
                 m_state == GameState::GameOver)
        {
            restart(); // 重置整局数据并切回 Playing
        }
    }
}

void GameDev::update()
{
    // 只有 Playing 状态才推进游戏逻辑：
    // GameOver 之后玩家停止移动、倒计时不再走、金币也不再刷新
    if (m_state != GameState::Playing)
    {
        return;
    }

    const bool* keyboard = SDL_GetKeyboardState(nullptr);

    // 每帧查询窗口实际尺寸并缓存：玩家边界、金币重生范围都基于它，
    // 窗口尺寸一旦变化，逻辑也能自动跟随
    SDL_GetWindowSize(m_window, &m_width, &m_height);

    // 把窗口实际尺寸传给玩家：update 内部据此计算移动边界（不硬编码）
    m_player.update(m_deltaTime, keyboard,
                    static_cast<float>(m_width), static_cast<float>(m_height));

    // 玩家矩形只取一次：本帧所有金币都对它做碰撞检测
    const SDL_FRect playerRect = m_player.rect();

    // 逐个检测金币与玩家是否重叠：重叠即视为"收集成功"
    for (Coin& coin : m_coins)
    {
        // 推进本枚金币的旋转动画（纯画面表现，不影响 24x24 碰撞盒）
        coin.update(m_deltaTime);

        // AABB 相交检测；返回 true 说明玩家和这枚金币碰到了
        if (rectsOverlap(playerRect, coin.rect()))
        {
            m_score += 1; // 每枚金币计 1 分，累加到总分里

            // 立刻把这枚金币传送到新的随机位置（随机范围跟随窗口实际尺寸）
            coin.respawn(m_width, m_height, playerRect);
        }
    }

    // 倒计时按真实流逝时间递减（delta 单位是秒，与 30 秒常量同量纲）
    m_timeLeft -= m_deltaTime;

    // 时间耗尽：进入 GameOver 状态
    if (m_timeLeft <= 0.0f)
    {
        m_timeLeft = 0.0f;             // 夹到 0，避免 HUD 显示 -0.1 这类负数
        m_state = GameState::GameOver; // 冻结世界，等待玩家按 R 重开
    }
}

// 重开一局：把整局相关的数据全部恢复到初始状态
void GameDev::restart()
{
    // 分数清零，倒计时回到满值
    m_score = 0;
    m_timeLeft = kRoundSeconds;

    // 刷新窗口实际尺寸缓存：重开时窗口可能已被调整过大小
    SDL_GetWindowSize(m_window, &m_width, &m_height);

    // 玩家归位到窗口中心：坐标指左上角，所以要减去自身宽高的一半
    m_player.reset(m_width / 2.0f - m_player.width() / 2.0f,
                   m_height / 2.0f - m_player.height() / 2.0f);

    // 玩家矩形取一次，作为所有金币重生的避让区域
    const SDL_FRect playerRect = m_player.rect();

    // 所有金币重新随机位置，避免重开后还停在上一局的位置
    for (Coin& coin : m_coins)
    {
        coin.respawn(m_width, m_height, playerRect);
    }

    // 最后切回 Playing，新一局正式开始
    m_state = GameState::Playing;
}

GameError GameDev::render()
{
    // 设置清屏颜色。这里使用固定深蓝色作为背景。
    if (!SDL_SetRenderDrawColor(m_renderer, 20, 30, 50, 255))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "设置绘制颜色失败：%s", SDL_GetError());
        return GameError::RenderingFailed;
    }

    // 清除上一帧内容，避免旧画面残留。
    if (!SDL_RenderClear(m_renderer))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "清屏失败：%s", SDL_GetError());
        return GameError::RenderingFailed;
    }

    m_player.render(m_renderer);

    // 遍历绘制所有金币；数量少，直接循环即可（金币画在玩家之后）
    for (const Coin& coin : m_coins)
    {
        coin.render(m_renderer); // 每个金币把自身纹理画到当前位置
    }

    // 游戏世界画完后再叠加 HUD 文字（分数/倒计时/结束界面）。
    // HUD 绘制失败不影响主流程，最终由 RenderPresent 统一兜底判定
    renderHud();


    // 将本帧绘制结果提交到窗口。
    if (!SDL_RenderPresent(m_renderer))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "提交画面失败：%s", SDL_GetError());
        return GameError::RenderingFailed;
    }


    return GameError::None;
}

// 绘制 HUD 文字。使用 SDL3 内置的 SDL_RenderDebugText* 系列 API：
// 每个字符天生是 8x8 的等宽方形点阵，因此文字的像素宽度 = 8 * 字符数
void GameDev::renderHud()
{
    // 查询渲染器输出尺寸（物理像素）。HUD 的右上角对齐等定位
    // 都基于这个实际尺寸，而不是把 1280x720 硬编码进来
    int outW = 0;
    int outH = 0;
    SDL_GetRenderOutputSize(m_renderer, &outW, &outH);

    // 开启 kHudScale 倍渲染缩放：8x8 字模直接绘制太小，放大更易读。
    // 缩放后绘制坐标按"逻辑空间"解释：逻辑尺寸 = 输出尺寸 / kHudScale
    SDL_SetRenderScale(m_renderer, kHudScale, kHudScale);

    // 换算出逻辑空间宽高，供右上角对齐与水平居中计算
    const float logicalW = static_cast<float>(outW) / kHudScale;
    const float logicalH = static_cast<float>(outH) / kHudScale;

    if (m_state == GameState::Playing)
    {
        // 左上角：当前分数（白色文字）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        SDL_RenderDebugTextFormat(m_renderer, 10.0f, 10.0f, "Score: %d", m_score);

        // 右上角：剩余时间（黄色文字）。%4.1f 让数字部分恒占 4 个字符，
        // 于是整串 "Time: xx.x" 恒为 10 字符，宽度可精确算出
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 0, 255);
        constexpr float timeTextW = 10.0f * 8.0f; // 10 字符 × 每字符 8 逻辑像素
        SDL_RenderDebugTextFormat(m_renderer, logicalW - timeTextW - 10.0f, 10.0f,
                                  "Time: %4.1f", m_timeLeft);
    }
    else
    {
        // GameOver 界面：三行文字全部水平居中、围绕窗口中心排布。
        // 先算出每行文字宽度（8 逻辑像素/字符），再反推左起点实现居中

        // 水平中心与垂直中心，三段文字共用的基准点
        const float centerX = logicalW / 2.0f;
        const float centerY = logicalH / 2.0f;

        // 第一行：GAME OVER（白色）。"GAME OVER" 共 9 个字符
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        constexpr float overW = 9.0f * 8.0f; // 文字总宽（逻辑像素）
        SDL_RenderDebugTextFormat(m_renderer, centerX - overW / 2.0f, centerY - 30.0f, "GAME OVER");

        // 第二行：最终得分（黄色）。分数位数不定，先格式化到缓冲区，
        // 用实际字符数算出宽度，才能正确地水平居中
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 0, 255);
        char scoreText[32]; // 容纳 "Final Score: " + 最长数字，绰绰有余
        SDL_snprintf(scoreText, sizeof(scoreText), "Final Score: %d", m_score);
        const float scoreW = 8.0f * static_cast<float>(SDL_strlen(scoreText));
        SDL_RenderDebugText(m_renderer, centerX - scoreW / 2.0f, centerY, scoreText);

        // 第三行：重开提示（白色）。"Press R to Restart" 共 17 个字符
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        constexpr float restartW = 17.0f * 8.0f; // 文字总宽（逻辑像素）
        SDL_RenderDebugTextFormat(m_renderer, centerX - restartW / 2.0f, centerY + 30.0f,
                                  "Press R to Restart");
    }

    // 关键收尾：恢复默认缩放 (1,1)。
    // 否则下一帧的游戏世界（玩家/金币）也会被放大成 2 倍并错位
    SDL_SetRenderScale(m_renderer, 1.0f, 1.0f);
}

void GameDev::shutdown()
{
    m_running = false;

    // 关键顺序：所有纹理必须先于 renderer 释放。
    // 纹理由 renderer 创建并在其内部持有引用；若 renderer 先被销毁，
    // 之后再销毁纹理会构成"销毁后使用"的未定义行为（可能崩溃或泄漏驱动资源）。
    m_player.release(); // 提前释放玩家纹理；release 幂等，之后析构再调一次也安全

    // clear() 会析构所有 Coin 对象，每个 Coin 的析构内部会各自 release 纹理
    m_coins.clear();

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


