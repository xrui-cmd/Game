#include "game/scenes/PlayScene.h"
#include "game/scenes/MenuScene.h" // MenuScene：GameOver 按 M 时要切换回主菜单
#include "engine/DebugText.h"      // DebugText：仅用于左下角 FPS/帧时间调试行（ASCII）
#include "engine/Rect.h"           // rectsOverlap：金币碰撞的 AABB 判定
#include "engine/SceneManager.h"   // SceneManager：m_sceneManager->requestScene 需要完整类型
#include <SDL3_image/SDL_image.h>  // IMG_LoadTexture：加载 assets/background.png 背景图
#include <algorithm>               // std::max：金币数量下限保护
#include <memory>                  // std::make_unique：构造 MenuScene
#include <string>                  // std::string：拼接素材与字体的绝对路径

// 析构：释放背景纹理。
// 关键时机：SceneManager 先 onExit + 析构旧场景、后销毁 renderer，
// 因此这里释放纹理时 renderer 尚在，安全；
// 玩家/金币的纹理由各自 Entity 的析构函数兜底释放
PlayScene::~PlayScene()
{
    // 只有纹理确实存在时才销毁，避免对空指针调用 SDL_DestroyTexture
    if (m_background != nullptr)
    {
        SDL_DestroyTexture(m_background); // 释放背景图占用的显存
        m_background = nullptr;           // 置空：即使重复调用也不会二次销毁
    }
}

