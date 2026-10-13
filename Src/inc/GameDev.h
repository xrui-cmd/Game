// GameDev.h: 应用外壳——SDL 生命周期 + 主循环 + 场景转发

#ifndef __GAMEDEV_H__
#define __GAMEDEV_H__

#include <SDL3/SDL.h>             // SDL_Window / SDL_Renderer / SDL_Event：外壳的基础类型
#include <memory>                 // std::unique_ptr：SceneManager 的所有权管理
#include "ErrorCode.h"            // GameError：初始化/渲染失败的返回码
#include "engine/Config.h"        // Config：启动时加载的配置数据（成员类型）
#include "engine/SceneManager.h"  // SceneManager：当前场景的管理与转发

// ============================================================
// GameDev：应用外壳
// ============================================================
//
// 职责边界（本次改造起）：
//   - 持有窗口/渲染器，完成 SDL 初始化与销毁（原有职责保留）
//   - 启动时加载 config/game.ini（可选）：窗口尺寸由配置决定，
//     其余数值随 Config 注入场景使用
//   - 主循环：算 delta -> 事件（QUIT 在应用层处理）-> 转发给场景
//     -> 帧末结算场景切换
//   - 不再包含任何具体玩法（玩家/金币/计分/菜单/暂停均已迁入场景）
//
// 场景切换由 SceneManager 延迟结算；shutdown 时先 clear() 场景
// （纹理先于 renderer 释放），再按 Renderer -> Window -> SDL_Quit 逆序销毁
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
    void processEvents(); // 取出全部事件：QUIT 置停，其余转发给场景
    void update();        // 装配 UpdateContext（delta/键盘/视口）并转发
    GameError render();   // 转发场景绘制 + 提交画面
    void shutdown();      // 逆序释放：场景 -> renderer -> window -> SDL_Quit

private:
    // 配置项缺省值：与改造前的硬编码常量逐一相同。
    // 没有 config/game.ini 时，窗口尺寸等数值与改造前行为完全一致
    static constexpr int kDefaultWindowWidth = 1280;  // 默认窗口宽（像素）
    static constexpr int kDefaultWindowHeight = 720;  // 默认窗口高（像素）

    Config m_config; // 配置数据：initialize() 加载后只读；生命周期覆盖整个应用

    SDL_Window* m_window = nullptr;     // 窗口：主循环尺寸查询用（本类创建/销毁）
    SDL_Renderer* m_renderer = nullptr; // 渲染器：依赖窗口，创建顺序在窗口之后

    // 场景管理器：在 renderer 创建成功后构造（场景 onEnter 需要 renderer）
    std::unique_ptr<SceneManager> m_sceneManager;

    bool m_running = false;   // 主循环开关：收到 QUIT 事件置 false
    float m_deltaTime = 0.0f; // 本帧真实流逝秒数（主循环每帧计算）
    Uint64 m_lastTime = 0;    // 上一帧的时间戳（性能计数器读数）
};

#endif // !__GAMEDEV_H__
