#include "GameDev.h"
#include "game/scenes/MenuScene.h"  // 初始场景：主菜单（Enter 进入对局）
#include <SDL3_ttf/SDL_ttf.h>       // TTF_Init / TTF_Quit：SDL3_ttf 库级生命周期
#include <cctype>                   // std::toupper：键名解析前统一大写归一化
#include <memory>                   // std::make_unique：构造场景管理器与场景
#include <string>                   // std::string：拼接配置文件的绝对路径

// 本编译单元私有的输入绑定解析工具（不对外暴露）
namespace
{

// 键名 -> SDL 扫描码的「命名键」查找表：只收录带名字的非字母数字键。
// 字母 A~Z 与数字 0~9 不在此表中——它们的扫描码是连续区段，由
// parseKeyName 直接按偏移计算（少写几十行表项，见下）
struct NamedKey
{
    const char* name;  // 配置里书写的键名（解析前统一归一化为大写）
    SDL_Scancode key;  // 对应的 SDL 扫描码（与键盘布局无关的物理键位码）
};

// 命名键表：大小写不敏感；需要新键时在此追加（表小，线性查找足够直观）
constexpr NamedKey kNamedKeys[] = {
    { "ESC", SDL_SCANCODE_ESCAPE },          // ESC 简写
    { "ESCAPE", SDL_SCANCODE_ESCAPE },       // ESC 全拼
    { "ENTER", SDL_SCANCODE_RETURN },        // 主键盘回车
    { "RETURN", SDL_SCANCODE_RETURN },       // ENTER 的别名写法
    { "KP_ENTER", SDL_SCANCODE_KP_ENTER },   // 小键盘回车
    { "SPACE", SDL_SCANCODE_SPACE },         // 空格
    { "TAB", SDL_SCANCODE_TAB },             // 制表
    { "BACKSPACE", SDL_SCANCODE_BACKSPACE }, // 退格
    { "LEFT", SDL_SCANCODE_LEFT },           // 方向键：左
    { "RIGHT", SDL_SCANCODE_RIGHT },         // 方向键：右
    { "UP", SDL_SCANCODE_UP },               // 方向键：上
    { "DOWN", SDL_SCANCODE_DOWN },           // 方向键：下
    { "SHIFT", SDL_SCANCODE_LSHIFT },        // 修饰键统一取左侧键位
    { "CTRL", SDL_SCANCODE_LCTRL },          // CTRL 简写
    { "CONTROL", SDL_SCANCODE_LCTRL },       // CTRL 全拼
    { "ALT", SDL_SCANCODE_LALT },            // ALT（取左侧）
};

// 把配置里的键名解析成 SDL 扫描码。解析规则（大小写不敏感）：
//   1. 单字母 A~Z —— SDL_SCANCODE_A 起连续区段，按字母偏移直达
//   2. 单数字 0~9 —— '1'~'9' 连续；'0' 在 SDL 枚举里排在 '9' 之后，单独映射
//   3. F1~F12    —— SDL_SCANCODE_F1 起连续区段，按编号偏移
//   4. 命名键     —— 查 kNamedKeys 表
// 未识别的键名返回 SDL_SCANCODE_UNKNOWN（枚举值 0），调用方据此警告并回退
SDL_Scancode parseKeyName(const std::string& name)
{
    // 归一化为大写后逐规则匹配：拷贝一份处理，不修改传入的原文
    std::string upper = name;
    for (char& c : upper)
    {
        // toupper 只接受 unsigned char 范围或 EOF：先转换再转回 char，
        // 避免负值字符（非 ASCII）传入造成未定义行为
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    // 规则 1：单字母（'A' 的扫描码 + 字母序号 = 该字母的扫描码）
    if (upper.size() == 1 && upper[0] >= 'A' && upper[0] <= 'Z')
    {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (upper[0] - 'A'));
    }

    // 规则 2：单数字——'1'~'9' 连续段按偏移映射
    if (upper.size() == 1 && upper[0] >= '1' && upper[0] <= '9')
    {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (upper[0] - '1'));
    }
    // '0' 不取巧：SDL 枚举里 0 排在 9 之后，直接给出（单条目不值得再凑公式）
    if (upper.size() == 1 && upper[0] == '0')
    {
        return SDL_SCANCODE_0;
    }