// 进入场景：保存 renderer 与音效系统、读取配置、加载资源、布置初始局面。
// 每次进入都是新构造的 PlayScene 对象 => 天然是「全新一局」
void PlayScene::onEnter(SDL_Renderer* renderer, const Config& config, AudioSystem& audio)
{
    // 1) 保存 renderer：后续每帧渲染、资源加载都用它（只借用，不负责销毁）
    m_renderer = renderer;

    // 2) 保存音效系统指针：收币/结束/暂停三处事件用它触发播放。
    //    所有权在 GameDev，受 SceneManager 管理期间必然非空（与 Config 同一管线）
    m_audio = &audio;

    // 3) 由 renderer 反查窗口：玩法边界/金币重生需要窗口整数尺寸
    m_window = SDL_GetRenderWindow(renderer);

    // 4) 读取本场景相关的配置项。getFloat/getInt 在缺键或值非法时会记录
    //    警告并回退到内置默认值（与改造前的常量逐一相同），因此没有
    //    config/game.ini 时行为与改造前完全一致：
    //      round_seconds       本局时长（秒）
    //      coin_count          金币数量
    //      player_speed        玩家移动速度（像素/秒）
    //      player_accel        玩家加速度（像素/秒²）
    //      player_friction     玩家摩擦减速度（像素/秒²）
    //      player_anim_interval 玩家行走动画每帧时长（秒）
    //      coin_anim_interval  金币旋转动画每帧时长（秒）
    m_roundSeconds = config.getFloat("round_seconds", kDefaultRoundSeconds);
    m_timeLeft = m_roundSeconds; // 首局倒计时从满值开始（与按 R 重开保持一致）

    // 负数金币数会被 resize 当作巨大的长度值（size_t 回绕）引发异常，
    // 所以先用 std::max 夹到 0 以上；0 枚金币（空场）是允许的合法配置
    const int coinCount = std::max(0, config.getInt("coin_count", kDefaultCoinCount));

    // 玩家与金币的动画节奏、玩家速度与加减速：先读出配置值（默认值与
    // Player/Coin 各自的默认常量一致），等到相应实体 load 完成后再注入
    const float playerSpeed = config.getFloat("player_speed", Player::kDefaultSpeed);
    const float playerAccel = config.getFloat("player_accel", Player::kDefaultAccel);
    const float playerFriction = config.getFloat("player_friction", Player::kDefaultFriction);
    const float playerAnimInterval =
        config.getFloat("player_anim_interval", Player::kDefaultFrameTime);
    const float coinAnimInterval = config.getFloat("coin_anim_interval", Coin::kDefaultFrameTime);

    // 冒烟观测点：日志确认场景切换实际发生（自动化测试据此断言流程）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "进入场景: Play");

    // 5) 加载背景图（可选资源）：拼出 <exe目录>/assets/background.png 交给 SDL3_image 解码。
    //    背景不是游戏必需品——加载失败只记录警告并继续；渲染阶段会整块跳过背景，
    //    露出清屏深蓝色兜底，玩法完全不受影响
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

    // 6) 加载 UI 字体（assets/fonts/SmileySans-Oblique.ttf，中文开源字体）。
    //    路径拼装模式与背景图一致：以 exe 所在目录为基准。
    //    HUD 与标题各加载一份（字号不同）；Font::load 内部会记录失败警告，
    //    失败后对应文字安全跳过，玩法完全不受影响
    if (basePath != nullptr)
    {
        // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
        const std::string fontPath = std::string(basePath) + "assets/fonts/SmileySans-Oblique.ttf";
        // 两个字号彼此独立：一份加载失败不影响另一份
        m_hudFont.load(fontPath, kHudPtSize); // HUD/提示文字
        m_bigFont.load(fontPath, kBigPtSize); // 暂停/结束标题
    }

    // 7) 玩家纹理：三级回退（精灵表 -> 单帧图 -> 程序化纯色）在 Entity 内完成，
    //    这里失败只可能是最后的程序化兜底也失败（内存不足等极端情况）
    if (!m_player.load(m_renderer))
    {
        // 纹理创建失败：记录错误但不中断（该实体渲染时跳过，游戏不至于崩溃）
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "创建玩家纹理失败：%s", SDL_GetError());
    }

    // 8) 注入玩家配置：速度、加减速与行走动画节奏。
    //    必须在 load 之后设置——load 会把内置默认帧时长先写进动画数据，
    //    配置值晚一步落定才能覆盖它（setFrameTime 对非正值会忽略并警告；
    //    setMotion 对负值整组忽略，0 合法）
    m_player.setSpeed(playerSpeed);
    m_player.setMotion(playerAccel, playerFriction);
    m_player.setFrameTime(playerAnimInterval);

    // 9) 查询窗口实际尺寸并缓存：金币出生和后续边界计算都基于它
    SDL_GetWindowSize(m_window, &m_width, &m_height);

    // 10) 玩家归位到窗口中心：坐标指左上角，所以要减去自身宽高的一半。
    //    每次进入 PlayScene 都从这里开始 => 与「按 R 重开」的初始状态一致
    m_player.reset(m_width / 2.0f - m_player.width() / 2.0f,
                   m_height / 2.0f - m_player.height() / 2.0f);

    // 11) 预创建全部金币对象；resize 按配置数量（默认 5）构造对象，此时纹理还是空的
    m_coins.resize(static_cast<size_t>(coinCount));

    // 玩家当前矩形：金币出生点尽量避开玩家所站的位置
    const SDL_FRect playerRect = m_player.rect();

    for (Coin& coin : m_coins)
    {
        // 为每个金币创建纹理（同样是三级回退），失败只记录不中断
        if (!coin.load(m_renderer))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "创建金币纹理失败：%s", SDL_GetError());
        }

        // 注入金币动画节奏（同样在 load 之后，覆盖内置默认帧时长）
        coin.setFrameTime(coinAnimInterval);

        // 立即给一个随机出生点，避免所有金币叠在 (0,0)
        coin.respawn(m_width, m_height, playerRect);
    }
}

