// GameDev.h: 应用外壳——SDL 生命周期 + 主循环 + 场景转发

#ifndef __GAMEDEV_H__
#define __GAMEDEV_H__

#include <SDL3/SDL.h>             // SDL_Window / SDL_Renderer / SDL_Event：外壳的基础类型
#include <memory>                 // std::unique_ptr：SceneManager 的所有权管理
#include "ErrorCode.h"            // GameError：初始化/渲染失败的返回码
#include "engine/AudioSystem.h"   // AudioSystem：音效系统的所有权持有者（成员类型）
#include "engine/Config.h"        // Config：启动时加载的配置数据（成员类型）
#include "engine/Input.h"         // Input：键盘动作映射/边沿检测（主循环每帧采样）
#include "engine/SceneManager.h"  // SceneManager：当前场景的管理与转发

// ============================================================
// GameDev：应用外壳
// ============================================================
//
// 职责边界：
//   - 持有窗口/渲染器，完成 SDL 初始化与销毁（原有职责保留）
//   - 启动时加载 config/game.ini（可选）：窗口尺寸、vsync 由配置决定，
//     其余数值随 Config 注入场景使用
//   - 主循环：算真实帧间隔 -> 钳制 -> 事件（QUIT 在应用层处理）
//     -> 固定步长补步 -> 插值渲染 -> 帧末结算场景切换
//   - 不再包含任何具体玩法（玩家/战斗/菜单/暂停均已迁入场景）
//
// 固定时间步长 + 插值渲染（本次改造的核心）：
//   逻辑更新不再直接吃「真实帧间隔」，而是以 kFixedDelta（1/120 秒）的
//   固定步长推进：累加器 m_accumulator 攒够一步就调用一次场景 update；
//   渲染时把「剩余不足一步的余额」换算成 [0,1) 的插值系数 alpha，
//   转发给场景，由实体在「上一固定步位置 <-> 当前固定步位置」之间做
//   线性插值。收益：逻辑与计时和渲染帧率彻底解耦（不同刷新率下行为
//   一致）、倒计时按固定步精确走、高刷屏上画面平滑。
//
//   为什么选 1/120 而非 1/60：
//     - 1/60 步长：CPU 逻辑开销减半，但在 144/165/240 等非 60 整除的
//       屏幕上，每渲染帧补 0/1 步交替出现，alpha 规律抖动，快速移动
//       时可见轻微顿挫；
//     - 1/120 步长：与 60（每帧恰好 2 步）、120（每帧 1 步）整倍对齐，
//       144Hz 下也只有 1.2 步/帧，抖动幅度减半。代价是逻辑调用次数
//       翻倍——本项目实体稀少，开销可忽略，平滑收益更大。
//
// 防死亡螺旋（两道防线，量纲上恰好一致地对应 0.25 秒）：
//   机器太慢时，单帧要补的步数会越滚越多（补步耗时超过补步覆盖的
//   游戏时间），逻辑永远追不上真实时间，帧率雪崩——即「死亡螺旋」。
//   两道防线：
//     1. frameDelta 钳制在 kMaxFrameDelta（0.25 秒）：超长停顿（断点
//        调试、拖动窗口、系统休眠）最多按 0.25 秒处理；
//     2. 单帧补步数上限 kMaxStepsPerFrame（0.25s x 120Hz = 30 步）：
//        达到上限后剩余余额直接丢弃。
//   最坏结果是游戏逻辑降速运行（慢动作），绝不会滚雪球卡死。
//
// 场景切换由 SceneManager 延迟结算；shutdown 时先 clear() 场景
// （纹理先于 renderer 释放），再按依赖逆序释放：音效系统（先于 SDL_Quit，
// 其内部要关闭音频设备）-> Renderer -> Window -> SDL_Quit
// ============================================================

class GameDev
{
public:
    GameDev() = default; // 资源初始状态由成员默认值表达（全部为空）
    ~GameDev();          // 析构兜底调用 shutdown，main 不需要手动收尾

    // 初始化 SDL、窗口、渲染器，并进入初始场景
    GameError initialize();

    // 主循环：跑帧直到 QUIT 或渲染致命错误
    GameError run();

private:
    void processEvents();          // 取出全部事件：QUIT 置停，其余转发给场景
    void update(float frameDelta); // 余额累加 + 固定步长补步（一帧内可能多次调用场景 update）
    GameError render();            // 计算插值系数 -> 转发场景绘制 -> 提交画面
    void shutdown();               // 逆序释放：场景 -> renderer -> window -> SDL_Quit
    void bindInputActions();       // 装配输入绑定：config 的 bind_* 键 -> 语义动作（非法键名警告+默认回退）

private:
    // 配置项缺省值：与改造前的硬编码常量逐一相同。
    // 没有 config/game.ini 时，窗口尺寸等数值与改造前行为完全一致
    static constexpr int kDefaultWindowWidth = 1280;  // 默认窗口宽（像素）
    static constexpr int kDefaultWindowHeight = 720;  // 默认窗口高（像素）
    static constexpr int kDefaultVsync = 1;           // 默认开启垂直同步（1 开 / 0 关）

    // ---- 固定时间步长参数 ----
    static constexpr int kFixedStepsPerSecond = 120;                  // 固定逻辑步频（步/秒）
    static constexpr float kFixedDelta = 1.0f / kFixedStepsPerSecond; // 固定步长（秒/步）
    static constexpr float kMaxFrameDelta = 0.25f;                    // 单帧真实间隔上限（秒）：防螺旋第一道防线
    static constexpr int kMaxStepsPerFrame = 30;                      // 单帧补步上限：0.25s x 120Hz = 30 步（第二道防线）

    Config m_config; // 配置数据：initialize() 加载后只读；生命周期覆盖整个应用

    // 输入系统：语义动作（move_up/attack/pause 等）到物理按键的映射表 + 边沿状态。
    // initialize() 依据 config 的 bind_* 键完成绑定（见 bindInputActions）；
    // run() 每个渲染帧在事件处理之后调用 m_input.update() 采样一次键盘；
    // 上下文装配时以指针注入（UpdateContext::input），场景与实体经动作名
    // 查询 held/pressed，不再直接读取物理按键
    Input m_input;

    // 音效系统：所有权在本类，生命周期覆盖全部场景（场景只借用其指针）。
    // initialize() 里初始化；shutdown() 里在场景之后、renderer 之前销毁。
    // 任何环境下失败都只降级不中止（详见 AudioSystem.h 的失败降级契约）
    AudioSystem m_audio;

    SDL_Window* m_window = nullptr;     // 窗口：主循环尺寸查询用（本类创建/销毁）
    SDL_Renderer* m_renderer = nullptr; // 渲染器：依赖窗口，创建顺序在窗口之后

    // 场景管理器：在 renderer 创建成功后构造（场景 onEnter 需要 renderer
    // 与音效系统的注入引用，两者构造就绪后才可创建）
    std::unique_ptr<SceneManager> m_sceneManager;

    bool m_running = false; // 主循环开关：收到 QUIT 事件置 false
    Uint64 m_lastTime = 0;  // 上一渲染帧的时间戳（性能计数器读数）

    // 固定步长累加器（秒）：攒够 kFixedDelta 就推进一步逻辑。
    // 帧末的残留值（不足一步的部分）就是「距下一个固定步的进度」，
    // 渲染用它在两次固定步位置之间插值
    float m_accumulator = 0.0f;
};

#endif // !__GAMEDEV_H__