    // 规则 3：F1~F12（"F12" 两位数时按十位/个位还原编号）
    if (upper.size() >= 2 && upper[0] == 'F' && upper[1] >= '1' && upper[1] <= '9')
    {
        int number = upper[1] - '0'; // 十位数字（F1~F9 时即编号本身）
        if (upper.size() == 3 && upper[2] >= '0' && upper[2] <= '9')
        {
            number = number * 10 + (upper[2] - '0'); // 个位：拼出 F10~F19 的编号
        }
        if (upper.size() <= 3 && number >= 1 && number <= 12)
        {
            return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + (number - 1));
        }
    }

    // 规则 4：命名键线性查表（条目少，几次字符串比较即可）
    for (const NamedKey& entry : kNamedKeys)
    {
        if (upper == entry.name)
        {
            return entry.key;
        }
    }

    // 全部规则未命中：未知键名，交由调用方走「警告 + 默认回退」
    return SDL_SCANCODE_UNKNOWN;
}

} // 匿名命名空间结束

// GameDev 离开作用域时自动释放 SDL 资源。
// main() 不需要手动调用 shutdown()。
GameDev::~GameDev()
{
    shutdown();
}

// 装配输入绑定：config/game.ini 的 bind_* 键 -> 语义动作名。
// 调用时机：配置加载完成之后、场景创建之前（场景/实体随时可以查询动作）。
// 每个动作的默认键名在调用处给出：缺键时 getString 返回默认键名；
// 键名无法识别时警告并回退该动作的默认键——配置写错不会让游戏失去操作
void GameDev::bindInputActions()
{
    // 单个动作的绑定流程（lambda 收敛重复十次的读-解析-回退-绑定逻辑）：
    // 读配置键名 -> 解析为扫描码 -> 无法识别则警告并改用默认键 -> 写入输入系统
    const auto bindAction =
        [this](const char* action, const char* configKey,
               const char* defaultKeyName, SDL_Scancode defaultKeyCode)
    {
        // 读取键名：缺键/空值由 getString 内部警告并返回默认键名（本处传入）
        const std::string keyName = m_config.getString(configKey, defaultKeyName);

        // 解析为 SDL 扫描码；BADKEY 之类的未知名走警告 + 默认回退分支
        const SDL_Scancode key = parseKeyName(keyName);
        if (key == SDL_SCANCODE_UNKNOWN)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "配置项 %s 的键名 \"%s\" 无法识别，回退默认键 %s",
                        configKey, keyName.c_str(), defaultKeyName);
            m_input.bind(action, defaultKeyCode); // 回退路径：用内置默认键
            return;
        }

        m_input.bind(action, key); // 正常路径：按配置键名解析出的按键绑定
    };

    // 移动动作（held 持续查询：按住即走，松开即停）
    bindAction("move_up", "bind_move_up", "W", SDL_SCANCODE_W);
    bindAction("move_down", "bind_move_down", "S", SDL_SCANCODE_S);
    bindAction("move_left", "bind_move_left", "A", SDL_SCANCODE_A);
    bindAction("move_right", "bind_move_right", "D", SDL_SCANCODE_D);

    // 说明：攻击动作（bind_attack）已随「自动战斗改版」移除——战斗改为
    // 自动索敌出手，不再需要玩家按攻击键，绑定表随之瘦身

    // 对局控制（pressed 边缘：暂停/重开/回菜单）
    bindAction("pause", "bind_pause", "ESC", SDL_SCANCODE_ESCAPE);
    bindAction("restart", "bind_restart", "R", SDL_SCANCODE_R);
    bindAction("menu", "bind_menu", "M", SDL_SCANCODE_M);

    // 菜单确认（pressed 边缘：菜单按确认进入对局）
    bindAction("confirm", "bind_confirm", "ENTER", SDL_SCANCODE_RETURN);

    // 小键盘回车：确认动作的内置别名（历史行为两个 Enter 都接受；
    // 它不占配置键位——八个 bind_* 配置保持最小集合，菜单查询时一并检查）
    m_input.bind("confirm_kp", SDL_SCANCODE_KP_ENTER);
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

    // 装配输入绑定：把 config 的 bind_* 键（缺键回退内置默认键、非法键名
    // 警告后回退）写入输入系统。必须放在配置加载之后、任何场景创建之前
    bindInputActions();

    // 窗口尺寸由配置决定（window_width / window_height，默认 1280x720）：
    // 改配置后重启即生效。极端非法尺寸（如 0）由 SDL_CreateWindow 的
    // 失败路径兜底，不会崩溃，只是初始化报错退出
    const int windowWidth = m_config.getInt("window_width", kDefaultWindowWidth);
    const int windowHeight = m_config.getInt("window_height", kDefaultWindowHeight);

    // 窗口由 GameDev 持有：主循环每帧查询它的尺寸装配视口信息。
    // 标题使用工作名「ROGUE ARENA」——占位、易改：与菜单标题保持一致，
    // 正式定名时两处字符串同步替换即可（纯 ASCII，不依赖 UTF-8 编码约定）
    m_window = SDL_CreateWindow(
        "ROGUE ARENA",
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
        //    事件按「渲染帧」粒度处理，本帧的所有固定步共享同一份键盘快照。
        //    动作边沿（Input::update）的采样时机在 update() 内、补步循环之前
        //    （输入采样与补步绑定，防边沿丢失——原因详见 update 内注释）
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
        // 输入采样（防边沿丢失的关键设计）：只在「本帧确实要补至少一步」时
        // 采样一次键盘，且紧邻补步循环之前。
        // 原因：Input 的 pressed/released 边沿只存活到下一次采样——若在一个
        // 没有补步的渲染帧里采样，边沿会在任何固定步看到它之前被下一帧的
        // 采样覆盖。高刷新率下（如 180Hz 显示、120Hz 固定步）多数帧不补步，
        // 键按下/松开会有相当比例随机丢失；采样与补步绑定后，边沿必然被
        // 同帧的固定步观察到（同一帧内补多步时共享同一边沿快照，场景侧
        // 用「本帧已处理」标记去重）。
        // held 状态不受影响：按住期间每个补步帧都会重新采样为按下
        m_input.update();

        // 每渲染帧只查一次窗口尺寸：本帧的所有固定步共享同一视口快照。
        // 视口尺寸是玩家移动边界与刷新范围的计算基准（窗口一变化，
        // 本帧逻辑立即跟随）
        int width = 0;
        int height = 0;

        // 查询窗口实际尺寸（整数像素）
        SDL_GetWindowSize(m_window, &width, &height);

        // 装配固定步的「世界上下文」：一次构造，本帧所有固定步复用。
        // deltaTime 恒为 kFixedDelta（固定步长，与真实帧率无关）。
        // input 指向本帧已采样的输入系统（边沿在整帧内可见）；
        // keyboard 是过渡期保留的键盘直读数组，新代码一律走 input 动作查询
        const UpdateContext fixedCtx{
            kFixedDelta,                   // 固定步长（秒）：逻辑与计时的唯一时间基准
            SDL_GetKeyboardState(nullptr), // SDL 键盘状态数组（过渡字段，待后续批次移除）
            static_cast<float>(width),     // 视口宽：移动边界 / 重生范围的计算基准
            static_cast<float>(height),    // 视口高：同上
            &m_input                       // 输入系统指针：动作查询入口（held/pressed）
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