// 事件处理：ESC 暂停/恢复、GameOver 下按 R 重开、Paused/GameOver 下按 M 回主菜单。
// 用「按键按下」事件而不是每帧轮询键盘状态，天然保证按一次只触发一次
void PlayScene::handleEvent(const SDL_Event& event)
{
    // 只关心按键按下事件；其余事件一律忽略
    if (event.type != SDL_EVENT_KEY_DOWN)
    {
        return;
    }

    // ESC：在 Playing 与 Paused 之间切换（GameOver 下不响应）
    if (event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        if (m_state == PlayState::Playing)
        {
            // 进入暂停：世界与倒计时从此冻结（update 对非 Playing 直接返回）
            m_state = PlayState::Paused;
            // 播放暂停轻音（pause）：与恢复共用同一个音效，切换动作的听觉确认
            m_audio->play(SoundId::Pause);
            // 冒烟观测点：确认暂停触发（自动化测试据此断言）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "对局暂停（ESC 恢复，M 回菜单）");
        }
        else if (m_state == PlayState::Paused)
        {
            // 从暂停处继续：玩家/金币/倒计时的状态原样保留，继续推进
            m_state = PlayState::Playing;
            // 播放恢复轻音（同样是 pause 音效：暂停/恢复切换共用）
            m_audio->play(SoundId::Pause);
            // 冒烟观测点：确认恢复触发（自动化测试据此断言）
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "对局恢复");
        }
        return; // ESC 已处理：直接返回，避免落入下面的按键分支
    }

    // F3：切换左下角 FPS/帧时间显示（任何对局状态下都响应）。
    // 放在 ESC 之后、其他分支之前：它不影响对局状态，只控制显示
    if (event.key.scancode == SDL_SCANCODE_F3)
    {
        m_fpsVisible = !m_fpsVisible; // 开 <-> 关
        // 冒烟观测点：日志确认切换实际发生（自动化测试据此断言）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "帧率显示: %s", m_fpsVisible ? "开" : "关");
        return; // F3 已处理：直接返回，避免落入下面的按键分支
    }

    // GameOver 下按 R：重置整局数据并切回 Playing
    if (event.key.scancode == SDL_SCANCODE_R && m_state == PlayState::GameOver)
    {
        restart(); // 重开一局
    }

    // Paused 与 GameOver 下按 M：回主菜单（Playing 下不响应）。
    // 请求只登记，帧末由 SceneManager 结算，避免在自身回调里析构自己
    if (event.key.scancode == SDL_SCANCODE_M &&
        (m_state == PlayState::Paused || m_state == PlayState::GameOver))
    {
        m_sceneManager->requestScene(std::make_unique<MenuScene>());
    }
}

// 每帧更新：只有 Playing 状态才推进游戏逻辑。
// Paused（暂停）与 GameOver（结束）都冻结玩家、倒计时与金币——暂停由此
// 天然实现了「世界与计时冻结」
void PlayScene::update(const UpdateContext& ctx)
{
    // 非 Playing 状态：世界冻结。
    if (m_state != PlayState::Playing)
    {
        // 冻结期间的插值自查（重要）：把各实体的插值快照对齐到当前位置。
        // 若不对齐，最后一次 update 留下的 prev->cur 区间会随渲染帧的
        // alpha 波动来回插值，暂停画面上会出现亚像素级漂移/抖动；
        // 对齐后 prev==cur，无论 alpha 取何值，渲染位置恒等于当前位置
        m_player.snapshotPreviousPosition(); // 玩家：prev=cur
        for (Coin& coin : m_coins)
        {
            coin.snapshotPreviousPosition(); // 金币：逐枚对齐
        }
        return;
    }

    // 每帧查询窗口实际尺寸并缓存：玩家边界、金币重生范围都基于它，
    // 窗口尺寸一旦变化，本帧逻辑立即跟随（与改造前行为一致）
    SDL_GetWindowSize(m_window, &m_width, &m_height);

    // 玩家使用上下文更新：内部按 ctx.keyboard 移动、按 ctx.viewport* 做边界 clamp
    m_player.update(ctx);

    // 玩家矩形只取一次：本帧所有金币都对它做碰撞检测
    const SDL_FRect playerRect = m_player.rect();

    // 逐个检测金币与玩家是否重叠：重叠即视为「收集成功」
    for (Coin& coin : m_coins)
    {
        // 推进本枚金币的旋转动画（纯画面表现，不影响 24x24 碰撞盒）。
        // 走的是 Entity 默认 update：无条件累加动画时间轴，与改造前一致
        coin.update(ctx);

        // AABB 相交检测；返回 true 说明玩家和这枚金币碰到了
        if (rectsOverlap(playerRect, coin.rect()))
        {
            m_score += 1; // 每枚金币计 1 分，累加到总分里

            // 播放收币短促「叮」（coin）：每次收集都触发；
            // 同一音效连续触发时轨道从头重播，连收多枚时的听感自然衔接
            m_audio->play(SoundId::Coin);

            // 立刻把这枚金币传送到新的随机位置（随机范围跟随窗口实际尺寸）
            coin.respawn(m_width, m_height, playerRect);
        }
    }

    // 倒计时按真实流逝时间递减（delta 单位是秒，与 30 秒常量同量纲）
    m_timeLeft -= ctx.deltaTime;

    // 时间耗尽：进入 GameOver 状态
    if (m_timeLeft <= 0.0f)
    {
        m_timeLeft = 0.0f;             // 夹到 0，避免 HUD 显示 -0.1 这类负数
        m_state = PlayState::GameOver; // 冻结世界，等待玩家按 R 重开
        // 播放结束下行音（gameover）：进入结束状态的唯一入口在这一处，
        // 音效天然与状态切换一一对应（R 重开不会重复触发）
        m_audio->play(SoundId::GameOver);
        // 冒烟观测点：日志标记本局结束（自动化测试据此确认 GameOver 已到达）
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "倒计时归零，本局结束");
    }
}

