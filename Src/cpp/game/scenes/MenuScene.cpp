#include "game/scenes/MenuScene.h"
#include "game/scenes/PlayScene.h" // PlayScene：Enter 后要切换过去的对局场景
#include "engine/Input.h"          // Input：ctx.input->pressed 查询确认动作边缘
#include "engine/SceneManager.h"   // SceneManager：m_sceneManager->requestScene 需要完整类型
#include <SDL3_image/SDL_image.h>  // IMG_LoadTexture：加载 assets/background.png 背景图
#include <memory>                  // std::make_unique：构造 PlayScene
#include <string>                  // std::string：拼接素材与字体的绝对路径

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

// 进入场景：保存 renderer 与音效系统、加载背景图。
// config 由 SceneManager 注入，本阶段菜单没有可配置项，参数暂不使用
void MenuScene::onEnter(SDL_Renderer* renderer, const Config& /*config*/, AudioSystem& audio)
{
    // 1) 保存 renderer：后续渲染与资源加载都用它（只借用，不负责销毁）
    m_renderer = renderer;

    // 2) 保存音效系统指针：按 Enter 开始时用它播 start 音效。
    //    所有权在 GameDev，受 SceneManager 管理期间必然非空（同一管线的 Config 亦然）
    m_audio = &audio;

    // 冒烟观测点：日志确认场景切换实际发生（自动化测试据此断言流程）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "进入场景: Menu");

    // 3) 加载背景图（可选资源，与对局共用同一张 background.png）：
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

    // 4) 加载 UI 字体（assets/fonts/SmileySans-Oblique.ttf，中文开源字体）。
    //    路径拼装模式与背景图一致：以 exe 所在目录为基准。
    //    标题/提示各加载一份（字号不同）；Font::load 内部会记录失败警告，
    //    失败后对应文字安全跳过，菜单仍可进入对局，不阻塞游戏流程
    if (basePath != nullptr)
    {
        // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
        const std::string fontPath = std::string(basePath) + "assets/fonts/SmileySans-Oblique.ttf";
        // 两个字号彼此独立：一份加载失败不影响另一份
        m_titleFont.load(fontPath, kTitlePtSize); // 大号标题
        m_hintFont.load(fontPath, kHintPtSize);   // 提示文字
    }

    // 5) 读取最佳战绩（只读展示）：文件缺失/旧格式/损坏都按无记录处理并
    //    记录警告，本函数永不失败——菜单不会因为存档问题开不了局
    //   （容错契约见 SaveData.h；写盘只发生在对局侧的 GameOver 结算）
    m_best = SaveData::load();
}

// 每帧更新：查询确认动作的按下边缘（Enter -> 进入对局）。
// 迁移说明：原先走 SDL_EVENT_KEY_DOWN 事件，现改为经输入系统查询 pressed；
// 语义仍是「按一次进入一次」，且确认键走 config 的 bind_confirm 可改键
void MenuScene::update(const UpdateContext& ctx)
{
    // 确认动作 = 主键盘 Enter（bind_confirm）或内置别名小键盘 Enter；
    // ctx.input 为 nullptr（旧调用方未装配输入系统）时按「未按下」安全降级
    const bool confirmPressed =
        ctx.input != nullptr &&
        (ctx.input->pressed("confirm") || ctx.input->pressed("confirm_kp"));

    // 去重标记：同一帧补多个固定步时 pressed 恒 true，只在首次见到上升沿
    // 的那一步请求切换，避免同一帧构造多个 PlayScene
    if (confirmPressed && !m_confirmHandled)
    {
        // 播放下发确认音（start，上扬音）：先给耳朵一个"开局"信号，
        // 场景切换本身由帧末结算，听觉与画面各走各的时序互不阻塞。
        // play 在音效系统降级时是安全空操作（只记日志），不影响下面的切换
        m_audio->play(SoundId::Start);

        // 请求切换到对局场景：只登记，帧末由 SceneManager 统一结算。
        // 每次按 Enter 都构造一个新的 PlayScene —— 「每次进入 = 全新一局」
        m_sceneManager->requestScene(std::make_unique<PlayScene>());
    }
    m_confirmHandled = confirmPressed; // 记录本步采样：边沿消失前不再重复处理
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

    // 第一行：大号标题（金黄色）——工作名「ROGUE ARENA」为占位，改名成本
    // 极低：只改这一处字符串与 GameDev 的窗口标题即可
    SDL_SetRenderDrawColor(m_renderer, 255, 215, 0, 255);
    m_titleFont.drawCentered(m_renderer, "ROGUE ARENA", screenW / 2.0f, screenH * 0.30f);

    // 第二行：开始提示（白色，UTF-8 中文文案由 TTF 字体渲染）
    SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
    m_hintFont.drawCentered(m_renderer, "按 Enter 开始", screenW / 2.0f, screenH * 0.55f);

    // 第三行：操作说明（白色，与上一行同字号）——本批起战斗为自动攻击：
    // 无需攻击键，靠近怪物即自动出手（冷却门控见 PlayScene/Player）
    m_hintFont.drawCentered(m_renderer, "WASD 移动 / 自动攻击 / ESC 暂停", screenW / 2.0f, screenH * 0.68f);

    // 第四行：最佳战绩（柔和金灰色：与上方白色提示拉开层级）。
    // 无记录（三项全 0）：整行显示「暂无」；有记录：三项数值一行展示——
    // 数值来自 onEnter 读取的快照（上次对局结算写盘后的最新纪录）
    SDL_SetRenderDrawColor(m_renderer, 210, 190, 140, 255);
    if (m_best.empty())
    {
        m_hintFont.drawCentered(m_renderer, "最佳战绩: 暂无", screenW / 2.0f, screenH * 0.80f);
    }
    else
    {
        char recordText[96]; // 容纳三项纪录一行，绰绰有余
        SDL_snprintf(recordText, sizeof(recordText),
                     "最佳战绩: 击杀 %d · 等级 %d · 存活 %02d:%02d",
                     m_best.kills, m_best.level, m_best.seconds / 60, m_best.seconds % 60);
        m_hintFont.drawCentered(m_renderer, recordText, screenW / 2.0f, screenH * 0.80f);
    }
}