// 重开一局：把整局相关的数据全部恢复到初始状态
void PlayScene::restart()
{
    // 分数清零，倒计时回到满值（局时长用 onEnter 时读到的配置值）
    m_score = 0;
    m_timeLeft = m_roundSeconds;

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
    m_state = PlayState::Playing;

    // 冒烟观测点：确认 R 重开真正触发（自动化测试据此断言）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "重开一局");
}

// 每帧渲染：清屏 -> 背景 -> 玩家 -> 金币 -> HUD。
// 不调用 SDL_RenderPresent（提交由应用外壳统一负责）。
// alpha 是插值系数，原样传给各实体——它们据此在上一固定步位置与当前
// 固定步位置之间平滑绘制
void PlayScene::render(float alpha)
{
    // 采样本渲染帧的间隔并更新 FPS 平滑值（只统计，不影响绘制内容）
    sampleFrameTime();

    // 设置清屏颜色。深蓝是背景图缺失时的兜底底色
    if (!SDL_SetRenderDrawColor(m_renderer, 20, 30, 50, 255))
    {
        // 设置颜色失败不影响继续绘制：记录后继续（最终由提交环节兜底判定）
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "设置绘制颜色失败：%s", SDL_GetError());
    }

    // 清除上一帧内容，避免旧画面残留
    if (!SDL_RenderClear(m_renderer))
    {
        // 清屏失败同样只记录：避免单帧故障中断整局游戏
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "清屏失败：%s", SDL_GetError());
    }

    // 背景作为最底层画面绘制：目标矩形 = 渲染输出实际尺寸，保持尺寸无关——
    // 无论窗口多大，背景总是铺满。纹理缺失（加载失败）时整块跳过，
    // 清屏的深蓝色即为兜底背景
    if (m_background != nullptr)
    {
        // 查询渲染输出尺寸，得到背景目标矩形
        int outW = 0;
        int outH = 0;
        SDL_GetRenderOutputSize(m_renderer, &outW, &outH);
        // srcRect 传 nullptr 表示使用整张背景纹理
        const SDL_FRect bgDst{ 0.0f, 0.0f,
                               static_cast<float>(outW), static_cast<float>(outH) };
        SDL_RenderTexture(m_renderer, m_background, nullptr, &bgDst);
    }

    // 玩家先画、金币后画（金币叠在玩家上的观感与改造前一致）。
    // 两者都按插值位置绘制：逻辑 120Hz 的离散位移在渲染帧上平滑呈现
    m_player.render(m_renderer, alpha);

    // 遍历绘制所有金币；数量少，直接循环即可
    for (const Coin& coin : m_coins)
    {
        coin.render(m_renderer, alpha); // 每个金币按插值位置画到屏幕上
    }

    // 游戏世界画完后再叠加 HUD 文字（分数/倒计时/暂停叠层/结束界面）
    renderHud();
}

// 绘制 HUD 文字（分数、倒计时、暂停叠层、结束界面）。
// 游戏文案统一走 Font（TTF 中文字体）：传入屏幕像素坐标，文字颜色由调用
// 前的 SDL_SetRenderDrawColor 决定；左下角 FPS 调试行例外，仍用 DebugText
// （SDL 内置调试字体，仅 ASCII，观感与改造前一致）
void PlayScene::renderHud()
{
    // 查询渲染器输出尺寸（物理像素）。HUD 的右上角对齐等定位
    // 都基于这个实际尺寸，而不是把 1280x720 硬编码进来
    int outW = 0;
    int outH = 0;
    SDL_GetRenderOutputSize(m_renderer, &outW, &outH);

    // 换成浮点别名：后续所有文字定位都用「屏幕像素」表达
    const float screenW = static_cast<float>(outW); // 屏幕宽（像素）
    const float screenH = static_cast<float>(outH); // 屏幕高（像素）

    // Paused / GameOver 叠层加半透明暗化遮罩：把游戏世界（背景/玩家/金币）
    // 整体压暗，让叠层文字从画面中浮出来。绘制顺序：遮罩放在最前——
    // 之后绘制的 FPS 行、分数/时间与叠层文字都压在遮罩之上，保持全亮
    if (m_state == PlayState::Paused || m_state == PlayState::GameOver)
    {
        // 打开混合模式：颜色按 alpha 与画面混合（默认模式会直接覆盖）
        if (!SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND))
        {
            // 设置失败只记录：遮罩退化为不透明填充也在可接受范围
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "设置混合模式失败：%s", SDL_GetError());
        }
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 140); // 半透明黑：保留世界轮廓

        // 目标矩形传 nullptr：填充整个渲染目标（全屏遮罩）
        if (!SDL_RenderFillRect(m_renderer, nullptr))
        {
            // 绘制失败只记录：遮罩是纯装饰，缺失不影响玩法
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "绘制遮罩失败：%s", SDL_GetError());
        }

        // 恢复默认混合模式：避免后续绘制（及下一帧世界绘制）被混合模式影响
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    }

    // ---- 左下角：FPS / 帧时间（灰白色小字，F3 控制显隐）----
    // 放在状态分支之前：无论 Playing/Paused/GameOver 都显示，方便对比
    // 三种状态下渲染性能的差异（数值来自 EMA，会逐帧连续变化）。
    // 这行是调试信息而非游戏 UI 文案，保持 SDL 内置调试字体（ASCII）
    if (m_fpsVisible)
    {
        // 灰白色：不抢分数/时间的视觉焦点
        SDL_SetRenderDrawColor(m_renderer, 180, 180, 180, 255);

        // 左对齐 x=20（与分数行同一条左边线）；文字高 = 8 逻辑像素 x
        // kFpsScale = 16 屏幕像素，再留 24 像素底边距 => y = 屏高 - 40
        const float fpsY = screenH - 40.0f;

        if (m_hasFrameSample)
        {
            // FPS 与帧时间同源换算（FPS = 1000 / 毫秒），两者必然自洽
            const float fps = 1000.0f / m_smoothedFrameMs;
            DebugText::drawFormat(m_renderer, kFpsScale, 20.0f, fpsY,
                                  "FPS: %.1f | %.1f ms", fps, m_smoothedFrameMs);
        }
        else
        {
            // 首帧还没有有效间隔：显示占位符，下一帧起给出真实数值
            DebugText::drawFormat(m_renderer, kFpsScale, 20.0f, fpsY,
                                  "FPS: -- | -- ms");
        }
    }

    // Playing 与 Paused 都显示分数/倒计时：
    // 暂停时保留完整 HUD，玩家能看清冻结时刻的分数与剩余时间
    if (m_state == PlayState::Playing || m_state == PlayState::Paused)
    {
        // 左上角：当前分数（白色）。位置沿用旧版屏幕坐标 (20, 20)
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        char scoreText[32]; // 容纳 "分数: " + 最长数字，绰绰有余
        SDL_snprintf(scoreText, sizeof(scoreText), "分数: %d", m_score);
        m_hudFont.draw(m_renderer, scoreText, 20.0f, 20.0f);

        // 右上角：剩余时间（黄色）。右对齐：先实测文本宽度再反推 x
        // （中文字体数字宽度不定，不能按字符数估算）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 0, 255);
        char timeText[32]; // 容纳 "时间: " + "xx.x"，绰绰有余
        SDL_snprintf(timeText, sizeof(timeText), "时间: %.1f", m_timeLeft);
        const SDL_FPoint timeSize = m_hudFont.measure(timeText); // 实测文本尺寸
        m_hudFont.draw(m_renderer, timeText, screenW - timeSize.x - 20.0f, 20.0f);
    }

    if (m_state == PlayState::Paused)
    {
        // 暂停叠层：大号「暂停」+ 两条操作提示。
        // 世界画面保持冻结在叠层之下（update 不推进，render 每帧重画同一画面）

        // 水平中心：叠层文字共用的对齐基准
        const float screenCenterX = screenW / 2.0f;

        // 大号「暂停」（黄色，kBigPtSize 字号，强调暂停状态）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 0, 255);
        m_bigFont.drawCentered(m_renderer, "暂停", screenCenterX, screenH * 0.38f);

        // 恢复提示（白色，常规 HUD 字号）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        m_hudFont.drawCentered(m_renderer, "按 ESC 继续", screenCenterX, screenH * 0.56f);

        // 回菜单提示（白色，常规 HUD 字号）
        m_hudFont.drawCentered(m_renderer, "按 M 返回菜单", screenCenterX, screenH * 0.66f);
    }
    else if (m_state == PlayState::GameOver)
    {
        // GameOver 界面：四行文字全部水平居中、围绕窗口中心排布。
        // 标题行用大字号（kBigPtSize），行距据此重新排布

        // 水平中心：四行文字共用的对齐基准（垂直位置逐行下移）
        const float screenCenterX = screenW / 2.0f;

        // 第一行：游戏结束（白色大号标题）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        m_bigFont.drawCentered(m_renderer, "游戏结束", screenCenterX, screenH / 2.0f - 110.0f);

        // 第二行：最终得分（黄色）。分数位数不定，先格式化到缓冲区，
        // 由 drawCentered 实测宽度实现居中
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 0, 255);
        char scoreText[32]; // 容纳 "最终分数: " + 最长数字，绰绰有余
        SDL_snprintf(scoreText, sizeof(scoreText), "最终分数: %d", m_score);
        m_hudFont.drawCentered(m_renderer, scoreText, screenCenterX, screenH / 2.0f - 10.0f);

        // 第三行：重开提示（白色）
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        m_hudFont.drawCentered(m_renderer, "按 R 重新开始", screenCenterX, screenH / 2.0f + 60.0f);

        // 第四行：回菜单提示（白色）。行距与上一行保持一致
        m_hudFont.drawCentered(m_renderer, "按 M 返回菜单", screenCenterX, screenH / 2.0f + 120.0f);
    }
}

// 采样渲染帧间隔并做 EMA 平滑：FPS 显示的数据来源。
// 与固定步长逻辑完全无关——它统计的是「渲染帧」的频率，
// 因此放在 render 里（每渲染帧一次），而不是 update 里（每固定步一次）
void PlayScene::sampleFrameTime()
{
    const Uint64 now = SDL_GetPerformanceCounter(); // 本帧时刻（性能计数器）

    // 第一次调用没有「上一帧」可参照：只记录时刻，间隔从下一次开始算
    if (m_lastFrameCounter != 0)
    {
        // 渲染帧间隔（毫秒）= 计数器差值 / 频率 x 1000
        const float frameMs = static_cast<float>(now - m_lastFrameCounter) * 1000.0f /
                              static_cast<float>(SDL_GetPerformanceFrequency());

        if (!m_hasFrameSample)
        {
            // 首个有效间隔直接作为初值：避免从 0 缓慢爬升，显示假数字
            m_smoothedFrameMs = frameMs;
            m_hasFrameSample = true; // 之后所有采样都走 EMA 分支
        }
        else
        {
            // 指数滑动平均（EMA）：新值权重 0.05 —— 约等于最近 20 帧的
            // 滑动窗口（60fps 下约 0.33 秒）。选它而非「固定 1 秒窗口」：
            // 数字连续不跳变，且一行公式完成，无需时间桶与计数
            constexpr float kEmaWeight = 0.05f;
            m_smoothedFrameMs = m_smoothedFrameMs * (1.0f - kEmaWeight) + frameMs * kEmaWeight;
        }
    }

    m_lastFrameCounter = now; // 记录本帧时刻，供下一次调用计算间隔
}
